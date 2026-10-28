#pragma once

#include <windows.h>

#include <string>

// 能力边界：**剪贴板自检与诊断**。
// 只回答"此刻剪贴板上有什么、归谁所有"，不写入、不解析内容、不改状态。
namespace clipd2d::core::clipboard {

// 标准剪贴板格式 ID → 名字（未命中返回空串）。
std::string ClipboardStandardFormatName(UINT format_id);

// "Snipaste.exe(hwnd=1234)" 形态；无 owner 时返回 "none"。
std::string DescribeClipboardOwnerForDiagnostics();

// 一行式格式清单：数量、关键格式可用性、owner、序列号、枚举明细。
std::string DescribeClipboardFormatsForDiagnostics();

// 采集失败时把上面那行打到 clipboard.formats 类别（仅诊断，不改行为）。
void LogClipboardFormatsAtCaptureFailure();

}  // namespace clipd2d::core::clipboard
