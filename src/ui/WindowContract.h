#pragma once

// 跨进程窗口契约：主面板窗口类名 + 唤出消息 ID。
// CLI（scraps-cli show）与主进程都依赖这一份；此前只能通过 MainWindow 类的
// 静态成员取到，导致 CLI 拖进整个 MainWindow 类定义。

#include <windows.h>

namespace clipd2d::ui::contract {

inline constexpr wchar_t kMainWindowClassName[] = L"Scraps.MainWindow";
inline constexpr UINT kShowWindowMessage = WM_APP + 100;

}  // namespace clipd2d::ui::contract
