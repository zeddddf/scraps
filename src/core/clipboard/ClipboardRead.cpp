#include "core/clipboard/ClipboardRead.h"

#include <windows.h>
#include <shellapi.h>
#include <wincodec.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "core/policies/ClipboardRetryPolicy.h"
#include "common/Utf8.h"

namespace clipd2d::core::clipboard {

std::vector<std::uint8_t> ReadGlobalBytes(HGLOBAL handle) {
  if (handle == nullptr) {
    return {};
  }
  const SIZE_T size = GlobalSize(handle);
  if (size == 0) {
    return {};
  }
  const auto* bytes = static_cast<const std::uint8_t*>(GlobalLock(handle));
  if (bytes == nullptr) {
    return {};
  }
  std::vector<std::uint8_t> copy(bytes, bytes + size);
  GlobalUnlock(handle);
  return copy;
}

std::vector<std::wstring> ReadFiles(HDROP drop) {
  std::vector<std::wstring> files;
  const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
  for (UINT index = 0; index < count; ++index) {
    std::wstring path(4096, L'\0');
    const UINT written = DragQueryFileW(drop, index, path.data(), static_cast<UINT>(path.size()));
    path.resize(written);
    files.push_back(std::move(path));
  }
  return files;
}

ReadGlobalTextResult ReadGlobalText(HGLOBAL handle) {
  ReadGlobalTextResult result;
  if (handle == nullptr) {
    result.status = common::Status::Error(common::StatusCode::Unavailable, "CF_UNICODETEXT handle is null.");
    return result;
  }
  const SIZE_T size = GlobalSize(handle);
  if (size == 0) {
    result.status = common::Status::Error(common::StatusCode::Unavailable, "CF_UNICODETEXT memory block is empty.");
    return result;
  }
  const wchar_t* text = static_cast<const wchar_t*>(GlobalLock(handle));
  if (text == nullptr) {
    result.status = common::Status::Error(common::StatusCode::Unavailable, "GlobalLock failed for CF_UNICODETEXT.");
    return result;
  }
  // CONSTRAINT: the clipboard payload may come from a hostile process and might
  // not be NUL-terminated - never scan past the HGLOBAL block size.
  const size_t max_chars = static_cast<size_t>(size / sizeof(wchar_t));
  size_t length = 0;
  while (length < max_chars && text[length] != L'\0') {
    ++length;
  }
  const std::wstring wide_text(text, length);
  GlobalUnlock(handle);
  const common::Utf8Conversion utf8 = common::WideToUtf8(wide_text);
  if (!utf8.status.ok()) {
    result.status = utf8.status;
    return result;
  }
  result.text = utf8.utf8;
  return result;
}

ReadGlobalTextResult ReadAnsiGlobal(HGLOBAL handle) {
  ReadGlobalTextResult result;
  if (handle == nullptr) {
    result.status = common::Status::Error(common::StatusCode::Unavailable, "ANSI clipboard handle is null.");
    return result;
  }
  const SIZE_T size = GlobalSize(handle);
  if (size == 0) {
    result.status = common::Status::Error(common::StatusCode::Unavailable, "ANSI clipboard memory block is empty.");
    return result;
  }
  const char* text = static_cast<const char*>(GlobalLock(handle));
  if (text == nullptr) {
    result.status = common::Status::Error(common::StatusCode::Unavailable, "GlobalLock failed for ANSI clipboard text.");
    return result;
  }
  // CONSTRAINT: the clipboard payload may come from a hostile process and might
  // not be NUL-terminated - never scan past the HGLOBAL block size.
  size_t length = 0;
  while (length < static_cast<size_t>(size) && text[length] != '\0') {
    ++length;
  }
  result.text.assign(text, length);
  GlobalUnlock(handle);
  return result;
}

}  // namespace clipd2d::core::clipboard
