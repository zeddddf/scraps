#include "ui/TextLayoutCache.h"

#include <algorithm>
#include <vector>

namespace clipd2d::ui {

std::size_t TextLayoutCache::KeyHash::operator()(const Key& key) const noexcept {
  std::size_t value = std::hash<std::uint64_t>{}(key.clip_id);
  value ^= std::hash<std::uint64_t>{}(key.version) + 0x9e3779b9 + (value << 6) + (value >> 2);
  value ^= std::hash<int>{}(key.width_bucket) + 0x9e3779b9 + (value << 6) + (value >> 2);
  value ^= std::hash<int>{}(key.role) + 0x9e3779b9 + (value << 6) + (value >> 2);
  return value;
}

Microsoft::WRL::ComPtr<IDWriteTextLayout> TextLayoutCache::GetOrCreate(
    IDWriteFactory* factory,
    const Key& key,
    const std::wstring& text,
    IDWriteTextFormat* format,
    float width,
    float height,
    bool single_line) {
  if (factory == nullptr || format == nullptr || width <= 0.0f || height <= 0.0f || text.empty()) {
    return nullptr;
  }

  std::lock_guard<std::mutex> lock(mutex_);

  if (auto it = entries_.find(key); it != entries_.end()) {
    it->second.use_tick = ++use_tick_;
    return it->second.layout;
  }

  Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
  if (FAILED(factory->CreateTextLayout(
          text.c_str(),
          static_cast<UINT32>(text.size()),
          format,
          width,
          height,
          layout.ReleaseAndGetAddressOf()))) {
    return nullptr;
  }

  if (single_line) {
    layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
  }

  entries_[key] = Entry{++use_tick_, layout};
  return layout;
}

void TextLayoutCache::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  entries_.clear();
  use_tick_ = 0;
}

void TextLayoutCache::Trim(std::size_t max_entries) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (entries_.size() <= max_entries) {
    return;
  }

  std::vector<std::pair<Key, std::uint64_t>> usage;
  usage.reserve(entries_.size());
  for (const auto& [key, entry] : entries_) {
    usage.emplace_back(key, entry.use_tick);
  }
  std::sort(
      usage.begin(),
      usage.end(),
      [](const auto& left, const auto& right) {
        return left.second < right.second;
      });

  const std::size_t remove_count = entries_.size() - max_entries;
  for (std::size_t i = 0; i < remove_count; ++i) {
    entries_.erase(usage[i].first);
  }
}

}  // namespace clipd2d::ui
