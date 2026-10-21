#include "ipc/NamedPipeServer.h"

#include <windows.h>

#include <array>
#include <chrono>
#include <string>

#include "common/Log.h"
#include "ipc/NamedPipeProtocol.h"
#include "ipc/PipeSecurity.h"
#include "ipc/SessionToken.h"

namespace clipd2d::ipc {

namespace {

std::string FormatWin32Error(const char* operation, DWORD error_code) {
  return std::string(operation) + " failed with error " + std::to_string(error_code);
}

std::string ReadMessage(HANDLE pipe) {
  std::string request_json;
  std::array<char, 4096> read_buffer{};
  DWORD bytes_read = 0;

  while (true) {
    const BOOL read_ok = ReadFile(
        pipe,
        read_buffer.data(),
        static_cast<DWORD>(read_buffer.size()),
        &bytes_read,
        nullptr);

    if (read_ok) {
      request_json.append(read_buffer.data(), bytes_read);
      return request_json;
    }

    const DWORD error = GetLastError();
    if (error == ERROR_MORE_DATA) {
      request_json.append(read_buffer.data(), bytes_read);
    } else {
      return {};
    }
  }
}

void WriteMessage(HANDLE pipe, std::string_view message) {
  DWORD bytes_written = 0;
  WriteFile(
      pipe,
      message.data(),
      static_cast<DWORD>(message.size()),
      &bytes_written,
      nullptr);
  FlushFileBuffers(pipe);
}

}  // namespace

NamedPipeServer::~NamedPipeServer() {
  Stop();
}

common::Status NamedPipeServer::Start(RequestHandler handler) {
  if (running_.exchange(true)) {
    return common::Status::Error(
        common::StatusCode::AlreadyExists,
        "Named Pipe server is already running.");
  }

  {
    std::scoped_lock lock(startup_mutex_);
    startup_signaled_ = false;
    startup_status_ = common::Status::Ok();
    handler_ = std::move(handler);
  }

  // S3：生成会话令牌并写入仅当前用户可读的令牌文件（CLI 同用户读取后随请求携带）。
  expected_token_ = GenerateSessionToken();
  if (expected_token_.empty()) {
    common::WriteLog(
        common::LogLevel::Error,
        "ipc.server",
        "会话令牌生成失败，动作命令鉴权降级为仅管道 ACL。");
  } else {
    const common::Status token_status = WriteSessionTokenFile(expected_token_);
    if (!token_status.ok()) {
      common::WriteLog(
          common::LogLevel::Error,
          "ipc.server",
          "令牌文件写入失败（动作命令将无法通过握手）：" + token_status.message);
    }
  }

  worker_ = std::thread(&NamedPipeServer::RunWorker, this);

  std::unique_lock lock(startup_mutex_);
  const bool ready = startup_cv_.wait_for(
      lock,
      std::chrono::seconds(5),
      [this]() { return startup_signaled_; });
  if (!ready) {
    running_.store(false);
    lock.unlock();
    if (worker_.joinable()) {
      worker_.join();
    }
    DeleteSessionTokenFile();
    expected_token_.clear();
    return common::Status::Error(
        common::StatusCode::Unavailable,
        "Timed out while starting the Named Pipe server.");
  }

  if (!startup_status_.ok()) {
    DeleteSessionTokenFile();
    expected_token_.clear();
  }
  return startup_status_;
}

void NamedPipeServer::Stop() {
  if (!running_.exchange(false)) {
    return;
  }

  for (int attempt = 0; attempt < 10; ++attempt) {
    HANDLE wake_client = CreateFileW(
        kCommandPipeName,
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);
    if (wake_client != INVALID_HANDLE_VALUE) {
      CloseHandle(wake_client);
      break;
    }
    if (GetLastError() == ERROR_FILE_NOT_FOUND) {
      Sleep(10);
    } else {
      break;
    }
  }

  if (worker_.joinable()) {
    worker_.join();
  }

  DeleteSessionTokenFile();
  expected_token_.clear();
}

void NamedPipeServer::NotifyStartup(common::Status status) {
  {
    std::scoped_lock lock(startup_mutex_);
    startup_status_ = std::move(status);
    startup_signaled_ = true;
  }
  startup_cv_.notify_all();
}

void NamedPipeServer::RunWorker() {
  bool startup_notified = false;

  // U2：管道 DACL 仅当前用户 + SYSTEM/Administrators（对端为同用户 CLI）。
  ScopedSecurityAttributes pipe_security(DaclScope::kUserWithSystemAdmins);
  if (!pipe_security.ok()) {
    common::WriteLog(
        common::LogLevel::Error,
        "ipc.server",
        "构建管道安全描述符失败，退回默认 DACL（仍拒绝远程客户端）。");
  }

  while (running_.load()) {
    HANDLE pipe = CreateNamedPipeW(
        kCommandPipeName,
        PIPE_ACCESS_DUPLEX | PIPE_REJECT_REMOTE_CLIENTS,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        1,
        8192,
        8192,
        0,
        pipe_security.get());
    if (pipe == INVALID_HANDLE_VALUE) {
      const DWORD error_code = GetLastError();
      if (!startup_notified) {
        NotifyStartup(common::Status::Error(
            common::StatusCode::PlatformError,
            FormatWin32Error("CreateNamedPipeW", error_code)));
        startup_notified = true;
      }
      common::WriteLog(
          common::LogLevel::Error,
          "ipc.server",
          FormatWin32Error("CreateNamedPipeW", error_code));
      break;
    }

    if (!startup_notified) {
      NotifyStartup(common::Status::Ok());
      startup_notified = true;
      common::WriteLog(common::LogLevel::Info, "ipc.server", "Named Pipe worker is accepting connections.");
    }

    const BOOL connected = ConnectNamedPipe(pipe, nullptr)
        ? TRUE
        : (GetLastError() == ERROR_PIPE_CONNECTED ? TRUE : FALSE);
    if (!connected) {
      const DWORD error_code = GetLastError();
      common::WriteLog(
          common::LogLevel::Warning,
          "ipc.server",
          FormatWin32Error("ConnectNamedPipe", error_code));
      CloseHandle(pipe);
      continue;
    }

    const std::string request_json = ReadMessage(pipe);
    core::commands::CommandRequest request;
    std::string error_message;
    core::commands::CommandResponse response;

    std::string provided_token;
    const bool token_parsed = TryExtractSessionToken(request_json, &provided_token);
    const bool token_ok =
        token_parsed && IsSessionTokenAccepted(expected_token_, provided_token);

    if (IsHelloRequest(request_json)) {
      // S3：握手探测消息（可协商），不触发任何动作。
      response = token_ok
          ? core::commands::CommandResponse::Success("auth_ok", "握手通过，会话令牌有效。")
          : core::commands::CommandResponse::Failure("unauthorized", "握手令牌无效。");
    } else if (!TryDeserializeRequest(request_json, &request, &error_message)) {
      response = core::commands::CommandResponse::Failure(
          "invalid_request",
          error_message);
    } else if (RequiresHandshake(request.id) && !token_ok) {
      // S3：未通过握手的连接不接受动作命令（粘贴/输入合成注入面）。
      common::WriteLog(
          common::LogLevel::Warning,
          "ipc.server",
          std::string("拒绝未通过握手的动作命令: ") + core::commands::ToCommandName(request.id));
      response = core::commands::CommandResponse::Failure(
          "unauthorized",
          "未通过握手，动作命令需要有效会话令牌。");
    } else if (!handler_) {
      response = core::commands::CommandResponse::Failure(
          "service_unavailable",
          "Command handler is not installed.");
    } else {
      response = handler_(request);
    }

    const std::string response_json = SerializeResponse(response);
    WriteMessage(pipe, response_json);

    DisconnectNamedPipe(pipe);
    CloseHandle(pipe);
  }

  if (!startup_notified) {
    NotifyStartup(common::Status::Error(
        common::StatusCode::Unavailable,
        "Named Pipe worker exited before initialization completed."));
  }
  common::WriteLog(common::LogLevel::Info, "ipc.server", "Named Pipe worker stopped.");
}

}  // namespace clipd2d::ipc
