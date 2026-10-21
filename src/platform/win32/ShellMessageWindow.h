#pragma once

#include <functional>

#include <windows.h>

#include "common/Status.h"

namespace clipd2d::platform::win32 {

class ShellMessageWindow {
 public:
  struct Callbacks {
    std::function<void()> on_hotkey;
    std::function<void()> on_quick_paste_hotkey;
    std::function<void()> on_screenshot_hotkey;
    std::function<void()> on_tray_activate;
    std::function<void()> on_tray_menu;
  };

  ShellMessageWindow() = default;
  ~ShellMessageWindow();

  void SetCallbacks(Callbacks callbacks);
  common::Status Create(HINSTANCE instance);
  common::Status RegisterHotkey(UINT modifiers, UINT virtual_key);
  common::Status RegisterQuickPasteHotkey(UINT modifiers, UINT virtual_key);
  common::Status RegisterScreenshotHotkey(UINT modifiers, UINT virtual_key);
  void UnregisterHotkey();
  void UnregisterQuickPasteHotkey();
  void UnregisterScreenshotHotkey();
  void Destroy();

  HWND hwnd() const { return hwnd_; }

 private:
  static LRESULT CALLBACK StaticWindowProc(
      HWND hwnd,
      UINT message,
      WPARAM w_param,
      LPARAM l_param);
  LRESULT WindowProc(UINT message, WPARAM w_param, LPARAM l_param);

  HWND hwnd_ = nullptr;
  bool hotkey_registered_ = false;
  bool quick_paste_hotkey_registered_ = false;
  bool screenshot_hotkey_registered_ = false;
  UINT registered_hotkey_id_ = 1;
  UINT registered_quick_paste_hotkey_id_ = 2;
  UINT registered_screenshot_hotkey_id_ = 3;
  Callbacks callbacks_;
};

}  // namespace clipd2d::platform::win32
