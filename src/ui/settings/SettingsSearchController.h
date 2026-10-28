#pragma once

// 业务核抽取计划 §3：跨节搜索的纯逻辑核。
// 索引构建 / 命中导航 / 预览与跳转决策 / 节名兜底 / 几何命中映射都在这里；
// 绘制（RenderSearchResults）与布局几何（UpdateSearchResultsLayout）留在
// SettingsWindow 壳。不依赖 HWND / D2D / textbox。
//
// 产品语义（测试固化，改动即行为变化）：
//  - Activate「跳转即消费查询词」：清空查询与结果，防止目标节被继续过滤成空。
//  - Preview 只读：给出跳转意图，不改查询、不改导航状态。
//  - 行级 0 命中时节名兜底只切节、不带滚动意图（与 Preview 的 pending_scroll 不同）。

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <windows.h>

#include "ui/settings/SettingsRow.h"
#include "ui/settings/SettingsTypes.h"

namespace clipd2d::ui {

// 原 SettingsWindow::SearchHit 平移。
struct SettingsSearchHit {
  Section section = Section::General;
  std::wstring title;
  std::wstring subtitle;
  HitKind hit_kind = HitKind::Toggle;
  FieldId field_id = FieldId::None;
  SelectField select_field = SelectField::None;
  int hit_option = 0;
};

class SettingsSearchController {
 public:
  // 搜索结果下拉可见行数上限；渲染壳的行数/页脚布局共用此值。
  static constexpr int kMaxVisibleHits = 6;

  struct Jump {
    Section section = Section::General;
    std::wstring title;
    bool valid = false;
  };

  struct RebuildOutcome {
    bool has_hits = false;
    // 行级 0 命中时的节名兜底（MatchSection 命中才有值）；只切节、不滚动。
    std::optional<Section> fallback_section;
  };

  // 建立索引。query 会被 Trim；空查询 → 0 命中、无兜底。
  // section_rows 由壳按面板顺序收集（BuildRows），pair.first = 该面板的节。
  RebuildOutcome Rebuild(std::wstring_view query,
                         const std::vector<std::pair<Section, std::vector<SettingRow>>>& section_rows);

  // 节名/关键词兜底（纯函数）：命中返回该节；空查询或无匹配返回 nullopt。
  static std::optional<Section> MatchSection(std::wstring_view query);

  const std::vector<SettingsSearchHit>& hits() const { return hits_; }
  const std::wstring& query() const { return query_; }
  int hover_index() const { return hover_index_; }    // -1 = 无结果
  int window_offset() const { return window_offset_; }

  // ↑↓ 环绕导航 + 可见窗夹取；调用方随后 Preview(hover_index()) 应用预览。
  void MoveHover(int delta);

  // 鼠标直设高亮（不预览、不动可见窗）——移开后保留键盘选中项的现状语义。
  void SetHoverIndex(int index);

  // 只读跳转意图；不改查询、不改导航状态。
  Jump Preview(int index) const;

  // 消费查询词并清空结果，返回跳转目标；textbox/EndEditing/Invalidate 由壳执行。
  Jump Activate(int index);

  // 清空结果与查询（Esc / 关窗 / 重开面板路径共用）。
  void Close();

  // hit_rects：可见槽位矩形（槽 i ↔ hits[window_offset_ + i]）。
  int HitAtPoint(const std::vector<RECT>& hit_rects, POINT pt) const;

 private:
  std::vector<SettingsSearchHit> hits_;
  std::wstring query_;
  int hover_index_ = -1;
  int window_offset_ = 0;
};

}  // namespace clipd2d::ui
