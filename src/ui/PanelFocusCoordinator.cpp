#include "ui/PanelFocusCoordinator.h"

#include "common/Log.h"

namespace clipd2d::ui {

void PanelFocusCoordinator::ResetForHide() {
  last_external_focus_context_.reset();
  focus_restore_context_.reset();
}

void PanelFocusCoordinator::ObserveExternalWindow(
    HWND window,
    const WindowPredicate& is_owned_window,
    const WindowPredicate& is_menu_popup_window) {
  if (window == nullptr || (is_owned_window && is_owned_window(window)) ||
      (is_menu_popup_window && is_menu_popup_window(window))) {
    return;
  }
  const core::clipboard::PasteTargetContext context = core::clipboard::CapturePasteTargetContext(window);
  if (!core::clipboard::IsValidPasteTargetContext(context)) {
    return;
  }
  paste_target_context_ = context;
  last_external_focus_context_ = context;
  common::WriteLog(
      common::LogLevel::Info,
      "ui.external_focus",
      "updated external context root=" + std::to_string(reinterpret_cast<std::uintptr_t>(context.root_window)) +
          " focus=" + std::to_string(reinterpret_cast<std::uintptr_t>(context.focus_window)));
}

void PanelFocusCoordinator::CaptureRestoreContext(
    HWND foreground,
    const WindowPredicate& is_owned_window,
    const WindowPredicate& is_menu_popup_window) {
  focus_restore_context_.reset();
  if (foreground != nullptr &&
      !(is_owned_window && is_owned_window(foreground)) &&
      !(is_menu_popup_window && is_menu_popup_window(foreground))) {
    const core::clipboard::PasteTargetContext current_context = core::clipboard::CapturePasteTargetContext(foreground);
    if (core::clipboard::IsValidPasteTargetContext(current_context)) {
      focus_restore_context_ = current_context;
      paste_target_context_ = current_context;
      last_external_focus_context_ = current_context;
    }
  }
  if (!focus_restore_context_.has_value()) {
    focus_restore_context_ = BuildRestoreContext();
  }
  if (focus_restore_context_.has_value()) {
    common::WriteLog(
        common::LogLevel::Info,
        "ui.focus_restore",
        "captured restore context root=" + std::to_string(reinterpret_cast<std::uintptr_t>(focus_restore_context_->root_window)) +
            " focus=" + std::to_string(reinterpret_cast<std::uintptr_t>(focus_restore_context_->focus_window)));
  }
}

bool PanelFocusCoordinator::RestoreCapturedContext(
    const WindowPredicate& is_owned_window,
    const WindowPredicate& is_menu_popup_window) {
  if (!focus_restore_context_.has_value()) {
    return false;
  }
  const core::clipboard::PasteTargetContext context = *focus_restore_context_;
  focus_restore_context_.reset();
  if (!core::clipboard::IsValidPasteTargetContext(context)) {
    return false;
  }
  const bool restored = RestoreContext(context);
  if (restored) {
    ObserveExternalWindow(GetForegroundWindow(), is_owned_window, is_menu_popup_window);
  }
  return restored;
}

bool PanelFocusCoordinator::RestoreExplicitContext(
    const core::clipboard::PasteTargetContext& context,
    const WindowPredicate& is_owned_window,
    const WindowPredicate& is_menu_popup_window) {
  if (!core::clipboard::IsValidPasteTargetContext(context)) {
    return false;
  }
  const bool restored = RestoreContext(context);
  if (restored) {
    ObserveExternalWindow(GetForegroundWindow(), is_owned_window, is_menu_popup_window);
  }
  return restored;
}

std::optional<core::clipboard::PasteTargetContext> PanelFocusCoordinator::BuildRestoreContext() const {
  if (last_external_focus_context_.has_value() &&
      core::clipboard::IsValidPasteTargetContext(*last_external_focus_context_)) {
    return last_external_focus_context_;
  }
  return std::nullopt;
}

core::clipboard::PasteTargetContext PanelFocusCoordinator::CapturePreferredPasteTarget(
    HWND foreground,
    const WindowPredicate& is_owned_window,
    const WindowPredicate& is_menu_popup_window) const {
  HWND target_window = nullptr;
  if (core::clipboard::IsValidPasteTargetContext(paste_target_context_)) {
    target_window = paste_target_context_.root_window;
  }
  if (foreground != nullptr &&
      !(is_owned_window && is_owned_window(foreground)) &&
      !(is_menu_popup_window && is_menu_popup_window(foreground))) {
    target_window = foreground;
  }
  if (target_window == nullptr) {
    return {};
  }
  return core::clipboard::CapturePasteTargetContext(target_window);
}

bool PanelFocusCoordinator::RestoreContext(const core::clipboard::PasteTargetContext& context) {
  if (!core::clipboard::IsValidPasteTargetContext(context)) {
    return false;
  }

  HWND target_root = context.root_window;
  if (IsIconic(target_root)) {
    ShowWindow(target_root, SW_RESTORE);
  }

  const HWND foreground_window = GetForegroundWindow();
  const DWORD current_thread_id = GetCurrentThreadId();
  const DWORD target_thread_id =
      context.thread_id != 0 ? context.thread_id : GetWindowThreadProcessId(target_root, nullptr);
  const DWORD foreground_thread_id =
      foreground_window == nullptr ? 0 : GetWindowThreadProcessId(foreground_window, nullptr);

  bool attached_foreground = false;
  bool attached_target = false;
  if (foreground_thread_id != 0 && foreground_thread_id != current_thread_id) {
    attached_foreground = AttachThreadInput(current_thread_id, foreground_thread_id, TRUE) == TRUE;
  }
  if (target_thread_id != 0 &&
      target_thread_id != current_thread_id &&
      target_thread_id != foreground_thread_id) {
    attached_target = AttachThreadInput(current_thread_id, target_thread_id, TRUE) == TRUE;
  }

  BringWindowToTop(target_root);
  SetForegroundWindow(target_root);
  SetActiveWindow(target_root);
  if (context.focus_window != nullptr &&
      IsWindow(context.focus_window) &&
      GetAncestor(context.focus_window, GA_ROOT) == target_root) {
    SetFocus(context.focus_window);
  }

  if (attached_target) {
    AttachThreadInput(current_thread_id, target_thread_id, FALSE);
  }
  if (attached_foreground) {
    AttachThreadInput(current_thread_id, foreground_thread_id, FALSE);
  }

  const HWND restored_foreground = GetForegroundWindow();
  return GetAncestor(restored_foreground, GA_ROOT) == target_root;
}

}  // namespace clipd2d::ui
