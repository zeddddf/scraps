#include "ui/MainWindowInternal.h"
#include "core/commands/ISettingsStore.h"
#include "core/commands/IHistoryFeed.h"

// 输入面：鼠标/键盘事件、静态命中解析、行内动作命中。
namespace clipd2d::ui {

std::optional<MainWindow::HitRegion> MainWindow::ResolveStaticHit(POINT point) const {
  if (hwnd_ == nullptr) {
    return std::nullopt;
  }

  RECT client{};
  GetClientRect(hwnd_, &client);
  const PanelLayout layout = ComputeLayout(client.right, client.bottom);
  const bool has_search_text = !search_textbox_.GetText().empty();
  if (has_search_text && RectContains(SearchClearButtonRect(layout), point)) {
    return HitRegion{SearchClearButtonRect(layout), HitKind::SearchClear, 0};
  }
  for (int index = 0; index < static_cast<int>(std::size(layout.toolbar)); ++index) {
    if (RectContains(layout.toolbar[index], point)) {
      return HitRegion{layout.toolbar[index], HitKind::ToolbarButton, index};
    }
  }
  for (int index = 0; index < static_cast<int>(std::size(kVisibleTabs)); ++index) {
    if (RectContains(layout.tabs[index], point)) {
      return HitRegion{layout.tabs[index], HitKind::Tab, index};
    }
  }
  for (int index = 0; index < static_cast<int>(std::size(layout.nav_actions)); ++index) {
    if (RectContains(layout.nav_actions[index], point)) {
      return HitRegion{layout.nav_actions[index], HitKind::NavButton, index};
    }
  }
  if (ShouldShowBackToTopButton(layout.list) && RectContains(layout.back_to_top, point)) {
    return HitRegion{layout.back_to_top, HitKind::BackToTop, 0};
  }
  return std::nullopt;
}

bool MainWindow::IsPressedStaticHit(HitKind kind, int index) const {
  return pressed_static_hit_.has_value() &&
      pressed_static_hit_->inside &&
      pressed_static_hit_->hit.kind == kind &&
      pressed_static_hit_->hit.index == index;
}

bool MainWindow::ShouldShowBackToTopButton(const RECT& list_rect) const {
  const int viewport = std::max(1L, list_rect.bottom - list_rect.top);
  if (scroll_controller_->CurrentScrollOffset() <= viewport) {
    return false;
  }
  const RECT reveal_rect{
      list_rect.left,
      list_rect.top,
      list_rect.right,
      std::min(list_rect.bottom, list_rect.top + kBackToTopRevealHeight)};
  return scroll_controller_->GetManualScrollDirection() < 0 || RectContains(reveal_rect, hover_point_);
}

const core::store::ClipSummary* MainWindow::GetBottomTimeSummary(const RECT& list_rect) {
  if (!ShouldShowTimeGroupsInCurrentState() || state_.item_count_ <= 0) {
    return nullptr;
  }

  const int viewport = std::max(1L, list_rect.bottom - list_rect.top);
  const float visual_scroll_offset = scroll_controller_->VisualScrollOffset();
  const int first_visible_index = std::clamp(
      viewport_.IndexAtOffset(static_cast<int>(visual_scroll_offset)),
      0,
      std::max(0, state_.item_count_ - 1));
  const int last_visible_index = std::clamp(
      viewport_.IndexAtOffset(
          static_cast<int>(visual_scroll_offset) + std::max(1, viewport) - 1),
      0,
      std::max(0, state_.item_count_ - 1));
  if (const auto* summary = GetSummaryAt(last_visible_index)) {
    return summary;
  }
  if (last_visible_index != first_visible_index) {
    return GetSummaryAt(first_visible_index);
  }
  return nullptr;
}

void MainWindow::EnsureAnimationTimer() {
  // L6:统一经 has_pending_work() 判断是否需要下一帧(硬规则 3),替代原先
  // 各动画启动点各自为战的条件(滚动/拖拽/骨架 shimmer/加载在途)。
  if (hwnd_ == nullptr || !has_pending_work()) {
    return;
  }
  if (animation_timer_active_) {
    return;
  }
  UINT interval = frame_interval_ms_;
  if (interval == 0) {
    interval = 8;
  }
  if (row_drag_active_ || row_reorder_animation_active_) {
    interval = std::min<UINT>(interval, 3);
  } else if (scroll_controller_->IsListScrollAnimating()) {
    interval = std::min<UINT>(interval, 5);
  }
  if (SetTimer(hwnd_, kAnimationTimerId, interval, nullptr)) {
    animation_timer_active_ = true;
  }
}

void MainWindow::OnMouseMove(POINT point) {
  // hover 内层计时：WM_MOUSEMOVE 曾被测到 47ms。外层慢消息探针无法区分
  // 「本函数耗时」与「系统抢占」，这里补内层数字以便判断是否需要改算法。
  // 计时用 QPC：GetTickCount64 会把 0.5ms 的移动记成 16ms（跨 tick 即进位）。
  struct MoveProbe {
    common::Stopwatch stopwatch;
    POINT point;
    ~MoveProbe() {
      const std::int64_t elapsed_us = stopwatch.ElapsedUs();
      if (elapsed_us >= 8000) {
        common::WriteLog(
            common::LogLevel::Warning,
            "ui.mousemove.slow",
            "elapsed_ms=" + common::FormatMs(elapsed_us) +
                " x=" + std::to_string(point.x) + " y=" + std::to_string(point.y));
      }
    }
  } move_probe{common::Stopwatch{}, point};
  if (IsOverlayDialogVisible()) {
    hover_point_ = point;
    CancelHoverPreview(false);
    if (!mouse_tracking_) {
      TRACKMOUSEEVENT track{};
      track.cbSize = sizeof(track);
      track.dwFlags = TME_LEAVE;
      track.hwndTrack = hwnd_;
      TrackMouseEvent(&track);
      mouse_tracking_ = true;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
    return;
  }
  if (pressed_static_hit_.has_value()) {
    bool inside = false;
    if (const auto static_hit = ResolveStaticHit(point); static_hit.has_value()) {
      inside = static_hit->kind == pressed_static_hit_->hit.kind &&
          static_hit->index == pressed_static_hit_->hit.index;
    }
    if (inside != pressed_static_hit_->inside) {
      pressed_static_hit_->inside = inside;
      InvalidateRect(hwnd_, nullptr, FALSE);
    }
  }
  if (row_drag_active_) {
    RECT client{};
    GetClientRect(hwnd_, &client);
    if (!RectContains(client, point) && TryStartExternalDrag(row_drag_item_index_)) {
      return;
    }
    hover_point_ = point;
    CancelHoverPreview(false);
    UpdateRowDrag(point);
    return;
  }
  if (MaybeStartRowDrag(point)) {
    CancelHoverPreview(false);
    return;
  }

  auto resolve_hover_token = [&](POINT probe) -> std::int64_t {
    if (const auto hit = ResolveStaticHit(probe)) {
      return (static_cast<std::int64_t>(static_cast<int>(hit->kind)) << 32) |
          static_cast<std::uint32_t>(hit->index + 1);
    }
    for (auto it = hit_regions_.rbegin(); it != hit_regions_.rend(); ++it) {
      const auto& hit = *it;
      if (RectContains(hit.rect, probe)) {
        return (static_cast<std::int64_t>(static_cast<int>(hit.kind)) << 32) |
            static_cast<std::uint32_t>(hit.index + 1);
      }
    }
    RECT client{};
    GetClientRect(hwnd_, &client);
    const PanelLayout layout = ComputeLayout(client.right, client.bottom);
    const int viewport = std::max(1, static_cast<int>(layout.list.bottom - layout.list.top));
    if (GetTotalContentHeight() > viewport &&
        RectContains(ScrollbarHitRect(layout.scrollbar_track, drag_scrollbar_ || RectContains(layout.list, probe)), probe)) {
      return -2;
    }
    return -1;
  };

  const std::int64_t previous_token = resolve_hover_token(hover_point_);
  const int previous_row = RowIndexAtPoint(hover_point_);
  hover_point_ = point;
  const int current_row = RowIndexAtPoint(point);
  if (!mouse_tracking_) {
    TRACKMOUSEEVENT track{};
    track.cbSize = sizeof(track);
    track.dwFlags = TME_LEAVE;
    track.hwndTrack = hwnd_;
    TrackMouseEvent(&track);
    mouse_tracking_ = true;
  }
  if (drag_scrollbar_) {
    RECT client{};
    GetClientRect(hwnd_, &client);
    const PanelLayout layout = ComputeLayout(client.right, client.bottom);
    const int viewport = std::max(1, static_cast<int>(layout.list.bottom - layout.list.top));
    const int content_height = GetTotalContentHeight();
    if (content_height > viewport) {
      const float track_top = static_cast<float>(layout.scrollbar_track.top);
      const float track_height = static_cast<float>(layout.scrollbar_track.bottom - layout.scrollbar_track.top);
      const float thumb_height = std::max(44.0f, track_height * (static_cast<float>(viewport) / static_cast<float>(content_height)));
      const float travel = std::max(1.0f, track_height - thumb_height);
      const float centered = static_cast<float>(point.y) - track_top - thumb_height * 0.5f;
      const float ratio = std::clamp(centered / travel, 0.0f, 1.0f);
      const int next_offset = static_cast<int>(std::round(ratio * std::max(0, content_height - viewport)));
      if (next_offset != scroll_controller_->GetTargetScrollOffset()) {
        scroll_controller_->GetManualScrollDirection() = next_offset < scroll_controller_->GetTargetScrollOffset() ? -1 : 1;
      }
      scroll_controller_->SetScrollOffsetTarget(next_offset, true);
      scroll_controller_->SetShowScrollbarUntilMs(NowEpochMs() + 1200);
      ::SetTimer(hwnd_, kDragDecayTimerId, 80, nullptr);
    }
    // Fetch the newly visible pages right away instead of waiting for a
    // skeleton frame to trigger the animation timer. Thumbnails stay off —
    // they would churn the thumbnail request epoch on every mouse move.
    RequestVisiblePages(false);
    InvalidateListRegion();
    HideHoverTooltip();
    CancelHoverPreview(false);
    return;
  }
  if (resolve_hover_token(point) != previous_token) {
    const bool previous_in_list = previous_row >= 0 || previous_token == -2;
    const bool current_in_list = current_row >= 0 || resolve_hover_token(point) == -2;
    if (previous_in_list || current_in_list) {
      if (previous_row >= 0) {
        MarkRowDirty(previous_row);
      }
      if (current_row >= 0) {
        MarkRowDirty(current_row);
      }
      last_hovered_list_index_ = current_row;
      InvalidateListRegion();
      UpdateHoverTooltip(point);
      UpdateHoverPreview(current_row);
      return;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
  }
  UpdateHoverTooltip(point);
  UpdateHoverPreview(current_row);
}

void MainWindow::OnLeftButtonDown(POINT point, bool double_click) {
  (void)double_click;
  if (IsOverlayDialogVisible()) {
    armed_click_item_index_ = -1;
    row_drag_candidate_active_ = false;
    pressed_static_hit_.reset();
    if (overlay_dialog_.has_text_input && RectContains(overlay_dialog_.input_rect, point)) {
      FocusOverlayDialogInput(false);
      return;
    }
    for (auto it = hit_regions_.rbegin(); it != hit_regions_.rend(); ++it) {
      if (RectContains(it->rect, point) &&
          (it->kind == HitKind::DialogConfirm || it->kind == HitKind::DialogCancel || it->kind == HitKind::DialogColorChip)) {
        PerformHitAction(*it);
        return;
      }
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
    return;
  }
  if (scroll_controller_->IsInertialScrollActive()) {
    scroll_controller_->StopInertialScroll(false);
    InvalidateListRegion();
  }
  armed_click_item_index_ = -1;
  row_drag_candidate_active_ = false;
  pressed_static_hit_.reset();
  RECT client{};
  GetClientRect(hwnd_, &client);
  const PanelLayout layout = ComputeLayout(client.right, client.bottom);
  if (const auto static_hit = ResolveStaticHit(point)) {
    pressed_static_hit_ = StaticPressState{*static_hit, true};
    SetCapture(hwnd_);
    InvalidateRect(hwnd_, nullptr, FALSE);
    return;
  }
  if (RectContains(layout.search_box, point)) {
    FocusSearchBox(false);
    return;
  }
  const int viewport = std::max(1, static_cast<int>(layout.list.bottom - layout.list.top));
  const int content_height = GetTotalContentHeight();
  if (content_height > viewport) {
    const float track_top = static_cast<float>(layout.scrollbar_track.top);
    const float track_height = static_cast<float>(layout.scrollbar_track.bottom - layout.scrollbar_track.top);
    const float thumb_height = std::max(44.0f, track_height * (static_cast<float>(viewport) / static_cast<float>(content_height)));
    const float travel = std::max(1.0f, track_height - thumb_height);
    const float ratio = content_height > viewport ? scroll_controller_->VisualScrollOffset() / static_cast<float>(content_height - viewport) : 0.0f;
    RECT thumb_rect{
        layout.scrollbar_track.left,
        static_cast<int>(std::round(track_top + travel * ratio)),
        layout.scrollbar_track.right,
        static_cast<int>(std::round(track_top + travel * ratio + thumb_height))};
    if ((scroll_controller_->ShouldShowScrollThumb() && RectContains(ScrollbarHitRect(thumb_rect, true), point)) ||
        RectContains(ScrollbarHitRect(layout.scrollbar_track, true), point)) {
      drag_scrollbar_ = true;
      SetCapture(hwnd_);
      OnMouseMove(point);
      return;
    }
  }
  if (TryPerformRowActionAtPoint(point)) {
    return;
  }
  for (auto it = hit_regions_.rbegin(); it != hit_regions_.rend(); ++it) {
    if (RectContains(it->rect, point)) {
      HitRegion hit = *it;
      if (double_click && hit.kind == HitKind::Item) {
        hit.index = -hit.index - 1;
      }
      if (PerformHitAction(hit)) {
        if (!double_click && hit.kind == HitKind::Item) {
          const int item_index = hit.index < 0 ? (-hit.index - 1) : hit.index;
          ArmRowDrag(point, item_index);
        }
        return;
      }
    }
  }
  UpdateHoverTooltip(point);
}

void MainWindow::OnRightButtonUp(POINT point) {
  if (IsOverlayDialogVisible()) {
    return;
  }
  for (auto it = hit_regions_.rbegin(); it != hit_regions_.rend(); ++it) {
    if ((it->kind == HitKind::Item || it->kind == HitKind::ItemSticker || it->kind == HitKind::ItemFavorite || it->kind == HitKind::ItemPinned ||
         it->kind == HitKind::ItemDelete) &&
        RectContains(it->rect, point)) {
      SelectIndex(it->index, false);
      POINT screen_point = point;
      ClientToScreen(hwnd_, &screen_point);
      OpenSelectedContextMenu(screen_point);
      return;
    }
  }
}

void MainWindow::OnMouseWheel(short delta) {
  if (IsOverlayDialogVisible()) {
    return;
  }
  RECT client{};
  GetClientRect(hwnd_, &client);
  const PanelLayout layout = ComputeLayout(client.right, client.bottom);
  if (!RectContains(layout.list, hover_point_)) {
    return;
  }
  scroll_controller_->OnMouseWheel(delta, NowEpochMs());
}

void MainWindow::OnKeyboardNavigate(UINT virtual_key, bool shift_down) {
  if (IsOverlayDialogVisible()) {
    switch (virtual_key) {
      case VK_ESCAPE:
        CloseOverlayDialog(true);
        return;
      case VK_RETURN:
        CommitOverlayDialog();
        return;
      default:
        return;
    }
  }
  switch (virtual_key) {
    case VK_ESCAPE:
      if (search_textbox_.IsFocused()) {
        if (!search_textbox_.GetText().empty()) {
          ClearSearch();
          return;
        }
        search_focus_requested_ = false;
        SetPanelAllowsActivation(false);
        search_textbox_.SetFocused(false, hwnd_);
        RestoreExternalFocusRestoreContext();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
      }
      HideWindow();
      return;
    case VK_RETURN:
      if (shift_down) {
        PerformPlainTextPaste();
      } else {
        PerformDefaultPaste();
      }
      return;
    case VK_SPACE:
      OpenSelectedPreview();
      return;
    case VK_UP:
      if (state_.selected_index_ > 0) {
        SelectIndex(state_.selected_index_ - 1, false);
      }
      return;
    case VK_DOWN:
      if (state_.selected_index_ + 1 < state_.item_count_) {
        SelectIndex(state_.selected_index_ + 1, false);
      }
      return;
    case VK_PRIOR:
      if (state_.selected_index_ >= 0) {
        SelectIndex(std::max(0, state_.selected_index_ - 6), false);
      }
      return;
    case VK_NEXT:
      if (state_.selected_index_ >= 0) {
        SelectIndex(std::min(state_.item_count_ - 1, state_.selected_index_ + 6), false);
      }
      return;
    case VK_HOME:
      if (state_.item_count_ > 0) {
        SelectIndex(0, false);
      }
      return;
    case VK_END:
      if (state_.item_count_ > 0) {
        SelectIndex(state_.item_count_ - 1, false);
      }
      return;
    case VK_DELETE:
      DeleteSelected();
      return;
  }
}

bool MainWindow::TryPerformRowActionAtPoint(POINT point) {
  if (hwnd_ == nullptr) {
    return false;
  }
  const core::store::AppSettings render_settings =
      settings_store_ != nullptr ? settings_store_->current_settings() : core::store::AppSettings{};

  RECT client{};
  GetClientRect(hwnd_, &client);
  const PanelLayout layout = ComputeLayout(client.right, client.bottom);
  if (!RectContains(layout.list, point)) {
    return false;
  }

  const float visual_scroll_offset = scroll_controller_->VisualScrollOffset();
  const int list_height = layout.list.bottom - layout.list.top;
  const int item_height = GetItemHeight();
  const vlist::VisibleRange visible = viewport_.RangeAt(
      visual_scroll_offset, list_height, item_height, item_height);
  const int first_index = std::max(0, visible.first - 1);
  const int last_index = std::min(state_.item_count_ - 1, visible.last + 1);
  for (int index = first_index; index <= last_index; ++index) {
    const vlist::RowRect row_pos = vlist::RowRectFor(
        viewport_.heights(), index, static_cast<float>(layout.list.top), visual_scroll_offset);
    RECT row{
        layout.list.left,
        static_cast<LONG>(std::lround(row_pos.top)),
        layout.list.right,
        static_cast<LONG>(std::lround(row_pos.bottom))};
    if (!RectContains(row, point)) {
      continue;
    }

    const auto* summary = GetSummaryAt(index);
    if (summary == nullptr) {
      return false;
    }

    const RECT header_rect{
        row.left + kRowPaddingX,
        row.top + 2,
        row.right - kRowPaddingX,
        std::min(row.bottom - 2, row.top + 2 + kRowHeaderHeight)};
    RECT number_rect{header_rect.right - 22, header_rect.top + 4, header_rect.right, header_rect.bottom - 4};

    int action_right = number_rect.left - 6;
    if (render_settings.show_source_icon) {
      action_right -= (kRowHeaderIconSize + 12);
    }

    const bool show_sticker = ShouldShowThirdPartyStickerButton(*summary);
    const RECT delete_rect{
        action_right - kRowActionButtonSize,
        header_rect.top + (kRowHeaderHeight - kRowActionButtonSize) / 2,
        action_right,
        header_rect.top + (kRowHeaderHeight - kRowActionButtonSize) / 2 + kRowActionButtonSize};
    action_right = delete_rect.left - kRowActionGap;
    const RECT pinned_rect{
        action_right - kRowActionButtonSize,
        header_rect.top + (kRowHeaderHeight - kRowActionButtonSize) / 2,
        action_right,
        header_rect.top + (kRowHeaderHeight - kRowActionButtonSize) / 2 + kRowActionButtonSize};
    action_right = pinned_rect.left - kRowActionGap;
    const RECT favorite_rect{
        action_right - kRowActionButtonSize,
        header_rect.top + (kRowHeaderHeight - kRowActionButtonSize) / 2,
        action_right,
        header_rect.top + (kRowHeaderHeight - kRowActionButtonSize) / 2 + kRowActionButtonSize};
    RECT sticker_rect{};
    if (show_sticker) {
      action_right = favorite_rect.left - kRowActionGap;
      sticker_rect = RECT{
          action_right - kRowActionButtonSize,
          header_rect.top + (kRowHeaderHeight - kRowActionButtonSize) / 2,
          action_right,
          header_rect.top + (kRowHeaderHeight - kRowActionButtonSize) / 2 + kRowActionButtonSize};
    }

    if (show_sticker && RectContains(sticker_rect, point)) {
      SelectIndex(index, false);
      PerformThirdPartySticker();
      return true;
    }

    if (RectContains(favorite_rect, point)) {
      SelectIndex(index, false);
      ToggleFavoriteSelected();
      return true;
    }
    if (RectContains(pinned_rect, point)) {
      SelectIndex(index, false);
      TogglePinnedSelected();
      return true;
    }
    if (RectContains(delete_rect, point)) {
      SelectIndex(index, false);
      DeleteSelected();
      return true;
    }
    return false;
  }

  return false;
}

// ---- 悬停预览 ----

bool MainWindow::HoverPreviewEnabled() const {
  return settings_store_ != nullptr && settings_store_->current_settings().hover_preview_enabled;
}

void MainWindow::UpdateHoverPreview(int row) {
  if (!HoverPreviewEnabled() || IsOverlayDialogVisible() || row_drag_active_ || drag_scrollbar_) {
    CancelHoverPreview(false);
    return;
  }
  if (row < 0) {
    // 离开列表行：光标进入预览窗则保留显示（允许点预览内按钮）。
    CancelHoverPreview(true);
    return;
  }
  if (hover_preview_showing_ && hover_preview_row_ == row) {
    return;
  }
  if (hover_preview_row_ != row) {
    hover_preview_row_ = row;
    SetTimer(hwnd_, kHoverPreviewTimerId, 120, nullptr);
  }
}

void MainWindow::CancelHoverPreview(bool keep_if_pointer_in_preview) {
  ::KillTimer(hwnd_, kHoverPreviewTimerId);
  hover_preview_row_ = -1;
  if (!hover_preview_showing_) {
    return;
  }
  if (keep_if_pointer_in_preview && preview_window_.hwnd() != nullptr) {
    // 不立刻隐藏：预览窗与主面板之间有缝隙，鼠标离开主面板的瞬间
    // 光标多半还在缝隙里。给 250ms 缓冲，到点再按光标位置决定去留。
    SetTimer(hwnd_, kHoverPreviewGraceTimerId, 250, nullptr);
    return;
  }
  // 编辑中的预览窗不因悬停路径隐藏（用户正打字，鼠标可能只是飘出窗口）；
  // 显式隐藏走 HideWindow，那里先提交再隐藏。
  if (preview_window_.editing()) {
    return;
  }
  ::KillTimer(hwnd_, kHoverPreviewGraceTimerId);
  preview_window_.Hide();
  hover_preview_showing_ = false;
}

void MainWindow::OnHoverPreviewGraceTimer() {
  if (!hover_preview_showing_) {
    ::KillTimer(hwnd_, kHoverPreviewGraceTimerId);
    return;
  }
  if (preview_window_.ContainsCursor()) {
    // 停在预览窗上：继续守（允许长时间停留点 OCR 等按钮）。
    SetTimer(hwnd_, kHoverPreviewGraceTimerId, 250, nullptr);
    return;
  }
  // 缝隙超时未到达预览窗，或已回到主面板/桌面其它区域：隐藏。
  // 回到列表行时 mousemove 会在 120ms 后重新弹出。
  CancelHoverPreview(false);
}

void MainWindow::ShowHoverPreviewForRow(int row) {
  hover_preview_row_ = -1;
  if (!HoverPreviewEnabled() || row < 0) {
    return;
  }
  // 定时器触发时鼠标可能已移走：行必须仍与当前 hover 一致。
  if (RowIndexAtPoint(hover_point_) != row) {
    return;
  }
  const auto* summary = GetSummaryAt(row);
  if (summary == nullptr || history_feed_ == nullptr) {
    return;
  }
  if (preview_window_.hwnd() != nullptr && IsWindowVisible(preview_window_.hwnd()) &&
      preview_window_.ShowingClip(summary->id)) {
    hover_preview_showing_ = true;
    return;
  }
  RECT row_rect{0, 0, 0, 0};
  for (auto it = hit_regions_.rbegin(); it != hit_regions_.rend(); ++it) {
    if (it->kind == HitKind::Item && it->index == row) {
      row_rect = it->rect;
      break;
    }
  }
  if (row_rect.bottom <= row_rect.top) {
    return;
  }
  RECT anchor_screen = row_rect;
  ClientToScreen(hwnd_, reinterpret_cast<POINT*>(&anchor_screen.left));
  ClientToScreen(hwnd_, reinterpret_cast<POINT*>(&anchor_screen.right));
  core::store::ClipRecord clip;
  if (!history_feed_->GetClipById(summary->id, &clip).ok()) {
    return;
  }
  const common::Status created = EnsurePreviewWindowCreated();
  if (!created.ok()) {
    return;
  }
  preview_window_.ShowClipAt(clip, BuildPreviewHostActions(), anchor_screen, true);
  hover_preview_showing_ = true;
}

}  // namespace clipd2d::ui
