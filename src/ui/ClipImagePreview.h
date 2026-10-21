#pragma once

#include <windows.h>

#include <d2d1.h>

#include "ui/D2dShell.h"

namespace clipd2d::ui::image_preview {

struct Layout {
  D2D1_RECT_F draw_rect = D2D1::RectF();
  bool has_bitmap = false;
};

Layout ComputeLayout(UINT bitmap_width, UINT bitmap_height, const RECT& preview_rect);

void Draw(
    ID2D1RenderTarget* target,
    D2dShell& shell,
    ID2D1Bitmap* bitmap,
    UINT bitmap_width,
    UINT bitmap_height,
    const RECT& preview_rect);

}  // namespace clipd2d::ui::image_preview
