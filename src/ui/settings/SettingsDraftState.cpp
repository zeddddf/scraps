#include "ui/settings/SettingsDraftState.h"

#include <algorithm>
#include <cwchar>

#include "common/StringUtil.h"
#include "platform/win32/HotkeySpec.h"

namespace clipd2d::ui {

void SettingsDraftState::LoadFrom(const core::store::AppSettings& settings) {
  draft_.history_limit_text = std::to_wstring(settings.history_limit);
  draft_.history_retention_days_text = std::to_wstring(settings.history_retention_days);
  draft_.start_with_windows = settings.start_with_windows;
  draft_.open_panel_on_first_run = settings.open_panel_on_first_run;
  draft_.show_source_icon = settings.show_source_icon;
  draft_.monitoring_enabled = settings.monitoring_enabled;
  draft_.queue_auto_pop = settings.queue_auto_pop;
  draft_.enable_win_v_override = settings.enable_win_v_override;
  draft_.window_blur_type = settings.window_blur_type;
  draft_.window_blur_opacity = settings.window_blur_opacity;
  draft_.theme = settings.theme;
  draft_.list_density = settings.list_density;
  draft_.window_position_mode = settings.window_position_mode;
  draft_.hotkey = settings.hotkey;
  draft_.quick_paste_hotkey = settings.quick_paste_hotkey;
  if (draft_.quick_paste_hotkey.empty()) {
    draft_.quick_paste_hotkey = L"Alt+~";
  }
  draft_.focus_search_on_open = settings.focus_search_on_open;
  draft_.search_focus_hotkey = settings.search_focus_hotkey;
  draft_.hover_preview_enabled = settings.hover_preview_enabled;
  if (draft_.search_focus_hotkey.empty()) {
    draft_.search_focus_hotkey = L"Tab";
  }
  draft_.third_party_sticker_enabled = settings.third_party_sticker_enabled;
  draft_.third_party_sticker_hotkey = settings.third_party_sticker_hotkey;
  draft_.click_paste_mode = settings.click_paste_mode;
  draft_.promote_pasted_clip_on_reuse = settings.promote_pasted_clip_on_reuse;
  draft_.external_editor_path = settings.external_editor_path;
  draft_.image_ocr_enabled = settings.image_ocr_enabled;
  draft_.ocr_executable_path = settings.ocr_executable_path;
  draft_.ocr_language_hint = settings.ocr_language_hint;
  draft_.blacklist = settings.blacklist;
  draft_.auto_ignore_password_managers = settings.auto_ignore_password_managers;
  draft_.custom_blacklist_apps = settings.custom_blacklist_apps;
  draft_.ignore_large_text = settings.ignore_large_text;
  draft_.max_text_size_kb_text = std::to_wstring(settings.max_text_size_kb);
  draft_.ignore_large_images = settings.ignore_large_images;
  draft_.max_image_size_kb_text = std::to_wstring(settings.max_image_size_kb);
  draft_.ignore_otp_codes = settings.ignore_otp_codes;
  draft_.ignore_complex_passwords = settings.ignore_complex_passwords;
  draft_.screenshot_enabled = settings.screenshot_enabled;
  draft_.screenshot_prewarm = settings.screenshot_prewarm;
  draft_.screenshot_hotkey = settings.screenshot_hotkey;
}

core::store::AppSettings SettingsDraftState::BuildSettings(const core::store::AppSettings& base) const {
  core::store::AppSettings settings = base;
  try {
    settings.history_limit = std::max(1, std::stoi(draft_.history_limit_text));
  } catch (...) {
    settings.history_limit = base.history_limit;
  }
  try {
    settings.history_retention_days = std::max(0, std::stoi(draft_.history_retention_days_text));
  } catch (...) {
    settings.history_retention_days = base.history_retention_days;
  }
  settings.start_with_windows = draft_.start_with_windows;
  settings.open_panel_on_first_run = draft_.open_panel_on_first_run;
  settings.show_source_icon = draft_.show_source_icon;
  settings.monitoring_enabled = draft_.monitoring_enabled;
  settings.queue_auto_pop = draft_.queue_auto_pop;
  settings.enable_win_v_override = draft_.enable_win_v_override;
  settings.window_blur_type = draft_.window_blur_type;
  settings.window_blur_opacity = draft_.window_blur_opacity;
  settings.theme = draft_.theme;
  settings.list_density = draft_.list_density;
  settings.window_position_mode = draft_.window_position_mode;
  settings.hotkey = common::TrimWide(draft_.hotkey);
  settings.quick_paste_hotkey = common::TrimWide(draft_.quick_paste_hotkey);
  settings.focus_search_on_open = draft_.focus_search_on_open;
  settings.search_focus_hotkey = common::TrimWide(draft_.search_focus_hotkey);
  settings.hover_preview_enabled = draft_.hover_preview_enabled;
  settings.third_party_sticker_enabled = draft_.third_party_sticker_enabled;
  settings.third_party_sticker_hotkey = common::TrimWide(draft_.third_party_sticker_hotkey);
  settings.click_paste_mode = draft_.click_paste_mode;
  settings.promote_pasted_clip_on_reuse = draft_.promote_pasted_clip_on_reuse;
  settings.external_editor_path = common::TrimWide(draft_.external_editor_path);
  settings.image_ocr_enabled = draft_.image_ocr_enabled;
  settings.ocr_executable_path = common::TrimWide(draft_.ocr_executable_path);
  settings.ocr_language_hint = common::TrimWide(draft_.ocr_language_hint);
  settings.blacklist = draft_.blacklist;
  settings.auto_ignore_password_managers = draft_.auto_ignore_password_managers;
  settings.custom_blacklist_apps = common::TrimWide(draft_.custom_blacklist_apps);
  settings.ignore_large_text = draft_.ignore_large_text;
  try {
    settings.max_text_size_kb = std::max(1, std::stoi(draft_.max_text_size_kb_text));
  } catch (...) {
    settings.max_text_size_kb = base.max_text_size_kb;
  }
  settings.ignore_large_images = draft_.ignore_large_images;
  try {
    settings.max_image_size_kb = std::max(1, std::stoi(draft_.max_image_size_kb_text));
  } catch (...) {
    settings.max_image_size_kb = base.max_image_size_kb;
  }
  settings.ignore_otp_codes = draft_.ignore_otp_codes;
  settings.ignore_complex_passwords = draft_.ignore_complex_passwords;
  settings.screenshot_enabled = draft_.screenshot_enabled;
  settings.screenshot_prewarm = draft_.screenshot_prewarm;
  settings.screenshot_hotkey = common::TrimWide(draft_.screenshot_hotkey);
  return settings;
}

bool SettingsDraftState::Validate(std::wstring* error_message) const {
  const std::wstring history_text = common::TrimWide(draft_.history_limit_text);
  if (history_text.empty()) {
    if (error_message != nullptr) {
      *error_message = L"请输入合法的历史记录上限。";
    }
    return false;
  }

  wchar_t* end = nullptr;
  const long value = std::wcstol(history_text.c_str(), &end, 10);
  if (end == history_text.c_str() || *end != L'\0' || value <= 0 || value > 500000) {
    if (error_message != nullptr) {
      *error_message = L"历史记录上限必须是 1 到 500000 之间的整数。";
    }
    return false;
  }

  const std::wstring retention_text = common::TrimWide(draft_.history_retention_days_text);
  if (retention_text.empty()) {
    if (error_message != nullptr) {
      *error_message = L"请输入合法的历史保留天数。";
    }
    return false;
  }
  end = nullptr;
  const long retention_value = std::wcstol(retention_text.c_str(), &end, 10);
  if (end == retention_text.c_str() || *end != L'\0' || retention_value < 0 || retention_value > 3650) {
    if (error_message != nullptr) {
      *error_message = L"历史保留天数必须是 0 到 3650 之间的整数。"; // 历史保留天数必须是 0 到 3650 之间的整数。
    }
    return false;
  }

  const std::wstring text_limit_text = common::TrimWide(draft_.max_text_size_kb_text);
  if (draft_.ignore_large_text && text_limit_text.empty()) {
    if (error_message != nullptr) {
      *error_message = L"请输入最大文本大小限制。"; // 请输入最大文本大小限制。
    }
    return false;
  }
  if (draft_.ignore_large_text) {
    end = nullptr;
    const long limit_value = std::wcstol(text_limit_text.c_str(), &end, 10);
    if (end == text_limit_text.c_str() || *end != L'\0' || limit_value < 1 || limit_value > 102400) {
      if (error_message != nullptr) {
        *error_message = L"文本大小限制必须是 1 到 102400 KB 之间的整数。"; // 文本大小限制必须是 1 到 102400 KB 之间的整数。
      }
      return false;
    }
  }

  const std::wstring image_limit_text = common::TrimWide(draft_.max_image_size_kb_text);
  if (draft_.ignore_large_images && image_limit_text.empty()) {
    if (error_message != nullptr) {
      *error_message = L"请输入最大图片大小限制。"; // 请输入最大图片大小限制。
    }
    return false;
  }
  if (draft_.ignore_large_images) {
    end = nullptr;
    const long limit_value = std::wcstol(image_limit_text.c_str(), &end, 10);
    if (end == image_limit_text.c_str() || *end != L'\0' || limit_value < 1 || limit_value > 102400) {
      if (error_message != nullptr) {
        *error_message = L"图片大小限制必须是 1 到 102400 KB 之间的整数。"; // 图片大小限制必须是 1 到 102400 KB 之间的整数。
      }
      return false;
    }
  }

  platform::win32::HotkeySpec global_hotkey;
  if (!platform::win32::ParseHotkeyText(common::TrimWide(draft_.hotkey), &global_hotkey).ok()) {
    if (error_message != nullptr) {
      *error_message = L"面板热键格式无效，示例：Ctrl+Shift+V";
    }
    return false;
  }

  if (!platform::win32::ParseHotkeyText(common::TrimWide(draft_.quick_paste_hotkey), &global_hotkey).ok()) {
    if (error_message != nullptr) {
      *error_message = L"快捷粘贴热键无效，示例：Alt+~";
    }
    return false;
  }

  if (!platform::win32::ParseHotkeyText(common::TrimWide(draft_.screenshot_hotkey), &global_hotkey).ok()) {
    if (error_message != nullptr) {
      *error_message = L"截图快捷键格式无效，示例：Win+Shift+S";
    }
    return false;
  }

  platform::win32::HotkeySpec local_hotkey;
  if (!platform::win32::ParseLocalHotkeyText(common::TrimWide(draft_.search_focus_hotkey), &local_hotkey).ok()) {
    if (error_message != nullptr) {
      *error_message = L"聚焦搜索框快捷键无效，示例：Tab 或 Ctrl+F";
    }
    return false;
  }

  const std::wstring sticker_hotkey_text = common::TrimWide(draft_.third_party_sticker_hotkey);
  if (draft_.third_party_sticker_enabled && sticker_hotkey_text.empty()) {
    if (error_message != nullptr) {
      *error_message = L"请配置第三方贴图快捷键。";
    }
    return false;
  }
  if (!sticker_hotkey_text.empty() &&
      !platform::win32::ParseLocalHotkeyText(sticker_hotkey_text, &local_hotkey).ok()) {
    if (error_message != nullptr) {
      *error_message = L"第三方贴图快捷键无效，示例：F2 或 Ctrl+Shift+P";
    }
    return false;
  }

  return true;
}

bool SettingsDraftState::IsSectionDirty(Section section, const core::store::AppSettings& next,
                                        const core::store::AppSettings& cur) const {
  switch (section) {
    case Section::General:
      return next.history_limit != cur.history_limit ||
          next.history_retention_days != cur.history_retention_days ||
          next.start_with_windows != cur.start_with_windows ||
          next.open_panel_on_first_run != cur.open_panel_on_first_run ||
          next.screenshot_enabled != cur.screenshot_enabled ||
          next.screenshot_prewarm != cur.screenshot_prewarm;
    case Section::Appearance:
      return next.show_source_icon != cur.show_source_icon ||
          next.list_density != cur.list_density ||
          next.window_blur_type != cur.window_blur_type ||
          next.window_blur_opacity != cur.window_blur_opacity ||
          next.theme != cur.theme;
    case Section::Hotkey:
      return next.hotkey != cur.hotkey ||
          next.quick_paste_hotkey != cur.quick_paste_hotkey ||
          next.search_focus_hotkey != cur.search_focus_hotkey ||
          next.third_party_sticker_enabled != cur.third_party_sticker_enabled ||
          next.third_party_sticker_hotkey != cur.third_party_sticker_hotkey ||
          next.screenshot_hotkey != cur.screenshot_hotkey;
    case Section::Clipboard:
      return next.monitoring_enabled != cur.monitoring_enabled ||
          next.window_position_mode != cur.window_position_mode ||
          next.focus_search_on_open != cur.focus_search_on_open ||
          next.hover_preview_enabled != cur.hover_preview_enabled ||
          next.click_paste_mode != cur.click_paste_mode ||
          next.promote_pasted_clip_on_reuse != cur.promote_pasted_clip_on_reuse ||
          next.queue_auto_pop != cur.queue_auto_pop;
    case Section::Filter:
      return next.auto_ignore_password_managers != cur.auto_ignore_password_managers ||
          next.ignore_otp_codes != cur.ignore_otp_codes ||
          next.ignore_complex_passwords != cur.ignore_complex_passwords ||
          next.ignore_large_text != cur.ignore_large_text ||
          next.max_text_size_kb != cur.max_text_size_kb ||
          next.ignore_large_images != cur.ignore_large_images ||
          next.max_image_size_kb != cur.max_image_size_kb ||
          next.blacklist != cur.blacklist ||
          next.custom_blacklist_apps != cur.custom_blacklist_apps;
    case Section::Data:
      return next.external_editor_path != cur.external_editor_path ||
          next.image_ocr_enabled != cur.image_ocr_enabled ||
          next.ocr_executable_path != cur.ocr_executable_path ||
          next.ocr_language_hint != cur.ocr_language_hint;
    case Section::About:
      return next.enable_win_v_override != cur.enable_win_v_override;
  }
  return false;
}

unsigned SettingsDraftState::ComputeDirtyMask(const core::store::AppSettings& cur) const {
  const core::store::AppSettings next = BuildSettings(cur);
  unsigned mask = 0;
  for (int index = 0; index < 7; ++index) {
    if (IsSectionDirty(static_cast<Section>(index), next, cur)) {
      mask |= (1u << index);
    }
  }
  return mask;
}

bool SettingsDraftState::IsDirty(const core::store::AppSettings& cur) const {
  return ComputeDirtyMask(cur) != 0;
}

void SettingsDraftState::ResetSectionToDefaults(Section section) {
  const core::store::AppSettings d{};
  switch (section) {
    case Section::General:
      draft_.history_limit_text = std::to_wstring(d.history_limit);
      draft_.history_retention_days_text = std::to_wstring(d.history_retention_days);
      draft_.start_with_windows = d.start_with_windows;
      draft_.open_panel_on_first_run = d.open_panel_on_first_run;
      draft_.screenshot_enabled = d.screenshot_enabled;
      draft_.screenshot_prewarm = d.screenshot_prewarm;
      break;
    case Section::Appearance:
      draft_.show_source_icon = d.show_source_icon;
      draft_.list_density = d.list_density;
      draft_.window_blur_type = d.window_blur_type;
      draft_.window_blur_opacity = d.window_blur_opacity;
      draft_.theme = d.theme;
      break;
    case Section::Hotkey:
      draft_.hotkey = d.hotkey;
      draft_.quick_paste_hotkey = d.quick_paste_hotkey;
      draft_.search_focus_hotkey = d.search_focus_hotkey;
      draft_.third_party_sticker_enabled = d.third_party_sticker_enabled;
      draft_.third_party_sticker_hotkey = d.third_party_sticker_hotkey;
      draft_.screenshot_hotkey = d.screenshot_hotkey;
      break;
    case Section::Clipboard:
      draft_.monitoring_enabled = d.monitoring_enabled;
      draft_.window_position_mode = d.window_position_mode;
      draft_.focus_search_on_open = d.focus_search_on_open;
      draft_.hover_preview_enabled = d.hover_preview_enabled;
      draft_.click_paste_mode = d.click_paste_mode;
      draft_.promote_pasted_clip_on_reuse = d.promote_pasted_clip_on_reuse;
      draft_.queue_auto_pop = d.queue_auto_pop;
      break;
    case Section::Filter:
      draft_.auto_ignore_password_managers = d.auto_ignore_password_managers;
      draft_.ignore_otp_codes = d.ignore_otp_codes;
      draft_.ignore_complex_passwords = d.ignore_complex_passwords;
      draft_.ignore_large_text = d.ignore_large_text;
      draft_.max_text_size_kb_text = std::to_wstring(d.max_text_size_kb);
      draft_.ignore_large_images = d.ignore_large_images;
      draft_.max_image_size_kb_text = std::to_wstring(d.max_image_size_kb);
      draft_.blacklist = d.blacklist;
      draft_.custom_blacklist_apps = d.custom_blacklist_apps;
      break;
    case Section::Data:
      draft_.external_editor_path = d.external_editor_path;
      draft_.image_ocr_enabled = d.image_ocr_enabled;
      draft_.ocr_executable_path = d.ocr_executable_path;
      draft_.ocr_language_hint = d.ocr_language_hint;
      break;
    case Section::About:
      draft_.enable_win_v_override = d.enable_win_v_override;
      break;
  }
}

}  // namespace clipd2d::ui
