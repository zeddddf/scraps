#include "core/browser/ClipBrowserService.h"

#include <algorithm>

#include "core/store/ClipTypes.h"

namespace clipd2d::core::browser {

BrowserState ClipBrowserService::ApplyCommand(BrowserState state, const BrowserCommand& command) const {
  switch (command.kind) {
    case BrowserCommandKind::SetScopeHistory:
      state.scope = BrowserScope::History;
      state.group_id.reset();
      state.group_name.clear();
      break;
    case BrowserCommandKind::SetScopeFavorites:
      state.scope = BrowserScope::Favorites;
      state.group_id.reset();
      state.group_name.clear();
      break;
    case BrowserCommandKind::SetScopeLibrary:
      state.scope = BrowserScope::Library;
      state.group_id.reset();
      state.group_name.clear();
      break;
    case BrowserCommandKind::SetScopeQueue:
      state.scope = BrowserScope::Queue;
      state.group_id.reset();
      state.group_name.clear();
      break;
    case BrowserCommandKind::SelectGroup:
      state.scope = BrowserScope::Library;
      state.group_id = command.group_id;
      state.group_name = command.group_name;
      break;
    case BrowserCommandKind::SetSearchText:
      state.search_text = command.search_text;
      break;
    case BrowserCommandKind::ClearSearchText:
      state.search_text.clear();
      break;
    case BrowserCommandKind::SetTypeFilter:
      state.filters.type = command.type;
      break;
    case BrowserCommandKind::ClearTypeFilter:
      state.filters.type.reset();
      break;
    case BrowserCommandKind::SetDateRange:
      state.filters.date_range = command.date_range;
      break;
    case BrowserCommandKind::ClearDateRange:
      state.filters.date_range = store::DateRangeFilter::All;
      break;
    case BrowserCommandKind::SetSourceAppFilter:
      state.filters.source_app_name = command.source_app_name;
      break;
    case BrowserCommandKind::ClearSourceAppFilter:
      state.filters.source_app_name.reset();
      break;
    case BrowserCommandKind::SelectTag:
      if (command.tag_id.has_value()) {
        state.filters.tag_ids = {*command.tag_id};
      }
      break;
    case BrowserCommandKind::RemoveTag:
      if (command.tag_id.has_value()) {
        std::erase(state.filters.tag_ids, *command.tag_id);
      }
      break;
    case BrowserCommandKind::ClearTags:
      state.filters.tag_ids.clear();
      break;
    case BrowserCommandKind::TogglePinnedOnly:
      state.filters.pinned_only = !state.filters.pinned_only;
      break;
    case BrowserCommandKind::ClearPinnedOnly:
      state.filters.pinned_only = false;
      break;
    case BrowserCommandKind::ToggleFrequentOnly:
      state.filters.frequent_only = !state.filters.frequent_only;
      break;
    case BrowserCommandKind::ClearFrequentOnly:
      state.filters.frequent_only = false;
      break;
    case BrowserCommandKind::ClearAllStructuredFilters:
      state.filters = BrowserFilters{};
      break;
    case BrowserCommandKind::Refresh:
    case BrowserCommandKind::HidePanel:
      break;
  }
  return state;
}

store::ClipQuery ClipBrowserService::BuildQuery(const BrowserState& state) const {
  return ClipQueryBuilder::Build(state);
}

BrowserPresentation ClipBrowserService::BuildPresentation(const BrowserState& state) const {
  BrowserPresentation presentation;
  switch (state.scope) {
    case BrowserScope::History:
      presentation.title = L"\u5386\u53f2";
      break;
    case BrowserScope::Favorites:
      presentation.title = L"\u6536\u85cf";
      break;
    case BrowserScope::Library:
      presentation.title = state.group_name.empty() ? L"\u8d44\u6599\u5e93" : state.group_name;
      break;
    case BrowserScope::Queue:
      presentation.title = L"\u961f\u5217";
      break;
  }

  presentation.show_time_groups =
      state.scope != BrowserScope::Queue &&
      state.scope != BrowserScope::Favorites &&
      !(state.scope == BrowserScope::Library && state.group_id.has_value()) &&
      state.filters.tag_ids.empty();

  if (!state.search_text.empty()) {
    presentation.chips.push_back(FilterChip{
        FilterChipKind::Search,
        L"\u641c\u7d22: " + state.search_text,
        0,
        true});
  }
  if (state.filters.type.has_value()) {
    presentation.chips.push_back(FilterChip{
        FilterChipKind::Type,
        std::wstring(store::ToDisplayLabel(*state.filters.type)),
        0,
        true});
  }
  if (state.filters.date_range != store::DateRangeFilter::All) {
    presentation.chips.push_back(FilterChip{
        FilterChipKind::DateRange,
        std::wstring(store::ToDisplayLabel(state.filters.date_range)),
        0,
        true});
  }
  if (state.filters.source_app_name.has_value() && !state.filters.source_app_name->empty()) {
    presentation.chips.push_back(FilterChip{
        FilterChipKind::SourceApp,
        L"\u6765\u6e90: " + *state.filters.source_app_name,
        0,
        true});
  }
  for (const auto& tag_id : state.filters.tag_ids) {
    presentation.chips.push_back(FilterChip{
        FilterChipKind::Tag,
        L"\u6807\u7b7e",
        tag_id,
        true});
  }
  if (state.filters.pinned_only) {
    presentation.chips.push_back(FilterChip{
        FilterChipKind::Pinned,
        L"\u7f6e\u9876",
        0,
        true});
  }
  if (state.filters.frequent_only) {
    presentation.chips.push_back(FilterChip{
        FilterChipKind::Frequent,
        L"\u5e38\u7528",
        0,
        true});
  }
  if (state.scope == BrowserScope::Library && !state.group_name.empty()) {
    presentation.chips.push_back(FilterChip{
        FilterChipKind::GroupContext,
        L"\u8d44\u6599\u5939: " + state.group_name,
        state.group_id.value_or(0),
        true});
  }

  return presentation;
}

BrowserResult ClipBrowserService::BuildResult(const BrowserState& state) const {
  return BrowserResult{
      BuildQuery(state),
      BuildPresentation(state)};
}

std::optional<BrowserCommand> ClipBrowserService::BuildRemoveChipCommand(
    const BrowserState& state,
    const FilterChip& chip) const {
  switch (chip.kind) {
    case FilterChipKind::Search:
      return BrowserCommand{BrowserCommandKind::ClearSearchText};
    case FilterChipKind::Type:
      return BrowserCommand{BrowserCommandKind::ClearTypeFilter};
    case FilterChipKind::DateRange:
      return BrowserCommand{BrowserCommandKind::ClearDateRange};
    case FilterChipKind::SourceApp:
      return BrowserCommand{BrowserCommandKind::ClearSourceAppFilter};
    case FilterChipKind::Tag: {
      if (state.filters.tag_ids.size() <= 1) {
        return BrowserCommand{BrowserCommandKind::ClearTags};
      }
      BrowserCommand command;
      command.kind = BrowserCommandKind::RemoveTag;
      command.tag_id = chip.id;
      return command;
    }
    case FilterChipKind::Pinned:
      return BrowserCommand{BrowserCommandKind::ClearPinnedOnly};
    case FilterChipKind::Frequent:
      return BrowserCommand{BrowserCommandKind::ClearFrequentOnly};
    case FilterChipKind::GroupContext:
      return BrowserCommand{BrowserCommandKind::SetScopeLibrary};
  }
  return std::nullopt;
}

}  // namespace clipd2d::core::browser
