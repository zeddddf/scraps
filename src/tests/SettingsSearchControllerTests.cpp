#include "tests/TestSupport.h"

#include "ui/settings/SettingsSearchController.h"

// 业务核抽取计划 §3.3：跨节搜索纯逻辑核测试矩阵。
// 「Activate 跳转即消费查询词」是产品行为（防目标节被继续过滤成空），必须保持全绿。

namespace clipd2d::tests {
namespace {

using clipd2d::ui::FieldId;
using clipd2d::ui::HitKind;
using clipd2d::ui::Section;
using clipd2d::ui::SelectField;
using clipd2d::ui::SettingRow;
using clipd2d::ui::SettingsSearchController;

SettingRow MakeRow(const wchar_t* title, const wchar_t* subtitle = L"") {
  SettingRow row;
  row.title_text = title;
  row.subtitle_text = subtitle;
  return row;
}

using SectionRows = std::vector<std::pair<Section, std::vector<SettingRow>>>;

SectionRows TwoSections() {
  return {
      {Section::General,
       {MakeRow(L"历史条数上限", L"保留多少条历史记录"),
        MakeRow(L"开机自启动", L"登录后自动运行")}},
      {Section::Appearance,
       {MakeRow(L"List Density", L"紧凑 / 舒适"),
        MakeRow(L"主题", L"Theme mode")}},
  };
}

}  // namespace

// ── 索引 ───────────────────────────────────────────────────────────

CLIPD2D_TEST(SettingsSearchEmptyQueryYieldsNoHits) {
  SettingsSearchController search;
  const auto outcome = search.Rebuild(L"", TwoSections());
  CLIPD2D_CHECK(!outcome.has_hits);
  CLIPD2D_CHECK(!outcome.fallback_section.has_value());
  CLIPD2D_CHECK(search.hits().empty());
  CLIPD2D_CHECK_EQ(search.hover_index(), -1);
  CLIPD2D_CHECK(search.query().empty());
}

CLIPD2D_TEST(SettingsSearchBlankQueryIsTreatedAsEmpty) {
  SettingsSearchController search;
  const auto outcome = search.Rebuild(L"   \t ", TwoSections());
  CLIPD2D_CHECK(!outcome.has_hits);
  CLIPD2D_CHECK(search.hits().empty());
  CLIPD2D_CHECK(search.query().empty());  // Trim 后为空
}

CLIPD2D_TEST(SettingsSearchMatchesTitleAndSubtitle) {
  SettingsSearchController search;

  auto by_title = search.Rebuild(L"历史条数", TwoSections());
  CLIPD2D_CHECK(by_title.has_hits);
  CLIPD2D_CHECK_EQ(search.hits().size(), 1u);
  CLIPD2D_CHECK_EQ(search.hits()[0].title, std::wstring(L"历史条数上限"));

  auto by_subtitle = search.Rebuild(L"紧凑", TwoSections());
  CLIPD2D_CHECK(by_subtitle.has_hits);
  CLIPD2D_CHECK_EQ(search.hits().size(), 1u);
  CLIPD2D_CHECK_EQ(search.hits()[0].title, std::wstring(L"List Density"));
}

CLIPD2D_TEST(SettingsSearchMatchIsCaseInsensitive) {
  SettingsSearchController search;
  const auto outcome = search.Rebuild(L"density", TwoSections());
  CLIPD2D_CHECK(outcome.has_hits);
  CLIPD2D_CHECK_EQ(search.hits().size(), 1u);
  CLIPD2D_CHECK_EQ(search.hits()[0].section, Section::Appearance);

  const auto outcome2 = search.Rebuild(L"THEME", TwoSections());
  CLIPD2D_CHECK(outcome2.has_hits);
  CLIPD2D_CHECK_EQ(search.hits()[0].title, std::wstring(L"主题"));
}

CLIPD2D_TEST(SettingsSearchPreservesPanelRowOrder) {
  // "e" 命中 List Density（Appearance）与 Theme（Appearance 副题）……换用跨面板词：
  // "发" 命中 General「开发」行与 Appearance「启发」行，验证面板顺序稳定。
  SectionRows rows = {
      {Section::General, {MakeRow(L"开发选项", L""), MakeRow(L"其他", L"")}},
      {Section::Appearance, {MakeRow(L"启发提示", L""), MakeRow(L"密度", L"")}},
  };
  SettingsSearchController search;
  const auto outcome = search.Rebuild(L"发", rows);
  CLIPD2D_CHECK(outcome.has_hits);
  CLIPD2D_CHECK_EQ(search.hits().size(), 2u);
  // 面板顺序 = General 在前 Appearance 在后；同面板内 BuildRows 行序。
  CLIPD2D_CHECK_EQ(search.hits()[0].section, Section::General);
  CLIPD2D_CHECK_EQ(search.hits()[0].title, std::wstring(L"开发选项"));
  CLIPD2D_CHECK_EQ(search.hits()[1].section, Section::Appearance);
  CLIPD2D_CHECK_EQ(search.hits()[1].title, std::wstring(L"启发提示"));
}

CLIPD2D_TEST(SettingsSearchSkipsEmptyTitleRows) {
  SectionRows rows = {{Section::General, {MakeRow(L"", L"只有副题"), MakeRow(L"有效行", L"")}}};
  SettingsSearchController search;
  const auto outcome = search.Rebuild(L"副题", rows);
  CLIPD2D_CHECK(!outcome.has_hits);  // 空标题行不参与索引
  const auto outcome2 = search.Rebuild(L"有效", rows);
  CLIPD2D_CHECK(outcome2.has_hits);
  CLIPD2D_CHECK_EQ(search.hits().size(), 1u);
}

// ── 兜底：行级 0 命中 → 节名匹配 ───────────────────────────────────

CLIPD2D_TEST(SettingsSearchFallsBackToSectionName) {
  SettingsSearchController search;
  // 「外观」不命中任何行文本 → 兜底到节名。
  const auto outcome = search.Rebuild(L"外观", TwoSections());
  CLIPD2D_CHECK(!outcome.has_hits);
  CLIPD2D_CHECK(outcome.fallback_section.has_value());
  CLIPD2D_CHECK_EQ(*outcome.fallback_section, Section::Appearance);
}

CLIPD2D_TEST(SettingsSearchFallbackMatchesSectionKeywords) {
  SettingsSearchController search;
  // 「热键」是 Hotkey 节的关键词。
  const auto outcome = search.Rebuild(L"热键", TwoSections());
  CLIPD2D_CHECK(!outcome.has_hits);
  CLIPD2D_CHECK(outcome.fallback_section.has_value());
  CLIPD2D_CHECK_EQ(*outcome.fallback_section, Section::Hotkey);
}

CLIPD2D_TEST(SettingsSearchFallbackIgnoresEmptyAndUnknownQuery) {
  CLIPD2D_CHECK(!SettingsSearchController::MatchSection(L"").has_value());
  CLIPD2D_CHECK(!SettingsSearchController::MatchSection(L"   ").has_value());
  CLIPD2D_CHECK(!SettingsSearchController::MatchSection(L"绝对不存在的词xyz").has_value());
}

// ── 导航 ───────────────────────────────────────────────────────────

CLIPD2D_TEST(SettingsSearchMoveHoverWrapsAround) {
  SettingsSearchController search;
  search.Rebuild(L"e", TwoSections());  // List Density + Theme mode → 2 命中
  CLIPD2D_CHECK_EQ(search.hits().size(), 2u);
  CLIPD2D_CHECK_EQ(search.hover_index(), 0);  // 有命中时初始高亮首条

  search.MoveHover(1);
  CLIPD2D_CHECK_EQ(search.hover_index(), 1);
  search.MoveHover(1);  // 越界绕回
  CLIPD2D_CHECK_EQ(search.hover_index(), 0);
  search.MoveHover(-1);  // 反向绕回
  CLIPD2D_CHECK_EQ(search.hover_index(), 1);
}

CLIPD2D_TEST(SettingsSearchMoveHoverScrollsVisibleWindow) {
  // 8 命中 > kMaxVisibleHits(6)：走到第 7 条时窗口必须下滑。
  static const wchar_t* kTitles[8] = {L"命中0", L"命中1", L"命中2", L"命中3",
                                      L"命中4", L"命中5", L"命中6", L"命中7"};
  SectionRows rows = {{Section::General, {}}};
  for (const wchar_t* title : kTitles) {
    rows[0].second.push_back(MakeRow(title));
  }

  SettingsSearchController search;
  search.Rebuild(L"命中", rows);
  CLIPD2D_CHECK_EQ(search.hits().size(), 8u);
  CLIPD2D_CHECK_EQ(search.window_offset(), 0);

  for (int i = 0; i < 6; ++i) {
    search.MoveHover(1);
  }
  CLIPD2D_CHECK_EQ(search.hover_index(), 6);
  CLIPD2D_CHECK_EQ(search.window_offset(), 1);  // 6+1-6

  search.MoveHover(1);  // → 7
  CLIPD2D_CHECK_EQ(search.window_offset(), 2);
  search.MoveHover(1);  // 环绕 → 0，窗口回卷
  CLIPD2D_CHECK_EQ(search.hover_index(), 0);
  CLIPD2D_CHECK_EQ(search.window_offset(), 0);
}

CLIPD2D_TEST(SettingsSearchMoveHoverOnEmptyHitsIsNoop) {
  SettingsSearchController search;
  search.Rebuild(L"", TwoSections());
  search.MoveHover(1);
  CLIPD2D_CHECK_EQ(search.hover_index(), -1);
}

CLIPD2D_TEST(SettingsSearchPreviewDoesNotChangeQueryOrNavigation) {
  SettingsSearchController search;
  search.Rebuild(L"历史", TwoSections());

  const auto jump = search.Preview(0);
  CLIPD2D_CHECK(jump.valid);
  CLIPD2D_CHECK_EQ(jump.section, Section::General);
  CLIPD2D_CHECK_EQ(jump.title, std::wstring(L"历史条数上限"));
  // Preview 只读：查询、命中、高亮全部不动。
  CLIPD2D_CHECK_EQ(search.query(), std::wstring(L"历史"));
  CLIPD2D_CHECK_EQ(search.hits().size(), 1u);
  CLIPD2D_CHECK_EQ(search.hover_index(), 0);

  const auto invalid = search.Preview(99);
  CLIPD2D_CHECK(!invalid.valid);
  const auto invalid_neg = search.Preview(-1);
  CLIPD2D_CHECK(!invalid_neg.valid);
}

// ── Activate：跳转即消费查询词（产品行为）─────────────────────────

CLIPD2D_TEST(SettingsSearchActivateConsumesQueryAndClearsHits) {
  SettingsSearchController search;
  search.Rebuild(L"历史", TwoSections());

  const auto jump = search.Activate(0);
  CLIPD2D_CHECK(jump.valid);
  CLIPD2D_CHECK_EQ(jump.section, Section::General);
  CLIPD2D_CHECK_EQ(jump.title, std::wstring(L"历史条数上限"));

  // 跳转即清查询：否则目标节会被继续过滤成「只剩一行」甚至空白。
  CLIPD2D_CHECK(search.query().empty());
  CLIPD2D_CHECK(search.hits().empty());
  CLIPD2D_CHECK_EQ(search.hover_index(), -1);
  CLIPD2D_CHECK_EQ(search.window_offset(), 0);
}

CLIPD2D_TEST(SettingsSearchActivateRejectsInvalidIndex) {
  SettingsSearchController search;
  search.Rebuild(L"历史", TwoSections());

  CLIPD2D_CHECK(!search.Activate(-1).valid);
  CLIPD2D_CHECK(!search.Activate(99).valid);
  // 无效激活不得消费查询。
  CLIPD2D_CHECK_EQ(search.query(), std::wstring(L"历史"));
  CLIPD2D_CHECK_EQ(search.hits().size(), 1u);
}

// ── Close / SetHoverIndex ──────────────────────────────────────────

CLIPD2D_TEST(SettingsSearchCloseClearsEverything) {
  SettingsSearchController search;
  search.Rebuild(L"历史", TwoSections());
  search.Close();
  CLIPD2D_CHECK(search.hits().empty());
  CLIPD2D_CHECK(search.query().empty());
  CLIPD2D_CHECK_EQ(search.hover_index(), -1);
  CLIPD2D_CHECK_EQ(search.window_offset(), 0);
}

CLIPD2D_TEST(SettingsSearchSetHoverIndexClampsToRange) {
  SettingsSearchController search;
  search.Rebuild(L"e", TwoSections());  // List Density + Theme mode → 2 命中
  search.SetHoverIndex(1);
  CLIPD2D_CHECK_EQ(search.hover_index(), 1);
  search.SetHoverIndex(99);  // 越界忽略
  CLIPD2D_CHECK_EQ(search.hover_index(), 1);
  search.SetHoverIndex(-3);  // 越界忽略
  CLIPD2D_CHECK_EQ(search.hover_index(), 1);
}

// ── HitAtPoint：几何命中映射 ───────────────────────────────────────

CLIPD2D_TEST(SettingsSearchHitAtPointMapsSlotsWithWindowOffset) {
  SettingsSearchController search;
  SectionRows rows = {{Section::General, {}}};
  static const wchar_t* kTitles[8] = {L"命中0", L"命中1", L"命中2", L"命中3",
                                      L"命中4", L"命中5", L"命中6", L"命中7"};
  for (const wchar_t* title : kTitles) {
    rows[0].second.push_back(MakeRow(title));
  }
  search.Rebuild(L"命中", rows);

  // 槽位矩形：6 个可见槽，每个高 10px。
  std::vector<RECT> slots;
  for (int i = 0; i < 6; ++i) {
    slots.push_back({0, i * 10, 100, i * 10 + 10});
  }

  POINT inside_first{5, 5};
  CLIPD2D_CHECK_EQ(search.HitAtPoint(slots, inside_first), 0);  // offset=0 + 槽0

  POINT inside_last_slot{5, 55};  // 槽 5
  CLIPD2D_CHECK_EQ(search.HitAtPoint(slots, inside_last_slot), 5);

  POINT outside{200, 5};
  CLIPD2D_CHECK_EQ(search.HitAtPoint(slots, outside), -1);
  POINT below{5, 999};
  CLIPD2D_CHECK_EQ(search.HitAtPoint(slots, below), -1);

  // 窗口下滑后槽 0 ↔ 命中 window_offset_。
  for (int i = 0; i < 7; ++i) {
    search.MoveHover(1);
  }
  CLIPD2D_CHECK_EQ(search.window_offset(), 2);
  CLIPD2D_CHECK_EQ(search.HitAtPoint(slots, inside_first), 2);  // offset=2 + 槽0
}

CLIPD2D_TEST(SettingsSearchHitAtPointBoundaryIsExclusive) {
  SettingsSearchController search;
  search.Rebuild(L"历史", TwoSections());
  std::vector<RECT> slots = {{0, 0, 100, 10}};

  POINT on_left_edge{0, 5};
  CLIPD2D_CHECK_EQ(search.HitAtPoint(slots, on_left_edge), 0);  // PtInRect 左闭
  POINT on_right_edge{100, 5};
  CLIPD2D_CHECK_EQ(search.HitAtPoint(slots, on_right_edge), -1);  // PtInRect 右开
}

// ── SettingRowMatches 纯函数（随迁语义）───────────────────────────

CLIPD2D_TEST(SettingsSearchRowMatchesTitleSubtitleAndEmptyQuery) {
  const SettingRow row = MakeRow(L"Hello World", L"副题说明");
  CLIPD2D_CHECK(clipd2d::ui::SettingRowMatches(row, L""));        // 空查询恒真
  CLIPD2D_CHECK(clipd2d::ui::SettingRowMatches(row, L"hello"));   // 标题大小写不敏感
  CLIPD2D_CHECK(clipd2d::ui::SettingRowMatches(row, L"说明"));    // 副题命中
  CLIPD2D_CHECK(!clipd2d::ui::SettingRowMatches(row, L"不存在"));
}

}  // namespace clipd2d::tests
