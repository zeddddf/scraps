#pragma once

#include <windows.h>

#include <atomic>
#include <thread>

#include "common/Status.h"
#include "core/commands/AppController.h"
#include "core/commands/CommandService.h"
#include "ipc/NamedPipeClient.h"
#include "ipc/NamedPipeServer.h"
#include "platform/win32/ClipboardEventReceiver.h"
#include "platform/win32/NotifyIconHost.h"
#include "platform/win32/ShellMessageWindow.h"
#include "app/QuickPasteSession.h"
#include "app/ScreenshotHostManager.h"
#include "app/UpdateChecker.h"
#include "ui/MainWindow.h"
#include "ui/SettingsWindow.h"

namespace clipd2d::app {

class Application {
 public:
  Application() = default;
  ~Application();

  common::Status Initialize(HINSTANCE instance, int n_cmd_show);
  int Run();
  void Shutdown();

  bool should_exit_immediately() const {
    return should_exit_immediately_;
  }

 private:
  common::Status AcquireSingleInstance();
  common::Status InitializeInfrastructure();
  common::Status EnsureSettingsWindowCreated();
  void StartNamedPipeInBackground();
  void StopNamedPipeBackgroundStart();
  void StartStartupMaintenance();
  void StopStartupMaintenance();
  void OpenSettingsWindow();
  bool ApplySettings(const core::store::AppSettings& settings);
  void ShowTrayMenu();
  void ClearHistory();
  void ClearImageCache();
  void ExitApplication();
  common::Status ApplyStartupSetting(bool enabled);

  HINSTANCE instance_ = nullptr;
  HANDLE instance_mutex_ = nullptr;
  bool com_initialized_ = false;
  bool should_exit_immediately_ = false;
  std::atomic<bool> startup_maintenance_stop_{false};
  std::atomic<bool> ipc_start_stop_{false};
  std::atomic<bool> ipc_server_started_{false};
  std::thread startup_maintenance_thread_;
  std::thread ipc_start_thread_;

  common::AppPaths app_paths_;
  core::commands::AppController app_controller_;
  core::commands::CommandService command_service_;
  ipc::NamedPipeClient named_pipe_client_;
  ipc::NamedPipeServer named_pipe_server_;
  platform::win32::ClipboardEventReceiver clipboard_event_receiver_;
  platform::win32::NotifyIconHost notify_icon_host_;
  platform::win32::ShellMessageWindow shell_message_window_;
  ScreenshotHostManager screenshot_host_manager_;
  QuickPasteSession quick_paste_session_;
  UpdateChecker update_checker_;
  std::wstring update_release_url_;
  ui::MainWindow main_window_;
  ui::SettingsWindow settings_window_;
};

}  // namespace clipd2d::app
