#include "ui/ClipImagePreview.h"

#include <algorithm>

#include "ui/D2dUtil.h"
#include "ui/FluentIcons.h"

namespace clipd2d::ui::image_preview {

Layout ComputeLayout(UINT bitmap_width, UINT bitmap_height, const RECT& preview_rect) {
  Layout layout;
  if (bitmap_width == 0 || bitmap_height == 0) {
    return layout;
  }

  const float available_width = static_cast<float>(std::max<LONG>(1, preview_rect.right - preview_rect.left));
  const float available_height = static_cast<float>(std::max<LONG>(1, preview_rect.bottom - preview_rect.top));
  const float scale = std::min(
      1.0f,
      std::min(
          available_width / static_cast<float>(bitmap_width),
          available_height / static_cast<float>(bitmap_height)));
  const float draw_width = static_cast<float>(bitmap_width) * scale;
  const float draw_height = static_cast<float>(bitmap_height) * scale;

  layout.has_bitmap = true;
  layout.draw_rect = D2D1::RectF(
      static_cast<float>(preview_rect.left),
      static_cast<float>(preview_rect.top),
      static_cast<float>(preview_rect.left) + draw_width,
      static_cast<float>(preview_rect.top) + draw_height);
  return layout;
}

void Draw(
    ID2D1RenderTarget* target,
    D2dShell& shell,
    ID2D1Bitmap* bitmap,
    UINT bitmap_width,
    UINT bitmap_height,
    const RECT& preview_rect) {
  if (target == nullptr) {
    return;
  }

  const Layout layout = ComputeLayout(bitmap_width, bitmap_height, preview_rect);
  if (bitmap != nullptr && layout.has_bitmap) {
    target->DrawBitmap(
        bitmap,
        layout.draw_rect,
        1.0f,
        D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    return;
  }

  const float available_height = static_cast<float>(std::max<LONG>(1, preview_rect.bottom - preview_rect.top));
  const float icon_size = available_height * 0.5f;
  const float center_x = static_cast<float>(preview_rect.left) + available_height * 0.5f;
  const float center_y = static_cast<float>(preview_rect.top) + available_height * 0.5f;
  const D2D1_RECT_F icon_rect = D2D1::RectF(
      center_x - icon_size * 0.5f,
      center_y - icon_size * 0.5f,
      center_x + icon_size * 0.5f,
      center_y + icon_size * 0.5f);

  DrawFluentIcon(
      shell.d2d_factory(),
      target,
      FluentIcon::Image,
      icon_rect,
      shell.subtle_text_brush(),
      0.0f);
}

}  // namespace clipd2d::ui::image_preview
