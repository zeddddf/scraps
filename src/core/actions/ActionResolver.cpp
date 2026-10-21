#include "core/actions/ActionResolver.h"

#include <string>
#include <string_view>

namespace clipd2d::core::actions {

namespace {

void AddAction(
    std::vector<ActionDescriptor>* actions,
    std::string_view id,
    std::string_view label,
    const wchar_t* icon_token,
    const char* placement,
    bool is_primary,
    int priority) {
  actions->push_back(ActionDescriptor{
      .id = std::string(id),
      .label = std::string(label),
      .icon_token = icon_token,
      .placement = placement,
      .is_primary = is_primary,
      .priority = priority,
  });
}

}  // namespace

std::vector<ActionDescriptor> ActionResolver::Resolve(
    const clipboard::ContentProfile& profile) const {
  std::vector<ActionDescriptor> actions;

  switch (profile.primary_kind) {
    case clipboard::ContentKind::Text:
    case clipboard::ContentKind::RichText:
    case clipboard::ContentKind::Html: {
      AddAction(&actions, "paste-original", "原样粘贴", L"\xE8C8", "quick", true, 100);
      AddAction(&actions, "paste-plain-text", "纯文本粘贴", L"\xE8D2", "quick", true, 95);

      if (profile.can_project_to_text) {
        AddAction(&actions, "upper", "转大写", L"\xE8D2", "drawer", true, 90);
        AddAction(&actions, "lower", "转小写", L"\xE8D3", "drawer", true, 85);
        AddAction(&actions, "camel", "转驼峰", L"\xE8A5", "drawer", false, 80);

        if (profile.has_trim_candidate) {
          AddAction(&actions, "trim", "去除首尾空白", L"\xE8C3", "drawer", false, 75);
        }

        AddAction(&actions, "powershell-here-string", "PowerShell Here-String 粘贴", L"\xE8C8", "drawer", false, 74);

        if (profile.open_target.kind != clipboard::OpenTargetKind::None) {
          std::string_view label = "打开";
          switch (profile.open_target.kind) {
            case clipboard::OpenTargetKind::Url:
              label = "打开网页";
              break;
            case clipboard::OpenTargetKind::File:
              label = "打开文件";
              break;
            case clipboard::OpenTargetKind::Directory:
              label = "打开文件夹";
              break;
            case clipboard::OpenTargetKind::None:
            default:
              break;
          }
          AddAction(&actions, "open-target", label, L"\xE8A7", "drawer", false, 72);
          if (profile.open_target.kind == clipboard::OpenTargetKind::Url) {
            AddAction(&actions, "show-qr", "链接转二维码", L"\xE0AB", "drawer", false, 71);
          }
        }

        AddAction(&actions, "url-encode", "URL 编码", L"\xE71B", "drawer", false, 70);

        if (profile.looks_url_encoded) {
          AddAction(&actions, "url-decode", "URL 解码", L"\xE71B", "drawer", false, 65);
        }

        if (profile.looks_like_json) {
          AddAction(&actions, "json-pretty", "JSON 格式化", L"\xE8A0", "drawer", false, 60);
        }
      }

      break;
    }

    case clipboard::ContentKind::Image: {
      AddAction(&actions, "paste-original", "原样粘贴", L"\xE8C8", "quick", true, 100);
      break;
    }

    case clipboard::ContentKind::File: {
      AddAction(&actions, "paste-original", "原样粘贴", L"\xE8C8", "quick", true, 100);
      break;
    }
  }

  return actions;
}

}  // namespace clipd2d::core::actions
