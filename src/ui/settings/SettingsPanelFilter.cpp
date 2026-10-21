#include "ui/settings/SettingsPanelFilter.h"
#include "ui/settings/ISettingsHost.h"
#include "ui/settings/SettingsTypes.h"
#include "ui/settings/SettingsDrawUtil.h"
#include "ui/layout/FlexNode.h"
#include <algorithm>

namespace clipd2d::ui {

void SettingsPanelFilter::BuildLayout(float width, float height, ISettingsHost* host) {
  float padding = static_cast<float>(host->DipToPx(24.0f));

  if (!root_) {
    root_ = std::make_shared<FlexNode>();
    c1_title_ = std::make_shared<FlexNode>();
    root_->AddChild(c1_title_);

    c1_ = std::make_shared<FlexNode>();
    root_->AddChild(c1_);

    r1_ = std::make_shared<FlexNode>();
    c1_->AddChild(r1_);
    r1_txt_ = std::make_shared<FlexNode>();
    r1_txt_->FlexDirection(YGFlexDirectionColumn).JustifyContent(YGJustifyCenter).FlexGrow(1.0f).FlexShrink(1.0f);
    r1_->AddChild(r1_txt_);
    r1_t_ = std::make_shared<FlexNode>(); r1_txt_->AddChild(r1_t_);
    r1_s_ = std::make_shared<FlexNode>(); r1_txt_->AddChild(r1_s_);
    r1_c_ = std::make_shared<FlexNode>(); r1_->AddChild(r1_c_);

    r2_ = std::make_shared<FlexNode>();
    c1_->AddChild(r2_);
    r2_txt_ = std::make_shared<FlexNode>();
    r2_txt_->FlexDirection(YGFlexDirectionColumn).JustifyContent(YGJustifyCenter).FlexGrow(1.0f).FlexShrink(1.0f);
    r2_->AddChild(r2_txt_);
    r2_t_ = std::make_shared<FlexNode>(); r2_txt_->AddChild(r2_t_);
    r2_s_ = std::make_shared<FlexNode>(); r2_txt_->AddChild(r2_s_);
    r2_c_ = std::make_shared<FlexNode>(); r2_->AddChild(r2_c_);

    r3_ = std::make_shared<FlexNode>();
    c1_->AddChild(r3_);
    r3_txt_ = std::make_shared<FlexNode>();
    r3_txt_->FlexDirection(YGFlexDirectionColumn).JustifyContent(YGJustifyCenter).FlexGrow(1.0f).FlexShrink(1.0f);
    r3_->AddChild(r3_txt_);
    r3_t_ = std::make_shared<FlexNode>(); r3_txt_->AddChild(r3_t_);
    r3_s_ = std::make_shared<FlexNode>(); r3_txt_->AddChild(r3_s_);
    r3_c_ = std::make_shared<FlexNode>(); r3_->AddChild(r3_c_);

    c2_title_ = std::make_shared<FlexNode>();
    root_->AddChild(c2_title_);

    c2_ = std::make_shared<FlexNode>();
    root_->AddChild(c2_);

    r4_ = std::make_shared<FlexNode>();
    c2_->AddChild(r4_);
    r4_txt_ = std::make_shared<FlexNode>();
    r4_txt_->FlexDirection(YGFlexDirectionColumn).JustifyContent(YGJustifyCenter).FlexGrow(1.0f).FlexShrink(1.0f);
    r4_->AddChild(r4_txt_);
    r4_t_ = std::make_shared<FlexNode>(); r4_txt_->AddChild(r4_t_);
    r4_s_ = std::make_shared<FlexNode>(); r4_txt_->AddChild(r4_s_);
    auto r4_c_group = std::make_shared<FlexNode>(); r4_c_group->FlexDirection(YGFlexDirectionRow).AlignItems(YGAlignCenter); r4_->AddChild(r4_c_group);
    r4_c_input_ = std::make_shared<FlexNode>(); r4_c_group->AddChild(r4_c_input_);
    r4_c_toggle_ = std::make_shared<FlexNode>(); r4_c_group->AddChild(r4_c_toggle_);

    r5_ = std::make_shared<FlexNode>();
    c2_->AddChild(r5_);
    r5_txt_ = std::make_shared<FlexNode>();
    r5_txt_->FlexDirection(YGFlexDirectionColumn).JustifyContent(YGJustifyCenter).FlexGrow(1.0f).FlexShrink(1.0f);
    r5_->AddChild(r5_txt_);
    r5_t_ = std::make_shared<FlexNode>(); r5_txt_->AddChild(r5_t_);
    r5_s_ = std::make_shared<FlexNode>(); r5_txt_->AddChild(r5_s_);
    auto r5_c_group = std::make_shared<FlexNode>(); r5_c_group->FlexDirection(YGFlexDirectionRow).AlignItems(YGAlignCenter); r5_->AddChild(r5_c_group);
    r5_c_input_ = std::make_shared<FlexNode>(); r5_c_group->AddChild(r5_c_input_);
    r5_c_toggle_ = std::make_shared<FlexNode>(); r5_c_group->AddChild(r5_c_toggle_);

    c3_title_ = std::make_shared<FlexNode>();
    root_->AddChild(c3_title_);

    c3_ = std::make_shared<FlexNode>();
    root_->AddChild(c3_);

    r6_ = std::make_shared<FlexNode>();
    c3_->AddChild(r6_);
  }

  root_->Width(width).FlexDirection(YGFlexDirectionColumn);

  c1_title_->Height(static_cast<float>(host->DipToPx(32.0f))).Margin(YGEdgeBottom, static_cast<float>(host->DipToPx(4.0f)));

  c1_->FlexDirection(YGFlexDirectionColumn).Margin(YGEdgeBottom, static_cast<float>(host->DipToPx(24.0f)));

  r1_->Height(static_cast<float>(host->DipToPx(60.0f))).Padding(YGEdgeHorizontal, padding).FlexDirection(YGFlexDirectionRow).AlignItems(YGAlignCenter).JustifyContent(YGJustifySpaceBetween);
  r1_txt_->Margin(YGEdgeRight, static_cast<float>(host->DipToPx(16.0f)));
  r1_t_->Height(static_cast<float>(host->DipToPx(18.0f))).Margin(YGEdgeBottom, static_cast<float>(host->DipToPx(2.0f)));
  r1_s_->Height(static_cast<float>(host->DipToPx(14.0f)));
  r1_c_->Width(static_cast<float>(host->DipToPx(44.0f))).Height(static_cast<float>(host->DipToPx(24.0f)));

  r2_->Height(static_cast<float>(host->DipToPx(60.0f))).Padding(YGEdgeHorizontal, padding).FlexDirection(YGFlexDirectionRow).AlignItems(YGAlignCenter).JustifyContent(YGJustifySpaceBetween);
  r2_txt_->Margin(YGEdgeRight, static_cast<float>(host->DipToPx(16.0f)));
  r2_t_->Height(static_cast<float>(host->DipToPx(18.0f))).Margin(YGEdgeBottom, static_cast<float>(host->DipToPx(2.0f)));
  r2_s_->Height(static_cast<float>(host->DipToPx(14.0f)));
  r2_c_->Width(static_cast<float>(host->DipToPx(44.0f))).Height(static_cast<float>(host->DipToPx(24.0f)));

  r3_->Height(static_cast<float>(host->DipToPx(60.0f))).Padding(YGEdgeHorizontal, padding).FlexDirection(YGFlexDirectionRow).AlignItems(YGAlignCenter).JustifyContent(YGJustifySpaceBetween);
  r3_txt_->Margin(YGEdgeRight, static_cast<float>(host->DipToPx(16.0f)));
  r3_t_->Height(static_cast<float>(host->DipToPx(18.0f))).Margin(YGEdgeBottom, static_cast<float>(host->DipToPx(2.0f)));
  r3_s_->Height(static_cast<float>(host->DipToPx(14.0f)));
  r3_c_->Width(static_cast<float>(host->DipToPx(44.0f))).Height(static_cast<float>(host->DipToPx(24.0f)));

  c2_title_->Height(static_cast<float>(host->DipToPx(32.0f))).Margin(YGEdgeBottom, static_cast<float>(host->DipToPx(4.0f)));

  c2_->FlexDirection(YGFlexDirectionColumn).Margin(YGEdgeBottom, static_cast<float>(host->DipToPx(24.0f)));

  r4_->Height(static_cast<float>(host->DipToPx(60.0f))).Padding(YGEdgeHorizontal, padding).FlexDirection(YGFlexDirectionRow).AlignItems(YGAlignCenter).JustifyContent(YGJustifySpaceBetween);
  r4_txt_->Margin(YGEdgeRight, static_cast<float>(host->DipToPx(16.0f)));
  r4_t_->Height(static_cast<float>(host->DipToPx(18.0f))).Margin(YGEdgeBottom, static_cast<float>(host->DipToPx(2.0f)));
  r4_s_->Height(static_cast<float>(host->DipToPx(14.0f)));
  r4_c_input_->Width(static_cast<float>(host->DipToPx(80.0f))).Height(static_cast<float>(host->DipToPx(28.0f))).Margin(YGEdgeRight, static_cast<float>(host->DipToPx(16.0f)));
  r4_c_toggle_->Width(static_cast<float>(host->DipToPx(44.0f))).Height(static_cast<float>(host->DipToPx(24.0f)));

  r5_->Height(static_cast<float>(host->DipToPx(60.0f))).Padding(YGEdgeHorizontal, padding).FlexDirection(YGFlexDirectionRow).AlignItems(YGAlignCenter).JustifyContent(YGJustifySpaceBetween);
  r5_txt_->Margin(YGEdgeRight, static_cast<float>(host->DipToPx(16.0f)));
  r5_t_->Height(static_cast<float>(host->DipToPx(18.0f))).Margin(YGEdgeBottom, static_cast<float>(host->DipToPx(2.0f)));
  r5_s_->Height(static_cast<float>(host->DipToPx(14.0f)));
  r5_c_input_->Width(static_cast<float>(host->DipToPx(80.0f))).Height(static_cast<float>(host->DipToPx(28.0f))).Margin(YGEdgeRight, static_cast<float>(host->DipToPx(16.0f)));
  r5_c_toggle_->Width(static_cast<float>(host->DipToPx(44.0f))).Height(static_cast<float>(host->DipToPx(24.0f)));

  c3_title_->Height(static_cast<float>(host->DipToPx(32.0f))).Margin(YGEdgeBottom, static_cast<float>(host->DipToPx(4.0f)));

  c3_->FlexDirection(YGFlexDirectionColumn).Margin(YGEdgeBottom, static_cast<float>(host->DipToPx(48.0f)));

  r6_->Height(static_cast<float>(host->DipToPx(120.0f)));

  root_->CalculateLayout(width, YGUndefined);
  max_scroll_y_ = std::max(0.0f, root_->GetHeight() - height);
}

float SettingsPanelFilter::Render(ID2D1HwndRenderTarget* target, const DraftState& draft, ISettingsHost* host, const RECT& content_rect) {
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

  DrawTextLine(target, host->GetShell().section_format(), host->GetShell().text_brush(), c1_title_->GetAbsoluteRect(cx, cy), L"\u667a\u80fd\u7b56\u7565");
  FillRoundedPanel(target, c1_->GetAbsoluteRect(cx, cy), 16.0f, host->GetShell().surface_brush(), host->GetShell().border_brush(), 0.5f);

  DrawParagraph(target, host->GetShell().body_bold_format(), host->GetShell().text_brush(), r1_t_->GetAbsoluteRect(cx, cy), L"\u5ffd\u7565\u5bc6\u7801\u7ba1\u7406\u5668");
  DrawParagraph(target, host->GetShell().small_format(), host->GetShell().muted_text_brush(), r1_s_->GetAbsoluteRect(cx, cy), L"\u81ea\u52a8\u5ffd\u7565 1Password, KeePass \u7b49\u5e38\u89c1\u5bc6\u7801\u5de5\u5177");
  RECT auto_ignore_password_managers_toggle_rect = r1_c_->GetAbsoluteRect(cx, cy);
  DrawToggle(target, auto_ignore_password_managers_toggle_rect, draft.auto_ignore_password_managers, host->GetShell());
  host->AddHitRegion({auto_ignore_password_managers_toggle_rect, HitKind::Toggle, Section::Filter, FieldId::None, 11});
  DrawRowDivider(target, host->GetShell(), r1_->GetAbsoluteRect(cx, cy));

  DrawParagraph(target, host->GetShell().body_bold_format(), host->GetShell().text_brush(), r2_t_->GetAbsoluteRect(cx, cy), L"\u5ffd\u7565\u9a8c\u8bc1\u7801");
  DrawParagraph(target, host->GetShell().small_format(), host->GetShell().muted_text_brush(), r2_s_->GetAbsoluteRect(cx, cy), L"\u4e0d\u8bb0\u5f55 4-8 \u4f4d\u7eaf\u6570\u5b57\u6216\u5b57\u6bcd\u9a8c\u8bc1\u7801");
  RECT ignore_otp_codes_toggle_rect = r2_c_->GetAbsoluteRect(cx, cy);
  DrawToggle(target, ignore_otp_codes_toggle_rect, draft.ignore_otp_codes, host->GetShell());
  host->AddHitRegion({ignore_otp_codes_toggle_rect, HitKind::Toggle, Section::Filter, FieldId::None, 14});
  DrawRowDivider(target, host->GetShell(), r2_->GetAbsoluteRect(cx, cy));

  DrawParagraph(target, host->GetShell().body_bold_format(), host->GetShell().text_brush(), r3_t_->GetAbsoluteRect(cx, cy), L"\u5ffd\u7565\u590d\u6742\u5bc6\u7801");
  DrawParagraph(target, host->GetShell().small_format(), host->GetShell().muted_text_brush(), r3_s_->GetAbsoluteRect(cx, cy), L"\u5ffd\u7565\u5305\u542b\u5927\u5c0f\u5199\u3001\u6570\u5b57\u548c\u7b26\u53f7\u7684\u968f\u673a\u5b57\u7b26\u4e32");
  RECT ignore_complex_passwords_toggle_rect = r3_c_->GetAbsoluteRect(cx, cy);
  DrawToggle(target, ignore_complex_passwords_toggle_rect, draft.ignore_complex_passwords, host->GetShell());
  host->AddHitRegion({ignore_complex_passwords_toggle_rect, HitKind::Toggle, Section::Filter, FieldId::None, 15});

  DrawTextLine(target, host->GetShell().section_format(), host->GetShell().text_brush(), c2_title_->GetAbsoluteRect(cx, cy), L"\u4f53\u79ef\u9650\u5236");
  FillRoundedPanel(target, c2_->GetAbsoluteRect(cx, cy), 16.0f, host->GetShell().surface_brush(), host->GetShell().border_brush(), 0.5f);

  DrawParagraph(target, host->GetShell().body_bold_format(), host->GetShell().text_brush(), r4_t_->GetAbsoluteRect(cx, cy), L"\u8d85\u5927\u6587\u672c\u62e6\u622a");
  DrawParagraph(target, host->GetShell().small_format(), host->GetShell().muted_text_brush(), r4_s_->GetAbsoluteRect(cx, cy), L"\u62d2\u7edd\u8bb0\u5f55\u8d85\u8fc7\u9650\u5236\u7684\u6587\u672c\uff08\u5355\u4f4d: KB\uff09");
  RECT max_text_size_rect = r4_c_input_->GetAbsoluteRect(cx, cy);
  host->RegisterFieldRect(FieldId::MaxTextSize, max_text_size_rect);
  FillRoundedPanel(target, max_text_size_rect, 6.0f, host->GetShell().surface_alt_brush(), host->GetShell().border_brush(), 0.5f);
  host->AddHitRegion({max_text_size_rect, HitKind::Field, Section::Filter, FieldId::MaxTextSize, static_cast<int>(FieldId::MaxTextSize)});
  if (host->GetActiveField() != FieldId::MaxTextSize) {
    DrawTextLine(target, host->GetShell().small_format(), host->GetShell().text_brush(), InsetRectCopy(max_text_size_rect, host->DipToPx(8.0f), 0), draft.max_text_size_kb_text);
  }
  RECT ignore_large_text_toggle_rect = r4_c_toggle_->GetAbsoluteRect(cx, cy);
  DrawToggle(target, ignore_large_text_toggle_rect, draft.ignore_large_text, host->GetShell());
  host->AddHitRegion({ignore_large_text_toggle_rect, HitKind::Toggle, Section::Filter, FieldId::None, 12});
  DrawRowDivider(target, host->GetShell(), r4_->GetAbsoluteRect(cx, cy));

  DrawParagraph(target, host->GetShell().body_bold_format(), host->GetShell().text_brush(), r5_t_->GetAbsoluteRect(cx, cy), L"\u8d85\u5927\u56fe\u7247\u62e6\u622a");
  DrawParagraph(target, host->GetShell().small_format(), host->GetShell().muted_text_brush(), r5_s_->GetAbsoluteRect(cx, cy), L"\u62d2\u7edd\u8bb0\u5f55\u8d85\u8fc7\u9650\u5236\u7684\u56fe\u7247\uff08\u5355\u4f4d: KB\uff09");
  RECT max_image_size_rect = r5_c_input_->GetAbsoluteRect(cx, cy);
  host->RegisterFieldRect(FieldId::MaxImageSize, max_image_size_rect);
  FillRoundedPanel(target, max_image_size_rect, 6.0f, host->GetShell().surface_alt_brush(), host->GetShell().border_brush(), 0.5f);
  host->AddHitRegion({max_image_size_rect, HitKind::Field, Section::Filter, FieldId::MaxImageSize, static_cast<int>(FieldId::MaxImageSize)});
  if (host->GetActiveField() != FieldId::MaxImageSize) {
    DrawTextLine(target, host->GetShell().small_format(), host->GetShell().text_brush(), InsetRectCopy(max_image_size_rect, host->DipToPx(8.0f), 0), draft.max_image_size_kb_text);
  }
  RECT ignore_large_images_toggle_rect = r5_c_toggle_->GetAbsoluteRect(cx, cy);
  DrawToggle(target, ignore_large_images_toggle_rect, draft.ignore_large_images, host->GetShell());
  host->AddHitRegion({ignore_large_images_toggle_rect, HitKind::Toggle, Section::Filter, FieldId::None, 13});

  DrawTextLine(target, host->GetShell().section_format(), host->GetShell().text_brush(), c3_title_->GetAbsoluteRect(cx, cy), L"\u81ea\u5b9a\u4e49\u9ed1\u540d\u5355");
  FillRoundedPanel(target, c3_->GetAbsoluteRect(cx, cy), 16.0f, host->GetShell().surface_brush(), host->GetShell().border_brush(), 0.5f);
  RECT custom_blacklist_rect = InsetRectCopy(r6_->GetAbsoluteRect(cx, cy), host->DipToPx(1.0f), host->DipToPx(1.0f));
  host->RegisterFieldRect(FieldId::CustomBlacklist, custom_blacklist_rect);
  host->AddHitRegion({custom_blacklist_rect, HitKind::Field, Section::Filter, FieldId::CustomBlacklist, static_cast<int>(FieldId::CustomBlacklist)});
  if (host->GetActiveField() != FieldId::CustomBlacklist) {
    DrawParagraph(target, host->GetShell().small_format(), draft.custom_blacklist_apps.empty() ? host->GetShell().muted_text_brush() : host->GetShell().text_brush(), InsetRectCopy(custom_blacklist_rect, host->DipToPx(16.0f), host->DipToPx(16.0f)), draft.custom_blacklist_apps.empty() ? L"\u4f8b: keepass.exe, passwordmanager.exe" : draft.custom_blacklist_apps);
  }

  return max_scroll_y_;
}

}  // namespace clipd2d::ui
