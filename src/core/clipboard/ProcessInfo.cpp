#include "core/clipboard/ProcessInfo.h"

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

#include "common/Utf8.h"

namespace clipd2d::core::clipboard {

std::wstring GetProcessPath(HWND window) {
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
  return path;
}

std::wstring BaseName(std::wstring_view path) {
  const size_t pos = path.find_last_of(L"\\/");
  if (pos == std::wstring_view::npos) {
    return std::wstring(path);
  }
  return std::wstring(path.substr(pos + 1));
}

std::wstring WindowTitle(HWND window) {
  std::wstring title(512, L'\0');
  const int length = GetWindowTextW(window, title.data(), static_cast<int>(title.size()));
  if (length <= 0) {
    return {};
  }
  title.resize(length);
  return title;
}

}  // namespace clipd2d::core::clipboard
