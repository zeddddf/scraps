#pragma once
#include "ui/MainWindow.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <objidl.h>
#include <ole2.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl_core.h>
#include <windowsx.h>
#include <wincodec.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <cwctype>
#include <filesystem>
#include <memory>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>
#include <functional>

#include "common/ClipboardSuppression.h"
#include "core/browser/ClipTextDisplay.h"
#include "common/Log.h"
#include "common/Stopwatch.h"
#include "common/Paths.h"
#include "common/StringUtil.h"
#include "common/Utf8.h"
#include "ui/FluentIcons.h"
#include "ui/PanelWindowChrome.h"
#include "ui/GroupMenuCommand.h"

#include "ui/MainWindowTypes.h"
#include "ui/D2dUtil.h"
#include "ui/DragDropInterop.h"
#include "ui/HistoryPayloads.h"
#include "ui/ImageLoaderUtil.h"
#include "ui/MainWindowMetrics.h"
#include "ui/OwnedMessage.h"
#include "ui/WindowContract.h"
#include "ui/ClipRowText.h"
#include "ui/DrawTextUtil.h"

namespace clipd2d::ui {

// ---------------------------------------------------------------------------
// L7:跨 TU 共享的可变状态与消息 payload 类型必须是具名实体。
// 匿名命名空间里的函数级 static 会被每个 include 该头的 .cpp 各持一份副本,
// 各 TU 读写的是互不相通的变量;类型同理会分裂成"同名不同类型",跨 TU 的
// new/delete 交接(工作线程构造 → UI 线程析构)类型身份不一致。故:
//   - ShowFilterChipBarFlag 的唯一定义在 MainWindow.cpp(单一 TU 具名实体);
//   - 消息 payload 结构体保持具名类型(见下),供 OwnedMessage 交接。
// ---------------------------------------------------------------------------

// 过滤条件 chip 条是否可见(影响 ComputeLayout 的列表布局)。定义见 MainWindow.cpp。
bool& ShowFilterChipBarFlag();

inline int ActiveChipBarHeight() {
  return ShowFilterChipBarFlag() ? 28 : 0;
}

namespace {

inline RECT SearchClearButtonRect(const clipd2d::ui::PanelLayout& layout) {
  return RECT{
      layout.search_box.right - 38,
      layout.search_box.top + 1,
      layout.search_box.right - 4,
      layout.search_box.bottom - 1};
}

inline int SearchTextRightEdge(const clipd2d::ui::PanelLayout& layout, bool has_search_text) {
  if (has_search_text) {
    return SearchClearButtonRect(layout).left - 6;
  }
  return layout.search_box.right - 12;
}

struct TabDefinition {
  core::store::ClipView view;
  FluentIcon icon;
  const wchar_t* label;
  bool opens_menu = false;
};

enum class TimeGroupBucket {
  Today,
  Yesterday,
  Last7Days,
  Older,
};

constexpr TabDefinition kTabs[] = {
    {core::store::ClipView::All, FluentIcon::List, strings::kTabHistory, false},
    {core::store::ClipView::Favorites, FluentIcon::StarOutline, strings::kTabFavorites, false},
    {core::store::ClipView::Pinned, FluentIcon::PanelPinned, L"\u7f6e\u9876", false},
    {core::store::ClipView::Text, FluentIcon::Text, strings::kFilterTypeText, false},
    {core::store::ClipView::Html, FluentIcon::Code, L"HTML", false},
    {core::store::ClipView::Image, FluentIcon::Image, strings::kFilterTypeImage, false},
    {core::store::ClipView::Files, FluentIcon::Folder, strings::kFilterTypeFiles, false},
    {core::store::ClipView::Group, FluentIcon::Folder, strings::kTabGroups, true},
};

constexpr FluentIcon kToolbarIcons[] = {
    FluentIcon::PanelPin,
    FluentIcon::Settings,
    FluentIcon::More,
};

constexpr TabDefinition kVisibleTabs[] = {
    {core::store::ClipView::All, FluentIcon::List, strings::kTabHistory, false},
    {core::store::ClipView::Favorites, FluentIcon::StarOutline, strings::kTabFavorites, false},
    {core::store::ClipView::Group, FluentIcon::Folder, strings::kTabGroups, true},
};

constexpr const wchar_t* kToolbarTooltips[] = {
    strings::kToolbarPanelLock,
    strings::kToolbarSettings,
    strings::kToolbarMore,
    L"",
};

HGLOBAL DuplicateGlobalMemory(HGLOBAL source) {
  if (source == nullptr) {
    return nullptr;
  }
  const SIZE_T size = GlobalSize(source);
  if (size == 0) {
    return nullptr;
  }
  HGLOBAL copy = GlobalAlloc(GMEM_MOVEABLE, size);
  if (copy == nullptr) {
    return nullptr;
  }
  const void* source_ptr = GlobalLock(source);
  void* copy_ptr = GlobalLock(copy);
  if (source_ptr == nullptr || copy_ptr == nullptr) {
    if (copy_ptr != nullptr) {
      GlobalUnlock(copy);
    }
    if (source_ptr != nullptr) {
      GlobalUnlock(source);
    }
    GlobalFree(copy);
    return nullptr;
  }
  memcpy(copy_ptr, source_ptr, size);
  GlobalUnlock(copy);
  GlobalUnlock(source);
  return copy;
}

HGLOBAL CreateFileDropHandle(const std::vector<std::wstring>& paths) {
  if (paths.empty()) {
    return nullptr;
  }
  SIZE_T total_bytes = sizeof(DROPFILES) + sizeof(wchar_t);
  for (const auto& path : paths) {
    total_bytes += (path.size() + 1) * sizeof(wchar_t);
  }
  HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, total_bytes);
  if (memory == nullptr) {
    return nullptr;
  }
  auto* drop_files = static_cast<DROPFILES*>(GlobalLock(memory));
  if (drop_files == nullptr) {
    GlobalFree(memory);
    return nullptr;
  }
  drop_files->pFiles = sizeof(DROPFILES);
  drop_files->fWide = TRUE;
  auto* cursor = reinterpret_cast<wchar_t*>(reinterpret_cast<BYTE*>(drop_files) + sizeof(DROPFILES));
  for (const auto& path : paths) {
    wcscpy_s(cursor, path.size() + 1, path.c_str());
    cursor += path.size() + 1;
  }
  *cursor = L'\0';
  GlobalUnlock(memory);
  return memory;
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

class FileDropDataObject final : public IDataObject {
 public:
  explicit FileDropDataObject(HGLOBAL file_drop)
      : ref_count_(1),
        file_drop_(file_drop),
        preferred_drop_effect_format_(RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT)) {}

  ~FileDropDataObject() {
    if (file_drop_ != nullptr) {
      GlobalFree(file_drop_);
    }
  }

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
    if (object == nullptr) {
      return E_POINTER;
    }
    *object = nullptr;
    if (iid == IID_IUnknown || iid == IID_IDataObject) {
      *object = static_cast<IDataObject*>(this);
      AddRef();
      return S_OK;
    }
    return E_NOINTERFACE;
  }

  ULONG STDMETHODCALLTYPE AddRef() override {
    return static_cast<ULONG>(InterlockedIncrement(&ref_count_));
  }

  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG remaining = static_cast<ULONG>(InterlockedDecrement(&ref_count_));
    if (remaining == 0) {
      delete this;
    }
    return remaining;
  }

  HRESULT STDMETHODCALLTYPE GetData(FORMATETC* format, STGMEDIUM* medium) override {
    if (format == nullptr || medium == nullptr) {
      return E_INVALIDARG;
    }
    if ((format->tymed & TYMED_HGLOBAL) == 0) {
      return DV_E_FORMATETC;
    }
    if (format->cfFormat == CF_HDROP) {
      HGLOBAL duplicated = DuplicateGlobalMemory(file_drop_);
      if (duplicated == nullptr) {
        return E_OUTOFMEMORY;
      }
      medium->tymed = TYMED_HGLOBAL;
      medium->hGlobal = duplicated;
      medium->pUnkForRelease = nullptr;
      return S_OK;
    }
    if (format->cfFormat == preferred_drop_effect_format_) {
      HGLOBAL effect = CreatePreferredDropEffectHandle(DROPEFFECT_COPY);
      if (effect == nullptr) {
        return E_OUTOFMEMORY;
      }
      medium->tymed = TYMED_HGLOBAL;
      medium->hGlobal = effect;
      medium->pUnkForRelease = nullptr;
      return S_OK;
    }
    return DV_E_FORMATETC;
  }

  HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override { return DATA_E_FORMATETC; }
  HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC* format) override {
    if (format == nullptr) {
      return E_INVALIDARG;
    }
    if ((format->tymed & TYMED_HGLOBAL) == 0) {
      return DV_E_FORMATETC;
    }
    return (format->cfFormat == CF_HDROP || format->cfFormat == preferred_drop_effect_format_) ? S_OK : DV_E_FORMATETC;
  }
  HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC* out) override {
    if (out != nullptr) {
      out->ptd = nullptr;
    }
    return E_NOTIMPL;
  }
  HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }
  HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD direction, IEnumFORMATETC** enum_formats) override {
    if (enum_formats == nullptr) {
      return E_POINTER;
    }
    *enum_formats = nullptr;
    if (direction != DATADIR_GET) {
      return E_NOTIMPL;
    }
    FORMATETC formats[2]{
        {CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL},
        {static_cast<CLIPFORMAT>(preferred_drop_effect_format_), nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL},
    };
    return SHCreateStdEnumFmtEtc(2, formats, enum_formats);
  }
  HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
  HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
  HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }

 private:
  LONG ref_count_;
  HGLOBAL file_drop_ = nullptr;
  UINT preferred_drop_effect_format_ = 0;
};

class SimpleDropSource final : public IDropSource {
 public:
  SimpleDropSource(HWND preview_hwnd, HBITMAP preview_bitmap, POINT preview_offset)
      : preview_hwnd_(preview_hwnd), preview_bitmap_(preview_bitmap), preview_offset_(preview_offset) {}

  ~SimpleDropSource() {
    DestroyPreview();
  }

  HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
    if (object == nullptr) {
      return E_POINTER;
    }
    *object = nullptr;
    if (iid == IID_IUnknown || iid == IID_IDropSource) {
      *object = static_cast<IDropSource*>(this);
      AddRef();
      return S_OK;
    }
    return E_NOINTERFACE;
  }

  ULONG STDMETHODCALLTYPE AddRef() override {
    return static_cast<ULONG>(InterlockedIncrement(&ref_count_));
  }

  ULONG STDMETHODCALLTYPE Release() override {
    const ULONG remaining = static_cast<ULONG>(InterlockedDecrement(&ref_count_));
    if (remaining == 0) {
      delete this;
    }
    return remaining;
  }

  HRESULT STDMETHODCALLTYPE QueryContinueDrag(BOOL escape_pressed, DWORD key_state) override {
    if (escape_pressed) {
      DestroyPreview();
      return DRAGDROP_S_CANCEL;
    }
    if ((key_state & MK_LBUTTON) == 0) {
      DestroyPreview();
      return DRAGDROP_S_DROP;
    }
    return S_OK;
  }

  HRESULT STDMETHODCALLTYPE GiveFeedback(DWORD) override {
    if (preview_hwnd_ != nullptr) {
      POINT cursor{};
      GetCursorPos(&cursor);
      SetWindowPos(
          preview_hwnd_,
          HWND_TOPMOST,
          cursor.x + preview_offset_.x,
          cursor.y + preview_offset_.y,
          0,
          0,
          SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    return DRAGDROP_S_USEDEFAULTCURSORS;
  }

 private:
  void DestroyPreview() {
    if (preview_hwnd_ != nullptr) {
      DestroyWindow(preview_hwnd_);
      preview_hwnd_ = nullptr;
    }
    if (preview_bitmap_ != nullptr) {
      DeleteObject(preview_bitmap_);
      preview_bitmap_ = nullptr;
    }
  }

  LONG ref_count_ = 1;
  HWND preview_hwnd_ = nullptr;
  HBITMAP preview_bitmap_ = nullptr;
  POINT preview_offset_{12, 12};
};

float EaseOutCubic(float t) {
  const float clamped = std::clamp(t, 0.0f, 1.0f);
  const float inv = 1.0f - clamped;
  return 1.0f - inv * inv * inv;
}

HBITMAP CaptureClientRectBitmap(HWND hwnd, const RECT& client_rect) {
  if (hwnd == nullptr) {
    return nullptr;
  }
  const int width = std::max(1, static_cast<int>(client_rect.right - client_rect.left));
  const int height = std::max(1, static_cast<int>(client_rect.bottom - client_rect.top));
  HDC window_dc = GetDC(hwnd);
  if (window_dc == nullptr) {
    return nullptr;
  }
  HDC memory_dc = CreateCompatibleDC(window_dc);
  if (memory_dc == nullptr) {
    ReleaseDC(hwnd, window_dc);
    return nullptr;
  }
  HBITMAP bitmap = CreateCompatibleBitmap(window_dc, width, height);
  if (bitmap == nullptr) {
    DeleteDC(memory_dc);
    ReleaseDC(hwnd, window_dc);
    return nullptr;
  }
  HGDIOBJ previous = SelectObject(memory_dc, bitmap);
  BitBlt(
      memory_dc,
      0,
      0,
      width,
      height,
      window_dc,
      client_rect.left,
      client_rect.top,
      SRCCOPY);
  SelectObject(memory_dc, previous);
  DeleteDC(memory_dc);
  ReleaseDC(hwnd, window_dc);
  return bitmap;
}

bool UpdateDragPreviewWindowBitmap(HWND preview, HBITMAP bitmap, const SIZE& size, POINT screen_position) {
  if (preview == nullptr || bitmap == nullptr || size.cx <= 0 || size.cy <= 0) {
    return false;
  }

  HDC screen_dc = GetDC(nullptr);
  if (screen_dc == nullptr) {
    return false;
  }
  HDC memory_dc = CreateCompatibleDC(screen_dc);
  if (memory_dc == nullptr) {
    ReleaseDC(nullptr, screen_dc);
    return false;
  }

  HGDIOBJ previous = SelectObject(memory_dc, bitmap);
  POINT source_point{0, 0};
  BLENDFUNCTION blend{};
  blend.BlendOp = AC_SRC_OVER;
  blend.SourceConstantAlpha = 255;

  const BOOL updated = UpdateLayeredWindow(
      preview,
      screen_dc,
      &screen_position,
      const_cast<SIZE*>(&size),
      memory_dc,
      &source_point,
      0,
      &blend,
      ULW_OPAQUE);

  SelectObject(memory_dc, previous);
  DeleteDC(memory_dc);
  ReleaseDC(nullptr, screen_dc);
  return updated == TRUE;
}

HWND CreateDragPreviewWindow(HWND owner, HBITMAP bitmap, const SIZE& size, POINT preview_offset) {
  if (bitmap == nullptr || size.cx <= 0 || size.cy <= 0) {
    return nullptr;
  }

  POINT cursor{};
  GetCursorPos(&cursor);
  POINT screen_position{cursor.x + preview_offset.x, cursor.y + preview_offset.y};
  HWND preview = CreateWindowExW(
      WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_LAYERED,
      L"STATIC",
      nullptr,
      WS_POPUP,
      screen_position.x,
      screen_position.y,
      size.cx,
      size.cy,
      owner,
      nullptr,
      GetModuleHandleW(nullptr),
      nullptr);
  if (preview == nullptr) {
    return nullptr;
  }

  if (!UpdateDragPreviewWindowBitmap(preview, bitmap, size, screen_position)) {
    DestroyWindow(preview);
    return nullptr;
  }

  ShowWindow(preview, SW_SHOWNOACTIVATE);
  return preview;
}

std::wstring DragPreviewTitle(const core::store::ClipSummary& summary, const std::vector<std::wstring>& paths) {
  if (!paths.empty()) {
    std::filesystem::path first_path(paths.front());
    std::wstring name = first_path.filename().wstring();
    if (!name.empty()) {
      if (paths.size() > 1) {
        name += L" +" + std::to_wstring(paths.size() - 1);
      }
      return name;
    }
  }
  if (!summary.title.empty()) {
    return common::Utf8ToWide(summary.title).wide;
  }
  return summary.content_type == core::store::ClipContentType::Image ? L"Image" : L"File";
}

HBITMAP LoadImageThumbnailBitmap(const std::wstring& path, int target_width, int target_height) {
  if (path.empty() || target_width <= 0 || target_height <= 0) {
    return nullptr;
  }

  Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
  if (FAILED(CoCreateInstance(
          CLSID_WICImagingFactory,
          nullptr,
          CLSCTX_INPROC_SERVER,
          IID_PPV_ARGS(&factory)))) {
    return nullptr;
  }

  Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
  if (FAILED(factory->CreateDecoderFromFilename(
          path.c_str(),
          nullptr,
          GENERIC_READ,
          WICDecodeMetadataCacheOnDemand,
          &decoder))) {
    return nullptr;
  }

  Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
  if (FAILED(decoder->GetFrame(0, &frame))) {
    return nullptr;
  }

  UINT width = 0;
  UINT height = 0;
  frame->GetSize(&width, &height);
  if (width == 0 || height == 0) {
    return nullptr;
  }

  const double scale = std::min(
      static_cast<double>(target_width) / static_cast<double>(width),
      static_cast<double>(target_height) / static_cast<double>(height));
  const UINT scaled_width = std::max<UINT>(1, static_cast<UINT>(std::lround(width * scale)));
  const UINT scaled_height = std::max<UINT>(1, static_cast<UINT>(std::lround(height * scale)));

  Microsoft::WRL::ComPtr<IWICBitmapScaler> scaler;
  if (FAILED(factory->CreateBitmapScaler(&scaler))) {
    return nullptr;
  }
  if (FAILED(scaler->Initialize(frame.Get(), scaled_width, scaled_height, WICBitmapInterpolationModeFant))) {
    return nullptr;
  }

  Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
  if (FAILED(factory->CreateFormatConverter(&converter))) {
    return nullptr;
  }
  if (FAILED(converter->Initialize(
          scaler.Get(),
          GUID_WICPixelFormat32bppPBGRA,
          WICBitmapDitherTypeNone,
          nullptr,
          0.0,
          WICBitmapPaletteTypeCustom))) {
    return nullptr;
  }

  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = static_cast<LONG>(scaled_width);
  info.bmiHeader.biHeight = -static_cast<LONG>(scaled_height);
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  void* pixels = nullptr;
  HDC screen_dc = GetDC(nullptr);
  HBITMAP bitmap = CreateDIBSection(screen_dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
  ReleaseDC(nullptr, screen_dc);
  if (bitmap == nullptr || pixels == nullptr) {
    return nullptr;
  }

  const UINT stride = scaled_width * 4;
  const HRESULT copy_hr = converter->CopyPixels(nullptr, stride, stride * scaled_height, static_cast<BYTE*>(pixels));
  if (FAILED(copy_hr)) {
    DeleteObject(bitmap);
    return nullptr;
  }
  return bitmap;
}

void DrawBitmapCentered(HDC dc, HBITMAP bitmap, const RECT& rect) {
  if (dc == nullptr || bitmap == nullptr) {
    return;
  }
  BITMAP bm{};
  if (GetObject(bitmap, sizeof(bm), &bm) != sizeof(bm) || bm.bmWidth <= 0 || bm.bmHeight <= 0) {
    return;
  }
  HDC image_dc = CreateCompatibleDC(dc);
  if (image_dc == nullptr) {
    return;
  }
  HGDIOBJ previous = SelectObject(image_dc, bitmap);
  const float scale = std::min(
      static_cast<float>(rect.right - rect.left) / static_cast<float>(bm.bmWidth),
      static_cast<float>(rect.bottom - rect.top) / static_cast<float>(bm.bmHeight));
  const int draw_width = std::max(1, static_cast<int>(std::lround(bm.bmWidth * scale)));
  const int draw_height = std::max(1, static_cast<int>(std::lround(bm.bmHeight * scale)));
  const int left = rect.left + ((rect.right - rect.left) - draw_width) / 2;
  const int top = rect.top + ((rect.bottom - rect.top) - draw_height) / 2;
  SetStretchBltMode(dc, HALFTONE);
  StretchBlt(dc, left, top, draw_width, draw_height, image_dc, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
  SelectObject(image_dc, previous);
  DeleteDC(image_dc);
}

HBITMAP CreateDragPreviewCardBitmap(const core::store::ClipSummary& summary, const std::vector<std::wstring>& paths) {
  constexpr int kPreviewWidth = 240;
  constexpr int kPreviewHeight = 68;
  constexpr int kThumbSize = 40;

  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = kPreviewWidth;
  info.bmiHeader.biHeight = -kPreviewHeight;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  void* pixels = nullptr;
  HDC screen_dc = GetDC(nullptr);
  HBITMAP bitmap = CreateDIBSection(screen_dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
  if (bitmap == nullptr) {
    ReleaseDC(nullptr, screen_dc);
    return nullptr;
  }

  HDC dc = CreateCompatibleDC(screen_dc);
  ReleaseDC(nullptr, screen_dc);
  if (dc == nullptr) {
    DeleteObject(bitmap);
    return nullptr;
  }
  HGDIOBJ previous_bitmap = SelectObject(dc, bitmap);

  HBRUSH background = CreateSolidBrush(RGB(255, 255, 255));
  HBRUSH panel = CreateSolidBrush(RGB(248, 250, 252));
  HPEN border = CreatePen(PS_SOLID, 1, RGB(209, 213, 219));
  RECT full_rect{0, 0, kPreviewWidth, kPreviewHeight};
  FillRect(dc, &full_rect, background);
  HGDIOBJ previous_brush = SelectObject(dc, panel);
  HGDIOBJ previous_pen = SelectObject(dc, border);
  RoundRect(dc, 0, 0, kPreviewWidth, kPreviewHeight, 14, 14);

  RECT thumb_rect{10, 14, 10 + kThumbSize, 14 + kThumbSize};
  HBRUSH thumb_bg = CreateSolidBrush(RGB(239, 242, 247));
  FillRect(dc, &thumb_rect, thumb_bg);

  HBITMAP thumb_bitmap = nullptr;
  if (summary.content_type == core::store::ClipContentType::Image && !paths.empty()) {
    thumb_bitmap = LoadImageThumbnailBitmap(paths.front(), kThumbSize, kThumbSize);
  }
  if (thumb_bitmap != nullptr) {
    DrawBitmapCentered(dc, thumb_bitmap, thumb_rect);
    DeleteObject(thumb_bitmap);
  } else {
    std::wstring icon_path;
    if (!paths.empty()) {
      icon_path = paths.front();
    } else if (!summary.source_app_path.empty()) {
      icon_path = summary.source_app_path;
    }
    SHFILEINFOW file_info{};
    if (!icon_path.empty() &&
        SHGetFileInfoW(icon_path.c_str(), 0, &file_info, sizeof(file_info), SHGFI_ICON | SHGFI_SMALLICON)) {
      DrawIconEx(dc, thumb_rect.left + 4, thumb_rect.top + 4, file_info.hIcon, 32, 32, 0, nullptr, DI_NORMAL);
      DestroyIcon(file_info.hIcon);
    }
  }

  const std::wstring title = DragPreviewTitle(summary, paths);
  HFONT font = CreateFontW(
      -18,
      0,
      0,
      0,
      FW_SEMIBOLD,
      FALSE,
      FALSE,
      FALSE,
      DEFAULT_CHARSET,
      OUT_DEFAULT_PRECIS,
      CLIP_DEFAULT_PRECIS,
      CLEARTYPE_QUALITY,
      DEFAULT_PITCH | FF_DONTCARE,
      L"Segoe UI");
  HGDIOBJ previous_font = SelectObject(dc, font);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, RGB(17, 24, 39));
  RECT text_rect{64, 0, kPreviewWidth - 12, kPreviewHeight};
  DrawTextW(dc, title.c_str(), static_cast<int>(title.size()), &text_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

  SelectObject(dc, previous_font);
  SelectObject(dc, previous_pen);
  SelectObject(dc, previous_brush);
  SelectObject(dc, previous_bitmap);
  DeleteObject(font);
  DeleteObject(thumb_bg);
  DeleteObject(border);
  DeleteObject(panel);
  DeleteObject(background);
  DeleteDC(dc);
  return bitmap;
}

std::wstring SanitizeFileName(std::wstring text) {
  if (text.empty()) {
    return L"clip";
  }
  for (auto& ch : text) {
    switch (ch) {
      case L'\\':
      case L'/':
      case L':':
      case L'*':
      case L'?':
      case L'"':
      case L'<':
      case L'>':
      case L'|':
        ch = L'_';
        break;
      default:
        break;
    }
  }
  text = common::TrimWide(text);
  return text.empty() ? std::wstring(L"clip") : text;
}

clipd2d::ui::PanelLayout ComputeLayout(int client_width, int client_height) {
  clipd2d::ui::PanelLayout layout{};
  layout.client = RECT{0, 0, client_width, client_height};

  const int padding = kHorizontalPadding;
  layout.surface = RECT{0, 0, client_width, client_height};
  layout.header = RECT{0, kTopPadding, client_width, kTopPadding + kHeaderHeight};

  int button_right = client_width - padding;
  for (int i = 2; i >= 0; --i) {
    layout.toolbar[i] = RECT{
        button_right - kToolbarButtonSize,
        layout.header.top + (kHeaderHeight - kToolbarButtonSize) / 2,
        button_right,
        layout.header.top + (kHeaderHeight + kToolbarButtonSize) / 2};
    button_right -= kToolbarButtonSize + kToolbarGap;
  }

  layout.search_box = RECT{
      padding,
      layout.header.top + (kHeaderHeight - kToolbarButtonSize) / 2,
      layout.toolbar[0].left - 10,
      layout.header.top + (kHeaderHeight + kToolbarButtonSize) / 2};

  const int tabs_top = layout.header.bottom;
  int tab_left = padding;
  for (auto& tab : layout.tabs) {
    tab = RECT{};
  }
  for (auto& action : layout.nav_actions) {
    action = RECT{};
  }
  for (size_t i = 0; i < std::size(kVisibleTabs) && i < std::size(layout.tabs); ++i) {
    auto& tab = layout.tabs[i];
    tab = RECT{
        tab_left,
        tabs_top + (kToolbarHeight - kToolbarButtonSize) / 2,
        tab_left + kTabButtonWidth,
        tabs_top + (kToolbarHeight - kToolbarButtonSize) / 2 + kToolbarButtonSize};
    tab_left += kTabButtonWidth + kToolbarGap;
  }
  int nav_action_right = client_width - padding;
  for (int index = 1; index >= 0; --index) {
    layout.nav_actions[index] = RECT{
        nav_action_right - kToolbarButtonSize,
        tabs_top + (kToolbarHeight - kToolbarButtonSize) / 2,
        nav_action_right,
        tabs_top + (kToolbarHeight - kToolbarButtonSize) / 2 + kToolbarButtonSize};
    nav_action_right -= kToolbarButtonSize + kToolbarGap;
  }

  const int chip_gap = ShowFilterChipBarFlag() ? 4 : 0;
  layout.chips_bar = RECT{
      padding,
      tabs_top + kToolbarHeight + chip_gap,
      client_width - padding,
      tabs_top + kToolbarHeight + chip_gap + ActiveChipBarHeight()};
  layout.bottom_bar = RECT{0, client_height - kBottomBarHeight, client_width, client_height};

  layout.list = RECT{
      0,
      tabs_top + kToolbarHeight + chip_gap + ActiveChipBarHeight(),
      client_width,
      layout.bottom_bar.top};

  layout.list_top_reveal = RECT{
      layout.list.left,
      layout.list.top,
      layout.list.right,
      std::min(layout.list.bottom, layout.list.top + kBackToTopRevealHeight)};
  layout.back_to_top = RECT{
      std::max(layout.list.left + padding, layout.list.left + (client_width - kBackToTopButtonWidth) / 2),
      layout.list.top + 6,
      std::min(layout.list.right - padding, layout.list.left + (client_width + kBackToTopButtonWidth) / 2),
      std::min(layout.list.bottom, layout.list.top + 6 + kBackToTopButtonHeight)};

  const int bottom_button_width = 110;
  layout.status_left = RECT{
      padding,
      layout.bottom_bar.top + 3,
      client_width - padding * 2 - bottom_button_width - kBottomBarButtonGap,
      layout.bottom_bar.bottom - 3};
  layout.status_right = RECT{
      client_width - padding - bottom_button_width,
      layout.bottom_bar.top + 3,
      client_width - padding,
      layout.bottom_bar.bottom - 3};

  layout.scrollbar_track = RECT{
      client_width - (kScrollBarWidth + 2),
      layout.list.top + 2,
      client_width - 2,
      layout.list.bottom - 2};

  return layout;
}

bool RectContains(const RECT& rect, POINT point) {
  return point.x >= rect.left && point.x < rect.right && point.y >= rect.top && point.y < rect.bottom;
}

D2D1_RECT_F Inset(const RECT& rect, float dx, float dy) {
  return D2D1::RectF(
      static_cast<float>(rect.left) + dx,
      static_cast<float>(rect.top) + dy,
      static_cast<float>(rect.right) - dx,
      static_cast<float>(rect.bottom) - dy);
}

std::wstring ReadWindowText(HWND hwnd) {
  const int length = GetWindowTextLengthW(hwnd);
  std::wstring text(static_cast<size_t>(length) + 1, L'\0');
  GetWindowTextW(hwnd, text.data(), length + 1);
  text.resize(static_cast<size_t>(length));
  return text;
}





RECT QueueDragHandleRect(const RECT& header_rect) {
  const int top = header_rect.top + std::max(0, (kRowHeaderHeight - 18) / 2);
  return RECT{
      header_rect.left,
      top,
      header_rect.left + kQueueDragHandleWidth,
      top + 18};
}

void DrawQueueDragHandle(ID2D1RenderTarget* target, const RECT& rect, ID2D1Brush* brush) {
  if (target == nullptr || brush == nullptr) {
    return;
  }
  const float center_x_left = static_cast<float>(rect.left) + 4.0f;
  const float center_x_right = static_cast<float>(rect.left) + 9.0f;
  float center_y = static_cast<float>(rect.top) + 4.0f;
  for (int row = 0; row < 3; ++row) {
    target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(center_x_left, center_y), 1.1f, 1.1f), brush);
    target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(center_x_right, center_y), 1.1f, 1.1f), brush);
    center_y += 5.0f;
  }
}



void ComputeRequestRange(
    int item_count,
    int item_height,
    int viewport_height,
    int current_scroll,
    int target_scroll,
    float velocity,
    int* first_index,
    int* last_index,
    const vlist::HeightIndex* heights = nullptr) {
  if (first_index == nullptr || last_index == nullptr || item_count <= 0 || item_height <= 0 || viewport_height <= 0) {
    if (first_index != nullptr) {
      *first_index = 0;
    }
    if (last_index != nullptr) {
      *last_index = -1;
    }
    return;
  }

  const int delta = std::abs(target_scroll - current_scroll);
  int before_rows = 8;
  int after_rows = 16;

  if (delta >= viewport_height || std::abs(velocity) > 2000.0f) {
    before_rows = 16;
    after_rows = 32;
    if (velocity > 0.0f) after_rows = 64;
    if (velocity < 0.0f) before_rows = 64;
  } else if (delta >= item_height * 6 || std::abs(velocity) > 500.0f) {
    before_rows = 12;
    after_rows = 24;
    if (velocity > 0.0f) after_rows = 36;
    if (velocity < 0.0f) before_rows = 36;
  }

  const int request_top = std::min(current_scroll, target_scroll);
  const int request_bottom = std::max(current_scroll, target_scroll) + viewport_height;
  if (heights != nullptr && heights->Count() > 0) {
    // Variable-height aware: resolve indices through the real height index.
    *first_index = std::max(0, heights->IndexAtOffset(request_top) - before_rows);
    const int bottom_offset = std::min(request_bottom, heights->TotalHeight() - 1);
    *last_index = std::min(item_count - 1, heights->IndexAtOffset(bottom_offset) + after_rows);
  } else {
    // Estimate-based fallback (staging/cold-start when heights are unknown).
    *first_index = std::max(0, request_top / item_height - before_rows);
    *last_index = std::min(item_count - 1, request_bottom / item_height + after_rows);
  }
}



std::vector<std::wstring> TokenizeSearchQuery(const std::wstring& text) {
  std::vector<std::wstring> tokens;
  std::wstring current;
  for (wchar_t ch : text) {
    if (iswspace(ch) != 0) {
      if (!current.empty()) {
        tokens.push_back(ToLowerCopy(current));
        current.clear();
      }
    } else {
      current.push_back(ch);
    }
  }
  if (!current.empty()) {
    tokens.push_back(ToLowerCopy(current));
  }
  tokens.erase(
      std::remove_if(tokens.begin(), tokens.end(), [](const std::wstring& token) {
        return token.empty();
      }),
      tokens.end());
  return tokens;
}




D2D1_COLOR_F ParseTagColor(std::wstring_view color) {
  std::wstring value(color);
  if (value.size() == 7 && value[0] == L'#') {
    const int r = std::wcstol(value.substr(1, 2).c_str(), nullptr, 16);
    const int g = std::wcstol(value.substr(3, 2).c_str(), nullptr, 16);
    const int b = std::wcstol(value.substr(5, 2).c_str(), nullptr, 16);
    return D2D1::ColorF(
        static_cast<float>(r) / 255.0f,
        static_cast<float>(g) / 255.0f,
        static_cast<float>(b) / 255.0f,
        1.0f);
  }
  return D2D1::ColorF(0x3B82F6);
}

TimeGroupBucket GetTimeGroupBucket(std::int64_t epoch_ms) {
  const std::time_t seconds = static_cast<std::time_t>(epoch_ms / 1000);
  std::tm clip_tm{};
  _localtime64_s(&clip_tm, &seconds);

  SYSTEMTIME now{};
  GetLocalTime(&now);
  std::tm now_tm{};
  now_tm.tm_year = now.wYear - 1900;
  now_tm.tm_mon = now.wMonth - 1;
  now_tm.tm_mday = now.wDay;
  now_tm.tm_hour = 0;
  now_tm.tm_min = 0;
  now_tm.tm_sec = 0;
  now_tm.tm_isdst = -1;
  clip_tm.tm_hour = 0;
  clip_tm.tm_min = 0;
  clip_tm.tm_sec = 0;
  clip_tm.tm_isdst = -1;
  const std::time_t today = _mkgmtime64(&now_tm);
  const std::time_t clip_day = _mkgmtime64(&clip_tm);
  const auto delta_days = static_cast<int>((today - clip_day) / (24 * 60 * 60));
  if (delta_days <= 0) {
    return TimeGroupBucket::Today;
  }
  if (delta_days == 1) {
    return TimeGroupBucket::Yesterday;
  }
  if (delta_days <= 7) {
    return TimeGroupBucket::Last7Days;
  }
  return TimeGroupBucket::Older;
}

const wchar_t* TimeGroupLabel(TimeGroupBucket bucket) {
  switch (bucket) {
    case TimeGroupBucket::Today:
      return L"Today";
    case TimeGroupBucket::Yesterday:
      return L"Yesterday";
    case TimeGroupBucket::Last7Days:
      return L"Last 7 Days";
    case TimeGroupBucket::Older:
      return L"Older";
  }
  return L"Today";
}

std::uint64_t MakePageRequestKey(std::uint64_t generation, int page_index) {
  return (generation << 32) ^ static_cast<std::uint32_t>(page_index);
}

bool ShouldShowTimeGroups(core::store::ClipView view) {
  return view != core::store::ClipView::Queue;
}

const wchar_t* DisplayTimeGroupLabel(TimeGroupBucket bucket) {
  switch (bucket) {
    case TimeGroupBucket::Today:
      return L"Today";
    case TimeGroupBucket::Yesterday:
      return L"Yesterday";
    case TimeGroupBucket::Last7Days:
      return L"Last 7 Days";
    case TimeGroupBucket::Older:
      return L"Older";
  }
  return L"Today";
}


std::wstring TrimForDisplay(std::wstring text, size_t limit) {
  return common::clip_text_display::TrimForDisplay(std::move(text), limit);
}

std::wstring NormalizeDisplayText(std::string_view text) {
  return common::clip_text_display::NormalizeMultilineText(text);
}

std::wstring_view ViewLabel(core::store::ClipView view) {
  switch (view) {
    case core::store::ClipView::All:
      return strings::kTabHistory;
    case core::store::ClipView::Favorites:
      return strings::kTabFavorites;
    case core::store::ClipView::Pinned:
      return L"\u7f6e\u9876";
    case core::store::ClipView::Text:
      return strings::kFilterTypeText;
    case core::store::ClipView::Html:
      return L"HTML";
    case core::store::ClipView::Image:
      return strings::kFilterTypeImage;
    case core::store::ClipView::Files:
      return strings::kFilterTypeFiles;
    case core::store::ClipView::Group:
      return strings::kTabGroups;
    case core::store::ClipView::Frequent:
      return L"\u5e38\u7528";
    case core::store::ClipView::Queue:
      return strings::kScopeQueue;
  }
  return strings::kTabHistory;
}

std::wstring FormatTimeLabel(std::int64_t epoch_ms) {
  const std::time_t seconds = static_cast<std::time_t>(epoch_ms / 1000);
  std::tm local_tm{};
  _localtime64_s(&local_tm, &seconds);

  SYSTEMTIME now{};
  GetLocalTime(&now);
  if ((now.wYear == local_tm.tm_year + 1900) &&
      (now.wMonth == local_tm.tm_mon + 1) &&
      (now.wDay == local_tm.tm_mday)) {
    wchar_t buffer[32]{};
    swprintf_s(buffer, L"Today %02d:%02d", local_tm.tm_hour, local_tm.tm_min);
    return buffer;
  }

  wchar_t buffer[32]{};
  swprintf_s(buffer, L"%02d-%02d %02d:%02d", local_tm.tm_mon + 1, local_tm.tm_mday, local_tm.tm_hour, local_tm.tm_min);
  return buffer;
}

std::wstring FormatMetaLine(const core::store::ClipSummary& summary) {
  std::wstring text = FormatTimeLabel(summary.updated_at);
  switch (summary.content_type) {
    case core::store::ClipContentType::Text:
    case core::store::ClipContentType::Html:
    case core::store::ClipContentType::Rtf:
      if (summary.text_length > 0) {
        text += L" | ";
        text += std::to_wstring(summary.text_length);
        text += L" chars";
      }
      break;
    case core::store::ClipContentType::Image:
      if (summary.image_width > 0 && summary.image_height > 0) {
        text += L" | ";
        text += std::to_wstring(summary.image_width);
        text += L" x ";
        text += std::to_wstring(summary.image_height);
      }
      break;
    case core::store::ClipContentType::Files:
      if (summary.file_count > 0) {
        text += L" | ";
        text += std::to_wstring(summary.file_count);
        text += L" files";
      }
      break;
  }
  if (summary.storage_tier == core::store::StorageTier::Library) {
    text += L" | Library";
  }
  if (summary.is_frequent) {
    text += L" | Frequent";
  }
  return text;
}


std::wstring SummarySnippet(const core::store::ClipSummary& summary) {
  return common::clip_text_display::SummarySnippet(summary);
}


const wchar_t* ContentGlyph(core::store::ClipContentType type) {
  switch (type) {
    case core::store::ClipContentType::Text:
      return L"\xE8A5";
    case core::store::ClipContentType::Html:
      return L"\xE943";
    case core::store::ClipContentType::Image:
      return L"\xE91B";
    case core::store::ClipContentType::Files:
      return L"\xE8B7";
  }
  return L"\xE8A5";
}

std::wstring PrimaryActionId(core::store::ClipContentType type) {
  switch (type) {
    case core::store::ClipContentType::Text:
    case core::store::ClipContentType::Html:
      return L"paste-plain-text";
    case core::store::ClipContentType::Image:
      return L"open-folder";
    case core::store::ClipContentType::Files:
      return L"copy-path";
  }
  return L"paste-original";
}

std::wstring PrimaryActionGlyph(core::store::ClipContentType type) {
  switch (type) {
    case core::store::ClipContentType::Text:
    case core::store::ClipContentType::Html:
      return L"\xE8C8";
    case core::store::ClipContentType::Image:
      return L"\xE838";
    case core::store::ClipContentType::Files:
      return L"\xE8C8";
  }
  return L"\xE8C8";
}

std::int64_t NowEpochMs() {
  FILETIME file_time{};
  GetSystemTimeAsFileTime(&file_time);
  ULARGE_INTEGER ticks{};
  ticks.LowPart = file_time.dwLowDateTime;
  ticks.HighPart = file_time.dwHighDateTime;
  constexpr std::uint64_t kEpochDiff = 116444736000000000ull;
  return static_cast<std::int64_t>((ticks.QuadPart - kEpochDiff) / 10000ull);
}

void SuppressClipboardCapture() {
  common::SetGlobalClipboardSuppressUntil(NowEpochMs() + 1000);
}

bool CopyTextToClipboard(HWND owner, const std::wstring& text) {
  bool opened = false;
  for (int i = 0; i < 5; ++i) {
    if (OpenClipboard(owner)) {
      opened = true;
      break;
    }
    Sleep(10);
  }
  if (!opened) {
    return false;
  }
  const auto close = std::unique_ptr<void, void (*)(void*)>(reinterpret_cast<void*>(1), [](void*) {
    CloseClipboard();
  });

  if (!EmptyClipboard()) {
    return false;
  }

  const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
  HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
  if (memory == nullptr) {
    return false;
  }
  void* ptr = GlobalLock(memory);
  if (ptr == nullptr) {
    GlobalFree(memory);
    return false;
  }
  memcpy(ptr, text.c_str(), bytes);
  GlobalUnlock(memory);
  if (SetClipboardData(CF_UNICODETEXT, memory) == nullptr) {
    GlobalFree(memory);
    return false;
  }
  return true;
}

std::wstring DirectoryFromPath(const std::wstring& path) {
  if (path.empty()) {
    return {};
  }
  std::error_code ec;
  std::filesystem::path fs_path(path);
  if (std::filesystem::is_directory(fs_path, ec)) {
    return fs_path.wstring();
  }
  return fs_path.parent_path().wstring();
}

void OpenDirectoryPath(const std::wstring& path) {
  if (!path.empty()) {
    ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
  }
}

std::wstring FirstFilePathFromClip(const core::store::ClipRecord& clip) {
  const std::wstring wide = ToWide(clip.plain_text);
  const size_t pos = wide.find_first_of(L"\r\n");
  return common::TrimWide(pos == std::wstring::npos ? wide : wide.substr(0, pos));
}

std::vector<std::wstring> ExtractFilePathsFromText(const std::wstring& text) {
  std::vector<std::wstring> paths;
  size_t start = 0;
  while (start < text.size()) {
    const size_t end = text.find_first_of(L"\r\n", start);
    const std::wstring line = common::TrimWide(
        text.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start));
    if (!line.empty()) {
      paths.push_back(line);
    }
    if (end == std::wstring::npos) {
      break;
    }
    start = text.find_first_not_of(L"\r\n", end);
    if (start == std::wstring::npos) {
      break;
    }
  }
  return paths;
}

std::vector<std::wstring> ExtractFilePathsFromClip(const core::store::ClipRecord& clip) {
  for (const auto& format : clip.formats) {
    if (_stricmp(format.format_name.c_str(), "CF_HDROP") == 0 && !format.text_data.empty()) {
      return ExtractFilePathsFromText(common::Utf8ToWide(format.text_data).wide);
    }
  }
  return ExtractFilePathsFromText(common::Utf8ToWide(clip.plain_text).wide);
}






bool LoadSystemThumbnailPixels(
    const std::wstring& path,
    int target_width,
    int target_height,
    UINT* width,
    UINT* height,
    std::vector<std::uint8_t>* pixels) {
  if (path.empty() || target_width <= 0 || target_height <= 0 || width == nullptr || height == nullptr || pixels == nullptr) {
    return false;
  }

  *width = 0;
  *height = 0;
  pixels->clear();

  const HRESULT init_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  const bool need_uninit = SUCCEEDED(init_result);

  Microsoft::WRL::ComPtr<IShellItemImageFactory> image_factory;
  const HRESULT create_result = SHCreateItemFromParsingName(
      path.c_str(),
      nullptr,
      IID_PPV_ARGS(&image_factory));
  if (FAILED(create_result) || image_factory == nullptr) {
    if (need_uninit) {
      CoUninitialize();
    }
    return false;
  }

  SIZE requested_size{target_width, target_height};
  HBITMAP bitmap = nullptr;
  const HRESULT image_result = image_factory->GetImage(
      requested_size,
      SIIGBF_BIGGERSIZEOK | SIIGBF_RESIZETOFIT | SIIGBF_THUMBNAILONLY,
      &bitmap);
  if (FAILED(image_result) || bitmap == nullptr) {
    if (bitmap != nullptr) {
      DeleteObject(bitmap);
    }
    if (need_uninit) {
      CoUninitialize();
    }
    return false;
  }

  BITMAP info{};
  if (GetObjectW(bitmap, sizeof(info), &info) == 0 || info.bmWidth <= 0 || info.bmHeight <= 0) {
    DeleteObject(bitmap);
    if (need_uninit) {
      CoUninitialize();
    }
    return false;
  }

  *width = static_cast<UINT>(info.bmWidth);
  *height = static_cast<UINT>(info.bmHeight);

  BITMAPINFO bitmap_info{};
  bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bitmap_info.bmiHeader.biWidth = info.bmWidth;
  bitmap_info.bmiHeader.biHeight = -info.bmHeight;
  bitmap_info.bmiHeader.biPlanes = 1;
  bitmap_info.bmiHeader.biBitCount = 32;
  bitmap_info.bmiHeader.biCompression = BI_RGB;

  const size_t pixel_bytes = static_cast<size_t>(info.bmWidth) * static_cast<size_t>(info.bmHeight) * 4u;
  pixels->resize(pixel_bytes);

  HDC screen_dc = GetDC(nullptr);
  const int copied = GetDIBits(
      screen_dc,
      bitmap,
      0,
      static_cast<UINT>(info.bmHeight),
      pixels->data(),
      &bitmap_info,
      DIB_RGB_COLORS);
  ReleaseDC(nullptr, screen_dc);
  DeleteObject(bitmap);

  if (need_uninit) {
    CoUninitialize();
  }

  if (copied == 0) {
    pixels->clear();
    *width = 0;
    *height = 0;
    return false;
  }
  return true;
}

std::uint64_t TryFileSize(const std::wstring& path) {
  std::error_code error;
  const auto size = std::filesystem::file_size(path, error);
  if (error) {
    return 0;
  }
  return static_cast<std::uint64_t>(size);
}

std::wstring ThumbnailKeyFromSummary(const core::store::ClipSummary& summary) {
  if (summary.content_type == core::store::ClipContentType::Image) {
    if (!summary.thumbnail_path.empty()) {
      return summary.thumbnail_path;
    }
    return summary.storage_path;
  }
  if (summary.content_type == core::store::ClipContentType::Files) {
    if (!summary.thumbnail_path.empty()) {
      return summary.thumbnail_path;
    }
    const std::wstring first_path = FirstFilePathFromSummary(summary);
    if (SupportsSystemThumbnailPreviewPath(first_path)) {
      return first_path;
    }
  }
  return L"";
}

std::wstring FormatPanelTimeLabel(std::int64_t epoch_ms) {
  const std::time_t seconds = static_cast<std::time_t>(epoch_ms / 1000);
  std::tm local_tm{};
  _localtime64_s(&local_tm, &seconds);

  SYSTEMTIME now{};
  GetLocalTime(&now);
  if ((now.wYear == local_tm.tm_year + 1900) &&
      (now.wMonth == local_tm.tm_mon + 1) &&
      (now.wDay == local_tm.tm_mday)) {
    wchar_t buffer[32]{};
    swprintf_s(buffer, L"\x4ECA\x5929 %02d:%02d", local_tm.tm_hour, local_tm.tm_min);
    return buffer;
  }

  wchar_t buffer[32]{};
  swprintf_s(buffer, L"%02d-%02d %02d:%02d", local_tm.tm_mon + 1, local_tm.tm_mday, local_tm.tm_hour, local_tm.tm_min);
  return buffer;
}

std::wstring FormatPanelMetaLine(const core::store::ClipSummary& summary) {
  std::wstring text = FormatPanelTimeLabel(summary.updated_at);
  switch (summary.content_type) {
    case core::store::ClipContentType::Text:
    case core::store::ClipContentType::Html:
      if (summary.text_length > 0) {
        text += L" \x00B7 ";
        text += std::to_wstring(summary.text_length);
        text += L" \x5B57\x7B26";
      }
      break;
    case core::store::ClipContentType::Image:
      if (summary.image_width > 0 && summary.image_height > 0) {
        text += L" \x00B7 ";
        text += std::to_wstring(summary.image_width);
        text += L" \x00D7 ";
        text += std::to_wstring(summary.image_height);
      }
      break;
    case core::store::ClipContentType::Files:
      if (summary.file_count > 0) {
        text += L" \x00B7 ";
        text += std::to_wstring(summary.file_count);
        if (summary.file_count == 1) {
          text += L" \x4E2A\x6587\x4EF6";
        } else {
          text += L" \x9879";
        }
      }
      break;
  }
  if (summary.storage_tier == core::store::StorageTier::Library) {
    text += L" \x00B7 \x8D44\x6599\x5E93";
  }
  if (summary.is_frequent) {
    text += L" \x00B7 \x5E38\x7528";
  }
  if (!summary.source_app_name.empty()) {
    text += L" \x00B7 ";
    text += summary.source_app_name;
  }
  if (!summary.tag_names.empty()) {
    const size_t max_tags = std::min<size_t>(2, summary.tag_names.size());
    for (size_t index = 0; index < max_tags; ++index) {
      text += L" \x00B7 #";
      text += summary.tag_names[index];
    }
    if (summary.tag_names.size() > max_tags) {
      text += L" \x00B7 +";
      text += std::to_wstring(summary.tag_names.size() - max_tags);
    }
  }
  return text;
}





UINT DetectRefreshRateHz(HWND hwnd) {
  if (hwnd == nullptr) {
    return 120;
  }

  MONITORINFOEXW monitor_info{};
  monitor_info.cbSize = sizeof(monitor_info);
  const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
  if (monitor == nullptr || !GetMonitorInfoW(monitor, &monitor_info)) {
    return 120;
  }

  DEVMODEW mode{};
  mode.dmSize = sizeof(mode);
  if (!EnumDisplaySettingsW(monitor_info.szDevice, ENUM_CURRENT_SETTINGS, &mode) || mode.dmDisplayFrequency == 0) {
    return 120;
  }

  return static_cast<UINT>(std::clamp<DWORD>(mode.dmDisplayFrequency, 60, 240));
}

void SortActions(std::vector<core::actions::ActionDescriptor>* actions) {
  std::sort(actions->begin(), actions->end(), [](const auto& left, const auto& right) {
    if (left.placement != right.placement) {
      return left.placement < right.placement;
    }
    if (left.priority != right.priority) {
      return left.priority < right.priority;
    }
    return left.label < right.label;
  });
}

std::wstring ActionLabel(const core::actions::ActionDescriptor& action) {
  return ToWide(action.label);
}

common::Status LoadScaledBitmapPixels(
    const std::wstring& path,
    int max_width,
    int max_height,
    UINT* width,
    UINT* height,
    std::vector<std::uint8_t>* pixels) {
  if (width == nullptr || height == nullptr || pixels == nullptr) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "Invalid image decode output.");
  }

  *width = 0;
  *height = 0;
  pixels->clear();

  const HRESULT init_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  const bool need_uninit = SUCCEEDED(init_result);

  IWICImagingFactory* factory = nullptr;
  HRESULT hr = CoCreateInstance(
      CLSID_WICImagingFactory,
      nullptr,
      CLSCTX_INPROC_SERVER,
      IID_PPV_ARGS(&factory));
  if (FAILED(hr)) {
    if (need_uninit) {
      CoUninitialize();
    }
    return common::Status::Error(common::StatusCode::PlatformError, "Create WIC factory failed.");
  }

  IWICBitmapDecoder* decoder = nullptr;
  hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
  if (FAILED(hr)) {
    factory->Release();
    if (need_uninit) {
      CoUninitialize();
    }
    return common::Status::Error(common::StatusCode::NotFound, "Open image file failed.");
  }

  IWICBitmapFrameDecode* frame = nullptr;
  hr = decoder->GetFrame(0, &frame);
  if (FAILED(hr)) {
    decoder->Release();
    factory->Release();
    if (need_uninit) {
      CoUninitialize();
    }
    return common::Status::Error(common::StatusCode::PlatformError, "Read image frame failed.");
  }

  UINT source_width = 0;
  UINT source_height = 0;
  hr = frame->GetSize(&source_width, &source_height);
  if (FAILED(hr) || source_width == 0 || source_height == 0) {
    frame->Release();
    decoder->Release();
    factory->Release();
    if (need_uninit) {
      CoUninitialize();
    }
    return common::Status::Error(common::StatusCode::PlatformError, "Read image size failed.");
  }

  const double scale_x = max_width > 0 ? static_cast<double>(max_width) / source_width : 1.0;
  const double scale_y = max_height > 0 ? static_cast<double>(max_height) / source_height : 1.0;
  const double scale = std::min(1.0, std::min(scale_x, scale_y));
  *width = std::max<UINT>(1, static_cast<UINT>(std::round(source_width * scale)));
  *height = std::max<UINT>(1, static_cast<UINT>(std::round(source_height * scale)));

  IWICBitmapScaler* scaler = nullptr;
  hr = factory->CreateBitmapScaler(&scaler);
  if (SUCCEEDED(hr)) {
    hr = scaler->Initialize(frame, *width, *height, WICBitmapInterpolationModeFant);
  }

  IWICFormatConverter* converter = nullptr;
  if (SUCCEEDED(hr)) {
    hr = factory->CreateFormatConverter(&converter);
  }
  if (SUCCEEDED(hr)) {
    hr = converter->Initialize(
        scaler,
        GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0,
        WICBitmapPaletteTypeCustom);
  }

  if (FAILED(hr)) {
    if (converter != nullptr) {
      converter->Release();
    }
    if (scaler != nullptr) {
      scaler->Release();
    }
    frame->Release();
    decoder->Release();
    factory->Release();
    if (need_uninit) {
      CoUninitialize();
    }
    return common::Status::Error(common::StatusCode::PlatformError, "Convert image pixels failed.");
  }

  const UINT stride = *width * 4;
  pixels->resize(static_cast<size_t>(stride) * static_cast<size_t>(*height));
  hr = converter->CopyPixels(nullptr, stride, static_cast<UINT>(pixels->size()), pixels->data());

  converter->Release();
  scaler->Release();
  frame->Release();
  decoder->Release();
  factory->Release();
  if (need_uninit) {
    CoUninitialize();
  }

  if (FAILED(hr)) {
    pixels->clear();
    *width = 0;
    *height = 0;
    return common::Status::Error(common::StatusCode::PlatformError, "Copy image pixels failed.");
  }

  return common::Status::Ok();
}

MainWindow* g_hook_owner = nullptr;

}  // namespace

}  // namespace clipd2d::ui
