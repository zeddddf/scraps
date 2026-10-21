#pragma once

#include <windows.h>
#include <shellapi.h>

#include "common/Status.h"

namespace clipd2d::platform::win32 {

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

  common::Status Initialize(HWND owner);
  void Shutdown();
  void UpdateMonitoringState(bool enabled);
  int ShowContextMenu(bool monitoring_enabled);

 private:
  HWND owner_ = nullptr;
  NOTIFYICONDATAW data_{};
};

}  // namespace clipd2d::platform::win32
