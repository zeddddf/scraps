#pragma once

// Virtualized list core — thin types.
// Ported/adapted from Wield d2d-viewport (stable_id identity + variable-height
// layout). See docs/plans/virtual-list-port.md.

#include <cstdint>

namespace clipd2d::ui::vlist {

// Identity of a list item across data refreshes. Never an index.
using ItemId = std::int64_t;

struct VisibleRange {
  int first = 0;
  int last = -1;  // last < first means empty

  bool empty() const { return last < first; }
  int count() const { return empty() ? 0 : last - first + 1; }
};

}  // namespace clipd2d::ui::vlist
