#include "common/Log.h"

#include <windows.h>

#include <array>
#include <cstdio>
#include <mutex>
#include <string>

namespace clipd2d::common {

namespace {

const char* ToLevelName(LogLevel level) {
  switch (level) {
    case LogLevel::Info:
      return "info";
    case LogLevel::Warning:
      return "warning";
    case LogLevel::Error:
      return "error";
  }

  return "unknown";
}

std::string BuildTimestamp() {
  SYSTEMTIME local_time{};
  GetLocalTime(&local_time);

  char buffer[64]{};
  snprintf(
      buffer,
      sizeof(buffer),
      "%04u-%02u-%02u %02u:%02u:%02u.%03u",
      static_cast<unsigned>(local_time.wYear),
      static_cast<unsigned>(local_time.wMonth),
      static_cast<unsigned>(local_time.wDay),
      static_cast<unsigned>(local_time.wHour),
      static_cast<unsigned>(local_time.wMinute),
      static_cast<unsigned>(local_time.wSecond),
      static_cast<unsigned>(local_time.wMilliseconds));
  return buffer;
}

std::wstring ResolveLogPath() {
  std::array<wchar_t, 32767> buffer{};

  // 允许重定向日志目录。测试进程必须用它把自己的日志写到临时目录：否则
  // scraps_tests.exe 在临时库里"故意制造损坏"的用例会把 "file is not a database"
  // 混进生产日志——曾据此误判「数据库一天损坏 185 次、自愈反复回退导致条目跳顶」，
  // 实际每一条都来自测试。日志分析的可信度取决于它只记录真实运行。
  const DWORD override_length =
      GetEnvironmentVariableW(L"SCRAPS_LOG_DIR", buffer.data(), static_cast<DWORD>(buffer.size()));
  if (override_length > 0 && override_length < buffer.size()) {
    std::wstring override_directory(buffer.data(), override_length);
    CreateDirectoryW(override_directory.c_str(), nullptr);
    return override_directory + L"\\debug.log";
  }

  const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer.data(), static_cast<DWORD>(buffer.size()));

  std::wstring base_directory;
  if (length > 0 && length < buffer.size()) {
    base_directory.assign(buffer.data(), length);
  } else {
    const DWORD temp_length = GetTempPathW(static_cast<DWORD>(buffer.size()), buffer.data());
    if (temp_length == 0 || temp_length >= buffer.size()) {
      return L"Scraps.log";
    }
    base_directory.assign(buffer.data(), temp_length);
    if (!base_directory.empty() && base_directory.back() == L'\\') {
      base_directory.pop_back();
    }
  }

  const std::wstring log_directory = base_directory + L"\\Scraps";
  CreateDirectoryW(log_directory.c_str(), nullptr);
  return log_directory + L"\\debug.log";
}

void AppendLogLine(std::string_view line) {
  static std::mutex mutex;
  static const std::wstring log_path = ResolveLogPath();

  std::scoped_lock lock(mutex);
  HANDLE file = CreateFileW(
      log_path.c_str(),
      FILE_APPEND_DATA,
      FILE_SHARE_READ | FILE_SHARE_WRITE,
      nullptr,
      OPEN_ALWAYS,
      FILE_ATTRIBUTE_NORMAL,
      nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return;
  }

  DWORD bytes_written = 0;
  WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &bytes_written, nullptr);
  CloseHandle(file);
}

}  // namespace

void WriteLog(LogLevel level, std::string_view category, std::string_view message) {
  std::string line;
  line.reserve(category.size() + message.size() + 64);
  line.append(BuildTimestamp());
  line.push_back(' ');
  line.push_back('[');
  line.append(ToLevelName(level));
  line.append("] ");
  line.append(category);
  line.append(": ");
  line.append(message);
  line.push_back('\n');

  OutputDebugStringA(line.c_str());
  AppendLogLine(line);
}

}  // namespace clipd2d::common
