#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "core/store/ClipTypes.h"

namespace clipd2d::core::browser {

enum class BrowserCommandKind {
  SetScopeHistory,
  SetScopeFavorites,
  SetScopeLibrary,
  SetScopeQueue,
  SelectGroup,
  SetSearchText,
  ClearSearchText,
  SetTypeFilter,
  ClearTypeFilter,
  SetDateRange,
  ClearDateRange,
  SetSourceAppFilter,
  ClearSourceAppFilter,
  SelectTag,
  RemoveTag,
  ClearTags,
  TogglePinnedOnly,
  ClearPinnedOnly,
  ToggleFrequentOnly,
  ClearFrequentOnly,
  ClearAllStructuredFilters,
  Refresh,
  HidePanel,
};

struct BrowserCommand {
  BrowserCommandKind kind = BrowserCommandKind::Refresh;
  std::optional<std::int64_t> group_id;
  std::wstring group_name;
  std::wstring search_text;
  std::optional<store::ClipContentType> type;
  store::DateRangeFilter date_range = store::DateRangeFilter::All;
  std::optional<std::wstring> source_app_name;
  std::optional<std::int64_t> tag_id;
};

}  // namespace clipd2d::core::browser
