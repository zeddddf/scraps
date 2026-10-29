#include "ui/MainWindowInternal.h"
#include "ui/ClipImagePreview.h"
#include "ui/ClipVisualFileContent.h"
#include "ui/PreviewContentModel.h"
#include "ui/RowRenderer.h"
#include <d2d1effects.h>
#include "core/commands/ISettingsStore.h"

namespace clipd2d::ui {

namespace {

const std::wstring& GetCachedNumberString(int n) {
  static std::vector<std::wstring> cache;
  if (cache.empty()) {
    cache.reserve(100);
    for (int i = 0; i < 100; ++i) {
      cache.push_back(std::to_wstring(i));
    }
  }
  if (n >= 0 && n < 100) {
    return cache[n];
  }
  static std::wstring fallback;
  fallback = std::to_wstring(n);
  return fallback;
}

const std::wstring& GetCachedPlusNumberString(int n) {
  static std::vector<std::wstring> cache;
  if (cache.empty()) {
    cache.reserve(100);
    for (int i = 0; i < 100; ++i) {
      cache.push_back(L"+" + std::to_wstring(i));
    }
  }
  if (n >= 0 && n < 100) {
    return cache[n];
  }
  static std::wstring fallback;
  fallback = L"+" + std::to_wstring(n);
  return fallback;
}


RECT OffsetRectCopy(const RECT& rect, int dx, int dy) {
  RECT offset = rect;
  ::OffsetRect(&offset, dx, dy);
  return offset;
}

int MultiFileStackLayerCount(int file_count) {
  if (file_count <= 1) {
    return 1;
  }
  return std::min(file_count, 3);
}

void DrawMultiFileStackBackdrop(
    ID2D1RenderTarget* target,
    D2dShell& shell,
    const RECT& front_rect,
    int file_count,
    bool emphasized) {
  if (target == nullptr || file_count <= 1) {
    return;
  }

  const int layer_count = MultiFileStackLayerCount(file_count);
  const bool compact = (front_rect.right - front_rect.left) <= 34;
  const int step_x = compact ? 4 : 6;
  const int step_y = compact ? 2 : 3;
  const float radius = compact ? 7.0f : 9.0f;

  for (int layer = layer_count - 1; layer >= 1; --layer) {
    RECT layer_rect = OffsetRectCopy(front_rect, step_x * layer, step_y * layer);
    layer_rect.right += compact ? 0 : 1;
    const D2D1_ROUNDED_RECT rounded = D2D1::RoundedRect(ToRectF(layer_rect), radius, radius);
    ID2D1Brush* fill_brush =
        layer == 1
            ? (emphasized ? shell.surface_hover_brush() : shell.surface_alt_brush())
            : shell.background_brush();
    target->FillRoundedRectangle(rounded, fill_brush);
    target->DrawRoundedRectangle(rounded, shell.border_brush(), 1.0f);
  }
}

void DrawMultiFileSecondaryImageLayer(
    ID2D1RenderTarget* target,
    D2dShell& shell,
    const RECT& front_rect,
    ID2D1Bitmap* bitmap,
    UINT bitmap_width,
    UINT bitmap_height) {
  if (target == nullptr || bitmap == nullptr || bitmap_width == 0 || bitmap_height == 0) {
    return;
  }

  const bool compact = (front_rect.right - front_rect.left) <= 34;
  RECT layer_rect = OffsetRectCopy(front_rect, compact ? 4 : 6, compact ? 2 : 3);
  const float radius = compact ? 7.0f : 9.0f;
  const D2D1_ROUNDED_RECT rounded = D2D1::RoundedRect(ToRectF(layer_rect), radius, radius);
  target->FillRoundedRectangle(rounded, shell.surface_alt_brush());

  const float available_width = static_cast<float>(std::max(1L, layer_rect.right - layer_rect.left));
  const float available_height = static_cast<float>(std::max(1L, layer_rect.bottom - layer_rect.top));
  const float scale = std::min(1.0f, std::min(available_width / static_cast<float>(bitmap_width), available_height / static_cast<float>(bitmap_height)));
  const float draw_width = static_cast<float>(bitmap_width) * scale;
  const float draw_height = static_cast<float>(bitmap_height) * scale;
  const float left = static_cast<float>(layer_rect.left) + (available_width - draw_width) * 0.5f;
  const float top = static_cast<float>(layer_rect.top) + (available_height - draw_height) * 0.5f;
  target->DrawBitmap(
      bitmap,
      D2D1::RectF(left, top, left + draw_width, top + draw_height),
      0.28f,
      D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
  target->DrawRoundedRectangle(rounded, shell.border_brush(), 1.0f);
}

void DrawMultiFileSecondaryIconLayer(
    ID2D1RenderTarget* target,
    D2dShell& shell,
    const RECT& front_rect,
    ID2D1Bitmap* bitmap) {
  if (target == nullptr || bitmap == nullptr) {
    return;
  }

  const bool compact = (front_rect.right - front_rect.left) <= 34;
  RECT layer_rect = OffsetRectCopy(front_rect, compact ? 4 : 6, compact ? 2 : 3);
  const float radius = compact ? 7.0f : 9.0f;
  const D2D1_ROUNDED_RECT rounded = D2D1::RoundedRect(ToRectF(layer_rect), radius, radius);
  target->FillRoundedRectangle(rounded, shell.surface_alt_brush());
  target->DrawBitmap(bitmap, ToRectF(layer_rect), 0.32f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
  target->DrawRoundedRectangle(rounded, shell.border_brush(), 1.0f);
}

void DrawMultiFileCountBadge(
    TextLayoutCache* cache,
    ID2D1RenderTarget* target,
    D2dShell& shell,
    const RECT& front_rect,
    int file_count) {
  if (cache == nullptr || target == nullptr || file_count <= 1) {
    return;
  }

  const std::wstring badge_text = file_count > 99 ? L"99+" : GetCachedNumberString(file_count);
  const bool compact = (front_rect.right - front_rect.left) <= 34;
  const int badge_width = compact ? 24 : 28;
  const int badge_height = compact ? 17 : 19;
  const RECT badge_rect{
      front_rect.right - badge_width + 4,
      front_rect.bottom - badge_height + 4,
      front_rect.right + 4,
      front_rect.bottom + 4};
  const D2D1_ROUNDED_RECT rounded = D2D1::RoundedRect(ToRectF(badge_rect), 8.0f, 8.0f);
  target->FillRoundedRectangle(rounded, shell.accent_soft_brush());
  target->DrawRoundedRectangle(rounded, shell.border_brush(), 1.0f);
  DrawCachedCenteredText(cache, shell.dwrite_factory(), target, badge_text, shell.tiny_format(), shell.accent_brush(), badge_rect);
}

}  // namespace



void MainWindow::RenderListRow(
    ID2D1RenderTarget* target,
    const RECT& client,
    const RECT&,
    const RECT& row,
    int index,
    POINT hover,
    const core::store::AppSettings& render_settings,
    bool collect_hits) {
  const auto* summary = GetSummaryAt(index);
  const bool selected = index == state_.selected_index_;
  const bool hovered = RectContains(row, hover);

  // 行底/列表底用 surface（随主题），白刷只用于强调件上的文字/图标。
  target->FillRectangle(ToRectF(row), d2d_shell_.surface_brush());
  if (selected) {
    target->FillRectangle(ToRectF(row), d2d_shell_.surface_selected_brush());
  } else if (hovered) {
    target->FillRectangle(ToRectF(row), d2d_shell_.surface_hover_brush());
  }
  if (collect_hits) {
    hit_regions_.push_back(HitRegion{row, HitKind::Item, index});
  }
  target->DrawLine(
      D2D1::Point2F(0.0f, static_cast<float>(row.bottom - 1)),
      D2D1::Point2F(static_cast<float>(client.right), static_cast<float>(row.bottom - 1)),
      d2d_shell_.border_brush(),
      kSeparatorStrokeWidth);
  if (summary == nullptr) {
    const float time_ms = static_cast<float>(GetTickCount64());
    const float phase = time_ms * 0.003f - static_cast<float>(row.top) * 0.01f;
    const float base_alpha = 0.04f;
    const float wave_alpha = 0.08f * (0.5f + 0.5f * std::sin(phase));
    const float alpha = base_alpha + wave_alpha;

    auto brush = d2d_shell_.muted_text_brush();
    const float old_opacity = brush->GetOpacity();
    brush->SetOpacity(alpha);

    const float h_center = row.top + 2.0f + kRowHeaderHeight * 0.5f;
    const float row_left = static_cast<float>(row.left);
    const float row_right = static_cast<float>(row.right);
    const float padding_x = static_cast<float>(kRowPaddingX);
    
    target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(row_left + padding_x, h_center - 8.0f, row_left + padding_x + 16.0f, h_center + 8.0f), 4.0f, 4.0f), brush);
    target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(row_left + padding_x + 24.0f, h_center - 5.0f, row_left + padding_x + 80.0f, h_center + 5.0f), 3.0f, 3.0f), brush);
    target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(row_right - padding_x - 40.0f, h_center - 5.0f, row_right - padding_x, h_center + 5.0f), 3.0f, 3.0f), brush);

    const float content_top = row.top + 2.0f + kRowHeaderHeight + 8.0f;
    const float content_width = (row_right - row_left) - 2.0f * padding_x;
    
    target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(row_left + padding_x, content_top, row_left + padding_x + content_width * 0.85f, content_top + 12.0f), 4.0f, 4.0f), brush);
    target->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(row_left + padding_x, content_top + 20.0f, row_left + padding_x + content_width * 0.6f, content_top + 32.0f), 4.0f, 4.0f), brush);
    
    brush->SetOpacity(old_opacity);
    
    if (!skeleton_animating_) {
      skeleton_animating_ = true;
    }
    // L6:骨架 shimmer 需要持续帧——统一走 EnsureAnimationTimer(has_pending_work
    // 判断),替代此处的裸 SetTimer(旧写法漏置 animation_timer_active_,且定时器
    // 会在下一拍被过早杀死导致 shimmer 冻结)。
    EnsureAnimationTimer();
    return;
  }

  const auto& search_tokens = state_.display_query_tokens_;

  const RECT header_rect{
      row.left + kRowPaddingX,
      row.top + 2,
      row.right - kRowPaddingX,
      std::min(row.bottom - 2, row.top + 2 + kRowHeaderHeight)};
  RECT content_rect{
      row.left + kRowPaddingX,
      header_rect.bottom,
      row.right - kRowPaddingX,
      row.bottom - 4};

  const bool secondary_text_formats_ready = d2d_shell_.secondary_text_formats_ready();
  RECT number_rect{header_rect.right - 22, header_rect.top + 4, header_rect.right, header_rect.bottom - 4};
  if (secondary_text_formats_ready) {
    DrawCachedCenteredText(&text_layout_cache_, d2d_shell_.dwrite_factory(), target, GetCachedNumberString(index + 1), d2d_shell_.badge_format(), d2d_shell_.subtle_text_brush(), number_rect);
  }

  const bool show_row_actions = hovered || selected;
  const bool show_sticker_action = ShouldShowThirdPartyStickerButton(*summary);
  RECT favorite_rect{};
  RECT sticker_rect{};
  RECT pinned_rect{};
  RECT delete_rect{};
  RECT source_rect{};
  int header_text_right = number_rect.left - 6;
  if (render_settings.show_source_icon && !summary->source_app_path.empty()) {
    source_rect = RECT{
        number_rect.left - (kRowHeaderIconSize + 6),
        header_rect.top + (kRowHeaderHeight - kRowHeaderIconSize) / 2,
        number_rect.left - 6,
        header_rect.top + (kRowHeaderHeight - kRowHeaderIconSize) / 2 + kRowHeaderIconSize};
    header_text_right = source_rect.left - 6;

    const auto source_request = BuildSourceIconRequest(*summary);
    const std::wstring source_key = visual_assets::BuildShellIconCacheKey(source_request);
    const auto it = state_.thumbnail_bitmaps_.find(source_key);
    if (it != state_.thumbnail_bitmaps_.end() && it->second.bitmap != nullptr) {
      TouchBitmap(&it->second);
      target->DrawBitmap(it->second.bitmap.Get(), ToRectF(source_rect), 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    } else {
      history_asset_loader_.RequestSourceIconAsync(*summary);
    }
  }
  if (show_row_actions) {
    int action_right = header_text_right;
    delete_rect = RECT{action_right - kRowActionButtonSize, header_rect.top + (kRowHeaderHeight - kRowActionButtonSize) / 2, action_right, header_rect.top + (kRowHeaderHeight - kRowActionButtonSize) / 2 + kRowActionButtonSize};
    action_right = delete_rect.left - kRowActionGap;
    pinned_rect = RECT{action_right - kRowActionButtonSize, header_rect.top + (kRowHeaderHeight - kRowActionButtonSize) / 2, action_right, header_rect.top + (kRowHeaderHeight - kRowActionButtonSize) / 2 + kRowActionButtonSize};
    action_right = pinned_rect.left - kRowActionGap;
    favorite_rect = RECT{action_right - kRowActionButtonSize, header_rect.top + (kRowHeaderHeight - kRowActionButtonSize) / 2, action_right, header_rect.top + (kRowHeaderHeight - kRowActionButtonSize) / 2 + kRowActionButtonSize};
    header_text_right = favorite_rect.left - 6;
    if (show_sticker_action) {
      action_right = favorite_rect.left - kRowActionGap;
      sticker_rect = RECT{action_right - kRowActionButtonSize, header_rect.top + (kRowHeaderHeight - kRowActionButtonSize) / 2, action_right, header_rect.top + (kRowHeaderHeight - kRowActionButtonSize) / 2 + kRowActionButtonSize};
      header_text_right = sticker_rect.left - 6;
    }
  }

  const int header_text_left = header_rect.left;

  const std::wstring time_text = FormatMetaLine(*summary);
  RECT time_rect{header_text_left, header_rect.top + 4, std::max(header_text_left, header_text_right), header_rect.bottom - 4};
  if (!search_tokens.empty()) {
    DrawHighlightedText(
        d2d_shell_.dwrite_factory(),
        target,
        time_text,
        d2d_shell_.meta_format(),
        d2d_shell_.muted_text_brush(),
        d2d_shell_.accent_soft_brush(),
        time_rect,
        true,
        search_tokens);
  } else {
    DrawCachedText(
        &text_layout_cache_,
        d2d_shell_.dwrite_factory(),
        target,
        *summary,
        CachedTextRole::Meta,
        time_text,
        d2d_shell_.meta_format(),
        d2d_shell_.muted_text_brush(),
        time_rect,
        true);
  }

  std::vector<size_t> visible_tag_indexes;
  const size_t visible_tag_limit = 2;
  const size_t hidden_tag_count = summary->tag_names.size() > visible_tag_limit ? (summary->tag_names.size() - visible_tag_limit) : 0;
  for (size_t tag_index = 0; tag_index < summary->tag_names.size() && tag_index < visible_tag_limit; ++tag_index) {
    visible_tag_indexes.push_back(tag_index);
  }
  if (secondary_text_formats_ready && !visible_tag_indexes.empty()) {
    content_rect.top += kTagChipHeight + 4;
    int chip_left = content_rect.left;
    for (size_t visible_index = 0; visible_index < visible_tag_indexes.size(); ++visible_index) {
      const size_t tag_index = visible_tag_indexes[visible_index];
      const std::wstring& tag_name = summary->tag_names[tag_index];
      const std::wstring& tag_color = summary->tag_colors[tag_index];
      const int chip_width = std::min(96, 22 + static_cast<int>(tag_name.size()) * 8);
      RECT chip_rect{
          chip_left,
          header_rect.bottom + 2,
          chip_left + chip_width,
          header_rect.bottom + 2 + kTagChipHeight};
      // Q2:标签底色/文字色都是动态色,走 D2dShell 色刷缓存,不再每帧创建。
      const auto color = ParseTagColor(tag_color);
      if (ID2D1SolidColorBrush* chip_brush = d2d_shell_.solid_brush(target, color)) {
        target->FillRoundedRectangle(D2D1::RoundedRect(ToRectF(chip_rect), 9.0f, 9.0f), chip_brush);
      }
      const float tag_luminance = 0.2126f * color.r + 0.7152f * color.g + 0.0722f * color.b;
      ID2D1Brush* text_brush = d2d_shell_.solid_brush(
          target,
          tag_luminance < 0.55f ? D2D1::ColorF(1, 1, 1, 1) : D2D1::ColorF(0.11f, 0.14f, 0.18f, 1.0f));
      RECT chip_text_rect = chip_rect;
      chip_text_rect.left += 8;
      chip_text_rect.right -= 8;
      DrawCachedCenteredText(&text_layout_cache_, d2d_shell_.dwrite_factory(), target, tag_name, d2d_shell_.tiny_format(), text_brush, chip_text_rect);
      if (collect_hits && tag_index < summary->tag_ids.size()) {
        hit_regions_.push_back(HitRegion{chip_rect, HitKind::TagChip, index, summary->id, summary->tag_ids[tag_index]});
      }
      chip_left = chip_rect.right + kTagChipGap;
    }
    if (hidden_tag_count > 0) {
      RECT more_rect{
          chip_left,
          header_rect.bottom + 2,
          chip_left + 30,
          header_rect.bottom + 2 + kTagChipHeight};
      target->FillRoundedRectangle(D2D1::RoundedRect(ToRectF(more_rect), 9.0f, 9.0f), d2d_shell_.surface_hover_brush());
      DrawCachedCenteredText(&text_layout_cache_, d2d_shell_.dwrite_factory(), target, GetCachedPlusNumberString(static_cast<int>(hidden_tag_count)), d2d_shell_.tiny_format(), d2d_shell_.text_brush(), more_rect);
    }
  }

  if (show_row_actions) {
    std::vector<RECT> action_rects;
    std::vector<FluentIcon> action_icons;
    std::vector<HitKind> action_kinds;
    if (show_sticker_action) {
      action_rects.push_back(sticker_rect);
      action_icons.push_back(FluentIcon::Clipboard);
      action_kinds.push_back(HitKind::ItemSticker);
    }
    action_rects.push_back(favorite_rect);
    action_icons.push_back(summary->is_favorite ? FluentIcon::StarFilled : FluentIcon::StarOutline);
    action_kinds.push_back(HitKind::ItemFavorite);
    action_rects.push_back(pinned_rect);
    action_icons.push_back(summary->is_pinned ? FluentIcon::PanelPinned : FluentIcon::PanelPin);
    action_kinds.push_back(HitKind::ItemPinned);
    action_rects.push_back(delete_rect);
    action_icons.push_back(FluentIcon::Delete);
    action_kinds.push_back(HitKind::ItemDelete);

    for (size_t action_index = 0; action_index < action_rects.size(); ++action_index) {
      const bool action_hovered = RectContains(action_rects[action_index], hover);
      if (action_hovered) {
        target->FillRoundedRectangle(
            D2D1::RoundedRect(Inset(action_rects[action_index], 0.0f, 0.0f), 8.0f, 8.0f),
            d2d_shell_.surface_hover_brush());
        target->DrawRoundedRectangle(
            D2D1::RoundedRect(Inset(action_rects[action_index], 0.0f, 0.0f), 8.0f, 8.0f),
            d2d_shell_.border_brush(),
            kControlStrokeWidth);
      }
      ID2D1Brush* icon_brush = d2d_shell_.text_brush();
      if (action_kinds[action_index] == HitKind::ItemFavorite && summary->is_favorite) {
        icon_brush = d2d_shell_.accent_brush();
      } else if (action_kinds[action_index] == HitKind::ItemPinned && summary->is_pinned) {
        icon_brush = d2d_shell_.accent_brush();
      } else if (action_kinds[action_index] == HitKind::ItemDelete) {
        icon_brush = d2d_shell_.danger_brush();
      }
      DrawFluentIcon(
          d2d_shell_.d2d_factory(),
          target,
          action_icons[action_index],
          Inset(action_rects[action_index], 2.5f, 2.5f),
          icon_brush);
      if (collect_hits) {
        hit_regions_.push_back(HitRegion{action_rects[action_index], action_kinds[action_index], index});
      }
    }
  }

  // P3-16: 行内容渲染经 IRowRenderer 注册表按类型分发（文本/图片/文件三实现，
  // 见 ui/RowRenderer.h）。行框架（背景/骨架/头部/操作按钮）仍由本函数绘制。
  {
    using clipd2d::ui::RowBitmapKind;
    using clipd2d::ui::RowBitmapRef;
    clipd2d::ui::RowContentContext ctx;
    ctx.target = target;
    ctx.content_rect = content_rect;
    ctx.summary = summary;
    ctx.shell = &d2d_shell_;
    ctx.text_layout_cache = &text_layout_cache_;
    ctx.search_tokens = &search_tokens;
    ctx.resolve_bitmap = [this](RowBitmapKind kind, const core::store::ClipSummary& s) -> RowBitmapRef {
      if (kind == RowBitmapKind::FileItemIcon) {
        const auto req = BuildFileItemIconRequest(s);
        if (!req.has_value()) {
          return {};
        }
        const std::wstring key = visual_assets::BuildShellIconCacheKey(*req);
        const auto it = state_.thumbnail_bitmaps_.find(key);
        if (it != state_.thumbnail_bitmaps_.end() && it->second.bitmap != nullptr) {
          TouchBitmap(&it->second);
          return {it->second.bitmap.Get(), it->second.width, it->second.height};
        }
        RequestFileItemIconAsync(s);
        return {};
      }
      const std::wstring key = ThumbnailKeyFromSummary(s);
      const auto it = state_.thumbnail_bitmaps_.find(key);
      if (it != state_.thumbnail_bitmaps_.end() && it->second.bitmap != nullptr) {
        TouchBitmap(&it->second);
        return {it->second.bitmap.Get(), it->second.width, it->second.height};
      }
      if (!key.empty()) {
        RequestThumbnailAsync(s);
      }
      return {};
    };
    clipd2d::ui::RowContentRendererFor(summary->content_type).Render(ctx);
  }
}

void MainWindow::RenderDirtyRows(
    ID2D1RenderTarget* target,
    const RECT& client,
    const RECT& list_rect,
    POINT hover,
    const core::store::AppSettings& render_settings) {
  if (target == nullptr || state_.dirty_rows_.empty()) {
    return;
  }

  target->PushAxisAlignedClip(ToRectF(list_rect), D2D1_ANTIALIAS_MODE_ALIASED);
  const float visual_scroll_offset = scroll_controller_->VisualScrollOffset();
  for (int index : state_.dirty_rows_) {
    const vlist::RowRect row_pos = vlist::RowRectFor(
        viewport_.heights(), index, static_cast<float>(list_rect.top), visual_scroll_offset);
    RECT row{
        list_rect.left,
        static_cast<LONG>(std::lround(row_pos.top)),
        list_rect.right,
        static_cast<LONG>(std::lround(row_pos.bottom))};
    if (row.bottom <= list_rect.top || row.top >= list_rect.bottom) {
      continue;
    }
    RenderListRow(target, client, list_rect, row, index, hover, render_settings, false);
  }
  target->PopAxisAlignedClip();
}

void MainWindow::RenderEmptyListGuidance(
    ID2D1RenderTarget* target,
    const RECT& list_rect,
    const core::store::AppSettings& render_settings) {
  // C4 空态引导：真空库附热键引导行；搜索/筛选无结果给调正提示。
  RECT rect{list_rect.left + 20, list_rect.top + 80, list_rect.right - 20, list_rect.top + 190};
  const bool has_query = !search_textbox_.GetText().empty();
  const wchar_t* title = has_query ? strings::kEmptySearchTitle : strings::kEmptyListTitle;
  const wchar_t* body = has_query ? strings::kEmptySearchBody : strings::kEmptyListBody;
  target->DrawTextW(
      title, static_cast<UINT32>(wcslen(title)),
      d2d_shell_.section_format(), ToRectF(rect),
      d2d_shell_.text_brush(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
  rect.top += 36;
  target->DrawTextW(
      body, static_cast<UINT32>(wcslen(body)),
      d2d_shell_.small_format(), ToRectF(rect),
      d2d_shell_.muted_text_brush(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
  if (has_query) {
    return;
  }
  struct GuideRow {
    const wchar_t* hotkey;
    const wchar_t* label;
  };
  const GuideRow rows[] = {
      {render_settings.hotkey.c_str(), strings::kEmptyGuidePanel},
      {L"Ctrl+F", strings::kEmptyGuideSearch},
      {render_settings.screenshot_hotkey.c_str(), strings::kEmptyGuideScreenshot},
  };
  rect.top += 46;
  const float row_height_dip = 24.0f;
  const float s = d2d_shell_.dpi_scale();
  const int row_height = static_cast<int>(row_height_dip * s);
  const int key_width = static_cast<int>(150.0f * s);
  for (const GuideRow& row : rows) {
    const RECT key_rect{rect.left, rect.top, rect.left + key_width, rect.top + row_height};
    const RECT label_rect{rect.left + key_width + static_cast<int>(8.0f * s), rect.top, rect.right, rect.top + row_height};
    target->DrawTextW(
        row.hotkey, static_cast<UINT32>(wcslen(row.hotkey)),
        d2d_shell_.small_format(), ToRectF(key_rect),
        d2d_shell_.accent_brush(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    target->DrawTextW(
        row.label, static_cast<UINT32>(wcslen(row.label)),
        d2d_shell_.small_format(), ToRectF(label_rect),
        d2d_shell_.muted_text_brush(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    rect.top += row_height + static_cast<int>(6.0f * s);
  }
}

void MainWindow::RenderLiveResizeListSurface(
    ID2D1RenderTarget* target,
    const RECT& client,
    const RECT& list_rect,
    const core::store::AppSettings& render_settings) {
  if (target == nullptr) {
    return;
  }

  target->PushAxisAlignedClip(ToRectF(list_rect), D2D1_ANTIALIAS_MODE_ALIASED);

  if (state_.item_count_ == 0) {
    RenderEmptyListGuidance(target, list_rect, render_settings);
    target->PopAxisAlignedClip();
    return;
  }

  const float visual_scroll_offset = scroll_controller_->VisualScrollOffset();
  const int viewport_height = std::max(1L, list_rect.bottom - list_rect.top);
  const vlist::VisibleRange live_range =
      viewport_.RangeAt(visual_scroll_offset, viewport_height, 0, 0);
  const int first_index = live_range.first;
  const int last_index = std::min(state_.item_count_ - 1, live_range.last);
  const int first_page = first_index / state_.page_size_;
  const int last_page = last_index / state_.page_size_;

  for (int page = first_page; page <= last_page; ++page) {
    auto page_it = state_.page_cache_.find(page);
    if (page_it == state_.page_cache_.end()) {
      continue;
    }

    TouchPage(page);
    const int start_offset = page == first_page ? (first_index % state_.page_size_) : 0;
    const int end_offset = page == last_page
        ? std::min(last_index % state_.page_size_, static_cast<int>(page_it->second.size()) - 1)
        : static_cast<int>(page_it->second.size()) - 1;

    for (int offset = start_offset; offset <= end_offset; ++offset) {
      const int index = page * state_.page_size_ + offset;
      const vlist::RowRect row_pos = vlist::RowRectFor(
          viewport_.heights(), index, static_cast<float>(list_rect.top), visual_scroll_offset);
      RECT row{
          list_rect.left,
          static_cast<LONG>(std::lround(row_pos.top)),
          list_rect.right,
          static_cast<LONG>(std::lround(row_pos.bottom))};
      if (row.bottom <= list_rect.top || row.top >= list_rect.bottom) {
        continue;
      }
      RenderListRow(target, client, list_rect, row, index, POINT{-1, -1}, render_settings, false);
    }
  }

  target->PopAxisAlignedClip();
}

void MainWindow::RenderListShellIconOverlay(
    ID2D1RenderTarget* target,
    const RECT& list_rect,
    const core::store::AppSettings& render_settings) {
  (void)target;
  (void)list_rect;
  (void)render_settings;
}

void MainWindow::RenderListSurface(
    ID2D1RenderTarget* target,
    const RECT& client,
    const RECT& list_rect,
    POINT hover,
    const core::store::AppSettings& render_settings,
    bool collect_hits) {
  if (target == nullptr) {
    return;
  }
  skeleton_animating_ = false;

  Microsoft::WRL::ComPtr<ID2D1DeviceContext> device_context;
  target->QueryInterface(IID_PPV_ARGS(&device_context));

  float delta_render_s = 0.0166f; // Default to 60Hz
  LARGE_INTEGER counter{};
  if (performance_counter_frequency_.QuadPart > 0 && QueryPerformanceCounter(&counter)) {
    if (last_render_tick_qpc_ != 0) {
      delta_render_s = static_cast<float>(counter.QuadPart - last_render_tick_qpc_) / static_cast<float>(performance_counter_frequency_.QuadPart);
    }
    last_render_tick_qpc_ = counter.QuadPart;
  }
  delta_render_s = std::clamp(delta_render_s, 0.001f, 0.1f);
  const float physical_velocity = scroll_controller_->GetAccumulatedScrollDelta() / delta_render_s;
  scroll_controller_->ClearAccumulatedScrollDelta();

  float max_blur_limit = 1.0f;
  if (d2d_shell_.background_brush() != nullptr) {
    const D2D1_COLOR_F bg_color = d2d_shell_.background_brush()->GetColor();
    const float background_luminance = 0.2126f * bg_color.r + 0.7152f * bg_color.g + 0.0722f * bg_color.b;
    max_blur_limit = (background_luminance < 0.5f) ? 0.8f : 1.2f;
  }

  const bool speed_triggered = render_settings.enable_scroll_blur && !drag_scrollbar_ && std::fabs(physical_velocity) > 3000.0f;
  const float target_blur_strength = speed_triggered ? std::clamp((std::fabs(physical_velocity) - 3000.0f) * (max_blur_limit / 147000.0f), 0.01f, max_blur_limit) : 0.0f;
  
  const float blend = 1.0f - std::exp(-12.0f * delta_render_s);
  float blur_strength = last_rendered_blur_strength_ + (target_blur_strength - last_rendered_blur_strength_) * blend;
  if (blur_strength < 0.005f) {
    blur_strength = 0.0f;
  }
  last_rendered_blur_strength_ = blur_strength;

  const bool apply_blur = blur_strength > 0.0f && device_context != nullptr;

  Microsoft::WRL::ComPtr<ID2D1Image> original_target;
  Microsoft::WRL::ComPtr<ID2D1CommandList> command_list;

  if (apply_blur) {
    device_context->GetTarget(&original_target);
    device_context->CreateCommandList(&command_list);
    device_context->SetTarget(command_list.Get());
  }

  target->PushAxisAlignedClip(ToRectF(list_rect), D2D1_ANTIALIAS_MODE_ALIASED);
  target->FillRectangle(ToRectF(list_rect), d2d_shell_.surface_brush());
  

  const int item_height = GetItemHeight();
  const float visual_scroll_offset = scroll_controller_->VisualScrollOffset();
  const int list_pixel_height = list_rect.bottom - list_rect.top;
  const vlist::VisibleRange visible_range = viewport_.RangeAt(
      visual_scroll_offset, list_pixel_height, item_height * 2, item_height * 2);
  const int first_index = visible_range.first;
  const int last_index = std::min(state_.item_count_ - 1, visible_range.last);
  const bool use_drag_session = row_drag_active_ && row_drag_scope_ != ReorderScope::None && row_drag_session_.has_value();
  const int drag_insert_index = row_drag_active_
      ? std::clamp(row_drop_index_ + (row_drop_after_ ? 1 : 0), 0, state_.item_count_)
      : -1;
  struct RenderRowEntry {
    int index = -1;
    D2D1_RECT_F rect{};
  };
  std::vector<RenderRowEntry> row_entries;
  row_entries.reserve(std::max(0, last_index - first_index + 1));

  for (int index = first_index; index <= last_index; ++index) {
    if (row_drag_active_ && index == row_drag_item_index_) {
      continue;
    }
    int visual_index = index;
    if (row_drag_active_ && !use_drag_session) {
      if (row_drag_item_index_ < drag_insert_index) {
        if (index > row_drag_item_index_ && index < drag_insert_index) {
          visual_index -= 1;
        }
      } else if (row_drag_item_index_ > drag_insert_index) {
        if (index >= drag_insert_index && index < row_drag_item_index_) {
          visual_index += 1;
        }
      }
    }
    const vlist::RowRect row_pos = vlist::RowRectFor(
        viewport_.heights(), visual_index, static_cast<float>(list_rect.top), visual_scroll_offset);
    D2D1_RECT_F row = D2D1::RectF(
        static_cast<float>(list_rect.left),
        row_pos.top,
        static_cast<float>(list_rect.right),
        row_pos.bottom);
    if (row_drag_active_ && row_drag_scope_ != ReorderScope::None) {
      const float animated_top = RowReorderDisplayTop(index, row.top, list_rect, visual_scroll_offset);
      row.top = animated_top;
      row.bottom = animated_top + (row_pos.bottom - row_pos.top);
    }
    if (row.bottom <= list_rect.top || row.top >= list_rect.bottom) {
      continue;
    }
    row_entries.push_back(RenderRowEntry{index, row});
  }

  if (row_drag_active_ && row_drag_scope_ != ReorderScope::None) {
    std::stable_sort(
        row_entries.begin(),
        row_entries.end(),
        [](const RenderRowEntry& left, const RenderRowEntry& right) {
          if (left.rect.top != right.rect.top) {
            return left.rect.top < right.rect.top;
          }
          return left.index < right.index;
        });
  }

  for (const auto& entry : row_entries) {
    const int index = entry.index;
    const D2D1_RECT_F draw_rect = entry.rect;
    const RECT row{
        static_cast<LONG>(std::lround(draw_rect.left)),
        static_cast<LONG>(std::lround(draw_rect.top)),
        static_cast<LONG>(std::lround(draw_rect.right)),
        static_cast<LONG>(std::lround(draw_rect.bottom))};
    RenderListRow(target, client, list_rect, row, index, hover, render_settings, collect_hits);
  }

  if (row_drag_active_ && row_drag_item_index_ >= 0 && row_drag_item_index_ < state_.item_count_) {
    D2D1_MATRIX_3X2_F old_transform;
    target->GetTransform(&old_transform);
    
    const float y_offset = static_cast<float>(row_drag_point_.y - row_drag_pointer_offset_y_);
    target->SetTransform(D2D1::Matrix3x2F::Translation(0.0f, y_offset) * old_transform);
    
    const float drag_row_height =
        static_cast<float>(viewport_.heights().HeightAt(row_drag_item_index_));
    RECT zero_row{
        list_rect.left,
        0,
        list_rect.right,
        static_cast<LONG>(std::lround(drag_row_height))};

    const float shadow_top = 3.0f;
    const float shadow_bottom = drag_row_height + 3.0f;
    target->FillRectangle(
        D2D1::RectF(
            static_cast<float>(zero_row.left) + 2.0f,
            shadow_top,
            static_cast<float>(zero_row.right) + 2.0f,
            shadow_bottom),
        d2d_shell_.surface_hover_brush());
        
    RenderListRow(target, client, list_rect, zero_row, row_drag_item_index_, POINT{-1, -1}, render_settings, false);
    
    target->SetTransform(old_transform);
  }

  if (state_.item_count_ == 0) {
    RenderEmptyListGuidance(target, list_rect, render_settings);
  }

  target->PopAxisAlignedClip();

  if (apply_blur && device_context != nullptr && command_list != nullptr) {
    device_context->SetTarget(original_target.Get());
    command_list->Close();

    Microsoft::WRL::ComPtr<ID2D1Effect> blur_effect;
    HRESULT hr = device_context->CreateEffect(CLSID_D2D1DirectionalBlur, &blur_effect);
    if (SUCCEEDED(hr) && blur_effect != nullptr) {
      blur_effect->SetInput(0, command_list.Get());
      blur_effect->SetValue(D2D1_DIRECTIONALBLUR_PROP_STANDARD_DEVIATION, blur_strength);
      blur_effect->SetValue(D2D1_DIRECTIONALBLUR_PROP_ANGLE, 90.0f);

      device_context->PushAxisAlignedClip(ToRectF(list_rect), D2D1_ANTIALIAS_MODE_ALIASED);
      device_context->DrawImage(blur_effect.Get());
      device_context->PopAxisAlignedClip();
    } else {
      device_context->DrawImage(command_list.Get());
    }
  }

  // If we are actively dragging the scrollbar and the blur is fading out,
  // request a repaint on the next tick to continue the organic fade-out animation.
  if (drag_scrollbar_ && last_rendered_blur_strength_ > 0.0f) {
    InvalidateListRegion();
  }
}

void MainWindow::RenderFrame() {
  // 分段计时：单帧 94ms 远超 60Hz 预算（16.7ms）。setup/draw/present 三段分开记，
  // 才能判断是「数据准备」还是「D2D 绘制」还是「Present 等垂直同步」在吃时间。
  // 必须用 QPC：GetTickCount64 的 15.625ms 粒度会把每帧都量化成 0 或 16ms，
  // 从而伪造出「Present 一直阻塞 16ms」的假象（实测真实值多在 0.5~2ms）。
  common::Stopwatch rf_stopwatch;
  EnsureBitmapCachesValid();
  // C1 暗色模式：主题纪元变化时重建刷子并刷新 DWM 暗色属性与文本框颜色。
  if (d2d_shell_.SyncTheme()) {
    ApplyDarkModeAttribute(hwnd_, clipd2d::ui::IsDarkThemeActive());
    ApplyThemeToTextboxes();
    UpdateRoundedRegion();
  }
  ID2D1DeviceContext* target = d2d_shell_.BeginFrame1_1();
  if (target == nullptr) {
    return;
  }
  const std::int64_t rf_setup_us = rf_stopwatch.ElapsedUs();

  RECT client{};
  GetClientRect(hwnd_, &client);
  const auto browser_state = BuildBrowserState();
  const auto browser_presentation = browser_service_.BuildPresentation(browser_state);
  ShowFilterChipBarFlag() = !browser_presentation.chips.empty();
  const PanelLayout layout = ComputeLayout(client.right, client.bottom);
  const POINT hover = hover_point_;
  const core::store::AppSettings render_settings =
      settings_store_ != nullptr ? settings_store_->current_settings() : core::store::AppSettings{};
  hit_regions_.clear();
  const bool secondary_text_formats_ready = d2d_shell_.secondary_text_formats_ready();

  ConfigureFormat(d2d_shell_.icon_format(), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
  if (secondary_text_formats_ready) {
    ConfigureFormat(d2d_shell_.icon_small_format(), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    ConfigureFormat(d2d_shell_.badge_format(), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    ConfigureFormat(d2d_shell_.tiny_format(), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
  }
  ConfigureFormat(d2d_shell_.meta_format(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
  ConfigureFormat(d2d_shell_.small_format(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
  ConfigureFormat(d2d_shell_.body_format(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
  ConfigureFormat(d2d_shell_.body_bold_format(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
  ConfigureFormat(d2d_shell_.section_format(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

  target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
  float bg_alpha = 1.0f;
  // 复用上面已取的快照：原先这里又调了一次 current_settings()（每次拷贝一整份
  // 设置并加锁），单帧白跑一次锁 + 一次深拷贝。
  const auto& settings = render_settings;
  // C3 外观即时预览：设置窗拖动毛玻璃控件时未保存的值实时生效。
  const core::store::WindowBlurType blur_type = blur_preview_active_
      ? preview_blur_type_ : settings.window_blur_type;
  const int blur_opacity = blur_preview_active_
      ? preview_blur_opacity_ : settings.window_blur_opacity;
  if (blur_type != core::store::WindowBlurType::None) {
      bg_alpha = static_cast<float>(blur_opacity) / 100.0f;
  }
  
  if (d2d_shell_.is_dcomp()) {
    target->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
  } else {
    D2D1_COLOR_F clear_color = d2d_shell_.background_brush()->GetColor();
    clear_color.a = bg_alpha;
    target->Clear(clear_color);
  }
  
  float old_opacity = d2d_shell_.background_brush()->GetOpacity();
  d2d_shell_.background_brush()->SetOpacity(bg_alpha);

  target->FillRectangle(ToRectF(client), d2d_shell_.background_brush());
  
  d2d_shell_.background_brush()->SetOpacity(old_opacity);

  target->FillRoundedRectangle(D2D1::RoundedRect(Inset(layout.search_box, 0.0f, 0.0f), 14.0f, 14.0f), d2d_shell_.surface_alt_brush());
  target->DrawRoundedRectangle(
      D2D1::RoundedRect(Inset(layout.search_box, 0.0f, 0.0f), 14.0f, 14.0f),
      d2d_shell_.border_brush(),
      kControlStrokeWidth);
  RECT search_icon_rect = layout.search_box;
  search_icon_rect.right = search_icon_rect.left + 28;
  DrawFluentIcon(
      d2d_shell_.d2d_factory(),
      target,
      FluentIcon::Search,
      Inset(search_icon_rect, kSearchIconInset, kSearchIconInset),
      d2d_shell_.subtle_text_brush(),
      kTopIconWeightBoost);

  search_textbox_.SetTextColor(d2d_shell_.text_brush()->GetColor());
  search_textbox_.SetPlaceholderColor(d2d_shell_.muted_text_brush()->GetColor());
  search_textbox_.Draw(target);

  const bool has_search_text = !search_textbox_.GetText().empty();
  if (has_search_text) {
    const RECT clear_rect = SearchClearButtonRect(layout);
    const bool hovered = RectContains(clear_rect, hover) || search_clear_hovered_;
    const bool pressed = IsPressedStaticHit(HitKind::SearchClear, 0) || search_clear_pressed_;
    const D2D1_COLOR_F text_color = pressed ? d2d_shell_.ColorText() : d2d_shell_.ColorMutedText();

    if (hovered || pressed) {
      const D2D1_COLOR_F fill_color = pressed ? d2d_shell_.ColorAccentSoft() : d2d_shell_.ColorSurfaceHover();
      const D2D1_COLOR_F border_color = pressed ? d2d_shell_.ColorAccent() : d2d_shell_.ColorBorder();

      // Q2:固定色走 D2dShell 缓存,不再每帧 CreateSolidColorBrush。
      if (ID2D1SolidColorBrush* fill_brush = d2d_shell_.solid_brush(target, fill_color)) {
        target->FillRoundedRectangle(D2D1::RoundedRect(ToRectF(clear_rect), 10.0f, 10.0f), fill_brush);
      }
      if (ID2D1SolidColorBrush* border_brush = d2d_shell_.solid_brush(target, border_color)) {
        target->DrawRoundedRectangle(D2D1::RoundedRect(ToRectF(clear_rect), 10.0f, 10.0f), border_brush, kControlStrokeWidth);
      }
    }

    // Q2:× 字形格式与文字色均走缓存(原每帧创建 CreateTextFormat + 刷子)。
    if (IDWriteTextFormat* format = d2d_shell_.search_clear_format()) {
      if (ID2D1SolidColorBrush* text_brush = d2d_shell_.solid_brush(target, text_color)) {
        RECT text_rect = clear_rect;
        text_rect.top -= 1;
        target->DrawTextW(
            L"\u00D7", 1, format, ToRectF(text_rect), text_brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
      }
    }
    
    hit_regions_.push_back(HitRegion{clear_rect, HitKind::SearchClear, 0});
  }

  for (int i = 0; i < static_cast<int>(std::size(layout.toolbar)); ++i) {
    const bool hovered = RectContains(layout.toolbar[i], hover);
    const bool pressed = IsPressedStaticHit(HitKind::ToolbarButton, i);
    const bool active = i == static_cast<int>(ToolbarCommand::PanelLock) && panel_locked_;
    if (active || pressed || hovered) {
      target->FillRoundedRectangle(
          D2D1::RoundedRect(Inset(layout.toolbar[i], 0.0f, 0.0f), 9.0f, 9.0f),
          active ? d2d_shell_.accent_soft_brush() : (pressed ? d2d_shell_.surface_alt_brush() : d2d_shell_.surface_hover_brush()));
      target->DrawRoundedRectangle(
          D2D1::RoundedRect(Inset(layout.toolbar[i], 0.0f, 0.0f), 9.0f, 9.0f),
          active ? d2d_shell_.accent_brush() : (pressed ? d2d_shell_.text_brush() : d2d_shell_.border_brush()),
          kControlStrokeWidth);
    }
    FluentIcon toolbar_icon = kToolbarIcons[i];
    if (i == static_cast<int>(ToolbarCommand::PanelLock) && active) {
      toolbar_icon = FluentIcon::PanelPinned;
    }
    DrawFluentIcon(
        d2d_shell_.d2d_factory(),
        target,
        toolbar_icon,
        Inset(layout.toolbar[i], kToolbarIconInset, kToolbarIconInset),
        active ? d2d_shell_.accent_brush() : d2d_shell_.text_brush(),
        kTopIconWeightBoost);
    hit_regions_.push_back(HitRegion{layout.toolbar[i], HitKind::ToolbarButton, i});
  }

  const float header_line_y = static_cast<float>(layout.header.bottom);
  const float toolbar_line_y = static_cast<float>(layout.chips_bar.top);
  target->DrawLine(
      D2D1::Point2F(0.0f, header_line_y),
      D2D1::Point2F(static_cast<float>(client.right), header_line_y),
      d2d_shell_.border_brush(),
      kSeparatorStrokeWidth);
  target->DrawLine(
      D2D1::Point2F(0.0f, toolbar_line_y),
      D2D1::Point2F(static_cast<float>(client.right), toolbar_line_y),
      d2d_shell_.border_brush(),
      kSeparatorStrokeWidth);

  for (int i = 0; i < static_cast<int>(std::size(kVisibleTabs)); ++i) {
    const bool selected =
        (i == 0 && browser_state.scope == core::browser::BrowserScope::History) ||
        (i == 1 && browser_state.scope == core::browser::BrowserScope::Favorites) ||
        (kVisibleTabs[i].opens_menu && browser_state.scope == core::browser::BrowserScope::Library);
    const bool hovered = RectContains(layout.tabs[i], hover);
    const bool pressed = IsPressedStaticHit(HitKind::Tab, i);
    if (selected || pressed || hovered) {
      target->FillRoundedRectangle(
          D2D1::RoundedRect(Inset(layout.tabs[i], 0.0f, 0.0f), 9.0f, 9.0f),
          selected ? d2d_shell_.accent_soft_brush() : (pressed ? d2d_shell_.surface_alt_brush() : d2d_shell_.surface_hover_brush()));
      target->DrawRoundedRectangle(
          D2D1::RoundedRect(Inset(layout.tabs[i], 0.0f, 0.0f), 9.0f, 9.0f),
          selected ? d2d_shell_.accent_brush() : (pressed ? d2d_shell_.text_brush() : d2d_shell_.border_brush()),
          kControlStrokeWidth);
    }
    DrawFluentIcon(
        d2d_shell_.d2d_factory(),
        target,
        kVisibleTabs[i].icon,
        Inset(layout.tabs[i], kTabIconInset, kTabIconInset),
        selected ? d2d_shell_.accent_brush() : d2d_shell_.text_brush(),
        kTopIconWeightBoost);
    hit_regions_.push_back(HitRegion{layout.tabs[i], HitKind::Tab, i});
  }

  const FluentIcon nav_icons[] = {FluentIcon::Clipboard, FluentIcon::Filter};
  for (int i = 0; i < static_cast<int>(std::size(layout.nav_actions)); ++i) {
    const bool hovered = RectContains(layout.nav_actions[i], hover);
    const bool pressed = IsPressedStaticHit(HitKind::NavButton, i);
    const bool active =
        (i == 0 && browser_state.scope == core::browser::BrowserScope::Queue) ||
        (i == 1 && !browser_presentation.chips.empty());
    if (active || pressed || hovered) {
      target->FillRoundedRectangle(
          D2D1::RoundedRect(Inset(layout.nav_actions[i], 0.0f, 0.0f), 9.0f, 9.0f),
          active ? d2d_shell_.accent_soft_brush() : (pressed ? d2d_shell_.surface_alt_brush() : d2d_shell_.surface_hover_brush()));
      target->DrawRoundedRectangle(
          D2D1::RoundedRect(Inset(layout.nav_actions[i], 0.0f, 0.0f), 9.0f, 9.0f),
          active ? d2d_shell_.accent_brush() : (pressed ? d2d_shell_.text_brush() : d2d_shell_.border_brush()),
          kControlStrokeWidth);
    }
    DrawFluentIcon(
        d2d_shell_.d2d_factory(),
        target,
        nav_icons[i],
        Inset(layout.nav_actions[i], kToolbarIconInset, kToolbarIconInset),
        active ? d2d_shell_.accent_brush() : d2d_shell_.text_brush(),
        kTopIconWeightBoost);
    hit_regions_.push_back(HitRegion{layout.nav_actions[i], HitKind::NavButton, i});
  }

  const float visual_scroll_offset = scroll_controller_->VisualScrollOffset();
  const bool live_resize_light_render = in_live_resize_ && !row_drag_active_ && !row_reorder_animation_active_;
  const bool collect_list_hits = !row_drag_active_;

  if (live_resize_light_render) {
    RenderLiveResizeListSurface(target, client, layout.list, render_settings);
  } else {
    RenderListSurface(target, client, layout.list, hover, render_settings, collect_list_hits);
  }

  // RenderListShellIconOverlay is deprecated since D2D 1.1 SwapChain model does not support GDI interop.
  // Icons are now rendered fully asynchronously in RenderListRow.
  if (!browser_presentation.chips.empty()) {
    float chip_left = static_cast<float>(layout.chips_bar.left);
    const float chip_height = static_cast<float>(kTagChipHeight);
    const float chip_top = static_cast<float>(layout.chips_bar.top) +
        (static_cast<float>(layout.chips_bar.bottom - layout.chips_bar.top) - chip_height) * 0.5f;
    const std::wstring clear_all = strings::kFilterClearAll;
    const float clear_width = 72.0f;
    RECT clear_rect{
        static_cast<LONG>(std::lround(static_cast<float>(layout.chips_bar.right) - clear_width)),
        static_cast<LONG>(std::lround(chip_top)),
        layout.chips_bar.right,
        static_cast<LONG>(std::lround(chip_top + chip_height))};
    for (const auto& chip : browser_presentation.chips) {
      std::wstring chip_label = chip.label;
      if (chip.kind == core::browser::FilterChipKind::Tag && chip.id > 0) {
        const auto tag_it = std::find_if(state_.tags_.begin(), state_.tags_.end(), [&chip](const core::store::TagRecord& tag) {
          return tag.id == chip.id;
        });
        if (tag_it != state_.tags_.end()) {
          chip_label = L"\u6807\u7b7e: " + tag_it->name;
        }
      }
      chip_label = TrimForDisplay(chip_label, 12);
      const std::wstring chip_text = chip_label + L" \u00d7";
      const float chip_width = std::min(132.0f, 18.0f + static_cast<float>(chip_text.size()) * 7.0f);
      if (chip_left + chip_width > static_cast<float>(clear_rect.left - kTagChipGap)) {
        break;
      }
      RECT chip_rect{
          static_cast<LONG>(std::lround(chip_left)),
          static_cast<LONG>(std::lround(chip_top)),
          static_cast<LONG>(std::lround(chip_left + chip_width)),
          static_cast<LONG>(std::lround(chip_top + chip_height))};
      target->FillRoundedRectangle(
          D2D1::RoundedRect(ToRectF(chip_rect), 10.0f, 10.0f),
          d2d_shell_.surface_hover_brush());
      target->DrawRoundedRectangle(
          D2D1::RoundedRect(ToRectF(chip_rect), 10.0f, 10.0f),
          d2d_shell_.border_brush(),
          kControlStrokeWidth);
      DrawCachedCenteredText(&text_layout_cache_, d2d_shell_.dwrite_factory(), target,
          chip_text,
          d2d_shell_.tiny_format(),
          d2d_shell_.text_brush(),
          chip_rect);
      hit_regions_.push_back(HitRegion{
          chip_rect,
          HitKind::FilterChip,
          static_cast<int>(chip.kind),
          0,
          chip.id});
      chip_left += chip_width + static_cast<float>(kTagChipGap);
    }
    DrawCachedCenteredText(&text_layout_cache_, d2d_shell_.dwrite_factory(), target,
        clear_all,
        d2d_shell_.tiny_format(),
        d2d_shell_.accent_brush(),
        clear_rect);
    hit_regions_.push_back(HitRegion{clear_rect, HitKind::ClearFilters, 0});
  }
  const bool back_to_top_visible = ShouldShowBackToTopButton(layout.list);
  if (back_to_top_visible) {
    const bool hovered = RectContains(layout.back_to_top, hover);
    const bool pressed = IsPressedStaticHit(HitKind::BackToTop, 0);
    if (pressed || hovered) {
      target->FillRoundedRectangle(
          D2D1::RoundedRect(Inset(layout.back_to_top, 0.0f, 0.0f), 12.0f, 12.0f),
          pressed ? d2d_shell_.surface_alt_brush() : d2d_shell_.surface_hover_brush());
      target->DrawRoundedRectangle(
          D2D1::RoundedRect(Inset(layout.back_to_top, 0.0f, 0.0f), 12.0f, 12.0f),
          pressed ? d2d_shell_.text_brush() : d2d_shell_.border_brush(),
          kControlStrokeWidth);
    }
    if (secondary_text_formats_ready) {
      DrawCachedCenteredText(&text_layout_cache_, d2d_shell_.dwrite_factory(), target,
          L"\x56DE\x5230\x9876\x90E8",
          d2d_shell_.tiny_format(),
          d2d_shell_.text_brush(),
          layout.back_to_top);
    }
    hit_regions_.push_back(HitRegion{layout.back_to_top, HitKind::BackToTop, 0});
  }

  if (browser_presentation.show_time_groups) {
    if (const auto* bottom_time_summary = GetBottomTimeSummary(layout.list)) {
      RECT bottom_time_rect{
          layout.list.left + 40,
          std::max(layout.list.top + 4, layout.list.bottom - kTimeGroupHeaderHeight - 6),
          layout.list.right - 40,
          layout.list.bottom - 6};
      if (secondary_text_formats_ready) {
        DrawCachedCenteredText(&text_layout_cache_, d2d_shell_.dwrite_factory(), target,
            DisplayTimeGroupLabel(GetTimeGroupBucket(bottom_time_summary->updated_at)),
            d2d_shell_.tiny_format(),
            d2d_shell_.muted_text_brush(),
            bottom_time_rect);
      }
    }
  }
  const bool left_status_hovered = RectContains(layout.status_left, hover);
  const bool right_status_hovered = RectContains(layout.status_right, hover);

  static std::wstring cached_left_status;
  static std::wstring cached_right_status;
  static std::wstring last_title;
  static int last_item_count = -1;
  static int last_queue_count = -1;
  static std::wstring last_status_text;

  if (last_title != browser_presentation.title || last_item_count != state_.item_count_) {
    last_title = browser_presentation.title;
    last_item_count = state_.item_count_;
    cached_left_status = browser_presentation.title;
    for (size_t chip_index = 0; chip_index < browser_presentation.chips.size() && chip_index < 2; ++chip_index) {
      cached_left_status += strings::kStatusSeparator;
      cached_left_status += browser_presentation.chips[chip_index].label;
    }
    cached_left_status += strings::kStatusTotalSummaryMiddle;
    cached_left_status += GetCachedNumberString(state_.item_count_);
    cached_left_status += strings::kStatusItemsSuffix;
  }

  if (last_status_text != state_.status_text_ || last_queue_count != state_.queue_count_ || last_item_count != state_.item_count_) {
    last_status_text = state_.status_text_;
    last_queue_count = state_.queue_count_;
    last_item_count = state_.item_count_;
    cached_right_status = state_.status_text_;
    if (cached_right_status.empty()) {
      cached_right_status = std::wstring(strings::kStatusQueueSummaryPrefix) + GetCachedNumberString(state_.queue_count_) +
                            strings::kStatusTotalSummaryMiddle + GetCachedNumberString(state_.item_count_) + strings::kStatusItemsSuffix;
    }
  }

  target->DrawLine(
      D2D1::Point2F(0.0f, static_cast<float>(layout.bottom_bar.top)),
      D2D1::Point2F(static_cast<float>(client.right), static_cast<float>(layout.bottom_bar.top)),
      d2d_shell_.border_brush(),
      kSeparatorStrokeWidth);
  if (!cached_left_status.empty()) {
    DrawCachedCenteredText(
        &text_layout_cache_,
        d2d_shell_.dwrite_factory(),
        target,
        cached_left_status,
        secondary_text_formats_ready ? d2d_shell_.tiny_format() : d2d_shell_.small_format(),
        left_status_hovered ? d2d_shell_.text_brush() : d2d_shell_.muted_text_brush(),
        layout.status_left);
  }
  DrawCachedCenteredText(
      &text_layout_cache_,
      d2d_shell_.dwrite_factory(),
      target,
      cached_right_status,
      secondary_text_formats_ready ? d2d_shell_.tiny_format() : d2d_shell_.small_format(),
      right_status_hovered ? d2d_shell_.text_brush() : d2d_shell_.muted_text_brush(),
      layout.status_right);
  if (secondary_text_formats_ready) {
  } else {
    ConfigureFormat(d2d_shell_.small_format(), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    ConfigureFormat(d2d_shell_.small_format(), DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
  }

  #if 0
  if (layout.drawer_width > 0) {
    target->FillRoundedRectangle(D2D1::RoundedRect(Inset(layout.drawer, 0.0f, 0.0f), 16.0f, 16.0f), d2d_shell_.surface_alt_brush());
    target->DrawRoundedRectangle(D2D1::RoundedRect(Inset(layout.drawer, 0.0f, 0.0f), 16.0f, 16.0f), d2d_shell_.border_brush(), 1.0f);
    if (state_.selected_clip_ && layout.drawer_actions.right > layout.drawer_actions.left) {
      RECT close_rect{layout.drawer.right - 38, layout.drawer.top + 14, layout.drawer.right - 14, layout.drawer.top + 38};
      RECT fav_rect{layout.drawer.right - 120, layout.drawer.top + 14, layout.drawer.right - 96, layout.drawer.top + 38};
      RECT pin_rect{layout.drawer.right - 92, layout.drawer.top + 14, layout.drawer.right - 68, layout.drawer.top + 38};
      RECT del_rect{layout.drawer.right - 64, layout.drawer.top + 14, layout.drawer.right - 40, layout.drawer.top + 38};
      const RECT header_buttons[] = {fav_rect, pin_rect, del_rect, close_rect};
      const std::wstring header_icons[] = {
          state_.selected_clip_->summary.is_favorite ? L"\xE735" : L"\xE734",
          state_.selected_clip_->summary.is_pinned ? L"\xE840" : L"\xE718",
          L"\xE74D",
          L"\xE711"};
      const HitKind header_kinds[] = {HitKind::DrawerFavorite, HitKind::DrawerPinned, HitKind::DrawerDelete, HitKind::DrawerClose};
      for (int i = 0; i < 4; ++i) {
        target->FillRoundedRectangle(D2D1::RoundedRect(Inset(header_buttons[i], 0.0f, 0.0f), 8.0f, 8.0f), d2d_shell_.surface_brush());
        target->DrawRoundedRectangle(D2D1::RoundedRect(Inset(header_buttons[i], 0.0f, 0.0f), 8.0f, 8.0f), d2d_shell_.border_brush(), 1.0f);
        DrawCachedCenteredText(&text_layout_cache_, d2d_shell_.dwrite_factory(), target, header_icons[i], d2d_shell_.icon_small_format(), i == 2 ? d2d_shell_.danger_brush() : d2d_shell_.text_brush(), header_buttons[i]);
        hit_regions_.push_back(HitRegion{header_buttons[i], header_kinds[i]});
      }

      RECT content_type_pill_rect{layout.drawer.left + 16, layout.drawer.top + 16, layout.drawer.left + 82, layout.drawer.top + 40};
      target->FillRoundedRectangle(D2D1::RoundedRect(Inset(content_type_pill_rect, 0.0f, 0.0f), 10.0f, 10.0f), d2d_shell_.accent_soft_brush());
      DrawCachedCenteredText(&text_layout_cache_, d2d_shell_.dwrite_factory(), target, core::store::ToDisplayLabel(state_.selected_clip_->summary.content_type), d2d_shell_.tiny_format(), d2d_shell_.accent_brush(), content_type_pill_rect);

      RECT title_rect{layout.drawer.left + 16, layout.drawer.top + 52, layout.drawer.right - 16, layout.drawer.top + 80};
      RECT subtitle_rect{layout.drawer.left + 16, layout.drawer.top + 86, layout.drawer.right - 16, layout.drawer.top + 108};
      const std::wstring title = SummaryTitle(state_.selected_clip_->summary);
      const std::wstring subtitle = FormatTimeLabel(state_.selected_clip_->summary.updated_at) + L" \u00B7 " +
          (state_.selected_clip_->summary.source_app_name.empty() ? L"\u672A\u77E5\u6765\u6E90" : state_.selected_clip_->summary.source_app_name);
      target->DrawTextW(title.c_str(), static_cast<UINT32>(title.size()), d2d_shell_.section_format(), ToRectF(title_rect), d2d_shell_.text_brush(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
      target->DrawTextW(subtitle.c_str(), static_cast<UINT32>(subtitle.size()), d2d_shell_.small_format(), ToRectF(subtitle_rect), d2d_shell_.muted_text_brush(), D2D1_DRAW_TEXT_OPTIONS_CLIP);

      target->FillRoundedRectangle(D2D1::RoundedRect(Inset(layout.drawer_preview, 0.0f, 0.0f), 14.0f, 14.0f), d2d_shell_.surface_brush());
      target->DrawRoundedRectangle(D2D1::RoundedRect(Inset(layout.drawer_preview, 0.0f, 0.0f), 14.0f, 14.0f), d2d_shell_.border_brush(), 1.0f);
      if (state_.selected_clip_->summary.content_type == core::store::ClipContentType::Image) {
        if (state_.preview_bitmap_.bitmap != nullptr) {
          const float available_width = static_cast<float>(layout.drawer_preview.right - layout.drawer_preview.left - 24);
          const float available_height = static_cast<float>(layout.drawer_preview.bottom - layout.drawer_preview.top - 24);
          const float bitmap_width = static_cast<float>(state_.preview_bitmap_.width);
          const float bitmap_height = static_cast<float>(state_.preview_bitmap_.height);
          const float scale = std::min(1.0f, std::min(available_width / bitmap_width, available_height / bitmap_height));
          const float draw_width = bitmap_width * scale;
          const float draw_height = bitmap_height * scale;
          const float left = layout.drawer_preview.left + (available_width - draw_width) * 0.5f + 12.0f;
          const float top = layout.drawer_preview.top + (available_height - draw_height) * 0.5f + 12.0f;
          target->DrawBitmap(state_.preview_bitmap_.bitmap.Get(), D2D1::RectF(left, top, left + draw_width, top + draw_height), 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        } else {
          DrawCachedCenteredText(&text_layout_cache_, d2d_shell_.dwrite_factory(), target, L"\xEB9F", d2d_shell_.title_format(), d2d_shell_.subtle_text_brush(), layout.drawer_preview);
        }
      } else {
        RECT text_rect{layout.drawer_preview.left + 16, layout.drawer_preview.top + 16, layout.drawer_preview.right - 16, layout.drawer_preview.bottom - 16};
        std::wstring body;
        if (state_.selected_clip_->summary.content_type == core::store::ClipContentType::Files) {
          body = ToWide(state_.selected_clip_->plain_text);
        } else if (state_.selected_clip_->summary.content_type == core::store::ClipContentType::Html) {
          RECT toggle_preview{layout.drawer_preview.left + 16, layout.drawer_preview.top + 12, layout.drawer_preview.left + 74, layout.drawer_preview.top + 36};
          RECT toggle_source{layout.drawer_preview.left + 80, layout.drawer_preview.top + 12, layout.drawer_preview.left + 148, layout.drawer_preview.top + 36};
          for (int i = 0; i < 2; ++i) {
            const RECT toggle_rect = i == 0 ? toggle_preview : toggle_source;
            const bool active = (i == 0 && !show_html_source_) || (i == 1 && show_html_source_);
            target->FillRoundedRectangle(D2D1::RoundedRect(Inset(toggle_rect, 0.0f, 0.0f), 10.0f, 10.0f), active ? d2d_shell_.accent_soft_brush() : d2d_shell_.surface_alt_brush());
            target->DrawRoundedRectangle(D2D1::RoundedRect(Inset(toggle_rect, 0.0f, 0.0f), 10.0f, 10.0f), d2d_shell_.border_brush(), 1.0f);
            DrawCachedCenteredText(&text_layout_cache_, d2d_shell_.dwrite_factory(), target, i == 0 ? L"\u9884\u89C8" : L"\u6E90\u7801", d2d_shell_.tiny_format(), active ? d2d_shell_.accent_brush() : d2d_shell_.text_brush(), toggle_rect);
            hit_regions_.push_back(HitRegion{toggle_rect, HitKind::DrawerHtmlMode, i});
          }
          text_rect.top += 34;
          // 存储层保留原始 CF_HTML（回写剪贴板需要头），源码视图展示正文片段。
          body = show_html_source_ ? ToWide(ExtractHtmlFragment(state_.selected_clip_->html_text)) : ToWide(state_.selected_clip_->plain_text);
        } else {
          body = ToWide(state_.selected_clip_->plain_text);
        }
        target->DrawTextW(body.c_str(), static_cast<UINT32>(body.size()), d2d_shell_.small_format(), ToRectF(text_rect), d2d_shell_.text_brush(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
      }

      target->FillRoundedRectangle(D2D1::RoundedRect(Inset(layout.drawer_actions, 0.0f, 0.0f), 14.0f, 14.0f), d2d_shell_.surface_brush());
      target->DrawRoundedRectangle(D2D1::RoundedRect(Inset(layout.drawer_actions, 0.0f, 0.0f), 14.0f, 14.0f), d2d_shell_.border_brush(), 1.0f);
      RECT actions_title{layout.drawer_actions.left + 16, layout.drawer_actions.top + 14, layout.drawer_actions.right - 16, layout.drawer_actions.top + 34};
      target->DrawTextW(L"\u53EF\u7528\u64CD\u4F5C", 4, d2d_shell_.body_bold_format(), ToRectF(actions_title), d2d_shell_.text_brush(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
      int action_left = layout.drawer_actions.left + 16;
      int action_top = layout.drawer_actions.top + 42;
      for (const auto& action : state_.actions_) {
        const std::wstring label = ActionLabel(action);
        const int available_action_width =
            static_cast<int>(layout.drawer_actions.right - layout.drawer_actions.left - 32);
        if (available_action_width < 84) {
          break;
        }
        const int pill_width = std::clamp(
            48 + static_cast<int>(label.size()) * 14,
            84,
            available_action_width);
        RECT action_pill_rect{action_left, action_top, action_left + pill_width, action_top + 32};
        if (action_pill_rect.right > layout.drawer_actions.right - 16) {
          action_left = layout.drawer_actions.left + 16;
          action_top += 38;
          action_pill_rect = RECT{action_left, action_top, action_left + pill_width, action_top + 32};
        }
        target->FillRoundedRectangle(
            D2D1::RoundedRect(Inset(action_pill_rect, 0.0f, 0.0f), 12.0f, 12.0f),
            action.is_primary || action.placement == "quick" ? d2d_shell_.accent_soft_brush() : d2d_shell_.surface_alt_brush());
        target->DrawRoundedRectangle(D2D1::RoundedRect(Inset(action_pill_rect, 0.0f, 0.0f), 12.0f, 12.0f), d2d_shell_.border_brush(), 1.0f);
        RECT icon_rect{action_pill_rect.left + 8, action_pill_rect.top + 6, action_pill_rect.left + 26, action_pill_rect.bottom - 6};
        RECT label_rect{action_pill_rect.left + 28, action_pill_rect.top + 6, action_pill_rect.right - 8, action_pill_rect.bottom - 6};
        if (!action.icon_token.empty()) {
          DrawCachedCenteredText(&text_layout_cache_, d2d_shell_.dwrite_factory(), target, action.icon_token, d2d_shell_.icon_small_format(), d2d_shell_.accent_brush(), icon_rect);
        }
        target->DrawTextW(label.c_str(), static_cast<UINT32>(label.size()), d2d_shell_.tiny_format(), ToRectF(label_rect), d2d_shell_.text_brush(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
        hit_regions_.push_back(HitRegion{action_pill_rect, HitKind::DrawerAction, 0, state_.selected_clip_->summary.id, action.id});
        action_left = action_pill_rect.right + 8;
      }

      target->FillRoundedRectangle(D2D1::RoundedRect(Inset(layout.drawer_footer, 0.0f, 0.0f), 14.0f, 14.0f), d2d_shell_.surface_brush());
      target->DrawRoundedRectangle(D2D1::RoundedRect(Inset(layout.drawer_footer, 0.0f, 0.0f), 14.0f, 14.0f), d2d_shell_.border_brush(), 1.0f);
      RECT footer_title{layout.drawer_footer.left + 16, layout.drawer_footer.top + 14, layout.drawer_footer.right - 16, layout.drawer_footer.top + 36};
      RECT footer_body{layout.drawer_footer.left + 16, layout.drawer_footer.top + 42, layout.drawer_footer.right - 16, layout.drawer_footer.bottom - 16};
      target->DrawTextW(L"\u6765\u6E90\u4E0E\u8DEF\u5F84", 5, d2d_shell_.body_bold_format(), ToRectF(footer_title), d2d_shell_.text_brush(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
      std::wstring footer_text = state_.selected_clip_->summary.source_app_name;
      if (!footer_text.empty()) {
        footer_text += L"\n";
      }
      if (state_.selected_clip_->summary.content_type == core::store::ClipContentType::Image) {
        footer_text += state_.selected_clip_->storage_path;
      } else if (state_.selected_clip_->summary.content_type == core::store::ClipContentType::Files) {
        footer_text += FirstFilePathFromClip(*state_.selected_clip_);
      } else {
        footer_text += state_.selected_clip_->summary.source_app_path;
      }
      target->DrawTextW(footer_text.c_str(), static_cast<UINT32>(footer_text.size()), d2d_shell_.tiny_format(), ToRectF(footer_body), d2d_shell_.muted_text_brush(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
  }

  if (state_.item_count_ > 0) {
    const float list_height = static_cast<float>(layout.list.bottom - layout.list.top);
    const float content_height = static_cast<float>(viewport_.TotalHeight());
    if (content_height > list_height) {
      const float thumb_height = std::max(40.0f, list_height * (list_height / content_height));
      const float travel = list_height - thumb_height;
      const float offset_ratio = scroll_controller_->VisualScrollOffset() / std::max(1.0f, content_height - list_height);
      target->FillRoundedRectangle(
          D2D1::RoundedRect(
              D2D1::RectF(
                  static_cast<float>(layout.list.right - 6),
                  static_cast<float>(layout.list.top) + travel * offset_ratio,
                  static_cast<float>(layout.list.right - 2),
                  static_cast<float>(layout.list.top) + travel * offset_ratio + thumb_height),
              2.0f,
              2.0f),
          d2d_shell_.border_brush());
    }
  }

  #endif

  const float list_height = static_cast<float>(layout.list.bottom - layout.list.top);
  const float content_height = static_cast<float>(viewport_.TotalHeight());
  if (content_height > list_height && scroll_controller_->ShouldShowScrollThumb()) {
    const float track_height = static_cast<float>(layout.scrollbar_track.bottom - layout.scrollbar_track.top);
    const float thumb_height = std::max(44.0f, track_height * (list_height / content_height));
    const float travel = std::max(1.0f, track_height - thumb_height);
    const float ratio = visual_scroll_offset / std::max(1.0f, content_height - list_height);
    D2D1_RECT_F thumb_rect = D2D1::RectF(
        static_cast<float>(layout.scrollbar_track.left),
        static_cast<float>(layout.scrollbar_track.top) + travel * ratio,
        static_cast<float>(layout.scrollbar_track.right),
        static_cast<float>(layout.scrollbar_track.top) + travel * ratio + thumb_height);
    target->FillRoundedRectangle(
        D2D1::RoundedRect(thumb_rect, 2.0f, 2.0f),
        drag_scrollbar_ || RectContains(ScrollbarHitRect(layout.scrollbar_track, true), hover) ? d2d_shell_.accent_brush() : d2d_shell_.subtle_text_brush());
  }

  if (overlay_dialog_.visible) {
    // Q2:压暗遮罩是固定色,走 D2dShell 缓存(原每帧创建)。
    if (ID2D1SolidColorBrush* overlay_brush =
            d2d_shell_.solid_brush(target, D2D1::ColorF(0x111827, 0.18f))) {
      target->FillRectangle(ToRectF(client), overlay_brush);
    }

    const auto card = D2D1::RoundedRect(ToRectF(overlay_dialog_.card_rect), 16.0f, 16.0f);
    target->FillRoundedRectangle(card, d2d_shell_.surface_brush());
    target->DrawRoundedRectangle(card, d2d_shell_.border_brush(), 1.0f);

    const D2D1_RECT_F title_rect = D2D1::RectF(
        static_cast<float>(overlay_dialog_.card_rect.left + 18),
        static_cast<float>(overlay_dialog_.card_rect.top + 16),
        static_cast<float>(overlay_dialog_.card_rect.right - 18),
        static_cast<float>(overlay_dialog_.card_rect.top + 40));
    target->DrawTextW(
        overlay_dialog_.title.c_str(),
        static_cast<UINT32>(overlay_dialog_.title.size()),
        d2d_shell_.section_format(),
        title_rect,
        d2d_shell_.text_brush(),
        D2D1_DRAW_TEXT_OPTIONS_CLIP);

    const std::wstring body_text = overlay_dialog_.has_text_input ? overlay_dialog_.prompt : overlay_dialog_.message;
    target->DrawTextW(
        body_text.c_str(),
        static_cast<UINT32>(body_text.size()),
        d2d_shell_.small_format(),
        ToRectF(overlay_dialog_.message_rect),
        d2d_shell_.muted_text_brush(),
        D2D1_DRAW_TEXT_OPTIONS_NONE);

    if (overlay_dialog_.has_text_input) {
      const auto input = D2D1::RoundedRect(ToRectF(overlay_dialog_.input_rect), 10.0f, 10.0f);
      target->FillRoundedRectangle(input, d2d_shell_.surface_alt_brush());
      target->DrawRoundedRectangle(
          input,
          overlay_textbox_.IsFocused() ? d2d_shell_.accent_brush() : d2d_shell_.border_brush(),
          1.0f);
      overlay_textbox_.SetTextColor(d2d_shell_.text_brush()->GetColor());
      overlay_textbox_.SetPlaceholderColor(d2d_shell_.muted_text_brush()->GetColor());
      overlay_textbox_.Draw(target);
      if (!overlay_dialog_.error_text.empty()) {
        target->DrawTextW(
            overlay_dialog_.error_text.c_str(),
            static_cast<UINT32>(overlay_dialog_.error_text.size()),
            d2d_shell_.tiny_format(),
            ToRectF(overlay_dialog_.error_rect),
            d2d_shell_.danger_brush(),
            D2D1_DRAW_TEXT_OPTIONS_CLIP);
      }
    }

    if (overlay_dialog_.has_color_options && !overlay_dialog_.color_rects.empty()) {
      const float color_prompt_top = static_cast<float>(overlay_dialog_.color_rects.front().top - 20);
      target->DrawTextW(
          L"\u989c\u8272",
          2,
          d2d_shell_.small_format(),
          D2D1::RectF(
              static_cast<float>(overlay_dialog_.input_rect.left),
              color_prompt_top,
              static_cast<float>(overlay_dialog_.input_rect.right),
              color_prompt_top + 16.0f),
          d2d_shell_.muted_text_brush(),
          D2D1_DRAW_TEXT_OPTIONS_CLIP);
      for (size_t index = 0; index < overlay_dialog_.color_rects.size(); ++index) {
        const RECT& chip_rect = overlay_dialog_.color_rects[index];
        const bool selected = overlay_dialog_.selected_color == overlay_dialog_.color_options[index].value;
        const bool hovered = RectContains(chip_rect, hover);
        const auto rounded = D2D1::RoundedRect(ToRectF(chip_rect), 10.0f, 10.0f);
        target->FillRoundedRectangle(
            rounded,
            selected ? d2d_shell_.accent_soft_brush()
                     : (hovered ? d2d_shell_.surface_hover_brush() : d2d_shell_.surface_alt_brush()));
        target->DrawRoundedRectangle(
            rounded,
            selected ? d2d_shell_.accent_brush() : d2d_shell_.border_brush(),
            1.0f);
        DrawCachedCenteredText(&text_layout_cache_, d2d_shell_.dwrite_factory(), target,
            overlay_dialog_.color_options[index].label,
            d2d_shell_.tiny_format(),
            selected ? d2d_shell_.accent_brush() : d2d_shell_.text_brush(),
            chip_rect);
        hit_regions_.push_back(HitRegion{chip_rect, HitKind::DialogColorChip, static_cast<int>(index)});
      }
    }

    const auto draw_dialog_button = [&](const RECT& rect, const std::wstring& text, bool primary, bool dangerous) {
      const bool hovered = RectContains(rect, hover);
      const auto rounded = D2D1::RoundedRect(ToRectF(rect), 10.0f, 10.0f);
      ID2D1SolidColorBrush* fill = nullptr;
      ID2D1SolidColorBrush* border = nullptr;
      ID2D1SolidColorBrush* text_brush = nullptr;
      if (primary) {
        fill = dangerous ? d2d_shell_.danger_brush() : d2d_shell_.accent_soft_brush();
        border = dangerous ? d2d_shell_.danger_brush() : d2d_shell_.accent_brush();
        text_brush = dangerous ? d2d_shell_.white_brush() : d2d_shell_.accent_brush();
      } else {
        fill = hovered ? d2d_shell_.surface_hover_brush() : d2d_shell_.surface_alt_brush();
        border = d2d_shell_.border_brush();
        text_brush = d2d_shell_.text_brush();
      }
      target->FillRoundedRectangle(rounded, fill);
      target->DrawRoundedRectangle(rounded, border, 1.0f);
      DrawCachedCenteredText(&text_layout_cache_, d2d_shell_.dwrite_factory(), target, text, d2d_shell_.small_format(), text_brush, rect);
    };

    draw_dialog_button(overlay_dialog_.confirm_rect, overlay_dialog_.confirm_text, true, overlay_dialog_.dangerous_confirm);
    draw_dialog_button(overlay_dialog_.cancel_rect, overlay_dialog_.cancel_text, false, false);
    hit_regions_.push_back(HitRegion{overlay_dialog_.confirm_rect, HitKind::DialogConfirm, 0});
    hit_regions_.push_back(HitRegion{overlay_dialog_.cancel_rect, HitKind::DialogCancel, 0});
  }

  const std::int64_t rf_draw_us = rf_stopwatch.ElapsedUs() - rf_setup_us;
  const std::size_t rf_dirty_rows = state_.dirty_rows_.size();
  const bool presented = d2d_shell_.EndFrame1_1();
  const std::int64_t rf_total_us = rf_stopwatch.ElapsedUs();
  const std::int64_t rf_present_us = rf_total_us - rf_setup_us - rf_draw_us;
  if (presented) {
    LayoutControls();
    // L5/L6:本帧已呈现,行内容脏标记清空(不清会让 has_pending_work 永真)。
    state_.dirty_rows_.clear();
  }
  const std::size_t rf_hit_count = hit_regions_.size();
  if (rf_total_us >= 16000) {
    common::WriteLog(
        common::LogLevel::Warning,
        "ui.frame.slow",
        "total_ms=" + common::FormatMs(rf_total_us) +
            " setup_ms=" + common::FormatMs(rf_setup_us) +
            " draw_ms=" + common::FormatMs(rf_draw_us) +
            " present_ms=" + common::FormatMs(rf_present_us) +
            " items=" + std::to_string(state_.item_count_) +
            " dirty_rows=" + std::to_string(rf_dirty_rows) +
            " hits=" + std::to_string(rf_hit_count) +
            " h=" + std::to_string(window_height_));
  }
}


}  // namespace clipd2d::ui
