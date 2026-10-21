#include "ui/settings/SettingsPanelHotkey.h"
#include "ui/settings/ISettingsHost.h"
#include "ui/settings/SettingsTypes.h"
#include "ui/settings/SettingsDrawUtil.h"
#include "ui/layout/FlexNode.h"
#include <algorithm>

namespace clipd2d::ui {

void SettingsPanelHotkey::BuildLayout(float width, float height, ISettingsHost* host) {
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

    r3_ = std::make_shared<FlexNode>();
    card1_->AddChild(r3_);
    auto r3_txt = std::make_shared<FlexNode>();
    r3_txt->FlexDirection(YGFlexDirectionColumn).JustifyContent(YGJustifyCenter);
    r3_->AddChild(r3_txt);
    r3_t_ = std::make_shared<FlexNode>(); r3_txt->AddChild(r3_t_);
    r3_s_ = std::make_shared<FlexNode>(); r3_txt->AddChild(r3_s_);
    r3_c_ = std::make_shared<FlexNode>(); r3_->AddChild(r3_c_);

    r4_ = std::make_shared<FlexNode>();
    card1_->AddChild(r4_);
    auto r4_txt = std::make_shared<FlexNode>();
    r4_txt->FlexDirection(YGFlexDirectionColumn).JustifyContent(YGJustifyCenter);
    r4_->AddChild(r4_txt);
    r4_t_ = std::make_shared<FlexNode>(); r4_txt->AddChild(r4_t_);
    r4_c_ = std::make_shared<FlexNode>(); r4_->AddChild(r4_c_);

    r5_ = std::make_shared<FlexNode>();
    card1_->AddChild(r5_);
    auto r5_txt = std::make_shared<FlexNode>();
    r5_txt->FlexDirection(YGFlexDirectionColumn).JustifyContent(YGJustifyCenter);
    r5_->AddChild(r5_txt);
    r5_t_ = std::make_shared<FlexNode>(); r5_txt->AddChild(r5_t_);
    r5_s_ = std::make_shared<FlexNode>(); r5_txt->AddChild(r5_s_);
    r5_c_ = std::make_shared<FlexNode>(); r5_->AddChild(r5_c_);
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

  r3_->Height(static_cast<float>(host->DipToPx(60.0f))).Padding(YGEdgeHorizontal, padding).FlexDirection(YGFlexDirectionRow).AlignItems(YGAlignCenter).JustifyContent(YGJustifySpaceBetween);
  r3_t_->Height(static_cast<float>(host->DipToPx(18.0f))).Margin(YGEdgeBottom, static_cast<float>(host->DipToPx(2.0f)));
  r3_s_->Height(static_cast<float>(host->DipToPx(14.0f)));
  r3_c_->Width(static_cast<float>(host->DipToPx(180.0f))).Height(static_cast<float>(host->DipToPx(28.0f)));

  r4_->Height(static_cast<float>(host->DipToPx(60.0f))).Padding(YGEdgeHorizontal, padding).FlexDirection(YGFlexDirectionRow).AlignItems(YGAlignCenter).JustifyContent(YGJustifySpaceBetween);
  r4_t_->Height(static_cast<float>(host->DipToPx(18.0f)));
  r4_c_->Width(static_cast<float>(host->DipToPx(180.0f))).Height(static_cast<float>(host->DipToPx(28.0f)));

  r5_->Height(static_cast<float>(host->DipToPx(60.0f))).Padding(YGEdgeHorizontal, padding).FlexDirection(YGFlexDirectionRow).AlignItems(YGAlignCenter).JustifyContent(YGJustifySpaceBetween);
  r5_t_->Height(static_cast<float>(host->DipToPx(18.0f))).Margin(YGEdgeBottom, static_cast<float>(host->DipToPx(2.0f)));
  r5_s_->Height(static_cast<float>(host->DipToPx(14.0f)));
  r5_c_->Width(static_cast<float>(host->DipToPx(180.0f))).Height(static_cast<float>(host->DipToPx(28.0f)));

  root_->CalculateLayout(width, YGUndefined);
  max_scroll_y_ = std::max(0.0f, root_->GetHeight() - height);
}

float SettingsPanelHotkey::Render(ID2D1HwndRenderTarget* target, const DraftState& draft, ISettingsHost* host, const RECT& content_rect) {
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

  DrawTextLine(target, host->GetShell().section_format(), host->GetShell().text_brush(), title_node_->GetAbsoluteRect(cx, cy), L"\u5feb\u6377\u952e");
  FillRoundedPanel(target, card1_->GetAbsoluteRect(cx, cy), 16.0f, host->GetShell().surface_brush(), host->GetShell().border_brush(), 0.5f);

  DrawParagraph(target, host->GetShell().body_bold_format(), host->GetShell().text_brush(), r1_t_->GetAbsoluteRect(cx, cy), L"\u9762\u677f\u5524\u9192\u70ed\u952e");
  DrawParagraph(target, host->GetShell().small_format(), host->GetShell().muted_text_brush(), r1_s_->GetAbsoluteRect(cx, cy), L"\u5168\u5c40\u547c\u51fa Scraps \u4e3b\u9762\u677f");
  RECT hotkey_rect = r1_c_->GetAbsoluteRect(cx, cy);
  host->RegisterFieldRect(FieldId::Hotkey, hotkey_rect);
  FillRoundedPanel(target, hotkey_rect, 10.0f, host->GetShell().surface_alt_brush(), host->GetShell().border_brush(), 0.5f);
  host->AddHitRegion({hotkey_rect, HitKind::Field, Section::Hotkey, FieldId::Hotkey, static_cast<int>(FieldId::Hotkey)});
  if (host->GetActiveField() != FieldId::Hotkey) {
    DrawTextLine(target, host->GetShell().small_format(), draft.hotkey.empty() ? host->GetShell().muted_text_brush() : host->GetShell().text_brush(), InsetRectCopy(hotkey_rect, host->DipToPx(10.0f), 0), draft.hotkey.empty() ? L"\u672a\u8bbe\u7f6e" : draft.hotkey);
  }
  DrawRowDivider(target, host->GetShell(), r1_->GetAbsoluteRect(cx, cy));

  DrawParagraph(target, host->GetShell().body_bold_format(), host->GetShell().text_brush(), r2_t_->GetAbsoluteRect(cx, cy), L"\u5feb\u6377\u7c98\u8d34\u70ed\u952e");
  DrawParagraph(target, host->GetShell().small_format(), host->GetShell().muted_text_brush(), r2_s_->GetAbsoluteRect(cx, cy), L"\u5feb\u901f\u7c98\u8d34\u4e0a\u4e00\u6761\u8bb0\u5f55\uff08\u53ef\u9009\uff09");
  RECT quick_paste_hotkey_rect = r2_c_->GetAbsoluteRect(cx, cy);
  host->RegisterFieldRect(FieldId::QuickPasteHotkey, quick_paste_hotkey_rect);
  FillRoundedPanel(target, quick_paste_hotkey_rect, 10.0f, host->GetShell().surface_alt_brush(), host->GetShell().border_brush(), 0.5f);
  host->AddHitRegion({quick_paste_hotkey_rect, HitKind::Field, Section::Hotkey, FieldId::QuickPasteHotkey, static_cast<int>(FieldId::QuickPasteHotkey)});
  if (host->GetActiveField() != FieldId::QuickPasteHotkey) {
    DrawTextLine(target, host->GetShell().small_format(), draft.quick_paste_hotkey.empty() ? host->GetShell().muted_text_brush() : host->GetShell().text_brush(), InsetRectCopy(quick_paste_hotkey_rect, host->DipToPx(10.0f), 0), draft.quick_paste_hotkey.empty() ? L"\u65e0" : draft.quick_paste_hotkey);
  }
  DrawRowDivider(target, host->GetShell(), r2_->GetAbsoluteRect(cx, cy));

  DrawParagraph(target, host->GetShell().body_bold_format(), host->GetShell().text_brush(), r3_t_->GetAbsoluteRect(cx, cy), L"\u805a\u7126\u641c\u7d22\u6846\u70ed\u952e");
  DrawParagraph(target, host->GetShell().small_format(), host->GetShell().muted_text_brush(), r3_s_->GetAbsoluteRect(cx, cy), L"\u5feb\u901f\u805a\u7126\u5230\u641c\u7d22\u6846");
  RECT search_focus_hotkey_rect = r3_c_->GetAbsoluteRect(cx, cy);
  host->RegisterFieldRect(FieldId::SearchFocusHotkey, search_focus_hotkey_rect);
  FillRoundedPanel(target, search_focus_hotkey_rect, 10.0f, host->GetShell().surface_alt_brush(), host->GetShell().border_brush(), 0.5f);
  host->AddHitRegion({search_focus_hotkey_rect, HitKind::Field, Section::Hotkey, FieldId::SearchFocusHotkey, static_cast<int>(FieldId::SearchFocusHotkey)});
  if (host->GetActiveField() != FieldId::SearchFocusHotkey) {
    DrawTextLine(target, host->GetShell().small_format(), draft.search_focus_hotkey.empty() ? host->GetShell().muted_text_brush() : host->GetShell().text_brush(), InsetRectCopy(search_focus_hotkey_rect, host->DipToPx(10.0f), 0), draft.search_focus_hotkey.empty() ? L"\u65e0" : draft.search_focus_hotkey);
  }
  DrawRowDivider(target, host->GetShell(), r3_->GetAbsoluteRect(cx, cy));

  DrawParagraph(target, host->GetShell().body_bold_format(), host->GetShell().text_brush(), r4_t_->GetAbsoluteRect(cx, cy), L"\u7b2c\u4e09\u65b9\u8d34\u56fe\u5feb\u6377\u952e");
  RECT third_party_sticker_hotkey_rect = r4_c_->GetAbsoluteRect(cx, cy);
  host->RegisterFieldRect(FieldId::ThirdPartyStickerHotkey, third_party_sticker_hotkey_rect);
  FillRoundedPanel(target, third_party_sticker_hotkey_rect, 10.0f, host->GetShell().surface_alt_brush(), host->GetShell().border_brush(), 0.5f);
  host->AddHitRegion({third_party_sticker_hotkey_rect, HitKind::Field, Section::Hotkey, FieldId::ThirdPartyStickerHotkey, static_cast<int>(FieldId::ThirdPartyStickerHotkey)});
  if (host->GetActiveField() != FieldId::ThirdPartyStickerHotkey) {
    DrawTextLine(target, host->GetShell().small_format(), draft.third_party_sticker_hotkey.empty() ? host->GetShell().muted_text_brush() : host->GetShell().text_brush(), InsetRectCopy(third_party_sticker_hotkey_rect, host->DipToPx(10.0f), 0), draft.third_party_sticker_hotkey.empty() ? L"\u65e0" : draft.third_party_sticker_hotkey);
  }
  DrawRowDivider(target, host->GetShell(), r4_->GetAbsoluteRect(cx, cy));

  DrawParagraph(target, host->GetShell().body_bold_format(), host->GetShell().text_brush(), r5_t_->GetAbsoluteRect(cx, cy), L"\u5c4f\u5e55\u622a\u56fe\u5feb\u6377\u952e");
  DrawParagraph(target, host->GetShell().small_format(), host->GetShell().muted_text_brush(), r5_s_->GetAbsoluteRect(cx, cy), L"\u5168\u5c40\u547c\u51fa\u622a\u56fe\u529f\u80fd");
  RECT screenshot_hotkey_rect = r5_c_->GetAbsoluteRect(cx, cy);
  host->RegisterFieldRect(FieldId::ScreenshotHotkey, screenshot_hotkey_rect);
  FillRoundedPanel(target, screenshot_hotkey_rect, 10.0f, host->GetShell().surface_alt_brush(), host->GetShell().border_brush(), 0.5f);
  host->AddHitRegion({screenshot_hotkey_rect, HitKind::Field, Section::Hotkey, FieldId::ScreenshotHotkey, static_cast<int>(FieldId::ScreenshotHotkey)});
  if (host->GetActiveField() != FieldId::ScreenshotHotkey) {
    DrawTextLine(target, host->GetShell().small_format(), draft.screenshot_hotkey.empty() ? host->GetShell().muted_text_brush() : host->GetShell().text_brush(), InsetRectCopy(screenshot_hotkey_rect, host->DipToPx(10.0f), 0), draft.screenshot_hotkey.empty() ? L"\u672a\u8bbe\u7f6e" : draft.screenshot_hotkey);
  }

  return max_scroll_y_;
}

}  // namespace clipd2d::ui
