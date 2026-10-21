#include "platform/win32/ClipboardEventReceiver.h"

namespace clipd2d::platform::win32 {

namespace {

constexpr wchar_t kClipboardEventReceiverClassName[] = L"Scraps.ClipboardEventReceiver";

}  // namespace

ClipboardEventReceiver::~ClipboardEventReceiver() {
  Destroy();
}

void ClipboardEventReceiver::SetCallback(ClipboardUpdateCallback callback) {
  callback_ = std::move(callback);
}

common::Status ClipboardEventReceiver::Create(HINSTANCE instance) {
  WNDCLASSEXW window_class{};
  window_class.cbSize = sizeof(window_class);
  window_class.lpfnWndProc = &ClipboardEventReceiver::StaticWindowProc;
  window_class.hInstance = instance;
  window_class.lpszClassName = kClipboardEventReceiverClassName;
  RegisterClassExW(&window_class);

  hwnd_ = CreateWindowExW(
      0,
      kClipboardEventReceiverClassName,
      L"Scraps Clipboard Event Receiver",
      0,
      0,
      0,
      0,
      0,
      HWND_MESSAGE,
      nullptr,
      instance,
      this);
  if (hwnd_ == nullptr) {
    return common::Status::Error(common::StatusCode::PlatformError, "Create clipboard event receiver failed.");
  }

  baseline_sequence_ = GetClipboardSequenceNumber();
  baseline_guard_active_ = true;
  return common::Status::Ok();
}

common::Status ClipboardEventReceiver::SetMonitoringEnabled(bool enabled) {
  if (hwnd_ == nullptr) {
    return common::Status::Error(common::StatusCode::Unavailable, "Clipboard event receiver is not created.");
  }
  if (enabled == monitoring_enabled_) {
    return common::Status::Ok();
  }

  if (enabled) {
    if (!AddClipboardFormatListener(hwnd_)) {
      return common::Status::Error(common::StatusCode::PlatformError, "Clipboard receiver registration failed.");
    }
    baseline_sequence_ = GetClipboardSequenceNumber();
    baseline_guard_active_ = true;
  } else {
    RemoveClipboardFormatListener(hwnd_);
  }

  monitoring_enabled_ = enabled;
  return common::Status::Ok();
}

void ClipboardEventReceiver::Destroy() {
  if (hwnd_ != nullptr) {
    RemoveClipboardFormatListener(hwnd_);
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
}

void ClipboardEventReceiver::HandleClipboardUpdate() {
  if (!monitoring_enabled_ || !callback_) {
    return;
  }

  const DWORD sequence = GetClipboardSequenceNumber();
  if (sequence == 0) {
    return;
  }
  if (baseline_guard_active_ && sequence <= baseline_sequence_) {
    return;
  }
  baseline_guard_active_ = false;
  baseline_sequence_ = sequence;

  HWND source_window = GetClipboardOwner();
  if (source_window == nullptr) {
    source_window = GetForegroundWindow();
  }
  callback_(sequence, source_window);
}

LRESULT CALLBACK ClipboardEventReceiver::StaticWindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
  ClipboardEventReceiver* self = nullptr;
  if (message == WM_NCCREATE) {
    auto* create_struct = reinterpret_cast<CREATESTRUCTW*>(l_param);
    self = static_cast<ClipboardEventReceiver*>(create_struct->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  } else {
    self = reinterpret_cast<ClipboardEventReceiver*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  }
  if (self != nullptr) {
    self->hwnd_ = hwnd;
    return self->WindowProc(message, w_param, l_param);
  }
  return DefWindowProcW(hwnd, message, w_param, l_param);
}

LRESULT ClipboardEventReceiver::WindowProc(UINT message, WPARAM w_param, LPARAM l_param) {
  switch (message) {
    case WM_CLIPBOARDUPDATE:
      HandleClipboardUpdate();
      return 0;
  }
  return DefWindowProcW(hwnd_, message, w_param, l_param);
}

}  // namespace clipd2d::platform::win32
