#pragma once

#include <string>
#include <vector>

#include <windows.h>

namespace clipd2d::ui {

struct PanelInputDialogColorOption {
  std::wstring label;
  std::wstring value;
};

struct PanelInputDialogConfig {
  std::wstring title;
  std::wstring prompt;
  std::wstring initial_text;
  std::wstring empty_text_error;
  std::wstring initial_color;
  std::vector<PanelInputDialogColorOption> color_options;
};

bool ShowPanelInputDialog(
    HINSTANCE instance,
    HWND owner,
    const PanelInputDialogConfig& config,
    std::wstring* out_text,
    std::wstring* out_color);

}  // namespace clipd2d::ui
