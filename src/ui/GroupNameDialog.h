#pragma once

#include <string>

#include <windows.h>

namespace clipd2d::ui {

bool ShowGroupNameDialog(
    HINSTANCE instance,
    HWND owner,
    const std::wstring& title,
    const std::wstring& prompt,
    const std::wstring& initial_name,
    std::wstring* out_name);

}  // namespace clipd2d::ui
