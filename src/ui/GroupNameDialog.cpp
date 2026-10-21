#include "ui/GroupNameDialog.h"

#include "ui/PanelInputDialog.h"

namespace clipd2d::ui {

bool ShowGroupNameDialog(
    HINSTANCE instance,
    HWND owner,
    const std::wstring& title,
    const std::wstring& prompt,
    const std::wstring& initial_name,
    std::wstring* out_name) {
  if (out_name == nullptr) {
    return false;
  }

  PanelInputDialogConfig config;
  config.title = title;
  config.prompt = prompt;
  config.initial_text = initial_name;
  config.empty_text_error = L"\u8d44\u6599\u5939\u540d\u79f0\u4e0d\u80fd\u4e3a\u7a7a\u3002";

  std::wstring unused_color;
  return ShowPanelInputDialog(instance, owner, config, out_name, &unused_color);
}

}  // namespace clipd2d::ui
