#pragma once

#include <array>

#include <windows.h>

namespace clipd2d::ui::detail {

enum class InteractionGuardKind {
  PopupMenu = 0,
  SettingsWindow = 1,
  PreviewWindow = 2,
  Count = 3,
};

enum class InteractionHidePolicy {
  None = 0,
  DeferExternalHide,
  SwallowExternalHide,
};

enum class ExternalPointerDisposition {
  Ignore = 0,
  HideImmediately,
  DeferHide,
};

struct DeferredExternalHide {
  bool active = false;
  POINT screen_point{};
};

struct ExternalPointerCapture {
  bool internal_hit = false;
  ExternalPointerDisposition disposition = ExternalPointerDisposition::Ignore;
  InteractionGuardKind deferred_guard = InteractionGuardKind::PopupMenu;
};

struct InteractionGuardState {
  std::array<int, static_cast<size_t>(InteractionGuardKind::Count)> counts{};
  DeferredExternalHide deferred_external_hide{};
};

inline constexpr size_t InteractionGuardIndex(InteractionGuardKind kind) {
  return static_cast<size_t>(kind);
}

inline void ClearDeferredExternalHide(InteractionGuardState* state) {
  if (state == nullptr) {
    return;
  }
  state->deferred_external_hide = DeferredExternalHide{};
}

inline void BeginInteractionGuard(InteractionGuardState* state, InteractionGuardKind kind) {
  if (state == nullptr) {
    return;
  }
  ++state->counts[InteractionGuardIndex(kind)];
}

inline void SetVisibilityInteractionGuard(InteractionGuardState* state, InteractionGuardKind kind, bool active) {
  if (state == nullptr) {
    return;
  }
  state->counts[InteractionGuardIndex(kind)] = active ? 1 : 0;
}

inline bool EndInteractionGuard(InteractionGuardState* state, InteractionGuardKind kind) {
  if (state == nullptr) {
    return false;
  }
  int& counter = state->counts[InteractionGuardIndex(kind)];
  if (counter <= 0) {
    return false;
  }
  --counter;
  return counter == 0;
}

inline bool HasInteractionGuard(const InteractionGuardState& state, InteractionGuardKind kind) {
  return state.counts[InteractionGuardIndex(kind)] > 0;
}

inline bool HasAnyInteractionGuard(const InteractionGuardState& state) {
  for (int count : state.counts) {
    if (count > 0) {
      return true;
    }
  }
  return false;
}

inline InteractionHidePolicy CurrentGuardHidePolicy(const InteractionGuardState& state) {
  if (HasInteractionGuard(state, InteractionGuardKind::SettingsWindow) ||
      HasInteractionGuard(state, InteractionGuardKind::PreviewWindow)) {
    return InteractionHidePolicy::SwallowExternalHide;
  }
  if (HasInteractionGuard(state, InteractionGuardKind::PopupMenu)) {
    return InteractionHidePolicy::DeferExternalHide;
  }
  return InteractionHidePolicy::None;
}

inline ExternalPointerDisposition HandleExternalPointerDown(const InteractionGuardState& state) {
  switch (CurrentGuardHidePolicy(state)) {
    case InteractionHidePolicy::None:
      return ExternalPointerDisposition::HideImmediately;
    case InteractionHidePolicy::DeferExternalHide:
      return ExternalPointerDisposition::DeferHide;
    case InteractionHidePolicy::SwallowExternalHide:
    default:
      return ExternalPointerDisposition::Ignore;
  }
}

inline ExternalPointerCapture BuildExternalPointerCapture(
    const InteractionGuardState& state,
    bool internal_hit) {
  ExternalPointerCapture capture;
  capture.internal_hit = internal_hit;
  if (internal_hit) {
    capture.disposition = ExternalPointerDisposition::Ignore;
    return capture;
  }
  capture.disposition = HandleExternalPointerDown(state);
  if (capture.disposition == ExternalPointerDisposition::DeferHide) {
    capture.deferred_guard = InteractionGuardKind::PopupMenu;
  }
  return capture;
}

inline void RecordDeferredExternalHide(
    InteractionGuardState* state,
    POINT screen_point) {
  if (state == nullptr) {
    return;
  }
  state->deferred_external_hide.active = true;
  state->deferred_external_hide.screen_point = screen_point;
}

inline void ClearTransientInteractionState(InteractionGuardState* state) {
  if (state == nullptr) {
    return;
  }
  state->counts[InteractionGuardIndex(InteractionGuardKind::PopupMenu)] = 0;
  ClearDeferredExternalHide(state);
}

inline bool FlushDeferredExternalHideIfNeeded(InteractionGuardState* state) {
  if (state == nullptr || !state->deferred_external_hide.active) {
    return false;
  }
  if (HasInteractionGuard(*state, InteractionGuardKind::PopupMenu)) {
    return false;
  }
  if (CurrentGuardHidePolicy(*state) == InteractionHidePolicy::SwallowExternalHide) {
    ClearDeferredExternalHide(state);
    return false;
  }
  ClearDeferredExternalHide(state);
  return true;
}

}  // namespace clipd2d::ui::detail
