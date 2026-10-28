#include "tests/TestSupport.h"

#include "ui/settings/SettingsDraftState.h"

// 业务核抽取计划 §2.3：设置草稿域规则矩阵。
// 这些断言就是保存正确性的守门规则——迁移/改动后必须全绿。

namespace clipd2d::tests {
namespace {

using clipd2d::ui::Section;
using clipd2d::ui::SettingsDraftState;

// 默认 AppSettings 携带合法热键，可直接作「合法基线」。
SettingsDraftState LoadedFromDefaults() {
  SettingsDraftState state;
  state.LoadFrom(core::store::AppSettings{});
  return state;
}

}  // namespace

// ── Validate：数值区间 ─────────────────────────────────────────────

CLIPD2D_TEST(SettingsDraftValidateHistoryLimitBounds) {
  auto state = LoadedFromDefaults();
  std::wstring err;

  state.draft().history_limit_text = L"";
  CLIPD2D_CHECK(!state.Validate(&err));
  CLIPD2D_CHECK(!err.empty());

  state.draft().history_limit_text = L"0";
  CLIPD2D_CHECK(!state.Validate(&err));
  state.draft().history_limit_text = L"-1";
  CLIPD2D_CHECK(!state.Validate(&err));
  state.draft().history_limit_text = L"500001";
  CLIPD2D_CHECK(!state.Validate(&err));
  state.draft().history_limit_text = L"abc";
  CLIPD2D_CHECK(!state.Validate(&err));
  state.draft().history_limit_text = L"12x";
  CLIPD2D_CHECK(!state.Validate(&err));

  state.draft().history_limit_text = L"1";
  CLIPD2D_CHECK(state.Validate(&err));
  state.draft().history_limit_text = L"500000";
  CLIPD2D_CHECK(state.Validate(&err));
}

CLIPD2D_TEST(SettingsDraftValidateRetentionDaysBounds) {
  auto state = LoadedFromDefaults();
  std::wstring err;

  state.draft().history_retention_days_text = L"";
  CLIPD2D_CHECK(!state.Validate(&err));
  state.draft().history_retention_days_text = L"-1";
  CLIPD2D_CHECK(!state.Validate(&err));
  state.draft().history_retention_days_text = L"3651";
  CLIPD2D_CHECK(!state.Validate(&err));

  state.draft().history_retention_days_text = L"0";
  CLIPD2D_CHECK(state.Validate(&err));
  state.draft().history_retention_days_text = L"3650";
  CLIPD2D_CHECK(state.Validate(&err));
}

CLIPD2D_TEST(SettingsDraftValidateSizeLimitOnlyWhenFlagOn) {
  auto state = LoadedFromDefaults();
  std::wstring err;

  // 忽略大文本开：空值/越界都拦；关：空值放行（跳过校验）。
  state.draft().ignore_large_text = true;
  state.draft().max_text_size_kb_text = L"";
  CLIPD2D_CHECK(!state.Validate(&err));
  state.draft().max_text_size_kb_text = L"0";
  CLIPD2D_CHECK(!state.Validate(&err));
  state.draft().max_text_size_kb_text = L"102401";
  CLIPD2D_CHECK(!state.Validate(&err));
  state.draft().max_text_size_kb_text = L"1";
  CLIPD2D_CHECK(state.Validate(&err));

  state.draft().ignore_large_text = false;
  state.draft().max_text_size_kb_text = L"";
  CLIPD2D_CHECK(state.Validate(&err));

  // 图片同构。
  state.draft().ignore_large_images = true;
  state.draft().max_image_size_kb_text = L"";
  CLIPD2D_CHECK(!state.Validate(&err));
  state.draft().max_image_size_kb_text = L"102401";
  CLIPD2D_CHECK(!state.Validate(&err));
  state.draft().max_image_size_kb_text = L"100";
  CLIPD2D_CHECK(state.Validate(&err));
}

// ── Validate：热键规则 ────────────────────────────────────────────

CLIPD2D_TEST(SettingsDraftValidateHotkeyFormats) {
  auto state = LoadedFromDefaults();
  std::wstring err;

  // 确定性非法：空串（解析器拒空）与 "Ctrl+"（空 token）。
  state.draft().hotkey = L"";
  CLIPD2D_CHECK(!state.Validate(&err));
  state.draft().hotkey = L"Ctrl+";
  CLIPD2D_CHECK(!state.Validate(&err));
  state.draft().hotkey = L"Ctrl+Shift+V";
  CLIPD2D_CHECK(state.Validate(&err));

  state.draft().quick_paste_hotkey = L"Ctrl+";
  CLIPD2D_CHECK(!state.Validate(&err));
  state.draft().quick_paste_hotkey = L"Alt+~";
  CLIPD2D_CHECK(state.Validate(&err));

  state.draft().screenshot_hotkey = L"";
  CLIPD2D_CHECK(!state.Validate(&err));
  state.draft().screenshot_hotkey = L"Win+Shift+S";
  CLIPD2D_CHECK(state.Validate(&err));

  state.draft().search_focus_hotkey = L"Ctrl+";
  CLIPD2D_CHECK(!state.Validate(&err));
  state.draft().search_focus_hotkey = L"Tab";
  CLIPD2D_CHECK(state.Validate(&err));
}

CLIPD2D_TEST(SettingsDraftValidateStickerHotkeyRule) {
  auto state = LoadedFromDefaults();
  std::wstring err;

  // 开启贴图但未配热键 → 拦。
  state.draft().third_party_sticker_enabled = true;
  state.draft().third_party_sticker_hotkey = L"";
  CLIPD2D_CHECK(!state.Validate(&err));

  // 开启 + 合法热键 → 过。
  state.draft().third_party_sticker_hotkey = L"F2";
  CLIPD2D_CHECK(state.Validate(&err));

  // 关闭 + 非法热键文本仍拦（只要填了就必须合法）。
  state.draft().third_party_sticker_enabled = false;
  state.draft().third_party_sticker_hotkey = L"Ctrl+";
  CLIPD2D_CHECK(!state.Validate(&err));

  // 关闭 + 空 → 过。
  state.draft().third_party_sticker_hotkey = L"";
  CLIPD2D_CHECK(state.Validate(&err));
}

// ── 脏标记：字段归属回归 ──────────────────────────────────────────

CLIPD2D_TEST(SettingsDraftSectionDirtyOwnership) {
  const SettingsDraftState state;  // 空草稿不参与；直接测 IsSectionDirty 纯函数
  core::store::AppSettings cur{};
  core::store::AppSettings next = cur;

  // General：历史上限
  next.history_limit = cur.history_limit + 1;
  CLIPD2D_CHECK(state.IsSectionDirty(Section::General, next, cur));
  CLIPD2D_CHECK(!state.IsSectionDirty(Section::Appearance, next, cur));
  CLIPD2D_CHECK(!state.IsSectionDirty(Section::Hotkey, next, cur));

  // 节归属回归：screenshot_enabled 属 General，screenshot_hotkey 属 Hotkey（防串节）。
  next = cur;
  next.screenshot_enabled = !cur.screenshot_enabled;
  CLIPD2D_CHECK(state.IsSectionDirty(Section::General, next, cur));
  CLIPD2D_CHECK(!state.IsSectionDirty(Section::Hotkey, next, cur));

  next = cur;
  next.screenshot_hotkey = L"Win+Shift+X";
  CLIPD2D_CHECK(state.IsSectionDirty(Section::Hotkey, next, cur));
  CLIPD2D_CHECK(!state.IsSectionDirty(Section::General, next, cur));

  // 各节抽一条代表字段。
  next = cur;
  next.theme = core::store::ThemeMode::Dark;
  CLIPD2D_CHECK(state.IsSectionDirty(Section::Appearance, next, cur));

  next = cur;
  next.queue_auto_pop = !cur.queue_auto_pop;
  CLIPD2D_CHECK(state.IsSectionDirty(Section::Clipboard, next, cur));

  next = cur;
  next.ignore_otp_codes = !cur.ignore_otp_codes;
  CLIPD2D_CHECK(state.IsSectionDirty(Section::Filter, next, cur));

  next = cur;
  next.image_ocr_enabled = !cur.image_ocr_enabled;
  CLIPD2D_CHECK(state.IsSectionDirty(Section::Data, next, cur));

  next = cur;
  next.enable_win_v_override = !cur.enable_win_v_override;
  CLIPD2D_CHECK(state.IsSectionDirty(Section::About, next, cur));

  // 全同 → 无节脏（必须先 LoadFrom 建立草稿基线；空草稿会把热键等文本字段清成 ""）。
  auto loaded = LoadedFromDefaults();
  CLIPD2D_CHECK(!loaded.IsDirty(cur));
}

CLIPD2D_TEST(SettingsDraftDirtyMaskBitOrderMatchesSectionEnum) {
  auto state = LoadedFromDefaults();
  const core::store::AppSettings base{};
  CLIPD2D_CHECK_EQ(state.ComputeDirtyMask(base), 0u);

  // 改一个 Filter 字段 → 只置 bit4（General=0, Appearance=1, Hotkey=2, Clipboard=3, Filter=4）。
  state.draft().ignore_otp_codes = !base.ignore_otp_codes;
  CLIPD2D_CHECK_EQ(state.ComputeDirtyMask(base), 1u << 4);

  // 改一个 About 字段 → 只置 bit6。
  state = LoadedFromDefaults();
  state.draft().enable_win_v_override = !base.enable_win_v_override;
  CLIPD2D_CHECK_EQ(state.ComputeDirtyMask(base), 1u << 6);

  // 多节同脏 → 按位或。
  state.draft().history_limit_text = L"12345";  // General 默认 10000
  CLIPD2D_CHECK_EQ(state.ComputeDirtyMask(base), (1u << 0) | (1u << 6));
}

// ── 分节重置 ──────────────────────────────────────────────────────

CLIPD2D_TEST(SettingsDraftResetSectionOnlyTouchesThatSection) {
  auto state = LoadedFromDefaults();
  // 把 General 和 Appearance 都弄脏。
  state.draft().history_limit_text = L"123";
  state.draft().start_with_windows = true;
  state.draft().theme = core::store::ThemeMode::Dark;

  state.ResetSectionToDefaults(Section::General);
  // General 回默认。
  CLIPD2D_CHECK(state.draft().history_limit_text == L"10000");
  CLIPD2D_CHECK(state.draft().start_with_windows == false);
  // Appearance 不受影响。
  CLIPD2D_CHECK(state.draft().theme == core::store::ThemeMode::Dark);
}

// ── BuildSettings：解析失败回退 base ─────────────────────────────

CLIPD2D_TEST(SettingsDraftBuildSettingsFallsBackToBaseOnParseFailure) {
  auto state = LoadedFromDefaults();
  core::store::AppSettings base{};
  base.history_limit = 4242;
  base.history_retention_days = 77;
  base.max_text_size_kb = 333;

  state.draft().history_limit_text = L"abc";
  state.draft().history_retention_days_text = L"";
  state.draft().max_text_size_kb_text = L"xyz";

  const core::store::AppSettings out = state.BuildSettings(base);
  CLIPD2D_CHECK_EQ(out.history_limit, 4242);
  CLIPD2D_CHECK_EQ(out.history_retention_days, 77);
  CLIPD2D_CHECK_EQ(out.max_text_size_kb, 333);

  // 合法数字走解析路径并夹取下限。
  state.draft().history_limit_text = L"0";
  CLIPD2D_CHECK_EQ(state.BuildSettings(base).history_limit, 1);  // max(1, stoi)
  state.draft().history_limit_text = L"200";
  CLIPD2D_CHECK_EQ(state.BuildSettings(base).history_limit, 200);
}

CLIPD2D_TEST(SettingsDraftLoadFromDefaultsEmptyHotkeys) {
  SettingsDraftState state;
  core::store::AppSettings settings{};  // 默认 quick_paste=Alt+~ / search_focus=Tab
  settings.quick_paste_hotkey.clear();
  settings.search_focus_hotkey.clear();

  state.LoadFrom(settings);
  // 空值回填缺省（LoadFrom 语义）。
  CLIPD2D_CHECK(state.draft().quick_paste_hotkey == L"Alt+~");
  CLIPD2D_CHECK(state.draft().search_focus_hotkey == L"Tab");
}

}  // namespace clipd2d::tests
