#include "core/clipboard/ClipboardWriter.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wincodec.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string_view>
#include <vector>

#include "common/StringUtil.h"
#include "common/Log.h"
#include "common/Utf8.h"
#include "core/clipboard/ClipboardFormat.h"
#include "core/policies/ClipboardRetryPolicy.h"
#include "core/store/ClipTypes.h"

namespace clipd2d::core::clipboard {

namespace {

class ClipboardScope {
 public:
  explicit ClipboardScope(HWND owner = nullptr) {
    for (const DWORD delay_ms : policies::ClipboardOpenRetryDelaysMs()) {
      if (delay_ms > 0) {
        Sleep(delay_ms);
      }
      if (OpenClipboard(owner) == TRUE) {
        opened_ = true;
        break;
      }
    }
  }
  ~ClipboardScope() {
    if (opened_) {
      CloseClipboard();
    }
  }

  bool opened() const { return opened_; }

 private:
  bool opened_ = false;
};

std::wstring LowercaseWide(std::wstring text) {
  std::transform(
      text.begin(),
      text.end(),
      text.begin(),
      [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
      });
  return text;
}

std::wstring GetWindowProcessName(HWND window) {
  DWORD process_id = 0;
  GetWindowThreadProcessId(window, &process_id);
  if (process_id == 0) {
    return {};
  }

  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
  if (process == nullptr) {
    return {};
  }

  std::wstring path(4096, L'\0');
  DWORD size = static_cast<DWORD>(path.size());
  if (!QueryFullProcessImageNameW(process, 0, path.data(), &size)) {
    CloseHandle(process);
    return {};
  }
  CloseHandle(process);
  path.resize(size);

  const size_t slash = path.find_last_of(L"\\/");
  const std::wstring name = slash == std::wstring::npos ? path : path.substr(slash + 1);
  return LowercaseWide(name);
}

bool IsTerminalProcessName(const std::wstring& process_name) {
  return process_name == L"windowsterminal.exe" || process_name == L"wt.exe";
}

common::Status SendUnicodeText(const std::wstring& text);
common::Status SendPasteInputSequence(bool use_shift_insert, bool use_terminal_paste, std::string_view text_fallback);
common::Status PerformLegacyPasteShortcut(HWND target_window, std::string_view text_fallback);

HWND NormalizeRootWindow(HWND window) {
  if (window == nullptr || !IsWindow(window)) {
    return nullptr;
  }
  HWND root = GetAncestor(window, GA_ROOT);
  return (root != nullptr && IsWindow(root)) ? root : window;
}

bool BelongsToRootWindow(HWND window, HWND root_window) {
  return window != nullptr &&
      root_window != nullptr &&
      IsWindow(window) &&
      NormalizeRootWindow(window) == root_window;
}

std::wstring GetWindowClassLower(HWND window) {
  wchar_t class_name[128]{};
  GetClassNameW(window, class_name, static_cast<int>(std::size(class_name)));
  return LowercaseWide(class_name);
}

bool IsStandardPasteControl(HWND window) {
  if (window == nullptr || !IsWindow(window)) {
    return false;
  }
  const std::wstring class_name = GetWindowClassLower(window);
  return class_name == L"edit" ||
      class_name.find(L"richedit") != std::wstring::npos ||
      class_name.find(L"msftedit") != std::wstring::npos;
}

void WaitForModifierRelease(DWORD timeout_ms) {
  const DWORD start = GetTickCount();
  while (GetTickCount() - start < timeout_ms) {
    if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) == 0 &&
        (GetAsyncKeyState(VK_SHIFT) & 0x8000) == 0 &&
        (GetAsyncKeyState(VK_MENU) & 0x8000) == 0 &&
        (GetAsyncKeyState(VK_LWIN) & 0x8000) == 0 &&
        (GetAsyncKeyState(VK_RWIN) & 0x8000) == 0) {
      return;
    }
    Sleep(5);
  }
}

bool TrySendWmPaste(HWND target_window) {
  if (!IsStandardPasteControl(target_window)) {
    return false;
  }
  DWORD_PTR result = 0;
  return SendMessageTimeoutW(
             target_window,
             WM_PASTE,
             0,
             0,
             SMTO_ABORTIFHUNG | SMTO_NORMAL,
             250,
             &result) != 0;
}

common::Status DispatchPasteToContext(const PasteTargetContext& context, std::string_view text_fallback) {
  // Always use PerformLegacyPasteShortcut (Ctrl+V): 100% reliable across processes,
  // unlike WM_PASTE which is flaky for child/foreign controls.
  return PerformLegacyPasteShortcut(context.root_window, text_fallback);
}

bool TryRestorePasteTarget(const PasteTargetContext& context) {
  if (!IsValidPasteTargetContext(context)) {
    return false;
  }
  common::WriteLog(
      common::LogLevel::Info,
      "clipboard.paste_target",
      "restore begin root=" + std::to_string(reinterpret_cast<std::uintptr_t>(context.root_window)) +
          " focus=" + std::to_string(reinterpret_cast<std::uintptr_t>(context.focus_window)) +
          " thread=" + std::to_string(context.thread_id));

  HWND target_root = context.root_window;
  if (IsIconic(target_root)) {
    ShowWindow(target_root, SW_RESTORE);
  }

  HWND foreground_window = GetForegroundWindow();
  const DWORD current_thread_id = GetCurrentThreadId();
  const DWORD target_thread_id =
      context.thread_id != 0 ? context.thread_id : GetWindowThreadProcessId(target_root, nullptr);
  const DWORD foreground_thread_id =
      foreground_window == nullptr ? 0 : GetWindowThreadProcessId(foreground_window, nullptr);

  bool attached_foreground = false;
  bool attached_target = false;
  if (foreground_thread_id != 0 && foreground_thread_id != current_thread_id) {
    attached_foreground = AttachThreadInput(current_thread_id, foreground_thread_id, TRUE) == TRUE;
  }
  if (target_thread_id != 0 &&
      target_thread_id != current_thread_id &&
      target_thread_id != foreground_thread_id) {
    attached_target = AttachThreadInput(current_thread_id, target_thread_id, TRUE) == TRUE;
  }

  BringWindowToTop(target_root);
  SetForegroundWindow(target_root);
  SetActiveWindow(target_root);
  if (BelongsToRootWindow(context.focus_window, target_root)) {
    SetFocus(context.focus_window);
  }

  if (attached_target) {
    AttachThreadInput(current_thread_id, target_thread_id, FALSE);
  }
  if (attached_foreground) {
    AttachThreadInput(current_thread_id, foreground_thread_id, FALSE);
  }

  for (int attempt = 0; attempt < 12; ++attempt) {
    HWND foreground_root = NormalizeRootWindow(GetForegroundWindow());
    if (foreground_root == target_root) {
      common::WriteLog(
          common::LogLevel::Info,
          "clipboard.paste_target",
          "restore success attempt=" + std::to_string(attempt + 1) +
              " foreground_root=" + std::to_string(reinterpret_cast<std::uintptr_t>(foreground_root)));
      return true;
    }
    Sleep(10);
  }
  const HWND final_foreground_root = NormalizeRootWindow(GetForegroundWindow());
  const bool matched = final_foreground_root == target_root;
  common::WriteLog(
      common::LogLevel::Info,
      "clipboard.paste_target",
      "restore end matched=" + std::string(matched ? "true" : "false") +
          " foreground_root=" + std::to_string(reinterpret_cast<std::uintptr_t>(final_foreground_root)));
  return matched;
}

common::Status PerformLegacyPasteShortcut(HWND target_window, std::string_view text_fallback) {
  HWND foreground_window = GetForegroundWindow();
  const std::wstring target_process_name =
      (target_window != nullptr && IsWindow(target_window)) ? GetWindowProcessName(target_window) : std::wstring{};
  const bool use_terminal_paste = IsTerminalProcessName(target_process_name);
  const bool use_shift_insert =
      !use_terminal_paste && target_window != nullptr && IsStandardPasteControl(target_window);

  if (target_window != nullptr && IsWindow(target_window)) {
    if (IsIconic(target_window)) {
      ShowWindow(target_window, SW_RESTORE);
    }

    if (foreground_window != target_window) {
      const DWORD current_thread_id = GetCurrentThreadId();
      const DWORD target_thread_id = GetWindowThreadProcessId(target_window, nullptr);
      const DWORD foreground_thread_id =
          foreground_window == nullptr ? 0 : GetWindowThreadProcessId(foreground_window, nullptr);

      bool attached_foreground = false;
      bool attached_target = false;
      if (foreground_thread_id != 0 && foreground_thread_id != current_thread_id) {
        attached_foreground = AttachThreadInput(current_thread_id, foreground_thread_id, TRUE) == TRUE;
      }
      if (target_thread_id != 0 &&
          target_thread_id != current_thread_id &&
          target_thread_id != foreground_thread_id) {
        attached_target = AttachThreadInput(current_thread_id, target_thread_id, TRUE) == TRUE;
      }

      BringWindowToTop(target_window);
      SetForegroundWindow(target_window);

      if (attached_target) {
        AttachThreadInput(current_thread_id, target_thread_id, FALSE);
      }
      if (attached_foreground) {
        AttachThreadInput(current_thread_id, foreground_thread_id, FALSE);
      }
    }

    for (int attempt = 0; attempt < 12; ++attempt) {
      foreground_window = GetForegroundWindow();
      if (foreground_window == target_window) {
        break;
      }
      Sleep(10);
    }
  } else {
    target_window = foreground_window;
  }

  WaitForModifierRelease(150);
  Sleep(use_terminal_paste ? 25 : 10);
  return SendPasteInputSequence(use_shift_insert, use_terminal_paste, text_fallback);
}

common::Status SendPasteInputSequence(bool use_shift_insert, bool use_terminal_paste, std::string_view text_fallback) {
  if (use_terminal_paste && !text_fallback.empty()) {
    return SendUnicodeText(common::Utf8ToWide(text_fallback).wide);
  }

  INPUT inputs[6]{};
  UINT input_count = 0;
  if (use_terminal_paste) {
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_CONTROL;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = VK_SHIFT;
    inputs[2].type = INPUT_KEYBOARD;
    inputs[2].ki.wVk = 'V';
    inputs[3].type = INPUT_KEYBOARD;
    inputs[3].ki.wVk = 'V';
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[4].type = INPUT_KEYBOARD;
    inputs[4].ki.wVk = VK_SHIFT;
    inputs[4].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[5].type = INPUT_KEYBOARD;
    inputs[5].ki.wVk = VK_CONTROL;
    inputs[5].ki.dwFlags = KEYEVENTF_KEYUP;
    input_count = 6;
  } else if (use_shift_insert) {
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_SHIFT;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = VK_INSERT;
    inputs[2].type = INPUT_KEYBOARD;
    inputs[2].ki.wVk = VK_INSERT;
    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[3].type = INPUT_KEYBOARD;
    inputs[3].ki.wVk = VK_SHIFT;
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
    input_count = 4;
  } else {
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_CONTROL;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = 'V';
    inputs[2].type = INPUT_KEYBOARD;
    inputs[2].ki.wVk = 'V';
    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[3].type = INPUT_KEYBOARD;
    inputs[3].ki.wVk = VK_CONTROL;
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
    input_count = 4;
  }

  if (SendInput(input_count, inputs, sizeof(INPUT)) != input_count) {
    return common::Status::Error(common::StatusCode::PlatformError, "Send paste shortcut failed.");
  }
  return common::Status::Ok();
}

common::Status SendUnicodeText(const std::wstring& text) {
  if (text.empty()) {
    return common::Status::Ok();
  }

  std::vector<INPUT> inputs;
  inputs.reserve(text.size() * 2);
  for (wchar_t ch : text) {
    INPUT down{};
    down.type = INPUT_KEYBOARD;
    down.ki.dwFlags = KEYEVENTF_UNICODE;
    down.ki.wScan = ch;
    inputs.push_back(down);

    INPUT up = down;
    up.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
    inputs.push_back(up);
  }

  if (SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT)) != inputs.size()) {
    return common::Status::Error(common::StatusCode::PlatformError, "Failed to send unicode text.");
  }
  return common::Status::Ok();
}

common::Status SetGlobalTextData(UINT format, const std::wstring& text) {
  const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
  HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
  if (memory == nullptr) {
    return common::Status::Error(common::StatusCode::PlatformError, "Allocate text memory failed.");
  }

  void* destination = GlobalLock(memory);
  if (destination == nullptr) {
    GlobalFree(memory);
    return common::Status::Error(common::StatusCode::PlatformError, "Lock text memory failed.");
  }
  memcpy(destination, text.c_str(), bytes);
  GlobalUnlock(memory);

  if (SetClipboardData(format, memory) == nullptr) {
    GlobalFree(memory);
    return common::Status::Error(common::StatusCode::PlatformError, "Write text clipboard data failed.");
  }
  return common::Status::Ok();
}

common::Status SetGlobalAnsiData(UINT format, const std::string& text) {
  HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, text.size() + 1);
  if (memory == nullptr) {
    return common::Status::Error(common::StatusCode::PlatformError, "Allocate ansi memory failed.");
  }

  void* destination = GlobalLock(memory);
  if (destination == nullptr) {
    GlobalFree(memory);
    return common::Status::Error(common::StatusCode::PlatformError, "Lock ansi memory failed.");
  }
  memcpy(destination, text.c_str(), text.size() + 1);
  GlobalUnlock(memory);

  if (SetClipboardData(format, memory) == nullptr) {
    GlobalFree(memory);
    return common::Status::Error(common::StatusCode::PlatformError, "Write ansi clipboard data failed.");
  }
  return common::Status::Ok();
}

common::Status SetGlobalBinaryData(UINT format, const std::vector<std::uint8_t>& bytes) {
  if (bytes.empty()) {
    return common::Status::Ok();
  }

  HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
  if (memory == nullptr) {
    return common::Status::Error(common::StatusCode::PlatformError, "Allocate binary memory failed.");
  }

  void* destination = GlobalLock(memory);
  if (destination == nullptr) {
    GlobalFree(memory);
    return common::Status::Error(common::StatusCode::PlatformError, "Lock binary memory failed.");
  }
  memcpy(destination, bytes.data(), bytes.size());
  GlobalUnlock(memory);

  if (SetClipboardData(format, memory) == nullptr) {
    GlobalFree(memory);
    return common::Status::Error(common::StatusCode::PlatformError, "Write binary clipboard data failed.");
  }
  return common::Status::Ok();
}

HGLOBAL CreatePreferredDropEffectHandle(DWORD effect) {
  HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DWORD));
  if (memory == nullptr) {
    return nullptr;
  }
  auto* value = static_cast<DWORD*>(GlobalLock(memory));
  if (value == nullptr) {
    GlobalFree(memory);
    return nullptr;
  }
  *value = effect;
  GlobalUnlock(memory);
  return memory;
}

common::Status SetPreferredDropEffectCopyData() {
  const UINT format = RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT);
  if (format == 0) {
    return common::Status::Error(common::StatusCode::PlatformError, "Register preferred drop effect format failed.");
  }
  HGLOBAL memory = CreatePreferredDropEffectHandle(DROPEFFECT_COPY);
  if (memory == nullptr) {
    return common::Status::Error(common::StatusCode::PlatformError, "Allocate preferred drop effect memory failed.");
  }
  if (SetClipboardData(format, memory) == nullptr) {
    GlobalFree(memory);
    return common::Status::Error(common::StatusCode::PlatformError, "Write preferred drop effect failed.");
  }
  return common::Status::Ok();
}

common::Status SetClipboardFilesData(const std::wstring& joined_paths) {
  const std::vector<std::wstring> paths = common::SplitWide(joined_paths, L'\n');
  std::wstring multi_string;
  for (const std::wstring& path : paths) {
    const std::wstring trimmed = common::TrimWide(path);
    if (trimmed.empty()) {
      continue;
    }
    multi_string.append(trimmed);
    multi_string.push_back(L'\0');
  }
  multi_string.push_back(L'\0');

  const size_t bytes = sizeof(DROPFILES) + multi_string.size() * sizeof(wchar_t);
  HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
  if (memory == nullptr) {
    return common::Status::Error(common::StatusCode::PlatformError, "Allocate file list memory failed.");
  }

  auto* dropfiles = static_cast<DROPFILES*>(GlobalLock(memory));
  if (dropfiles == nullptr) {
    GlobalFree(memory);
    return common::Status::Error(common::StatusCode::PlatformError, "Lock file list memory failed.");
  }
  dropfiles->pFiles = sizeof(DROPFILES);
  dropfiles->fWide = TRUE;
  memcpy(
      reinterpret_cast<BYTE*>(dropfiles) + sizeof(DROPFILES),
      multi_string.data(),
      multi_string.size() * sizeof(wchar_t));
  GlobalUnlock(memory);

  if (SetClipboardData(CF_HDROP, memory) == nullptr) {
    GlobalFree(memory);
    return common::Status::Error(common::StatusCode::PlatformError, "Write file list clipboard data failed.");
  }
  return SetPreferredDropEffectCopyData();
}

common::Status LoadBitmapFromPng(const std::wstring& path, HBITMAP* bitmap) {
  if (bitmap == nullptr) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "Bitmap output cannot be null.");
  }
  *bitmap = nullptr;

  IWICImagingFactory* factory = nullptr;
  HRESULT hr = CoCreateInstance(
      CLSID_WICImagingFactory,
      nullptr,
      CLSCTX_INPROC_SERVER,
      IID_PPV_ARGS(&factory));
  if (FAILED(hr)) {
    return common::Status::Error(common::StatusCode::PlatformError, "Create WIC factory failed.");
  }

  IWICBitmapDecoder* decoder = nullptr;
  hr = factory->CreateDecoderFromFilename(
      path.c_str(),
      nullptr,
      GENERIC_READ,
      WICDecodeMetadataCacheOnLoad,
      &decoder);
  if (FAILED(hr)) {
    factory->Release();
    return common::Status::Error(common::StatusCode::NotFound, "Open image file failed.");
  }

  IWICBitmapFrameDecode* frame = nullptr;
  hr = decoder->GetFrame(0, &frame);
  IWICFormatConverter* converter = nullptr;
  if (SUCCEEDED(hr)) {
    hr = factory->CreateFormatConverter(&converter);
  }
  if (SUCCEEDED(hr)) {
    hr = converter->Initialize(
        frame,
        GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0,
        WICBitmapPaletteTypeCustom);
  }
  if (FAILED(hr)) {
    if (converter != nullptr) converter->Release();
    if (frame != nullptr) frame->Release();
    decoder->Release();
    factory->Release();
    return common::Status::Error(common::StatusCode::PlatformError, "Decode image file failed.");
  }

  UINT width = 0;
  UINT height = 0;
  hr = converter->GetSize(&width, &height);
  if (FAILED(hr) || width == 0 || height == 0) {
    converter->Release();
    frame->Release();
    decoder->Release();
    factory->Release();
    return common::Status::Error(common::StatusCode::PlatformError, "Read image size failed.");
  }

  BITMAPINFO bitmap_info{};
  bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bitmap_info.bmiHeader.biWidth = static_cast<LONG>(width);
  bitmap_info.bmiHeader.biHeight = -static_cast<LONG>(height);
  bitmap_info.bmiHeader.biPlanes = 1;
  bitmap_info.bmiHeader.biBitCount = 32;
  bitmap_info.bmiHeader.biCompression = BI_RGB;

  void* dib_pixels = nullptr;
  HDC screen = GetDC(nullptr);
  HBITMAP created_bitmap = CreateDIBSection(
      screen,
      &bitmap_info,
      DIB_RGB_COLORS,
      &dib_pixels,
      nullptr,
      0);
  ReleaseDC(nullptr, screen);
  if (created_bitmap == nullptr || dib_pixels == nullptr) {
    converter->Release();
    frame->Release();
    decoder->Release();
    factory->Release();
    return common::Status::Error(common::StatusCode::PlatformError, "Create DIB section failed.");
  }

  const UINT stride = width * 4;
  const UINT image_size = stride * height;
  hr = converter->CopyPixels(nullptr, stride, image_size, static_cast<BYTE*>(dib_pixels));
  converter->Release();
  frame->Release();
  decoder->Release();
  factory->Release();
  if (FAILED(hr)) {
    DeleteObject(created_bitmap);
    return common::Status::Error(common::StatusCode::PlatformError, "Copy image pixels failed.");
  }

  *bitmap = created_bitmap;
  return common::Status::Ok();
}

std::vector<std::uint8_t> ReadFileBytes(const std::wstring& path) {
  if (path.empty()) {
    return {};
  }

  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    return {};
  }
  input.seekg(0, std::ios::end);
  const std::streamoff size = input.tellg();
  if (size <= 0) {
    return {};
  }
  input.seekg(0, std::ios::beg);

  std::vector<std::uint8_t> bytes(static_cast<size_t>(size));
  input.read(reinterpret_cast<char*>(bytes.data()), size);
  if (!input.good() && !input.eof()) {
    return {};
  }
  return bytes;
}

common::Status ExtractBitmapPixels(
    HBITMAP bitmap,
    UINT* width,
    UINT* height,
    std::vector<std::uint8_t>* pixels) {
  if (bitmap == nullptr || width == nullptr || height == nullptr || pixels == nullptr) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "Bitmap extract output is invalid.");
  }

  *width = 0;
  *height = 0;
  pixels->clear();

  BITMAP info{};
  if (GetObjectW(bitmap, sizeof(info), &info) == 0 || info.bmWidth <= 0 || info.bmHeight <= 0) {
    return common::Status::Error(common::StatusCode::PlatformError, "Read bitmap metadata failed.");
  }

  BITMAPINFO bitmap_info{};
  bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bitmap_info.bmiHeader.biWidth = info.bmWidth;
  bitmap_info.bmiHeader.biHeight = -info.bmHeight;
  bitmap_info.bmiHeader.biPlanes = 1;
  bitmap_info.bmiHeader.biBitCount = 32;
  bitmap_info.bmiHeader.biCompression = BI_RGB;

  pixels->resize(static_cast<size_t>(info.bmWidth) * static_cast<size_t>(info.bmHeight) * 4u);
  HDC screen = GetDC(nullptr);
  if (screen == nullptr) {
    pixels->clear();
    return common::Status::Error(common::StatusCode::PlatformError, "GetDC failed.");
  }

  const int result = GetDIBits(
      screen,
      bitmap,
      0,
      static_cast<UINT>(info.bmHeight),
      pixels->data(),
      &bitmap_info,
      DIB_RGB_COLORS);
  ReleaseDC(nullptr, screen);
  if (result == 0) {
    pixels->clear();
    return common::Status::Error(common::StatusCode::PlatformError, "Extract bitmap pixels failed.");
  }

  *width = static_cast<UINT>(info.bmWidth);
  *height = static_cast<UINT>(info.bmHeight);
  return common::Status::Ok();
}

std::vector<std::uint8_t> BuildDibPayload(
    UINT width,
    UINT height,
    const std::vector<std::uint8_t>& pixels,
    bool v5_header) {
  if (width == 0 || height == 0 || pixels.empty()) {
    return {};
  }

  const size_t pixel_bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
  if (pixels.size() < pixel_bytes) {
    return {};
  }

  if (v5_header) {
    std::vector<std::uint8_t> payload(sizeof(BITMAPV5HEADER) + pixel_bytes);
    auto* header = reinterpret_cast<BITMAPV5HEADER*>(payload.data());
    ZeroMemory(header, sizeof(BITMAPV5HEADER));
    header->bV5Size = sizeof(BITMAPV5HEADER);
    header->bV5Width = static_cast<LONG>(width);
    header->bV5Height = -static_cast<LONG>(height);
    header->bV5Planes = 1;
    header->bV5BitCount = 32;
    header->bV5Compression = BI_BITFIELDS;
    header->bV5RedMask = 0x00FF0000;
    header->bV5GreenMask = 0x0000FF00;
    header->bV5BlueMask = 0x000000FF;
    header->bV5AlphaMask = 0xFF000000;
    header->bV5CSType = LCS_sRGB;
    memcpy(payload.data() + sizeof(BITMAPV5HEADER), pixels.data(), pixel_bytes);
    return payload;
  }

  std::vector<std::uint8_t> payload(sizeof(BITMAPINFOHEADER) + pixel_bytes);
  auto* header = reinterpret_cast<BITMAPINFOHEADER*>(payload.data());
  ZeroMemory(header, sizeof(BITMAPINFOHEADER));
  header->biSize = sizeof(BITMAPINFOHEADER);
  header->biWidth = static_cast<LONG>(width);
  header->biHeight = -static_cast<LONG>(height);
  header->biPlanes = 1;
  header->biBitCount = 32;
  header->biCompression = BI_RGB;
  memcpy(payload.data() + sizeof(BITMAPINFOHEADER), pixels.data(), pixel_bytes);
  return payload;
}

common::Status WriteGeneratedImageFormatsFromPng(const std::wstring& path) {
  HBITMAP bitmap = nullptr;
  common::Status status = LoadBitmapFromPng(path, &bitmap);
  if (!status.ok()) {
    return status;
  }

  UINT width = 0;
  UINT height = 0;
  std::vector<std::uint8_t> pixels;
  status = ExtractBitmapPixels(bitmap, &width, &height, &pixels);
  if (!status.ok()) {
    DeleteObject(bitmap);
    return status;
  }

  const std::vector<std::uint8_t> dibv5_bytes = BuildDibPayload(width, height, pixels, true);
  const std::vector<std::uint8_t> dib_bytes = BuildDibPayload(width, height, pixels, false);
  const std::vector<std::uint8_t> png_bytes = ReadFileBytes(path);

  ClipboardScope clipboard;
  if (!clipboard.opened()) {
    DeleteObject(bitmap);
    return common::Status::Error(common::StatusCode::Unavailable, "Open clipboard failed.");
  }

  EmptyClipboard();
  bool wrote_any = false;

  if (!dibv5_bytes.empty()) {
    const common::Status dibv5_status = SetGlobalBinaryData(CF_DIBV5, dibv5_bytes);
    if (dibv5_status.ok()) {
      wrote_any = true;
    }
  }
  if (!dib_bytes.empty()) {
    const common::Status dib_status = SetGlobalBinaryData(CF_DIB, dib_bytes);
    if (dib_status.ok()) {
      wrote_any = true;
    }
  }
  if (!png_bytes.empty()) {
    const common::Status png_status = SetGlobalBinaryData(GetPngClipboardFormat(), png_bytes);
    if (png_status.ok()) {
      wrote_any = true;
    }
  }
  if (SetClipboardData(CF_BITMAP, bitmap) != nullptr) {
    wrote_any = true;
    bitmap = nullptr;
  }

  if (bitmap != nullptr) {
    DeleteObject(bitmap);
  }
  if (!wrote_any) {
    return common::Status::Error(common::StatusCode::PlatformError, "Write generated image formats failed.");
  }
  if (!path.empty()) {
    SetClipboardFilesData(path);
  }
  return common::Status::Ok();
}

common::Status WriteGeneratedImageFormatsFromFile(const std::wstring& path) {
  HBITMAP bitmap = nullptr;
  common::Status status = LoadBitmapFromPng(path, &bitmap);
  if (!status.ok()) {
    return status;
  }

  UINT width = 0;
  UINT height = 0;
  std::vector<std::uint8_t> pixels;
  status = ExtractBitmapPixels(bitmap, &width, &height, &pixels);
  if (!status.ok()) {
    DeleteObject(bitmap);
    return status;
  }

  const std::vector<std::uint8_t> dibv5_bytes = BuildDibPayload(width, height, pixels, true);
  const std::vector<std::uint8_t> dib_bytes = BuildDibPayload(width, height, pixels, false);
  const std::wstring extension = common::ToLowerWide(std::filesystem::path(path).extension().wstring());
  const bool include_png_payload = extension == L".png";
  const std::vector<std::uint8_t> png_bytes = include_png_payload ? ReadFileBytes(path) : std::vector<std::uint8_t>{};

  ClipboardScope clipboard;
  if (!clipboard.opened()) {
    DeleteObject(bitmap);
    return common::Status::Error(common::StatusCode::Unavailable, "Open clipboard failed.");
  }

  EmptyClipboard();
  bool wrote_any = false;

  if (!dibv5_bytes.empty()) {
    const common::Status dibv5_status = SetGlobalBinaryData(CF_DIBV5, dibv5_bytes);
    if (dibv5_status.ok()) {
      wrote_any = true;
    }
  }
  if (!dib_bytes.empty()) {
    const common::Status dib_status = SetGlobalBinaryData(CF_DIB, dib_bytes);
    if (dib_status.ok()) {
      wrote_any = true;
    }
  }
  if (!png_bytes.empty()) {
    const common::Status png_status = SetGlobalBinaryData(GetPngClipboardFormat(), png_bytes);
    if (png_status.ok()) {
      wrote_any = true;
    }
  }
  if (SetClipboardData(CF_BITMAP, bitmap) != nullptr) {
    wrote_any = true;
    bitmap = nullptr;
  }

  if (bitmap != nullptr) {
    DeleteObject(bitmap);
  }
  if (!wrote_any) {
    return common::Status::Error(common::StatusCode::PlatformError, "Write generated image formats failed.");
  }
  if (!path.empty()) {
    SetClipboardFilesData(path);
  }
  return common::Status::Ok();
}

std::optional<UINT> ResolveClipboardFormat(std::string_view format_name) {
  if (format_name == "CF_UNICODETEXT") {
    return CF_UNICODETEXT;
  }
  if (format_name == "HTML Format") {
    return GetHtmlClipboardFormat();
  }
  if (format_name == "Rich Text Format") {
    return GetRtfClipboardFormat();
  }
  if (format_name == "CF_HDROP") {
    return CF_HDROP;
  }
  if (format_name == "CF_DIB") {
    return CF_DIB;
  }
  if (format_name == "CF_DIBV5") {
    return CF_DIBV5;
  }
  if (format_name == "PNG") {
    return GetPngClipboardFormat();
  }
  return std::nullopt;
}

common::Status WriteStandardFormat(const store::ClipFormatPayload& format) {
  const auto format_id = ResolveClipboardFormat(format.format_name);
  if (!format_id.has_value()) {
    return common::Status::Ok();
  }

  if (*format_id == CF_UNICODETEXT) {
    return SetGlobalTextData(CF_UNICODETEXT, common::Utf8ToWide(format.text_data).wide);
  }
  if (*format_id == CF_HDROP) {
    return SetClipboardFilesData(common::Utf8ToWide(format.text_data).wide);
  }
  if (*format_id == GetHtmlClipboardFormat() || *format_id == GetRtfClipboardFormat()) {
    return SetGlobalAnsiData(*format_id, format.text_data);
  }
  return SetGlobalBinaryData(*format_id, format.binary_data);
}

common::Status WriteStandardFormatsToClipboard(const std::vector<store::ClipFormatPayload>& formats) {
  ClipboardScope clipboard;
  if (!clipboard.opened()) {
    return common::Status::Error(common::StatusCode::Unavailable, "Open clipboard failed.");
  }

  EmptyClipboard();
  for (const auto& format : formats) {
    const common::Status status = WriteStandardFormat(format);
    if (!status.ok()) {
      return status;
    }
  }
  return common::Status::Ok();
}

bool IsImageFormatName(std::string_view format_name) {
  return format_name == "CF_DIB" || format_name == "CF_DIBV5" || format_name == "PNG";
}

common::Status SetClipboardImageClip(
    const std::vector<store::ClipFormatPayload>& formats,
    const std::wstring& fallback_png_path) {
  ClipboardScope clipboard;
  if (!clipboard.opened()) {
    return common::Status::Error(common::StatusCode::Unavailable, "Open clipboard failed.");
  }

  EmptyClipboard();

  bool wrote_image = false;
  for (const auto& format : formats) {
    if (!IsImageFormatName(format.format_name) || format.binary_data.empty()) {
      continue;
    }
    const auto format_id = ResolveClipboardFormat(format.format_name);
    if (!format_id.has_value()) {
      continue;
    }
    const common::Status status = SetGlobalBinaryData(*format_id, format.binary_data);
    if (!status.ok()) {
      return status;
    }
    wrote_image = true;
  }

  if (!fallback_png_path.empty()) {
    HBITMAP bitmap = nullptr;
    const common::Status status = LoadBitmapFromPng(fallback_png_path, &bitmap);
    if (!status.ok()) {
      if (!wrote_image) {
        return status;
      }
    } else if (SetClipboardData(CF_BITMAP, bitmap) == nullptr) {
      DeleteObject(bitmap);
      if (!wrote_image) {
        return common::Status::Error(common::StatusCode::PlatformError, "Write image bitmap failed.");
      }
    } else {
      wrote_image = true;
    }
  }

  if (!wrote_image) {
    return common::Status::Error(common::StatusCode::NotFound, "No image payload available.");
  }
  return common::Status::Ok();
}

}  // namespace

common::Status SetClipboardText(
    std::string_view text,
    std::string_view html_text,
    std::string_view rtf_text) {
  ClipboardScope clipboard;
  if (!clipboard.opened()) {
    return common::Status::Error(common::StatusCode::Unavailable, "Open clipboard failed.");
  }

  EmptyClipboard();

  common::Status status = SetGlobalTextData(CF_UNICODETEXT, common::Utf8ToWide(text).wide);
  if (!status.ok()) {
    return status;
  }
  if (!html_text.empty()) {
    status = SetGlobalAnsiData(GetHtmlClipboardFormat(), std::string(html_text));
    if (!status.ok()) {
      return status;
    }
  }
  if (!rtf_text.empty()) {
    status = SetGlobalAnsiData(GetRtfClipboardFormat(), std::string(rtf_text));
  }
  return status;
}

common::Status SetClipboardFiles(const std::wstring& joined_paths) {
  ClipboardScope clipboard;
  if (!clipboard.opened()) {
    return common::Status::Error(common::StatusCode::Unavailable, "Open clipboard failed.");
  }

  EmptyClipboard();
  return SetClipboardFilesData(joined_paths);
}

common::Status SetClipboardImageFromPng(const std::wstring& path) {
  return WriteGeneratedImageFormatsFromPng(path);
}

common::Status SetClipboardImageFromFile(const std::wstring& path) {
  return WriteGeneratedImageFormatsFromFile(path);
}

bool IsValidPasteTargetContext(const PasteTargetContext& context) {
  return context.root_window != nullptr && IsWindow(context.root_window);
}

PasteTargetContext CapturePasteTargetContext(HWND preferred_window) {
  PasteTargetContext context{};
  HWND root_window = NormalizeRootWindow(preferred_window);
  if (root_window == nullptr) {
    root_window = NormalizeRootWindow(GetForegroundWindow());
  }
  if (root_window == nullptr) {
    return context;
  }

  context.root_window = root_window;
  context.thread_id = GetWindowThreadProcessId(root_window, nullptr);

  GUITHREADINFO info{};
  info.cbSize = sizeof(info);
  if (context.thread_id != 0 &&
      GetGUIThreadInfo(context.thread_id, &info) &&
      BelongsToRootWindow(info.hwndFocus, root_window)) {
    context.focus_window = info.hwndFocus;
  } else {
    context.focus_window = root_window;
  }
  return context;
}

bool HasSpecificFocusedChild(const PasteTargetContext& context) {
  return IsValidPasteTargetContext(context) &&
      context.focus_window != nullptr &&
      context.focus_window != context.root_window &&
      BelongsToRootWindow(context.focus_window, context.root_window);
}

PasteTargetContext RefinePasteTargetContext(const PasteTargetContext& context, const char* stage) {
  if (!IsValidPasteTargetContext(context) || HasSpecificFocusedChild(context)) {
    return context;
  }

  for (int attempt = 0; attempt < 8; ++attempt) {
    const PasteTargetContext refined = CapturePasteTargetContext(context.root_window);
    if (!IsValidPasteTargetContext(refined)) {
      break;
    }
    if (HasSpecificFocusedChild(refined)) {
      common::WriteLog(
          common::LogLevel::Info,
          "clipboard.paste_target",
          std::string(stage) +
              " refined focus attempt=" + std::to_string(attempt + 1) +
              " root=" + std::to_string(reinterpret_cast<std::uintptr_t>(refined.root_window)) +
              " focus=" + std::to_string(reinterpret_cast<std::uintptr_t>(refined.focus_window)));
      return refined;
    }
    Sleep(10);
  }

  common::WriteLog(
      common::LogLevel::Info,
      "clipboard.paste_target",
      std::string(stage) +
          " stayed root-only root=" + std::to_string(reinterpret_cast<std::uintptr_t>(context.root_window)) +
          " focus=" + std::to_string(reinterpret_cast<std::uintptr_t>(context.focus_window)));
  return context;
}

common::Status PerformPasteToTarget(
    const PasteTargetContext& requested_context,
    PasteRoute route,
    std::string_view text_fallback) {
  PasteTargetContext context = requested_context;
  if (!IsValidPasteTargetContext(context)) {
    context = CapturePasteTargetContext();
  }
  if (!IsValidPasteTargetContext(context)) {
    return common::Status::Error(common::StatusCode::Unavailable, "Paste target unavailable.");
  }
  context = RefinePasteTargetContext(context, "pre-dispatch");
  common::WriteLog(
      common::LogLevel::Info,
      "clipboard.paste_target",
      "perform begin requested_root=" + std::to_string(reinterpret_cast<std::uintptr_t>(requested_context.root_window)) +
          " actual_root=" + std::to_string(reinterpret_cast<std::uintptr_t>(context.root_window)) +
          " actual_focus=" + std::to_string(reinterpret_cast<std::uintptr_t>(context.focus_window)) +
          " route=" + std::to_string(static_cast<int>(route)));

  const HWND foreground_root = NormalizeRootWindow(GetForegroundWindow());

  auto try_fast_dispatch = [&]() -> common::Status {
      if (foreground_root != context.root_window) {
        return common::Status::Error(common::StatusCode::Unavailable, "Fast paste target changed.");
      }
      return DispatchPasteToContext(context, text_fallback);
    };

  if (route == PasteRoute::Fast) {
    const common::Status fast_status = try_fast_dispatch();
    if (fast_status.ok()) {
      return fast_status;
    }
    route = PasteRoute::Recovery;
  }

  if (!TryRestorePasteTarget(context)) {
    context = CapturePasteTargetContext(context.root_window);
    if (!TryRestorePasteTarget(context)) {
      return common::Status::Error(common::StatusCode::Unavailable, "Restore paste target failed.");
    }
  }

  context = RefinePasteTargetContext(context, "post-restore");

  return DispatchPasteToContext(context, text_fallback);
}

common::Status WriteClipToClipboard(
    const store::ClipRecord& clip,
    std::string_view transformed_text,
    bool use_transformed_text) {
  if (use_transformed_text) {
    return SetClipboardText(transformed_text, {}, {});
  }

  if (clip.summary.content_type == store::ClipContentType::Image) {
    if (!clip.storage_path.empty()) {
      const common::Status png_status = SetClipboardImageFromPng(clip.storage_path);
      if (png_status.ok()) {
        return png_status;
      }
    }
    const common::Status image_status = SetClipboardImageClip(clip.formats, clip.storage_path);
    if (image_status.ok()) {
      return image_status;
    }
  }

  if (!clip.formats.empty()) {
    const common::Status status = WriteStandardFormatsToClipboard(clip.formats);
    if (status.ok()) {
      return status;
    }
  }

  switch (clip.summary.content_type) {
    case store::ClipContentType::Text:
      return SetClipboardText(clip.plain_text, {}, {});
    case store::ClipContentType::Html:
      return SetClipboardText(clip.plain_text, clip.html_text, {});
    case store::ClipContentType::Rtf:
      return SetClipboardText(clip.plain_text, {}, clip.rtf_text);
    case store::ClipContentType::Files:
      return SetClipboardFiles(common::Utf8ToWide(clip.plain_text).wide);
    case store::ClipContentType::Image:
      return SetClipboardImageFromPng(clip.storage_path);
  }

  return common::Status::Error(common::StatusCode::NotImplemented, "Unsupported clip content type.");
}

}  // namespace clipd2d::core::clipboard
