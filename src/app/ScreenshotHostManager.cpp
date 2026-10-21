#include "app/ScreenshotHostManager.h"
#include "common/Log.h"
#include "ipc/PipeSecurity.h"
#include <nlohmann/json.hpp>
#include <array>
#include <cstdint>
#include <exception>
#include <filesystem>

namespace clipd2d::app {

namespace {

// S2：payload 只是被排空防阻塞客户端写入，不做整块保留。
// 硬上限 64MB，超限按协议违规断开管道。
constexpr std::uint64_t kMaxDrainedPayloadBytes = 64ull * 1024 * 1024;
constexpr DWORD kDrainChunkBytes = 64 * 1024;

// 分段排空：小块循环读取并丢弃，处理消息模式的 ERROR_MORE_DATA。
void DrainPipePayload(HANDLE pipe, std::uint64_t payload_size, const std::atomic<bool>& running) {
  std::array<char, kDrainChunkBytes> chunk{};
  std::uint64_t remaining = payload_size;
  while (remaining > 0 && running.load()) {
    const std::uint64_t capped =
        remaining < static_cast<std::uint64_t>(chunk.size()) ? remaining : static_cast<std::uint64_t>(chunk.size());
    const DWORD bytes_to_read = static_cast<DWORD>(capped);
    DWORD bytes_read = 0;
    const BOOL read_ok = ReadFile(pipe, chunk.data(), bytes_to_read, &bytes_read, nullptr);
    if (!read_ok && GetLastError() != ERROR_MORE_DATA) {
      break;
    }
    if (bytes_read == 0) {
      break;
    }
    remaining -= bytes_read;
  }
}

}  // namespace

ScreenshotHostManager::~ScreenshotHostManager() {
  Shutdown();
}

void ScreenshotHostManager::Initialize(const std::wstring& root_dir, bool prewarm_enabled) {
  root_dir_ = root_dir;
  prewarm_enabled_ = prewarm_enabled;
  pipe_name_ = L"\\\\.\\pipe\\Scraps_Screenshot_" + std::to_wstring(GetCurrentProcessId());

  // Resolve screenshot path
  wchar_t exe_path[MAX_PATH]{};
  GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
  std::wstring exe_dir = exe_path;
  size_t last_slash = exe_dir.find_last_of(L"\\/");
  if (last_slash != std::wstring::npos) {
    exe_dir = exe_dir.substr(0, last_slash);
  }
  screenshot_exe_path_ = exe_dir + L"\\scraps_screenshot.exe";

  running_ = true;

  if (prewarm_enabled_) {
    // Start the process inside the listener thread so startup is not blocked
    // on screenshot exe launch; only listen for results when the process is up.
    listener_thread_ = std::thread([this]() {
      // U3：线程入口兜住异常，避免未捕获异常导致 std::terminate 全进程退出。
      try {
        if (StartProcess()) {
          PipeListenerLoop();
        }
      } catch (const std::exception& e) {
        common::WriteLog(
            common::LogLevel::Error,
            "screenshot.host",
            std::string("Screenshot listener thread exception: ") + e.what());
      } catch (...) {
        common::WriteLog(
            common::LogLevel::Error,
            "screenshot.host",
            "Screenshot listener thread unknown exception.");
      }
    });
  }
}

void ScreenshotHostManager::TriggerCapture() {
  if (!prewarm_enabled_) {
    common::WriteLog(common::LogLevel::Info, "screenshot.host", "TriggerCapture: Launching screenshot app on-demand.");
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"\"" + screenshot_exe_path_ + L"\" --mode capture";
    BOOL ok = CreateProcessW(
        nullptr,
        const_cast<wchar_t*>(cmd.c_str()),
        nullptr, nullptr, FALSE,
        0, nullptr, nullptr, &si, &pi
    );
    if (ok) {
      CloseHandle(pi.hProcess);
      CloseHandle(pi.hThread);
    } else {
      common::WriteLog(common::LogLevel::Error, "screenshot.host", "TriggerCapture: Failed to launch screenshot app on-demand.");
    }
    return;
  }

  common::WriteLog(common::LogLevel::Info, "screenshot.host", "TriggerCapture called. Sending wake_up command...");
  SendIpcCommand("{\"command\": \"wake_up\"}");
}

void ScreenshotHostManager::Shutdown() {
  running_ = false;
  StopProcess();

  if (listener_thread_.joinable()) {
    listener_thread_.join();
  }
}

bool ScreenshotHostManager::StartProcess() {
  std::scoped_lock lock(mutex_);
  if (child_process_ != nullptr) return true;

  if (pipe_handle_ == INVALID_HANDLE_VALUE) {
    // U2：管道 DACL 仅当前用户 + SYSTEM/Administrators，拒绝远程客户端。
    ipc::ScopedSecurityAttributes pipe_security(ipc::DaclScope::kUserWithSystemAdmins);
    if (!pipe_security.ok()) {
      common::WriteLog(
          common::LogLevel::Error,
          "screenshot.host",
          "构建截图管道安全描述符失败，退回默认 DACL（仍拒绝远程客户端）。");
    }
    pipe_handle_ = CreateNamedPipeW(
        pipe_name_.c_str(),
        PIPE_ACCESS_DUPLEX | FILE_FLAG_FIRST_PIPE_INSTANCE | PIPE_REJECT_REMOTE_CLIENTS,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        1,
        1024 * 1024,
        1024 * 1024,
        0,
        pipe_security.get()
    );
    if (pipe_handle_ == INVALID_HANDLE_VALUE) {
      common::WriteLog(common::LogLevel::Error, "screenshot.host", "CreateNamedPipe failed.");
      return false;
    }
  }

  std::wstring cmd = L"\"" + screenshot_exe_path_ + L"\" --mode standby --pipe \"" + pipe_name_ + L"\"";
  STARTUPINFOW si{};
  si.cb = sizeof(si);
  PROCESS_INFORMATION pi{};
  BOOL ok = CreateProcessW(
      nullptr,
      const_cast<wchar_t*>(cmd.c_str()),
      nullptr, nullptr, FALSE,
      0, nullptr, nullptr, &si, &pi
  );
  if (!ok) {
    common::WriteLog(common::LogLevel::Error, "screenshot.host", "CreateProcess for screenshot failed.");
    return false;
  }

  child_process_ = pi.hProcess;
  child_thread_ = pi.hThread;
  
  common::WriteLog(common::LogLevel::Info, "screenshot.host", "Screenshot process spawned in standby mode.");
  return true;
}

void ScreenshotHostManager::StopProcess() {
  std::scoped_lock lock(mutex_);
  if (child_process_ != nullptr) {
    TerminateProcess(child_process_, 0);
    CloseHandle(child_process_);
    child_process_ = nullptr;
  }
  if (child_thread_ != nullptr) {
    CloseHandle(child_thread_);
    child_thread_ = nullptr;
  }
  if (pipe_handle_ != INVALID_HANDLE_VALUE) {
    DisconnectNamedPipe(pipe_handle_);
    CloseHandle(pipe_handle_);
    pipe_handle_ = INVALID_HANDLE_VALUE;
  }
}

void ScreenshotHostManager::PipeListenerLoop() {
  bool pipe_connected = false;

  while (running_) {
    HANDLE pipe = INVALID_HANDLE_VALUE;
    HANDLE child_proc = nullptr;
    {
      std::scoped_lock lock(mutex_);
      pipe = pipe_handle_;
      child_proc = child_process_;
    }

    if (pipe == INVALID_HANDLE_VALUE || child_proc == nullptr) {
      Sleep(100);
      continue;
    }

    if (!pipe_connected) {
      BOOL connected = ConnectNamedPipe(pipe, nullptr);
      if (connected || GetLastError() == ERROR_PIPE_CONNECTED) {
        pipe_connected = true;
        common::WriteLog(common::LogLevel::Info, "screenshot.host", "Screenshot client connected to pipe.");
      } else {
        Sleep(100);
        continue;
      }
    }

    // Check if there is data to read without blocking
    DWORD bytes_avail = 0;
    if (PeekNamedPipe(pipe, nullptr, 0, nullptr, &bytes_avail, nullptr)) {
      if (bytes_avail > 0) {
        std::vector<char> buffer(4096);
        DWORD bytes_read = 0;
        BOOL read_ok = ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes_read, nullptr);
        if (read_ok && bytes_read > 0) {
          std::string json_str(buffer.data(), bytes_read);
          try {
            auto j = nlohmann::json::parse(json_str);
            std::string type = j.value("type", "");
            if (type == "capture.completed") {
              // S2：payloadSize 来自管道 JSON（对端可控），只允许无符号/正整数。
              std::uint64_t payload_size = 0;
              if (j.contains("image") && j["image"].is_object()) {
                const auto& image = j["image"];
                if (image.contains("payloadSize")) {
                  const auto& size_node = image["payloadSize"];
                  if (size_node.is_number_unsigned()) {
                    payload_size = size_node.get<std::uint64_t>();
                  } else if (size_node.is_number_integer()) {
                    const std::int64_t signed_size = size_node.get<std::int64_t>();
                    if (signed_size > 0) {
                      payload_size = static_cast<std::uint64_t>(signed_size);
                    }
                  }
                }
              }
              if (payload_size > kMaxDrainedPayloadBytes) {
                // S2：超限视为协议违规，记录日志并断开管道（丢弃未读数据）。
                common::WriteLog(
                    common::LogLevel::Error,
                    "screenshot.host",
                    "payloadSize " + std::to_string(payload_size) +
                        " exceeds hard limit " + std::to_string(kMaxDrainedPayloadBytes) +
                        "; disconnecting pipe.");
                DisconnectNamedPipe(pipe);
                pipe_connected = false;
              } else if (payload_size > 0) {
                // S2：分段排空丢弃，防客户端 WriteFile 阻塞，不做整块分配。
                DrainPipePayload(pipe, payload_size, running_);
              }
              common::WriteLog(common::LogLevel::Info, "screenshot.host", "Capture completed IPC received.");
            } else if (type == "capture.canceled") {
              common::WriteLog(common::LogLevel::Info, "screenshot.host", "Capture canceled IPC received.");
            } else if (type == "capture.failed") {
              common::WriteLog(common::LogLevel::Warning, "screenshot.host", "Capture failed IPC received.");
            }
          } catch (const nlohmann::json::parse_error& e) {
            common::WriteLog(
                common::LogLevel::Warning,
                "screenshot.host",
                std::string("Screenshot IPC JSON parse error: ") + e.what());
          } catch (const nlohmann::json::exception& e) {
            common::WriteLog(
                common::LogLevel::Warning,
                "screenshot.host",
                std::string("Screenshot IPC JSON error: ") + e.what());
          } catch (const std::exception& e) {
            common::WriteLog(
                common::LogLevel::Error,
                "screenshot.host",
                std::string("Screenshot IPC handling error: ") + e.what());
          } catch (...) {
            common::WriteLog(
                common::LogLevel::Error,
                "screenshot.host",
                "Screenshot IPC handling unknown error.");
          }
        }
      }
    } else {
      DWORD err = GetLastError();
      if (err == ERROR_BROKEN_PIPE || err == ERROR_PIPE_NOT_CONNECTED) {
        pipe_connected = false;
      }
    }

    // Monitor child process
    DWORD wait_result = WaitForSingleObject(child_proc, 50);
    if (wait_result == WAIT_OBJECT_0) {
      common::WriteLog(common::LogLevel::Warning, "screenshot.host", "Screenshot process exited unexpectedly. Re-initializing standby process...");
      
      bool should_respawn = false;
      {
        std::scoped_lock lock(mutex_);
        should_respawn = running_;
      }

      if (should_respawn) {
        pipe_connected = false;
        StopProcess();
        Sleep(500); // Small guard delay before respawning
        StartProcess();
      }
    }
  }
}

void ScreenshotHostManager::SendIpcCommand(const std::string& command_json) {
  std::scoped_lock lock(mutex_);
  if (pipe_handle_ == INVALID_HANDLE_VALUE) return;

  DWORD bytes_written = 0;
  WriteFile(
      pipe_handle_,
      command_json.data(),
      static_cast<DWORD>(command_json.size()),
      &bytes_written,
      nullptr
  );
}

}  // namespace clipd2d::app
