#pragma once

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <string_view>

#include "ui/D2dShell.h"

namespace clipd2d::ui {

std::wstring FormatBytes(std::uint64_t bytes);


inline RECT MakeRect(int left, int top, int right, int bottom) {
  RECT rect{};
  rect.left = left;
  rect.top = top;
  rect.right = right;
  rect.bottom = bottom;
  return rect;
}

inline RECT InsetRectCopy(const RECT& rect, int dx, int dy) {
  RECT r = rect;
  InflateRect(&r, -dx, -dy);
  return r;
}


void DrawTextLine(
    ID2D1HwndRenderTarget* target,
    IDWriteTextFormat* format,
    ID2D1Brush* brush,
    const RECT& rect,
    std::wstring_view text,
    DWRITE_TEXT_ALIGNMENT alignment = DWRITE_TEXT_ALIGNMENT_LEADING);

void DrawParagraph(
    ID2D1HwndRenderTarget* target,
    IDWriteTextFormat* format,
    ID2D1Brush* brush,
    const RECT& rect,
    std::wstring_view text);

void FillRoundedPanel(
    ID2D1HwndRenderTarget* target,
    const RECT& rect,
    float radius,
    ID2D1Brush* fill,
    ID2D1Brush* border = nullptr,
    float border_width = 1.0f);

void DrawToggle(
    ID2D1HwndRenderTarget* target,
    const RECT& rect,
    bool enabled,
    D2dShell& shell);

void DrawSelectTrigger(
    ID2D1HwndRenderTarget* target,
    D2dShell& shell,
    const RECT& rect,
    std::wstring_view text,
    bool active);

void DrawRowDivider(
    ID2D1HwndRenderTarget* target,
    D2dShell& shell,
    const RECT& row_rect);

void DrawSlider(
    ID2D1HwndRenderTarget* target,
    D2dShell& shell,
    const RECT& rect,
    int min_value,
    int max_value,
    int value);

}  // namespace clipd2d::ui
