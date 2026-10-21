#include "ui/settings/SettingsPanelAbout.h"
#include "ui/settings/ISettingsHost.h"
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
  }

  root_->Width(width).Height(height).FlexDirection(YGFlexDirectionColumn);

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

  root_->CalculateLayout(width, YGUndefined);
  max_scroll_y_ = std::max(0.0f, root_->GetHeight() - height);
}

float SettingsPanelAbout::Render(ID2D1HwndRenderTarget* target, const DraftState& draft, ISettingsHost* host, const RECT& content_rect) {
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

  // Render using calculated rects
  DrawTextLine(target, host->GetShell().section_format(), host->GetShell().text_brush(), title_node_->GetAbsoluteRect(cx, cy), L"\u5173\u4e8e");
  FillRoundedPanel(target, card_->GetAbsoluteRect(cx, cy), 16.0f, host->GetShell().surface_brush(), host->GetShell().border_brush(), 0.5f);

  DrawParagraph(target, host->GetShell().body_bold_format(), host->GetShell().text_brush(), row1_title_->GetAbsoluteRect(cx, cy), L"Scraps");
  DrawParagraph(target, host->GetShell().small_format(), host->GetShell().muted_text_brush(), row1_subtitle_->GetAbsoluteRect(cx, cy), L"\u7248\u672c 1.0.0");
  DrawRowDivider(target, host->GetShell(), row1_->GetAbsoluteRect(cx, cy));

  RECT r2_rect = row2_->GetAbsoluteRect(cx, cy);
  r2_rect.left += host->DipToPx(24.0f); // manual padding compensation for DrawTextLine full row span
  DrawTextLine(target, host->GetShell().body_bold_format(), host->GetShell().text_brush(), r2_rect, L"\u63a5\u7ba1 Win+V");
  RECT win_v_toggle_rect = row2_toggle_->GetAbsoluteRect(cx, cy);
  DrawToggle(target, win_v_toggle_rect, draft.enable_win_v_override, host->GetShell());
  host->AddHitRegion({win_v_toggle_rect, HitKind::Toggle, Section::About, FieldId::None, 7});

  return max_scroll_y_;
}

}  // namespace clipd2d::ui
