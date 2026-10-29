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
// 大图缩略预览文件的落盘路径（与原图同目录、加 preview_ 前缀）。大图原图
// 解码可达数百毫秒（实测 2557px PNG ~200ms），悬停预览/OCR 都应优先吃这份
// 缩小版；由 ingest（新图）与 OCR worker（旧图自愈）负责生成。
std::wstring DeriveLargeImagePreviewPath(const std::wstring& storage_path);

}  // namespace clipd2d::core::policies
