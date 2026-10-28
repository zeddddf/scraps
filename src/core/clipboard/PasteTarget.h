#pragma once

#include <windows.h>

#include "common/Status.h"

// 能力边界：**粘贴目标的定位与恢复**。
// - 确定剪贴板内容要粘贴到哪个窗口（root + focus）；
// - 把前台/焦点恢复到目标窗口（AttachThreadInput 方案）；
// - 焦点未就绪时的有限轮询细化。
// 绝不发送按键（那是 PasteDispatch 的职责），绝不触碰剪贴板内容。
namespace clipd2d::core::clipboard {

struct PasteTargetContext {
  HWND root_window = nullptr;
  HWND focus_window = nullptr;
  DWORD thread_id = 0;
};

bool IsValidPasteTargetContext(const PasteTargetContext& context);
PasteTargetContext CapturePasteTargetContext(HWND preferred_window = nullptr);

// 焦点还停在 root（未落到子控件）时做有限轮询等待；stage 仅用于日志。
PasteTargetContext RefinePasteTargetContext(const PasteTargetContext& context, const char* stage);

// 把前台窗口/焦点恢复到 context 指定的目标（失败前最多轮询 12×10ms）。
bool TryRestorePasteTarget(const PasteTargetContext& context);

// 供 PasteDispatch 复用的窗口原语。
HWND NormalizeRootWindow(HWND window);
bool BelongsToRootWindow(HWND window, HWND root_window);
bool IsStandardPasteControl(HWND window);
std::wstring GetWindowProcessNameLower(HWND window);
bool IsTerminalProcessName(const std::wstring& process_name);

}  // namespace clipd2d::core::clipboard
