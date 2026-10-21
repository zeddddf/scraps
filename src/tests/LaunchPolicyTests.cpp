#include "tests/TestSupport.h"

#include "app/LaunchPolicy.h"

CLIPD2D_TEST(LaunchPolicyTreatsExistingInstallAsCompletedFirstRun) {
  clipd2d::core::store::AppSettings settings;
  settings.has_completed_first_run = false;
  settings.open_panel_on_first_run = false;
  const auto decision = clipd2d::app::DecideLaunchBehavior(settings, true, false, true);
  CLIPD2D_CHECK_EQ(decision.initial_panel_behavior, clipd2d::app::InitialPanelBehavior::ShowAfterFirstLayout);
  CLIPD2D_CHECK(decision.should_mark_first_run_complete);
}

CLIPD2D_TEST(LaunchPolicyHonorsFirstRunPanelToggleForFreshInstall) {
  clipd2d::core::store::AppSettings settings;
  settings.has_completed_first_run = false;
  settings.open_panel_on_first_run = false;
  const auto decision = clipd2d::app::DecideLaunchBehavior(settings, false, false, true);
  CLIPD2D_CHECK_EQ(decision.initial_panel_behavior, clipd2d::app::InitialPanelBehavior::Hidden);
  CLIPD2D_CHECK(decision.should_mark_first_run_complete);
}

CLIPD2D_TEST(LaunchPolicyStaysHiddenWhenLaunchIsExplicitlyHidden) {
  clipd2d::core::store::AppSettings settings;
  settings.has_completed_first_run = true;
  const auto decision = clipd2d::app::DecideLaunchBehavior(settings, true, true, false);
  CLIPD2D_CHECK_EQ(decision.initial_panel_behavior, clipd2d::app::InitialPanelBehavior::Hidden);
}
