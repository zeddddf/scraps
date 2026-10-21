#include "tests/TestSupport.h"

#include <cstdlib>

#include "ui/ClipVisualAssets.h"
#include "ui/ClipVisualFileContent.h"

CLIPD2D_TEST(ClipVisualFileContentUsesFourByThreeThumbnailContainer) {
  const RECT content_rect{0, 0, 240, 60};
  const auto layout = clipd2d::ui::file_content_preview::ComputeLayout(
      content_rect,
      160,
      90,
      1,
      clipd2d::ui::file_content_preview::FileVisualMode::ThumbnailCard);
  const int container_width = layout.front_card_rect.right - layout.front_card_rect.left;
  const int container_height = layout.front_card_rect.bottom - layout.front_card_rect.top;
  CLIPD2D_CHECK(std::abs(container_width * 3 - container_height * 4) <= 4);
}

CLIPD2D_TEST(ClipVisualFileContentShowsBadgeForMultiFile) {
  const RECT content_rect{0, 0, 240, 60};
  const auto layout = clipd2d::ui::file_content_preview::ComputeLayout(
      content_rect,
      160,
      90,
      3,
      clipd2d::ui::file_content_preview::FileVisualMode::ThumbnailCard);
  CLIPD2D_CHECK(layout.show_badge);
  CLIPD2D_CHECK_EQ(layout.badge_rect.right - layout.badge_rect.left, 28);
  CLIPD2D_CHECK_EQ(layout.badge_rect.bottom - layout.badge_rect.top, 18);
}

CLIPD2D_TEST(ClipVisualFileContentAnchorsFrontCardToBottomRightOfStack) {
  const RECT content_rect{0, 0, 240, 60};
  const auto layout = clipd2d::ui::file_content_preview::ComputeLayout(
      content_rect,
      160,
      90,
      3,
      clipd2d::ui::file_content_preview::FileVisualMode::ThumbnailCard);
  CLIPD2D_CHECK_EQ(layout.front_card_rect.right, layout.thumbnail_block_rect.right);
  CLIPD2D_CHECK_EQ(layout.front_card_rect.bottom, layout.thumbnail_block_rect.bottom);
}

CLIPD2D_TEST(ClipVisualFileContentBackmostCardMovesLeftAndUp) {
  const RECT content_rect{0, 0, 240, 60};
  const auto layout = clipd2d::ui::file_content_preview::ComputeLayout(
      content_rect,
      160,
      90,
      3,
      clipd2d::ui::file_content_preview::FileVisualMode::ThumbnailCard);
  CLIPD2D_CHECK(layout.backmost_card_rect.left < layout.front_card_rect.left);
  CLIPD2D_CHECK(layout.backmost_card_rect.top < layout.front_card_rect.top);
  CLIPD2D_CHECK_EQ(layout.backmost_card_rect.left, layout.thumbnail_block_rect.left);
  CLIPD2D_CHECK_EQ(layout.backmost_card_rect.top, layout.thumbnail_block_rect.top);
}

CLIPD2D_TEST(ClipVisualFileContentIconModeUsesSquareIconBounds) {
  const RECT content_rect{0, 0, 240, 60};
  const auto layout = clipd2d::ui::file_content_preview::ComputeLayout(
      content_rect,
      32,
      32,
      1,
      clipd2d::ui::file_content_preview::FileVisualMode::IconOnly);
  const int width = layout.front_card_rect.right - layout.front_card_rect.left;
  const int height = layout.front_card_rect.bottom - layout.front_card_rect.top;
  CLIPD2D_CHECK_EQ(width, height);
}

CLIPD2D_TEST(ClipVisualFileContentSingleFileIconHasNoBadgeOrStack) {
  const RECT content_rect{0, 0, 240, 60};
  const auto layout = clipd2d::ui::file_content_preview::ComputeLayout(
      content_rect,
      32,
      32,
      1,
      clipd2d::ui::file_content_preview::FileVisualMode::SingleFileIcon);
  CLIPD2D_CHECK(!layout.show_badge);
  CLIPD2D_CHECK_EQ(layout.backmost_card_rect.left, layout.front_card_rect.left);
  CLIPD2D_CHECK_EQ(layout.backmost_card_rect.top, layout.front_card_rect.top);
}

CLIPD2D_TEST(ClipVisualFileContentSingleFileIconUsesLargerBoundsThanStackIconMode) {
  const RECT content_rect{0, 0, 240, 60};
  const auto compact = clipd2d::ui::file_content_preview::ComputeLayout(
      content_rect,
      32,
      32,
      1,
      clipd2d::ui::file_content_preview::FileVisualMode::SingleFileIcon);
  const auto stacked = clipd2d::ui::file_content_preview::ComputeLayout(
      content_rect,
      32,
      32,
      1,
      clipd2d::ui::file_content_preview::FileVisualMode::IconOnly);
  const int compact_width = compact.front_card_rect.right - compact.front_card_rect.left;
  const int stacked_width = stacked.front_card_rect.right - stacked.front_card_rect.left;
  CLIPD2D_CHECK(compact_width > stacked_width);
}

CLIPD2D_TEST(ClipVisualFileContentSingleFileIconUsesFixedFortyPixelTargetWhenSpaceAllows) {
  CLIPD2D_CHECK_EQ(clipd2d::ui::file_content_preview::ResolveSingleFileIconRenderExtent(48), 40);
}

CLIPD2D_TEST(ClipVisualAssetsResolveFileIconDecodeExtentUsesLargeSourceIcons) {
  CLIPD2D_CHECK_EQ(clipd2d::ui::visual_assets::ResolveFileIconDecodeExtent(20), 64);
  CLIPD2D_CHECK_EQ(clipd2d::ui::visual_assets::ResolveFileIconDecodeExtent(32), 96);
  CLIPD2D_CHECK_EQ(clipd2d::ui::visual_assets::ResolveFileIconDecodeExtent(60), 128);
}

CLIPD2D_TEST(ClipVisualAssetsSourceAppIconDecodeExtentScalesByPurposeAndDpi) {
  using clipd2d::ui::visual_assets::ResolveShellIconDecodeExtent;
  using clipd2d::ui::visual_assets::ShellIconPurpose;

  CLIPD2D_CHECK_EQ(ResolveShellIconDecodeExtent(ShellIconPurpose::SourceAppBadge, 14, 96), 28);
  CLIPD2D_CHECK_EQ(ResolveShellIconDecodeExtent(ShellIconPurpose::SourceAppBadge, 18, 192), 48);
}

CLIPD2D_TEST(ClipVisualAssetsShellIconCacheKeySeparatesPurposeAndExtent) {
  const auto source_request = clipd2d::ui::visual_assets::BuildSourceAppIconRequest(
      L"C:\\Temp\\Demo.exe",
      18,
      96);
  const auto file_request = clipd2d::ui::visual_assets::BuildFileIconRequest(
      L"C:\\Temp\\Demo.exe",
      18,
      96);
  const auto larger_file_request = clipd2d::ui::visual_assets::BuildFileIconRequest(
      L"C:\\Temp\\Demo.exe",
      32,
      96);

  const std::wstring source_key = clipd2d::ui::visual_assets::BuildShellIconCacheKey(source_request);
  const std::wstring file_key = clipd2d::ui::visual_assets::BuildShellIconCacheKey(file_request);
  const std::wstring larger_file_key = clipd2d::ui::visual_assets::BuildShellIconCacheKey(larger_file_request);

  CLIPD2D_CHECK(!source_key.empty());
  CLIPD2D_CHECK(!file_key.empty());
  CLIPD2D_CHECK(source_key != file_key);
  CLIPD2D_CHECK(file_key != larger_file_key);
}
