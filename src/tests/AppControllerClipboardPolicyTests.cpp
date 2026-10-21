#include "core/commands/AppController.Internal.h"

#include "tests/TestSupport.h"

namespace clipd2d::tests {

CLIPD2D_TEST(AppControllerDeferredClipboardRetryDelayProvidesThreeSteps) {
  CLIPD2D_CHECK_EQ(clipd2d::core::commands::detail::DeferredClipboardRetryDelayMs(0).value_or(0), 150u);
  CLIPD2D_CHECK_EQ(clipd2d::core::commands::detail::DeferredClipboardRetryDelayMs(1).value_or(0), 350u);
  CLIPD2D_CHECK_EQ(clipd2d::core::commands::detail::DeferredClipboardRetryDelayMs(2).value_or(0), 700u);
  CLIPD2D_CHECK(!clipd2d::core::commands::detail::DeferredClipboardRetryDelayMs(3).has_value());
}

CLIPD2D_TEST(AppControllerShouldSkipClipboardCaptureOnlyForMatchingStickerWriteback) {
  CLIPD2D_CHECK(clipd2d::core::commands::detail::ShouldSkipClipboardCapture(
      clipd2d::core::store::ClipArrivalReason::InternalStickerWriteback,
      42,
      42));
  CLIPD2D_CHECK(!clipd2d::core::commands::detail::ShouldSkipClipboardCapture(
      clipd2d::core::store::ClipArrivalReason::InternalStickerWriteback,
      42,
      43));
  CLIPD2D_CHECK(!clipd2d::core::commands::detail::ShouldSkipClipboardCapture(
      clipd2d::core::store::ClipArrivalReason::InternalClipboardWriteback,
      42,
      42));
}

CLIPD2D_TEST(AppControllerResolveClipboardTouchPolicyKeepsPositionForInternalWriteback) {
  bool should_clear = false;
  const auto policy = clipd2d::core::commands::detail::ResolveClipboardTouchPolicy(
      clipd2d::core::store::ClipArrivalReason::InternalClipboardWriteback,
      42,
      42,
      false,
      &should_clear);
  CLIPD2D_CHECK_EQ(policy, clipd2d::core::store::ReuseTouchPolicy::KeepPositionOnReuse);
  CLIPD2D_CHECK(should_clear);
}

CLIPD2D_TEST(AppControllerResolveClipboardTouchPolicyPromotesInternalWritebackWhenEnabled) {
  bool should_clear = false;
  const auto policy = clipd2d::core::commands::detail::ResolveClipboardTouchPolicy(
      clipd2d::core::store::ClipArrivalReason::InternalClipboardWriteback,
      42,
      42,
      true,
      &should_clear);
  CLIPD2D_CHECK_EQ(policy, clipd2d::core::store::ReuseTouchPolicy::PromoteOnReuse);
  CLIPD2D_CHECK(should_clear);
}

CLIPD2D_TEST(AppControllerResolveClipboardTouchPolicyKeepsStickerWritebackStable) {
  bool should_clear = false;
  const auto policy = clipd2d::core::commands::detail::ResolveClipboardTouchPolicy(
      clipd2d::core::store::ClipArrivalReason::InternalStickerWriteback,
      42,
      42,
      true,
      &should_clear);
  CLIPD2D_CHECK_EQ(policy, clipd2d::core::store::ReuseTouchPolicy::KeepPositionOnReuse);
  CLIPD2D_CHECK(should_clear);
}

CLIPD2D_TEST(AppControllerResolveClipboardTouchPolicyClearsExpiredFutureSequences) {
  bool should_clear = false;
  const auto policy = clipd2d::core::commands::detail::ResolveClipboardTouchPolicy(
      clipd2d::core::store::ClipArrivalReason::InternalClipboardWriteback,
      42,
      43,
      false,
      &should_clear);
  CLIPD2D_CHECK_EQ(policy, clipd2d::core::store::ReuseTouchPolicy::PromoteOnReuse);
  CLIPD2D_CHECK(should_clear);
}

CLIPD2D_TEST(AppControllerResolveClipboardTouchPolicyKeepsEarlierMismatchesArmed) {
  bool should_clear = true;
  const auto policy = clipd2d::core::commands::detail::ResolveClipboardTouchPolicy(
      clipd2d::core::store::ClipArrivalReason::InternalClipboardWriteback,
      42,
      41,
      false,
      &should_clear);
  CLIPD2D_CHECK_EQ(policy, clipd2d::core::store::ReuseTouchPolicy::PromoteOnReuse);
  CLIPD2D_CHECK(!should_clear);
}

CLIPD2D_TEST(AppControllerShouldRetryTransientImageClipboardFailures) {
  CLIPD2D_CHECK(clipd2d::core::commands::ShouldRetryClipboardCapture(
      clipd2d::common::Status::Error(
          clipd2d::common::StatusCode::PlatformError,
          "Read bitmap info failed. png=0/0 dibv5=0/0 dib=0/0 bitmap=1/1")));
  CLIPD2D_CHECK(clipd2d::core::commands::ShouldRetryClipboardCapture(
      clipd2d::common::Status::Error(
          clipd2d::common::StatusCode::InvalidArgument,
          "Image formats were advertised but no payload was ready. png=1/0 dibv5=0/0 dib=0/0 bitmap=0/0")));
}

CLIPD2D_TEST(AppControllerShouldNotRetryNonTransientClipboardPlatformErrors) {
  CLIPD2D_CHECK(!clipd2d::core::commands::ShouldRetryClipboardCapture(
      clipd2d::common::Status::Error(
          clipd2d::common::StatusCode::PlatformError,
          "Create WIC factory failed.")));
}

CLIPD2D_TEST(AppControllerBuildClipboardRefreshRequestPreservesViewportForKeepPosition) {
  const auto request = clipd2d::core::commands::detail::BuildClipboardRefreshRequest(
      clipd2d::core::store::ReuseTouchPolicy::KeepPositionOnReuse,
      123);
  CLIPD2D_CHECK_EQ(request.target_clip_id.value_or(0), 123);
  CLIPD2D_CHECK_EQ(request.selection_policy, clipd2d::core::commands::RefreshSelectionPolicy::PreserveCurrent);
  CLIPD2D_CHECK_EQ(request.scroll_policy, clipd2d::core::commands::RefreshScrollPolicy::PreserveViewport);
}

CLIPD2D_TEST(AppControllerBuildClipboardRefreshRequestJumpsToTopForPromote) {
  const auto request = clipd2d::core::commands::detail::BuildClipboardRefreshRequest(
      clipd2d::core::store::ReuseTouchPolicy::PromoteOnReuse,
      123);
  CLIPD2D_CHECK_EQ(request.target_clip_id.value_or(0), 123);
  CLIPD2D_CHECK_EQ(request.selection_policy, clipd2d::core::commands::RefreshSelectionPolicy::SelectTargetClip);
  CLIPD2D_CHECK_EQ(request.scroll_policy, clipd2d::core::commands::RefreshScrollPolicy::JumpToTop);
}

CLIPD2D_TEST(AppControllerOtpFilterMatchesExactlySixDigitCodes) {
  CLIPD2D_CHECK(clipd2d::core::commands::detail::IsLikelyOtpCode("123456"));
  CLIPD2D_CHECK(clipd2d::core::commands::detail::IsLikelyOtpCode("000000"));
  CLIPD2D_CHECK(clipd2d::core::commands::detail::IsLikelyOtpCode("908172"));
}

CLIPD2D_TEST(AppControllerOtpFilterAllowsOrdinaryShortText) {
  CLIPD2D_CHECK(!clipd2d::core::commands::detail::IsLikelyOtpCode("2026"));
  CLIPD2D_CHECK(!clipd2d::core::commands::detail::IsLikelyOtpCode("Test"));
  CLIPD2D_CHECK(!clipd2d::core::commands::detail::IsLikelyOtpCode("hello1"));
  CLIPD2D_CHECK(!clipd2d::core::commands::detail::IsLikelyOtpCode("12345"));
  CLIPD2D_CHECK(!clipd2d::core::commands::detail::IsLikelyOtpCode("1234567"));
  CLIPD2D_CHECK(!clipd2d::core::commands::detail::IsLikelyOtpCode("12a456"));
  CLIPD2D_CHECK(!clipd2d::core::commands::detail::IsLikelyOtpCode(""));
}

}  // namespace clipd2d::tests
