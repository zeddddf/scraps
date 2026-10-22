#include "ui/ClipVisualFileContent.h"

#include <algorithm>
#include <cmath>

#include <wrl/client.h>

#include "ui/D2dUtil.h"
#include "ui/FluentIcons.h"

namespace clipd2d::ui::file_content_preview {

namespace {

constexpr int kVerticalInset = 2;
constexpr int kStackOffsetX = 4;
constexpr int kStackOffsetY = 2;
constexpr int kTextGap = 12;
constexpr int kTextMinWidth = 110;
constexpr int kPrimaryTextHeight = 18;
constexpr int kSecondaryTextHeight = 18;
constexpr int kTextLineGap = 3;
constexpr int kBadgeWidth = 28;
constexpr int kBadgeHeight = 18;
constexpr int kBadgeOffset = 3;
constexpr int kSingleIconGap = 10;

void DrawTextLine(
    ID2D1RenderTarget* target,
    IDWriteTextFormat* format,
    ID2D1Brush* brush,
    const RECT& rect,
    std::wstring_view text) {
  if (target == nullptr || format == nullptr || brush == nullptr || text.empty()) {
    return;
  }
  target->DrawTextW(
      text.data(),
      static_cast<UINT32>(text.size()),
      format,
      ToRectF(rect),
      brush,
      D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void FillRoundedPanel(
    ID2D1RenderTarget* target,
    const RECT& rect,
    float radius,
    ID2D1Brush* fill_brush,
    ID2D1Brush* stroke_brush,
    float stroke_width) {
  const auto rounded = D2D1::RoundedRect(ToRectF(rect), radius, radius);
  if (fill_brush != nullptr) {
    target->FillRoundedRectangle(rounded, fill_brush);
  }
  if (stroke_brush != nullptr && stroke_width > 0.0f) {
    target->DrawRoundedRectangle(rounded, stroke_brush, stroke_width);
  }
}

void DrawCenteredText(
    ID2D1RenderTarget* target,
    IDWriteFactory* factory,
    const std::wstring& text,
    IDWriteTextFormat* format,
    ID2D1Brush* brush,
    const RECT& rect) {
  if (target == nullptr || factory == nullptr || format == nullptr || brush == nullptr || text.empty()) {
    return;
  }
  Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
  const auto size = D2D1::SizeF(
      static_cast<float>(std::max<LONG>(1, rect.right - rect.left)),
      static_cast<float>(std::max<LONG>(1, rect.bottom - rect.top)));
  if (FAILED(factory->CreateTextLayout(
          text.c_str(),
          static_cast<UINT32>(text.size()),
          format,
          size.width,
          size.height,
          layout.ReleaseAndGetAddressOf())) ||
      layout == nullptr) {
    return;
  }
  layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
  layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
  target->DrawTextLayout(
      D2D1::Point2F(static_cast<float>(rect.left), static_cast<float>(rect.top)),
      layout.Get(),
      brush,
      D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

D2D1_RECT_F ComputeContainedBitmapRect(UINT bitmap_width, UINT bitmap_height, const RECT& rect) {
  if (bitmap_width == 0 || bitmap_height == 0) {
    return D2D1::RectF();
  }
  const float available_width = static_cast<float>(std::max<LONG>(1, rect.right - rect.left));
  const float available_height = static_cast<float>(std::max<LONG>(1, rect.bottom - rect.top));
  const float scale = std::min(
      1.0f,
      std::min(
          available_width / static_cast<float>(bitmap_width),
          available_height / static_cast<float>(bitmap_height)));
  const float draw_width = static_cast<float>(bitmap_width) * scale;
  const float draw_height = static_cast<float>(bitmap_height) * scale;
  const float left = static_cast<float>(rect.left) + (available_width - draw_width) * 0.5f;
  const float top = static_cast<float>(rect.top) + (available_height - draw_height) * 0.5f;
  return D2D1::RectF(left, top, left + draw_width, top + draw_height);
}

int StackLayerCount(int file_count) {
  if (file_count <= 1) {
    return 1;
  }
  return std::min(file_count, 3);
}

}  // namespace

int ResolveIconOnlyRenderExtent(int content_height) {
  return std::min(32, std::max(20, content_height - 6));
}

int ResolveSingleFileIconRenderExtent(int content_height) {
  return std::min(40, std::max(24, content_height - 8));
}

Layout ComputeLayout(const RECT& content_rect, UINT bitmap_width, UINT bitmap_height, int file_count, FileVisualMode mode) {
  Layout layout;
  layout.mode = mode;

  const int content_height = std::max(1, static_cast<int>(content_rect.bottom - content_rect.top));
  const int content_width = std::max(1, static_cast<int>(content_rect.right - content_rect.left));
  int card_height_nominal = 0;
  int card_width_nominal = 0;
  if (mode == FileVisualMode::ThumbnailCard) {
    card_height_nominal = std::max(3, content_height - kVerticalInset * 2);
    card_height_nominal = (card_height_nominal / 3) * 3;
    card_width_nominal = std::max(4, (card_height_nominal / 3) * 4);
  } else if (mode == FileVisualMode::SingleFileIcon) {
    card_height_nominal = ResolveSingleFileIconRenderExtent(content_height);
    card_width_nominal = card_height_nominal;
  } else {
    card_height_nominal = ResolveIconOnlyRenderExtent(content_height);
    card_width_nominal = card_height_nominal;
  }

  const int layer_count = mode == FileVisualMode::SingleFileIcon ? 1 : StackLayerCount(file_count);
  const int stack_width = card_width_nominal + (layer_count - 1) * kStackOffsetX;
  const int min_required_width = stack_width + kTextGap + kTextMinWidth;
  int card_height = card_height_nominal;
  int card_width = card_width_nominal;
  if (min_required_width > content_width) {
    const int max_thumb_block_width = std::max(40, content_width - kTextGap - kTextMinWidth);
    const int adjusted_card_width = std::max(
        mode == FileVisualMode::ThumbnailCard ? 32 : (mode == FileVisualMode::SingleFileIcon ? 16 : 16),
        max_thumb_block_width - (layer_count - 1) * kStackOffsetX);
    if (mode == FileVisualMode::ThumbnailCard) {
      card_width = std::max(4, (adjusted_card_width / 4) * 4);
      card_height = std::max(3, (card_width / 4) * 3);
    } else {
      card_width = adjusted_card_width;
      card_height = adjusted_card_width;
    }
  }

  const int block_width = card_width + (layer_count - 1) * kStackOffsetX;
  const int block_height = card_height + (layer_count - 1) * kStackOffsetY;
  const int block_top = content_rect.top + std::max(0, (content_height - block_height) / 2);
  layout.thumbnail_block_rect = RECT{
      content_rect.left,
      block_top,
      content_rect.left + block_width,
      block_top + block_height};

  layout.front_card_rect = RECT{
      layout.thumbnail_block_rect.right - card_width,
      layout.thumbnail_block_rect.bottom - card_height,
      layout.thumbnail_block_rect.right,
      layout.thumbnail_block_rect.bottom};
  layout.backmost_card_rect = RECT{
      layout.front_card_rect.left - (layer_count - 1) * kStackOffsetX,
      layout.front_card_rect.top - (layer_count - 1) * kStackOffsetY,
      layout.front_card_rect.right - (layer_count - 1) * kStackOffsetX,
      layout.front_card_rect.bottom - (layer_count - 1) * kStackOffsetY};

  const int text_left = layout.thumbnail_block_rect.right + (mode == FileVisualMode::SingleFileIcon ? kSingleIconGap : kTextGap);
  const int text_right = content_rect.right;
  const int text_block_height = kPrimaryTextHeight + kTextLineGap + kSecondaryTextHeight;
  const int text_top = content_rect.top + std::max(0, (content_height - text_block_height) / 2);
  layout.primary_text_rect = RECT{text_left, text_top, text_right, text_top + kPrimaryTextHeight};
  layout.secondary_text_rect = RECT{
      text_left,
      layout.primary_text_rect.bottom + kTextLineGap,
      text_right,
      layout.primary_text_rect.bottom + kTextLineGap + kSecondaryTextHeight};

  if (file_count > 1 && mode != FileVisualMode::SingleFileIcon) {
    layout.show_badge = true;
    layout.badge_rect = RECT{
        layout.front_card_rect.right - kBadgeWidth + kBadgeOffset,
        layout.front_card_rect.bottom - kBadgeHeight + kBadgeOffset,
        layout.front_card_rect.right + kBadgeOffset,
        layout.front_card_rect.bottom + kBadgeOffset};
  }

  layout.bitmap_rect = ComputeContainedBitmapRect(bitmap_width, bitmap_height, layout.front_card_rect);
  return layout;
}

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
    FileVisualMode mode) {
  if (target == nullptr) {
    return;
  }

  const Layout layout = ComputeLayout(content_rect, bitmap_width, bitmap_height, file_count, mode);
  const int layer_count = StackLayerCount(file_count);
  for (int layer = (mode == FileVisualMode::SingleFileIcon ? 1 : layer_count) - 1; layer >= 1; --layer) {
    const RECT layer_rect{
        layout.front_card_rect.left - layer * kStackOffsetX,
        layout.front_card_rect.top - layer * kStackOffsetY,
        layout.front_card_rect.right - layer * kStackOffsetX,
        layout.front_card_rect.bottom - layer * kStackOffsetY};
    if (mode == FileVisualMode::ThumbnailCard) {
      FillRoundedPanel(
          target,
          layer_rect,
          9.0f,
          layer == 1 ? shell.surface_alt_brush() : shell.background_brush(),
          shell.border_brush(),
          1.0f);
      if (bitmap != nullptr && layer == 1) {
        const auto back_bitmap_rect = ComputeContainedBitmapRect(bitmap_width, bitmap_height, layer_rect);
        target->DrawBitmap(bitmap, back_bitmap_rect, 0.28f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
      }
    } else {
      FillRoundedPanel(
          target,
          layer_rect,
          7.0f,
          layer == 1 ? shell.surface_hover_brush() : shell.surface_alt_brush(),
          nullptr,
          0.0f);
      if (bitmap != nullptr) {
        const auto back_bitmap_rect = ComputeContainedBitmapRect(bitmap_width, bitmap_height, layer_rect);
        target->DrawBitmap(bitmap, back_bitmap_rect, layer == 1 ? 0.36f : 0.22f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
      }
    }
  }

  if (mode == FileVisualMode::SingleFileIcon) {
    if (bitmap != nullptr && bitmap_width > 0 && bitmap_height > 0) {
      target->DrawBitmap(bitmap, layout.bitmap_rect, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }
  } else if (bitmap != nullptr && bitmap_width > 0 && bitmap_height > 0) {
    if (mode == FileVisualMode::ThumbnailCard) {
      FillRoundedPanel(target, layout.front_card_rect, 9.0f, shell.surface_brush(), shell.border_brush(), 1.0f);
    }
    target->DrawBitmap(bitmap, layout.bitmap_rect, 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
  } else {
    if (mode == FileVisualMode::ThumbnailCard) {
      FillRoundedPanel(target, layout.front_card_rect, 9.0f, shell.surface_brush(), shell.border_brush(), 1.0f);
      const float card_width = static_cast<float>(layout.front_card_rect.right - layout.front_card_rect.left);
      const float card_height = static_cast<float>(layout.front_card_rect.bottom - layout.front_card_rect.top);
      const float icon_size = std::min(card_width, card_height) * 0.4f;
      const float center_x = static_cast<float>(layout.front_card_rect.left) + card_width * 0.5f;
      const float center_y = static_cast<float>(layout.front_card_rect.top) + card_height * 0.5f;
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
    } else {
      const float card_width = static_cast<float>(layout.front_card_rect.right - layout.front_card_rect.left);
      const float card_height = static_cast<float>(layout.front_card_rect.bottom - layout.front_card_rect.top);
      const float icon_size = std::min(card_width, card_height) * 0.75f;
      const float center_x = static_cast<float>(layout.front_card_rect.left) + card_width * 0.5f;
      const float center_y = static_cast<float>(layout.front_card_rect.top) + card_height * 0.5f;
      const D2D1_RECT_F icon_rect = D2D1::RectF(
          center_x - icon_size * 0.5f,
          center_y - icon_size * 0.5f,
          center_x + icon_size * 0.5f,
          center_y + icon_size * 0.5f);

      DrawFluentIcon(
          shell.d2d_factory(),
          target,
          file_count > 1 ? FluentIcon::Folder : FluentIcon::Clipboard,
          icon_rect,
          shell.subtle_text_brush(),
          0.0f);
    }
  }

  if (layout.show_badge) {
    FillRoundedPanel(target, layout.badge_rect, 8.0f, shell.accent_soft_brush(), shell.border_brush(), 1.0f);
    DrawCenteredText(target, shell.dwrite_factory(), std::to_wstring(file_count), shell.badge_format(), shell.accent_brush(), layout.badge_rect);
  }

  DrawTextLine(target, shell.body_bold_format(), shell.text_brush(), layout.primary_text_rect, primary_text);
  DrawTextLine(target, shell.small_format(), shell.muted_text_brush(), layout.secondary_text_rect, secondary_text);
}

}  // namespace clipd2d::ui::file_content_preview
