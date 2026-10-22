#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <windows.h>
#include <wrl/client.h>

#include "common/Status.h"
#include "core/store/ClipTypes.h"
#include "ui/D2dShell.h"

struct ID2D1Bitmap;

namespace clipd2d::ui {

namespace detail {

std::wstring ResolvePreviewImagePath(const core::store::ClipRecord& clip);
std::wstring ResolveExternalImageOpenPath(const core::store::ClipRecord& clip);

}  // namespace detail

class PreviewWindow {
 public:
  struct HostActions {
    std::function<common::Status(std::int64_t, bool)> on_request_ocr;
    std::function<common::Status(std::int64_t)> on_copy_ocr_text;
    std::function<common::Status(std::int64_t)> on_paste_ocr_text;
    std::function<common::Status()> on_copy_image;
    std::function<common::Status(std::wstring*)> query_ocr_provider_status;
    std::function<void()> on_show;
    std::function<void()> on_hide;
    bool enable_image_ocr = true;
    std::wstring image_copy_label;
  };

  PreviewWindow() = default;
  ~PreviewWindow();

  common::Status Create(HINSTANCE instance, HWND owner);
  void Destroy();
  void ShowClip(const core::store::ClipRecord& clip, HostActions actions);
  HWND hwnd() const { return hwnd_; }
  bool ShowingClip(std::int64_t clip_id) const;

 private:
  static constexpr int kIdHtmlPlain = 3001;
  static constexpr int kIdHtmlSource = 3002;
  static constexpr int kIdOpenExternal = 3003;
  static constexpr int kIdRichEdit = 3004;
  static constexpr int kIdOcrCopy = 3005;
  static constexpr int kIdOcrPaste = 3006;
  static constexpr int kIdOcrRerun = 3007;
  static constexpr int kIdImageCopy = 3008;

  static LRESULT CALLBACK StaticWindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param);
  LRESULT WindowProc(UINT message, WPARAM w_param, LPARAM l_param);

  common::Status CreateControls();
  void ApplyPreviewRichEditTheme();
  void LayoutControls();
  void RenderFrame();
  void UpdateVisibleControls();
  void LoadPreviewImage();
  void UpdateRichEditContent();
  void RefreshOcrState();
  void TriggerAutoOcrIfNeeded();
  void SetInteractionGuardActive(bool active);
  void OpenWithSystemDefaultApp();

  HINSTANCE instance_ = nullptr;
  HWND owner_ = nullptr;
  HWND hwnd_ = nullptr;
  HWND html_plain_button_ = nullptr;
  HWND html_source_button_ = nullptr;
  HWND open_external_button_ = nullptr;
  HWND ocr_copy_button_ = nullptr;
  HWND ocr_paste_button_ = nullptr;
  HWND ocr_rerun_button_ = nullptr;
  HWND image_copy_button_ = nullptr;
  HWND rich_edit_ = nullptr;
  HFONT ui_font_ = nullptr;
  HMODULE rich_edit_module_ = nullptr;
  D2dShell d2d_shell_;
  HostActions host_actions_;
  std::optional<core::store::ClipRecord> clip_;
  bool interaction_guard_active_ = false;
  bool show_html_source_ = false;
  std::wstring ocr_provider_message_;
  Microsoft::WRL::ComPtr<ID2D1Bitmap> preview_bitmap_;
  UINT preview_width_ = 0;
  UINT preview_height_ = 0;
};

}  // namespace clipd2d::ui
