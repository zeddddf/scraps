#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include <windows.h>
#include <wrl/client.h>

#include "common/Status.h"
#include "core/browser/QuickPasteQuery.h"
#include "ui/D2dShell.h"

namespace clipd2d::ui {

class QuickPasteWindow {
 public:
  struct HostActions {
    std::function<void(int index, bool plain_text)> on_accept;
    std::function<void()> on_close;
  };

  static constexpr UINT kMoveSelectionMessage = WM_APP + 200;
  static constexpr UINT kAcceptCurrentMessage = WM_APP + 201;
  static constexpr UINT kAcceptIndexMessage = WM_APP + 202;
  static constexpr UINT kCloseWindowMessage = WM_APP + 203;

  ~QuickPasteWindow();

  common::Status Create(HINSTANCE instance, HWND owner);
  void Show(
      const POINT& anchor_screen,
      const std::vector<core::browser::QuickPasteCandidate>& candidates,
      int selected_index,
      core::store::WindowBlurType window_blur_type,
      int window_blur_opacity,
      HostActions actions);
  void Hide();
  void Destroy();

  HWND hwnd() const { return hwnd_; }
  bool visible() const { return visible_; }
  bool ContainsScreenPoint(POINT point) const;

 private:
  struct CandidateState {
    core::browser::QuickPasteCandidate candidate;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> source_icon;
  };

  static LRESULT CALLBACK StaticWindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param);
  LRESULT WindowProc(UINT message, WPARAM w_param, LPARAM l_param);

  void LayoutWindow();
  void RenderFrame();
  void EnsureBitmaps(ID2D1RenderTarget* target);
  void ReleaseBitmaps();
  void SetSelectedIndex(int index);
  void MoveSelection(int delta);
  void AcceptSelected(bool plain_text);
  void AcceptIndex(int index, bool plain_text);
  int HitTestCandidate(POINT client_point) const;

  HINSTANCE instance_ = nullptr;
  HWND owner_ = nullptr;
  HWND hwnd_ = nullptr;
  D2dShell d2d_shell_;
  HostActions host_actions_;
  std::vector<CandidateState> candidates_;
  std::vector<RECT> row_rects_;
  POINT anchor_screen_{};
  int selected_index_ = 0;
  int hover_index_ = -1;
  bool visible_ = false;
  bool shown_above_anchor_ = false;
  bool needs_layout_ = false;
  core::store::WindowBlurType window_blur_type_ = core::store::WindowBlurType::Mica;
  int window_blur_opacity_ = 50;
  std::uint64_t bitmap_generation_ = 0;
};

}  // namespace clipd2d::ui
