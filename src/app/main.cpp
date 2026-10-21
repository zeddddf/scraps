#include "app/Application.h"

#include <DbgHelp.h>
#include <windows.h>

#include <atomic>
#include <exception>
#include <string>

#include "common/Log.h"

namespace {

std::atomic<bool> g_crash_dump_in_progress = false;

std::wstring ResolveCrashDirectory() {
  wchar_t buffer[MAX_PATH]{};
  const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, static_cast<DWORD>(std::size(buffer)));
  std::wstring root;
  if (length > 0 && length < std::size(buffer)) {
    root.assign(buffer, length);
  } else {
    const DWORD temp_length = GetTempPathW(static_cast<DWORD>(std::size(buffer)), buffer);
    if (temp_length == 0 || temp_length >= std::size(buffer)) {
      return L".\\crash";
    }
    root.assign(buffer, temp_length);
    if (!root.empty() && (root.back() == L'\\' || root.back() == L'/')) {
      root.pop_back();
    }
  }

  const std::wstring app_dir = root + L"\\Scraps";
  CreateDirectoryW(app_dir.c_str(), nullptr);
  const std::wstring crash_dir = app_dir + L"\\crash";
  CreateDirectoryW(crash_dir.c_str(), nullptr);
  return crash_dir;
}

std::wstring BuildCrashFileBase() {
  SYSTEMTIME local_time{};
  GetLocalTime(&local_time);
  wchar_t filename[128]{};
  swprintf_s(
      filename,
      L"crash-%04u%02u%02u-%02u%02u%02u-%03u",
      static_cast<unsigned>(local_time.wYear),
      static_cast<unsigned>(local_time.wMonth),
      static_cast<unsigned>(local_time.wDay),
      static_cast<unsigned>(local_time.wHour),
      static_cast<unsigned>(local_time.wMinute),
      static_cast<unsigned>(local_time.wSecond),
      static_cast<unsigned>(local_time.wMilliseconds));
  return ResolveCrashDirectory() + L"\\" + filename;
}

void WriteCrashNote(const std::wstring& path, const std::wstring& note) {
  HANDLE file = CreateFileW(
      path.c_str(),
      GENERIC_WRITE,
      FILE_SHARE_READ,
      nullptr,
      CREATE_ALWAYS,
      FILE_ATTRIBUTE_NORMAL,
      nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return;
  }
  DWORD bytes_written = 0;
  WriteFile(file, note.data(), static_cast<DWORD>(note.size() * sizeof(wchar_t)), &bytes_written, nullptr);
  CloseHandle(file);
}

void WriteCrashDump(EXCEPTION_POINTERS* exception_pointers, const wchar_t* reason) {
  bool expected = false;
  if (!g_crash_dump_in_progress.compare_exchange_strong(expected, true)) {
    return;
  }

  const std::wstring base = BuildCrashFileBase();
  const std::wstring dump_path = base + L".dmp";
  const std::wstring note_path = base + L".txt";

  HANDLE dump_file = CreateFileW(
      dump_path.c_str(),
      GENERIC_WRITE,
      FILE_SHARE_READ,
      nullptr,
      CREATE_ALWAYS,
      FILE_ATTRIBUTE_NORMAL,
      nullptr);
  if (dump_file != INVALID_HANDLE_VALUE) {
    MINIDUMP_EXCEPTION_INFORMATION exception_info{};
    MINIDUMP_EXCEPTION_INFORMATION* exception_info_ptr = nullptr;
    if (exception_pointers != nullptr) {
      exception_info.ThreadId = GetCurrentThreadId();
      exception_info.ExceptionPointers = exception_pointers;
      exception_info.ClientPointers = FALSE;
      exception_info_ptr = &exception_info;
    }
    MiniDumpWriteDump(
        GetCurrentProcess(),
        GetCurrentProcessId(),
        dump_file,
        static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithThreadInfo),
        exception_info_ptr,
        nullptr,
        nullptr);
    CloseHandle(dump_file);
  }

  std::wstring note = L"Scraps crash capture\r\n";
  note += L"Reason: ";
  note += reason;
  note += L"\r\nProcessId: " + std::to_wstring(GetCurrentProcessId());
  note += L"\r\nThreadId: " + std::to_wstring(GetCurrentThreadId());
  if (exception_pointers != nullptr && exception_pointers->ExceptionRecord != nullptr) {
    note += L"\r\nExceptionCode: 0x" + std::to_wstring(exception_pointers->ExceptionRecord->ExceptionCode);
  }
  note += L"\r\nDump: " + dump_path;
  WriteCrashNote(note_path, note);
}

LONG WINAPI ScrapsUnhandledExceptionFilter(EXCEPTION_POINTERS* exception_pointers) {
  WriteCrashDump(exception_pointers, L"unhandled_exception");
  return EXCEPTION_EXECUTE_HANDLER;
}

void ScrapsTerminateHandler() {
  WriteCrashDump(nullptr, L"std_terminate");
  TerminateProcess(GetCurrentProcess(), 3);
}

void InstallCrashHandlers() {
  SetUnhandledExceptionFilter(&ScrapsUnhandledExceptionFilter);
  std::set_terminate(&ScrapsTerminateHandler);
  clipd2d::common::WriteLog(clipd2d::common::LogLevel::Info, "crash.handler", "Installed unhandled exception handlers.");
}

int RunScraps(HINSTANCE instance, int n_cmd_show) {
  clipd2d::app::Application application;
  const auto status = application.Initialize(instance, n_cmd_show);
  if (!status.ok()) {
    MessageBoxA(nullptr, status.message.c_str(), "Scraps", MB_ICONERROR | MB_OK);
    return 1;
  }

  const int exit_code = application.Run();
  application.Shutdown();
  return exit_code;
}

}  // namespace

int APIENTRY wWinMain(
    HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    int n_cmd_show) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  InstallCrashHandlers();
  __try {
    return RunScraps(instance, n_cmd_show);
  } __except (ScrapsUnhandledExceptionFilter(GetExceptionInformation())) {
    return 1;
  }
}
