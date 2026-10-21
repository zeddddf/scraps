#include "tests/TestSupport.h"

#include "ui/ClipImagePreview.h"

CLIPD2D_TEST(ClipImagePreviewLayoutUsesTopLeftContainForLandscapeImage) {
  const RECT preview_rect{10, 20, 110, 80};
  const auto layout = clipd2d::ui::image_preview::ComputeLayout(200, 100, preview_rect);
  CLIPD2D_CHECK(layout.has_bitmap);
  CLIPD2D_CHECK_EQ(layout.draw_rect.left, 10.0f);
  CLIPD2D_CHECK_EQ(layout.draw_rect.top, 20.0f);
  CLIPD2D_CHECK_EQ(layout.draw_rect.right, 110.0f);
  CLIPD2D_CHECK_EQ(layout.draw_rect.bottom, 70.0f);
}

CLIPD2D_TEST(ClipImagePreviewLayoutUsesTopLeftContainForPortraitImage) {
  const RECT preview_rect{0, 0, 100, 60};
  const auto layout = clipd2d::ui::image_preview::ComputeLayout(60, 120, preview_rect);
  CLIPD2D_CHECK(layout.has_bitmap);
  CLIPD2D_CHECK_EQ(layout.draw_rect.left, 0.0f);
  CLIPD2D_CHECK_EQ(layout.draw_rect.top, 0.0f);
  CLIPD2D_CHECK_EQ(layout.draw_rect.right, 30.0f);
  CLIPD2D_CHECK_EQ(layout.draw_rect.bottom, 60.0f);
}
