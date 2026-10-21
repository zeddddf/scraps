#pragma once

#include <string_view>

#include <windows.h>

#include <d2d1.h>

#include "ui/D2dShell.h"

namespace clipd2d::ui::file_content_preview {

enum class FileVisualMode {
  ThumbnailCard = 0,
  IconOnly,
  SingleFileIcon,
};

int ResolveIconOnlyRenderExtent(int content_height);
int ResolveSingleFileIconRenderExtent(int content_height);

struct Layout {
  FileVisualMode mode = FileVisualMode::ThumbnailCard;
  RECT thumbnail_block_rect{};
  RECT front_card_rect{};
  RECT backmost_card_rect{};
  D2D1_RECT_F bitmap_rect = D2D1::RectF();
  RECT primary_text_rect{};
  RECT secondary_text_rect{};
  RECT badge_rect{};
  bool show_badge = false;
};

Layout ComputeLayout(
    const RECT& content_rect,
    UINT bitmap_width,
    UINT bitmap_height,
    int file_count,
    FileVisualMode mode);

void Draw(
    ID2D1RenderTarget* target,
    D2dShell& shell,
    ID2D1Bitmap* bitmap,
    UINT bitmap_width,
    UINT bitmap_height,
    const RECT& content_rect,
    std::wstring_view primary_text,
    std::wstring_view secondary_text,
    int file_count,
    FileVisualMode mode);

}  // namespace clipd2d::ui::file_content_preview
