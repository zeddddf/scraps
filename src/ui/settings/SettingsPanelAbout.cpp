#include "ui/settings/SettingsPanelAbout.h"
#include "ui/settings/ISettingsHost.h"
#include "ui/settings/SettingsRow.h"
#include "ui/settings/SettingsTypes.h"
#include "ui/settings/SettingsDrawUtil.h"
#include "ui/layout/FlexNode.h"
#include <algorithm>

namespace clipd2d::ui {

void SettingsPanelAbout::BuildLayout(float width, float height, ISettingsHost* host) {
  float padding = static_cast<float>(host->DipToPx(24.0f));

  if (!root_) {
    root_ = std::make_shared<FlexNode>();
    title_node_ = std::make_shared<FlexNode>();
    root_->AddChild(title_node_);

    card_ = std::make_shared<FlexNode>();
    root_->AddChild(card_);

    row1_ = std::make_shared<FlexNode>();
    card_->AddChild(row1_);

    auto row1_text_block = std::make_shared<FlexNode>();
    row1_text_block->FlexDirection(YGFlexDirectionColumn);
    row1_->AddChild(row1_text_block);

    row1_title_ = std::make_shared<FlexNode>();
    row1_subtitle_ = std::make_shared<FlexNode>();
    row1_text_block->AddChild(row1_title_);
    row1_text_block->AddChild(row1_subtitle_);

    row2_ = std::make_shared<FlexNode>();
    card_->AddChild(row2_);

    auto row2_title = std::make_shared<FlexNode>();
    row2_title->Width(100.0f).HeightAuto();
    row2_->AddChild(row2_title);

    row2_toggle_ = std::make_shared<FlexNode>();
    row2_->AddChild(row2_toggle_);

    row3_ = std::make_shared<FlexNode>();
    card_->AddChild(row3_);
    auto row3_txt = std::make_shared<FlexNode>();
    row3_txt->FlexDirection(YGFlexDirectionColumn).JustifyContent(YGJustifyCenter);
    row3_->AddChild(row3_txt);
    row3_t_ = std::make_shared<FlexNode>(); row3_txt->AddChild(row3_t_);
    row3_s_ = std::make_shared<FlexNode>(); row3_txt->AddChild(row3_s_);
    row3_c_ = std::make_shared<FlexNode>(); row3_->AddChild(row3_c_);
  }

  root_->Width(width).FlexDirection(YGFlexDirectionColumn);  // 根高自适应内容：钉死视口高会让 max_scroll_y 恒为 0（滚动失效根因）

  title_node_->Height(static_cast<float>(host->DipToPx(32.0f)))
            .Margin(YGEdgeBottom, static_cast<float>(host->DipToPx(4.0f)));

  card_->FlexDirection(YGFlexDirectionColumn);

  row1_->Height(static_cast<float>(host->DipToPx(60.0f)))
      .Padding(YGEdgeHorizontal, padding)
      .JustifyContent(YGJustifyCenter); 

  row1_title_->Height(static_cast<float>(host->DipToPx(20.0f)));
  row1_subtitle_->Height(static_cast<float>(host->DipToPx(20.0f)));

  row2_->Height(static_cast<float>(host->DipToPx(60.0f)))
      .Padding(YGEdgeHorizontal, padding)
      .FlexDirection(YGFlexDirectionRow)
      .AlignItems(YGAlignCenter)
      .JustifyContent(YGJustifySpaceBetween);

  row2_toggle_->Width(static_cast<float>(host->DipToPx(44.0f)))
             .Height(static_cast<float>(host->DipToPx(18.0f)));

  row3_->Height(static_cast<float>(host->DipToPx(60.0f))).Padding(YGEdgeHorizontal, padding).FlexDirection(YGFlexDirectionRow).AlignItems(YGAlignCenter).JustifyContent(YGJustifySpaceBetween);
  row3_t_->Height(static_cast<float>(host->DipToPx(18.0f))).Margin(YGEdgeBottom, static_cast<float>(host->DipToPx(2.0f)));
  row3_s_->Height(static_cast<float>(host->DipToPx(14.0f)));
  row3_c_->Width(static_cast<float>(host->DipToPx(88.0f))).Height(static_cast<float>(host->DipToPx(28.0f)));

  root_->CalculateLayout(width, YGUndefined);
  max_scroll_y_ = std::max(0.0f, root_->GetHeight() - height);
}

std::vector<SettingRow> SettingsPanelAbout::BuildRows(const DraftState& draft, ISettingsHost* /*host*/) const {
  // row2 旧行为是标题画在整行矩形（左缩 24px）+ DrawTextLine，
  // 用 title_pad_left_dip + Line 模式精确复刻。
  SettingRow about_info = MakeInfoRow(row1_, row1_title_, row1_subtitle_, L"Scraps", L"\u7248\u672c 1.0.0");
  SettingRow about_toggle = MakeToggleRow(row2_, row2_, nullptr, row2_toggle_, L"\u63a5\u7ba1 Win+V", L"",
                                          draft.enable_win_v_override, Section::About, 7, false);
  about_toggle.text_mode = SettingRow::TextMode::Line;
  about_toggle.title_pad_left_dip = 24.0f;
  return {about_info, about_toggle, MakeResetRow(row3_, row3_t_, row3_s_, row3_c_, Section::About)};
}

float SettingsPanelAbout::Render(ID2D1HwndRenderTarget* target, const DraftState& draft, ISettingsHost* host, const RECT& content_rect) {
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

  DrawSettingSectionTitle(target, host, cx, cy, title_node_, L"\u5173\u4e8e");
  DrawSettingPanelBox(target, host, cx, cy, card_);
  DrawSettingRows(target, host, cx, cy, rows);

  return max_scroll_y_;
}

}  // namespace clipd2d::ui
