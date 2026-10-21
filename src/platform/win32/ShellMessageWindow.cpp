#include "platform/win32/ShellMessageWindow.h"

#include "platform/win32/NotifyIconHost.h"

namespace clipd2d::platform::win32 {

namespace {

constexpr wchar_t kShellMessageWindowClassName[] = L"Scraps.ShellMessageWindow";

}  // namespace

ShellMessageWindow::~ShellMessageWindow() {
  Destroy();
}

void ShellMessageWindow::SetCallbacks(Callbacks callbacks) {
  callbacks_ = std::move(callbacks);
}

common::Status ShellMessageWindow::Create(HINSTANCE instance) {
  WNDCLASSEXW window_class{};
  window_class.cbSize = sizeof(window_class);
  window_class.lpfnWndProc = &ShellMessageWindow::StaticWindowProc;
  window_class.hInstance = instance;
  window_class.lpszClassName = kShellMessageWindowClassName;
  RegisterClassExW(&window_class);

  hwnd_ = CreateWindowExW(
      0,
      kShellMessageWindowClassName,
      L"Scraps Shell Message Window",
      0,
      0,
      0,
      0,
      0,
      nullptr,
      nullptr,
      instance,
      this);
  if (hwnd_ == nullptr) {
    return common::Status::Error(common::StatusCode::PlatformError, "Create shell message window failed.");
  }

  ShowWindow(hwnd_, SW_HIDE);
  return common::Status::Ok();
}

common::Status ShellMessageWindow::RegisterHotkey(UINT modifiers, UINT virtual_key) {
  if (hwnd_ == nullptr) {
    return common::Status::Error(common::StatusCode::Unavailable, "Shell message window is not created.");
  }

  if (hotkey_registered_) {
    ::UnregisterHotKey(hwnd_, registered_hotkey_id_);
    hotkey_registered_ = false;
  }

  if (!::RegisterHotKey(hwnd_, registered_hotkey_id_, modifiers | MOD_NOREPEAT, virtual_key)) {
    return common::Status::Error(common::StatusCode::PlatformError, "Register hotkey failed.");
  }
  hotkey_registered_ = true;
  return common::Status::Ok();
}

common::Status ShellMessageWindow::RegisterQuickPasteHotkey(UINT modifiers, UINT virtual_key) {
  if (hwnd_ == nullptr) {
    return common::Status::Error(common::StatusCode::Unavailable, "Shell message window is not created.");
  }

  if (quick_paste_hotkey_registered_) {
    ::UnregisterHotKey(hwnd_, registered_quick_paste_hotkey_id_);
    quick_paste_hotkey_registered_ = false;
  }

  if (!::RegisterHotKey(hwnd_, registered_quick_paste_hotkey_id_, modifiers | MOD_NOREPEAT, virtual_key)) {
    return common::Status::Error(common::StatusCode::PlatformError, "Register quick paste hotkey failed.");
  }
  quick_paste_hotkey_registered_ = true;
  return common::Status::Ok();
}

common::Status ShellMessageWindow::RegisterScreenshotHotkey(UINT modifiers, UINT virtual_key) {
  if (hwnd_ == nullptr) {
    return common::Status::Error(common::StatusCode::Unavailable, "Shell message window is not created.");
  }

  if (screenshot_hotkey_registered_) {
    ::UnregisterHotKey(hwnd_, registered_screenshot_hotkey_id_);
    screenshot_hotkey_registered_ = false;
  }

  if (!::RegisterHotKey(hwnd_, registered_screenshot_hotkey_id_, modifiers | MOD_NOREPEAT, virtual_key)) {
    return common::Status::Error(common::StatusCode::PlatformError, "Register screenshot hotkey failed.");
  }
  screenshot_hotkey_registered_ = true;
  return common::Status::Ok();
}

void ShellMessageWindow::UnregisterHotkey() {
  if (hwnd_ != nullptr && hotkey_registered_) {
    ::UnregisterHotKey(hwnd_, registered_hotkey_id_);
    hotkey_registered_ = false;
  }
}

void ShellMessageWindow::UnregisterQuickPasteHotkey() {
  if (hwnd_ != nullptr && quick_paste_hotkey_registered_) {
    ::UnregisterHotKey(hwnd_, registered_quick_paste_hotkey_id_);
    quick_paste_hotkey_registered_ = false;
  }
}

void ShellMessageWindow::UnregisterScreenshotHotkey() {
  if (hwnd_ != nullptr && screenshot_hotkey_registered_) {
    ::UnregisterHotKey(hwnd_, registered_screenshot_hotkey_id_);
    screenshot_hotkey_registered_ = false;
  }
}

void ShellMessageWindow::Destroy() {
  if (hwnd_ != nullptr) {
    UnregisterHotkey();
    UnregisterQuickPasteHotkey();
    UnregisterScreenshotHotkey();
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
}

LRESULT CALLBACK ShellMessageWindow::StaticWindowProc(
    HWND hwnd,
    UINT message,
    WPARAM w_param,
    LPARAM l_param) {
  ShellMessageWindow* self = nullptr;
  if (message == WM_NCCREATE) {
    auto* create_struct = reinterpret_cast<CREATESTRUCTW*>(l_param);
    self = static_cast<ShellMessageWindow*>(create_struct->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  } else {
    self = reinterpret_cast<ShellMessageWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  }
  if (self != nullptr) {
    self->hwnd_ = hwnd;
    return self->WindowProc(message, w_param, l_param);
  }
  return DefWindowProcW(hwnd, message, w_param, l_param);
}

LRESULT ShellMessageWindow::WindowProc(UINT message, WPARAM w_param, LPARAM l_param) {
  switch (message) {
    case WM_HOTKEY:
      if (w_param == registered_hotkey_id_ && callbacks_.on_hotkey) {
        callbacks_.on_hotkey();
      } else if (w_param == registered_quick_paste_hotkey_id_ && callbacks_.on_quick_paste_hotkey) {
        callbacks_.on_quick_paste_hotkey();
      } else if (w_param == registered_screenshot_hotkey_id_ && callbacks_.on_screenshot_hotkey) {
        callbacks_.on_screenshot_hotkey();
      }
      return 0;

    case NotifyIconHost::kTrayMessage:
      if (l_param == WM_LBUTTONDBLCLK && callbacks_.on_tray_activate) {
        callbacks_.on_tray_activate();
      } else if ((l_param == WM_RBUTTONUP || l_param == WM_CONTEXTMENU) && callbacks_.on_tray_menu) {
        callbacks_.on_tray_menu();
      } else if (l_param == NIN_BALLOONUSERCLICK && callbacks_.on_balloon_click) {
        callbacks_.on_balloon_click();
      }
      return 0;

    case kUpdateAvailableMessage: {
      auto* payload = reinterpret_cast<std::pair<std::wstring, std::wstring>*>(l_param);
      if (payload != nullptr) {
        if (callbacks_.on_update_available) {
          callbacks_.on_update_available(payload->first, payload->second);
        }
        delete payload;
      }
      return 0;
    }
  }

  return DefWindowProcW(hwnd_, message, w_param, l_param);
}

}  // namespace clipd2d::platform::win32
