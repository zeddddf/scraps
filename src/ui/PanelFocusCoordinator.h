#pragma once

#include <functional>
#include <optional>

#include <windows.h>

#include "core/clipboard/ClipboardWriter.h"

namespace clipd2d::ui {

class PanelFocusCoordinator {
 public:
  using WindowPredicate = std::function<bool(HWND)>;

  void ResetForHide();

  void ObserveExternalWindow(HWND window, const WindowPredicate& is_owned_window, const WindowPredicate& is_menu_popup_window);

  void CaptureRestoreContext(HWND foreground, const WindowPredicate& is_owned_window, const WindowPredicate& is_menu_popup_window);
  bool RestoreCapturedContext(const WindowPredicate& is_owned_window, const WindowPredicate& is_menu_popup_window);
  bool RestoreExplicitContext(
      const core::clipboard::PasteTargetContext& context,
      const WindowPredicate& is_owned_window,
      const WindowPredicate& is_menu_popup_window);

  std::optional<core::clipboard::PasteTargetContext> BuildRestoreContext() const;
  core::clipboard::PasteTargetContext CapturePreferredPasteTarget(
      HWND foreground,
      const WindowPredicate& is_owned_window,
      const WindowPredicate& is_menu_popup_window) const;

  const core::clipboard::PasteTargetContext& paste_target_context() const { return paste_target_context_; }
  void set_paste_target_context(const core::clipboard::PasteTargetContext& context) { paste_target_context_ = context; }

 private:
  bool RestoreContext(const core::clipboard::PasteTargetContext& context);

  core::clipboard::PasteTargetContext paste_target_context_{};
  std::optional<core::clipboard::PasteTargetContext> last_external_focus_context_;
  std::optional<core::clipboard::PasteTargetContext> focus_restore_context_;
};

}  // namespace clipd2d::ui
