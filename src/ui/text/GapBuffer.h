#pragma once

#include <string>
#include <vector>

namespace wield::text {

class GapBuffer {
 public:
  explicit GapBuffer(size_t initial_capacity = 64);
  ~GapBuffer() = default;

  // Insert string at logical character index
  void Insert(size_t index, const std::wstring& str);

  // Delete length characters starting at logical character index
  void Delete(size_t index, size_t length);

  // Reconstruct and return the full string
  [[nodiscard]] std::wstring GetText() const;

  // Return the logical length of the text (excluding the gap)
  [[nodiscard]] size_t GetLength() const;

  // Get character at logical index
  [[nodiscard]] wchar_t GetCharAt(size_t index) const;

  // Clear all text and reset the gap
  void Clear();

 private:
  void MoveGap(size_t index);
  void Grow(size_t min_capacity);

  std::vector<wchar_t> buffer_;
  size_t gap_start_;
  size_t gap_end_;
};

}  // namespace wield::text
