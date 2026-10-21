#include "ui/settings/SettingsPanelGeneral.h"
#include "ui/settings/ISettingsHost.h"
#include "ui/settings/SettingsTypes.h"
#include "ui/settings/SettingsDrawUtil.h"
#include "ui/layout/FlexNode.h"
#include <algorithm>

namespace clipd2d::ui {

void SettingsPanelGeneral::BuildLayout(float width, float height, ISettingsHost* host) {
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
    auto r2_txt = std::make_shared<FlexNode>();
    r2_txt->FlexDirection(YGFlexDirectionColumn).JustifyContent(YGJustifyCenter);
    r2_->AddChild(r2_txt);
    r2_t_ = std::make_shared<FlexNode>(); r2_txt->AddChild(r2_t_);
    r2_s_ = std::make_shared<FlexNode>(); r2_txt->AddChild(r2_s_);
    r2_c_ = std::make_shared<FlexNode>(); r2_->AddChild(r2_c_);

    card2_ = std::make_shared<FlexNode>();
    root_->AddChild(card2_);

    r3_ = std::make_shared<FlexNode>();
    card2_->AddChild(r3_);
    auto r3_txt = std::make_shared<FlexNode>();
    r3_txt->FlexDirection(YGFlexDirectionColumn).JustifyContent(YGJustifyCenter);
    r3_->AddChild(r3_txt);
    r3_t_ = std::make_shared<FlexNode>(); r3_txt->AddChild(r3_t_);
    r3_s_ = std::make_shared<FlexNode>(); r3_txt->AddChild(r3_s_);
    r3_c_ = std::make_shared<FlexNode>(); r3_->AddChild(r3_c_);

    r4_ = std::make_shared<FlexNode>();
    card2_->AddChild(r4_);
    auto r4_txt = std::make_shared<FlexNode>();
    r4_txt->FlexDirection(YGFlexDirectionColumn).JustifyContent(YGJustifyCenter);
    r4_->AddChild(r4_txt);
    r4_t_ = std::make_shared<FlexNode>(); r4_txt->AddChild(r4_t_);
    r4_s_ = std::make_shared<FlexNode>(); r4_txt->AddChild(r4_s_);
    r4_c_ = std::make_shared<FlexNode>(); r4_->AddChild(r4_c_);

    card3_ = std::make_shared<FlexNode>();
    root_->AddChild(card3_);

    r5_ = std::make_shared<FlexNode>();
    card3_->AddChild(r5_);
    auto r5_txt = std::make_shared<FlexNode>();
    r5_txt->FlexDirection(YGFlexDirectionColumn).JustifyContent(YGJustifyCenter);
    r5_->AddChild(r5_txt);
    r5_t_ = std::make_shared<FlexNode>(); r5_txt->AddChild(r5_t_);
    r5_s_ = std::make_shared<FlexNode>(); r5_txt->AddChild(r5_s_);
    r5_c_ = std::make_shared<FlexNode>(); r5_->AddChild(r5_c_);

    r6_ = std::make_shared<FlexNode>();
    card3_->AddChild(r6_);
    auto r6_txt = std::make_shared<FlexNode>();
    r6_txt->FlexDirection(YGFlexDirectionColumn).JustifyContent(YGJustifyCenter);
    r6_->AddChild(r6_txt);
    r6_t_ = std::make_shared<FlexNode>(); r6_txt->AddChild(r6_t_);
    r6_s_ = std::make_shared<FlexNode>(); r6_txt->AddChild(r6_s_);
    r6_c_ = std::make_shared<FlexNode>(); r6_->AddChild(r6_c_);
  }

  root_->Width(width).Height(height).FlexDirection(YGFlexDirectionColumn);

  title_node_->Height(static_cast<float>(host->DipToPx(32.0f))).Margin(YGEdgeBottom, static_cast<float>(host->DipToPx(4.0f)));

  card1_->FlexDirection(YGFlexDirectionColumn).Margin(YGEdgeBottom, static_cast<float>(host->DipToPx(24.0f)));

  r1_->Height(static_cast<float>(host->DipToPx(60.0f))).Padding(YGEdgeHorizontal, padding).FlexDirection(YGFlexDirectionRow).AlignItems(YGAlignCenter).JustifyContent(YGJustifySpaceBetween);
  r1_t_->Height(static_cast<float>(host->DipToPx(18.0f))).Margin(YGEdgeBottom, static_cast<float>(host->DipToPx(2.0f)));
  r1_s_->Height(static_cast<float>(host->DipToPx(14.0f)));
  r1_c_->Width(static_cast<float>(host->DipToPx(180.0f))).Height(static_cast<float>(host->DipToPx(28.0f)));

  r2_->Height(static_cast<float>(host->DipToPx(60.0f))).Padding(YGEdgeHorizontal, padding).FlexDirection(YGFlexDirectionRow).AlignItems(YGAlignCenter).JustifyContent(YGJustifySpaceBetween);
  r2_t_->Height(static_cast<float>(host->DipToPx(18.0f))).Margin(YGEdgeBottom, static_cast<float>(host->DipToPx(2.0f)));
  r2_s_->Height(static_cast<float>(host->DipToPx(14.0f)));
  r2_c_->Width(static_cast<float>(host->DipToPx(180.0f))).Height(static_cast<float>(host->DipToPx(28.0f)));

  card2_->FlexDirection(YGFlexDirectionColumn).Margin(YGEdgeBottom, static_cast<float>(host->DipToPx(24.0f)));

  r3_->Height(static_cast<float>(host->DipToPx(60.0f))).Padding(YGEdgeHorizontal, padding).FlexDirection(YGFlexDirectionRow).AlignItems(YGAlignCenter).JustifyContent(YGJustifySpaceBetween);
  r3_t_->Height(static_cast<float>(host->DipToPx(18.0f))).Margin(YGEdgeBottom, static_cast<float>(host->DipToPx(2.0f)));
  r3_s_->Height(static_cast<float>(host->DipToPx(14.0f)));
  r3_c_->Width(static_cast<float>(host->DipToPx(44.0f))).Height(static_cast<float>(host->DipToPx(24.0f)));

  r4_->Height(static_cast<float>(host->DipToPx(60.0f))).Padding(YGEdgeHorizontal, padding).FlexDirection(YGFlexDirectionRow).AlignItems(YGAlignCenter).JustifyContent(YGJustifySpaceBetween);
  r4_t_->Height(static_cast<float>(host->DipToPx(18.0f))).Margin(YGEdgeBottom, static_cast<float>(host->DipToPx(2.0f)));
  r4_s_->Height(static_cast<float>(host->DipToPx(14.0f)));
  r4_c_->Width(static_cast<float>(host->DipToPx(44.0f))).Height(static_cast<float>(host->DipToPx(24.0f)));

  card3_->FlexDirection(YGFlexDirectionColumn).Margin(YGEdgeBottom, static_cast<float>(host->DipToPx(24.0f)));

  r5_->Height(static_cast<float>(host->DipToPx(60.0f))).Padding(YGEdgeHorizontal, padding).FlexDirection(YGFlexDirectionRow).AlignItems(YGAlignCenter).JustifyContent(YGJustifySpaceBetween);
  r5_t_->Height(static_cast<float>(host->DipToPx(18.0f))).Margin(YGEdgeBottom, static_cast<float>(host->DipToPx(2.0f)));
  r5_s_->Height(static_cast<float>(host->DipToPx(14.0f)));
  r5_c_->Width(static_cast<float>(host->DipToPx(44.0f))).Height(static_cast<float>(host->DipToPx(24.0f)));

  r6_->Height(static_cast<float>(host->DipToPx(60.0f))).Padding(YGEdgeHorizontal, padding).FlexDirection(YGFlexDirectionRow).AlignItems(YGAlignCenter).JustifyContent(YGJustifySpaceBetween);
  r6_t_->Height(static_cast<float>(host->DipToPx(18.0f))).Margin(YGEdgeBottom, static_cast<float>(host->DipToPx(2.0f)));
  r6_s_->Height(static_cast<float>(host->DipToPx(14.0f)));
  r6_c_->Width(static_cast<float>(host->DipToPx(44.0f))).Height(static_cast<float>(host->DipToPx(24.0f)));

  root_->CalculateLayout(width, YGUndefined);
  max_scroll_y_ = std::max(0.0f, root_->GetHeight() - height);
}

float SettingsPanelGeneral::Render(ID2D1HwndRenderTarget* target, const DraftState& draft, ISettingsHost* host, const RECT& content_rect) {
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

  DrawTextLine(target, host->GetShell().section_format(), host->GetShell().text_brush(), title_node_->GetAbsoluteRect(cx, cy), L"\u5e38\u89c4");
  FillRoundedPanel(target, card1_->GetAbsoluteRect(cx, cy), 16.0f, host->GetShell().surface_brush(), host->GetShell().border_brush(), 0.5f);
  FillRoundedPanel(target, card2_->GetAbsoluteRect(cx, cy), 16.0f, host->GetShell().surface_brush(), host->GetShell().border_brush(), 0.5f);

  DrawParagraph(target, host->GetShell().body_bold_format(), host->GetShell().text_brush(), r1_t_->GetAbsoluteRect(cx, cy), L"\u5386\u53f2\u8bb0\u5f55\u4e0a\u9650");
  DrawParagraph(target, host->GetShell().small_format(), host->GetShell().muted_text_brush(), r1_s_->GetAbsoluteRect(cx, cy), L"\u63a7\u5236\u4fdd\u7559\u7684\u6700\u5927\u6761\u76ee\u6570");
  RECT history_limit_rect = r1_c_->GetAbsoluteRect(cx, cy);
  host->RegisterFieldRect(FieldId::HistoryLimit, history_limit_rect);
  FillRoundedPanel(target, history_limit_rect, 10.0f, host->GetShell().surface_alt_brush(), host->GetShell().border_brush(), 0.5f);
  host->AddHitRegion({history_limit_rect, HitKind::Field, Section::General, FieldId::HistoryLimit, static_cast<int>(FieldId::HistoryLimit)});
  if (host->GetActiveField() != FieldId::HistoryLimit) {
    DrawTextLine(target, host->GetShell().small_format(), draft.history_limit_text.empty() ? host->GetShell().muted_text_brush() : host->GetShell().text_brush(), InsetRectCopy(history_limit_rect, host->DipToPx(10.0f), 0), draft.history_limit_text.empty() ? L"" : draft.history_limit_text);
  }
  DrawRowDivider(target, host->GetShell(), r1_->GetAbsoluteRect(cx, cy));

  DrawParagraph(target, host->GetShell().body_bold_format(), host->GetShell().text_brush(), r2_t_->GetAbsoluteRect(cx, cy), L"\u5386\u53f2\u4fdd\u7559\u5929\u6570");
  DrawParagraph(target, host->GetShell().small_format(), host->GetShell().muted_text_brush(), r2_s_->GetAbsoluteRect(cx, cy), L"\u8d85\u8fc7\u5929\u6570\u7684\u666e\u901a\u5386\u53f2\u662f\u81ea\u52a8\u6e05\u7406\uff0c0 \u8868\u793a\u4ec5\u6309\u6570\u91cf");
  RECT history_retention_days_rect = r2_c_->GetAbsoluteRect(cx, cy);
  host->RegisterFieldRect(FieldId::HistoryRetentionDays, history_retention_days_rect);
  FillRoundedPanel(target, history_retention_days_rect, 10.0f, host->GetShell().surface_alt_brush(), host->GetShell().border_brush(), 0.5f);
  host->AddHitRegion({history_retention_days_rect, HitKind::Field, Section::General, FieldId::HistoryRetentionDays, static_cast<int>(FieldId::HistoryRetentionDays)});
  if (host->GetActiveField() != FieldId::HistoryRetentionDays) {
    DrawTextLine(target, host->GetShell().small_format(), draft.history_retention_days_text.empty() ? host->GetShell().muted_text_brush() : host->GetShell().text_brush(), InsetRectCopy(history_retention_days_rect, host->DipToPx(10.0f), 0), draft.history_retention_days_text.empty() ? L"" : draft.history_retention_days_text);
  }

  DrawParagraph(target, host->GetShell().body_bold_format(), host->GetShell().text_brush(), r3_t_->GetAbsoluteRect(cx, cy), L"\u5f00\u673a\u81ea\u542f\u52a8");
  DrawParagraph(target, host->GetShell().small_format(), host->GetShell().muted_text_brush(), r3_s_->GetAbsoluteRect(cx, cy), L"\u767b\u5f55 Windows \u540e\u81ea\u52a8\u542f\u52a8 Scraps");
  RECT startup_toggle_rect = r3_c_->GetAbsoluteRect(cx, cy);
  DrawToggle(target, startup_toggle_rect, draft.start_with_windows, host->GetShell());
  host->AddHitRegion({startup_toggle_rect, HitKind::Toggle, Section::General, FieldId::None, 1});
  DrawRowDivider(target, host->GetShell(), r3_->GetAbsoluteRect(cx, cy));

  DrawParagraph(target, host->GetShell().body_bold_format(), host->GetShell().text_brush(), r4_t_->GetAbsoluteRect(cx, cy), L"\u9996\u6b21\u8fd0\u884c\u65f6\u6253\u5f00\u9762\u677f");
  DrawParagraph(target, host->GetShell().small_format(), host->GetShell().muted_text_brush(), r4_s_->GetAbsoluteRect(cx, cy), L"\u542f\u52a8\u540e\u81ea\u52a8\u5f39\u51fa\u4e3b\u9762\u677f\u4ee5\u786e\u8ba4\u8fd0\u884c\u72b6\u6001");
  RECT first_run_panel_toggle_rect = r4_c_->GetAbsoluteRect(cx, cy);
  DrawToggle(target, first_run_panel_toggle_rect, draft.open_panel_on_first_run, host->GetShell());
  host->AddHitRegion({first_run_panel_toggle_rect, HitKind::Toggle, Section::General, FieldId::None, 2});

  FillRoundedPanel(target, card3_->GetAbsoluteRect(cx, cy), 16.0f, host->GetShell().surface_brush(), host->GetShell().border_brush(), 0.5f);

  DrawParagraph(target, host->GetShell().body_bold_format(), host->GetShell().text_brush(), r5_t_->GetAbsoluteRect(cx, cy), L"\u542f\u7528\u5c4f\u5e55\u622a\u56fe");
  DrawParagraph(target, host->GetShell().small_format(), host->GetShell().muted_text_brush(), r5_s_->GetAbsoluteRect(cx, cy), L"\u542f\u7528\u6216\u7981\u7528\u5168\u5c40\u5c4f\u5e55\u622a\u56fe\u529f\u80fd\u53ca\u70ed\u952e");
  RECT screenshot_toggle_rect = r5_c_->GetAbsoluteRect(cx, cy);
  DrawToggle(target, screenshot_toggle_rect, draft.screenshot_enabled, host->GetShell());
  host->AddHitRegion({screenshot_toggle_rect, HitKind::Toggle, Section::General, FieldId::None, 16});
  DrawRowDivider(target, host->GetShell(), r5_->GetAbsoluteRect(cx, cy));

  DrawParagraph(target, host->GetShell().body_bold_format(), host->GetShell().text_brush(), r6_t_->GetAbsoluteRect(cx, cy), L"\u5e38\u9a7b\u540e\u53f0\u9884\u70ed");
  DrawParagraph(target, host->GetShell().small_format(), host->GetShell().muted_text_brush(), r6_s_->GetAbsoluteRect(cx, cy), L"\u5f00\u542f\u540e\u622a\u56fe\u7a0b\u5e8f\u5e38\u9a7b\u540e\u53f0\uff0c\u8d77\u59cb\u54cd\u5e94\u5ef6\u8fdf\u5c0f\u4e8e 10ms");
  RECT prewarm_toggle_rect = r6_c_->GetAbsoluteRect(cx, cy);
  DrawToggle(target, prewarm_toggle_rect, draft.screenshot_prewarm, host->GetShell());
  host->AddHitRegion({prewarm_toggle_rect, HitKind::Toggle, Section::General, FieldId::None, 17});

  return max_scroll_y_;
}

}  // namespace clipd2d::ui
