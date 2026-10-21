#pragma once

// Stable-id keyed visible-row buffer.
// Simplified from Wield d2d-viewport DynamicSlotPool: keeps the visible row
// set across frames in reusable storage (no per-frame reallocation) and
// reports stable_id entry/exit for dirty-row work.

#include <cstdint>
#include <unordered_set>
#include <vector>

#include "ui/vlist/VirtualListTypes.h"

namespace clipd2d::ui::vlist {

class StableSlotPool {
 public:
  struct Row {
    ItemId stable_id = 0;
    int index = -1;
    float top = 0.0f;
    float bottom = 0.0f;
  };

  // Start a frame: reuses the existing buffer capacity.
  void BeginFrame();
  void Push(const Row& row);
  // Finish the frame: computes added/removed stable ids vs the previous frame.
  void EndFrame();

  const std::vector<Row>& rows() const { return rows_; }
  const std::vector<ItemId>& added_ids() const { return added_ids_; }
  const std::vector<ItemId>& removed_ids() const { return removed_ids_; }

 private:
  std::vector<Row> rows_;
  std::unordered_set<ItemId> live_ids_;
  std::unordered_set<ItemId> prev_ids_;
  std::vector<ItemId> added_ids_;
  std::vector<ItemId> removed_ids_;
};

}  // namespace clipd2d::ui::vlist
