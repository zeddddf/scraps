#include "text/GapBuffer.h"
#include <algorithm>
#include <stdexcept>

namespace wield::text {

GapBuffer::GapBuffer(size_t initial_capacity) {
  if (initial_capacity == 0) {
    initial_capacity = 64;
  }
  buffer_.resize(initial_capacity);
  gap_start_ = 0;
  gap_end_ = initial_capacity;
}

void GapBuffer::Insert(size_t index, const std::wstring& str) {
  if (str.empty()) return;
  size_t len = GetLength();
  if (index > len) {
    index = len;
  }

  MoveGap(index);

  size_t required_gap_size = str.length();
  size_t current_gap_size = gap_end_ - gap_start_;

  if (current_gap_size < required_gap_size) {
    size_t min_needed_capacity = buffer_.size() + (required_gap_size - current_gap_size);
    Grow(min_needed_capacity);
  }

  std::copy(str.begin(), str.end(), buffer_.begin() + gap_start_);
  gap_start_ += required_gap_size;
}

void GapBuffer::Delete(size_t index, size_t length) {
  if (length == 0) return;
  size_t len = GetLength();
  if (index >= len) return;
  if (index + length > len) {
    length = len - index;
  }

  MoveGap(index);

  // Deleting characters simply expands the gap rightward (adds to gap_end_)
  gap_end_ += length;
}

std::wstring GapBuffer::GetText() const {
  std::wstring result;
  result.reserve(GetLength());
  if (gap_start_ > 0) {
    result.append(&buffer_[0], gap_start_);
  }
  size_t tail_len = buffer_.size() - gap_end_;
  if (tail_len > 0) {
    result.append(&buffer_[gap_end_], tail_len);
  }
  return result;
}

size_t GapBuffer::GetLength() const {
  return buffer_.size() - (gap_end_ - gap_start_);
}

wchar_t GapBuffer::GetCharAt(size_t index) const {
  size_t len = GetLength();
  if (index >= len) {
    return L'\0';
  }
  if (index < gap_start_) {
    return buffer_[index];
  }
  return buffer_[index + (gap_end_ - gap_start_)];
}

void GapBuffer::Clear() {
  gap_start_ = 0;
  gap_end_ = buffer_.size();
}

void GapBuffer::MoveGap(size_t index) {
  if (index == gap_start_) return;

  if (index < gap_start_) {
    size_t num_to_move = gap_start_ - index;
    std::copy_backward(
        buffer_.begin() + index,
        buffer_.begin() + gap_start_,
        buffer_.begin() + gap_end_
    );
    gap_start_ -= num_to_move;
    gap_end_ -= num_to_move;
  } else {
    size_t num_to_move = index - gap_start_;
    std::copy(
        buffer_.begin() + gap_end_,
        buffer_.begin() + gap_end_ + num_to_move,
        buffer_.begin() + gap_start_
    );
    gap_start_ += num_to_move;
    gap_end_ += num_to_move;
  }
}

void GapBuffer::Grow(size_t min_capacity) {
  size_t current_size = buffer_.size();
  size_t new_capacity = std::max(current_size * 2, min_capacity);

  std::vector<wchar_t> new_buffer(new_capacity);

  // Copy prefix before gap
  if (gap_start_ > 0) {
    std::copy(buffer_.begin(), buffer_.begin() + gap_start_, new_buffer.begin());
  }

  // Copy suffix after gap to the end of the new buffer
  size_t tail_len = current_size - gap_end_;
  size_t new_gap_end = new_capacity - tail_len;
  if (tail_len > 0) {
    std::copy(
        buffer_.begin() + gap_end_,
        buffer_.end(),
        new_buffer.begin() + new_gap_end
    );
  }

  buffer_ = std::move(new_buffer);
  gap_end_ = new_gap_end;
}

}  // namespace wield::text
