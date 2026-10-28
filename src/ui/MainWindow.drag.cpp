#include "ui/MainWindowInternal.h"

namespace clipd2d::ui {

void MainWindow::StepAnimation() {
  // L6:统一经 has_pending_work() 决定是否继续泵帧(硬规则 3)——含骨架 shimmer
  // 与加载在途等历史遗漏项,否则 shimmer 只走一拍就冻结。
  if (hwnd_ == nullptr || !has_pending_work()) {
    KillTimer(hwnd_, kAnimationTimerId);
    animation_timer_active_ = false;
    return;
  }
  const bool drag_auto_scroll_active = row_drag_active_ && std::fabs(row_drag_auto_scroll_velocity_) > 1.0f;

  const std::uint64_t now = static_cast<std::uint64_t>(GetTickCount64());
  const float fallback_ms = static_cast<float>(std::max<UINT>(1, frame_interval_ms_));
  float delta_ms = fallback_ms;
  LARGE_INTEGER counter{};
  if (performance_counter_frequency_.QuadPart > 0 && QueryPerformanceCounter(&counter)) {
    if (last_animation_tick_qpc_ != 0) {
      delta_ms = static_cast<float>(
          (static_cast<double>(counter.QuadPart - last_animation_tick_qpc_) * 1000.0) /
          static_cast<double>(performance_counter_frequency_.QuadPart));
      delta_ms = std::clamp(delta_ms, 0.5f, 40.0f);
    }
    last_animation_tick_qpc_ = counter.QuadPart;
  } else {
    delta_ms = last_animation_tick_ms_ == 0 ? fallback_ms : static_cast<float>(now - last_animation_tick_ms_);
  }
  last_animation_tick_ms_ = now;

  bool content_changed = false;
  scroll_controller_->SetLastFrameScrollDelta( 0.0f);

  if (drag_auto_scroll_active) {
    const float max_offset = static_cast<float>(scroll_controller_->MaxScrollOffset());
    const float next_offset = std::clamp(
        scroll_controller_->GetAnimatedScrollOffset() + row_drag_auto_scroll_velocity_ * (delta_ms / 1000.0f),
        0.0f,
        max_offset);
    if (std::fabs(next_offset - scroll_controller_->GetAnimatedScrollOffset()) > 0.01f) {
      scroll_controller_->SetLastFrameScrollDelta( next_offset - scroll_controller_->GetAnimatedScrollOffset());
      scroll_controller_->SetAnimatedScrollOffset( next_offset);
      scroll_controller_->SetTargetScrollOffset( static_cast<int>(std::lround(scroll_controller_->GetAnimatedScrollOffset())));
      content_changed = true;

      if (row_drag_session_.has_value()) {
        RebuildRowDragSessionSlots();
        UpdateRowReorderAnimationTargets();
      }
    }
  } else if (scroll_controller_->IsInertialScrollActive()) {
    const float previous_offset = scroll_controller_->GetAnimatedScrollOffset();
    const float max_offset = static_cast<float>(scroll_controller_->MaxScrollOffset());
    const float speed_abs = std::fabs(scroll_controller_->GetScrollVelocity());
    const float speed_t = std::clamp((speed_abs - 500.0f) / (1400.0f - 500.0f), 0.0f, 1.0f);
    const float damping = std::lerp(0.88f, 0.80f, speed_t);
    const float stop_velocity_threshold = std::lerp(12.0f, 28.0f, speed_t);
    const float stop_delta_threshold = std::lerp(0.04f, 0.08f, speed_t);
    scroll_controller_->SetAnimatedScrollOffset( std::clamp(
        scroll_controller_->GetAnimatedScrollOffset() + scroll_controller_->GetScrollVelocity() * (delta_ms / 1000.0f),
        0.0f,
        max_offset));
    scroll_controller_->SetLastFrameScrollDelta( scroll_controller_->GetAnimatedScrollOffset() - previous_offset);
    scroll_controller_->SetScrollVelocity(scroll_controller_->GetScrollVelocity() * damping);
    scroll_controller_->SetTargetScrollOffset( static_cast<int>(std::lround(scroll_controller_->GetAnimatedScrollOffset())));
    if (std::fabs(scroll_controller_->GetScrollVelocity()) < stop_velocity_threshold || std::fabs(scroll_controller_->GetLastFrameScrollDelta()) < stop_delta_threshold) {
      scroll_controller_->SetScrollVelocity(0.0f);
      scroll_controller_->SetInertialScrollActive(false);
    }
    if (scroll_controller_->GetAnimatedScrollOffset() <= 0.0f || scroll_controller_->GetAnimatedScrollOffset() >= max_offset) {
      scroll_controller_->SetScrollVelocity(0.0f);
      scroll_controller_->SetInertialScrollActive(false);
    }
    scroll_controller_->SetScrollBlurStrength(std::clamp(std::fabs(scroll_controller_->GetLastFrameScrollDelta()) / 18.0f, 0.0f, 1.0f));
    content_changed = true;
  } else if (scroll_controller_->IsScrollAnimationActive()) {
    const float target = static_cast<float>(scroll_controller_->GetTargetScrollOffset());
    const float distance = std::fabs(target - scroll_controller_->GetAnimatedScrollOffset());
    const float distance_items = distance / static_cast<float>(std::max(1, GetItemHeight()));
    const float response = std::clamp(18.0f + distance_items * 2.8f, 18.0f, 40.0f);
    const float blend = 1.0f - std::exp(-response * (delta_ms / 1000.0f));
    const float previous_offset = scroll_controller_->GetAnimatedScrollOffset();
    scroll_controller_->SetAnimatedScrollOffset(scroll_controller_->GetAnimatedScrollOffset() + (target - scroll_controller_->GetAnimatedScrollOffset()) * blend);
    scroll_controller_->SetLastFrameScrollDelta( scroll_controller_->GetAnimatedScrollOffset() - previous_offset);
    const float dt = delta_ms / 1000.0f;
    const float velocity = dt > 0.0f ? (scroll_controller_->GetLastFrameScrollDelta() / dt) : 0.0f;
    scroll_controller_->SetScrollAnimationVelocity(velocity);
    if (std::fabs(target - scroll_controller_->GetAnimatedScrollOffset()) < 0.5f) {
      scroll_controller_->SetAnimatedScrollOffset( target);
      scroll_controller_->SetScrollAnimationActive(false);
      scroll_controller_->SetScrollAnimationVelocity(0.0f);
    }
    scroll_controller_->SetScrollBlurStrength(0.0f);
    content_changed = true;
    if (row_drag_active_ && row_drag_session_.has_value()) {
      RECT client{};
      GetClientRect(hwnd_, &client);
      const PanelLayout layout = ComputeLayout(client.right, client.bottom);
      RebuildRowDragSessionSlots();
      row_drag_session_->desired_slot = ComputeDesiredRowDragSlot(layout.list, scroll_controller_->VisualScrollOffset());
      UpdateRowReorderAnimationTargets();
    }
  }

  scroll_controller_->AddAccumulatedScrollDelta(scroll_controller_->GetLastFrameScrollDelta());

  if (row_drag_active_ && row_drag_session_.has_value()) {
    RECT client{};
    GetClientRect(hwnd_, &client);
    const PanelLayout layout = ComputeLayout(client.right, client.bottom);
    const float visual_scroll_offset = scroll_controller_->VisualScrollOffset();
    row_drag_session_->desired_slot = ComputeDesiredRowDragSlot(layout.list, visual_scroll_offset);

    if (row_drag_gap_step_.has_value()) {
      row_drag_gap_step_->elapsed_ms += delta_ms;
      row_drag_gap_step_->progress = std::clamp(
          row_drag_gap_step_->elapsed_ms / std::max(1.0f, row_drag_gap_step_->duration_ms),
          0.0f,
          1.0f);
      content_changed = true;
      if (row_drag_gap_step_->progress >= 1.0f) {
        row_drag_gap_step_.reset();
      }
    }

    const int remaining_slots = std::abs(row_drag_session_->desired_slot - row_drag_session_->gap_slot);
    const float distance_factor = std::clamp((static_cast<float>(remaining_slots) - 1.0f) / 6.0f, 0.0f, 1.0f);
    const float drag_factor = std::clamp(row_drag_velocity_rows_per_s_ / 10.0f, 0.0f, 1.0f);
    const float scroll_factor = std::clamp(std::fabs(row_drag_auto_scroll_velocity_) / 2600.0f, 0.0f, 1.0f);
    const float boost = std::max({distance_factor * 0.55f, drag_factor * 0.35f, scroll_factor * 0.45f});
    const float next_step_progress_gate = std::lerp(0.42f, 0.12f, boost);
    const float next_step_elapsed_gate = std::lerp(50.0f, 14.0f, boost);
    const bool can_start_next_step =
        !row_drag_gap_step_.has_value() ||
        row_drag_gap_step_->progress >= next_step_progress_gate ||
        row_drag_gap_step_->elapsed_ms >= next_step_elapsed_gate;
    if (can_start_next_step && StartNextRowDragGapStep(layout.list, visual_scroll_offset)) {
      content_changed = true;
    }
  }

  if (!row_drag_visual_rows_.empty()) {
    bool any_animating = false;
    const float delta_seconds = std::max(0.001f, delta_ms / 1000.0f);
    RECT client{};
    GetClientRect(hwnd_, &client);
    const PanelLayout animation_layout = ComputeLayout(client.right, client.bottom);
    const float animation_visual_scroll_offset = scroll_controller_->VisualScrollOffset();
    auto step_ease = [](float t) -> float {
      t = std::clamp(t, 0.0f, 1.0f);
      if (t < 0.25f) {
        const float local = t / 0.25f;
        return 0.38f * (1.0f - std::pow(1.0f - local, 2.0f));
      }
      if (t < 0.75f) {
        const float local = (t - 0.25f) / 0.50f;
        return 0.38f + local * 0.42f;
      }
      const float local = (t - 0.75f) / 0.25f;
      return 0.80f + (1.0f - std::pow(1.0f - local, 2.0f)) * 0.20f;
    };
    for (auto& [index, state] : row_drag_visual_rows_) {
      if (row_drag_gap_step_.has_value() && row_drag_gap_step_->moving_index == index) {
        const float from_top = RowDragSessionSlotTop(row_drag_gap_step_->from_slot, animation_layout.list, animation_visual_scroll_offset);
        const float to_top = RowDragSessionSlotTop(row_drag_gap_step_->to_slot, animation_layout.list, animation_visual_scroll_offset);
        const float eased = step_ease(row_drag_gap_step_->progress);
        state.current_top = std::lerp(from_top, to_top, eased);
        state.target_top = to_top;
        state.velocity = 0.0f;
        any_animating = row_drag_gap_step_->progress < 1.0f;
        content_changed = true;
        continue;
      }
      const float distance = state.target_top - state.current_top;
      if (std::fabs(distance) <= 0.25f) {
        state.current_top = state.target_top;
        state.velocity = 0.0f;
        continue;
      }
      const float direction = distance > 0.0f ? 1.0f : -1.0f;
      const int remaining_slots = row_drag_session_.has_value()
          ? std::abs(row_drag_session_->desired_slot - row_drag_session_->gap_slot)
          : 0;
      const float distance_factor = std::clamp((static_cast<float>(remaining_slots) - 1.0f) / 6.0f, 0.0f, 1.0f);
      const float drag_factor = std::clamp(row_drag_velocity_rows_per_s_ / 10.0f, 0.0f, 1.0f);
      const float scroll_factor = std::clamp(std::fabs(row_drag_auto_scroll_velocity_) / 2600.0f, 0.0f, 1.0f);
      const float catchup = std::max({distance_factor, drag_factor * 0.7f, scroll_factor * 0.8f});
      const float row_target_center =
          state.target_top + static_cast<float>(GetItemHeight()) * 0.5f;
      const float drag_center =
          static_cast<float>(row_drag_point_.y - row_drag_pointer_offset_y_) +
          static_cast<float>(GetItemHeight()) * 0.5f;
      const float drag_dist_rows =
          std::fabs(drag_center - row_target_center) /
          static_cast<float>(std::max(1, GetItemHeight()));
      const float near_drag = std::clamp(1.2f - drag_dist_rows, 0.0f, 1.0f);
      const float speed_scale =
          std::lerp(0.78f, 1.0f, 1.0f - near_drag) * std::lerp(1.0f, 2.8f, catchup);
      const float desired_speed =
          std::clamp(std::fabs(distance) * 16.0f, 260.0f, 3600.0f) * speed_scale;
      const float accel_gain = 14.0f + 16.0f * catchup;
      const float accel = std::clamp(accel_gain * delta_seconds, 0.0f, 1.0f);
      state.velocity += (desired_speed - state.velocity) * accel;
      const float step = std::min(std::fabs(distance), state.velocity * delta_seconds);
      state.current_top += direction * step;
      any_animating = true;
      content_changed = true;
    }
    for (auto it = row_drag_visual_rows_.begin(); it != row_drag_visual_rows_.end();) {
      const bool protected_by_active_step =
          row_drag_gap_step_.has_value() && row_drag_gap_step_->moving_index == it->first;
      if (!protected_by_active_step &&
          std::fabs(it->second.current_top - it->second.target_top) <= 0.25f) {
        it = row_drag_visual_rows_.erase(it);
      } else {
        ++it;
      }
    }
    row_reorder_animation_active_ =
        any_animating || row_drag_gap_step_.has_value() || !row_drag_visual_rows_.empty();
  } else {
    row_reorder_animation_active_ = row_drag_gap_step_.has_value();
  }

  // L6:骨架 shimmer 由时间驱动(相位在渲染时计算),每拍都要置脏重绘。
  if (skeleton_animating_) {
    content_changed = true;
  }

  // L6:统一经 has_pending_work() 决定是否停泵(硬规则 3)。
  if (!has_pending_work()) {
    KillTimer(hwnd_, kAnimationTimerId);
    animation_timer_active_ = false;
  }

  
  UpdateScrollBar();
  if (!row_drag_active_ || scroll_controller_->IsListScrollAnimating() || drag_auto_scroll_active) {
    RequestVisiblePages();
  }
  if (content_changed) {
    InvalidateListRegion();
  }
}

void MainWindow::ClearRowReorderAnimation() {
  row_drag_visual_rows_.clear();
  row_drag_gap_step_.reset();
  row_reorder_animation_active_ = false;
  row_drag_auto_scroll_velocity_ = (0.0f);
  row_drag_velocity_rows_per_s_ = 0.0f;
  row_drag_last_update_tick_ms_ = 0;
  row_drag_last_update_qpc_ = 0;
  row_drag_visual_scroll_offset_ = 0;
}

void MainWindow::BeginRowDragSession() {
  ClearRowDragSession();
  if (!row_drag_active_ || row_drag_scope_ == ReorderScope::None || row_drag_item_index_ < 0) {
    return;
  }

  RowDragSession session;
  session.ordered_indexes.reserve(state_.item_count_);
  for (int index = 0; index < state_.item_count_; ++index) {
    const auto* summary = GetSummaryAt(index);
    if (summary != nullptr && CanReorderRow(*summary)) {
      if (index == row_drag_item_index_) {
        session.original_gap_slot = static_cast<int>(session.ordered_indexes.size());
      } else {
        session.ordered_indexes.push_back(index);
      }
    }
  }
  session.original_ordered_indexes = session.ordered_indexes;
  session.dragged_index = row_drag_item_index_;
  session.gap_slot = session.original_gap_slot;
  session.desired_slot = session.gap_slot;
  row_drag_session_ = std::move(session);
  RebuildRowDragSessionSlots();
  row_drag_velocity_rows_per_s_ = 0.0f;
  row_drag_last_update_tick_ms_ = GetTickCount64();
  LARGE_INTEGER counter{};
  row_drag_last_update_qpc_ =
      (performance_counter_frequency_.QuadPart > 0 && QueryPerformanceCounter(&counter))
          ? counter.QuadPart
          : 0;
  row_drag_visual_scroll_offset_ = scroll_controller_->VisualScrollOffset();
  row_drop_index_ = row_drag_item_index_;
}

void MainWindow::ClearRowDragSession() {
  row_drag_session_.reset();
ClearRowReorderAnimation();
}

void MainWindow::RebuildRowDragSessionSlots() {
  if (!row_drag_session_.has_value()) {
    return;
  }
  row_drag_session_->display_slot_by_index.clear();
  RECT client{};
  GetClientRect(hwnd_, &client);
  const PanelLayout layout = ComputeLayout(client.right, client.bottom);
  const float visual_scroll_offset = scroll_controller_->VisualScrollOffset();
  const int slot_count = static_cast<int>(row_drag_session_->ordered_indexes.size()) + 1;
  row_drag_session_->slot_tops.assign(slot_count, 0.0f);
  for (int slot = 0; slot < slot_count; ++slot) {
    row_drag_session_->slot_tops[slot] = RowDragSessionSlotTop(slot, layout.list, visual_scroll_offset);
  }
  for (int position = 0; position < static_cast<int>(row_drag_session_->ordered_indexes.size()); ++position) {
    const int index = row_drag_session_->ordered_indexes[position];
    const int display_slot = position < row_drag_session_->gap_slot ? position : position + 1;
    row_drag_session_->display_slot_by_index[index] = display_slot;
  }
  row_drag_session_->desired_slot = std::clamp(row_drag_session_->desired_slot, 0, std::max(0, slot_count - 1));
  row_drag_session_->gap_slot = std::clamp(row_drag_session_->gap_slot, 0, std::max(0, slot_count - 1));
}

float MainWindow::RowDragSessionSlotTop(int slot, const RECT& list_rect, float visual_scroll_offset) const {
  return static_cast<float>(list_rect.top + slot * GetItemHeight() - visual_scroll_offset);
}

int MainWindow::ComputeDesiredRowDragSlot(const RECT& list_rect, float visual_scroll_offset) const {
  if (!row_drag_active_ || !row_drag_session_.has_value()) {
    return -1;
  }
  const int item_height = GetItemHeight();
  const int drag_top = row_drag_point_.y - row_drag_pointer_offset_y_;
  const int drag_center_y = drag_top + item_height / 2;
  const float local_center = static_cast<float>(drag_center_y - list_rect.top + visual_scroll_offset);
  const int desired_slot = static_cast<int>(std::floor(local_center / static_cast<float>(item_height)));
  return std::clamp(desired_slot, 0, static_cast<int>(row_drag_session_->ordered_indexes.size()));
}

bool MainWindow::StartNextRowDragGapStep(const RECT& list_rect, float visual_scroll_offset) {
  if (!row_drag_active_ || !row_drag_session_.has_value()) {
    return false;
  }
  if (row_drag_session_->desired_slot < 0 || row_drag_session_->desired_slot == row_drag_session_->gap_slot) {
    return false;
  }
  const int previous_gap_slot = row_drag_session_->gap_slot;
  const int direction = row_drag_session_->desired_slot > row_drag_session_->gap_slot ? 1 : -1;
  const int moving_position = direction > 0 ? row_drag_session_->gap_slot : row_drag_session_->gap_slot - 1;
  if (moving_position < 0 || moving_position >= static_cast<int>(row_drag_session_->ordered_indexes.size())) {
    return false;
  }

  const int moving_index = row_drag_session_->ordered_indexes[moving_position];
  const int from_slot = direction > 0 ? row_drag_session_->gap_slot + 1 : row_drag_session_->gap_slot - 1;
  const int to_slot = row_drag_session_->gap_slot;
  if (from_slot < 0 || to_slot < 0) {
    return false;
  }

  const float from_top = RowDragSessionSlotTop(from_slot, list_rect, visual_scroll_offset);
  auto visual_it = row_drag_visual_rows_.find(moving_index);
  RowDragVisualState moving_state;
  if (visual_it != row_drag_visual_rows_.end()) {
    moving_state = visual_it->second;
  } else {
    moving_state.current_top = from_top;
    moving_state.velocity = 0.0f;
  }

  row_drag_session_->gap_slot += direction;
  RebuildRowDragSessionSlots();
  row_drop_index_ = row_drag_item_index_;
  row_drop_after_ = false;

  moving_state.target_top = RowDragSessionSlotTop(to_slot, list_rect, scroll_controller_->VisualScrollOffset());
  row_drag_visual_rows_[moving_index] = moving_state;

  const int remaining_slots = std::abs(row_drag_session_->desired_slot - row_drag_session_->gap_slot);
  const float distance_factor =
      std::clamp((static_cast<float>(remaining_slots) - 1.0f) / 6.0f, 0.0f, 1.0f);
  const float drag_factor = std::clamp(row_drag_velocity_rows_per_s_ / 10.0f, 0.0f, 1.0f);
  const float scroll_factor =
      std::clamp(std::fabs(row_drag_auto_scroll_velocity_) / 2600.0f, 0.0f, 1.0f);
  const float adaptive_boost = std::max(
      {distance_factor * 0.55f, drag_factor * 0.35f, scroll_factor * 0.45f});
  const float adaptive_duration = std::lerp(205.0f, 72.0f, adaptive_boost);

  row_drag_gap_step_ = RowDragGapStep{
      moving_index,
      from_slot,
      to_slot,
      0.0f,
      adaptive_duration,
      0.0f};
  UpdateRowReorderAnimationTargets();
  return row_drag_session_->gap_slot != previous_gap_slot;
}

bool MainWindow::RowDragSessionHasActiveMotion() const {
  return row_drag_active_ &&
      row_drag_session_.has_value() &&
      (row_reorder_animation_active_ ||
       row_drag_gap_step_.has_value() ||
       !row_drag_visual_rows_.empty() ||
       std::fabs(row_drag_auto_scroll_velocity_) > 1.0f ||
       row_drag_session_->desired_slot != row_drag_session_->gap_slot);
}

float MainWindow::RowReorderDisplayTop(int index, float default_top, const RECT& list_rect, float visual_scroll_offset) const {
  if (row_drag_active_ && row_drag_scope_ != ReorderScope::None && row_drag_session_.has_value()) {
    if (const auto visual_it = row_drag_visual_rows_.find(index); visual_it != row_drag_visual_rows_.end()) {
      return visual_it->second.current_top;
    }
    const auto slot_it = row_drag_session_->display_slot_by_index.find(index);
    if (slot_it != row_drag_session_->display_slot_by_index.end()) {
      return RowDragSessionSlotTop(slot_it->second, list_rect, visual_scroll_offset);
    }
  }
  return default_top;
}

void MainWindow::UpdateRowReorderAnimationTargets() {
  if (!row_drag_session_.has_value() || hwnd_ == nullptr) {
    row_drag_visual_rows_.clear();
    row_reorder_animation_active_ = false;
    return;
  }

  RECT client{};
  GetClientRect(hwnd_, &client);
  const PanelLayout layout = ComputeLayout(client.right, client.bottom);
  const float visual_scroll_offset = scroll_controller_->VisualScrollOffset();
  const float scroll_delta = visual_scroll_offset - row_drag_visual_scroll_offset_;
  if (std::fabs(scroll_delta) > 0.01f) {
    for (auto& [index, state] : row_drag_visual_rows_) {
      (void)index;
      state.current_top -= scroll_delta;
      state.target_top -= scroll_delta;
    }
  }
  row_drag_visual_scroll_offset_ = visual_scroll_offset;

  bool any_animating = false;
  for (auto it = row_drag_visual_rows_.begin(); it != row_drag_visual_rows_.end();) {
    const int index = it->first;
    if (row_drag_gap_step_.has_value() && row_drag_gap_step_->moving_index == index) {
      it->second.target_top = RowDragSessionSlotTop(row_drag_gap_step_->to_slot, layout.list, visual_scroll_offset);
      if (std::fabs(it->second.current_top - it->second.target_top) > 0.25f) {
        any_animating = true;
      }
      ++it;
      continue;
    }

    const auto slot_it = row_drag_session_->display_slot_by_index.find(index);
    if (slot_it == row_drag_session_->display_slot_by_index.end()) {
      it = row_drag_visual_rows_.erase(it);
      continue;
    }
    it->second.target_top = RowDragSessionSlotTop(slot_it->second, layout.list, visual_scroll_offset);
    if (std::fabs(it->second.current_top - it->second.target_top) > 0.25f) {
      any_animating = true;
    }
    ++it;
  }

  row_reorder_animation_active_ = any_animating || row_drag_gap_step_.has_value() || !row_drag_visual_rows_.empty();
}


}  // namespace clipd2d::ui
