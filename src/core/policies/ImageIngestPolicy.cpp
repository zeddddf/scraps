#include "core/policies/ImageIngestPolicy.h"

namespace clipd2d::core::policies {

namespace {

constexpr std::int64_t kLargeImageThresholdBytes = 10LL * 1024LL * 1024LL;

}  // namespace

std::int64_t LargeImageThresholdBytes() {
  return kLargeImageThresholdBytes;
}

std::int64_t EstimatedDecodedBytes(int width, int height) {
  if (width <= 0 || height <= 0) {
    return 0;
  }
  return static_cast<std::int64_t>(width) * static_cast<std::int64_t>(height) * 4LL;
}

bool IsLargeImageEstimate(std::int64_t estimated_decoded_bytes) {
  return estimated_decoded_bytes > kLargeImageThresholdBytes;
}

std::wstring ResolvePreviewPath(const std::wstring& storage_path, bool is_large_image) {
  return is_large_image ? std::wstring{} : storage_path;
}

}  // namespace clipd2d::core::policies
