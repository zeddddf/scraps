#pragma once

#include <functional>

#include <windows.h>

#include "common/Status.h"

namespace clipd2d::platform::win32 {

class ClipboardEventReceiver {
 public:
  using ClipboardUpdateCallback = std::function<void(DWORD, HWND)>;

  ClipboardEventReceiver() = default;
  ~ClipboardEventReceiver();

  void SetCallback(ClipboardUpdateCallback callback);
  common::Status Create(HINSTANCE instance);
  common::Status SetMonitoringEnabled(bool enabled);
  void Destroy();

  HWND hwnd() const { return hwnd_; }

 private:
  static LRESULT CALLBACK StaticWindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param);
  LRESULT WindowProc(UINT message, WPARAM w_param, LPARAM l_param);
  void HandleClipboardUpdate();

  HWND hwnd_ = nullptr;
  bool monitoring_enabled_ = false;
  DWORD baseline_sequence_ = 0;
  bool baseline_guard_active_ = true;
  ClipboardUpdateCallback callback_;
};

}  // namespace clipd2d::platform::win32
