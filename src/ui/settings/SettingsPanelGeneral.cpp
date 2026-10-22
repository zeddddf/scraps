#include "ui/settings/SettingsPanelGeneral.h"
#include "ui/settings/ISettingsHost.h"
#include "ui/settings/SettingsRow.h"
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

    r7_ = std::make_shared<FlexNode>();
    card3_->AddChild(r7_);
    auto r7_txt = std::make_shared<FlexNode>();
    r7_txt->FlexDirection(YGFlexDirectionColumn).JustifyContent(YGJustifyCenter);
    r7_->AddChild(r7_txt);
    r7_t_ = std::make_shared<FlexNode>(); r7_txt->AddChild(r7_t_);
    r7_s_ = std::make_shared<FlexNode>(); r7_txt->AddChild(r7_s_);
    r7_c_ = std::make_shared<FlexNode>(); r7_->AddChild(r7_c_);
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


  r7_->Height(static_cast<float>(host->DipToPx(60.0f))).Padding(YGEdgeHorizontal, padding).FlexDirection(YGFlexDirectionRow).AlignItems(YGAlignCenter).JustifyContent(YGJustifySpaceBetween);
  r7_t_->Height(static_cast<float>(host->DipToPx(18.0f))).Margin(YGEdgeBottom, static_cast<float>(host->DipToPx(2.0f)));
  r7_s_->Height(static_cast<float>(host->DipToPx(14.0f)));
  r7_c_->Width(static_cast<float>(host->DipToPx(88.0f))).Height(static_cast<float>(host->DipToPx(28.0f)));
  root_->CalculateLayout(width, YGUndefined);
  max_scroll_y_ = std::max(0.0f, root_->GetHeight() - height);
}

std::vector<SettingRow> SettingsPanelGeneral::BuildRows(const DraftState& draft, ISettingsHost* /*host*/) const {
  std::vector<SettingRow> rows = {
      MakeFieldRow(r1_, r1_t_, r1_s_, r1_c_, L"\u5386\u53f2\u8bb0\u5f55\u4e0a\u9650", L"\u63a7\u5236\u4fdd\u7559\u7684\u6700\u5927\u6761\u76ee\u6570",
                   draft.history_limit_text, Section::General, FieldId::HistoryLimit),
      MakeFieldRow(r2_, r2_t_, r2_s_, r2_c_, L"\u5386\u53f2\u4fdd\u7559\u5929\u6570", L"\u8d85\u8fc7\u5929\u6570\u7684\u666e\u901a\u5386\u53f2\u662f\u81ea\u52a8\u6e05\u7406\uff0c0 \u8868\u793a\u4ec5\u6309\u6570\u91cf",
                   draft.history_retention_days_text, Section::General, FieldId::HistoryRetentionDays, false),
      MakeToggleRow(r3_, r3_t_, r3_s_, r3_c_, L"\u5f00\u673a\u81ea\u542f\u52a8", L"\u767b\u5f55 Windows \u540e\u81ea\u52a8\u542f\u52a8 Scraps",
                    draft.start_with_windows, Section::General, 1),
      MakeToggleRow(r4_, r4_t_, r4_s_, r4_c_, L"\u9996\u6b21\u8fd0\u884c\u65f6\u6253\u5f00\u9762\u677f", L"\u542f\u52a8\u540e\u81ea\u52a8\u5f39\u51fa\u4e3b\u9762\u677f\u4ee5\u786e\u8ba4\u8fd0\u884c\u72b6\u6001",
                    draft.open_panel_on_first_run, Section::General, 2, false),
  };
  std::vector<SettingRow> bottom = {
      MakeToggleRow(r5_, r5_t_, r5_s_, r5_c_, L"\u542f\u7528\u5c4f\u5e55\u622a\u56fe", L"\u542f\u7528\u6216\u7981\u7528\u5168\u5c40\u5c4f\u5e55\u622a\u56fe\u529f\u80fd\u53ca\u70ed\u952e",
                    draft.screenshot_enabled, Section::General, 16),
      MakeToggleRow(r6_, r6_t_, r6_s_, r6_c_, L"\u5e38\u9a7b\u540e\u53f0\u9884\u70ed", L"\u5f00\u542f\u540e\u622a\u56fe\u7a0b\u5e8f\u5e38\u9a7b\u540e\u53f0\uff0c\u8d77\u59cb\u54cd\u5e94\u5ef6\u8fdf\u5c0f\u4e8e 10ms",
                    draft.screenshot_prewarm, Section::General, 17, false),
      MakeResetRow(r7_, r7_t_, r7_s_, r7_c_, Section::General),
  };
  rows.insert(rows.end(), bottom.begin(), bottom.end());
  return rows;
}

float SettingsPanelGeneral::Render(ID2D1HwndRenderTarget* target, const DraftState& draft, ISettingsHost* host, const RECT& content_rect) {
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

  // P3-16b：card1/card2 先铺底再画 r1-r4，card3 在 r5 之前铺底（保持旧 z 序）。
  DrawSettingSectionTitle(target, host, cx, cy, title_node_, L"\u5e38\u89c4");
  DrawSettingPanelBox(target, host, cx, cy, card1_);
  DrawSettingPanelBox(target, host, cx, cy, card2_);
  DrawSettingPanelBox(target, host, cx, cy, card3_);
  DrawSettingRows(target, host, cx, cy, rows);

  return max_scroll_y_;
}

}  // namespace clipd2d::ui
