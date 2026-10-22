#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <d2d1.h>
#include <windows.h>
#include <wrl/client.h>

#include "common/Status.h"
#include "core/actions/ActionDescriptor.h"
#include "core/browser/BrowserCommand.h"
#include "core/browser/BrowserState.h"
#include "core/browser/ClipBrowserService.h"
#include "core/commands/AppController.h"
#include "core/commands/RefreshRequest.h"
#include "core/store/ClipTypes.h"
#include "platform/win32/HotkeySpec.h"
#include "ui/D2DWindowlessTextBox.h"
#include "ui/D2dShell.h"
#include "ui/AutoHideInteractionState.h"
#include "ui/ClipVisualAssets.h"
#include "ui/DialogPanelWindow.h"
#include "ui/HoverHintWindow.h"
#include "ui/HistoryAssetLoader.h"
#include "ui/ListScrollController.h"
#include "ui/MainWindowTypes.h"
#include "ui/HistoryDataController.h"
#include "ui/PanelFocusCoordinator.h"
#include "ui/PreviewWindow.h"
#include "common/ThreadPool.h"
#include "ui/TextLayoutCache.h"
#include "ui/UiStrings.h"
#include "ui/vlist/VirtualListLayout.h"
#include "ui/vlist/VirtualListViewport.h"

namespace clipd2d::ui {

struct PopupMenuItemData;

class MainWindow : public ListScrollHost, public vlist::VirtualListSource {
protected:
  // ListScrollHost
  int GetItemCount() const override;
  int GetItemHeight() const override;
  int GetTotalContentHeight() const override;
  int GetViewportHeight() const override;
  void RequestRender() override;
  void EnsureAnimationTimer() override;

  // vlist::VirtualListSource (height measure is O(1), metadata-only;
  // unloaded rows return 0 and keep the default estimate)
  int ItemCount() const override;
  std::int64_t StableIdAt(int index) const override;
  int MeasureHeightAt(int index) const override;

public:
  using OpenSettingsCallback = std::function<void()>;
  using OwnedWindowPredicate = std::function<bool(HWND)>;

  MainWindow();
  ~MainWindow();

  static const wchar_t* WindowClassName();
  static UINT ShowWindowMessageId();

  common::Status Create(
      HINSTANCE instance,
      int n_cmd_show,
      bool show_initial_panel_after_layout,
      core::commands::AppController* controller,
      OpenSettingsCallback open_settings_callback,
      OwnedWindowPredicate owned_window_predicate);
  void Destroy();
  void RequestShowFromAnyThread() const;
  void RequestToggleFromAnyThread() const;
  void RequestRefreshFromAnyThread(core::commands::RefreshRequest request) const;
  void RequestRefreshPreserveViewport() const;
  void RequestRefreshForNewestClip(std::int64_t clip_id) const;
  void NotifySettingsWindowShown();
  void NotifySettingsWindowHidden();
  // C3 外观即时预览：设置窗未保存的毛玻璃改动实时套用/还原。
  void SetBlurPreview(core::store::WindowBlurType type, int opacity);
  void ClearBlurPreview();
  void NotifyPreviewWindowShown();
  void NotifyPreviewWindowHidden();
  void CaptureExternalFocusRestoreContext();
  void RestoreExternalFocusRestoreContext();
  void UpdateLastExternalFocusContextFromWindow(HWND window);

  HWND hwnd() const { return hwnd_; }

 private:
  friend class HistoryDataController;
  friend class HistoryAssetLoader;

  // C3 外观即时预览态（设置窗打开期间未保存的毛玻璃值）。
  bool blur_preview_active_ = false;
  core::store::WindowBlurType preview_blur_type_ = core::store::WindowBlurType::Mica;
  int preview_blur_opacity_ = 50;

  enum class HitKind {
    None,
    SearchClear,
    ToolbarButton,
    Tab,
    NavButton,
    ClearFilters,
    DialogConfirm,
    DialogCancel,
    DialogColorChip,
    Item,
    ItemSticker,
    ItemFavorite,
    ItemPinned,
    ItemDelete,
    TagChip,
    FilterChip,
    BottomButton,
    BackToTop,
  };

  enum class PendingPasteKind {
    Default,
    PlainText,
    Action,
  };

  enum class ReorderScope {
    None,
    Queue,
    Favorites,
    Tag,
    Pinned,
  };

  enum class PanelMenuKind {
    None,
    More,
    Filter,
    Group,
    ItemContext,
  };

  struct HitRegion {
    RECT rect{};
    HitKind kind = HitKind::None;
    int index = 0;
    std::int64_t clip_id = 0;
    std::int64_t tag_id = 0;
    std::string action_id;
  };

  struct PendingPasteRequest {
    bool active = false;
    PendingPasteKind kind = PendingPasteKind::Default;
    std::int64_t clip_id = 0;
    std::string action_id;
    core::clipboard::PasteTargetContext target_context{};
    core::clipboard::PasteRoute route = core::clipboard::PasteRoute::Fast;
  };

  enum class OverlayDialogKind {
    None,
    GroupName,
    TagEditor,
    ConfirmDeleteGroup,
    ConfirmDeleteTag,
  };

  enum class OverlayDialogAction {
    None,
    CreateGroup,
    RenameGroup,
    CreateTag,
    RenameTag,
    DeleteGroup,
    DeleteTag,
  };

  struct OverlayDialogColorOption {
    std::wstring label;
    std::wstring value;
  };

  struct OverlayDialogState {
    OverlayDialogKind kind = OverlayDialogKind::None;
    OverlayDialogAction action = OverlayDialogAction::None;
    bool visible = false;
    bool dangerous_confirm = false;
    bool has_text_input = false;
    bool has_color_options = false;
    bool restore_external_focus = false;
    bool select_all_on_open = false;
    std::int64_t target_id = 0;
    std::int64_t related_clip_id = 0;
    bool apply_filter_after_submit = false;
    std::wstring title;
    std::wstring prompt;
    std::wstring message;
    std::wstring confirm_text;
    std::wstring cancel_text;
    std::wstring input_text;
    std::wstring error_text;
    std::wstring selected_color;
    std::vector<OverlayDialogColorOption> color_options;
    RECT card_rect{};
    RECT message_rect{};
    RECT input_rect{};
    RECT error_rect{};
    RECT confirm_rect{};
    RECT cancel_rect{};
    std::vector<RECT> color_rects;
  };

  struct ExternalPointerMessagePayload {
    POINT screen_point{};
    detail::ExternalPointerDisposition disposition = detail::ExternalPointerDisposition::Ignore;
    detail::InteractionGuardKind deferred_guard = detail::InteractionGuardKind::PopupMenu;
  };

  struct BitmapEntry {
    std::uint64_t generation = 0;
    UINT width = 0;
    UINT height = 0;
    std::uint64_t last_used = 0;
    Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
  };

  struct StagingSnapshot {
    std::uint64_t generation = 0;
    core::store::ClipQuery query;
    core::commands::RefreshRequest request{};
    bool active = false;
    bool count_ready = false;
    int item_count = 0;
    int target_scroll_offset = 0;
    std::unordered_map<int, std::vector<core::store::ClipSummary>> page_cache;
    std::unordered_set<int> required_pages;
  };

  static constexpr UINT kShowWindowMessage = WM_APP + 100;
  static constexpr UINT kRefreshWindowMessage = WM_APP + 101;
  static constexpr UINT kCountReadyMessage = WM_APP + 102;
  static constexpr UINT kPageReadyMessage = WM_APP + 103;
  static constexpr UINT kClipReadyMessage = WM_APP + 104;
  static constexpr UINT kIconReadyMessage = WM_APP + 105;
  static constexpr UINT kPreviewReadyMessage = WM_APP + 106;
  static constexpr UINT kThumbnailReadyMessage = WM_APP + 107;
  static constexpr UINT kSearchKeyMessage = WM_APP + 108;
  static constexpr UINT kInvokeDefaultPasteMessage = WM_APP + 109;
  static constexpr UINT kPresentListMessage = WM_APP + 110;
  static constexpr UINT kToggleWindowMessage = WM_APP + 111;
  static constexpr UINT kFocusSearchMessage = WM_APP + 112;
  static constexpr UINT kHandleExternalPointerMessage = WM_APP + 113;
  static constexpr UINT kCommitPasteMessage = WM_APP + 114;
  static constexpr UINT kUpdatePasteTargetMessage = WM_APP + 115;
  static constexpr UINT kStartupMetadataReadyMessage = WM_APP + 116;
  static constexpr UINT kWarmSecondaryD2dMessage = WM_APP + 117;
  static constexpr UINT kInstallAutoHideHooksMessage = WM_APP + 118;
  static constexpr UINT kApplyWindowBlurMessage = WM_APP + 119;
  // L5:帧泵——WM_PAINT 只置脏,渲染由该消息在主循环里完成(合并重复帧)。
  static constexpr UINT kFrameRenderMessage = WM_APP + 120;
  // Q1:owned-message payload 的消息号区间(销毁前按此区间排空回收)。
  static constexpr UINT kOwnedMessageFirst = kShowWindowMessage;
  static constexpr UINT kOwnedMessageLast = kFrameRenderMessage;
  static constexpr UINT_PTR kAnimationTimerId = 1;
  static constexpr UINT_PTR kHiddenCacheTrimTimerId = 2;
  static constexpr UINT_PTR kHiddenWorkingSetTrimTimerId = 3;
  static constexpr UINT_PTR kHiddenWarmupTimerId = 4;
  static constexpr UINT_PTR kDragDecayTimerId = 5;
  static constexpr UINT_PTR kDeferredPageRequestTimerId = 6;
  static constexpr UINT_PTR kScrollBenchTimerId = 7;
  static constexpr UINT kHiddenCacheTrimDelayMs = 30000;
  static constexpr UINT kHiddenWorkingSetTrimDelayMs = 200000;
  static constexpr UINT kHiddenWarmupDelayMs = 2000;
  static constexpr std::size_t kHiddenWorkingSetTrimThresholdBytes = 80u * 1024u * 1024u;
  static constexpr int kIdSearchEdit = 1001;
  static constexpr int kIdSearchClearButton = 1002;
  static constexpr int kIdDialogEdit = 1003;
  static constexpr UINT_PTR kToolbarTooltipBase = 2000;
  static constexpr UINT_PTR kTabTooltipBase = 2100;
  static constexpr UINT_PTR kDynamicTooltipId = 2200;

  static LRESULT CALLBACK StaticWindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param);
  static void CALLBACK ForegroundChangedProc(
      HWINEVENTHOOK hook,
      DWORD event,
      HWND hwnd,
      LONG object_id,
      LONG child_id,
      DWORD event_thread,
      DWORD event_time);
  static LRESULT CALLBACK LowLevelMouseProc(int code, WPARAM w_param, LPARAM l_param);
  static LRESULT CALLBACK LowLevelKeyboardProc(int code, WPARAM w_param, LPARAM l_param);

  LRESULT WindowProc(UINT message, WPARAM w_param, LPARAM l_param);
  common::Status CreateControls();
  void ApplyThemeToTextboxes();
  void LayoutControls();
  void UpdateRoundedRegion();
  common::Status EnsureHoverHintWindowCreated();
  common::Status EnsureDialogPanelWindowCreated();
  void PrepareWindowPlacementForShow();
  void ShowAndActivate(bool refresh_data = true);
  void PresentPreparedWindow(bool focus_search_on_open, bool suppress_list_cache = false);
  void HideWindow();
  void ToggleWindow();
  void FocusSearchBox(bool select_all);
  void ClearSearch();
  void HandleSystemThemeChange();
  void SetPanelAllowsActivation(bool allow_activation);
  std::optional<core::clipboard::PasteTargetContext> BuildExternalFocusRestoreContext() const;
  bool RestoreExternalFocusContext(const core::clipboard::PasteTargetContext& context);
  bool IsSearchFocusHotkey(UINT virtual_key, UINT modifiers) const;
  bool IsOwnedWindow(HWND hwnd) const;
  void UpdateAutoHideHooks();
  void UpdateHoverTooltip(POINT point);
  void HideHoverTooltip();
  void StartAutoHideHooks();
  void StopAutoHideHooks();
  void StartDrawerAnimation(bool open);
  void StepAnimation();
  // L6:统一"是否需要下一帧"判断(硬规则 3)。覆盖:内容脏行/整帧脏、滚动物理与
  // 拖拽衰减、动画与骨架 shimmer、预览/缩略图加载中、延迟页请求挂起、拖拽 blur
  // 衰减。新增脏类型必须同步此清单——漏一项会导致"数据到了但列表不刷新"。
  bool has_pending_work() const;
  // L5:置脏 + 合并调度帧泵(重复请求合并为一帧,不漏帧不重复帧)。
  void MarkFrameDirty();
  void DrainPendingOwnedMessages();
  void EnsureBitmapCachesValid();
  void SaveWindowPlacementIfNeeded(bool include_position);
  POINT ComputeFollowMouseOrigin(int width, int height) const;
  RECT ScrollbarHitRect(const RECT& track_rect, bool expanded) const;

  void RefreshGroups();
  void RefreshTags();
  void RefreshData(bool preserve_selection, RefreshDataMode mode = RefreshDataMode::Full);
  void RefreshData(const core::commands::RefreshRequest& request, RefreshDataMode mode = RefreshDataMode::Full);
  void UpdateScrollBar();
  void EnsureSelectionVisible();
  bool IsColdStartPresentationPending() const;
  std::unordered_set<int> BuildColdStartPresentationPages(
      int item_count,
      int scroll_target_offset,
      int viewport_height) const;
  void RequestVisiblePages(bool include_thumbnails = true);
  void RequestVisibleThumbnails();
  void PrepareVisibleContentForPresentation();
  void WarmNeighborhoodPages();
  void SelectFirstItemIfNeeded();
  void CommitStagingSnapshotIfReady();
  void ScheduleListPresent();
  void InvalidateListRegion();
  void TouchPage(int page_index);
  void TrimPageCache();
  void TouchBitmap(BitmapEntry* entry);
  void TrimBitmapCaches();
  void ScheduleDeferredHiddenCacheTrim();
  void CancelDeferredHiddenCacheTrim();
  void RunDeferredHiddenCacheTrim();
  void ScheduleDeferredHiddenWorkingSetTrim();
  void CancelDeferredHiddenWorkingSetTrim();
  void RunDeferredHiddenWorkingSetTrim();
  void ScheduleDeferredHiddenWarmup();
  void CancelDeferredHiddenWarmup();
  void RunDeferredHiddenWarmup();
  std::unordered_set<int> BuildProtectedHiddenCachePages() const;
  std::unordered_set<std::wstring> BuildProtectedHiddenThumbnailKeys() const;
  void MarkAllVisibleRowsDirty();
  void MarkRowDirty(int index);
  void MarkRangeDirty(int start_index, int end_index);
  void MarkDirtyRowsForImageKey(const std::wstring& key);
  void MarkDirtyRowsForShellIcon(
      visual_assets::ShellIconPurpose purpose,
      const std::wstring& source_path);
  int RowIndexAtPoint(POINT point) const;
  void RenderDirtyRows(
      ID2D1RenderTarget* target,
      const RECT& client,
      const RECT& list_rect,
      POINT hover,
      const core::store::AppSettings& render_settings);
  void RenderListRow(
      ID2D1RenderTarget* target,
      const RECT& client,
      const RECT& list_rect,
      const RECT& row,
      int index,
      POINT hover,
      const core::store::AppSettings& render_settings,
      bool collect_hits);

  void RequestCountAsync(core::store::ClipQuery query, std::uint64_t generation);
  void RequestPageAsync(int page_index);
  void RequestPageAsync(int page_index, std::uint64_t generation, const core::store::ClipQuery& query, bool urgent = false);
  void RequestSelectedClipAsync(std::int64_t clip_id);
  void RequestStartupMetadataAsync();
  void RequestSourceIconAsync(const core::store::ClipSummary& summary);
  void RequestFileItemIconAsync(const core::store::ClipSummary& summary);
  void RequestThumbnailAsync(const core::store::ClipSummary& summary);
  void RequestPreviewAsync(const core::store::ClipRecord& clip);
  int GetFileThumbnailTargetExtent() const;
  visual_assets::ShellIconRequest BuildSourceIconRequest(
      const core::store::ClipSummary& summary) const;
  std::optional<visual_assets::ShellIconRequest> BuildFileItemIconRequest(
      const core::store::ClipSummary& summary) const;

  void HandleCountReady(WPARAM w_param, LPARAM l_param);
  void HandlePageReady(WPARAM w_param, LPARAM l_param);
  void HandleClipReady(WPARAM w_param, LPARAM l_param);
  void HandleStartupMetadataReady(WPARAM w_param, LPARAM l_param);
  void HandleIconReady(WPARAM w_param, LPARAM l_param);
  void HandlePreviewReady(WPARAM w_param, LPARAM l_param);
  void HandleThumbnailReady(WPARAM w_param, LPARAM l_param);

  void RenderFrame();
  void EnsureListRenderTarget(const RECT& list_rect);
  void RenderListSurface(
      ID2D1RenderTarget* target,
      const RECT& client,
      const RECT& list_rect,
      POINT hover,
      const core::store::AppSettings& render_settings,
      bool collect_hits);
  void RenderEmptyListGuidance(
      ID2D1RenderTarget* target,
      const RECT& list_rect,
      const core::store::AppSettings& render_settings);
  void RenderLiveResizeListSurface(
      ID2D1RenderTarget* target,
      const RECT& client,
      const RECT& list_rect,
      const core::store::AppSettings& render_settings);
  void RenderListShellIconOverlay(
      ID2D1RenderTarget* target,
      const RECT& list_rect,
      const core::store::AppSettings& render_settings);

  void OnMouseMove(POINT point);
  void OnLeftButtonDown(POINT point, bool double_click);
  void OnRightButtonUp(POINT point);
  void OnMouseWheel(short delta);
  void OnKeyboardNavigate(UINT virtual_key, bool shift_down);
  bool TryPerformRowActionAtPoint(POINT point);
  bool PerformHitAction(const HitRegion& hit);
  std::optional<HitRegion> ResolveStaticHit(POINT point) const;
  bool IsPressedStaticHit(HitKind kind, int index) const;
  bool ShouldShowBackToTopButton(const RECT& list_rect) const;
  const core::store::ClipSummary* GetBottomTimeSummary(const RECT& list_rect);
  void OpenGroupMenu();
  void OpenMoreMenu();
  void OpenFilterMenu();
  void OpenSelectedContextMenu(POINT screen_point);
  int TrackPopupMenuForPanel(PanelMenuKind kind, const std::vector<struct PopupMenuItemData>& items, POINT screen_point);
  bool IsMenuPopupWindow(HWND hwnd) const;
  void DismissOwnedPopupMenus();
  bool IsPanelMenuTogglePoint(POINT screen_point, PanelMenuKind* kind = nullptr) const;
  bool IsOverlayDialogVisible() const;
  void UpdateOverlayDialogLayout();
  void OpenCreateGroupDialog();
  void OpenRenameGroupDialog(std::int64_t group_id);
  void OpenCreateTagDialog(std::int64_t clip_id, bool apply_filter_after_create);
  void OpenRenameTagDialog(std::int64_t tag_id);
  void OpenDeleteGroupDialog(std::int64_t group_id);
  void OpenDeleteTagDialog(std::int64_t tag_id);
  void OpenOverlayDialog(OverlayDialogState state);
  void CloseOverlayDialog(bool restore_focus);
  void CommitOverlayDialog();
  bool HandleOverlayDialogHit(const HitRegion& hit);
  void FocusOverlayDialogInput(bool select_all);
  void SyncOverlayDialogInputText();
  void ClearOverlayDialogError();
  const core::store::GroupRecord* FindGroupById(std::int64_t group_id) const;
  std::optional<std::int64_t> PromptCreateGroup();
  bool SelectGroupById(std::int64_t group_id);
  bool SelectGroupByIndex(size_t index);
  bool RenameGroupById(std::int64_t group_id);
  bool DeleteGroupById(std::int64_t group_id);
  bool CreateGroupAndSelect();
  bool MoveClipToGroupById(std::int64_t clip_id, std::int64_t group_id, const std::wstring& success_status);
  bool MoveClipToGroupByIndex(std::int64_t clip_id, size_t group_index);
  bool CreateGroupAndMoveClip(std::int64_t clip_id);
  bool IsInternalInteractionPoint(POINT screen_point) const;
  ExternalPointerMessagePayload BuildExternalPointerMessagePayload(POINT screen_point) const;
  void HandleExternalPointerDown(const ExternalPointerMessagePayload& payload);
  void SetInteractionGuard(detail::InteractionGuardKind kind, bool active);
  void FlushDeferredExternalHideIfNeeded();
  void EnsureSelectedClipLoaded();
  bool PointInInteractiveRegion(POINT point) const;
  LRESULT HitTestNonClient(POINT screen_point) const;

  core::store::ClipQuery BuildCurrentQuery() const;
  core::browser::BrowserState BuildBrowserState() const;
  void ApplyBrowserCommand(const core::browser::BrowserCommand& command);
  int GetCollapsedWidth() const;
  int GetExpandedWidth() const;
  int GetDrawerWidth() const;
  const core::store::ClipSummary* GetSummaryAt(int index);
  const core::store::ClipSummary* GetSelectedSummary() const;
  void SelectIndex(int index, bool open_drawer);
  void LoadSelection();
  void ToggleFavoriteSelected();
  void TogglePinnedSelected();
  void DeleteSelected();
  void OpenSelectedFolder();
  void CopySelectedPath();
  void OpenSelectedPreview();
  common::Status EnsurePreviewWindowCreated();
  PreviewWindow::HostActions BuildPreviewHostActions();
  void OpenSelectedWithDefaultApp();
  void OpenSelectedResolvedTarget();
  void PerformThirdPartySticker();
  void PasteNextQueueItem();
  bool PromptCreateTag(std::int64_t clip_id, bool apply_filter_after_create);
  void RenameCurrentTag();
  void DeleteCurrentTag();
  ReorderScope CurrentReorderScope() const;
  bool CanReorderRow(const core::store::ClipSummary& summary) const;
  void ArmRowDrag(POINT point, int index);
  bool MaybeStartRowDrag(POINT point);
  void UpdateRowDrag(POINT point);
  void FinishRowDrag(POINT point, bool cancel);
  void BeginRowDragSession();
  void ClearRowDragSession();
  void RebuildRowDragSessionSlots();
  float RowDragSessionSlotTop(int slot, const RECT& list_rect, float visual_scroll_offset) const;
  int ComputeDesiredRowDragSlot(const RECT& list_rect, float visual_scroll_offset) const;
  bool StartNextRowDragGapStep(const RECT& list_rect, float visual_scroll_offset);
  bool RowDragSessionHasActiveMotion() const;
  void ClearRowReorderAnimation();
  void UpdateRowReorderAnimationTargets();
  float RowReorderDisplayTop(int index, float default_top, const RECT& list_rect, float visual_scroll_offset) const;
  bool TryStartExternalDrag(int index);
  common::Status BuildDragFileList(const core::store::ClipSummary& summary, std::vector<std::wstring>* paths);
  void QueuePasteRequest(PendingPasteKind kind, std::int64_t clip_id, std::string action_id = {});
  void CommitPendingPasteRequest();
  void PerformDefaultPaste();
  void PerformPlainTextPaste();
  void PerformActionById(const std::string& action_id);
  core::clipboard::PasteTargetContext CapturePasteTargetContext() const;
  void RefreshStatus(const std::wstring& text);
  void RefreshSettingsCache();
  bool ShouldShowThirdPartyStickerButton(const core::store::ClipSummary& summary) const;
  bool ShouldShowTimeGroupsInCurrentState() const;

  HINSTANCE instance_ = nullptr;
  HWND hwnd_ = nullptr;
  HWND dummy_owner_ = nullptr;
  D2DWindowlessTextBox search_textbox_;
  D2DWindowlessTextBox overlay_textbox_;
  HWND tooltip_ = nullptr;
  D2dShell d2d_shell_;
  DialogPanelWindow dialog_panel_window_;
  HoverHintWindow hover_hint_window_;
  PreviewWindow preview_window_;
  HistoryDataController history_data_controller_;
  HistoryAssetLoader history_asset_loader_;

  core::commands::AppController* controller_ = nullptr;
  OpenSettingsCallback open_settings_callback_;
  OwnedWindowPredicate owned_window_predicate_;
  core::browser::ClipBrowserService browser_service_;
  core::browser::BrowserState browser_state_;

  core::store::ClipView current_view_ = core::store::ClipView::All;
  std::int64_t current_group_id_ = 0;
  std::wstring current_group_name_;
  std::int64_t current_tag_id_ = 0;
  std::wstring current_tag_name_;
  core::store::DateRangeFilter current_date_range_ = core::store::DateRangeFilter::All;
  int queue_count_ = 0;
  std::wstring status_text_ = L"准备就绪";
  std::optional<std::int64_t> pending_selected_clip_id_;
  std::optional<std::int64_t> explicit_selected_clip_id_;
  std::optional<core::commands::RefreshRequest> pending_refresh_request_;

  core::store::ClipQuery display_query_;
  std::vector<std::wstring> display_query_tokens_;
  std::vector<core::store::GroupRecord> groups_;
  std::vector<core::store::TagRecord> tags_;
  std::vector<core::actions::ActionDescriptor> actions_;
  std::optional<core::store::ClipRecord> selected_clip_;
  std::unordered_map<int, std::vector<core::store::ClipSummary>> page_cache_;
  std::unordered_set<std::uint64_t> pending_page_request_keys_;
  std::unordered_map<std::wstring, std::uint64_t> pending_thumbnail_generations_;
  std::unordered_map<std::wstring, BitmapEntry> thumbnail_bitmaps_;
  BitmapEntry preview_bitmap_;
  std::unordered_map<int, std::uint64_t> page_access_ticks_;
  std::uint64_t page_access_tick_ = 0;
  std::uint64_t bitmap_access_tick_ = 0;
  std::unordered_set<int> dirty_rows_;
  std::vector<HitRegion> list_hit_regions_;
  TextLayoutCache text_layout_cache_;
  common::ThreadPool thread_pool_{4};

  std::vector<HitRegion> hit_regions_;
  StagingSnapshot staging_snapshot_;
  POINT hover_point_{-1, -1};
  struct StaticPressState {
    HitRegion hit{};
    bool inside = false;
  };
  std::optional<StaticPressState> pressed_static_hit_;
  bool mouse_tracking_ = false;
  bool search_clear_hovered_ = false;
  bool search_clear_pressed_ = false;
  bool show_html_source_ = false;
  bool drag_scrollbar_ = false;
  bool row_drag_active_ = false;
  bool row_drag_candidate_active_ = false;
  bool panel_locked_ = false;
  bool search_focus_requested_ = false;
  bool animation_timer_active_ = false;
  std::int64_t last_render_tick_qpc_ = 0;
  float last_rendered_blur_strength_ = 0.0f;
  bool auto_hide_hooks_active_ = false;
  bool list_present_scheduled_ = false;
  bool skeleton_animating_ = false;
  // L5 帧泵:frame_dirty_ 有待呈现的内容;frame_render_scheduled_ 合并重复调度。
  bool frame_dirty_ = false;
  bool frame_render_scheduled_ = false;
  // L6 has_pending_work 清单:延迟页请求挂起 / 预览图加载中。
  bool deferred_page_request_pending_ = false;
  bool preview_load_pending_ = false;
  bool startup_metadata_warmup_requested_ = false;
  bool startup_metadata_warmup_completed_ = false;
  bool in_live_resize_ = false;
  bool search_edit_had_focus_ = false;
  bool dialog_edit_had_focus_ = false;
  std::wstring resize_search_text_;
  OverlayDialogState overlay_dialog_{};

  int item_count_ = 0;
  int page_size_ = 128;
  int selected_index_ = -1;

  // Variable-height virtualized list (see docs/plans/virtual-list-port.md).
  vlist::VirtualListViewport viewport_;
  int viewport_content_width_ = 400;
  void RebuildViewportHeights();
  void SyncViewportHeightsForPage(int page_index);
  void ApplyViewportScrollAnchorDelta(int delta);
  // --bench-scroll harness (MainWindow.bench.cpp): scripted scroll/thumb-drag
  // sweep that measures frame cost, skeleton exposure and mapping accuracy.
  void MaybeArmScrollBench();
  void RunScrollBenchmark();
  std::uint64_t last_animation_tick_ms_ = 0;
  std::uint64_t last_page_request_time_ms_ = 0;
  LARGE_INTEGER performance_counter_frequency_{};
  std::int64_t last_animation_tick_qpc_ = 0;
  UINT frame_interval_ms_ = 8;
  int window_width_ = 460;
  int window_height_ = 720;
  int window_x_ = CW_USEDEFAULT;
  int window_y_ = CW_USEDEFAULT;
  bool has_window_position_ = false;
  PanelFocusCoordinator focus_coordinator_;
  bool paste_route_requires_recovery_ = false;
  PendingPasteRequest pending_paste_request_{};
  platform::win32::HotkeySpec search_focus_hotkey_{};
  platform::win32::HotkeySpec third_party_sticker_hotkey_{};
  bool third_party_sticker_enabled_ = false;
  bool third_party_sticker_hotkey_valid_ = false;
  HWINEVENTHOOK foreground_hook_ = nullptr;
  HHOOK mouse_hook_ = nullptr;
  HHOOK keyboard_hook_ = nullptr;
  bool panel_allows_activation_ = false;
  std::atomic<std::uint64_t> refresh_generation_{0};
  std::atomic<std::uint64_t> clip_generation_{0};
  std::uint64_t preview_generation_ = 0;
  std::uint64_t thumbnail_request_epoch_ = 0;
  std::uint64_t bitmap_generation_ = 0;
  std::atomic<std::uint64_t> display_generation_{0};
  std::atomic<std::uint64_t> active_thumbnail_request_epoch_{0};
  int last_thumbnail_request_first_index_ = -1;
  int last_thumbnail_request_last_index_ = -1;
  detail::InteractionGuardState interaction_guard_state_{};
  PanelMenuKind active_menu_kind_ = PanelMenuKind::None;
  int last_hovered_list_index_ = -1;

  int armed_click_item_index_ = -1;
  POINT row_drag_start_point_{};
  POINT row_drag_point_{-1, -1};
  int row_drag_pointer_offset_y_ = 0;
  ReorderScope row_drag_scope_ = ReorderScope::None;
  int row_drag_candidate_index_ = -1;
  int row_drag_item_index_ = -1;
  int row_drop_index_ = -1;
  bool row_drop_after_ = false;
  struct RowDragSession {
    std::vector<int> ordered_indexes;
    std::vector<int> original_ordered_indexes;
    std::unordered_map<int, int> display_slot_by_index;
    std::vector<float> slot_tops;
    int dragged_index = -1;
    int original_gap_slot = -1;
    int gap_slot = -1;
    int desired_slot = -1;
  };
  struct RowDragGapStep {
    int moving_index = -1;
    int from_slot = -1;
    int to_slot = -1;
    float progress = 0.0f;
    float duration_ms = 0.0f;
    float elapsed_ms = 0.0f;
  };
  struct RowDragVisualState {
    float current_top = 0.0f;
    float target_top = 0.0f;
    float velocity = 0.0f;
  };
  std::unique_ptr<ListScrollController> scroll_controller_;
  std::optional<RowDragSession> row_drag_session_;
  std::optional<RowDragGapStep> row_drag_gap_step_;
  std::unordered_map<int, RowDragVisualState> row_drag_visual_rows_;
  bool row_reorder_animation_active_ = false;
  float row_drag_auto_scroll_velocity_ = 0.0f;
  float row_drag_velocity_rows_per_s_ = 0.0f;
  std::uint64_t row_drag_last_update_tick_ms_ = 0;
  std::int64_t row_drag_last_update_qpc_ = 0;
  float row_drag_visual_scroll_offset_ = 0.0f;


  bool pending_initial_show_ = false;
  bool pending_show_after_refresh_ = false;
  bool suppress_list_cache_during_present_ = false;
};

}  // namespace clipd2d::ui
