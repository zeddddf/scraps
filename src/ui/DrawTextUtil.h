#pragma once

// 文本绘制工具与搜索高亮。原先在 MainWindowInternal.h 的匿名命名空间里
// 被每个 TU 各持一份；这里改为具名实体 + 单一定义。

#include <cstdint>
#include <string>
#include <vector>

#include <d2d1_1.h>
#include <dwrite.h>

#include "core/store/ClipTypes.h"
#include "ui/TextLayoutCache.h"

namespace clipd2d::ui {

enum class CachedTextRole {
  Meta = 1,
  FilePrimary = 2,
  FileTrailing = 3,
  FilePath = 4,
  Content = 5,
  UI = 6,
};

void DrawCenteredText(
    ID2D1RenderTarget* target,
    const std::wstring& text,
    IDWriteTextFormat* format,
    ID2D1Brush* brush,
    const RECT& rect);

void DrawCachedCenteredText(
    TextLayoutCache* cache,
    IDWriteFactory* factory,
    ID2D1RenderTarget* target,
    const std::wstring& text,
    IDWriteTextFormat* format,
    ID2D1Brush* brush,
    const RECT& rect);

void ConfigureFormat(IDWriteTextFormat* format, DWRITE_TEXT_ALIGNMENT alignment, DWRITE_PARAGRAPH_ALIGNMENT paragraph);

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
    bool single_line);

struct HighlightRange {
  UINT32 start = 0;
  UINT32 length = 0;
};

std::vector<HighlightRange> ComputeHighlightRanges(const std::wstring& text, const std::vector<std::wstring>& tokens);

void DrawHighlightedText(
    IDWriteFactory* factory,
    ID2D1RenderTarget* target,
    const std::wstring& text,
    IDWriteTextFormat* format,
    ID2D1Brush* brush,
    ID2D1Brush* highlight_brush,
    const RECT& rect,
    bool single_line,
    const std::vector<std::wstring>& tokens);

TextLayoutCache::Key MakeTextLayoutKey(
    const core::store::ClipSummary& summary,
    CachedTextRole role,
    float width);
int WidthBucket(float width);
std::wstring ToLowerCopy(std::wstring_view text);

}  // namespace clipd2d::ui
