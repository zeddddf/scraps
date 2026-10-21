#include "core/commands/RefreshRequest.h"

#include "tests/TestSupport.h"

namespace clipd2d::tests {

CLIPD2D_TEST(RefreshRequestPreserveViewportKeepsCurrentBaseScroll) {
  const auto request = clipd2d::core::commands::MakePreserveViewportRefreshRequest();
  CLIPD2D_CHECK_EQ(
      clipd2d::core::commands::ResolveInitialRefreshScrollTarget(request, 240),
      240);
  CLIPD2D_CHECK(!clipd2d::core::commands::ShouldRevealSelectionAfterRefresh(request));
}

CLIPD2D_TEST(RefreshRequestJumpToTopResetsBaseScroll) {
  const auto request = clipd2d::core::commands::MakeJumpToTopRefreshRequest(77);
  CLIPD2D_CHECK_EQ(
      clipd2d::core::commands::ResolveInitialRefreshScrollTarget(request, 240),
      0);
  CLIPD2D_CHECK(!clipd2d::core::commands::ShouldRevealSelectionAfterRefresh(request));
}

CLIPD2D_TEST(RefreshRequestRevealSelectionKeepsBaseScrollButMarksReveal) {
  const auto request = clipd2d::core::commands::MakeRefreshRequest(
      55,
      clipd2d::core::commands::RefreshSelectionPolicy::SelectTargetClip,
      clipd2d::core::commands::RefreshScrollPolicy::RevealSelection);
  CLIPD2D_CHECK_EQ(
      clipd2d::core::commands::ResolveInitialRefreshScrollTarget(request, 240),
      240);
  CLIPD2D_CHECK(clipd2d::core::commands::ShouldRevealSelectionAfterRefresh(request));
}

CLIPD2D_TEST(RefreshRequestRevealSelectionScrollTargetAlignsOffscreenRowToBottom) {
  const int target = clipd2d::core::commands::ResolveRevealSelectionScrollTarget(
      0,
      480,
      576,
      384);
  CLIPD2D_CHECK_EQ(target, 192);
}

CLIPD2D_TEST(RefreshRequestMergeKeepsJumpToTopAndTargetWhenLaterPreserveArrives) {
  const auto merged = clipd2d::core::commands::MergeRefreshRequest(
      clipd2d::core::commands::MakeJumpToTopRefreshRequest(77),
      clipd2d::core::commands::MakePreserveViewportRefreshRequest(
          77,
          clipd2d::core::commands::RefreshSelectionPolicy::PreserveCurrent));
  CLIPD2D_CHECK_EQ(merged.target_clip_id.value_or(0), 77);
  CLIPD2D_CHECK_EQ(merged.selection_policy, clipd2d::core::commands::RefreshSelectionPolicy::SelectTargetClip);
  CLIPD2D_CHECK_EQ(merged.scroll_policy, clipd2d::core::commands::RefreshScrollPolicy::JumpToTop);
}

CLIPD2D_TEST(RefreshRequestMergeAllowsNewExplicitTargetToOverrideOlderWeakRequest) {
  const auto merged = clipd2d::core::commands::MergeRefreshRequest(
      clipd2d::core::commands::MakePreserveViewportRefreshRequest(
          std::nullopt,
          clipd2d::core::commands::RefreshSelectionPolicy::SelectFirstIfNone),
      clipd2d::core::commands::MakeJumpToTopRefreshRequest(99));
  CLIPD2D_CHECK_EQ(merged.target_clip_id.value_or(0), 99);
  CLIPD2D_CHECK_EQ(merged.selection_policy, clipd2d::core::commands::RefreshSelectionPolicy::SelectTargetClip);
  CLIPD2D_CHECK_EQ(merged.scroll_policy, clipd2d::core::commands::RefreshScrollPolicy::JumpToTop);
}

}  // namespace clipd2d::tests
