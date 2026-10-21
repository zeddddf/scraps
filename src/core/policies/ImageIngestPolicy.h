#pragma once

#include <cstdint>
#include <string>

namespace clipd2d::core::policies {

// Responsibility: Centralize image ingest sizing and preview-path policy.
// Inputs: Image dimensions, decoded byte estimates, storage paths, and image size class.
// Outputs: Size estimates, large-image classification, and preview path decisions.
// Forbidden dependencies: Database writes, clipboard IO, or UI rendering logic.

std::int64_t LargeImageThresholdBytes();
std::int64_t EstimatedDecodedBytes(int width, int height);
bool IsLargeImageEstimate(std::int64_t estimated_decoded_bytes);
std::wstring ResolvePreviewPath(const std::wstring& storage_path, bool is_large_image);

}  // namespace clipd2d::core::policies
