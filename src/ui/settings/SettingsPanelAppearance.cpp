#include "ui/settings/SettingsPanelAppearance.h"
#include "ui/settings/ISettingsHost.h"
#include "ui/settings/SettingsTypes.h"
#include "ui/settings/SettingsDrawUtil.h"
#include "ui/layout/FlexNode.h"
#include <algorithm>

namespace clipd2d::ui {

void SettingsPanelAppearance::BuildLayout(float width, float height, ISettingsHost* host) {
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
    r4_s_ = std::make_shared<FlexNode>(); r4_txt->AddChild(r4_s_);
    r4_c_ = std::make_shared<FlexNode>(); r4_->AddChild(r4_c_);
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
  r2_c_->Width(static_cast<float>(host->DipToPx(44.0f))).Height(static_cast<float>(host->DipToPx(24.0f)));

  r3_->Height(static_cast<float>(host->DipToPx(60.0f))).Padding(YGEdgeHorizontal, padding).FlexDirection(YGFlexDirectionRow).AlignItems(YGAlignCenter).JustifyContent(YGJustifySpaceBetween);
  r3_t_->Height(static_cast<float>(host->DipToPx(18.0f))).Margin(YGEdgeBottom, static_cast<float>(host->DipToPx(2.0f)));
  r3_s_->Height(static_cast<float>(host->DipToPx(14.0f)));
  r3_c_->Width(static_cast<float>(host->DipToPx(180.0f))).Height(static_cast<float>(host->DipToPx(28.0f)));

  r4_->Height(static_cast<float>(host->DipToPx(60.0f))).Padding(YGEdgeHorizontal, padding).FlexDirection(YGFlexDirectionRow).AlignItems(YGAlignCenter).JustifyContent(YGJustifySpaceBetween);
  r4_t_->Height(static_cast<float>(host->DipToPx(18.0f))).Margin(YGEdgeBottom, static_cast<float>(host->DipToPx(2.0f)));
  r4_s_->Height(static_cast<float>(host->DipToPx(14.0f)));
  r4_c_->Width(static_cast<float>(host->DipToPx(180.0f))).Height(static_cast<float>(host->DipToPx(24.0f)));

  root_->CalculateLayout(width, YGUndefined);
  max_scroll_y_ = std::max(0.0f, root_->GetHeight() - height);
}

float SettingsPanelAppearance::Render(ID2D1HwndRenderTarget* target, const DraftState& draft, ISettingsHost* host, const RECT& content_rect) {
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

  DrawTextLine(target, host->GetShell().section_format(), host->GetShell().text_brush(), title_node_->GetAbsoluteRect(cx, cy), L"\u5916\u89c2");
  FillRoundedPanel(target, card1_->GetAbsoluteRect(cx, cy), 16.0f, host->GetShell().surface_brush(), host->GetShell().border_brush(), 0.5f);

  DrawParagraph(target, host->GetShell().body_bold_format(), host->GetShell().text_brush(), r1_t_->GetAbsoluteRect(cx, cy), L"\u5217\u8868\u5bc6\u5ea6");
  DrawParagraph(target, host->GetShell().small_format(), host->GetShell().muted_text_brush(), r1_s_->GetAbsoluteRect(cx, cy), L"\u8c03\u6574\u526a\u8d34\u677f\u5217\u8868\u9879\u7684\u663e\u793a\u9ad8\u5ea6");
  RECT density_select_rect = r1_c_->GetAbsoluteRect(cx, cy);
  host->RegisterSelectRect(SelectField::ListDensity, density_select_rect);
  DrawSelectTrigger(target, host->GetShell(), density_select_rect, host->GetSelectLabel(SelectField::ListDensity), host->GetActiveSelectField() == SelectField::ListDensity);
  host->AddHitRegion({density_select_rect, HitKind::SelectTrigger, Section::Appearance, FieldId::None, static_cast<int>(SelectField::ListDensity)});
  DrawRowDivider(target, host->GetShell(), r1_->GetAbsoluteRect(cx, cy));

  DrawParagraph(target, host->GetShell().body_bold_format(), host->GetShell().text_brush(), r2_t_->GetAbsoluteRect(cx, cy), L"\u663e\u793a\u6765\u6e90\u5e94\u7528\u56fe\u6807");
  DrawParagraph(target, host->GetShell().small_format(), host->GetShell().muted_text_brush(), r2_s_->GetAbsoluteRect(cx, cy), L"\u5728\u5217\u8868\u9879\u5de6\u4fa7\u663e\u793a\u590d\u5236\u6765\u6e90\u7a0b\u5e8f\u7684\u56fe\u6807");
  RECT source_icon_toggle_rect = r2_c_->GetAbsoluteRect(cx, cy);
  DrawToggle(target, source_icon_toggle_rect, draft.show_source_icon, host->GetShell());
  host->AddHitRegion({source_icon_toggle_rect, HitKind::Toggle, Section::Appearance, FieldId::None, 3});
  DrawRowDivider(target, host->GetShell(), r2_->GetAbsoluteRect(cx, cy));

  DrawParagraph(target, host->GetShell().body_bold_format(), host->GetShell().text_brush(), r3_t_->GetAbsoluteRect(cx, cy), L"\u7a97\u53e3\u6bdb\u73bb\u7483\u80cc\u666f");
  DrawParagraph(target, host->GetShell().small_format(), host->GetShell().muted_text_brush(), r3_s_->GetAbsoluteRect(cx, cy), L"\u9009\u62e9\u4e3b\u7a97\u53e3\u548c\u5feb\u6377\u7c98\u8d34\u7a97\u53e3\u7684\u534a\u900f\u660e\u6bdb\u73bb\u7483\u6548\u679c\u7c7b\u578b");
  RECT window_blur_select_rect = r3_c_->GetAbsoluteRect(cx, cy);
  host->RegisterSelectRect(SelectField::WindowBlurType, window_blur_select_rect);
  DrawSelectTrigger(target, host->GetShell(), window_blur_select_rect, host->GetSelectLabel(SelectField::WindowBlurType), host->GetActiveSelectField() == SelectField::WindowBlurType);
  host->AddHitRegion({window_blur_select_rect, HitKind::SelectTrigger, Section::Appearance, FieldId::None, static_cast<int>(SelectField::WindowBlurType)});
  DrawRowDivider(target, host->GetShell(), r3_->GetAbsoluteRect(cx, cy));

  DrawParagraph(target, host->GetShell().body_bold_format(), host->GetShell().text_brush(), r4_t_->GetAbsoluteRect(cx, cy), L"\u6bdb\u73bb\u7483\u4e0d\u900f\u660e\u5ea6");
  DrawParagraph(target, host->GetShell().small_format(), host->GetShell().muted_text_brush(), r4_s_->GetAbsoluteRect(cx, cy), L"\u8c03\u6574\u80cc\u666f\u7684\u4e0d\u900f\u660e\u5ea6 (0-100)");
  RECT opacity_slider_rect = r4_c_->GetAbsoluteRect(cx, cy);
  DrawSlider(target, host->GetShell(), opacity_slider_rect, 0, 100, draft.window_blur_opacity);
  host->AddHitRegion({opacity_slider_rect, HitKind::SliderDrag, Section::Appearance, FieldId::EnableWindowBlur, 18});

  return max_scroll_y_;
}

}  // namespace clipd2d::ui
