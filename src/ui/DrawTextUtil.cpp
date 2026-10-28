#include "ui/DrawTextUtil.h"

#include <algorithm>
#include <cwctype>

#include "common/StringUtil.h"
#include "common/Utf8.h"
#include "ui/D2dShell.h"
#include "ui/D2dUtil.h"
#include "ui/TextLayoutCache.h"

namespace clipd2d::ui {

void DrawCenteredText(
    ID2D1RenderTarget* target,
    const std::wstring& text,
    IDWriteTextFormat* format,
    ID2D1Brush* brush,
    const RECT& rect) {
  if (text.empty()) {
    return;
  }
  target->DrawTextW(
      text.c_str(),
      static_cast<UINT32>(text.size()),
      format,
      ToRectF(rect),
      brush,
      D2D1_DRAW_TEXT_OPTIONS_CLIP);
}


void DrawCachedCenteredText(
    TextLayoutCache* cache,
    IDWriteFactory* factory,
    ID2D1RenderTarget* target,
    const std::wstring& text,
    IDWriteTextFormat* format,
    ID2D1Brush* brush,
    const RECT& rect) {
  if (cache == nullptr || factory == nullptr || target == nullptr || format == nullptr || brush == nullptr || text.empty()) {
    return;
  }
  const float width = static_cast<float>(std::max(1L, rect.right - rect.left));
  const float height = static_cast<float>(std::max(1L, rect.bottom - rect.top));
  TextLayoutCache::Key key{static_cast<std::uint64_t>(std::hash<std::wstring>{}(text)), static_cast<int>(CachedTextRole::UI)};
  const auto layout = cache->GetOrCreate(
      factory,
      key,
      text,
      format,
      width,
      height,
      true);
  if (layout != nullptr) {
    target->DrawTextLayout(
        D2D1::Point2F(static_cast<float>(rect.left), static_cast<float>(rect.top)),
        layout.Get(),
        brush,
        D2D1_DRAW_TEXT_OPTIONS_CLIP);
  }
}


void ConfigureFormat(IDWriteTextFormat* format, DWRITE_TEXT_ALIGNMENT alignment, DWRITE_PARAGRAPH_ALIGNMENT paragraph) {
  format->SetTextAlignment(alignment);
  format->SetParagraphAlignment(paragraph);
}


void DrawCachedText(
    TextLayoutCache* cache,
    IDWriteFactory* factory,
    ID2D1RenderTarget* target,
    const core::store::ClipSummary& summary,
    CachedTextRole role,
    const std::wstring& text,
    IDWriteTextFormat* format,
    ID2D1Brush* brush,
    const RECT& rect,
    bool single_line) {
  if (cache == nullptr || factory == nullptr || target == nullptr || format == nullptr || brush == nullptr || text.empty()) {
    return;
  }
  const float width = static_cast<float>(std::max(1L, rect.right - rect.left));
  const float height = static_cast<float>(std::max(1L, rect.bottom - rect.top));
  const auto layout = cache->GetOrCreate(
      factory,
      MakeTextLayoutKey(summary, role, width),
      text,
      format,
      static_cast<float>(WidthBucket(width)),
      height,
      single_line);
  if (layout != nullptr) {
    target->DrawTextLayout(
        D2D1::Point2F(static_cast<float>(rect.left), static_cast<float>(rect.top)),
        layout.Get(),
        brush,
        D2D1_DRAW_TEXT_OPTIONS_CLIP);
    return;
  }
  target->DrawTextW(
      text.c_str(),
      static_cast<UINT32>(text.size()),
      format,
      ToRectF(rect),
      brush,
      D2D1_DRAW_TEXT_OPTIONS_CLIP);
}



std::vector<HighlightRange> ComputeHighlightRanges(const std::wstring& text, const std::vector<std::wstring>& tokens) {
  std::vector<HighlightRange> ranges;
  if (text.empty() || tokens.empty()) {
    return ranges;
  }

  const std::wstring lower = ToLowerCopy(text);
  for (const auto& token : tokens) {
    if (token.empty()) {
      continue;
    }
    size_t start = 0;
    while (start < lower.size()) {
      const size_t found = lower.find(token, start);
      if (found == std::wstring::npos) {
        break;
      }
      ranges.push_back(HighlightRange{
          static_cast<UINT32>(found),
          static_cast<UINT32>(token.size())});
      start = found + token.size();
    }
  }

  if (ranges.empty()) {
    return ranges;
  }
  std::sort(ranges.begin(), ranges.end(), [](const HighlightRange& lhs, const HighlightRange& rhs) {
    if (lhs.start != rhs.start) {
      return lhs.start < rhs.start;
    }
    return lhs.length < rhs.length;
  });

  std::vector<HighlightRange> merged;
  merged.push_back(ranges.front());
  for (size_t index = 1; index < ranges.size(); ++index) {
    HighlightRange& current = merged.back();
    const UINT32 current_end = current.start + current.length;
    if (ranges[index].start <= current_end) {
      current.length = std::max(current_end, ranges[index].start + ranges[index].length) - current.start;
    } else {
      merged.push_back(ranges[index]);
    }
  }
  return merged;
}


void DrawHighlightedText(
    IDWriteFactory* factory,
    ID2D1RenderTarget* target,
    const std::wstring& text,
    IDWriteTextFormat* format,
    ID2D1Brush* brush,
    ID2D1Brush* highlight_brush,
    const RECT& rect,
    bool single_line,
    const std::vector<std::wstring>& tokens) {
  if (factory == nullptr || target == nullptr || format == nullptr || brush == nullptr || text.empty()) {
    return;
  }
  if (tokens.empty()) {
    target->DrawTextW(
        text.c_str(),
        static_cast<UINT32>(text.size()),
        format,
        ToRectF(rect),
        brush,
        D2D1_DRAW_TEXT_OPTIONS_CLIP);
    return;
  }

  Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
  const float width = static_cast<float>(std::max(1L, rect.right - rect.left));
  const float height = static_cast<float>(std::max(1L, rect.bottom - rect.top));
  if (FAILED(factory->CreateTextLayout(
          text.c_str(),
          static_cast<UINT32>(text.size()),
          format,
          width,
          height,
          layout.ReleaseAndGetAddressOf()))) {
    target->DrawTextW(
        text.c_str(),
        static_cast<UINT32>(text.size()),
        format,
        ToRectF(rect),
        brush,
        D2D1_DRAW_TEXT_OPTIONS_CLIP);
    return;
  }
  if (single_line) {
    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
  }

  const auto ranges = ComputeHighlightRanges(text, tokens);
  for (const auto& range : ranges) {
    UINT32 actual_count = 0;
    layout->HitTestTextRange(
        range.start,
        range.length,
        static_cast<float>(rect.left),
        static_cast<float>(rect.top),
        nullptr,
        0,
        &actual_count);
    if (actual_count == 0) {
      continue;
    }
    std::vector<DWRITE_HIT_TEST_METRICS> metrics(actual_count);
    if (FAILED(layout->HitTestTextRange(
            range.start,
            range.length,
            static_cast<float>(rect.left),
            static_cast<float>(rect.top),
            metrics.data(),
            actual_count,
            &actual_count))) {
      continue;
    }
    for (UINT32 index = 0; index < actual_count; ++index) {
      const auto& metric = metrics[index];
      const D2D1_RECT_F fill_rect = D2D1::RectF(
          static_cast<float>(metric.left),
          static_cast<float>(metric.top),
          static_cast<float>(metric.left + metric.width),
          static_cast<float>(metric.top + metric.height));
      target->FillRoundedRectangle(
          D2D1::RoundedRect(fill_rect, 4.0f, 4.0f),
          highlight_brush);
    }
  }

  target->DrawTextLayout(
      D2D1::Point2F(static_cast<float>(rect.left), static_cast<float>(rect.top)),
      layout.Get(),
      brush,
      D2D1_DRAW_TEXT_OPTIONS_CLIP);
}


TextLayoutCache::Key MakeTextLayoutKey(
    const core::store::ClipSummary& summary,
    CachedTextRole role,
    float width) {
  return TextLayoutCache::Key{
      static_cast<std::uint64_t>(summary.id),
      static_cast<std::uint64_t>(summary.updated_at),
      WidthBucket(width),
      static_cast<int>(role)};
}


int WidthBucket(float width) {
  const int rounded = static_cast<int>(std::lround(width / 8.0f)) * 8;
  return std::max(8, rounded);
}


std::wstring ToLowerCopy(std::wstring_view text) {
  std::wstring lower(text);
  std::transform(lower.begin(), lower.end(), lower.begin(), [](wchar_t ch) {
    return static_cast<wchar_t>(std::towlower(ch));
  });
  return lower;
}

}  // namespace clipd2d::ui
