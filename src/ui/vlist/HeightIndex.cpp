#include "ui/vlist/HeightIndex.h"

#include <algorithm>
#include <cstdlib>

namespace clipd2d::ui::vlist {

void HeightIndex::Reset(int count, int default_height) {
  const int safe_count = std::max(0, count);
  const int safe_height = std::max(0, default_height);
  heights_.assign(static_cast<size_t>(safe_count), safe_height);
  tree_.clear();
  tree_.shrink_to_fit();
  uniform_ = true;
  uniform_height_ = safe_height;
  total_ = safe_count * safe_height;
}

bool HeightIndex::SetHeight(int index, int height) {
  if (index < 0 || index >= Count()) {
    return false;
  }
  const int safe_height = std::max(0, height);
  const int old_height = heights_[static_cast<size_t>(index)];
  if (old_height == safe_height) {
    return false;
  }
  if (uniform_) {
    EnsureTree();
    uniform_ = false;
  }
  const int delta = safe_height - old_height;
  heights_[static_cast<size_t>(index)] = safe_height;
  total_ += delta;
  // 1-based fenwick point update.
  for (int i = index + 1; i <= Count(); i += i & (-i)) {
    tree_[static_cast<size_t>(i)] += delta;
  }
  return true;
}

int HeightIndex::HeightAt(int index) const {
  if (index < 0 || index >= Count()) {
    return 0;
  }
  return heights_[static_cast<size_t>(index)];
}

int HeightIndex::TotalHeight() const {
  return total_;
}

int HeightIndex::OffsetOf(int index) const {
  const int clamped = std::clamp(index, 0, Count());
  if (uniform_) {
    return clamped * uniform_height_;
  }
  return Prefix(clamped);
}

int HeightIndex::IndexAtOffset(int offset) const {
  if (Count() <= 0) {
    return 0;
  }
  if (offset <= 0) {
    return 0;
  }
  if (offset >= total_) {
    return Count() - 1;
  }
  if (uniform_) {
    return std::min(offset / std::max(1, uniform_height_), Count() - 1);
  }
  return UpperBound(offset);
}

void HeightIndex::EnsureTree() {
  if (!tree_.empty()) {
    return;
  }
  const int n = Count();
  tree_.assign(static_cast<size_t>(n) + 1, 0);
  for (int i = 1; i <= n; ++i) {
    tree_[static_cast<size_t>(i)] = heights_[static_cast<size_t>(i - 1)];
  }
  for (int i = 1; i <= n; ++i) {
    const int j = i + (i & (-i));
    if (j <= n) {
      tree_[static_cast<size_t>(j)] += tree_[static_cast<size_t>(i)];
    }
  }
}

int HeightIndex::Prefix(int end) const {
  // Sum of heights [0, end); fenwick query over first `end` elements.
  int sum = 0;
  for (int i = end; i > 0; i -= i & (-i)) {
    sum += tree_[static_cast<size_t>(i)];
  }
  return sum;
}

int HeightIndex::UpperBound(int offset) const {
  // Binary lifting: largest index whose top offset (Prefix(index)) <= offset.
  // Equivalent to Wield FenwickTree::UpperBound.
  int pos = 0;
  int sum = 0;
  int step = 1;
  const int n = Count();
  while (step * 2 <= n) {
    step *= 2;
  }
  for (; step > 0; step >>= 1) {
    const int next = pos + step;
    if (next <= n && sum + tree_[static_cast<size_t>(next)] <= offset) {
      pos = next;
      sum += tree_[static_cast<size_t>(next)];
    }
  }
  // pos == number of rows entirely above `offset` == index of the row at offset.
  return std::min(pos, n - 1);
}

}  // namespace clipd2d::ui::vlist
