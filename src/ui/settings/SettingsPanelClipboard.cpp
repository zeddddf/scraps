#include "ui/settings/SettingsPanelClipboard.h"
#include "ui/settings/ISettingsHost.h"
#include "ui/settings/SettingsRow.h"
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

    r7_ = std::make_shared<FlexNode>();
    card1_->AddChild(r7_);
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


  r7_->Height(static_cast<float>(host->DipToPx(60.0f))).Padding(YGEdgeHorizontal, padding).FlexDirection(YGFlexDirectionRow).AlignItems(YGAlignCenter).JustifyContent(YGJustifySpaceBetween);
  r7_t_->Height(static_cast<float>(host->DipToPx(18.0f))).Margin(YGEdgeBottom, static_cast<float>(host->DipToPx(2.0f)));
  r7_s_->Height(static_cast<float>(host->DipToPx(14.0f)));
  r7_c_->Width(static_cast<float>(host->DipToPx(88.0f))).Height(static_cast<float>(host->DipToPx(28.0f)));
  root_->CalculateLayout(width, YGUndefined);
  max_scroll_y_ = std::max(0.0f, root_->GetHeight() - height);
}

std::vector<SettingRow> SettingsPanelClipboard::BuildRows(const DraftState& draft, ISettingsHost* /*host*/) const {
  // P3-16b：命中值（Section/option）与旧手写逐项一致；无副题行传 nullptr。
  return {
      MakeToggleRow(r1_, r1_t_, r1_s_, r1_c_, L"\u542f\u7528\u526a\u8d34\u677f\u76d1\u542c", L"\u5168\u5c40\u76d1\u542c\u5e76\u4fdd\u5b58\u526a\u8d34\u677f\u53d8\u5316",
                    draft.monitoring_enabled, Section::Clipboard, 4),
      MakeSelectRow(r2_, r2_t_, nullptr, r2_c_, L"\u7a97\u53e3\u4f4d\u7f6e\u6a21\u5f0f", L"",
                    Section::Clipboard, SelectField::WindowPositionMode),
      MakeToggleRow(r3_, r3_t_, nullptr, r3_c_, L"\u6253\u5f00\u65f6\u805a\u7126\u641c\u7d22\u6846", L"",
                    draft.focus_search_on_open, Section::Clipboard, 5),
      MakeSelectRow(r4_, r4_t_, r4_s_, r4_c_, L"\u5355\u51fb\u7c98\u8d34\u6a21\u5f0f", L"\u70b9\u51fb\u5217\u8868\u9879\u65f6\u7684\u9ed8\u8ba4\u884c\u4e3a",
                    Section::Clipboard, SelectField::ClickPasteMode),
      MakeToggleRow(r5_, r5_t_, r5_s_, r5_c_, L"\u590d\u7528\u540e\u7f6e\u9876", L"\u7c98\u8d34\u5386\u53f2\u8bb0\u5f55\u540e\u5c06\u5176\u63d0\u5347\u81f3\u5217\u8868\u9879\u9876\u90e8",
                    draft.promote_pasted_clip_on_reuse, Section::Clipboard, 10),
      MakeToggleRow(r6_, r6_t_, nullptr, r6_c_, L"\u81ea\u52a8\u5f39\u51fa\u961f\u5217", L"",
                    draft.queue_auto_pop, Section::Clipboard, 6, false),
      MakeResetRow(r7_, r7_t_, r7_s_, r7_c_, Section::Clipboard),
  };
}

float SettingsPanelClipboard::Render(ID2D1HwndRenderTarget* target, const DraftState& draft, ISettingsHost* host, const RECT& content_rect) {
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

  DrawSettingSectionTitle(target, host, cx, cy, title_node_, L"\u526a\u8d34\u677f");
  DrawSettingPanelBox(target, host, cx, cy, card1_);
  DrawSettingRows(target, host, cx, cy, rows);

  return max_scroll_y_;
}

}  // namespace clipd2d::ui
