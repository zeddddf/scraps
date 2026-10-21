#pragma once

// Layout math over HeightIndex: visible range + row placement.
// Overscan is expressed in pixels (row-count overscan of the old fixed-height
// code is preserved at the call sites that still want it).

#include <algorithm>

#include "ui/vlist/HeightIndex.h"
#include "ui/vlist/VirtualListTypes.h"

namespace clipd2d::ui::vlist {

// Rows intersecting [scroll - before_px, scroll + viewport_h + after_px).
inline VisibleRange VisibleRangeFor(
    const HeightIndex& heights,
    float scroll_offset,
    int viewport_height,
    int overscan_before_px,
    int overscan_after_px) {
  if (heights.Count() <= 0 || viewport_height <= 0) {
    return {};
  }
  const int total = heights.TotalHeight();
  const int scroll = std::clamp(static_cast<int>(scroll_offset), 0, std::max(0, total - 1));
  const int top = std::max(0, scroll - std::max(0, overscan_before_px));
  const int bottom = std::min(total - 1, scroll + viewport_height + std::max(0, overscan_after_px));
  return VisibleRange{heights.IndexAtOffset(top), heights.IndexAtOffset(bottom)};
}

// Top of row `index` in content coordinates (not viewport-adjusted).
inline float RowTop(const HeightIndex& heights, int index) {
  return static_cast<float>(heights.OffsetOf(index));
}

// Row rect in window coordinates for a list whose content top-left sits at
// (list_left, list_top) with `scroll_offset` applied.
struct RowRect {
  float top = 0.0f;
  float bottom = 0.0f;
};

inline RowRect RowRectFor(
    const HeightIndex& heights,
    int index,
    float list_top,
    float scroll_offset) {
  const float top = list_top + RowTop(heights, index) - scroll_offset;
  return RowRect{top, top + static_cast<float>(heights.HeightAt(index))};
}

}  // namespace clipd2d::ui::vlist
