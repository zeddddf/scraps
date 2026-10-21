#pragma once

// Data-source contract for the virtualized list (adapted from Wield
// IVirtualDataSource/IViewportItem, narrowed to this project's needs).

#include <cstdint>

namespace clipd2d::ui::vlist {

class VirtualListSource {
 public:
  virtual ~VirtualListSource() = default;

  virtual int ItemCount() const = 0;

  // Stable identity at index (clip id). Must survive data refreshes.
  virtual std::int64_t StableIdAt(int index) const = 0;

  // O(1) height for index. May return a default estimate for not-yet-loaded
  // rows; callers re-measure when real data arrives. MUST NOT run DWrite
  // layout (see RowHeightModel).
  virtual int MeasureHeightAt(int index) const = 0;
};

}  // namespace clipd2d::ui::vlist
