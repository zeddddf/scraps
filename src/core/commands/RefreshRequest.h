#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>

namespace clipd2d::core::commands {

enum class RefreshSelectionPolicy {
  PreserveCurrent,
  SelectFirstIfNone,
  SelectTargetClip,
};

enum class RefreshScrollPolicy {
  PreserveViewport,
  RevealSelection,
  JumpToTop,
};

struct RefreshRequest {
  std::optional<std::int64_t> target_clip_id;
  RefreshSelectionPolicy selection_policy = RefreshSelectionPolicy::PreserveCurrent;
  RefreshScrollPolicy scroll_policy = RefreshScrollPolicy::PreserveViewport;
};

inline RefreshRequest MakeRefreshRequest(
    std::optional<std::int64_t> target_clip_id,
    RefreshSelectionPolicy selection_policy,
    RefreshScrollPolicy scroll_policy) {
  RefreshRequest request;
  request.target_clip_id = target_clip_id;
  request.selection_policy = selection_policy;
  request.scroll_policy = scroll_policy;
  return request;
}

inline RefreshRequest MakePreserveViewportRefreshRequest(
    std::optional<std::int64_t> target_clip_id = std::nullopt,
    RefreshSelectionPolicy selection_policy = RefreshSelectionPolicy::PreserveCurrent) {
  return MakeRefreshRequest(target_clip_id, selection_policy, RefreshScrollPolicy::PreserveViewport);
}

inline RefreshRequest MakeJumpToTopRefreshRequest(
    std::optional<std::int64_t> target_clip_id,
    RefreshSelectionPolicy selection_policy = RefreshSelectionPolicy::SelectTargetClip) {
  return MakeRefreshRequest(target_clip_id, selection_policy, RefreshScrollPolicy::JumpToTop);
}

inline int RefreshSelectionPolicyPriority(RefreshSelectionPolicy policy) {
  switch (policy) {
    case RefreshSelectionPolicy::SelectTargetClip:
      return 3;
    case RefreshSelectionPolicy::SelectFirstIfNone:
      return 2;
    case RefreshSelectionPolicy::PreserveCurrent:
    default:
      return 1;
  }
}

inline int RefreshScrollPolicyPriority(RefreshScrollPolicy policy) {
  switch (policy) {
    case RefreshScrollPolicy::JumpToTop:
      return 3;
    case RefreshScrollPolicy::RevealSelection:
      return 2;
    case RefreshScrollPolicy::PreserveViewport:
    default:
      return 1;
  }
}

inline RefreshRequest MergeRefreshRequest(const RefreshRequest& current, const RefreshRequest& incoming) {
  RefreshRequest merged = current;

  const int current_selection_priority = RefreshSelectionPolicyPriority(current.selection_policy);
  const int incoming_selection_priority = RefreshSelectionPolicyPriority(incoming.selection_policy);
  if (incoming_selection_priority >= current_selection_priority) {
    merged.selection_policy = incoming.selection_policy;
  }

  merged.target_clip_id = current.target_clip_id;
  if (!merged.target_clip_id.has_value()) {
    merged.target_clip_id = incoming.target_clip_id;
  } else if (incoming.target_clip_id.has_value() && incoming_selection_priority >= current_selection_priority) {
    merged.target_clip_id = incoming.target_clip_id;
  }

  const int current_scroll_priority = RefreshScrollPolicyPriority(current.scroll_policy);
  const int incoming_scroll_priority = RefreshScrollPolicyPriority(incoming.scroll_policy);
  if (incoming_scroll_priority >= current_scroll_priority) {
    merged.scroll_policy = incoming.scroll_policy;
  }

  return merged;
}

inline int ResolveInitialRefreshScrollTarget(const RefreshRequest& request, int current_target_scroll) {
  return request.scroll_policy == RefreshScrollPolicy::JumpToTop ? 0 : std::max(0, current_target_scroll);
}

inline bool ShouldSelectTargetClip(const RefreshRequest& request) {
  return request.selection_policy == RefreshSelectionPolicy::SelectTargetClip && request.target_clip_id.has_value();
}

inline bool ShouldSelectFirstIfNone(const RefreshRequest& request) {
  return request.selection_policy == RefreshSelectionPolicy::SelectFirstIfNone;
}

inline bool ShouldRevealSelectionAfterRefresh(const RefreshRequest& request) {
  return request.scroll_policy == RefreshScrollPolicy::RevealSelection;
}

inline int ResolveRevealSelectionScrollTarget(
    int base_scroll_target,
    int item_top,
    int item_bottom,
    int viewport_height) {
  const int clamped_base = std::max(0, base_scroll_target);
  const int clamped_viewport = std::max(1, viewport_height);
  const int viewport_top = clamped_base;
  const int viewport_bottom = viewport_top + clamped_viewport;
  if (item_top < viewport_top) {
    return std::max(0, item_top);
  }
  if (item_bottom > viewport_bottom) {
    return std::max(0, item_bottom - clamped_viewport);
  }
  return clamped_base;
}

}  // namespace clipd2d::core::commands
