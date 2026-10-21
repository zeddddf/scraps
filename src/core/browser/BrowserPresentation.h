#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace clipd2d::core::browser {

enum class FilterChipKind {
  Search,
  Type,
  DateRange,
  SourceApp,
  Tag,
  Pinned,
  Frequent,
  GroupContext,
};

struct FilterChip {
  FilterChipKind kind = FilterChipKind::Search;
  std::wstring label;
  std::int64_t id = 0;
  bool removable = true;
};

struct BrowserPresentation {
  std::wstring title;
  std::wstring subtitle;
  std::vector<FilterChip> chips;
  bool show_time_groups = true;
  bool allow_reorder = false;
};

}  // namespace clipd2d::core::browser
