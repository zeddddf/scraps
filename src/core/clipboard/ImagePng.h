#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

#include "common/Status.h"

// 能力边界：**把位图/DIB/PNG 编码落盘成 PNG 文件**。
// 负责 WIC 编码与缩略图缩放；不决定存哪儿（路径由调用方给），不写数据库。
namespace clipd2d::core::clipboard {

common::Status SaveBitmapToPng(
    HBITMAP bitmap,
    const std::wstring& path,
    std::string* hash_out,
    int* width,
    int* height);

common::Status SaveDibToPng(
    const std::vector<std::uint8_t>& dib_bytes,
    const std::wstring& path,
    std::string* hash_out,
    int* width,
    int* height);

common::Status SaveImageFileToPng(
    const std::wstring& source_path,
    const std::wstring& output_path,
    std::string* hash_out,
    int* width,
    int* height);

common::Status SaveScaledImageFileToPng(
    const std::wstring& source_path,
    const std::wstring& output_path,
    int max_width,
    int max_height);

common::Status SaveClipboardPngToPng(
    const std::vector<std::uint8_t>& png_bytes,
    const std::wstring& source_path,
    const std::wstring& output_path,
    std::string* hash_out,
    int* width,
    int* height);

}  // namespace clipd2d::core::clipboard
