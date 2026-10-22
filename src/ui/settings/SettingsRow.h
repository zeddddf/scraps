#pragma once

// P3-16 SettingRow 声明式行视图：把设置面板 Render 中重复的
// 「标题/副题 + GetAbsoluteRect + 控件 + AddHitRegion + 分隔线」
// 样板收敛为一行声明 + 一个渲染循环。布局节点（FlexNode）仍由面板 BuildLayout
// 持有；本结构只描述"这一行画什么、值多少、命中是什么"，命中 ID/分区/option
// 与现行值逐项保持一致，SettingsWindow 的命中分发零变化。
//
// 绘制语义与旧手写代码逐像素等价：
//  - Field/FieldToggle 输入框：RegisterFieldRect 先行 → FillRoundedPanel(radius,
//    surface_alt, border, 0.5) → AddHitRegion(option=(int)field_id) →
//    仅非激活态 DrawTextLine（空值走 muted+placeholder）。
//  - Select：RegisterSelectRect 先行 → DrawSelectTrigger →
//    AddHitRegion(option=(int)select_field, field=None)。
//  - Slider：DrawSlider → AddHitRegion(field/option 由面板传入，
//    如 Appearance 的 EnableWindowBlur/18 是拖拽身份标识）。
//  - Action：FillRoundedPanel(6, surface_alt) → 居中 DrawTextLine（危险色可选）
//    → AddHitRegion({rect, kind})（section/field/option 取 HitRegion 默认值）。
//  - Toggle：DrawToggle → AddHitRegion(field=None)。

#include <windows.h>
#include <d2d1.h>
#include <dwrite.h>

#include <cwctype>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "ui/layout/FlexNode.h"
#include "ui/settings/ISettingsHost.h"
#include "ui/settings/SettingsDrawUtil.h"
#include "ui/settings/SettingsTypes.h"

namespace clipd2d::ui {

struct SettingRow {
  enum class Control { Info, Toggle, Field, FieldToggle, Select, Slider, Action };
  // 标题/副题绘制函数：Paragraph=DrawParagraph（默认）；Line=DrawTextLine
  // （Data 统计行、About 接管行的旧行为，单行裁剪不换行）。
  enum class TextMode { Paragraph, Line };

  Control control = Control::Toggle;
  TextMode text_mode = TextMode::Paragraph;
  // 布局节点（BuildLayout 创建；空节点跳过对应绘制）
  FlexNode* row = nullptr;
  FlexNode* title = nullptr;
  FlexNode* subtitle = nullptr;
  FlexNode* control_node = nullptr;
  FlexNode* input_node = nullptr;   // FieldToggle：文本输入区
  FlexNode* toggle_node = nullptr;  // FieldToggle：开关区
  // 文本
  const wchar_t* title_text = L"";
  const wchar_t* subtitle_text = L"";
  float title_pad_left_dip = 0.0f;  // >0 时标题画在 row 矩形上并左缩（About 接管行）
  // 命中（保持现行值；Toggle/Select 的 field 恒为 None，option 见各分支）
  HitKind hit_kind = HitKind::Toggle;
  Section section = Section::General;
  FieldId field_id = FieldId::None;
  SelectField select_field = SelectField::None;
  int hit_option = 0;
  bool divider = true;
  // Toggle
  bool toggle_value = false;
  // Field / FieldToggle 输入框
  std::wstring field_text;
  const wchar_t* field_placeholder = L"";
  float field_radius = 10.0f;
  float field_inset_dip = 10.0f;
  // Slider
  int slider_min = 0;
  int slider_max = 100;
  int slider_value = 0;
  // Action
  const wchar_t* action_label = L"";
  bool action_danger = false;
};

// ── 行构造工厂（面板声明处调用）────────────────────────────────────────

inline SettingRow MakeInfoRow(
    const std::shared_ptr<FlexNode>& row, const std::shared_ptr<FlexNode>& title,
    const std::shared_ptr<FlexNode>& subtitle,
    const wchar_t* title_text, const wchar_t* subtitle_text,
    bool divider = true) {
  SettingRow r;
  r.control = SettingRow::Control::Info;
  r.row = row.get(); r.title = title.get(); r.subtitle = subtitle.get();
  r.title_text = title_text; r.subtitle_text = subtitle_text;
  r.divider = divider;
  return r;
}

inline SettingRow MakeToggleRow(
    const std::shared_ptr<FlexNode>& row, const std::shared_ptr<FlexNode>& title,
    const std::shared_ptr<FlexNode>& subtitle, const std::shared_ptr<FlexNode>& control,
    const wchar_t* title_text, const wchar_t* subtitle_text, bool value,
    Section section, int hit_option, bool divider = true) {
  SettingRow r;
  r.control = SettingRow::Control::Toggle;
  r.row = row.get(); r.title = title.get(); r.subtitle = subtitle.get(); r.control_node = control.get();
  r.title_text = title_text; r.subtitle_text = subtitle_text;
  r.hit_kind = HitKind::Toggle; r.section = section; r.hit_option = hit_option;
  r.divider = divider; r.toggle_value = value;
  return r;
}

inline SettingRow MakeFieldRow(
    const std::shared_ptr<FlexNode>& row, const std::shared_ptr<FlexNode>& title,
    const std::shared_ptr<FlexNode>& subtitle, const std::shared_ptr<FlexNode>& control,
    const wchar_t* title_text, const wchar_t* subtitle_text,
    std::wstring field_text, Section section, FieldId field_id,
    bool divider = true, const wchar_t* placeholder = L"",
    float radius = 10.0f, float inset_dip = 10.0f) {
  SettingRow r;
  r.control = SettingRow::Control::Field;
  r.row = row.get(); r.title = title.get(); r.subtitle = subtitle.get(); r.control_node = control.get();
  r.title_text = title_text; r.subtitle_text = subtitle_text;
  r.hit_kind = HitKind::Field; r.section = section; r.field_id = field_id;
  r.divider = divider; r.field_text = std::move(field_text);
  r.field_placeholder = placeholder; r.field_radius = radius; r.field_inset_dip = inset_dip;
  return r;
}

inline SettingRow MakeFieldToggleRow(
    const std::shared_ptr<FlexNode>& row, const std::shared_ptr<FlexNode>& title,
    const std::shared_ptr<FlexNode>& subtitle,
    const std::shared_ptr<FlexNode>& input_node, const std::shared_ptr<FlexNode>& toggle_node,
    const wchar_t* title_text, const wchar_t* subtitle_text,
    std::wstring field_text, bool toggle_value,
    Section section, FieldId field_id, int toggle_hit_option,
    bool divider = true, const wchar_t* placeholder = L"",
    float radius = 10.0f, float inset_dip = 10.0f) {
  SettingRow r;
  r.control = SettingRow::Control::FieldToggle;
  r.row = row.get(); r.title = title.get(); r.subtitle = subtitle.get();
  r.input_node = input_node.get(); r.toggle_node = toggle_node.get();
  r.title_text = title_text; r.subtitle_text = subtitle_text;
  r.hit_kind = HitKind::Field; r.section = section; r.field_id = field_id;
  r.hit_option = toggle_hit_option;
  r.divider = divider; r.field_text = std::move(field_text); r.toggle_value = toggle_value;
  r.field_placeholder = placeholder; r.field_radius = radius; r.field_inset_dip = inset_dip;
  return r;
}

inline SettingRow MakeSelectRow(
    const std::shared_ptr<FlexNode>& row, const std::shared_ptr<FlexNode>& title,
    const std::shared_ptr<FlexNode>& subtitle, const std::shared_ptr<FlexNode>& control,
    const wchar_t* title_text, const wchar_t* subtitle_text,
    Section section, SelectField select_field, bool divider = true) {
  SettingRow r;
  r.control = SettingRow::Control::Select;
  r.row = row.get(); r.title = title.get(); r.subtitle = subtitle.get(); r.control_node = control.get();
  r.title_text = title_text; r.subtitle_text = subtitle_text;
  r.hit_kind = HitKind::SelectTrigger; r.section = section;
  r.select_field = select_field;
  r.divider = divider;
  return r;
}

inline SettingRow MakeSliderRow(
    const std::shared_ptr<FlexNode>& row, const std::shared_ptr<FlexNode>& title,
    const std::shared_ptr<FlexNode>& subtitle, const std::shared_ptr<FlexNode>& control,
    const wchar_t* title_text, const wchar_t* subtitle_text,
    int min_value, int max_value, int value,
    Section section, FieldId field_id, int hit_option, bool divider = true) {
  SettingRow r;
  r.control = SettingRow::Control::Slider;
  r.row = row.get(); r.title = title.get(); r.subtitle = subtitle.get(); r.control_node = control.get();
  r.title_text = title_text; r.subtitle_text = subtitle_text;
  r.hit_kind = HitKind::SliderDrag; r.section = section;
  r.field_id = field_id; r.hit_option = hit_option;
  r.divider = divider;
  r.slider_min = min_value; r.slider_max = max_value; r.slider_value = value;
  return r;
}

inline SettingRow MakeActionRow(
    const std::shared_ptr<FlexNode>& row, const std::shared_ptr<FlexNode>& title,
    const std::shared_ptr<FlexNode>& subtitle, const std::shared_ptr<FlexNode>& control,
    const wchar_t* title_text, const wchar_t* subtitle_text,
    const wchar_t* action_label, HitKind action_kind, bool danger,
    bool divider = true) {
  SettingRow r;
  r.control = SettingRow::Control::Action;
  r.row = row.get(); r.title = title.get(); r.subtitle = subtitle.get(); r.control_node = control.get();
  r.title_text = title_text; r.subtitle_text = subtitle_text;
  r.hit_kind = action_kind;
  r.divider = divider; r.action_label = action_label; r.action_danger = danger;
  return r;
}

// 「恢复本节默认」统一行：点击后仅重置草稿，仍需点保存生效。
inline SettingRow MakeResetRow(
    const std::shared_ptr<FlexNode>& row, const std::shared_ptr<FlexNode>& title,
    const std::shared_ptr<FlexNode>& subtitle, const std::shared_ptr<FlexNode>& control,
    Section section) {
  SettingRow r = MakeActionRow(row, title, subtitle, control,
                               L"\u6062\u590d\u672c\u8282\u9ed8\u8ba4",
                               L"\u5c06\u672c\u8282\u8bbe\u7f6e\u6062\u590d\u4e3a\u7a0b\u5e8f\u9ed8\u8ba4\u503c\uff0c\u4fdd\u5b58\u540e\u751f\u6548",
                               L"\u91cd\u7f6e", HitKind::ActionResetSection, false);
  r.section = section;
  r.hit_option = static_cast<int>(section);
  r.divider = false;  // 恢复行固定是各卡片最后一行，不出分隔线
  return r;
}

// ── 章节 / 面板底 / 单行渲染 ──────────────────────────────────────────

inline void DrawSettingSectionTitle(
    ID2D1HwndRenderTarget* target, ISettingsHost* host,
    float cx, float cy, const std::shared_ptr<FlexNode>& node, const wchar_t* text) {
  (void)target;
  (void)cx;
  (void)cy;
  if (node == nullptr) return;
  // B3 粘性节标题：延迟到宿主统一绘制（见 QueueStickyTitle 注释）。
  host->QueueStickyTitle(node.get(), text);
}

inline void DrawSettingPanelBox(
    ID2D1HwndRenderTarget* target, ISettingsHost* host,
    float cx, float cy, const std::shared_ptr<FlexNode>& node) {
  if (node == nullptr) return;
  FillRoundedPanel(target, node->GetAbsoluteRect(cx, cy), 16.0f,
                   host->GetShell().surface_brush(), host->GetShell().border_brush(), 0.5f);
}

namespace settings_row_detail {

inline void DrawRowText(
    ID2D1HwndRenderTarget* target, SettingRow::TextMode mode,
    IDWriteTextFormat* format, ID2D1Brush* brush, const RECT& rect, std::wstring_view text) {
  if (mode == SettingRow::TextMode::Line) {
    DrawTextLine(target, format, brush, rect, text);
  } else {
    DrawParagraph(target, format, brush, rect, text);
  }
}

// Field / FieldToggle 输入框共用画法（与旧代码逐像素等价）。
inline void DrawFieldInput(
    ID2D1HwndRenderTarget* target, ISettingsHost* host, const RECT& rect, const SettingRow& r) {
  D2dShell& shell = host->GetShell();
  host->RegisterFieldRect(r.field_id, rect);
  FillRoundedPanel(target, rect, r.field_radius, shell.surface_alt_brush(), shell.border_brush(), 0.5f);
  host->AddHitRegion({rect, HitKind::Field, r.section, r.field_id, static_cast<int>(r.field_id)});
  if (host->GetActiveField() != r.field_id) {
    const bool empty = r.field_text.empty();
    DrawTextLine(target, shell.small_format(), empty ? shell.muted_text_brush() : shell.text_brush(),
                 InsetRectCopy(rect, host->DipToPx(r.field_inset_dip), 0),
                 empty ? std::wstring_view(r.field_placeholder) : std::wstring_view(r.field_text));
  }
}

}  // namespace settings_row_detail

inline void DrawSettingRow(
    ID2D1HwndRenderTarget* target, ISettingsHost* host,
    float cx, float cy, const SettingRow& r) {
  D2dShell& shell = host->GetShell();
  if (r.title != nullptr && r.title_text != nullptr && r.title_text[0] != L'\0') {
    RECT title_rect = r.title->GetAbsoluteRect(cx, cy);
    if (r.title_pad_left_dip > 0.0f) {
      title_rect.left += host->DipToPx(r.title_pad_left_dip);
    }
    settings_row_detail::DrawRowText(target, r.text_mode, shell.body_bold_format(),
                                     shell.text_brush(), title_rect, r.title_text);
  }
  if (r.subtitle != nullptr && r.subtitle_text != nullptr && r.subtitle_text[0] != L'\0') {
    settings_row_detail::DrawRowText(target, r.text_mode, shell.small_format(),
                                     shell.muted_text_brush(), r.subtitle->GetAbsoluteRect(cx, cy),
                                     r.subtitle_text);
  }

  switch (r.control) {
    case SettingRow::Control::Info:
      break;
    case SettingRow::Control::Toggle: {
      if (r.control_node == nullptr) break;
      RECT rect = r.control_node->GetAbsoluteRect(cx, cy);
      DrawToggle(target, rect, r.toggle_value, shell);
      host->AddHitRegion({rect, HitKind::Toggle, r.section, FieldId::None, r.hit_option});
      break;
    }
    case SettingRow::Control::Field: {
      if (r.control_node == nullptr) break;
      settings_row_detail::DrawFieldInput(target, host, r.control_node->GetAbsoluteRect(cx, cy), r);
      break;
    }
    case SettingRow::Control::FieldToggle: {
      if (r.input_node != nullptr) {
        settings_row_detail::DrawFieldInput(target, host, r.input_node->GetAbsoluteRect(cx, cy), r);
      }
      if (r.toggle_node != nullptr) {
        RECT rect = r.toggle_node->GetAbsoluteRect(cx, cy);
        DrawToggle(target, rect, r.toggle_value, shell);
        host->AddHitRegion({rect, HitKind::Toggle, r.section, FieldId::None, r.hit_option});
      }
      break;
    }
    case SettingRow::Control::Select: {
      if (r.control_node == nullptr) break;
      RECT rect = r.control_node->GetAbsoluteRect(cx, cy);
      host->RegisterSelectRect(r.select_field, rect);
      DrawSelectTrigger(target, shell, rect, host->GetSelectLabel(r.select_field),
                        host->GetActiveSelectField() == r.select_field);
      host->AddHitRegion({rect, HitKind::SelectTrigger, r.section, FieldId::None,
                          static_cast<int>(r.select_field)});
      break;
    }
    case SettingRow::Control::Slider: {
      if (r.control_node == nullptr) break;
      RECT rect = r.control_node->GetAbsoluteRect(cx, cy);
      DrawSlider(target, shell, rect, r.slider_min, r.slider_max, r.slider_value);
      host->AddHitRegion({rect, HitKind::SliderDrag, r.section, r.field_id, r.hit_option});
      break;
    }
    case SettingRow::Control::Action: {
      if (r.control_node == nullptr) break;
      RECT rect = r.control_node->GetAbsoluteRect(cx, cy);
      FillRoundedPanel(target, rect, 6.0f, shell.surface_alt_brush(), shell.border_brush(), 0.5f);
      DrawTextLine(target, shell.small_format(),
                   r.action_danger ? shell.danger_brush() : shell.text_brush(),
                   rect, r.action_label, DWRITE_TEXT_ALIGNMENT_CENTER);
      host->AddHitRegion({rect, r.hit_kind, r.section, r.field_id, r.hit_option});
      break;
    }
  }

  if (r.divider && r.row != nullptr) {
    DrawRowDivider(target, shell, r.row->GetAbsoluteRect(cx, cy));
  }
}

inline void DrawSettingRows(
    ID2D1HwndRenderTarget* target, ISettingsHost* host,
    float cx, float cy, const std::vector<SettingRow>& rows) {
  for (const auto& r : rows) {
    DrawSettingRow(target, host, cx, cy, r);
  }
}

// ── 设置搜索：行匹配 / 过滤 / 空态 ──────────────────────────────────────
// 行标题与副题文本是搜索的唯一索引来源，避免和章节关键词表维护两份。

namespace settings_row_detail {

inline std::wstring ToLowerCopy(std::wstring_view text) {
  std::wstring out(text);
  for (auto& c : out) {
    c = static_cast<wchar_t>(std::towlower(static_cast<wint_t>(c)));
  }
  return out;
}

inline std::wstring TrimCopy(std::wstring_view text) {
  size_t begin = 0;
  size_t end = text.size();
  while (begin < end && std::iswspace(static_cast<wint_t>(text[begin])) != 0) {
    ++begin;
  }
  while (end > begin && std::iswspace(static_cast<wint_t>(text[end - 1])) != 0) {
    --end;
  }
  return std::wstring(text.substr(begin, end - begin));
}

inline bool ContainsInsensitive(std::wstring_view haystack, std::wstring_view needle) {
  if (needle.empty()) {
    return true;
  }
  return ToLowerCopy(haystack).find(ToLowerCopy(needle)) != std::wstring::npos;
}

}  // namespace settings_row_detail

inline bool SettingRowMatches(const SettingRow& r, std::wstring_view query) {
  if (query.empty()) {
    return true;
  }
  if (r.title_text != nullptr && settings_row_detail::ContainsInsensitive(r.title_text, query)) {
    return true;
  }
  if (r.subtitle_text != nullptr && settings_row_detail::ContainsInsensitive(r.subtitle_text, query)) {
    return true;
  }
  return false;
}

// 面板 Render 的布局前置：需要时重算 Yoga 布局。
// 搜索定位不再折叠行——面板始终完整渲染，跳转由宿主滚动到目标行完成。
inline void PrepareSettingRows(
    FlexNode* root, float width, float height, float* max_scroll_y,
    bool force_relayout) {
  if (root != nullptr && force_relayout) {
    root->CalculateLayout(width, YGUndefined);
    if (max_scroll_y != nullptr) {
      *max_scroll_y = std::max(0.0f, root->GetHeight() - height);
    }
  }
}

}  // namespace clipd2d::ui
