#pragma once

#include "core/store/ClipTypes.h"

namespace clipd2d::app {

enum class InitialPanelBehavior {
  Hidden,
  ShowAfterFirstLayout,
};

struct LaunchDecision {
  InitialPanelBehavior initial_panel_behavior = InitialPanelBehavior::Hidden;
  bool should_mark_first_run_complete = false;
};

LaunchDecision DecideLaunchBehavior(
    const core::store::AppSettings& settings,
    bool has_any_settings_data,
    bool has_any_clips,
    bool requested_visible);

}  // namespace clipd2d::app
