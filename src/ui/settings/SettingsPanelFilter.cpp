#include "ui/settings/SettingsPanelFilter.h"
#include "ui/settings/ISettingsHost.h"
#include "ui/settings/SettingsRow.h"
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

    r7_ = std::make_shared<FlexNode>();
    c3_->AddChild(r7_);
    auto r7_txt = std::make_shared<FlexNode>();
    r7_txt->FlexDirection(YGFlexDirectionColumn).JustifyContent(YGJustifyCenter);
    r7_->AddChild(r7_txt);
    r7_t_ = std::make_shared<FlexNode>(); r7_txt->AddChild(r7_t_);
    r7_s_ = std::make_shared<FlexNode>(); r7_txt->AddChild(r7_s_);
    r7_c_ = std::make_shared<FlexNode>(); r7_->AddChild(r7_c_);
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
  r7_->Height(static_cast<float>(host->DipToPx(60.0f))).Padding(YGEdgeHorizontal, padding).FlexDirection(YGFlexDirectionRow).AlignItems(YGAlignCenter).JustifyContent(YGJustifySpaceBetween);
  r7_t_->Height(static_cast<float>(host->DipToPx(18.0f))).Margin(YGEdgeBottom, static_cast<float>(host->DipToPx(2.0f)));
  r7_s_->Height(static_cast<float>(host->DipToPx(14.0f)));
  r7_c_->Width(static_cast<float>(host->DipToPx(88.0f))).Height(static_cast<float>(host->DipToPx(28.0f)));

  root_->CalculateLayout(width, YGUndefined);
  max_scroll_y_ = std::max(0.0f, root_->GetHeight() - height);
}

std::vector<SettingRow> SettingsPanelFilter::BuildRows(const DraftState& draft, ISettingsHost* /*host*/) const {
  // r1-r5 走声明式行视图；r6 自定义黑名单区是手写块（无面板底+双向 16px 缩进），
  // 不在此列表中，由 Render 单独按搜索词决定显隐。
  return {
      MakeToggleRow(r1_, r1_t_, r1_s_, r1_c_, L"\u5ffd\u7565\u5bc6\u7801\u7ba1\u7406\u5668", L"\u81ea\u52a8\u5ffd\u7565 1Password, KeePass \u7b49\u5e38\u89c1\u5bc6\u7801\u5de5\u5177",
                    draft.auto_ignore_password_managers, Section::Filter, 11),
      MakeToggleRow(r2_, r2_t_, r2_s_, r2_c_, L"\u5ffd\u7565\u9a8c\u8bc1\u7801", L"\u4e0d\u8bb0\u5f55 4-8 \u4f4d\u7eaf\u6570\u5b57\u6216\u5b57\u6bcd\u9a8c\u8bc1\u7801",
                    draft.ignore_otp_codes, Section::Filter, 14),
      MakeToggleRow(r3_, r3_t_, r3_s_, r3_c_, L"\u5ffd\u7565\u590d\u6742\u5bc6\u7801", L"\u5ffd\u7565\u5305\u542b\u5927\u5c0f\u5199\u3001\u6570\u5b57\u548c\u7b26\u53f7\u7684\u968f\u673a\u5b57\u7b26\u4e32",
                    draft.ignore_complex_passwords, Section::Filter, 15, false),
      MakeFieldToggleRow(r4_, r4_t_, r4_s_, r4_c_input_, r4_c_toggle_,
                         L"\u8d85\u5927\u6587\u672c\u62e6\u622a", L"\u62d2\u7edd\u8bb0\u5f55\u8d85\u8fc7\u9650\u5236\u7684\u6587\u672c\uff08\u5355\u4f4d: KB\uff09",
                         draft.max_text_size_kb_text, draft.ignore_large_text,
                         Section::Filter, FieldId::MaxTextSize, 12, true, L"", 6.0f, 8.0f),
      MakeFieldToggleRow(r5_, r5_t_, r5_s_, r5_c_input_, r5_c_toggle_,
                         L"\u8d85\u5927\u56fe\u7247\u62e6\u622a", L"\u62d2\u7edd\u8bb0\u5f55\u8d85\u8fc7\u9650\u5236\u7684\u56fe\u7247\uff08\u5355\u4f4d: KB\uff09",
                         draft.max_image_size_kb_text, draft.ignore_large_images,
                         Section::Filter, FieldId::MaxImageSize, 13, false, L"", 6.0f, 8.0f),
      MakeResetRow(r7_, r7_t_, r7_s_, r7_c_, Section::Filter),
  };
}

float SettingsPanelFilter::Render(ID2D1HwndRenderTarget* target, const DraftState& draft, ISettingsHost* host, const RECT& content_rect) {
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

  // 行按节点归属分回两节绘制（r6 自定义黑名单块是手写区，不在此列表中）。
  std::vector<SettingRow> vis_strategy;
  std::vector<SettingRow> vis_limit;
  for (const auto& r : rows) {
    if (r.row == r1_.get() || r.row == r2_.get() || r.row == r3_.get()) {
      vis_strategy.push_back(r);
    } else {
      vis_limit.push_back(r);
    }
  }

  float cx = static_cast<float>(content_rect.left);
  float cy = static_cast<float>(content_rect.top) - host->GetScrollY();

  DrawSettingSectionTitle(target, host, cx, cy, c1_title_, L"\u667a\u80fd\u7b56\u7565");
  DrawSettingPanelBox(target, host, cx, cy, c1_);
  DrawSettingRows(target, host, cx, cy, vis_strategy);

  DrawSettingSectionTitle(target, host, cx, cy, c2_title_, L"\u4f53\u79ef\u9650\u5236");
  DrawSettingPanelBox(target, host, cx, cy, c2_);
  DrawSettingRows(target, host, cx, cy, vis_limit);

  DrawSettingSectionTitle(target, host, cx, cy, c3_title_, L"\u81ea\u5b9a\u4e49\u9ed1\u540d\u5355");
  DrawSettingPanelBox(target, host, cx, cy, c3_);
  RECT custom_blacklist_rect = InsetRectCopy(r6_->GetAbsoluteRect(cx, cy), host->DipToPx(1.0f), host->DipToPx(1.0f));
  host->RegisterFieldRect(FieldId::CustomBlacklist, custom_blacklist_rect);
  host->AddHitRegion({custom_blacklist_rect, HitKind::Field, Section::Filter, FieldId::CustomBlacklist, static_cast<int>(FieldId::CustomBlacklist)});
  if (host->GetActiveField() != FieldId::CustomBlacklist) {
    DrawParagraph(target, host->GetShell().small_format(), draft.custom_blacklist_apps.empty() ? host->GetShell().muted_text_brush() : host->GetShell().text_brush(), InsetRectCopy(custom_blacklist_rect, host->DipToPx(16.0f), host->DipToPx(16.0f)), draft.custom_blacklist_apps.empty() ? L"\u4f8b: keepass.exe, passwordmanager.exe" : draft.custom_blacklist_apps);
  }

  return max_scroll_y_;
}

}  // namespace clipd2d::ui
