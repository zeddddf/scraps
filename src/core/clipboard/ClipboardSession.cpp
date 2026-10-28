#include "core/clipboard/ClipboardSession.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <cstring>

#include "common/StringUtil.h"
#include "common/Utf8.h"
#include "core/clipboard/ClipboardFormat.h"

namespace clipd2d::core::clipboard {

namespace {

ClipboardSessionProbe g_clipboard_session_probe = nullptr;

}  // namespace

ClipboardSessionProbe& ClipboardScope::session_probe() {
  return g_clipboard_session_probe;
}

void SetClipboardSessionProbe(ClipboardSessionProbe probe) {
  g_clipboard_session_probe = probe;
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

namespace {

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

}  // namespace

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

}  // namespace clipd2d::core::clipboard
