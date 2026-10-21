#include "cli/CliCommandParser.h"

#include <windows.h>

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

#include "ipc/NamedPipeClient.h"
#include "ipc/NamedPipeProtocol.h"
#include "ui/MainWindow.h"

namespace {

using clipd2d::core::commands::CommandId;
using clipd2d::core::commands::CommandRequest;
using clipd2d::core::commands::CommandResponse;

std::wstring ResolveSiblingPath(const wchar_t* file_name) {
  wchar_t module_path[MAX_PATH]{};
  GetModuleFileNameW(nullptr, module_path, MAX_PATH);

  std::wstring path = module_path;
  const size_t separator = path.find_last_of(L"\\/");
  if (separator == std::wstring::npos) {
    return file_name;
  }

  path.resize(separator + 1);
  path += file_name;
  return path;
}

bool TryActivateExistingWindow() {
  HWND hwnd = FindWindowW(clipd2d::ui::MainWindow::WindowClassName(), nullptr);
  if (hwnd == nullptr) {
    return false;
  }

  PostMessageW(hwnd, clipd2d::ui::MainWindow::ShowWindowMessageId(), 0, 0);
  return true;
}

CommandResponse SendThroughPipe(const CommandRequest& request) {
  clipd2d::ipc::NamedPipeClient client;
  return client.Send(request);
}

CommandResponse LaunchGuiProcess() {
  const std::wstring gui_path = ResolveSiblingPath(L"Scraps.exe");
  std::wstring command_line = L"\"" + gui_path + L"\"";

  STARTUPINFOW startup_info{};
  startup_info.cb = sizeof(startup_info);
  PROCESS_INFORMATION process_info{};
  if (!CreateProcessW(
          gui_path.c_str(),
          command_line.data(),
          nullptr,
          nullptr,
          FALSE,
          0,
          nullptr,
          nullptr,
          &startup_info,
          &process_info)) {
    return CommandResponse::Failure("launch_failed", "无法启动 Scraps.exe。");
  }

  CloseHandle(process_info.hThread);
  CloseHandle(process_info.hProcess);
  return CommandResponse::Success("ok", "Scraps 已启动。");
}

CommandResponse ExecuteShowCommand(const CommandRequest& request) {
  CommandResponse response = SendThroughPipe(request);
  if (response.ok) {
    return response;
  }

  if (TryActivateExistingWindow()) {
    return CommandResponse::Success("ok", "主窗口已激活。");
  }

  response = LaunchGuiProcess();
  if (!response.ok) {
    return response;
  }

  for (int attempt = 0; attempt < 20; ++attempt) {
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    CommandResponse retry = SendThroughPipe(request);
    if (retry.ok) {
      return retry;
    }
  }

  if (TryActivateExistingWindow()) {
    return CommandResponse::Success("ok", "主窗口已激活。");
  }

  return CommandResponse::Failure("app_start_timeout", "主进程已启动，但命令通道尚未就绪。");
}

}  // namespace

int wmain(int argc, wchar_t* argv[]) {
  const auto parsed = clipd2d::cli::ParseCommandLine(argc, argv);
  if (parsed.should_exit) {
    if (!parsed.output.empty()) {
      std::cout << parsed.output;
      if (parsed.output.back() != '\n') {
        std::cout << '\n';
      }
    }
    return parsed.exit_code;
  }

  CommandResponse response;
  if (parsed.request.id == CommandId::ShowWindow) {
    response = ExecuteShowCommand(parsed.request);
  } else {
    response = SendThroughPipe(parsed.request);
  }

  if (parsed.request.wants_json) {
    std::cout << clipd2d::ipc::SerializeResponse(response) << '\n';
  } else {
    std::cout << response.message << '\n';
    if (!response.data.empty()) {
      std::cout << response.data << '\n';
    }
  }

  return response.ok ? 0 : 1;
}
