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

std::wstring DeriveLargeImagePreviewPath(const std::wstring& storage_path) {
  if (storage_path.empty()) {
    return std::wstring{};
  }
  const std::size_t slash = storage_path.find_last_of(L"/\\");
  const std::wstring dir = slash == std::wstring::npos ? std::wstring{} : storage_path.substr(0, slash + 1);
  const std::wstring name = slash == std::wstring::npos ? storage_path : storage_path.substr(slash + 1);
  return dir + L"preview_" + name;
}

}  // namespace clipd2d::core::policies
