#include "ui/ListScrollController.h"

#include <algorithm>
#include <cmath>

namespace clipd2d::ui {

ListScrollController::ListScrollController(ListScrollHost* host)
    : host_(host) {
}

int ListScrollController::MaxScrollOffset() const {
  if (host_ == nullptr) {
    return 0;
  }
  const int content_height = host_->GetTotalContentHeight();
  const int viewport = std::max(1, host_->GetViewportHeight());
  return std::max(0, content_height - viewport);
}

float ListScrollController::VisualScrollOffset() const {
  return std::clamp(animated_scroll_offset_, 0.0f, static_cast<float>(MaxScrollOffset()));
}

int ListScrollController::CurrentScrollOffset() const {
  return std::clamp(static_cast<int>(std::lround(VisualScrollOffset())), 0, MaxScrollOffset());
}

bool ListScrollController::IsListScrollAnimating() const {
  return scroll_animation_active_ || inertial_scroll_active_;
}

bool ListScrollController::ShouldShowScrollThumb() const {
  if (host_ == nullptr) {
    return false;
  }
  return host_->GetTotalContentHeight() > host_->GetViewportHeight();
}

void ListScrollController::SetScrollOffsetTarget(int offset, bool immediate) {
  const float prev_offset = animated_scroll_offset_;
  target_scroll_offset_ = std::clamp(offset, 0, MaxScrollOffset());
  StopInertialScroll(immediate);
  
  if (immediate) {
    animated_scroll_offset_ = static_cast<float>(target_scroll_offset_);
    scroll_animation_active_ = false;
    
    const float delta = animated_scroll_offset_ - prev_offset;
    last_frame_scroll_delta_ = delta;
    accumulated_scroll_delta_ += delta;
  } else if (std::fabs(animated_scroll_offset_ - static_cast<float>(target_scroll_offset_)) >= 0.5f) {
    scroll_animation_active_ = true;
    if (host_) host_->EnsureAnimationTimer();
  } else {
    animated_scroll_offset_ = static_cast<float>(target_scroll_offset_);
    scroll_animation_active_ = false;
  }
}

void ListScrollController::StopInertialScroll(bool snap_to_target) {
  inertial_scroll_active_ = false;
  scroll_velocity_ = 0.0f;
  last_frame_scroll_delta_ = 0.0f;
  scroll_blur_strength_ = 0.0f;
  if (snap_to_target) {
    animated_scroll_offset_ = static_cast<float>(target_scroll_offset_);
  }
}

void ListScrollController::OnMouseWheel(short wheel_delta, std::uint64_t current_time_ms) {
  wheel_delta_accumulator_ += wheel_delta;
  const int steps = wheel_delta_accumulator_ / WHEEL_DELTA;
  wheel_delta_accumulator_ %= WHEEL_DELTA;
  
  if (steps == 0) {
    return;
  }
  
  const bool rapid = last_wheel_tick_ms_ != 0 && (current_time_ms - last_wheel_tick_ms_) <= 160;
  last_wheel_tick_ms_ = current_time_ms;
  rapid_wheel_streak_ = rapid ? std::min(5, rapid_wheel_streak_ + 1) : 1;
  
  const float item_height = host_ ? static_cast<float>(host_->GetItemHeight()) : 60.0f;
  const float base_impulse_velocity = std::max(288.0f, item_height * 9.6f);
  const float rapid_gain = 1.0f + 0.22f * static_cast<float>(std::max(0, rapid_wheel_streak_ - 1));
  const float impulse = static_cast<float>(steps) * base_impulse_velocity * rapid_gain;
  const float max_velocity = item_height * 154.88f;
  
  manual_scroll_direction_ = steps > 0 ? -1 : 1;
  scroll_animation_active_ = false;
  scroll_velocity_ = std::clamp(scroll_velocity_ - impulse, -max_velocity, max_velocity);
  inertial_scroll_active_ = true;
  target_scroll_offset_ = CurrentScrollOffset();
  last_frame_scroll_delta_ = 0.0f;
  scroll_blur_strength_ = 0.0f;
  
  show_scrollbar_until_ms_ = current_time_ms + 1200;
  
  if (host_) {
    host_->EnsureAnimationTimer();
    host_->RequestRender();
  }
}

void ListScrollController::OnScrollbarDrag(int track_top, int travel, float ratio) {
  // Left unimplemented as we handle logic in MainWindow mostly, or we could handle it here.
  // We'll keep MainWindow handling the drag_scrollbar_ state, and it can just call SetScrollOffsetTarget.
}

void ListScrollController::SetAnimatedScrollOffset(float offset) {
  animated_scroll_offset_ = offset;
  target_scroll_offset_ = static_cast<int>(std::lround(offset));
}

bool ListScrollController::AdvanceAnimation(float delta_ms) {
  bool changed = false;
  const float max_offset = static_cast<float>(MaxScrollOffset());
  
  if (inertial_scroll_active_) {
    const float previous_offset = animated_scroll_offset_;
    const float speed_abs = std::fabs(scroll_velocity_);
    const float speed_t = std::clamp((speed_abs - 500.0f) / (1400.0f - 500.0f), 0.0f, 1.0f);
    const float damping = std::lerp(0.88f, 0.80f, speed_t);
    const float stop_velocity_threshold = std::lerp(12.0f, 28.0f, speed_t);
    const float stop_delta_threshold = std::lerp(0.04f, 0.08f, speed_t);
    
    animated_scroll_offset_ = std::clamp(
        animated_scroll_offset_ + scroll_velocity_ * (delta_ms / 1000.0f),
        0.0f,
        max_offset);
        
    last_frame_scroll_delta_ = animated_scroll_offset_ - previous_offset;
    scroll_velocity_ *= damping;
    target_scroll_offset_ = static_cast<int>(std::lround(animated_scroll_offset_));
    
    if (std::fabs(scroll_velocity_) < stop_velocity_threshold || std::fabs(last_frame_scroll_delta_) < stop_delta_threshold) {
      scroll_velocity_ = 0.0f;
      inertial_scroll_active_ = false;
    }
    if (animated_scroll_offset_ <= 0.0f || animated_scroll_offset_ >= max_offset) {
      scroll_velocity_ = 0.0f;
      inertial_scroll_active_ = false;
    }
    
    scroll_blur_strength_ = std::clamp(std::fabs(last_frame_scroll_delta_) / 18.0f, 0.0f, 1.0f);
    changed = true;
    
  } else if (scroll_animation_active_) {
    const float target = static_cast<float>(target_scroll_offset_);
    const float dt = delta_ms / 1000.0f;
    const float previous_offset = animated_scroll_offset_;
    const float omega = 28.0f;
    const float f = 1.0f + omega * dt;
    const float diff = animated_scroll_offset_ - target;
    const float temp = (scroll_animation_velocity_ + omega * diff) * dt;
    
    animated_scroll_offset_ = target + (diff + temp) / (f * f);
    scroll_animation_velocity_ = (scroll_animation_velocity_ - omega * temp) / (f * f);
    last_frame_scroll_delta_ = animated_scroll_offset_ - previous_offset;
    
    if (std::fabs(target - animated_scroll_offset_) < 0.5f && std::fabs(scroll_animation_velocity_) < 5.0f) {
      animated_scroll_offset_ = target;
      scroll_animation_velocity_ = 0.0f;
      scroll_animation_active_ = false;
    }
    
    scroll_blur_strength_ = 0.0f;
    changed = true;
  }
  
  return changed;
}

} // namespace clipd2d::ui
