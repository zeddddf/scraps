#include <array>
#include <sstream>
#include <algorithm>
#include <cmath>
#include "ui/settings/SettingsDrawUtil.h"
#include "ui/D2dShell.h"
#include "ui/D2dUtil.h"

namespace clipd2d::ui {

std::wstring FormatBytes(std::uint64_t bytes) {
  constexpr std::array<const wchar_t*, 4> kUnits = {L"B", L"KB", L"MB", L"GB"};
  double value = static_cast<double>(bytes);
  size_t unit = 0;
  while (value >= 1024.0 && unit + 1 < kUnits.size()) {
    value /= 1024.0;
    ++unit;
  }
  std::wostringstream stream;
  if (unit == 0) {
    stream << static_cast<std::uint64_t>(value) << L' ' << kUnits[unit];
  } else {
    stream.setf(std::ios::fixed);
    stream.precision(value < 10.0 ? 1 : 0);
    stream << value << L' ' << kUnits[unit];
  }
  return stream.str();
}


namespace {

inline float SnapToPixel(float dip, float scale) {
  return std::round(dip * scale) / scale;
}

} // namespace

void DrawTextLine(
    ID2D1HwndRenderTarget* target,
    IDWriteTextFormat* format,
    ID2D1Brush* brush,
    const RECT& rect,
    std::wstring_view text,
    DWRITE_TEXT_ALIGNMENT alignment) {
  if (target == nullptr || format == nullptr || brush == nullptr) {
    return;
  }
  format->SetTextAlignment(alignment);
  format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
  D2D1_RECT_F rectF = ToRectF(rect);
  target->PushAxisAlignedClip(rectF, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
  target->DrawTextW(text.data(), static_cast<UINT32>(text.size()), format, rectF, brush);
  target->PopAxisAlignedClip();
}

void DrawParagraph(
    ID2D1HwndRenderTarget* target,
    IDWriteTextFormat* format,
    ID2D1Brush* brush,
    const RECT& rect,
    std::wstring_view text) {
  if (target == nullptr || format == nullptr || brush == nullptr) {
    return;
  }
  format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
  format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
  target->DrawTextW(text.data(), static_cast<UINT32>(text.size()), format, ToRectF(rect), brush);
}

void FillRoundedPanel(
    ID2D1HwndRenderTarget* target,
    const RECT& rect,
    float radius,
    ID2D1Brush* fill,
    ID2D1Brush* border,
    float border_width) {
  if (target == nullptr || fill == nullptr) {
    return;
  }
  float dpi_x = 96.0f;
  float dpi_y = 96.0f;
  target->GetDpi(&dpi_x, &dpi_y);
  const float scale = dpi_x / 96.0f;
  const float snapped_border_width = 1.0f / scale; // Ensure 1 physical pixel width

  auto rect_f = ToRectF(rect);
  if (border != nullptr) {
    float half_px = 0.5f / scale;
    rect_f.left = SnapToPixel(rect_f.left, scale) + half_px;
    rect_f.top = SnapToPixel(rect_f.top, scale) + half_px;
    rect_f.right = SnapToPixel(rect_f.right, scale) - half_px;
    rect_f.bottom = SnapToPixel(rect_f.bottom, scale) - half_px;
  }
  
  const auto rounded = D2D1::RoundedRect(rect_f, radius, radius);
  target->FillRoundedRectangle(rounded, fill);
  if (border != nullptr) {
    target->DrawRoundedRectangle(rounded, border, snapped_border_width);
  }
}

void DrawToggle(
    ID2D1HwndRenderTarget* target,
    const RECT& rect,
    bool enabled,
    D2dShell& shell) {
  const D2D1_COLOR_F knob_color = enabled ? D2D1::ColorF(0xFFFFFF) : D2D1::ColorF(0x5C5C5C);
  FillRoundedPanel(
      target,
      rect,
      static_cast<float>(rect.bottom - rect.top) * 0.5f,
      enabled ? shell.accent_brush() : shell.surface_alt_brush(),
      shell.border_brush(),
      1.0f);
  const float radius = static_cast<float>(rect.bottom - rect.top - 6) * 0.5f;
  const float center_x = enabled ? static_cast<float>(rect.right - 3) - radius : static_cast<float>(rect.left + 3) + radius;
  const float center_y = (static_cast<float>(rect.top) + static_cast<float>(rect.bottom)) * 0.5f;
  // Q2:开关旋钮是固定色,走 D2dShell 缓存,不再每帧 CreateSolidColorBrush。
  if (ID2D1SolidColorBrush* knob_brush = shell.solid_brush(target, knob_color)) {
    target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(center_x, center_y), radius, radius), knob_brush);
  }
}

void DrawSelectTrigger(
    ID2D1HwndRenderTarget* target,
    D2dShell& shell,
    const RECT& rect,
    std::wstring_view text,
    bool active) {
  FillRoundedPanel(
      target,
      rect,
      12.0f,
      shell.surface_brush(),
      active ? shell.accent_brush() : shell.border_brush(),
      1.0f);

  RECT text_rect = rect;
  text_rect.left += 12;
  text_rect.right -= 28;
  DrawTextLine(target, shell.body_format(), shell.text_brush(), text_rect, text, DWRITE_TEXT_ALIGNMENT_LEADING);

  const float center_x = static_cast<float>(rect.right - 14);
  const float center_y = (static_cast<float>(rect.top) + static_cast<float>(rect.bottom)) * 0.5f + (active ? 1.0f : 0.0f);
  const float half = 4.0f;
  const auto start = D2D1::Point2F(center_x - half, center_y - 1.5f);
  const auto mid = D2D1::Point2F(center_x, center_y + 2.0f);
  const auto end = D2D1::Point2F(center_x + half, center_y - 1.5f);
  target->DrawLine(start, mid, active ? shell.accent_brush() : shell.muted_text_brush(), 1.4f);
  target->DrawLine(mid, end, active ? shell.accent_brush() : shell.muted_text_brush(), 1.4f);
}

void DrawRowDivider(ID2D1HwndRenderTarget* target, D2dShell& shell, const RECT& row_rect) {
  const float y = static_cast<float>(row_rect.bottom) + 0.5f;
  target->DrawLine(
      D2D1::Point2F(static_cast<float>(row_rect.left), y),
      D2D1::Point2F(static_cast<float>(row_rect.right), y),
      shell.border_brush(),
      1.0f);
}

void DrawSlider(
    ID2D1HwndRenderTarget* target,
    D2dShell& shell,
    const RECT& rect,
    int min_value,
    int max_value,
    int value) {
  float width = static_cast<float>(rect.right - rect.left);
  float height = static_cast<float>(rect.bottom - rect.top);
  float cx = static_cast<float>(rect.left);
  float cy = static_cast<float>(rect.top);

  float track_height = 4.0f;
  float track_y = cy + (height - track_height) / 2.0f;
  D2D1_ROUNDED_RECT track_rect = D2D1::RoundedRect(
      D2D1::RectF(cx, track_y, cx + width, track_y + track_height),
      track_height / 2.0f, track_height / 2.0f);
  target->FillRoundedRectangle(&track_rect, shell.border_brush());

  float percent = 0.0f;
  if (max_value > min_value) {
    percent = static_cast<float>(value - min_value) / static_cast<float>(max_value - min_value);
    percent = std::clamp(percent, 0.0f, 1.0f);
  }

  if (percent > 0.0f) {
    D2D1_ROUNDED_RECT active_track_rect = D2D1::RoundedRect(
        D2D1::RectF(cx, track_y, cx + width * percent, track_y + track_height),
        track_height / 2.0f, track_height / 2.0f);
    target->FillRoundedRectangle(&active_track_rect, shell.accent_brush());
  }

  float thumb_radius = 8.0f;
  float thumb_x = cx + width * percent;
  float thumb_y = cy + height / 2.0f;
  D2D1_ELLIPSE thumb_ellipse = D2D1::Ellipse(D2D1::Point2F(thumb_x, thumb_y), thumb_radius, thumb_radius);
  target->FillEllipse(&thumb_ellipse, shell.accent_brush());
}

}  // namespace clipd2d::ui
