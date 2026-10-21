#include "core/browser/ClipQueryBuilder.h"

#include "common/Utf8.h"

namespace clipd2d::core::browser {

store::ClipQuery ClipQueryBuilder::Build(const BrowserState& state) {
  store::ClipQuery query;
  switch (state.scope) {
    case BrowserScope::History:
      query.view = store::ClipView::All;
      break;
    case BrowserScope::Favorites:
      query.view = store::ClipView::Favorites;
      query.favorite_only = true;
      break;
    case BrowserScope::Library:
      query.view = state.group_id.has_value() ? store::ClipView::Group : store::ClipView::All;
      query.group_id = state.group_id;
      query.group_name = state.group_name;
      query.storage_tier_filter = store::StorageTier::Library;
      break;
    case BrowserScope::Queue:
      query.view = store::ClipView::Queue;
      query.queue_only = true;
      break;
  }

  query.search_text = common::WideToUtf8(state.search_text).utf8;
  query.type_filter = state.filters.type;
  query.date_range = state.filters.date_range;
  if (state.filters.source_app_name.has_value()) {
    query.source_app_name = *state.filters.source_app_name;
  }
  query.tag_ids = state.filters.tag_ids;
  query.pinned_only = state.filters.pinned_only;
  query.frequent_only = state.filters.frequent_only;
  return query;
}

}  // namespace clipd2d::core::browser
