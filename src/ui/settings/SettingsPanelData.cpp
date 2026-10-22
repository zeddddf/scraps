#include "ui/settings/SettingsPanelData.h"
#include "ui/settings/ISettingsHost.h"
#include "ui/settings/SettingsRow.h"
#include "ui/settings/SettingsTypes.h"
#include "ui/settings/SettingsDrawUtil.h"
#include "ui/layout/FlexNode.h"
#include <algorithm>

namespace clipd2d::ui {

void SettingsPanelData::BuildLayout(float width, float height, ISettingsHost* host) {
  float padding = static_cast<float>(host->DipToPx(24.0f));

  if (!root_) {
    root_ = std::make_shared<FlexNode>();
    title_node_ = std::make_shared<FlexNode>();
    root_->AddChild(title_node_);

    card1_ = std::make_shared<FlexNode>();
    root_->AddChild(card1_);

    r1_ = std::make_shared<FlexNode>();
    card1_->AddChild(r1_);
    r1_txt_ = std::make_shared<FlexNode>();
    r1_txt_->FlexDirection(YGFlexDirectionRow).AlignItems(YGAlignCenter).FlexGrow(1.0f).FlexShrink(1.0f);
    r1_->AddChild(r1_txt_);
    r1_t_ = std::make_shared<FlexNode>(); r1_txt_->AddChild(r1_t_);
    r1_s_ = std::make_shared<FlexNode>(); r1_txt_->AddChild(r1_s_);
    r1_c_ = std::make_shared<FlexNode>(); r1_->AddChild(r1_c_);

    r2_ = std::make_shared<FlexNode>();
    card1_->AddChild(r2_);
    r2_txt_ = std::make_shared<FlexNode>();
    r2_txt_->FlexDirection(YGFlexDirectionRow).AlignItems(YGAlignCenter).FlexGrow(1.0f).FlexShrink(1.0f);
    r2_->AddChild(r2_txt_);
    r2_t_ = std::make_shared<FlexNode>(); r2_txt_->AddChild(r2_t_);
    r2_s_ = std::make_shared<FlexNode>(); r2_txt_->AddChild(r2_s_);
    r2_c_ = std::make_shared<FlexNode>(); r2_->AddChild(r2_c_);

    r3_ = std::make_shared<FlexNode>();
    card1_->AddChild(r3_);
    r3_txt_ = std::make_shared<FlexNode>();
    r3_txt_->FlexDirection(YGFlexDirectionRow).AlignItems(YGAlignCenter).FlexGrow(1.0f).FlexShrink(1.0f);
    r3_->AddChild(r3_txt_);
    r3_t_ = std::make_shared<FlexNode>(); r3_txt_->AddChild(r3_t_);
    r3_s_ = std::make_shared<FlexNode>(); r3_txt_->AddChild(r3_s_);
    r3_c_ = std::make_shared<FlexNode>(); r3_->AddChild(r3_c_);

    card2_ = std::make_shared<FlexNode>();
    root_->AddChild(card2_);

    r4_ = std::make_shared<FlexNode>();
    card2_->AddChild(r4_);
    r4_t_ = std::make_shared<FlexNode>(); r4_->AddChild(r4_t_);
    r4_c_ = std::make_shared<FlexNode>(); r4_->AddChild(r4_c_);

    r5_ = std::make_shared<FlexNode>();
    card2_->AddChild(r5_);
    r5_t_ = std::make_shared<FlexNode>(); r5_->AddChild(r5_t_);
    r5_c_ = std::make_shared<FlexNode>(); r5_->AddChild(r5_c_);

    r6_ = std::make_shared<FlexNode>();
    card2_->AddChild(r6_);
    r6_t_ = std::make_shared<FlexNode>(); r6_->AddChild(r6_t_);
    r6_c_ = std::make_shared<FlexNode>(); r6_->AddChild(r6_c_);

    r7_ = std::make_shared<FlexNode>();
    card2_->AddChild(r7_);
    auto r7_txt = std::make_shared<FlexNode>();
    r7_txt->FlexDirection(YGFlexDirectionColumn).JustifyContent(YGJustifyCenter);
    r7_->AddChild(r7_txt);
    r7_t_ = std::make_shared<FlexNode>(); r7_txt->AddChild(r7_t_);
    r7_s_ = std::make_shared<FlexNode>(); r7_txt->AddChild(r7_s_);
    r7_c_ = std::make_shared<FlexNode>(); r7_->AddChild(r7_c_);
  }

  root_->Width(width).FlexDirection(YGFlexDirectionColumn);

  title_node_->Height(static_cast<float>(host->DipToPx(32.0f))).Margin(YGEdgeBottom, static_cast<float>(host->DipToPx(4.0f)));

  card1_->FlexDirection(YGFlexDirectionColumn).Margin(YGEdgeBottom, static_cast<float>(host->DipToPx(24.0f)));

  r1_->Height(static_cast<float>(host->DipToPx(60.0f))).Padding(YGEdgeHorizontal, padding).FlexDirection(YGFlexDirectionRow).AlignItems(YGAlignCenter).JustifyContent(YGJustifySpaceBetween);
  r1_txt_->Margin(YGEdgeRight, static_cast<float>(host->DipToPx(16.0f)));
  r1_t_->Height(static_cast<float>(host->DipToPx(18.0f))).Width(static_cast<float>(host->DipToPx(100.0f)));
  r1_s_->Height(static_cast<float>(host->DipToPx(18.0f))).FlexGrow(1.0f).Margin(YGEdgeLeft, static_cast<float>(host->DipToPx(24.0f)));
  r1_c_->Width(static_cast<float>(host->DipToPx(100.0f))).Height(static_cast<float>(host->DipToPx(28.0f)));

  r2_->Height(static_cast<float>(host->DipToPx(60.0f))).Padding(YGEdgeHorizontal, padding).FlexDirection(YGFlexDirectionRow).AlignItems(YGAlignCenter).JustifyContent(YGJustifySpaceBetween);
  r2_txt_->Margin(YGEdgeRight, static_cast<float>(host->DipToPx(16.0f)));
  r2_t_->Height(static_cast<float>(host->DipToPx(18.0f))).Width(static_cast<float>(host->DipToPx(100.0f)));
  r2_s_->Height(static_cast<float>(host->DipToPx(18.0f))).FlexGrow(1.0f).Margin(YGEdgeLeft, static_cast<float>(host->DipToPx(24.0f)));
  r2_c_->Width(static_cast<float>(host->DipToPx(100.0f))).Height(static_cast<float>(host->DipToPx(28.0f)));

  r3_->Height(static_cast<float>(host->DipToPx(60.0f))).Padding(YGEdgeHorizontal, padding).FlexDirection(YGFlexDirectionRow).AlignItems(YGAlignCenter).JustifyContent(YGJustifySpaceBetween);
  r3_txt_->Margin(YGEdgeRight, static_cast<float>(host->DipToPx(16.0f)));
  r3_t_->Height(static_cast<float>(host->DipToPx(18.0f))).Width(static_cast<float>(host->DipToPx(100.0f)));
  r3_s_->Height(static_cast<float>(host->DipToPx(18.0f))).FlexGrow(1.0f).Margin(YGEdgeLeft, static_cast<float>(host->DipToPx(24.0f)));
  r3_c_->Width(static_cast<float>(host->DipToPx(100.0f))).Height(static_cast<float>(host->DipToPx(28.0f)));

  card2_->FlexDirection(YGFlexDirectionColumn).Margin(YGEdgeBottom, static_cast<float>(host->DipToPx(24.0f)));

  r4_->Height(static_cast<float>(host->DipToPx(60.0f))).Padding(YGEdgeHorizontal, padding).FlexDirection(YGFlexDirectionRow).AlignItems(YGAlignCenter).JustifyContent(YGJustifySpaceBetween);
  r4_t_->Height(static_cast<float>(host->DipToPx(18.0f)));
  r4_c_->Width(static_cast<float>(host->DipToPx(240.0f))).Height(static_cast<float>(host->DipToPx(28.0f)));

  r5_->Height(static_cast<float>(host->DipToPx(60.0f))).Padding(YGEdgeHorizontal, padding).FlexDirection(YGFlexDirectionRow).AlignItems(YGAlignCenter).JustifyContent(YGJustifySpaceBetween);
  r5_t_->Height(static_cast<float>(host->DipToPx(18.0f)));
  r5_c_->Width(static_cast<float>(host->DipToPx(44.0f))).Height(static_cast<float>(host->DipToPx(24.0f)));

  r6_->Height(static_cast<float>(host->DipToPx(60.0f))).Padding(YGEdgeHorizontal, padding).FlexDirection(YGFlexDirectionRow).AlignItems(YGAlignCenter).JustifyContent(YGJustifySpaceBetween);
  r6_t_->Height(static_cast<float>(host->DipToPx(18.0f)));
  r6_c_->Width(static_cast<float>(host->DipToPx(240.0f))).Height(static_cast<float>(host->DipToPx(28.0f)));
  r7_->Height(static_cast<float>(host->DipToPx(60.0f))).Padding(YGEdgeHorizontal, padding).FlexDirection(YGFlexDirectionRow).AlignItems(YGAlignCenter).JustifyContent(YGJustifySpaceBetween);
  r7_t_->Height(static_cast<float>(host->DipToPx(18.0f))).Margin(YGEdgeBottom, static_cast<float>(host->DipToPx(2.0f)));
  r7_s_->Height(static_cast<float>(host->DipToPx(14.0f)));
  r7_c_->Width(static_cast<float>(host->DipToPx(88.0f))).Height(static_cast<float>(host->DipToPx(28.0f)));

  root_->CalculateLayout(width, YGUndefined);
  max_scroll_y_ = std::max(0.0f, root_->GetHeight() - height);
}

std::vector<SettingRow> SettingsPanelData::BuildRows(const DraftState& draft, ISettingsHost* host) const {
  // 统计行（r1-r3）为 title/副题同行布局，走 Line 模式（单行裁剪不换行）；
  // Action 命中仅 {rect, kind}，section/field/option 取 HitRegion 默认值。
  const DataStats& stats = host->GetDataStats();
  db_size_text_ = FormatBytes(stats.database_bytes);
  cache_size_text_ = FormatBytes(stats.image_cache_bytes);

  SettingRow stat1 = MakeActionRow(r1_, r1_t_, r1_s_, r1_c_, L"\u6570\u636e\u5e93\u5927\u5c0f", db_size_text_.c_str(),
                                   L"\u6e05\u7a7a\u5386\u53f2", HitKind::ActionClearHistory, true);
  stat1.text_mode = SettingRow::TextMode::Line;
  SettingRow stat2 = MakeActionRow(r2_, r2_t_, r2_s_, r2_c_, L"\u56fe\u7247\u7f13\u5b58", cache_size_text_.c_str(),
                                   L"\u6e05\u7406\u7f13\u5b58", HitKind::ActionClearImageCache, true);
  stat2.text_mode = SettingRow::TextMode::Line;
  SettingRow stat3 = MakeActionRow(r3_, r3_t_, r3_s_, r3_c_, L"\u6570\u636e\u76ee\u5f55", stats.data_dir.c_str(),
                                   L"\u6253\u5f00\u6587\u4ef6\u5939", HitKind::ActionOpenDataDir, false, false);
  stat3.text_mode = SettingRow::TextMode::Line;

  std::vector<SettingRow> rows = {stat1, stat2, stat3};
  std::vector<SettingRow> bottom = {
      MakeFieldRow(r4_, r4_t_, nullptr, r4_c_, L"\u5916\u90e8\u7f16\u8f91\u5668\u8def\u5f84", L"",
                   draft.external_editor_path, Section::Data, FieldId::ExternalEditorPath, true,
                   L"\u672a\u8bbe\u7f6e", 6.0f, 10.0f),
      MakeToggleRow(r5_, r5_t_, nullptr, r5_c_, L"\u542f\u7528 OCR", L"",
                    draft.image_ocr_enabled, Section::Data, 9),
      MakeFieldRow(r6_, r6_t_, nullptr, r6_c_, L"OCR \u7a0b\u5e8f\u8def\u5f84", L"",
                   draft.ocr_executable_path, Section::Data, FieldId::OcrExecutablePath, false,
                   L"\u9ed8\u8ba4\u5185\u7f6e", 6.0f, 10.0f),
      MakeResetRow(r7_, r7_t_, r7_s_, r7_c_, Section::Data),
  };
  rows.insert(rows.end(), bottom.begin(), bottom.end());
  return rows;
}

float SettingsPanelData::Render(ID2D1HwndRenderTarget* target, const DraftState& draft, ISettingsHost* host, const RECT& content_rect) {
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

  // 行按节点归属分回两节绘制：card1_（统计）与 card2_（编辑器/OCR）。
  std::vector<SettingRow> vis_stats;
  std::vector<SettingRow> vis_tools;
  for (const auto& r : rows) {
    if (r.row == r1_.get() || r.row == r2_.get() || r.row == r3_.get()) {
      vis_stats.push_back(r);
    } else {
      vis_tools.push_back(r);
    }
  }

  float cx = static_cast<float>(content_rect.left);
  float cy = static_cast<float>(content_rect.top) - host->GetScrollY();

  DrawSettingSectionTitle(target, host, cx, cy, title_node_, L"\u6570\u636e\u4e0e\u5de5\u5177");
  DrawSettingPanelBox(target, host, cx, cy, card1_);
  DrawSettingRows(target, host, cx, cy, vis_stats);
  DrawSettingPanelBox(target, host, cx, cy, card2_);
  DrawSettingRows(target, host, cx, cy, vis_tools);

  return max_scroll_y_;
}

}  // namespace clipd2d::ui
