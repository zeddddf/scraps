#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

#include "common/Status.h"
#include "core/store/ClipTypes.h"

// 能力边界：**把图片条目合成为剪贴板格式 payload 并写入**。
// - PNG 文件 → HBITMAP（WIC 解码）→ DIB/DIBV5/CF_BITMAP/PNG 四格式；
// - ClipFormatPayload 里的图片二进制 → 剪贴板（含 CF_BITMAP fallback）。
// 不做目标窗口定位、不发送按键。重活（解码）必须在打开剪贴板之前完成。
namespace clipd2d::core::clipboard {

common::Status SetClipboardImageFromPng(const std::wstring& path);
common::Status SetClipboardImageFromFile(const std::wstring& path);

// ClipFormatPayload 中图片二进制 + fallback PNG 路径 → 一次剪贴板会话写入。
common::Status SetClipboardImageClip(
    const std::vector<store::ClipFormatPayload>& formats,
    const std::wstring& fallback_png_path);

}  // namespace clipd2d::core::clipboard
