#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "common/Status.h"

// 能力边界：**图片内容身份（哈希）**。
// 关键约束：同一张图必须只有一种身份。所有来源（PNG / CF_DIB / CF_DIBV5 /
// CF_BITMAP）都必须先解码成同一份规范像素再哈希；不得直接哈希编码字节，
// 否则写回剪贴板后会被当成新条目（实发事故，见原仓库 4d9f22d）。
namespace clipd2d::core::clipboard {

common::Status ComputeCanonicalImageContentHashFromBytes(
    std::string_view format_name,
    const std::vector<std::uint8_t>& image_bytes,
    std::string* content_hash,
    int* width,
    int* height);

// 对「已解码的规范像素」求哈希（宽、高 + 逐像素），identity 的唯一入口。
common::Status ComputeImageContentHashFromCanonicalPixels(
    int width,
    int height,
    std::string_view pixel_bytes,
    std::string* hash_out);

// 内部实现：format_name 决定解码方式，结果统一是规范像素哈希。
common::Status ComputeCanonicalImageContentHashFromBytesImpl(
    std::string_view format_name,
    const std::vector<std::uint8_t>& image_bytes,
    std::string* content_hash,
    int* width,
    int* height);

// 只读宽高，不做像素哈希。
common::Status ReadImageBytesMetadata(
    const std::vector<std::uint8_t>& encoded_bytes,
    int* width,
    int* height);

}  // namespace clipd2d::core::clipboard
