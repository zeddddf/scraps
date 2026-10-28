#pragma once

#include <string_view>

#include "common/Status.h"
#include "core/clipboard/PasteTarget.h"
#include "core/store/ClipTypes.h"

namespace clipd2d::core::clipboard {

// PasteRoute / PasteTargetContext / 目标定位 API 现由 PasteTarget.h 提供（经 include 保持兼容）。
enum class PasteRoute {
  Fast,
  Recovery,
};

common::Status SetClipboardText(
    std::string_view text,
    std::string_view html_text,
    std::string_view rtf_text = {});
common::Status SetClipboardFiles(const std::wstring& joined_paths);
common::Status SetClipboardImageFromFile(const std::wstring& path);
common::Status SetClipboardImageFromPng(const std::wstring& path);

// 测试探测点：ClipboardWriter 每成功打开一次剪贴板会话就回调一次。
// 用于断言一次图片写回只含一个剪贴板会话（Bug A 回归锁）。传 nullptr 关闭。
// 实现与 ClipboardSessionProbe 类型在 ClipboardSession.h（会话原语模块）。
using ClipboardSessionProbe = void (*)();
void SetClipboardSessionProbe(ClipboardSessionProbe probe);
bool IsValidPasteTargetContext(const PasteTargetContext& context);
PasteTargetContext CapturePasteTargetContext(HWND preferred_window);
common::Status PerformPasteToTarget(
    const PasteTargetContext& context,
    PasteRoute route,
    std::string_view text_fallback = {});
common::Status WriteClipToClipboard(
    const store::ClipRecord& clip,
    std::string_view transformed_text,
    bool use_transformed_text);

}  // namespace clipd2d::core::clipboard
