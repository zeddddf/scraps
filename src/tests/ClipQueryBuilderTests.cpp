#include "core/browser/ClipBrowserService.h"
#include "core/store/ClipRepository.Internal.h"

#include "tests/TestSupport.h"

namespace clipd2d::tests {

CLIPD2D_TEST(ClipQueryBuilderBuildsGroupScopeWithFilters) {
  clipd2d::core::browser::BrowserState state;
  state.scope = clipd2d::core::browser::BrowserScope::Library;
  state.group_id = 42;
  state.group_name = L"Work";
  state.search_text = L"hello";
  state.filters.type = clipd2d::core::store::ClipContentType::Image;
  state.filters.date_range = clipd2d::core::store::DateRangeFilter::Last7Days;
  state.filters.tag_ids = {7, 8};
  state.filters.frequent_only = true;

  const auto query = clipd2d::core::browser::ClipQueryBuilder::Build(state);
  CLIPD2D_CHECK_EQ(query.view, clipd2d::core::store::ClipView::Group);
  CLIPD2D_CHECK(query.storage_tier_filter.has_value());
  CLIPD2D_CHECK_EQ(query.storage_tier_filter.value(), clipd2d::core::store::StorageTier::Library);
  CLIPD2D_CHECK(query.group_id.has_value());
  CLIPD2D_CHECK_EQ(query.group_id.value(), 42);
  CLIPD2D_CHECK_EQ(query.group_name, L"Work");
  CLIPD2D_CHECK_EQ(query.search_text, "hello");
  CLIPD2D_CHECK(query.type_filter.has_value());
  CLIPD2D_CHECK_EQ(query.type_filter.value(), clipd2d::core::store::ClipContentType::Image);
  CLIPD2D_CHECK_EQ(query.date_range, clipd2d::core::store::DateRangeFilter::Last7Days);
  CLIPD2D_CHECK_EQ(query.tag_ids.size(), static_cast<size_t>(2));
  CLIPD2D_CHECK(query.frequent_only);
}

CLIPD2D_TEST(ClipBrowserServiceBuildPresentationEmitsExpectedChips) {
  clipd2d::core::browser::BrowserState state;
  state.scope = clipd2d::core::browser::BrowserScope::Library;
  state.group_id = 9;
  state.group_name = L"Archive";
  state.search_text = L"ocr";
  state.filters.type = clipd2d::core::store::ClipContentType::Text;
  state.filters.date_range = clipd2d::core::store::DateRangeFilter::Today;
  state.filters.source_app_name = L"WeChat";
  state.filters.tag_ids = {3};
  state.filters.pinned_only = true;
  state.filters.frequent_only = true;

  clipd2d::core::browser::ClipBrowserService service;
  const auto presentation = service.BuildPresentation(state);

  CLIPD2D_CHECK_EQ(presentation.title, L"Archive");
  CLIPD2D_CHECK(!presentation.show_time_groups);
  CLIPD2D_CHECK_EQ(presentation.chips.size(), static_cast<size_t>(8));
  CLIPD2D_CHECK_EQ(presentation.chips[0].kind, clipd2d::core::browser::FilterChipKind::Search);
  CLIPD2D_CHECK_EQ(presentation.chips[1].kind, clipd2d::core::browser::FilterChipKind::Type);
  CLIPD2D_CHECK_EQ(presentation.chips[2].kind, clipd2d::core::browser::FilterChipKind::DateRange);
  CLIPD2D_CHECK_EQ(presentation.chips[3].kind, clipd2d::core::browser::FilterChipKind::SourceApp);
  CLIPD2D_CHECK_EQ(presentation.chips[4].kind, clipd2d::core::browser::FilterChipKind::Tag);
  CLIPD2D_CHECK_EQ(presentation.chips[5].kind, clipd2d::core::browser::FilterChipKind::Pinned);
  CLIPD2D_CHECK_EQ(presentation.chips[6].kind, clipd2d::core::browser::FilterChipKind::Frequent);
  CLIPD2D_CHECK_EQ(presentation.chips[7].kind, clipd2d::core::browser::FilterChipKind::GroupContext);
}

CLIPD2D_TEST(ClipBrowserServiceBuildResultKeepsQueryAndPresentationInSync) {
  clipd2d::core::browser::BrowserState state;
  state.scope = clipd2d::core::browser::BrowserScope::Queue;
  state.search_text = L"hello";

  clipd2d::core::browser::ClipBrowserService service;
  const auto result = service.BuildResult(state);

  CLIPD2D_CHECK_EQ(result.query.view, clipd2d::core::store::ClipView::Queue);
  CLIPD2D_CHECK(result.query.queue_only);
  CLIPD2D_CHECK_EQ(result.query.search_text, "hello");
  CLIPD2D_CHECK_EQ(result.presentation.title, L"\u961f\u5217");
  CLIPD2D_CHECK(!result.presentation.show_time_groups);
}

CLIPD2D_TEST(ClipBrowserServiceApplyCommandUpdatesScopeAndFilters) {
  clipd2d::core::browser::ClipBrowserService service;
  clipd2d::core::browser::BrowserState state;

  clipd2d::core::browser::BrowserCommand command;
  command.kind = clipd2d::core::browser::BrowserCommandKind::SelectGroup;
  command.group_id = 12;
  command.group_name = L"Work";
  state = service.ApplyCommand(state, command);
  CLIPD2D_CHECK_EQ(state.scope, clipd2d::core::browser::BrowserScope::Library);
  CLIPD2D_CHECK(state.group_id.has_value());
  CLIPD2D_CHECK_EQ(state.group_id.value(), 12);
  CLIPD2D_CHECK_EQ(state.group_name, L"Work");

  command = {};
  command.kind = clipd2d::core::browser::BrowserCommandKind::SetTypeFilter;
  command.type = clipd2d::core::store::ClipContentType::Image;
  state = service.ApplyCommand(state, command);
  CLIPD2D_CHECK(state.filters.type.has_value());
  CLIPD2D_CHECK_EQ(state.filters.type.value(), clipd2d::core::store::ClipContentType::Image);

  command = {};
  command.kind = clipd2d::core::browser::BrowserCommandKind::ToggleFrequentOnly;
  state = service.ApplyCommand(state, command);
  CLIPD2D_CHECK(state.filters.frequent_only);

  command = {};
  command.kind = clipd2d::core::browser::BrowserCommandKind::ClearAllStructuredFilters;
  state = service.ApplyCommand(state, command);
  CLIPD2D_CHECK(!state.filters.type.has_value());
  CLIPD2D_CHECK_EQ(state.filters.date_range, clipd2d::core::store::DateRangeFilter::All);
  CLIPD2D_CHECK(state.filters.tag_ids.empty());
  CLIPD2D_CHECK(!state.filters.frequent_only);
}

CLIPD2D_TEST(ClipBrowserServiceApplyCommandCanClearIndividualFilterKinds) {
  clipd2d::core::browser::ClipBrowserService service;
  clipd2d::core::browser::BrowserState state;
  state.search_text = L"hello";
  state.filters.date_range = clipd2d::core::store::DateRangeFilter::Today;
  state.filters.source_app_name = L"WeChat";
  state.filters.tag_ids = {4, 9};
  state.filters.pinned_only = true;
  state.filters.frequent_only = true;

  state = service.ApplyCommand(
      state,
      clipd2d::core::browser::BrowserCommand{clipd2d::core::browser::BrowserCommandKind::ClearSearchText});
  CLIPD2D_CHECK(state.search_text.empty());

  state = service.ApplyCommand(
      state,
      clipd2d::core::browser::BrowserCommand{clipd2d::core::browser::BrowserCommandKind::ClearDateRange});
  CLIPD2D_CHECK_EQ(state.filters.date_range, clipd2d::core::store::DateRangeFilter::All);

  state = service.ApplyCommand(
      state,
      clipd2d::core::browser::BrowserCommand{clipd2d::core::browser::BrowserCommandKind::ClearSourceAppFilter});
  CLIPD2D_CHECK(!state.filters.source_app_name.has_value());

  clipd2d::core::browser::BrowserCommand remove_tag{};
  remove_tag.kind = clipd2d::core::browser::BrowserCommandKind::RemoveTag;
  remove_tag.tag_id = 4;
  state = service.ApplyCommand(state, remove_tag);
  CLIPD2D_CHECK_EQ(state.filters.tag_ids.size(), static_cast<size_t>(1));
  CLIPD2D_CHECK_EQ(state.filters.tag_ids.front(), 9);

  state = service.ApplyCommand(
      state,
      clipd2d::core::browser::BrowserCommand{clipd2d::core::browser::BrowserCommandKind::ClearPinnedOnly});
  CLIPD2D_CHECK(!state.filters.pinned_only);

  state = service.ApplyCommand(
      state,
      clipd2d::core::browser::BrowserCommand{clipd2d::core::browser::BrowserCommandKind::ClearFrequentOnly});
  CLIPD2D_CHECK(!state.filters.frequent_only);
}

CLIPD2D_TEST(ClipBrowserServiceBuildRemoveChipCommandMapsChipKindsToCommands) {
  clipd2d::core::browser::ClipBrowserService service;
  clipd2d::core::browser::BrowserState state;
  state.scope = clipd2d::core::browser::BrowserScope::Library;
  state.group_id = 11;
  state.group_name = L"Work";
  state.filters.tag_ids = {7, 8};

  auto command = service.BuildRemoveChipCommand(
      state,
      clipd2d::core::browser::FilterChip{
          clipd2d::core::browser::FilterChipKind::Tag,
          L"Tag",
          7,
          true});
  CLIPD2D_CHECK(command.has_value());
  CLIPD2D_CHECK_EQ(command->kind, clipd2d::core::browser::BrowserCommandKind::RemoveTag);
  CLIPD2D_CHECK(command->tag_id.has_value());
  CLIPD2D_CHECK_EQ(command->tag_id.value(), 7);

  command = service.BuildRemoveChipCommand(
      state,
      clipd2d::core::browser::FilterChip{
          clipd2d::core::browser::FilterChipKind::GroupContext,
          L"Group: Work",
          11,
          true});
  CLIPD2D_CHECK(command.has_value());
  CLIPD2D_CHECK_EQ(command->kind, clipd2d::core::browser::BrowserCommandKind::SetScopeLibrary);

  command = service.BuildRemoveChipCommand(
      state,
      clipd2d::core::browser::FilterChip{
          clipd2d::core::browser::FilterChipKind::SourceApp,
          L"Source: WeChat",
          0,
          true});
  CLIPD2D_CHECK(command.has_value());
  CLIPD2D_CHECK_EQ(command->kind, clipd2d::core::browser::BrowserCommandKind::ClearSourceAppFilter);
}

CLIPD2D_TEST(ClipQueryBuilderBuildMatchQueryKeepsPlainQueriesUnchanged) {
  CLIPD2D_CHECK_EQ(
      clipd2d::core::store::BuildMatchQuery("hello world"),
      std::string("\"hello\"* AND \"world\"*"));
  CLIPD2D_CHECK_EQ(
      clipd2d::core::store::BuildMatchQuery("中文测试"),
      std::string("\"中文测试\"*"));
  CLIPD2D_CHECK_EQ(clipd2d::core::store::BuildMatchQuery("   "), std::string());
}

CLIPD2D_TEST(ClipQueryBuilderBuildMatchQueryEscapesEmbeddedQuotes) {
  // FTS5 字符串字面量规则：内部的 " 翻倍转义，禁止逃逸出引号短语。
  CLIPD2D_CHECK_EQ(
      clipd2d::core::store::BuildMatchQuery("a\"b"),
      std::string("\"a\"\"b\"*"));
  CLIPD2D_CHECK_EQ(
      clipd2d::core::store::BuildMatchQuery("\""),
      std::string("\"\"\"\"*"));
}

CLIPD2D_TEST(ClipQueryBuilderBuildMatchQueryNeutralizesOperatorInjection) {
  // 引号逃逸后 NEAR/OR/NOT 一律以短语形式出现，MATCH 表达式不可被注入。
  CLIPD2D_CHECK_EQ(
      clipd2d::core::store::BuildMatchQuery("NEAR OR NOT"),
      std::string("\"NEAR\"* AND \"OR\"* AND \"NOT\"*"));
  CLIPD2D_CHECK_EQ(
      clipd2d::core::store::BuildMatchQuery("x\" OR \"y"),
      std::string("\"x\"\"\"* AND \"OR\"* AND \"\"\"y\"*"));
}

}  // namespace clipd2d::tests
