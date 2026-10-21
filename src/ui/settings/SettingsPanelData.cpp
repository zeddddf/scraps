#include "ui/settings/SettingsPanelData.h"
#include "ui/settings/ISettingsHost.h"
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

  root_->CalculateLayout(width, YGUndefined);
  max_scroll_y_ = std::max(0.0f, root_->GetHeight() - height);
}

float SettingsPanelData::Render(ID2D1HwndRenderTarget* target, const DraftState& draft, ISettingsHost* host, const RECT& content_rect) {
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

  DrawTextLine(target, host->GetShell().section_format(), host->GetShell().text_brush(), title_node_->GetAbsoluteRect(cx, cy), L"\u6570\u636e\u4e0e\u5de5\u5177");
  FillRoundedPanel(target, card1_->GetAbsoluteRect(cx, cy), 16.0f, host->GetShell().surface_brush(), host->GetShell().border_brush(), 0.5f);

  DrawTextLine(target, host->GetShell().body_bold_format(), host->GetShell().text_brush(), r1_t_->GetAbsoluteRect(cx, cy), L"\u6570\u636e\u5e93\u5927\u5c0f");
  DrawTextLine(target, host->GetShell().small_format(), host->GetShell().muted_text_brush(), r1_s_->GetAbsoluteRect(cx, cy), FormatBytes(host->GetDataStats().database_bytes));
  RECT clear_history_rect = r1_c_->GetAbsoluteRect(cx, cy);
  FillRoundedPanel(target, clear_history_rect, 6.0f, host->GetShell().surface_alt_brush(), host->GetShell().border_brush(), 0.5f);
  DrawTextLine(target, host->GetShell().small_format(), host->GetShell().danger_brush(), clear_history_rect, L"\u6e05\u7a7a\u5386\u53f2", DWRITE_TEXT_ALIGNMENT_CENTER);
  host->AddHitRegion({clear_history_rect, HitKind::ActionClearHistory});
  DrawRowDivider(target, host->GetShell(), r1_->GetAbsoluteRect(cx, cy));

  DrawTextLine(target, host->GetShell().body_bold_format(), host->GetShell().text_brush(), r2_t_->GetAbsoluteRect(cx, cy), L"\u56fe\u7247\u7f13\u5b58");
  DrawTextLine(target, host->GetShell().small_format(), host->GetShell().muted_text_brush(), r2_s_->GetAbsoluteRect(cx, cy), FormatBytes(host->GetDataStats().image_cache_bytes));
  RECT clear_image_cache_rect = r2_c_->GetAbsoluteRect(cx, cy);
  FillRoundedPanel(target, clear_image_cache_rect, 6.0f, host->GetShell().surface_alt_brush(), host->GetShell().border_brush(), 0.5f);
  DrawTextLine(target, host->GetShell().small_format(), host->GetShell().danger_brush(), clear_image_cache_rect, L"\u6e05\u7406\u7f13\u5b58", DWRITE_TEXT_ALIGNMENT_CENTER);
  host->AddHitRegion({clear_image_cache_rect, HitKind::ActionClearImageCache});
  DrawRowDivider(target, host->GetShell(), r2_->GetAbsoluteRect(cx, cy));

  DrawTextLine(target, host->GetShell().body_bold_format(), host->GetShell().text_brush(), r3_t_->GetAbsoluteRect(cx, cy), L"\u6570\u636e\u76ee\u5f55");
  DrawTextLine(target, host->GetShell().small_format(), host->GetShell().muted_text_brush(), r3_s_->GetAbsoluteRect(cx, cy), host->GetDataStats().data_dir);
  RECT open_data_dir_rect = r3_c_->GetAbsoluteRect(cx, cy);
  FillRoundedPanel(target, open_data_dir_rect, 6.0f, host->GetShell().surface_alt_brush(), host->GetShell().border_brush(), 0.5f);
  DrawTextLine(target, host->GetShell().small_format(), host->GetShell().text_brush(), open_data_dir_rect, L"\u6253\u5f00\u6587\u4ef6\u5939", DWRITE_TEXT_ALIGNMENT_CENTER);
  host->AddHitRegion({open_data_dir_rect, HitKind::ActionOpenDataDir});

  FillRoundedPanel(target, card2_->GetAbsoluteRect(cx, cy), 16.0f, host->GetShell().surface_brush(), host->GetShell().border_brush(), 0.5f);

  DrawParagraph(target, host->GetShell().body_bold_format(), host->GetShell().text_brush(), r4_t_->GetAbsoluteRect(cx, cy), L"\u5916\u90e8\u7f16\u8f91\u5668\u8def\u5f84");
  RECT external_editor_path_rect = r4_c_->GetAbsoluteRect(cx, cy);
  host->RegisterFieldRect(FieldId::ExternalEditorPath, external_editor_path_rect);
  FillRoundedPanel(target, external_editor_path_rect, 6.0f, host->GetShell().surface_alt_brush(), host->GetShell().border_brush(), 0.5f);
  host->AddHitRegion({external_editor_path_rect, HitKind::Field, Section::Data, FieldId::ExternalEditorPath, static_cast<int>(FieldId::ExternalEditorPath)});
  if (host->GetActiveField() != FieldId::ExternalEditorPath) {
    DrawTextLine(target, host->GetShell().small_format(), draft.external_editor_path.empty() ? host->GetShell().muted_text_brush() : host->GetShell().text_brush(), InsetRectCopy(external_editor_path_rect, host->DipToPx(10.0f), 0), draft.external_editor_path.empty() ? L"\u672a\u8bbe\u7f6e" : draft.external_editor_path);
  }
  DrawRowDivider(target, host->GetShell(), r4_->GetAbsoluteRect(cx, cy));

  DrawParagraph(target, host->GetShell().body_bold_format(), host->GetShell().text_brush(), r5_t_->GetAbsoluteRect(cx, cy), L"\u542f\u7528 OCR");
  RECT image_ocr_toggle_rect = r5_c_->GetAbsoluteRect(cx, cy);
  DrawToggle(target, image_ocr_toggle_rect, draft.image_ocr_enabled, host->GetShell());
  host->AddHitRegion({image_ocr_toggle_rect, HitKind::Toggle, Section::Data, FieldId::None, 9});
  DrawRowDivider(target, host->GetShell(), r5_->GetAbsoluteRect(cx, cy));

  DrawParagraph(target, host->GetShell().body_bold_format(), host->GetShell().text_brush(), r6_t_->GetAbsoluteRect(cx, cy), L"OCR \u7a0b\u5e8f\u8def\u5f84");
  RECT ocr_executable_path_rect = r6_c_->GetAbsoluteRect(cx, cy);
  host->RegisterFieldRect(FieldId::OcrExecutablePath, ocr_executable_path_rect);
  FillRoundedPanel(target, ocr_executable_path_rect, 6.0f, host->GetShell().surface_alt_brush(), host->GetShell().border_brush(), 0.5f);
  host->AddHitRegion({ocr_executable_path_rect, HitKind::Field, Section::Data, FieldId::OcrExecutablePath, static_cast<int>(FieldId::OcrExecutablePath)});
  if (host->GetActiveField() != FieldId::OcrExecutablePath) {
    DrawTextLine(target, host->GetShell().small_format(), draft.ocr_executable_path.empty() ? host->GetShell().muted_text_brush() : host->GetShell().text_brush(), InsetRectCopy(ocr_executable_path_rect, host->DipToPx(10.0f), 0), draft.ocr_executable_path.empty() ? L"\u9ed8\u8ba4\u5185\u7f6e" : draft.ocr_executable_path);
  }

  return max_scroll_y_;
}

}  // namespace clipd2d::ui
