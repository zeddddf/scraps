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
#include "ui/HoverHintWindow.h"
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
    // C3 外观即时预览：毛玻璃类型/透明度的未保存草稿值实时套用到主面板。
    std::function<void(const core::store::AppSettings&)> on_preview_appearance;
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
  int DipToPx(float dip) const override;
  float GetScrollY() const override { return scroll_y_; }
  std::wstring GetSelectLabel(SelectField field) const override;
  void QueueStickyTitle(const FlexNode* node, const wchar_t* text) override;

 private:
  float scroll_y_ = 0.0f;
  float max_scroll_y_ = 0.0f;
  bool is_dragging_scrollbar_ = false;
  bool is_dragging_slider_ = false;
  int active_slider_index_ = -1;
  int drag_start_y_ = 0;
  float drag_start_scroll_y_ = 0.0f;
  // C2 窄 rail：图标悬停时显示节名提示。
  HoverHintWindow hover_hint_;
  int nav_hover_hint_index_ = -1;
  // rail 分割栏可拖拽：宽度跟随拖拽（dip），双击复位默认窄 rail。
  float nav_width_dip_ = 56.0f;
  bool is_dragging_nav_splitter_ = false;
  bool nav_splitter_hover_ = false;

  static LRESULT CALLBACK StaticWindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param);
  LRESULT WindowProc(UINT message, WPARAM w_param, LPARAM l_param);

  common::Status CreateInputProxies();
  void ApplyThemeToTextboxes();
  void UpdateNavHoverHint(int nav_hover, POINT client_point);
  void PreviewAppearanceChanges();
  void DestroyInputProxies();
  void LayoutWindow();
  void RenderFrame();
  void RenderNavigation(ID2D1HwndRenderTarget* target);
  void RenderContent(ID2D1HwndRenderTarget* target);
  void RenderBottomBar(ID2D1HwndRenderTarget* target);
  void RenderScrollbar(ID2D1HwndRenderTarget* target);
  D2D1_RECT_F ScrollbarThumbRect() const;
  // rail 分割栏：命中/绘制/拖拽共用同一几何。
  RECT NavSplitterRect() const;
  void RenderNavSplitter(ID2D1HwndRenderTarget* target);
  // rail 宽度达到阈值后切换为「图标+文字」形态，悬停提示随之停用。
  bool NavWide() const { return nav_width_dip_ >= 96.0f; }
  void FlushStickyTitles(ID2D1HwndRenderTarget* target);
  void RebuildFocusableRegions();
  void DrawKeyboardFocusRing(ID2D1HwndRenderTarget* target);
  void MoveKeyboardFocus(int delta);
  void ScrollFocusIntoView();
  void ActivateKeyboardFocus(UINT virtual_key);

  void LoadDraftFromSettings(const core::store::AppSettings& settings);
  core::store::AppSettings BuildSettingsFromDraft() const;
  bool ValidateDraft(std::wstring* error_message) const;
  // 分节脏检查：导航脏点与保存按钮态都基于它；IsDraftDirty = 任意节脏。
  bool IsSectionDirty(Section section, const core::store::AppSettings& next) const;
  unsigned ComputeDirtyMask() const;
  bool IsDraftDirty() const;
  void ResetSectionToDefaults(Section section);
  void RefreshStats();
  void SetStatus(std::wstring text, bool error);
  void ClearStatus();

  // ── 跨节搜索（A1/B5）──────────────────────────────────────────────
  // 行级索引：遍历 7 个面板的 BuildRows，按 title/subtitle 匹配，
  // 结果列表带节标签，↑↓ 选择、Enter 跳转；输入时自动预览首个命中。
  struct SearchHit {
    Section section = Section::General;
    std::wstring title;
    std::wstring subtitle;
    HitKind hit_kind = HitKind::Toggle;
    FieldId field_id = FieldId::None;
    SelectField select_field = SelectField::None;
    int hit_option = 0;
  };

  void UpdateSearchSelection();
  void RebuildSearchHits();
  void PreviewSearchHit(int index);
  void ActivateSearchHit(int index);
  void MoveSearchHover(int delta);
  void CloseSearchResults();
  void UpdateSearchResultsLayout();
  void RenderSearchResults(ID2D1HwndRenderTarget* target);
  int SearchHitAtPoint(POINT point) const;
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
  void CloseWindowDiscardingChanges();
  bool ConfirmDiscardChanges();
  void RenderConfirmDiscardOverlay(ID2D1HwndRenderTarget* target);

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

  std::vector<SearchHit> search_hits_;
  int search_hover_index_ = -1;
  int search_window_offset_ = 0;
  RECT search_results_rect_{};
  std::vector<RECT> search_hit_rects_;

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

  // 跳转定位：下一帧 RenderContent 拿到新节布局后再滚动，避免对未构建的布局取坐标。
  std::wstring pending_scroll_title_;
  Section pending_scroll_section_ = Section::General;

  // A2 脏状态：每帧在 RenderFrame 刷新；导航脏点与保存按钮态据此绘制。
  unsigned dirty_mask_ = 0;
  // A3 未保存修改确认浮层（替代系统 MessageBox）。
  bool confirm_discard_ = false;
  RECT confirm_card_rect_{};
  RECT confirm_keep_rect_{};
  RECT confirm_discard_rect_{};

  // B3 粘性节标题队列（面板阶段入队，FlushStickyTitles 统一绘制）。
  std::vector<std::pair<const FlexNode*, const wchar_t*>> sticky_titles_;
  // B4 键盘焦点：focusable_regions_ 每帧按渲染顺序重建（Tab 序）。
  int keyboard_focus_ = -1;
  std::vector<HitRegion> focusable_regions_;
  bool window_allows_activation_ = false;
  bool interaction_guard_active_ = false;
  
  std::array<std::unique_ptr<ISettingsPanel>, 7> panels_;
};

}  // namespace clipd2d::ui
