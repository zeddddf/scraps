#include "ui/settings/SettingsPanelClipboard.h"
#include "ui/settings/ISettingsHost.h"
#include "ui/settings/SettingsTypes.h"
#include "ui/settings/SettingsDrawUtil.h"
#include "ui/layout/FlexNode.h"
#include <algorithm>

namespace clipd2d::ui {

void SettingsPanelClipboard::BuildLayout(float width, float height, ISettingsHost* host) {
  float padding = static_cast<float>(host->DipToPx(24.0f));

  if (!root_) {
    root_ = std::make_shared<FlexNode>();
    title_node_ = std::make_shared<FlexNode>();
    root_->AddChild(title_node_);

    card1_ = std::make_shared<FlexNode>();
    root_->AddChild(card1_);

    r1_ = std::make_shared<FlexNode>();
    card1_->AddChild(r1_);
    auto r1_txt = std::make_shared<FlexNode>();
    r1_txt->FlexDirection(YGFlexDirectionColumn).JustifyContent(YGJustifyCenter);
    r1_->AddChild(r1_txt);
    r1_t_ = std::make_shared<FlexNode>(); r1_txt->AddChild(r1_t_);
    r1_s_ = std::make_shared<FlexNode>(); r1_txt->AddChild(r1_s_);
    r1_c_ = std::make_shared<FlexNode>(); r1_->AddChild(r1_c_);

    r2_ = std::make_shared<FlexNode>();
    card1_->AddChild(r2_);
    r2_txt_ = std::make_shared<FlexNode>(); r2_txt_->FlexDirection(YGFlexDirectionColumn).JustifyContent(YGJustifyCenter); r2_->AddChild(r2_txt_);
    r2_t_ = std::make_shared<FlexNode>(); r2_txt_->AddChild(r2_t_);
    r2_c_ = std::make_shared<FlexNode>(); r2_->AddChild(r2_c_);

    r3_ = std::make_shared<FlexNode>();
    card1_->AddChild(r3_);
    r3_txt_ = std::make_shared<FlexNode>(); r3_txt_->FlexDirection(YGFlexDirectionColumn).JustifyContent(YGJustifyCenter); r3_->AddChild(r3_txt_);
    r3_t_ = std::make_shared<FlexNode>(); r3_txt_->AddChild(r3_t_);
    r3_c_ = std::make_shared<FlexNode>(); r3_->AddChild(r3_c_);

    r4_ = std::make_shared<FlexNode>();
    card1_->AddChild(r4_);
    auto r4_txt = std::make_shared<FlexNode>();
    r4_txt->FlexDirection(YGFlexDirectionColumn).JustifyContent(YGJustifyCenter);
    r4_->AddChild(r4_txt);
    r4_t_ = std::make_shared<FlexNode>(); r4_txt->AddChild(r4_t_);
    r4_s_ = std::make_shared<FlexNode>(); r4_txt->AddChild(r4_s_);
    r4_c_ = std::make_shared<FlexNode>(); r4_->AddChild(r4_c_);

    r5_ = std::make_shared<FlexNode>();
    card1_->AddChild(r5_);
    auto r5_txt = std::make_shared<FlexNode>();
    r5_txt->FlexDirection(YGFlexDirectionColumn).JustifyContent(YGJustifyCenter);
    r5_->AddChild(r5_txt);
    r5_t_ = std::make_shared<FlexNode>(); r5_txt->AddChild(r5_t_);
    r5_s_ = std::make_shared<FlexNode>(); r5_txt->AddChild(r5_s_);
    r5_c_ = std::make_shared<FlexNode>(); r5_->AddChild(r5_c_);

    r6_ = std::make_shared<FlexNode>();
    card1_->AddChild(r6_);
    r6_txt_ = std::make_shared<FlexNode>(); r6_txt_->FlexDirection(YGFlexDirectionColumn).JustifyContent(YGJustifyCenter); r6_->AddChild(r6_txt_);
    r6_t_ = std::make_shared<FlexNode>(); r6_txt_->AddChild(r6_t_);
    r6_c_ = std::make_shared<FlexNode>(); r6_->AddChild(r6_c_);
  }

  root_->Width(width).Height(height).FlexDirection(YGFlexDirectionColumn);

  title_node_->Height(static_cast<float>(host->DipToPx(32.0f))).Margin(YGEdgeBottom, static_cast<float>(host->DipToPx(4.0f)));

  card1_->FlexDirection(YGFlexDirectionColumn).Margin(YGEdgeBottom, static_cast<float>(host->DipToPx(24.0f)));

  r1_->Height(static_cast<float>(host->DipToPx(60.0f))).Padding(YGEdgeHorizontal, padding).FlexDirection(YGFlexDirectionRow).AlignItems(YGAlignCenter).JustifyContent(YGJustifySpaceBetween);
  r1_t_->Height(static_cast<float>(host->DipToPx(18.0f))).Margin(YGEdgeBottom, static_cast<float>(host->DipToPx(2.0f)));
  r1_s_->Height(static_cast<float>(host->DipToPx(14.0f)));
  r1_c_->Width(static_cast<float>(host->DipToPx(44.0f))).Height(static_cast<float>(host->DipToPx(24.0f)));

  r2_->Height(static_cast<float>(host->DipToPx(60.0f))).Padding(YGEdgeHorizontal, padding).FlexDirection(YGFlexDirectionRow).AlignItems(YGAlignCenter).JustifyContent(YGJustifySpaceBetween);
  r2_t_->Height(static_cast<float>(host->DipToPx(18.0f)));
  r2_c_->Width(static_cast<float>(host->DipToPx(180.0f))).Height(static_cast<float>(host->DipToPx(28.0f)));

  r3_->Height(static_cast<float>(host->DipToPx(60.0f))).Padding(YGEdgeHorizontal, padding).FlexDirection(YGFlexDirectionRow).AlignItems(YGAlignCenter).JustifyContent(YGJustifySpaceBetween);
  r3_t_->Height(static_cast<float>(host->DipToPx(18.0f)));
  r3_c_->Width(static_cast<float>(host->DipToPx(44.0f))).Height(static_cast<float>(host->DipToPx(24.0f)));

  r4_->Height(static_cast<float>(host->DipToPx(60.0f))).Padding(YGEdgeHorizontal, padding).FlexDirection(YGFlexDirectionRow).AlignItems(YGAlignCenter).JustifyContent(YGJustifySpaceBetween);
  r4_t_->Height(static_cast<float>(host->DipToPx(18.0f))).Margin(YGEdgeBottom, static_cast<float>(host->DipToPx(2.0f)));
  r4_s_->Height(static_cast<float>(host->DipToPx(14.0f)));
  r4_c_->Width(static_cast<float>(host->DipToPx(180.0f))).Height(static_cast<float>(host->DipToPx(28.0f)));

  r5_->Height(static_cast<float>(host->DipToPx(60.0f))).Padding(YGEdgeHorizontal, padding).FlexDirection(YGFlexDirectionRow).AlignItems(YGAlignCenter).JustifyContent(YGJustifySpaceBetween);
  r5_t_->Height(static_cast<float>(host->DipToPx(18.0f))).Margin(YGEdgeBottom, static_cast<float>(host->DipToPx(2.0f)));
  r5_s_->Height(static_cast<float>(host->DipToPx(14.0f)));
  r5_c_->Width(static_cast<float>(host->DipToPx(44.0f))).Height(static_cast<float>(host->DipToPx(24.0f)));

  r6_->Height(static_cast<float>(host->DipToPx(60.0f))).Padding(YGEdgeHorizontal, padding).FlexDirection(YGFlexDirectionRow).AlignItems(YGAlignCenter).JustifyContent(YGJustifySpaceBetween);
  r6_t_->Height(static_cast<float>(host->DipToPx(18.0f)));
  r6_c_->Width(static_cast<float>(host->DipToPx(44.0f))).Height(static_cast<float>(host->DipToPx(24.0f)));

  root_->CalculateLayout(width, YGUndefined);
  max_scroll_y_ = std::max(0.0f, root_->GetHeight() - height);
}

float SettingsPanelClipboard::Render(ID2D1HwndRenderTarget* target, const DraftState& draft, ISettingsHost* host, const RECT& content_rect) {
  float width = static_cast<float>(content_rect.right - content_rect.left);
  float height = static_cast<float>(content_rect.bottom - content_rect.top);
  int scale_key = host->DipToPx(100);

  if (width != last_width_ || height != last_height_ || scale_key != last_scale_key_ || !root_) {
    BuildLayout(width, height, host);
    last_width_ = width;
    last_height_ = height;
    last_scale_key_ = scale_key;
  }

  float cx = static_cast<float>(content_rect.left);
  float cy = static_cast<float>(content_rect.top) - host->GetScrollY();

  DrawTextLine(target, host->GetShell().section_format(), host->GetShell().text_brush(), title_node_->GetAbsoluteRect(cx, cy), L"\u526a\u8d34\u677f");
  FillRoundedPanel(target, card1_->GetAbsoluteRect(cx, cy), 16.0f, host->GetShell().surface_brush(), host->GetShell().border_brush(), 0.5f);

  DrawParagraph(target, host->GetShell().body_bold_format(), host->GetShell().text_brush(), r1_t_->GetAbsoluteRect(cx, cy), L"\u542f\u7528\u526a\u8d34\u677f\u76d1\u542c");
  DrawParagraph(target, host->GetShell().small_format(), host->GetShell().muted_text_brush(), r1_s_->GetAbsoluteRect(cx, cy), L"\u5168\u5c40\u76d1\u542c\u5e76\u4fdd\u5b58\u526a\u8d34\u677f\u53d8\u5316");
  RECT monitoring_toggle_rect = r1_c_->GetAbsoluteRect(cx, cy);
  DrawToggle(target, monitoring_toggle_rect, draft.monitoring_enabled, host->GetShell());
  host->AddHitRegion({monitoring_toggle_rect, HitKind::Toggle, Section::Clipboard, FieldId::None, 4});
  DrawRowDivider(target, host->GetShell(), r1_->GetAbsoluteRect(cx, cy));

  DrawParagraph(target, host->GetShell().body_bold_format(), host->GetShell().text_brush(), r2_t_->GetAbsoluteRect(cx, cy), L"\u7a97\u53e3\u4f4d\u7f6e\u6a21\u5f0f");
  RECT position_select_rect = r2_c_->GetAbsoluteRect(cx, cy);
  host->RegisterSelectRect(SelectField::WindowPositionMode, position_select_rect);
  DrawSelectTrigger(target, host->GetShell(), position_select_rect, host->GetSelectLabel(SelectField::WindowPositionMode), host->GetActiveSelectField() == SelectField::WindowPositionMode);
  host->AddHitRegion({position_select_rect, HitKind::SelectTrigger, Section::Clipboard, FieldId::None, static_cast<int>(SelectField::WindowPositionMode)});
  DrawRowDivider(target, host->GetShell(), r2_->GetAbsoluteRect(cx, cy));

  DrawParagraph(target, host->GetShell().body_bold_format(), host->GetShell().text_brush(), r3_t_->GetAbsoluteRect(cx, cy), L"\u6253\u5f00\u65f6\u805a\u7126\u641c\u7d22\u6846");
  RECT focus_search_toggle_rect = r3_c_->GetAbsoluteRect(cx, cy);
  DrawToggle(target, focus_search_toggle_rect, draft.focus_search_on_open, host->GetShell());
  host->AddHitRegion({focus_search_toggle_rect, HitKind::Toggle, Section::Clipboard, FieldId::None, 5});
  DrawRowDivider(target, host->GetShell(), r3_->GetAbsoluteRect(cx, cy));

  DrawParagraph(target, host->GetShell().body_bold_format(), host->GetShell().text_brush(), r4_t_->GetAbsoluteRect(cx, cy), L"\u5355\u51fb\u7c98\u8d34\u6a21\u5f0f");
  DrawParagraph(target, host->GetShell().small_format(), host->GetShell().muted_text_brush(), r4_s_->GetAbsoluteRect(cx, cy), L"\u70b9\u51fb\u5217\u8868\u9879\u65f6\u7684\u9ed8\u8ba4\u884c\u4e3a");
  RECT click_paste_select_rect = r4_c_->GetAbsoluteRect(cx, cy);
  host->RegisterSelectRect(SelectField::ClickPasteMode, click_paste_select_rect);
  DrawSelectTrigger(target, host->GetShell(), click_paste_select_rect, host->GetSelectLabel(SelectField::ClickPasteMode), host->GetActiveSelectField() == SelectField::ClickPasteMode);
  host->AddHitRegion({click_paste_select_rect, HitKind::SelectTrigger, Section::Clipboard, FieldId::None, static_cast<int>(SelectField::ClickPasteMode)});
  DrawRowDivider(target, host->GetShell(), r4_->GetAbsoluteRect(cx, cy));

  DrawParagraph(target, host->GetShell().body_bold_format(), host->GetShell().text_brush(), r5_t_->GetAbsoluteRect(cx, cy), L"\u590d\u7528\u540e\u7f6e\u9876");
  DrawParagraph(target, host->GetShell().small_format(), host->GetShell().muted_text_brush(), r5_s_->GetAbsoluteRect(cx, cy), L"\u7c98\u8d34\u5386\u53f2\u8bb0\u5f55\u540e\u5c06\u5176\u63d0\u5347\u81f3\u5217\u8868\u9879\u9876\u90e8");
  RECT promote_pasted_clip_toggle_rect = r5_c_->GetAbsoluteRect(cx, cy);
  DrawToggle(target, promote_pasted_clip_toggle_rect, draft.promote_pasted_clip_on_reuse, host->GetShell());
  host->AddHitRegion({promote_pasted_clip_toggle_rect, HitKind::Toggle, Section::Clipboard, FieldId::None, 10});
  DrawRowDivider(target, host->GetShell(), r5_->GetAbsoluteRect(cx, cy));

  DrawParagraph(target, host->GetShell().body_bold_format(), host->GetShell().text_brush(), r6_t_->GetAbsoluteRect(cx, cy), L"\u81ea\u52a8\u5f39\u51fa\u961f\u5217");
  RECT queue_auto_pop_toggle_rect = r6_c_->GetAbsoluteRect(cx, cy);
  DrawToggle(target, queue_auto_pop_toggle_rect, draft.queue_auto_pop, host->GetShell());
  host->AddHitRegion({queue_auto_pop_toggle_rect, HitKind::Toggle, Section::Clipboard, FieldId::None, 6});

  return max_scroll_y_;
}

}  // namespace clipd2d::ui
