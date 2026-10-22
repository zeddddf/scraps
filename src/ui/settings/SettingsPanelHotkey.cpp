#include "ui/settings/SettingsPanelHotkey.h"
#include "ui/settings/ISettingsHost.h"
#include "ui/settings/SettingsRow.h"
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

    r6_ = std::make_shared<FlexNode>();
    card1_->AddChild(r6_);
    auto r6_txt = std::make_shared<FlexNode>();
    r6_txt->FlexDirection(YGFlexDirectionColumn).JustifyContent(YGJustifyCenter);
    r6_->AddChild(r6_txt);
    r6_t_ = std::make_shared<FlexNode>(); r6_txt->AddChild(r6_t_);
    r6_s_ = std::make_shared<FlexNode>(); r6_txt->AddChild(r6_s_);
    r6_c_ = std::make_shared<FlexNode>(); r6_->AddChild(r6_c_);
  }

  root_->Width(width).FlexDirection(YGFlexDirectionColumn);  // 根高自适应内容：钉死视口高会让 max_scroll_y 恒为 0（滚动失效根因）

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


  r6_->Height(static_cast<float>(host->DipToPx(60.0f))).Padding(YGEdgeHorizontal, padding).FlexDirection(YGFlexDirectionRow).AlignItems(YGAlignCenter).JustifyContent(YGJustifySpaceBetween);
  r6_t_->Height(static_cast<float>(host->DipToPx(18.0f))).Margin(YGEdgeBottom, static_cast<float>(host->DipToPx(2.0f)));
  r6_s_->Height(static_cast<float>(host->DipToPx(14.0f)));
  r6_c_->Width(static_cast<float>(host->DipToPx(88.0f))).Height(static_cast<float>(host->DipToPx(28.0f)));
  root_->CalculateLayout(width, YGUndefined);
  max_scroll_y_ = std::max(0.0f, root_->GetHeight() - height);
}

std::vector<SettingRow> SettingsPanelHotkey::BuildRows(const DraftState& draft, ISettingsHost* /*host*/) const {
  // 命中 option=(int)field_id（SettingsWindow::BeginEditing 依赖该值）。
  return {
      MakeFieldRow(r1_, r1_t_, r1_s_, r1_c_, L"\u9762\u677f\u5524\u9192\u70ed\u952e", L"\u5168\u5c40\u547c\u51fa Scraps \u4e3b\u9762\u677f",
                   draft.hotkey, Section::Hotkey, FieldId::Hotkey, true, L"\u672a\u8bbe\u7f6e"),
      MakeFieldRow(r2_, r2_t_, r2_s_, r2_c_, L"\u5feb\u6377\u7c98\u8d34\u70ed\u952e", L"\u5feb\u901f\u7c98\u8d34\u4e0a\u4e00\u6761\u8bb0\u5f55\uff08\u53ef\u9009\uff09",
                   draft.quick_paste_hotkey, Section::Hotkey, FieldId::QuickPasteHotkey, true, L"\u65e0"),
      MakeFieldRow(r3_, r3_t_, r3_s_, r3_c_, L"\u805a\u7126\u641c\u7d22\u6846\u70ed\u952e", L"\u5feb\u901f\u805a\u7126\u5230\u641c\u7d22\u6846",
                   draft.search_focus_hotkey, Section::Hotkey, FieldId::SearchFocusHotkey, true, L"\u65e0"),
      MakeFieldRow(r4_, r4_t_, nullptr, r4_c_, L"\u7b2c\u4e09\u65b9\u8d34\u56fe\u5feb\u6377\u952e", L"",
                   draft.third_party_sticker_hotkey, Section::Hotkey, FieldId::ThirdPartyStickerHotkey, true, L"\u65e0"),
      MakeFieldRow(r5_, r5_t_, r5_s_, r5_c_, L"\u5c4f\u5e55\u622a\u56fe\u5feb\u6377\u952e", L"\u5168\u5c40\u547c\u51fa\u622a\u56fe\u529f\u80fd",
                   draft.screenshot_hotkey, Section::Hotkey, FieldId::ScreenshotHotkey, false, L"\u672a\u8bbe\u7f6e"),
      MakeResetRow(r6_, r6_t_, r6_s_, r6_c_, Section::Hotkey),
  };
}

float SettingsPanelHotkey::Render(ID2D1HwndRenderTarget* target, const DraftState& draft, ISettingsHost* host, const RECT& content_rect) {
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

  DrawSettingSectionTitle(target, host, cx, cy, title_node_, L"\u5feb\u6377\u952e");
  DrawSettingPanelBox(target, host, cx, cy, card1_);
  DrawSettingRows(target, host, cx, cy, rows);

  return max_scroll_y_;
}

}  // namespace clipd2d::ui
