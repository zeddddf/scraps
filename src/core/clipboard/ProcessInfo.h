#pragma once

#include <windows.h>

#include <string>
#include <string_view>

// 能力边界：**进程/窗口元数据**（谁往剪贴板里放了东西）。
// 不解析剪贴板内容，只回答"这个 HWND 属于哪个进程、叫什么"。
namespace clipd2d::core::clipboard {

std::wstring GetProcessPath(HWND window);
std::wstring BaseName(std::wstring_view path);
std::wstring WindowTitle(HWND window);

}  // namespace clipd2d::core::clipboard
