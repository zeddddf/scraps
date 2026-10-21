#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/store/ClipTypes.h"

namespace clipd2d::core::browser {

enum class BrowserScope {
  History,
  Favorites,
  Library,
  Queue,
};

struct BrowserFilters {
  std::optional<store::ClipContentType> type;
  store::DateRangeFilter date_range = store::DateRangeFilter::All;
  std::optional<std::wstring> source_app_name;
  std::vector<std::int64_t> tag_ids;
  bool pinned_only = false;
  bool frequent_only = false;
};

struct BrowserState {
  BrowserScope scope = BrowserScope::History;
  std::optional<std::int64_t> group_id;
  std::wstring group_name;
  std::wstring search_text;
  BrowserFilters filters;
};

}  // namespace clipd2d::core::browser
