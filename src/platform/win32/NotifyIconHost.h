#pragma once

#include <windows.h>
#include <shellapi.h>

#include "common/Status.h"

namespace clipd2d::platform::win32 {

// Locale-agnostic label set injected by the app layer (src/app/AppStrings.h).
struct TrayLabels {
  const wchar_t* show;
  const wchar_t* pause_monitoring;
  const wchar_t* resume_monitoring;
  const wchar_t* settings;
  const wchar_t* clear_history;
  const wchar_t* exit;
  const wchar_t* tip_monitoring;
  const wchar_t* tip_paused;
};

class NotifyIconHost {
 public:
  static constexpr UINT kTrayMessage = WM_APP + 44;
  enum MenuCommand {
    kCommandShow = 1001,
    kCommandToggleMonitoring,
    kCommandSettings,
    kCommandClearHistory,
    kCommandExit,
  };

  ~NotifyIconHost();

  common::Status Initialize(HWND owner, const TrayLabels& labels);
  void Shutdown();
  void UpdateMonitoringState(bool enabled);
  void ShowBalloon(const std::wstring& title, const std::wstring& text);
  int ShowContextMenu(bool monitoring_enabled);

 private:
  HWND owner_ = nullptr;
  NOTIFYICONDATAW data_{};
  TrayLabels labels_{};
};

}  // namespace clipd2d::platform::win32
