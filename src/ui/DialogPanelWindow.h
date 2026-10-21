#pragma once

#include <functional>
#include <string>
#include <vector>

#include <windows.h>

#include "common/Status.h"
#include "ui/D2DWindowlessTextBox.h"
#include "ui/D2dShell.h"

namespace clipd2d::ui {

class DialogPanelWindow {
 public:
  struct ColorOption {
    std::wstring label;
    std::wstring value;
  };

  struct Config {
    bool has_text_input = false;
    bool auto_focus_input = false;
    bool requires_non_empty_text = false;
    bool has_color_options = false;
    bool dangerous_confirm = false;
    std::wstring title;
    std::wstring prompt;
    std::wstring message;
    std::wstring confirm_text;
    std::wstring cancel_text;
    std::wstring initial_text;
    std::wstring empty_text_error;
    std::wstring initial_color;
    std::vector<ColorOption> color_options;
  };

  struct Result {
    bool confirmed = false;
    std::wstring text;
    std::wstring color;
  };

  struct HostActions {
    std::function<void()> on_show;
    std::function<void()> on_hide;
    std::function<void()> on_begin_input_focus;
    std::function<void()> on_end_input_focus;
    std::function<void(const Result&)> on_result;
  };

  ~DialogPanelWindow();

  common::Status Create(HINSTANCE instance, HWND owner);
  void Show(const Config& config, HostActions actions);
  void Destroy();
  HWND hwnd() const { return hwnd_; }
  bool visible() const { return visible_; }

 private:
  static constexpr UINT_PTR kCaretTimerId = 101;

  enum class HitKind {
    None = 0,
    Confirm,
    Cancel,
    ColorChip,
  };

  struct HitRegion {
    RECT rect{};
    HitKind kind = HitKind::None;
    int index = -1;
  };

  static LRESULT CALLBACK StaticWindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param);
  LRESULT WindowProc(UINT message, WPARAM w_param, LPARAM l_param);

  common::Status InitTextBox();
  void LayoutWindow();
  void RenderFrame();
  void SetWindowAllowsActivation(bool allow_activation);
  void SetInteractionGuardActive(bool active);
  void BeginInputFocus();
  void EndInputFocus(bool restore_external_focus);
  void CloseDialog(bool confirmed);
  void SyncTextBoxToState();
  void ClearError();
  const HitRegion* HitTest(POINT point) const;

  HINSTANCE instance_ = nullptr;
  HWND owner_ = nullptr;
  HWND hwnd_ = nullptr;
  D2DWindowlessTextBox text_box_;   // D2D 无窗口文本框（替代 Win32 EDIT）
  D2dShell d2d_shell_;

  HostActions host_actions_;
  Config config_{};
  std::wstring input_text_;
  std::wstring error_text_;
  std::wstring selected_color_;
  RECT message_rect_{};
  RECT input_rect_{};
  RECT error_rect_{};
  RECT confirm_rect_{};
  RECT cancel_rect_{};
  std::vector<RECT> color_rects_;
  std::vector<HitRegion> hit_regions_;
  POINT hover_point_{-1, -1};
  bool visible_ = false;
  bool interaction_guard_active_ = false;
  bool window_allows_activation_ = false;
  bool input_focus_active_ = false;
};

}  // namespace clipd2d::ui
