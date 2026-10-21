#pragma once

#include <windows.h>
#include <cstdint>

namespace clipd2d::ui {

class ListScrollHost {
public:
  virtual ~ListScrollHost() = default;
  virtual int GetItemCount() const = 0;
  virtual int GetItemHeight() const = 0;
  // Total content height in px (variable-height aware). Replaces
  // count * item_height for scroll range math.
  virtual int GetTotalContentHeight() const = 0;
  virtual int GetViewportHeight() const = 0;
  virtual void RequestRender() = 0;
  virtual void EnsureAnimationTimer() = 0;
};

class ListScrollController {
public:
  explicit ListScrollController(ListScrollHost* host);

  void OnMouseWheel(short wheel_delta, std::uint64_t current_time_ms);
  void OnScrollbarDrag(int track_top, int travel, float ratio);

  void SetScrollOffsetTarget(int offset, bool immediate);
  void StopInertialScroll(bool snap_to_target);
  
  bool AdvanceAnimation(float delta_ms);

  float VisualScrollOffset() const;
  int CurrentScrollOffset() const;
  int MaxScrollOffset() const;

  bool IsListScrollAnimating() const;
  bool ShouldShowScrollThumb() const;
  
  
  float GetScrollBlurStrength() const { return scroll_blur_strength_; }
  float GetLastFrameScrollDelta() const { return last_frame_scroll_delta_; }
  float GetScrollVelocity() const { return scroll_velocity_; }
  bool IsInertialScrollActive() const { return inertial_scroll_active_; }
  std::int64_t& GetShowScrollbarUntilMs() { return show_scrollbar_until_ms_; }
  int GetTargetScrollOffset() const { return target_scroll_offset_; }
  int& GetWheelDeltaAccumulator() { return wheel_delta_accumulator_; }
  int& GetManualScrollDirection() { return manual_scroll_direction_; }

  float GetAnimatedScrollOffset() const { return animated_scroll_offset_; }
  void SetAnimatedScrollOffset(float offset);
  float GetScrollAnimationVelocity() const { return scroll_animation_velocity_; }
  void SetScrollAnimationVelocity(float v) { scroll_animation_velocity_ = v; }

  void SetLastFrameScrollDelta(float v) { last_frame_scroll_delta_ = v; }
  void SetScrollVelocity(float v) { scroll_velocity_ = v; }
  void SetTargetScrollOffset(int v) { target_scroll_offset_ = v; }
  void SetScrollAnimationActive(bool v) { scroll_animation_active_ = v; }
  void SetShowScrollbarUntilMs(std::int64_t v) { show_scrollbar_until_ms_ = v; }
  void SetInertialScrollActive(bool v) { inertial_scroll_active_ = v; }
  void SetScrollBlurStrength(float v) { scroll_blur_strength_ = v; }
  float GetAccumulatedScrollDelta() const { return accumulated_scroll_delta_; }
  void AddAccumulatedScrollDelta(float delta) { accumulated_scroll_delta_ += delta; }
  void ClearAccumulatedScrollDelta() { accumulated_scroll_delta_ = 0.0f; }

  bool IsScrollAnimationActive() const { return scroll_animation_active_; }
  std::int64_t GetShowScrollbarUntilMs() const { return show_scrollbar_until_ms_; }


private:
  ListScrollHost* host_;

  int target_scroll_offset_ = 0;
  float animated_scroll_offset_ = 0.0f;
  
  float scroll_velocity_ = 0.0f;
  float scroll_animation_velocity_ = 0.0f;
  float last_frame_scroll_delta_ = 0.0f;
  float scroll_blur_strength_ = 0.0f;
  float accumulated_scroll_delta_ = 0.0f;
  
  bool scroll_animation_active_ = false;
  bool inertial_scroll_active_ = false;
  
  int wheel_delta_accumulator_ = 0;
  std::uint64_t last_wheel_tick_ms_ = 0;
  int rapid_wheel_streak_ = 0;
  int manual_scroll_direction_ = 0;
  
  std::int64_t show_scrollbar_until_ms_ = 0;
};

} // namespace clipd2d::ui
