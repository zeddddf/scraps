#pragma once

#include <string>

#include <windows.h>

namespace clipd2d::ui {

struct PanelConfirmDialogConfig {
  std::wstring title;
  std::wstring message;
  std::wstring confirm_text;
  std::wstring cancel_text;
  bool confirm_is_dangerous = false;
};

bool ShowPanelConfirmDialog(
    HINSTANCE instance,
    HWND owner,
    const PanelConfirmDialogConfig& config);

}  // namespace clipd2d::ui
