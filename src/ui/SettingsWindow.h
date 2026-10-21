#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

#include <windows.h>

#include "common/Status.h"
#include "core/store/ClipTypes.h"
#include "ui/D2dShell.h"
#include "ui/D2DWindowlessTextBox.h"
#include "ui/settings/SettingsTypes.h"
#include "ui/settings/ISettingsHost.h"
#include "ui/settings/ISettingsPanel.h"

namespace clipd2d::ui {

class SettingsWindow : public ISettingsHost {
 public:
  struct HostActions {
    std::function<bool(const core::store::AppSettings&)> on_save;
    std::function<bool()> on_clear_history;
    std::function<bool()> on_clear_image_cache;
    std::function<void()> on_open_data_directory;
    std::function<DataStats()> query_data_stats;
    std::function<void()> on_begin_input_focus;
    std::function<void()> on_end_input_focus;
    std::function<void()> on_show;
    std::function<void()> on_hide;
  };

  SettingsWindow();
  ~SettingsWindow() override;

  common::Status Create(HINSTANCE instance, HWND owner);
  void Show(const core::store::AppSettings& settings, HostActions actions);
  void Destroy();
  HWND hwnd() const { return hwnd_; }

  // ISettingsHost implementation
  D2dShell& GetShell() override { return d2d_shell_; }
  void AddHitRegion(const HitRegion& region) override;
  void RegisterFieldRect(FieldId field, const RECT& rect) override;
  void RegisterSelectRect(SelectField field, const RECT& rect) override;
  FieldId GetActiveField() const override { return active_field_; }
  SelectField GetActiveSelectField() const override { return active_select_field_; }
  const DataStats& GetDataStats() const override { return stats_; }
  const std::wstring& GetSearchText() const override { return search_text_; }
  int DipToPx(float dip) const override;
  float GetScrollY() const override { return scroll_y_; }
  std::wstring GetSelectLabel(SelectField field) const override;

 private:
  float scroll_y_ = 0.0f;
  float max_scroll_y_ = 0.0f;
  bool is_dragging_scrollbar_ = false;
  bool is_dragging_slider_ = false;
  int active_slider_index_ = -1;
  int drag_start_y_ = 0;
  float drag_start_scroll_y_ = 0.0f;

  static LRESULT CALLBACK StaticWindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param);
  LRESULT WindowProc(UINT message, WPARAM w_param, LPARAM l_param);

  common::Status CreateInputProxies();
  void DestroyInputProxies();
  void LayoutWindow();
  void RenderFrame();
  void RenderNavigation(ID2D1HwndRenderTarget* target);
  void RenderContent(ID2D1HwndRenderTarget* target);
  void RenderBottomBar(ID2D1HwndRenderTarget* target);

  void LoadDraftFromSettings(const core::store::AppSettings& settings);
  core::store::AppSettings BuildSettingsFromDraft() const;
  bool ValidateDraft(std::wstring* error_message) const;
  bool IsDraftDirty() const;
  void RefreshStats();
  void SetStatus(std::wstring text, bool error);
  void ClearStatus();
  void UpdateSearchSelection();
  void OpenSelectField(SelectField field);
  void CloseSelectField();
  int GetSelectOptionCount(SelectField field) const;
  int GetSelectSelectedIndex(SelectField field) const;
  std::wstring GetSelectOptionLabel(SelectField field, int option) const;
  void SetSelectOption(SelectField field, int option);
  void UpdateSelectMenuLayout();
  void SetWindowAllowsActivation(bool allow_activation);
  void SetInteractionGuardActive(bool active);

  void BeginEditing(FieldId field);
  void EndEditing(bool commit);
  void SyncActiveEditToState();
  void UpdateProxyLayout();
  void FocusNextField(bool reverse);
  bool IsHotkeyField(FieldId field) const;
  void CommitCapturedHotkey(UINT virtual_key);

  void HandlePrimaryAction(HitKind kind, int option);
  void HandleCloseRequest();
  bool ConfirmDiscardChanges();

  const HitRegion* HitTest(POINT point) const;

  HINSTANCE instance_ = nullptr;
  HWND owner_ = nullptr;
  HWND hwnd_ = nullptr;
  D2DWindowlessTextBox single_line_textbox_;
  D2DWindowlessTextBox multi_line_textbox_;
  D2dShell d2d_shell_;

  HostActions host_actions_;
  core::store::AppSettings current_settings_{};
  DraftState draft_{};
  DataStats stats_{};
  Section selected_section_ = Section::General;
  FieldId active_field_ = FieldId::None;
  std::wstring search_text_;
  std::wstring status_text_;
  bool status_error_ = false;
  bool status_persistent_ = false;

  RECT search_rect_{};
  RECT content_rect_{};
  RECT bottom_bar_rect_{};
  RECT save_button_rect_{};
  RECT cancel_button_rect_{};
  RECT status_rect_{};
  
  std::array<RECT, 7> nav_rects_{};
  RECT select_menu_rect_{};
  std::array<RECT, 4> select_option_rects_{};
  std::vector<HitRegion> hit_regions_;
  std::unordered_map<FieldId, RECT> field_rects_;
  std::unordered_map<SelectField, RECT> select_rects_;

  SelectField active_select_field_ = SelectField::None;
  int active_select_hover_index_ = -1;
  int active_nav_hover_index_ = -1;
  bool is_tracking_mouse_leave_ = false;
  bool window_allows_activation_ = false;
  bool interaction_guard_active_ = false;
  
  std::array<std::unique_ptr<ISettingsPanel>, 7> panels_;
};

}  // namespace clipd2d::ui
