#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <mutex>

#include <dwrite.h>
#include <wrl/client.h>

namespace clipd2d::ui {

class TextLayoutCache {
 public:
  struct Key {
    std::uint64_t clip_id = 0;
    std::uint64_t version = 0;
    int width_bucket = 0;
    int role = 0;

    bool operator==(const Key& other) const = default;
  };

  Microsoft::WRL::ComPtr<IDWriteTextLayout> GetOrCreate(
      IDWriteFactory* factory,
      const Key& key,
      const std::wstring& text,
      IDWriteTextFormat* format,
      float width,
      float height,
      bool single_line);

  void Clear();
  void Trim(std::size_t max_entries);

 private:
  struct Entry {
    std::uint64_t use_tick = 0;
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
  };

  struct KeyHash {
    std::size_t operator()(const Key& key) const noexcept;
  };

  std::unordered_map<Key, Entry, KeyHash> entries_;
  std::uint64_t use_tick_ = 0;
  mutable std::mutex mutex_;
};

}  // namespace clipd2d::ui
