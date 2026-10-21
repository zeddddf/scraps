#include "ui/MainWindowInternal.h"

// Responsibility: Panel auto-hide hooks, external-click handling, Esc hide behavior, and interaction-guard plumbing.
// Inputs: Global foreground/mouse/keyboard events plus MainWindow visibility, lock state, and owned-window relationships.
// Outputs: Hide/retain decisions routed back into MainWindow via posted messages or direct HideWindow() calls.
// Forbidden dependencies: List rendering, repository queries, clipboard persistence, or unrelated panel action workflows.

namespace clipd2d::ui {

void CALLBACK MainWindow::ForegroundChangedProc(
    HWINEVENTHOOK,
    DWORD,
    HWND hwnd,
    LONG object_id,
    LONG,
    DWORD,
    DWORD) {
  if (object_id != OBJID_WINDOW || g_hook_owner == nullptr) {
    return;
  }
  if (hwnd != nullptr &&
      !g_hook_owner->IsOwnedWindow(hwnd) &&
      !g_hook_owner->IsMenuPopupWindow(hwnd)) {
    if (detail::HasInteractionGuard(
            g_hook_owner->interaction_guard_state_,
            detail::InteractionGuardKind::PopupMenu)) {
      g_hook_owner->DismissOwnedPopupMenus();
    }
    PostMessageW(g_hook_owner->hwnd_, kUpdatePasteTargetMessage, reinterpret_cast<WPARAM>(hwnd), 0);
  }
}

LRESULT CALLBACK MainWindow::LowLevelMouseProc(int code, WPARAM w_param, LPARAM l_param) {
  if (code >= 0 && g_hook_owner != nullptr) {
    switch (w_param) {
      case WM_LBUTTONDOWN:
      case WM_RBUTTONDOWN:
      case WM_MBUTTONDOWN:
      case WM_XBUTTONDOWN: {
        const auto* info = reinterpret_cast<const MSLLHOOKSTRUCT*>(l_param);
        if (info != nullptr && IsWindowVisible(g_hook_owner->hwnd_)) {
          if (detail::HasInteractionGuard(
                  g_hook_owner->interaction_guard_state_,
                  detail::InteractionGuardKind::PopupMenu)) {
            const HWND hit = WindowFromPoint(info->pt);
            if ((hit == nullptr || !g_hook_owner->IsMenuPopupWindow(hit)) &&
                !g_hook_owner->IsPanelMenuTogglePoint(info->pt)) {
              g_hook_owner->DismissOwnedPopupMenus();
            }
          }
        }
        if (info != nullptr && IsWindowVisible(g_hook_owner->hwnd_) && !g_hook_owner->panel_locked_) {
          const auto payload_value = g_hook_owner->BuildExternalPointerMessagePayload(info->pt);
          if (payload_value.disposition != detail::ExternalPointerDisposition::Ignore) {
            // Q1:payload 所有权随消息交接,失败由 PostOwnedMessage 回收。
            (void)PostOwnedMessage(
                g_hook_owner->hwnd_,
                kHandleExternalPointerMessage,
                0,
                std::make_unique<ExternalPointerMessagePayload>(payload_value));
          }
        }
        break;
      }
    }
  }
  return CallNextHookEx(nullptr, code, w_param, l_param);
}

LRESULT CALLBACK MainWindow::LowLevelKeyboardProc(int code, WPARAM w_param, LPARAM l_param) {
  if (code >= 0 && g_hook_owner != nullptr && IsWindowVisible(g_hook_owner->hwnd_)) {
    if (w_param == WM_KEYDOWN || w_param == WM_SYSKEYDOWN) {
      const auto* info = reinterpret_cast<const KBDLLHOOKSTRUCT*>(l_param);
      if (info != nullptr) {
        const HWND foreground = GetForegroundWindow();
        const UINT modifiers = platform::win32::GetPressedHotkeyModifiers();
        if (!g_hook_owner->IsOwnedWindow(foreground)) {
          if (info->vkCode == VK_ESCAPE &&
              modifiers == 0) {
            if (detail::HasInteractionGuard(
                    g_hook_owner->interaction_guard_state_,
                    detail::InteractionGuardKind::PopupMenu)) {
              g_hook_owner->DismissOwnedPopupMenus();
              return 1;
            }
            if (!g_hook_owner->panel_locked_) {
              PostMessageW(g_hook_owner->hwnd_, WM_CLOSE, 0, 0);
              return 1;
            }
          }
          if (g_hook_owner->IsSearchFocusHotkey(info->vkCode, modifiers)) {
            PostMessageW(g_hook_owner->hwnd_, kFocusSearchMessage, 1, 0);
            return 1;
          }
        }
      }
    }
  }
  return CallNextHookEx(nullptr, code, w_param, l_param);
}

void MainWindow::NotifySettingsWindowShown() {
  paste_route_requires_recovery_ = true;
  SetInteractionGuard(detail::InteractionGuardKind::SettingsWindow, true);
}

void MainWindow::NotifySettingsWindowHidden() {
  SetInteractionGuard(detail::InteractionGuardKind::SettingsWindow, false);
}

void MainWindow::NotifyPreviewWindowShown() {
  paste_route_requires_recovery_ = true;
  SetInteractionGuard(detail::InteractionGuardKind::PreviewWindow, true);
}

void MainWindow::NotifyPreviewWindowHidden() {
  SetInteractionGuard(detail::InteractionGuardKind::PreviewWindow, false);
}

bool MainWindow::IsMenuPopupWindow(HWND hwnd) const {
  if (hwnd == nullptr) {
    return false;
  }
  wchar_t class_name[32]{};
  if (GetClassNameW(hwnd, class_name, static_cast<int>(std::size(class_name))) <= 0) {
    return false;
  }
  return std::wcscmp(class_name, L"#32768") == 0 ||
      std::wcscmp(class_name, L"Scraps.PopupMenuWindow") == 0;
}

bool MainWindow::IsInternalInteractionPoint(POINT screen_point) const {
  if (hwnd_ == nullptr) {
    return false;
  }
  const HWND hit = WindowFromPoint(screen_point);
  if (hit == nullptr) {
    return false;
  }
  if (IsOwnedWindow(hit)) {
    return true;
  }
  return detail::HasInteractionGuard(interaction_guard_state_, detail::InteractionGuardKind::PopupMenu) &&
      IsMenuPopupWindow(hit);
}

MainWindow::ExternalPointerMessagePayload MainWindow::BuildExternalPointerMessagePayload(POINT screen_point) const {
  ExternalPointerMessagePayload payload;
  payload.screen_point = screen_point;
  const auto capture = detail::BuildExternalPointerCapture(
      interaction_guard_state_,
      IsInternalInteractionPoint(screen_point));
  payload.disposition = capture.disposition;
  payload.deferred_guard = capture.deferred_guard;
  return payload;
}

void MainWindow::HandleExternalPointerDown(const ExternalPointerMessagePayload& payload) {
  if (!IsWindowVisible(hwnd_)) {
    return;
  }

  if (panel_locked_) {
    return;
  }

  switch (payload.disposition) {
    case detail::ExternalPointerDisposition::Ignore:
      return;
    case detail::ExternalPointerDisposition::HideImmediately:
      HideWindow();
      return;
    case detail::ExternalPointerDisposition::DeferHide:
      if (!detail::HasInteractionGuard(interaction_guard_state_, payload.deferred_guard)) {
        if (detail::CurrentGuardHidePolicy(interaction_guard_state_) != detail::InteractionHidePolicy::SwallowExternalHide) {
          HideWindow();
        }
        return;
      }
      detail::RecordDeferredExternalHide(&interaction_guard_state_, payload.screen_point);
      if (payload.deferred_guard == detail::InteractionGuardKind::PopupMenu) {
        DismissOwnedPopupMenus();
      }
      return;
  }
}

void MainWindow::SetInteractionGuard(detail::InteractionGuardKind kind, bool active) {
  if (kind != detail::InteractionGuardKind::PopupMenu) {
    detail::SetVisibilityInteractionGuard(&interaction_guard_state_, kind, active);
    return;
  }

  if (active) {
    detail::BeginInteractionGuard(&interaction_guard_state_, kind);
    return;
  }
  if (detail::EndInteractionGuard(&interaction_guard_state_, kind)) {
    FlushDeferredExternalHideIfNeeded();
  }
}

void MainWindow::FlushDeferredExternalHideIfNeeded() {
  if (detail::FlushDeferredExternalHideIfNeeded(&interaction_guard_state_)) {
    if (!panel_locked_ && hwnd_ != nullptr) {
      PostMessageW(hwnd_, WM_CLOSE, 0, 0);
    }
  }
}

void MainWindow::StartAutoHideHooks() {
  if (auto_hide_hooks_active_) {
    return;
  }
  g_hook_owner = this;
  foreground_hook_ = SetWinEventHook(
      EVENT_SYSTEM_FOREGROUND,
      EVENT_SYSTEM_FOREGROUND,
      nullptr,
      &MainWindow::ForegroundChangedProc,
      0,
      0,
      WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
  mouse_hook_ = SetWindowsHookExW(WH_MOUSE_LL, &MainWindow::LowLevelMouseProc, instance_, 0);
  keyboard_hook_ = SetWindowsHookExW(WH_KEYBOARD_LL, &MainWindow::LowLevelKeyboardProc, instance_, 0);
  auto_hide_hooks_active_ = foreground_hook_ != nullptr || mouse_hook_ != nullptr || keyboard_hook_ != nullptr;
}

void MainWindow::StopAutoHideHooks() {
  if (foreground_hook_ != nullptr) {
    UnhookWinEvent(foreground_hook_);
    foreground_hook_ = nullptr;
  }
  if (mouse_hook_ != nullptr) {
    UnhookWindowsHookEx(mouse_hook_);
    mouse_hook_ = nullptr;
  }
  if (keyboard_hook_ != nullptr) {
    UnhookWindowsHookEx(keyboard_hook_);
    keyboard_hook_ = nullptr;
  }
  if (g_hook_owner == this) {
    g_hook_owner = nullptr;
  }
  auto_hide_hooks_active_ = false;
}

void MainWindow::UpdateAutoHideHooks() {
  if (hwnd_ == nullptr) {
    return;
  }
  if (IsWindowVisible(hwnd_)) {
    StartAutoHideHooks();
  } else {
    StopAutoHideHooks();
  }
}

}  // namespace clipd2d::ui
