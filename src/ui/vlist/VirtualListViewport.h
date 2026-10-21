#pragma once

// Orchestrates HeightIndex + layout + slot pool over a VirtualListSource.
// UI-free and unit-testable; MainWindow drives it once per frame.

#include <algorithm>
#include <cstdint>

#include "ui/vlist/HeightIndex.h"
#include "ui/vlist/StableSlotPool.h"
#include "ui/vlist/VirtualListSource.h"
#include "ui/vlist/VirtualListTypes.h"

namespace clipd2d::ui::vlist {

class VirtualListViewport {
 public:
  void SetSource(VirtualListSource* source) { source_ = source; }
  void set_default_height(int height) { default_height_ = std::max(1, height); }
  int default_height() const { return default_height_; }

  // Rebuild the height index from the source (unloaded rows get the default
  // estimate). O(N).
  void Rebuild();

  // Re-measure a single row. Returns true when its height changed.
  bool UpdateHeight(int index);

  // Re-measure [first, last] (inclusive) and report how much the content
  // height *above* `anchor_index` changed. The caller shifts its scroll
  // offset by the returned value to keep the anchor row visually stable.
  int UpdateRange(int first, int last, int anchor_index);

  // Re-measure every row that currently has loaded data (used after a page
  // set swap). Returns the anchor delta like UpdateRange.
  int UpdateLoadedHeights(int anchor_index);

  const HeightIndex& heights() const { return heights_; }
  int TotalHeight() const { return heights_.TotalHeight(); }
  int OffsetOf(int index) const { return heights_.OffsetOf(index); }
  int IndexAtOffset(int offset) const { return heights_.IndexAtOffset(offset); }

  VisibleRange RangeAt(float scroll_offset, int viewport_height, int before_px, int after_px) const;

  StableSlotPool& slots() { return slots_; }

 private:
  int ApplyHeightDelta(int index, int anchor_index);

  VirtualListSource* source_ = nullptr;
  HeightIndex heights_;
  StableSlotPool slots_;
  int default_height_ = 96;
};

}  // namespace clipd2d::ui::vlist
