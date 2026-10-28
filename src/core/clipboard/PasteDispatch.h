#pragma once

#include <string_view>

#include "common/Status.h"
#include "core/clipboard/ClipboardWriter.h"
#include "core/clipboard/PasteTarget.h"

// 能力边界：**把「粘贴」这个动作派发到目标窗口**。
// - Ctrl+V / Shift+Insert / 终端 Ctrl+Shift+V / Unicode 直输的按键序列；
// - Fast / Recovery 两级路由与前台切换后的派发时机。
// 绝不解码图片、绝不明文读写剪贴板格式——内容写回由 Writer/ImageWriteback 完成。
namespace clipd2d::core::clipboard {

common::Status PerformPasteToTarget(
    const PasteTargetContext& requested_context,
    PasteRoute route,
    std::string_view text_fallback);

}  // namespace clipd2d::core::clipboard
