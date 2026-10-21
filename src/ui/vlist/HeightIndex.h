#pragma once

// Height index over variable-height rows.
// Fenwick tree (binary indexed) adapted from Wield d2d-viewport
// DetailsLayoutStrategy FenwickTree — O(log N) per-item update,
// O(log N) offset<->index, O(1) total. Uniform heights take an O(1)
// fast path (no tree materialized).

#include <vector>

namespace clipd2d::ui::vlist {

class HeightIndex {
 public:
  // Rebuild with `count` rows of `default_height`. O(N), resets to uniform.
  void Reset(int count, int default_height);

  // Update a single row. Returns true when the height actually changed.
  bool SetHeight(int index, int height);

  int HeightAt(int index) const;
  int TotalHeight() const;        // O(1)
  int OffsetOf(int index) const;  // top of row `index`; O(1) uniform / O(log N)
  // Row containing `offset` (largest index with OffsetOf(index) <= offset),
  // clamped to [0, count-1]. O(1) uniform / O(log N).
  int IndexAtOffset(int offset) const;

  int Count() const { return static_cast<int>(heights_.size()); }
  bool IsUniform() const { return uniform_; }

 private:
  void EnsureTree();
  int Prefix(int end) const;        // sum of heights [0, end)
  int UpperBound(int offset) const;  // tree-based IndexAtOffset

  std::vector<int> heights_;
  std::vector<int> tree_;  // 1-based fenwick, built lazily
  int total_ = 0;
  bool uniform_ = true;
  int uniform_height_ = 0;
};

}  // namespace clipd2d::ui::vlist
