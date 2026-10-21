#include "ui/TagEditorDialog.h"

#include "ui/PanelInputDialog.h"

namespace clipd2d::ui {

bool ShowTagEditorDialog(
    HINSTANCE instance,
    HWND owner,
    const std::wstring& title,
    const std::wstring& initial_name,
    const std::wstring& initial_color,
    std::wstring* out_name,
    std::wstring* out_color) {
  if (out_name == nullptr || out_color == nullptr) {
    return false;
  }
  PanelInputDialogConfig config;
  config.title = title;
  config.prompt = L"标签名称";
  config.initial_text = initial_name;
  config.empty_text_error = L"\u6807\u7b7e\u540d\u79f0\u4e0d\u80fd\u4e3a\u7a7a\u3002";
  config.initial_color = initial_color;
  config.color_options = {
      {L"蓝色", L"#3B82F6"},
      {L"绿色", L"#10B981"},
      {L"橙色", L"#F59E0B"},
      {L"红色", L"#EF4444"},
      {L"紫色", L"#8B5CF6"},
      {L"青色", L"#06B6D4"},
      {L"粉色", L"#EC4899"},
      {L"灰色", L"#6B7280"},
  };
  return ShowPanelInputDialog(instance, owner, config, out_name, out_color);
}

}  // namespace clipd2d::ui
