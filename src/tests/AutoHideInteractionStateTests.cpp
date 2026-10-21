#include "tests/TestSupport.h"

#include "ui/AutoHideInteractionState.h"

namespace clipd2d::tests {

CLIPD2D_TEST(AutoHideStateWithoutGuardsHidesImmediately) {
  clipd2d::ui::detail::InteractionGuardState state;
  const auto capture = clipd2d::ui::detail::BuildExternalPointerCapture(state, false);
  CLIPD2D_CHECK_EQ(capture.disposition, clipd2d::ui::detail::ExternalPointerDisposition::HideImmediately);
}

CLIPD2D_TEST(AutoHideStatePopupGuardDefersExternalClickUntilMenuEnds) {
  clipd2d::ui::detail::InteractionGuardState state;
  clipd2d::ui::detail::BeginInteractionGuard(&state, clipd2d::ui::detail::InteractionGuardKind::PopupMenu);
  const auto capture = clipd2d::ui::detail::BuildExternalPointerCapture(state, false);
  CLIPD2D_CHECK_EQ(capture.disposition, clipd2d::ui::detail::ExternalPointerDisposition::DeferHide);
}

CLIPD2D_TEST(AutoHideStateSettingsGuardSwallowsExternalHide) {
  clipd2d::ui::detail::InteractionGuardState state;
  clipd2d::ui::detail::BeginInteractionGuard(&state, clipd2d::ui::detail::InteractionGuardKind::SettingsWindow);
  const auto capture = clipd2d::ui::detail::BuildExternalPointerCapture(state, false);
  CLIPD2D_CHECK_EQ(capture.disposition, clipd2d::ui::detail::ExternalPointerDisposition::Ignore);
}

CLIPD2D_TEST(AutoHideStateInternalClickAlwaysIgnoresEvenWithPopupGuard) {
  clipd2d::ui::detail::InteractionGuardState state;
  clipd2d::ui::detail::BeginInteractionGuard(&state, clipd2d::ui::detail::InteractionGuardKind::PopupMenu);
  const auto capture = clipd2d::ui::detail::BuildExternalPointerCapture(state, true);
  CLIPD2D_CHECK_EQ(capture.disposition, clipd2d::ui::detail::ExternalPointerDisposition::Ignore);
}

CLIPD2D_TEST(AutoHideStateFlushesDeferredHideAfterPopupGuardEnds) {
  clipd2d::ui::detail::InteractionGuardState state;
  clipd2d::ui::detail::BeginInteractionGuard(&state, clipd2d::ui::detail::InteractionGuardKind::PopupMenu);
  clipd2d::ui::detail::RecordDeferredExternalHide(&state, POINT{100, 200});
  CLIPD2D_CHECK(clipd2d::ui::detail::EndInteractionGuard(&state, clipd2d::ui::detail::InteractionGuardKind::PopupMenu));
  CLIPD2D_CHECK(clipd2d::ui::detail::FlushDeferredExternalHideIfNeeded(&state));
}

CLIPD2D_TEST(AutoHideStateDiscardsDeferredHideWhenSettingsStillProtected) {
  clipd2d::ui::detail::InteractionGuardState state;
  clipd2d::ui::detail::BeginInteractionGuard(&state, clipd2d::ui::detail::InteractionGuardKind::PopupMenu);
  clipd2d::ui::detail::BeginInteractionGuard(&state, clipd2d::ui::detail::InteractionGuardKind::SettingsWindow);
  clipd2d::ui::detail::RecordDeferredExternalHide(&state, POINT{100, 200});
  CLIPD2D_CHECK(clipd2d::ui::detail::EndInteractionGuard(&state, clipd2d::ui::detail::InteractionGuardKind::PopupMenu));
  CLIPD2D_CHECK(!clipd2d::ui::detail::FlushDeferredExternalHideIfNeeded(&state));
  CLIPD2D_CHECK(!state.deferred_external_hide.active);
}

CLIPD2D_TEST(AutoHideStateTransientResetPreservesSettingsProtection) {
  clipd2d::ui::detail::InteractionGuardState state;
  clipd2d::ui::detail::SetVisibilityInteractionGuard(
      &state,
      clipd2d::ui::detail::InteractionGuardKind::SettingsWindow,
      true);
  clipd2d::ui::detail::BeginInteractionGuard(&state, clipd2d::ui::detail::InteractionGuardKind::PopupMenu);
  clipd2d::ui::detail::RecordDeferredExternalHide(&state, POINT{5, 9});

  clipd2d::ui::detail::ClearTransientInteractionState(&state);

  CLIPD2D_CHECK(clipd2d::ui::detail::HasInteractionGuard(
      state,
      clipd2d::ui::detail::InteractionGuardKind::SettingsWindow));
  CLIPD2D_CHECK(!clipd2d::ui::detail::HasInteractionGuard(
      state,
      clipd2d::ui::detail::InteractionGuardKind::PopupMenu));
  CLIPD2D_CHECK(!state.deferred_external_hide.active);
  CLIPD2D_CHECK_EQ(
      clipd2d::ui::detail::CurrentGuardHidePolicy(state),
      clipd2d::ui::detail::InteractionHidePolicy::SwallowExternalHide);
}

CLIPD2D_TEST(AutoHideStateVisibilityGuardUpdatesAreIdempotent) {
  clipd2d::ui::detail::InteractionGuardState state;
  clipd2d::ui::detail::SetVisibilityInteractionGuard(
      &state,
      clipd2d::ui::detail::InteractionGuardKind::PreviewWindow,
      true);
  clipd2d::ui::detail::SetVisibilityInteractionGuard(
      &state,
      clipd2d::ui::detail::InteractionGuardKind::PreviewWindow,
      true);

  CLIPD2D_CHECK_EQ(
      state.counts[clipd2d::ui::detail::InteractionGuardIndex(
          clipd2d::ui::detail::InteractionGuardKind::PreviewWindow)],
      1);

  clipd2d::ui::detail::SetVisibilityInteractionGuard(
      &state,
      clipd2d::ui::detail::InteractionGuardKind::PreviewWindow,
      false);
  CLIPD2D_CHECK(!clipd2d::ui::detail::HasInteractionGuard(
      state,
      clipd2d::ui::detail::InteractionGuardKind::PreviewWindow));
}

}  // namespace clipd2d::tests
