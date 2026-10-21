#pragma once

#include <string>

#include <windows.h>

namespace clipd2d::ui {

bool ShowTagEditorDialog(
    HINSTANCE instance,
    HWND owner,
    const std::wstring& title,
    const std::wstring& initial_name,
    const std::wstring& initial_color,
    std::wstring* out_name,
    std::wstring* out_color);

}  // namespace clipd2d::ui
