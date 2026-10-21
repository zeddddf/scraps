#include "ui/vlist/StableSlotPool.h"

#include <algorithm>

namespace clipd2d::ui::vlist {

void StableSlotPool::BeginFrame() {
  rows_.clear();
  live_ids_.clear();
}

void StableSlotPool::Push(const Row& row) {
  rows_.push_back(row);
  live_ids_.insert(row.stable_id);
}

void StableSlotPool::EndFrame() {
  added_ids_.clear();
  removed_ids_.clear();
  for (const ItemId id : live_ids_) {
    if (prev_ids_.find(id) == prev_ids_.end()) {
      added_ids_.push_back(id);
    }
  }
  for (const ItemId id : prev_ids_) {
    if (live_ids_.find(id) == live_ids_.end()) {
      removed_ids_.push_back(id);
    }
  }
  prev_ids_ = live_ids_;
  // Keep rows in visual order; caller fills them in index order already.
  std::sort(rows_.begin(), rows_.end(), [](const Row& a, const Row& b) {
    return a.top != b.top ? a.top < b.top : a.index < b.index;
  });
}

}  // namespace clipd2d::ui::vlist
