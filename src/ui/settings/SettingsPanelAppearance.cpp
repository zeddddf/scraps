#include "ui/settings/SettingsPanelAppearance.h"
#include "ui/settings/ISettingsHost.h"
#include "ui/settings/SettingsRow.h"
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

    r0_ = std::make_shared<FlexNode>();
    card1_->AddChild(r0_);
    auto r0_txt = std::make_shared<FlexNode>();
    r0_txt->FlexDirection(YGFlexDirectionColumn).JustifyContent(YGJustifyCenter);
    r0_->AddChild(r0_txt);
    r0_t_ = std::make_shared<FlexNode>(); r0_txt->AddChild(r0_t_);
    r0_s_ = std::make_shared<FlexNode>(); r0_txt->AddChild(r0_s_);
    r0_c_ = std::make_shared<FlexNode>(); r0_->AddChild(r0_c_);

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

    r5_ = std::make_shared<FlexNode>();
    card1_->AddChild(r5_);
    auto r5_txt = std::make_shared<FlexNode>();
    r5_txt->FlexDirection(YGFlexDirectionColumn).JustifyContent(YGJustifyCenter);
    r5_->AddChild(r5_txt);
    r5_t_ = std::make_shared<FlexNode>(); r5_txt->AddChild(r5_t_);
    r5_s_ = std::make_shared<FlexNode>(); r5_txt->AddChild(r5_s_);
    r5_c_ = std::make_shared<FlexNode>(); r5_->AddChild(r5_c_);
  }

  root_->Width(width).FlexDirection(YGFlexDirectionColumn);  // 根高自适应内容：钉死视口高会让 max_scroll_y 恒为 0（滚动失效根因）

  title_node_->Height(static_cast<float>(host->DipToPx(32.0f))).Margin(YGEdgeBottom, static_cast<float>(host->DipToPx(4.0f)));

  card1_->FlexDirection(YGFlexDirectionColumn).Margin(YGEdgeBottom, static_cast<float>(host->DipToPx(24.0f)));

  r0_->Height(static_cast<float>(host->DipToPx(60.0f))).Padding(YGEdgeHorizontal, padding).FlexDirection(YGFlexDirectionRow).AlignItems(YGAlignCenter).JustifyContent(YGJustifySpaceBetween);
  r0_t_->Height(static_cast<float>(host->DipToPx(18.0f))).Margin(YGEdgeBottom, static_cast<float>(host->DipToPx(2.0f)));
  r0_s_->Height(static_cast<float>(host->DipToPx(14.0f)));
  r0_c_->Width(static_cast<float>(host->DipToPx(180.0f))).Height(static_cast<float>(host->DipToPx(28.0f)));

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


  r5_->Height(static_cast<float>(host->DipToPx(60.0f))).Padding(YGEdgeHorizontal, padding).FlexDirection(YGFlexDirectionRow).AlignItems(YGAlignCenter).JustifyContent(YGJustifySpaceBetween);
  r5_t_->Height(static_cast<float>(host->DipToPx(18.0f))).Margin(YGEdgeBottom, static_cast<float>(host->DipToPx(2.0f)));
  r5_s_->Height(static_cast<float>(host->DipToPx(14.0f)));
  r5_c_->Width(static_cast<float>(host->DipToPx(88.0f))).Height(static_cast<float>(host->DipToPx(28.0f)));
  root_->CalculateLayout(width, YGUndefined);
  max_scroll_y_ = std::max(0.0f, root_->GetHeight() - height);
}

std::vector<SettingRow> SettingsPanelAppearance::BuildRows(const DraftState& draft, ISettingsHost* /*host*/) const {
  return {
      MakeSelectRow(r0_, r0_t_, r0_s_, r0_c_, L"\u4e3b\u9898",
                    L"\u9009\u62e9\u6d45\u8272 / \u6df1\u8272\u754c\u9762\uff0c\u6216\u8ddf\u968f\u7cfb\u7edf\u6df1\u6d45\u8272\u81ea\u52a8\u5207\u6362\uff08\u4fdd\u5b58\u540e\u751f\u6548\uff09",
                    Section::Appearance, SelectField::Theme),
      MakeSelectRow(r1_, r1_t_, r1_s_, r1_c_, L"\u5217\u8868\u5bc6\u5ea6", L"\u8c03\u6574\u526a\u8d34\u677f\u5217\u8868\u9879\u7684\u663e\u793a\u9ad8\u5ea6",
                    Section::Appearance, SelectField::ListDensity),
      MakeToggleRow(r2_, r2_t_, r2_s_, r2_c_, L"\u663e\u793a\u6765\u6e90\u5e94\u7528\u56fe\u6807", L"\u5728\u5217\u8868\u9879\u5de6\u4fa7\u663e\u793a\u590d\u5236\u6765\u6e90\u7a0b\u5e8f\u7684\u56fe\u6807",
                    draft.show_source_icon, Section::Appearance, 3),
      MakeSelectRow(r3_, r3_t_, r3_s_, r3_c_, L"\u7a97\u53e3\u6bdb\u73bb\u7483\u80cc\u666f", L"\u9009\u62e9\u4e3b\u7a97\u53e3\u548c\u5feb\u6377\u7c98\u8d34\u7a97\u53e3\u7684\u534a\u900f\u660e\u6bdb\u73bb\u7483\u6548\u679c\u7c7b\u578b",
                    Section::Appearance, SelectField::WindowBlurType),
      MakeSliderRow(r4_, r4_t_, r4_s_, r4_c_, L"\u6bdb\u73bb\u7483\u4e0d\u900f\u660e\u5ea6", L"\u8c03\u6574\u80cc\u666f\u7684\u4e0d\u900f\u660e\u5ea6 (0-100)",
                    0, 100, draft.window_blur_opacity,
                    Section::Appearance, FieldId::EnableWindowBlur, 18, false),
      MakeResetRow(r5_, r5_t_, r5_s_, r5_c_, Section::Appearance),
  };
}

float SettingsPanelAppearance::Render(ID2D1HwndRenderTarget* target, const DraftState& draft, ISettingsHost* host, const RECT& content_rect) {
  float width = static_cast<float>(content_rect.right - content_rect.left);
  float height = static_cast<float>(content_rect.bottom - content_rect.top);
  int scale_key = host->DipToPx(100);

  bool relayout = false;
  if (width != last_width_ || height != last_height_ || scale_key != last_scale_key_ || !root_) {
    BuildLayout(width, height, host);
    last_width_ = width;
    last_height_ = height;
    last_scale_key_ = scale_key;
    relayout = true;
  }

  const std::vector<SettingRow> rows = BuildRows(draft, host);
  SetDrawnRows(rows);
  PrepareSettingRows(root_.get(), width, height, &max_scroll_y_, relayout);

  float cx = static_cast<float>(content_rect.left);
  float cy = static_cast<float>(content_rect.top) - host->GetScrollY();

  // P3-16b：命中值（Section/FieldId/option）与旧手写逐项一致。
  DrawSettingSectionTitle(target, host, cx, cy, title_node_, L"\u5916\u89c2");
  DrawSettingPanelBox(target, host, cx, cy, card1_);
  DrawSettingRows(target, host, cx, cy, rows);

  return max_scroll_y_;
}

}  // namespace clipd2d::ui
