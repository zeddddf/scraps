#include "app/LaunchPolicy.h"

namespace clipd2d::app {

LaunchDecision DecideLaunchBehavior(
    const core::store::AppSettings& settings,
    bool has_any_settings_data,
    bool has_any_clips,
    bool requested_visible) {
  LaunchDecision decision;
  const bool looks_like_existing_install =
      has_any_settings_data || has_any_clips;
  const bool first_run_completed = settings.has_completed_first_run || looks_like_existing_install;
  decision.should_mark_first_run_complete = !settings.has_completed_first_run;

  if (!requested_visible) {
    decision.initial_panel_behavior = InitialPanelBehavior::Hidden;
    return decision;
  }

  if (!first_run_completed && !settings.open_panel_on_first_run) {
    decision.initial_panel_behavior = InitialPanelBehavior::Hidden;
    return decision;
  }

  decision.initial_panel_behavior = InitialPanelBehavior::ShowAfterFirstLayout;
  return decision;
}

}  // namespace clipd2d::app
