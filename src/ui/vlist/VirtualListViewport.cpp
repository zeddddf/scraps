#include "ui/vlist/VirtualListViewport.h"

#include <algorithm>

#include "ui/vlist/VirtualListLayout.h"

namespace clipd2d::ui::vlist {

void VirtualListViewport::Rebuild() {
  const int count = source_ != nullptr ? source_->ItemCount() : 0;
  heights_.Reset(count, default_height_);
  if (source_ == nullptr) {
    return;
  }
  for (int index = 0; index < count; ++index) {
    const int measured = source_->MeasureHeightAt(index);
    if (measured > 0) {
      heights_.SetHeight(index, measured);
    }
  }
}

bool VirtualListViewport::UpdateHeight(int index) {
  if (source_ == nullptr) {
    return false;
  }
  const int measured = source_->MeasureHeightAt(index);
  if (measured <= 0) {
    return false;
  }
  return heights_.SetHeight(index, measured);
}

int VirtualListViewport::UpdateRange(int first, int last, int anchor_index) {
  if (source_ == nullptr) {
    return 0;
  }
  int delta = 0;
  const int lo = std::max(0, first);
  const int hi = std::min(last, heights_.Count() - 1);
  for (int index = lo; index <= hi; ++index) {
    delta += ApplyHeightDelta(index, anchor_index);
  }
  return delta;
}

int VirtualListViewport::UpdateLoadedHeights(int anchor_index) {
  if (source_ == nullptr) {
    return 0;
  }
  return UpdateRange(0, heights_.Count() - 1, anchor_index);
}

int VirtualListViewport::ApplyHeightDelta(int index, int anchor_index) {
  const int old_height = heights_.HeightAt(index);
  if (!UpdateHeight(index)) {
    return 0;
  }
  if (index >= anchor_index) {
    return 0;
  }
  return heights_.HeightAt(index) - old_height;
}

VisibleRange VirtualListViewport::RangeAt(
    float scroll_offset,
    int viewport_height,
    int before_px,
    int after_px) const {
  return VisibleRangeFor(heights_, scroll_offset, viewport_height, before_px, after_px);
}

}  // namespace clipd2d::ui::vlist
