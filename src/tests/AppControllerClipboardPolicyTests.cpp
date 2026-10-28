#include "core/commands/AppController.Internal.h"

#include "tests/TestSupport.h"

namespace clipd2d::tests {

CLIPD2D_TEST(AppControllerDeferredClipboardRetryDelayProvidesThreeSteps) {
  CLIPD2D_CHECK_EQ(clipd2d::core::commands::detail::DeferredClipboardRetryDelayMs(0).value_or(0), 150u);
  CLIPD2D_CHECK_EQ(clipd2d::core::commands::detail::DeferredClipboardRetryDelayMs(1).value_or(0), 350u);
  CLIPD2D_CHECK_EQ(clipd2d::core::commands::detail::DeferredClipboardRetryDelayMs(2).value_or(0), 700u);
  CLIPD2D_CHECK(!clipd2d::core::commands::detail::DeferredClipboardRetryDelayMs(3).has_value());
}

CLIPD2D_TEST(AppControllerShouldSkipClipboardCaptureOnlyForInternalStickerWriteback) {
  CLIPD2D_CHECK(clipd2d::core::commands::detail::ShouldSkipClipboardCapture(
      clipd2d::core::store::ClipArrivalReason::InternalStickerWriteback,
      true));
  CLIPD2D_CHECK(!clipd2d::core::commands::detail::ShouldSkipClipboardCapture(
      clipd2d::core::store::ClipArrivalReason::InternalStickerWriteback,
      false));
  CLIPD2D_CHECK(!clipd2d::core::commands::detail::ShouldSkipClipboardCapture(
      clipd2d::core::store::ClipArrivalReason::InternalClipboardWriteback,
      true));
}

CLIPD2D_TEST(AppControllerResolveClipboardTouchPolicyKeepsPositionForInternalWriteback) {
  bool should_clear = false;
  const auto policy = clipd2d::core::commands::detail::ResolveClipboardTouchPolicy(
      clipd2d::core::store::ClipArrivalReason::InternalClipboardWriteback,
      true,
      false,
      &should_clear);
  CLIPD2D_CHECK_EQ(policy, clipd2d::core::store::ReuseTouchPolicy::KeepPositionOnReuse);
  CLIPD2D_CHECK(should_clear);
}

CLIPD2D_TEST(AppControllerResolveClipboardTouchPolicyPromotesInternalWritebackWhenEnabled) {
  bool should_clear = false;
  const auto policy = clipd2d::core::commands::detail::ResolveClipboardTouchPolicy(
      clipd2d::core::store::ClipArrivalReason::InternalClipboardWriteback,
      true,
      true,
      &should_clear);
  CLIPD2D_CHECK_EQ(policy, clipd2d::core::store::ReuseTouchPolicy::PromoteOnReuse);
  CLIPD2D_CHECK(should_clear);
}

CLIPD2D_TEST(AppControllerResolveClipboardTouchPolicyKeepsStickerWritebackStable) {
  bool should_clear = false;
  const auto policy = clipd2d::core::commands::detail::ResolveClipboardTouchPolicy(
      clipd2d::core::store::ClipArrivalReason::InternalStickerWriteback,
      true,
      true,
      &should_clear);
  CLIPD2D_CHECK_EQ(policy, clipd2d::core::store::ReuseTouchPolicy::KeepPositionOnReuse);
  CLIPD2D_CHECK(should_clear);
}

// 区间语义：事件是否内部写回由入队时的 [baseline, final] 区间判定并随事件携带；
// 消费时不再比对 sequence——写回期间 sequence 会多次推进（EmptyClipboard 与
// CF_DIB 派生格式合成），第一个事件的 sequence 必然小于 armed 最终值。
CLIPD2D_TEST(AppControllerResolveClipboardTouchPolicyMarkCarriesDecisionNotSequence) {
  bool should_clear = false;
  // internal_writeback=false 时即使 reason 仍在（接力事件）也不得保留位置。
  const auto promote = clipd2d::core::commands::detail::ResolveClipboardTouchPolicy(
      clipd2d::core::store::ClipArrivalReason::InternalClipboardWriteback,
      false,
      false,
      &should_clear);
  CLIPD2D_CHECK_EQ(promote, clipd2d::core::store::ReuseTouchPolicy::PromoteOnReuse);
  CLIPD2D_CHECK(should_clear);
  // 无 reason 且非 internal：外部常规捕获。
  const auto external = clipd2d::core::commands::detail::ResolveClipboardTouchPolicy(
      std::nullopt,
      false,
      false,
      &should_clear);
  CLIPD2D_CHECK_EQ(external, clipd2d::core::store::ReuseTouchPolicy::PromoteOnReuse);
  CLIPD2D_CHECK(should_clear);
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

CLIPD2D_TEST(AppControllerOtpFilterAllowsOrdinaryShortText) {  CLIPD2D_CHECK(!clipd2d::core::commands::detail::IsLikelyOtpCode("2026"));
  CLIPD2D_CHECK(!clipd2d::core::commands::detail::IsLikelyOtpCode("Test"));
  CLIPD2D_CHECK(!clipd2d::core::commands::detail::IsLikelyOtpCode("hello1"));
  CLIPD2D_CHECK(!clipd2d::core::commands::detail::IsLikelyOtpCode("12345"));
  CLIPD2D_CHECK(!clipd2d::core::commands::detail::IsLikelyOtpCode("1234567"));
  CLIPD2D_CHECK(!clipd2d::core::commands::detail::IsLikelyOtpCode("12a456"));
  CLIPD2D_CHECK(!clipd2d::core::commands::detail::IsLikelyOtpCode(""));
}

// 身份兜底：序列窗口被派生格式合成击穿时，用内容身份补判「这是自己刚写回的那条」。
CLIPD2D_TEST(AppControllerMatchesInternalWritebackIdentityRequiresSameKeyWithinWindow) {
  using clipd2d::core::commands::detail::MatchesInternalWritebackIdentity;

  CLIPD2D_CHECK(MatchesInternalWritebackIdentity("abc123", "abc123", 120, 5000));
  // 身份不同 —— 外部真的复制了别的内容，必须允许置顶。
  CLIPD2D_CHECK(!MatchesInternalWritebackIdentity("abc123", "def456", 120, 5000));
  // 超出回声窗口 —— 同样是外部复制。
  CLIPD2D_CHECK(!MatchesInternalWritebackIdentity("abc123", "abc123", 6000, 5000));
  // 任一侧为空都不能算命中（空 key 相等会误吞所有捕获）。
  CLIPD2D_CHECK(!MatchesInternalWritebackIdentity("", "", 120, 5000));
  CLIPD2D_CHECK(!MatchesInternalWritebackIdentity("abc123", "", 120, 5000));
  CLIPD2D_CHECK(!MatchesInternalWritebackIdentity("", "abc123", 120, 5000));
}

}  // namespace clipd2d::tests
