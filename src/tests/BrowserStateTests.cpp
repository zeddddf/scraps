#include "core/browser/ClipBrowserService.h"

#include <string>
#include <vector>

#include "tests/TestSupport.h"

namespace clipd2d::tests {

namespace {

using clipd2d::core::browser::BrowserCommand;
using clipd2d::core::browser::BrowserCommandKind;
using clipd2d::core::browser::BrowserFilters;
using clipd2d::core::browser::BrowserScope;
using clipd2d::core::browser::BrowserState;
using clipd2d::core::browser::ClipBrowserService;
using clipd2d::core::browser::FilterChip;
using clipd2d::core::browser::FilterChipKind;
using clipd2d::core::store::ClipContentType;
using clipd2d::core::store::DateRangeFilter;

void AssertStatesEqual(const BrowserState& lhs, const BrowserState& rhs) {
  CLIPD2D_CHECK_EQ(lhs.scope, rhs.scope);
  CLIPD2D_CHECK_EQ(lhs.group_id.has_value(), rhs.group_id.has_value());
  if (lhs.group_id.has_value() && rhs.group_id.has_value()) {
    CLIPD2D_CHECK_EQ(lhs.group_id.value(), rhs.group_id.value());
  }
  CLIPD2D_CHECK_EQ(lhs.group_name, rhs.group_name);
  CLIPD2D_CHECK_EQ(lhs.search_text, rhs.search_text);
  CLIPD2D_CHECK_EQ(lhs.filters.type.has_value(), rhs.filters.type.has_value());
  CLIPD2D_CHECK_EQ(lhs.filters.date_range, rhs.filters.date_range);
  CLIPD2D_CHECK_EQ(lhs.filters.source_app_name.has_value(), rhs.filters.source_app_name.has_value());
  CLIPD2D_CHECK_EQ(lhs.filters.tag_ids.size(), rhs.filters.tag_ids.size());
  CLIPD2D_CHECK_EQ(lhs.filters.pinned_only, rhs.filters.pinned_only);
  CLIPD2D_CHECK_EQ(lhs.filters.frequent_only, rhs.filters.frequent_only);
}

size_t CountChipsOfKind(const std::vector<FilterChip>& chips, FilterChipKind kind) {
  size_t count = 0;
  for (const auto& chip : chips) {
    if (chip.kind == kind) {
      ++count;
    }
  }
  return count;
}

}  // namespace

CLIPD2D_TEST(BrowserStateApplyCommandDoesNotMutateInputArgument) {
  ClipBrowserService service;

  // 入参按值传入：命令只作用于返回的副本，调用方状态保持不变。
  BrowserState input;
  input.scope = BrowserScope::Library;
  input.group_id = 5;
  input.group_name = L"Work";
  input.search_text = L"old";
  input.filters.tag_ids = {1, 2};
  input.filters.pinned_only = true;

  BrowserCommand set_search;
  set_search.kind = BrowserCommandKind::SetSearchText;
  set_search.search_text = L"new";
  const BrowserState output = service.ApplyCommand(input, set_search);

  CLIPD2D_CHECK_EQ(input.search_text, std::wstring(L"old"));
  CLIPD2D_CHECK_EQ(input.scope, BrowserScope::Library);
  CLIPD2D_CHECK_EQ(input.group_id.value(), static_cast<std::int64_t>(5));
  CLIPD2D_CHECK_EQ(input.filters.tag_ids.size(), static_cast<size_t>(2));
  CLIPD2D_CHECK(input.filters.pinned_only);

  CLIPD2D_CHECK_EQ(output.search_text, std::wstring(L"new"));
  CLIPD2D_CHECK_EQ(output.scope, BrowserScope::Library);
}

CLIPD2D_TEST(BrowserStateSetCommandsAppliedTwiceEqualAppliedOnce) {
  ClipBrowserService service;

  // Set 型命令是幂等的：同一命令应用两次的结果必须等于应用一次。
  const BrowserState base = [] {
    BrowserState state;
    state.scope = BrowserScope::Favorites;
    return state;
  }();

  BrowserCommand set_search;
  set_search.kind = BrowserCommandKind::SetSearchText;
  set_search.search_text = L"abc";
  AssertStatesEqual(
      service.ApplyCommand(service.ApplyCommand(base, set_search), set_search),
      service.ApplyCommand(base, set_search));

  BrowserCommand select_group;
  select_group.kind = BrowserCommandKind::SelectGroup;
  select_group.group_id = 12;
  select_group.group_name = L"Work";
  AssertStatesEqual(
      service.ApplyCommand(service.ApplyCommand(base, select_group), select_group),
      service.ApplyCommand(base, select_group));

  BrowserCommand set_type;
  set_type.kind = BrowserCommandKind::SetTypeFilter;
  set_type.type = ClipContentType::Image;
  AssertStatesEqual(
      service.ApplyCommand(service.ApplyCommand(base, set_type), set_type),
      service.ApplyCommand(base, set_type));

  BrowserCommand set_date;
  set_date.kind = BrowserCommandKind::SetDateRange;
  set_date.date_range = DateRangeFilter::Last7Days;
  AssertStatesEqual(
      service.ApplyCommand(service.ApplyCommand(base, set_date), set_date),
      service.ApplyCommand(base, set_date));

  BrowserCommand select_tag;
  select_tag.kind = BrowserCommandKind::SelectTag;
  select_tag.tag_id = 7;
  AssertStatesEqual(
      service.ApplyCommand(service.ApplyCommand(base, select_tag), select_tag),
      service.ApplyCommand(base, select_tag));
}

CLIPD2D_TEST(BrowserStateBuildQueryAndPresentationAgreeOnChips) {
  ClipBrowserService service;

  // 默认态：无过滤字段 → 无 chip；query 各过滤字段保持默认。
  const BrowserState empty;
  const auto empty_presentation = service.BuildPresentation(empty);
  CLIPD2D_CHECK(empty_presentation.chips.empty());
  const auto empty_query = service.BuildQuery(empty);
  CLIPD2D_CHECK(!empty_query.type_filter.has_value());
  CLIPD2D_CHECK_EQ(empty_query.date_range, DateRangeFilter::All);
  CLIPD2D_CHECK(empty_query.tag_ids.empty());
  CLIPD2D_CHECK(!empty_query.pinned_only);
  CLIPD2D_CHECK(!empty_query.frequent_only);

  // 单一过滤器打开 → 恰好一个对应 chip，且 query 对应字段同步非默认。
  BrowserState with_search;
  with_search.search_text = L"needle";
  auto presentation = service.BuildPresentation(with_search);
  CLIPD2D_CHECK_EQ(presentation.chips.size(), static_cast<size_t>(1));
  CLIPD2D_CHECK_EQ(presentation.chips[0].kind, FilterChipKind::Search);
  CLIPD2D_CHECK_EQ(service.BuildQuery(with_search).search_text, std::string("needle"));

  BrowserState with_type;
  with_type.filters.type = ClipContentType::Image;
  presentation = service.BuildPresentation(with_type);
  CLIPD2D_CHECK_EQ(presentation.chips.size(), static_cast<size_t>(1));
  CLIPD2D_CHECK_EQ(presentation.chips[0].kind, FilterChipKind::Type);
  CLIPD2D_CHECK_EQ(service.BuildQuery(with_type).type_filter.value(), ClipContentType::Image);

  BrowserState with_date;
  with_date.filters.date_range = DateRangeFilter::Today;
  presentation = service.BuildPresentation(with_date);
  CLIPD2D_CHECK_EQ(presentation.chips.size(), static_cast<size_t>(1));
  CLIPD2D_CHECK_EQ(presentation.chips[0].kind, FilterChipKind::DateRange);
  CLIPD2D_CHECK_EQ(service.BuildQuery(with_date).date_range, DateRangeFilter::Today);

  BrowserState with_tags;
  with_tags.filters.tag_ids = {7};
  presentation = service.BuildPresentation(with_tags);
  CLIPD2D_CHECK_EQ(presentation.chips.size(), static_cast<size_t>(1));
  CLIPD2D_CHECK_EQ(presentation.chips[0].kind, FilterChipKind::Tag);
  const auto tag_query = service.BuildQuery(with_tags);
  CLIPD2D_CHECK_EQ(tag_query.tag_ids.size(), static_cast<size_t>(1));
  CLIPD2D_CHECK_EQ(tag_query.tag_ids.front(), static_cast<std::int64_t>(7));

  BrowserState with_pinned;
  with_pinned.filters.pinned_only = true;
  presentation = service.BuildPresentation(with_pinned);
  CLIPD2D_CHECK_EQ(presentation.chips.size(), static_cast<size_t>(1));
  CLIPD2D_CHECK_EQ(presentation.chips[0].kind, FilterChipKind::Pinned);
  CLIPD2D_CHECK(service.BuildQuery(with_pinned).pinned_only);

  BrowserState with_frequent;
  with_frequent.filters.frequent_only = true;
  presentation = service.BuildPresentation(with_frequent);
  CLIPD2D_CHECK_EQ(presentation.chips.size(), static_cast<size_t>(1));
  CLIPD2D_CHECK_EQ(presentation.chips[0].kind, FilterChipKind::Frequent);
  CLIPD2D_CHECK(service.BuildQuery(with_frequent).frequent_only);

  // 反向：所有过滤字段都非默认 → chip 齐全且非空。
  BrowserState full;
  full.search_text = L"all";
  full.filters.type = ClipContentType::Text;
  full.filters.date_range = DateRangeFilter::Today;
  full.filters.tag_ids = {3};
  full.filters.pinned_only = true;
  full.filters.frequent_only = true;
  presentation = service.BuildPresentation(full);
  CLIPD2D_CHECK_EQ(presentation.chips.size(), static_cast<size_t>(6));
  const auto full_query = service.BuildQuery(full);
  CLIPD2D_CHECK_EQ(full_query.search_text, std::string("all"));
  CLIPD2D_CHECK(full_query.type_filter.has_value());
  CLIPD2D_CHECK_EQ(full_query.date_range, DateRangeFilter::Today);
  CLIPD2D_CHECK_EQ(full_query.tag_ids.size(), static_cast<size_t>(1));
  CLIPD2D_CHECK(full_query.pinned_only);
  CLIPD2D_CHECK(full_query.frequent_only);
}

CLIPD2D_TEST(BrowserStateRemoveChipCommandRemovesMatchingFilter) {
  ClipBrowserService service;

  BrowserState state;
  state.search_text = L"needle";
  state.filters.tag_ids = {7, 8};

  auto presentation = service.BuildPresentation(state);
  CLIPD2D_CHECK_EQ(CountChipsOfKind(presentation.chips, FilterChipKind::Search), 1u);
  CLIPD2D_CHECK_EQ(CountChipsOfKind(presentation.chips, FilterChipKind::Tag), 2u);

  // 移除搜索 chip → ClearSearchText → Search chip 消失，Tag chip 保留。
  const auto remove_search =
      service.BuildRemoveChipCommand(state, FilterChip{FilterChipKind::Search, L"Search", 0, true});
  CLIPD2D_CHECK(remove_search.has_value());
  state = service.ApplyCommand(state, *remove_search);
  presentation = service.BuildPresentation(state);
  CLIPD2D_CHECK(state.search_text.empty());
  CLIPD2D_CHECK_EQ(CountChipsOfKind(presentation.chips, FilterChipKind::Search), 0u);
  CLIPD2D_CHECK_EQ(CountChipsOfKind(presentation.chips, FilterChipKind::Tag), 2u);

  // 多个 tag 时移除一个 tag chip → RemoveTag 精确删除对应 id。
  const auto remove_tag_seven =
      service.BuildRemoveChipCommand(state, FilterChip{FilterChipKind::Tag, L"Tag", 7, true});
  CLIPD2D_CHECK(remove_tag_seven.has_value());
  CLIPD2D_CHECK_EQ(remove_tag_seven->kind, BrowserCommandKind::RemoveTag);
  state = service.ApplyCommand(state, *remove_tag_seven);
  presentation = service.BuildPresentation(state);
  CLIPD2D_CHECK_EQ(state.filters.tag_ids.size(), static_cast<size_t>(1));
  CLIPD2D_CHECK_EQ(state.filters.tag_ids.front(), static_cast<std::int64_t>(8));
  CLIPD2D_CHECK_EQ(CountChipsOfKind(presentation.chips, FilterChipKind::Tag), 1u);

  // 只剩一个 tag chip → ClearTags → Tag chip 全部消失。
  const auto remove_last_tag =
      service.BuildRemoveChipCommand(state, FilterChip{FilterChipKind::Tag, L"Tag", 8, true});
  CLIPD2D_CHECK(remove_last_tag.has_value());
  CLIPD2D_CHECK_EQ(remove_last_tag->kind, BrowserCommandKind::ClearTags);
  state = service.ApplyCommand(state, *remove_last_tag);
  presentation = service.BuildPresentation(state);
  CLIPD2D_CHECK(state.filters.tag_ids.empty());
  CLIPD2D_CHECK_EQ(CountChipsOfKind(presentation.chips, FilterChipKind::Tag), 0u);
  CLIPD2D_CHECK(presentation.chips.empty());
}

}  // namespace clipd2d::tests
