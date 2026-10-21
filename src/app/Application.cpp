#include "app/Application.h"

#include <algorithm>
#include <commctrl.h>
#include <filesystem>
#include <ole2.h>
#include <shellapi.h>

#include "common/Log.h"
#include "common/StringUtil.h"
#include "common/Utf8.h"
#include "app/LaunchPolicy.h"
#include "app/StartupTrace.h"
#include "platform/win32/HotkeySpec.h"

namespace clipd2d::app {

namespace {

constexpr wchar_t kSingleInstanceMutexName[] = L"Local\\Scraps.SingleInstance";

bool TryActivateExistingWindow() {
  HWND hwnd = FindWindowW(ui::MainWindow::WindowClassName(), nullptr);
  if (hwnd == nullptr) {
    return false;
  }

  PostMessageW(hwnd, ui::MainWindow::ShowWindowMessageId(), 0, 0);
  return true;
}

platform::win32::HotkeySpec ResolveHotkeySpec(const core::store::AppSettings& settings) {
  auto hotkey = platform::win32::DefaultHotkeySpec();
  platform::win32::HotkeySpec parsed;
  if (platform::win32::ParseHotkeyText(settings.hotkey, &parsed).ok()) {
    hotkey = parsed;
  }
  return hotkey;
}

platform::win32::HotkeySpec ResolveQuickPasteHotkeySpec(const core::store::AppSettings& settings) {
  platform::win32::HotkeySpec hotkey;
  if (platform::win32::ParseHotkeyText(settings.quick_paste_hotkey, &hotkey).ok()) {
    return hotkey;
  }
  hotkey.modifiers = MOD_ALT;
  hotkey.virtual_key = VK_OEM_3;
  hotkey.normalized_text = L"Alt+~";
  return hotkey;
}

platform::win32::HotkeySpec ResolveScreenshotHotkeySpec(const core::store::AppSettings& settings) {
  platform::win32::HotkeySpec hotkey;
  if (platform::win32::ParseHotkeyText(settings.screenshot_hotkey, &hotkey).ok()) {
    return hotkey;
  }
  hotkey.modifiers = MOD_WIN | MOD_SHIFT;
  hotkey.virtual_key = 'S';
  hotkey.normalized_text = L"Win+Shift+S";
  return hotkey;
}

void ShowStatusMessage(HWND owner, std::string_view message) {
  const auto wide = common::Utf8ToWide(message);
  MessageBoxW(owner, wide.wide.c_str(), L"Scraps", MB_ICONERROR | MB_OK);
}

std::uint64_t TryFileSize(const std::wstring& path) {
  std::error_code error;
  const auto size = std::filesystem::file_size(path, error);
  if (error) {
    return 0;
  }
  return static_cast<std::uint64_t>(size);
}

std::uint64_t TryDirectorySize(const std::wstring& path) {
  std::error_code error;
  if (!std::filesystem::exists(path, error)) {
    return 0;
  }

  std::uint64_t total = 0;
  for (std::filesystem::recursive_directory_iterator it(path, error), end; it != end && !error; it.increment(error)) {
    if (error) {
      break;
    }
    if (it->is_regular_file(error) && !error) {
      total += static_cast<std::uint64_t>(it->file_size(error));
      error.clear();
    }
  }
  return total;
}

}  // namespace

Application::~Application() {
  Shutdown();
}

common::Status Application::Initialize(HINSTANCE instance, int n_cmd_show) {
  instance_ = instance;

  const common::Status single_instance_status = AcquireSingleInstance();
  if (!single_instance_status.ok()) {
    return single_instance_status;
  }
  if (should_exit_immediately_) {
    return common::Status::Ok();
  }

  ResetStartupTrace();

  const HRESULT com_result = OleInitialize(nullptr);
  if (FAILED(com_result)) {
    return common::Status::Error(
        common::StatusCode::PlatformError,
        "初始化 OLE 失败。");
  }
  com_initialized_ = true;

  INITCOMMONCONTROLSEX common_controls{};
  common_controls.dwSize = sizeof(common_controls);
  common_controls.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES;
  InitCommonControlsEx(&common_controls);

  common::Status status = common::ResolveAppPaths(&app_paths_);
  if (!status.ok()) {
    return status;
  }
  MarkStartupPhase("paths_ready");

  status = app_controller_.Initialize(app_paths_);
  if (!status.ok()) {
    return status;
  }
  MarkStartupPhase("controller_ready");
  core::store::AppSettings startup_settings = app_controller_.current_settings();
  core::store::BootstrapState bootstrap_state;
  status = app_controller_.LoadBootstrapState(&bootstrap_state);
  if (!status.ok()) {
    return status;
  }
  const LaunchDecision launch_decision =
      DecideLaunchBehavior(startup_settings, bootstrap_state.has_any_settings, bootstrap_state.has_any_clips, n_cmd_show != SW_HIDE);
  MarkStartupPhase("launch_policy_ready");
  if (launch_decision.should_mark_first_run_complete && !startup_settings.has_completed_first_run) {
    startup_settings.has_completed_first_run = true;
    status = app_controller_.SaveSettings(startup_settings);
    if (!status.ok()) {
      return status;
    }
  }

  command_service_.SetController(&app_controller_);
  command_service_.SetShowWindowDispatcher([this]() {
    main_window_.RequestShowFromAnyThread();
  });

  status = main_window_.Create(
      instance_,
      n_cmd_show,
      launch_decision.initial_panel_behavior == InitialPanelBehavior::ShowAfterFirstLayout,
      &app_controller_,
      [this]() {
    OpenSettingsWindow();
  }, [this](HWND hwnd) {
    return hwnd == settings_window_.hwnd();
  });
  if (!status.ok()) {
    return status;
  }
  MarkStartupPhase("main_window_ready");
  app_controller_.SetRefreshDispatcher([this](const core::commands::RefreshRequest& request) {
    main_window_.RequestRefreshFromAnyThread(request);
  });

  status = InitializeInfrastructure();
  if (!status.ok()) {
    return status;
  }
  MarkStartupPhase("infrastructure_ready");
  const core::store::AppSettings settings = app_controller_.current_settings();
  screenshot_host_manager_.Initialize(app_paths_.root_dir, settings.screenshot_enabled && settings.screenshot_prewarm);

  MarkStartupPhase("initialize_complete");
  StartNamedPipeInBackground();
  StartStartupMaintenance();

  common::WriteLog(common::LogLevel::Info, "app.bootstrap", "Application initialized.");
  return common::Status::Ok();
}

int Application::Run() {
  if (should_exit_immediately_) {
    return 0;
  }

  MSG message{};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }

  return static_cast<int>(message.wParam);
}

void Application::Shutdown() {
  StopStartupMaintenance();
  StopNamedPipeBackgroundStart();
  screenshot_host_manager_.Shutdown();
  clipboard_event_receiver_.Destroy();
  app_controller_.Shutdown();
  if (ipc_server_started_.exchange(false)) {
    named_pipe_server_.Stop();
  }
  notify_icon_host_.Shutdown();
  quick_paste_session_.Destroy();
  shell_message_window_.Destroy();
  settings_window_.Destroy();
  main_window_.Destroy();

  if (com_initialized_) {
    OleUninitialize();
    com_initialized_ = false;
  }

  if (instance_mutex_ != nullptr) {
    CloseHandle(instance_mutex_);
    instance_mutex_ = nullptr;
  }
}

common::Status Application::AcquireSingleInstance() {
  instance_mutex_ = CreateMutexW(nullptr, FALSE, kSingleInstanceMutexName);
  if (instance_mutex_ == nullptr) {
    return common::Status::Error(
        common::StatusCode::PlatformError,
        "创建单实例互斥体失败。");
  }

  if (GetLastError() != ERROR_ALREADY_EXISTS) {
    return common::Status::Ok();
  }

  core::commands::CommandRequest request;
  request.id = core::commands::CommandId::ShowWindow;
  const auto response = named_pipe_client_.Send(request);
  if (!response.ok) {
    TryActivateExistingWindow();
  }

  should_exit_immediately_ = true;
  return common::Status::Ok();
}

common::Status Application::InitializeInfrastructure() {
  clipboard_event_receiver_.SetCallback([this](DWORD sequence, HWND source_window) {
    bool inserted = false;
    std::int64_t clip_id = 0;
    const common::Status status = app_controller_.OnClipboardChanged(source_window, sequence, &inserted, &clip_id);
    if (!status.ok()) {
      if (status.code != common::StatusCode::Unavailable) {
        common::WriteLog(common::LogLevel::Warning, "clipboard.monitor", status.message);
      }
    }
    if (inserted) {
      if (clip_id > 0) {
        main_window_.RequestRefreshForNewestClip(clip_id);
      } else {
        main_window_.RequestRefreshPreserveViewport();
      }
    }
  });
  platform::win32::ShellMessageWindow::Callbacks callbacks;
  callbacks.on_hotkey = [this]() {
    main_window_.RequestToggleFromAnyThread();
  };
  callbacks.on_quick_paste_hotkey = [this]() {
    quick_paste_session_.Toggle();
  };
  callbacks.on_screenshot_hotkey = [this]() {
    screenshot_host_manager_.TriggerCapture();
  };
  callbacks.on_tray_activate = [this]() {
    main_window_.RequestShowFromAnyThread();
  };
  callbacks.on_tray_menu = [this]() {
    ShowTrayMenu();
  };
  shell_message_window_.SetCallbacks(std::move(callbacks));

  common::Status status = shell_message_window_.Create(instance_);
  if (!status.ok()) {
    return status;
  }

  status = clipboard_event_receiver_.Create(instance_);
  if (!status.ok()) {
    return status;
  }

  const core::store::AppSettings settings = app_controller_.current_settings();
  const auto hotkey = ResolveHotkeySpec(settings);
  const auto quick_paste_hotkey = ResolveQuickPasteHotkeySpec(settings);

  status = shell_message_window_.RegisterHotkey(hotkey.modifiers, hotkey.virtual_key);
  if (!status.ok()) {
    return status;
  }
  status = shell_message_window_.RegisterQuickPasteHotkey(quick_paste_hotkey.modifiers, quick_paste_hotkey.virtual_key);
  if (!status.ok()) {
    return status;
  }
  if (settings.screenshot_enabled) {
    const auto screenshot_hotkey = ResolveScreenshotHotkeySpec(settings);
    const common::Status screenshot_hotkey_status = shell_message_window_.RegisterScreenshotHotkey(screenshot_hotkey.modifiers, screenshot_hotkey.virtual_key);
    if (!screenshot_hotkey_status.ok()) {
      common::WriteLog(common::LogLevel::Warning, "app.bootstrap", "Failed to register screenshot hotkey (" + common::WideToUtf8(screenshot_hotkey.normalized_text).utf8 + "): " + screenshot_hotkey_status.message);
    }
  }

  status = clipboard_event_receiver_.SetMonitoringEnabled(settings.monitoring_enabled);
  if (!status.ok()) {
    return status;
  }
  quick_paste_session_.Create(instance_, main_window_.hwnd(), &app_controller_);
  return common::Status::Ok();
}

common::Status Application::EnsureSettingsWindowCreated() {
  if (settings_window_.hwnd() != nullptr) {
    return common::Status::Ok();
  }
  return settings_window_.Create(instance_, main_window_.hwnd());
}

void Application::StartNamedPipeInBackground() {
  StopNamedPipeBackgroundStart();
  ipc_start_stop_.store(false);
  ipc_start_thread_ = std::thread([this]() {
    const core::store::AppSettings settings = app_controller_.current_settings();
    common::Status tray_status = notify_icon_host_.Initialize(shell_message_window_.hwnd());
    if (tray_status.ok()) {
      notify_icon_host_.UpdateMonitoringState(settings.monitoring_enabled);
    } else {
      common::WriteLog(common::LogLevel::Warning, "app.bootstrap", tray_status.message);
    }
    if (ipc_start_stop_.load()) {
      return;
    }
    const common::Status status = named_pipe_server_.Start([this](const core::commands::CommandRequest& request) {
      return command_service_.Handle(request);
    });
    if (status.ok()) {
      ipc_server_started_.store(true);
    } else if (!ipc_start_stop_.load()) {
      common::WriteLog(common::LogLevel::Warning, "app.bootstrap", status.message);
    }
  });
}

void Application::StopNamedPipeBackgroundStart() {
  ipc_start_stop_.store(true);
  if (ipc_start_thread_.joinable()) {
    ipc_start_thread_.join();
  }
}

void Application::StartStartupMaintenance() {
  StopStartupMaintenance();
  startup_maintenance_stop_.store(false);
  startup_maintenance_thread_ = std::thread([this]() {
    constexpr DWORD kDelayMs = 1500;
    constexpr DWORD kPollMs = 50;
    for (DWORD waited = 0; waited < kDelayMs; waited += kPollMs) {
      if (startup_maintenance_stop_.load()) {
        return;
      }
      Sleep(std::min(kPollMs, kDelayMs - waited));
    }
    const auto started_at = GetTickCount64();
    common::WriteLog(common::LogLevel::Info, "app.startup.maintenance", "begin delayed history maintenance");
    const common::Status status = app_controller_.RunStartupMaintenance();
    const auto elapsed_ms = GetTickCount64() - started_at;
    common::WriteLog(
        common::LogLevel::Info,
        "app.startup.maintenance",
        "end delayed history maintenance elapsed_ms=" + std::to_string(elapsed_ms) +
            " ok=" + std::string(status.ok() ? "true" : "false"));
    if (!status.ok()) {
      common::WriteLog(common::LogLevel::Warning, "app.startup", status.message);
    }
  });
}

void Application::StopStartupMaintenance() {
  startup_maintenance_stop_.store(true);
  if (startup_maintenance_thread_.joinable()) {
    startup_maintenance_thread_.join();
  }
}

void Application::OpenSettingsWindow() {
  const common::Status ensure_status = EnsureSettingsWindowCreated();
  if (!ensure_status.ok()) {
    ShowStatusMessage(main_window_.hwnd(), ensure_status.message);
    return;
  }
  ui::SettingsWindow::HostActions actions;
  actions.on_save = [this](const core::store::AppSettings& settings) {
    return ApplySettings(settings);
  };
  actions.on_clear_history = [this]() {
    const common::Status status = app_controller_.ClearHistory();
    if (!status.ok()) {
      ShowStatusMessage(main_window_.hwnd(), status.message);
      return false;
    }
    main_window_.RequestRefreshPreserveViewport();
    return true;
  };
  actions.on_clear_image_cache = [this]() {
    ClearImageCache();
    return true;
  };
  actions.on_open_data_directory = [this]() {
    ShellExecuteW(nullptr, L"open", app_paths_.root_dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
  };
  actions.query_data_stats = [this]() {
    ui::DataStats stats;
    stats.data_dir = app_paths_.root_dir;
    stats.database_path = app_paths_.database_path;
    stats.database_bytes = TryFileSize(app_paths_.database_path);
    stats.image_cache_bytes = TryDirectorySize(app_paths_.cache_dir);
    return stats;
  };
  actions.on_begin_input_focus = [this]() {
    main_window_.CaptureExternalFocusRestoreContext();
  };
  actions.on_end_input_focus = [this]() {
    main_window_.RestoreExternalFocusRestoreContext();
  };
  actions.on_show = [this]() {
    main_window_.NotifySettingsWindowShown();
  };
  actions.on_hide = [this]() {
    main_window_.NotifySettingsWindowHidden();
  };
  settings_window_.Show(app_controller_.current_settings(), std::move(actions));
}

bool Application::ApplySettings(const core::store::AppSettings& settings) {
  core::store::AppSettings normalized_settings = settings;
  const core::store::AppSettings previous_settings = app_controller_.current_settings();

  platform::win32::HotkeySpec hotkey;
  common::Status status = platform::win32::ParseHotkeyText(normalized_settings.hotkey, &hotkey);
  if (!status.ok()) {
    MessageBoxW(
        main_window_.hwnd(),
        L"\u70ed\u952e\u683c\u5f0f\u65e0\u6548\u3002\u793a\u4f8b\uff1aCtrl+Shift+V",
        L"Scraps",
        MB_ICONERROR | MB_OK);
    return false;
  }

  platform::win32::HotkeySpec quick_paste_hotkey;
  status = platform::win32::ParseHotkeyText(normalized_settings.quick_paste_hotkey, &quick_paste_hotkey);
  if (!status.ok()) {
    MessageBoxW(
        main_window_.hwnd(),
        L"\u5feb\u901f\u7c98\u8d34\u70ed\u952e\u683c\u5f0f\u65e0\u6548\u3002\u793a\u4f8b\uff1aAlt+~",
        L"Scraps",
        MB_ICONERROR | MB_OK);
    return false;
  }

  platform::win32::HotkeySpec screenshot_hotkey;
  if (normalized_settings.screenshot_enabled) {
    status = platform::win32::ParseHotkeyText(normalized_settings.screenshot_hotkey, &screenshot_hotkey);
    if (!status.ok()) {
      MessageBoxW(
          main_window_.hwnd(),
          L"截图快捷键格式无效。示例：Win+Shift+S",
          L"Scraps",
          MB_ICONERROR | MB_OK);
      return false;
    }
  }

  platform::win32::HotkeySpec search_hotkey;
  status = platform::win32::ParseLocalHotkeyText(normalized_settings.search_focus_hotkey, &search_hotkey);
  if (!status.ok()) {
    MessageBoxW(
        main_window_.hwnd(),
        L"\u805a\u7126\u641c\u7d22\u6846\u5feb\u6377\u952e\u683c\u5f0f\u65e0\u6548\u3002",
        L"Scraps",
        MB_ICONERROR | MB_OK);
    return false;
  }

  const std::wstring sticker_hotkey_text = common::TrimWide(normalized_settings.third_party_sticker_hotkey);
  platform::win32::HotkeySpec sticker_hotkey;
  if (normalized_settings.third_party_sticker_enabled && sticker_hotkey_text.empty()) {
    MessageBoxW(
        main_window_.hwnd(),
        L"\u8bf7\u5148\u914d\u7f6e\u7b2c\u4e09\u65b9\u8d34\u56fe\u5feb\u6377\u952e\u3002",
        L"Scraps",
        MB_ICONERROR | MB_OK);
    return false;
  }
  if (!sticker_hotkey_text.empty()) {
    status = platform::win32::ParseLocalHotkeyText(sticker_hotkey_text, &sticker_hotkey);
    if (!status.ok()) {
      MessageBoxW(
          main_window_.hwnd(),
          L"\u7b2c\u4e09\u65b9\u8d34\u56fe\u5feb\u6377\u952e\u683c\u5f0f\u65e0\u6548\u3002",
          L"Scraps",
          MB_ICONERROR | MB_OK);
      return false;
    }
    normalized_settings.third_party_sticker_hotkey = sticker_hotkey.normalized_text;
  } else {
    normalized_settings.third_party_sticker_hotkey.clear();
  }

  const auto previous_hotkey = ResolveHotkeySpec(previous_settings);
  const auto previous_quick_paste_hotkey = ResolveQuickPasteHotkeySpec(previous_settings);
  auto restore_previous_hotkeys = [&]() {
    shell_message_window_.RegisterHotkey(previous_hotkey.modifiers, previous_hotkey.virtual_key);
    shell_message_window_.RegisterQuickPasteHotkey(previous_quick_paste_hotkey.modifiers, previous_quick_paste_hotkey.virtual_key);
    if (previous_settings.screenshot_enabled) {
      const auto prev_ss_hk = ResolveScreenshotHotkeySpec(previous_settings);
      shell_message_window_.RegisterScreenshotHotkey(prev_ss_hk.modifiers, prev_ss_hk.virtual_key);
    } else {
      shell_message_window_.UnregisterScreenshotHotkey();
    }
  };

  status = shell_message_window_.RegisterHotkey(hotkey.modifiers, hotkey.virtual_key);
  if (!status.ok()) {
    MessageBoxW(
        main_window_.hwnd(),
        L"\u6ce8\u518c\u70ed\u952e\u5931\u8d25\uff0c\u53ef\u80fd\u5df2\u88ab\u5176\u4ed6\u7a0b\u5e8f\u5360\u7528\u3002",
        L"Scraps",
        MB_ICONERROR | MB_OK);
    return false;
  }

  status = shell_message_window_.RegisterQuickPasteHotkey(quick_paste_hotkey.modifiers, quick_paste_hotkey.virtual_key);
  if (!status.ok()) {
    restore_previous_hotkeys();
    MessageBoxW(
        main_window_.hwnd(),
        L"\u6ce8\u518c\u5feb\u901f\u7c98\u8d34\u70ed\u952e\u5931\u8d25\uff0c\u53ef\u80fd\u5df2\u88ab\u5176\u4ed6\u7a0b\u5e8f\u5360\u7528\u3002",
        L"Scraps",
        MB_ICONERROR | MB_OK);
    return false;
  }

  if (normalized_settings.screenshot_enabled) {
    status = shell_message_window_.RegisterScreenshotHotkey(screenshot_hotkey.modifiers, screenshot_hotkey.virtual_key);
    if (!status.ok()) {
      restore_previous_hotkeys();
      MessageBoxW(
          main_window_.hwnd(),
          L"注册截图热键失败，可能已被其他程序占用。",
          L"Scraps",
          MB_ICONERROR | MB_OK);
      return false;
    }
    normalized_settings.screenshot_hotkey = screenshot_hotkey.normalized_text;
  } else {
    shell_message_window_.UnregisterScreenshotHotkey();
    normalized_settings.screenshot_hotkey = screenshot_hotkey.normalized_text;
  }

  normalized_settings.hotkey = hotkey.normalized_text;
  normalized_settings.quick_paste_hotkey = quick_paste_hotkey.normalized_text;
  normalized_settings.search_focus_hotkey = search_hotkey.normalized_text;
  status = clipboard_event_receiver_.SetMonitoringEnabled(normalized_settings.monitoring_enabled);
  if (!status.ok()) {
    restore_previous_hotkeys();
    ShowStatusMessage(main_window_.hwnd(), status.message);
    return false;
  }
  status = ApplyStartupSetting(normalized_settings.start_with_windows);
  if (!status.ok()) {
    clipboard_event_receiver_.SetMonitoringEnabled(previous_settings.monitoring_enabled);
    restore_previous_hotkeys();
    ShowStatusMessage(main_window_.hwnd(), status.message);
    return false;
  }
  status = app_controller_.SaveSettings(normalized_settings);
  if (!status.ok()) {
    clipboard_event_receiver_.SetMonitoringEnabled(previous_settings.monitoring_enabled);
    restore_previous_hotkeys();
    ApplyStartupSetting(previous_settings.start_with_windows);
    ShowStatusMessage(main_window_.hwnd(), status.message);
    return false;
  }

  if (normalized_settings.screenshot_enabled != previous_settings.screenshot_enabled ||
      normalized_settings.screenshot_prewarm != previous_settings.screenshot_prewarm) {
    common::WriteLog(common::LogLevel::Info, "app.settings", "Screenshot settings changed. Reloading screenshot host manager...");
    screenshot_host_manager_.Shutdown();
    screenshot_host_manager_.Initialize(app_paths_.root_dir, normalized_settings.screenshot_enabled && normalized_settings.screenshot_prewarm);
  }

  notify_icon_host_.UpdateMonitoringState(normalized_settings.monitoring_enabled);
  main_window_.RequestRefreshPreserveViewport();
  return true;
}

void Application::ShowTrayMenu() {
  const int command = notify_icon_host_.ShowContextMenu(app_controller_.current_settings().monitoring_enabled);
  switch (command) {
    case platform::win32::NotifyIconHost::kCommandShow:
      main_window_.RequestShowFromAnyThread();
      break;
    case platform::win32::NotifyIconHost::kCommandToggleMonitoring: {
      core::store::AppSettings settings = app_controller_.current_settings();
      settings.monitoring_enabled = !settings.monitoring_enabled;
      ApplySettings(settings);
      break;
    }
    case platform::win32::NotifyIconHost::kCommandSettings:
      OpenSettingsWindow();
      break;
    case platform::win32::NotifyIconHost::kCommandClearHistory:
      ClearHistory();
      break;
    case platform::win32::NotifyIconHost::kCommandExit:
      ExitApplication();
      break;
  }
}

void Application::ClearHistory() {
  const common::Status status = app_controller_.ClearHistory();
  if (!status.ok()) {
    ShowStatusMessage(main_window_.hwnd(), status.message);
    return;
  }
  main_window_.RequestRefreshPreserveViewport();
}

void Application::ClearImageCache() {
  std::error_code error;
  std::filesystem::remove_all(app_paths_.cache_dir, error);
  common::EnsureDirectory(app_paths_.cache_dir);
  main_window_.RequestRefreshPreserveViewport();
}

void Application::ExitApplication() {
  PostQuitMessage(0);
}

common::Status Application::ApplyStartupSetting(bool enabled) {
  HKEY key = nullptr;
  if (RegCreateKeyExW(
          HKEY_CURRENT_USER,
          L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
          0,
          nullptr,
          0,
          KEY_SET_VALUE,
          nullptr,
          &key,
          nullptr) != ERROR_SUCCESS) {
    return common::Status::Error(
        common::StatusCode::PlatformError,
        "打开开机启动注册表项失败。");
  }

  wchar_t exe_path[MAX_PATH]{};
  GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
  common::Status status = common::Status::Ok();
  if (enabled) {
    if (RegSetValueExW(
            key,
            L"Scraps",
            0,
            REG_SZ,
            reinterpret_cast<const BYTE*>(exe_path),
            static_cast<DWORD>((wcslen(exe_path) + 1) * sizeof(wchar_t))) != ERROR_SUCCESS) {
      status = common::Status::Error(
          common::StatusCode::PlatformError,
          "更新开机启动注册表项失败。");
    }
  } else {
    RegDeleteValueW(key, L"Scraps");
  }
  RegCloseKey(key);
  return status;
}

}  // namespace clipd2d::app
