#include "ipc/NamedPipeClient.h"

#include <windows.h>

#include <array>
#include <string>

#include "ipc/NamedPipeProtocol.h"
#include "ipc/SessionToken.h"

namespace clipd2d::ipc {

core::commands::CommandResponse NamedPipeClient::Send(
    const core::commands::CommandRequest& request) const {
  if (!WaitNamedPipeW(kCommandPipeName, 1500)) {
    return core::commands::CommandResponse::Failure(
        "app_not_running",
        "主进程未运行，或命名管道尚未就绪。");
  }

  HANDLE pipe = CreateFileW(
      kCommandPipeName,
      GENERIC_READ | GENERIC_WRITE,
      0,
      nullptr,
      OPEN_EXISTING,
      0,
      nullptr);
  if (pipe == INVALID_HANDLE_VALUE) {
    return core::commands::CommandResponse::Failure(
        "app_not_running",
        "主进程未运行，或命名管道尚未就绪。");
  }

  DWORD mode = PIPE_READMODE_MESSAGE;
  SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);

  // S3：同用户 CLI 读取令牌文件并随请求携带；读取失败时留空，
  // 非动作命令仍可协商通过，动作命令由服务端拒绝。
  std::string session_token;
  LoadSessionTokenFile(&session_token);
  const std::string payload = SerializeRequest(request, session_token);
  DWORD bytes_written = 0;
  const BOOL write_ok = WriteFile(
      pipe,
      payload.data(),
      static_cast<DWORD>(payload.size()),
      &bytes_written,
      nullptr);
  if (!write_ok) {
    CloseHandle(pipe);
    return core::commands::CommandResponse::Failure(
        "pipe_write_failed",
        "写入命名管道请求失败。");
  }

  std::string response_json;
  std::array<char, 4096> read_buffer{};
  DWORD bytes_read = 0;
  bool read_success = false;

  while (true) {
    const BOOL read_ok = ReadFile(
        pipe,
        read_buffer.data(),
        static_cast<DWORD>(read_buffer.size()),
        &bytes_read,
        nullptr);

    if (read_ok) {
      response_json.append(read_buffer.data(), bytes_read);
      read_success = true;
      break;
    }

    const DWORD error = GetLastError();
    if (error == ERROR_MORE_DATA) {
      response_json.append(read_buffer.data(), bytes_read);
    } else {
      break;
    }
  }
  CloseHandle(pipe);

  if (!read_success) {
    return core::commands::CommandResponse::Failure(
        "pipe_read_failed",
        "读取命名管道响应失败。");
  }

  core::commands::CommandResponse response;
  std::string error_message;
  if (!TryDeserializeResponse(response_json, &response, &error_message)) {
    return core::commands::CommandResponse::Failure(
        "invalid_response",
        error_message);
  }

  return response;
}

}  // namespace clipd2d::ipc
