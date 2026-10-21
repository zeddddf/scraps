#include "ui/SettingsWindow.h"
#include "ui/layout/FlexNode.h"
#include "ui/settings/SettingsDrawUtil.h"
#include "ui/settings/SettingsPanelGeneral.h"
#include "ui/settings/SettingsPanelAppearance.h"
#include "ui/settings/SettingsPanelHotkey.h"
#include "ui/settings/SettingsPanelClipboard.h"
#include "ui/settings/SettingsPanelFilter.h"
#include "ui/settings/SettingsPanelData.h"
#include "ui/settings/SettingsPanelAbout.h"

#include <CommCtrl.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cwchar>
#include <sstream>
#include <string_view>

#include "common/StringUtil.h"
#include "platform/win32/HotkeySpec.h"
#include "ui/D2dUtil.h"
#include "ui/FluentIcons.h"

namespace clipd2d::ui {

namespace {

constexpr wchar_t kWindowClassName[] = L"Scraps.SettingsWindow";
constexpr UINT kProxyTabMessage = WM_APP + 620;
constexpr UINT kProxyEscapeMessage = WM_APP + 621;
constexpr UINT kProxyCommitMessage = WM_APP + 622;

float DpiScaleForWindow(HWND hwnd) {
  const UINT dpi = hwnd != nullptr ? GetDpiForWindow(hwnd) : 96;
  return static_cast<float>(dpi) / 96.0f;
}

const wchar_t* SectionTitle(Section section) {
  switch (section) {
    case Section::General:
      return L"\u5e38\u89c4";
    case Section::Appearance:
      return L"\u5916\u89c2";
    case Section::Hotkey:
      return L"\u5feb\u6377\u952e";
    case Section::Clipboard:
      return L"\u526a\u8d34\u677f";
    case Section::Filter:
      return L"\u8fc7\u6ee4";
    case Section::Data:
      return L"\u6570\u636e\u7ba1\u7406";
    case Section::About:
      return L"\u5173\u4e8e";
  }
  return L"";
}

FluentIcon SectionIcon(Section section) {
  switch (section) {
    case Section::General:
      return FluentIcon::List;
    case Section::Appearance:
      return FluentIcon::Palette;
    case Section::Hotkey:
      return FluentIcon::Keyboard;
    case Section::Clipboard:
      return FluentIcon::Clipboard;
    case Section::Filter:
      return FluentIcon::Filter;
    case Section::Data:
      return FluentIcon::Folder;
    case Section::About:
      return FluentIcon::Info;
  }
  return FluentIcon::Info;
}

bool ContainsInsensitive(std::wstring_view text, std::wstring_view query) {
  if (query.empty()) {
    return true;
  }
  const std::wstring lowered_text = common::ToLowerWide(text);
  const std::wstring lowered_query = common::ToLowerWide(query);
  return lowered_text.find(lowered_query) != std::wstring::npos;
}

bool IsModifierVirtualKey(UINT virtual_key) {
  switch (virtual_key) {
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL:
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU:
    case VK_LWIN:
    case VK_RWIN:
      return true;
    default:
      return false;
  }
}

LRESULT CALLBACK EditProxySubclassProc(
    HWND hwnd,
    UINT message,
    WPARAM w_param,
    LPARAM l_param,
    UINT_PTR subclass_id,
    DWORD_PTR ref_data) {
  (void)ref_data;
  const HWND parent = GetParent(hwnd);
  switch (message) {
    case WM_KEYDOWN:
      if (w_param == VK_TAB) {
        SendMessageW(parent, kProxyTabMessage, (GetKeyState(VK_SHIFT) & 0x8000) != 0, subclass_id);
        return 0;
      }
      if (w_param == VK_ESCAPE) {
        SendMessageW(parent, kProxyEscapeMessage, 0, subclass_id);
        return 0;
      }
      if (w_param == VK_RETURN && (GetWindowLongPtrW(hwnd, GWL_STYLE) & ES_MULTILINE) == 0) {
        SendMessageW(parent, kProxyCommitMessage, 0, subclass_id);
        return 0;
      }
      break;
  }
  return DefSubclassProc(hwnd, message, w_param, l_param);
}

std::wstring ReadWindowText(HWND hwnd) {
  const int length = GetWindowTextLengthW(hwnd);
  std::wstring text(static_cast<size_t>(length), L'\0');
  GetWindowTextW(hwnd, text.data(), length + 1);
  return text;
}

}  // namespace

SettingsWindow::SettingsWindow() {
  panels_[static_cast<int>(Section::General)] = std::make_unique<SettingsPanelGeneral>();
  panels_[static_cast<int>(Section::Appearance)] = std::make_unique<SettingsPanelAppearance>();
  panels_[static_cast<int>(Section::Hotkey)] = std::make_unique<SettingsPanelHotkey>();
  panels_[static_cast<int>(Section::Clipboard)] = std::make_unique<SettingsPanelClipboard>();
  panels_[static_cast<int>(Section::Filter)] = std::make_unique<SettingsPanelFilter>();
  panels_[static_cast<int>(Section::Data)] = std::make_unique<SettingsPanelData>();
  panels_[static_cast<int>(Section::About)] = std::make_unique<SettingsPanelAbout>();
}


void SettingsWindow::AddHitRegion(const HitRegion& region) {
  hit_regions_.push_back(region);
}

void SettingsWindow::RegisterFieldRect(FieldId field, const RECT& rect) {
  field_rects_[field] = rect;
}

void SettingsWindow::RegisterSelectRect(SelectField field, const RECT& rect) {
  select_rects_[field] = rect;
}

int SettingsWindow::DipToPx(float dip) const {
  return static_cast<int>(dip * DpiScaleForWindow(hwnd_) + 0.5f);
}

SettingsWindow::~SettingsWindow() {
  Destroy();
}

LRESULT CALLBACK SettingsWindow::StaticWindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
  if (message == WM_NCCREATE) {
    auto* create_struct = reinterpret_cast<CREATESTRUCTW*>(l_param);
    auto* self = static_cast<SettingsWindow*>(create_struct->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    self->hwnd_ = hwnd;
  }

  auto* self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  if (self != nullptr) {
    return self->WindowProc(message, w_param, l_param);
  }
  return DefWindowProcW(hwnd, message, w_param, l_param);
}

common::Status SettingsWindow::Create(HINSTANCE instance, HWND owner) {
  instance_ = instance;
  owner_ = owner;

  WNDCLASSEXW window_class{};
  window_class.cbSize = sizeof(window_class);
  window_class.style = CS_DBLCLKS;
  window_class.hInstance = instance_;
  window_class.lpfnWndProc = &SettingsWindow::StaticWindowProc;
  window_class.lpszClassName = kWindowClassName;
  window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  window_class.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  RegisterClassExW(&window_class);

  hwnd_ = CreateWindowExW(
      WS_EX_APPWINDOW | WS_EX_NOACTIVATE,
      kWindowClassName,
      L"Scraps \u8bbe\u7f6e",
      WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
      CW_USEDEFAULT,
      CW_USEDEFAULT,
      800,
      600,
      owner_,
      nullptr,
      instance_,
      this);
  if (hwnd_ == nullptr) {
    return common::Status::Error(common::StatusCode::PlatformError, "Failed to create settings window.");
  }

  common::Status status = d2d_shell_.Initialize(hwnd_);
  if (!status.ok()) {
    return status;
  }
  status = d2d_shell_.EnsureSecondaryTextFormats();
  if (!status.ok()) {
    return status;
  }
  status = CreateInputProxies();
  if (!status.ok()) {
    return status;
  }

  LayoutWindow();
  UpdateProxyLayout();
  return common::Status::Ok();
}

void SettingsWindow::Show(const core::store::AppSettings& settings, HostActions actions) {
  host_actions_ = std::move(actions);
  current_settings_ = settings;
  LoadDraftFromSettings(settings);
  RefreshStats();
  selected_section_ = Section::General;
  search_text_.clear();
  active_field_ = FieldId::None;
  active_select_field_ = SelectField::None;
  SetWindowAllowsActivation(false);
  ClearStatus();
  LayoutWindow();
  UpdateProxyLayout();
  POINT cursor{};
  GetCursorPos(&cursor);
  HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
  MONITORINFO monitor_info{};
  monitor_info.cbSize = sizeof(monitor_info);
  GetMonitorInfoW(monitor, &monitor_info);

  RECT window_rect{};
  GetWindowRect(hwnd_, &window_rect);
  const int width = window_rect.right - window_rect.left;
  const int height = window_rect.bottom - window_rect.top;
  const int left = monitor_info.rcWork.left + (monitor_info.rcWork.right - monitor_info.rcWork.left - width) / 2;
  const int top = monitor_info.rcWork.top + (monitor_info.rcWork.bottom - monitor_info.rcWork.top - height) / 2;

  SetWindowPos(hwnd_, HWND_TOP, left, top, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE);
  ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
  SetInteractionGuardActive(true);
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::SetWindowAllowsActivation(bool allow_activation) {
  if (hwnd_ == nullptr || window_allows_activation_ == allow_activation) {
    return;
  }

  LONG_PTR ex_style = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
  if (allow_activation) {
    ex_style &= ~static_cast<LONG_PTR>(WS_EX_NOACTIVATE);
  } else {
    ex_style |= static_cast<LONG_PTR>(WS_EX_NOACTIVATE);
  }
  SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, ex_style);
  SetWindowPos(
      hwnd_,
      nullptr,
      0,
      0,
      0,
      0,
      SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED |
          (allow_activation ? 0 : SWP_NOACTIVATE));
  window_allows_activation_ = allow_activation;
}

void SettingsWindow::SetInteractionGuardActive(bool active) {
  if (interaction_guard_active_ == active) {
    return;
  }
  interaction_guard_active_ = active;
  if (active) {
    if (host_actions_.on_show) {
      host_actions_.on_show();
    }
  } else if (host_actions_.on_hide) {
    host_actions_.on_hide();
  }
}

void SettingsWindow::Destroy() {
  SetInteractionGuardActive(false);
  DestroyInputProxies();
  d2d_shell_.Shutdown();
  if (hwnd_ != nullptr) {
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
}

common::Status SettingsWindow::CreateInputProxies() {
  if (!single_line_textbox_.Initialize(d2d_shell_.dwrite_factory())) {
    return common::Status::Error(common::StatusCode::PlatformError, "Failed to create single-line textbox.");
  }
  single_line_textbox_.SetDrawBorder(false);
  single_line_textbox_.SetDrawKnob(false);
  single_line_textbox_.SetDrawCloseButton(false);
  single_line_textbox_.SetMultiLine(false);
  single_line_textbox_.SetFontSize(12.0f);
  single_line_textbox_.SetTextColor(D2D1::ColorF(0x212121));
  single_line_textbox_.SetPlaceholderColor(D2D1::ColorF(0x757575));
  
  if (!multi_line_textbox_.Initialize(d2d_shell_.dwrite_factory())) {
    return common::Status::Error(common::StatusCode::PlatformError, "Failed to create multi-line textbox.");
  }
  multi_line_textbox_.SetDrawBorder(false);
  multi_line_textbox_.SetDrawKnob(false);
  multi_line_textbox_.SetDrawCloseButton(false);
  multi_line_textbox_.SetMultiLine(true);
  multi_line_textbox_.SetFontSize(12.0f);
  multi_line_textbox_.SetTextColor(D2D1::ColorF(0x212121));
  multi_line_textbox_.SetPlaceholderColor(D2D1::ColorF(0x757575));

  return common::Status::Ok();
}

void SettingsWindow::DestroyInputProxies() {
}

void SettingsWindow::LoadDraftFromSettings(const core::store::AppSettings& settings) {
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
  draft_.list_density = settings.list_density;
  draft_.window_position_mode = settings.window_position_mode;
  draft_.hotkey = settings.hotkey;
  draft_.quick_paste_hotkey = settings.quick_paste_hotkey;
  if (draft_.quick_paste_hotkey.empty()) {
    draft_.quick_paste_hotkey = L"Alt+~";
  }
  draft_.focus_search_on_open = settings.focus_search_on_open;
  draft_.search_focus_hotkey = settings.search_focus_hotkey;
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

core::store::AppSettings SettingsWindow::BuildSettingsFromDraft() const {
  core::store::AppSettings settings = current_settings_;
  try {
    settings.history_limit = std::max(1, std::stoi(draft_.history_limit_text));
  } catch (...) {
    settings.history_limit = current_settings_.history_limit;
  }
  try {
    settings.history_retention_days = std::max(0, std::stoi(draft_.history_retention_days_text));
  } catch (...) {
    settings.history_retention_days = current_settings_.history_retention_days;
  }
  settings.start_with_windows = draft_.start_with_windows;
  settings.open_panel_on_first_run = draft_.open_panel_on_first_run;
  settings.show_source_icon = draft_.show_source_icon;
  settings.monitoring_enabled = draft_.monitoring_enabled;
  settings.queue_auto_pop = draft_.queue_auto_pop;
  settings.enable_win_v_override = draft_.enable_win_v_override;
  settings.window_blur_type = draft_.window_blur_type;
  settings.window_blur_opacity = draft_.window_blur_opacity;
  settings.list_density = draft_.list_density;
  settings.window_position_mode = draft_.window_position_mode;
  settings.hotkey = common::TrimWide(draft_.hotkey);
  settings.quick_paste_hotkey = common::TrimWide(draft_.quick_paste_hotkey);
  settings.focus_search_on_open = draft_.focus_search_on_open;
  settings.search_focus_hotkey = common::TrimWide(draft_.search_focus_hotkey);
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
    settings.max_text_size_kb = current_settings_.max_text_size_kb;
  }
  settings.ignore_large_images = draft_.ignore_large_images;
  try {
    settings.max_image_size_kb = std::max(1, std::stoi(draft_.max_image_size_kb_text));
  } catch (...) {
    settings.max_image_size_kb = current_settings_.max_image_size_kb;
  }
  settings.ignore_otp_codes = draft_.ignore_otp_codes;
  settings.ignore_complex_passwords = draft_.ignore_complex_passwords;
  settings.screenshot_enabled = draft_.screenshot_enabled;
  settings.screenshot_prewarm = draft_.screenshot_prewarm;
  settings.screenshot_hotkey = common::TrimWide(draft_.screenshot_hotkey);
  return settings;
}

bool SettingsWindow::ValidateDraft(std::wstring* error_message) const {
  const std::wstring history_text = common::TrimWide(draft_.history_limit_text);
  if (history_text.empty()) {
    if (error_message != nullptr) {
      *error_message = L"\u8bf7\u8f93\u5165\u5408\u6cd5\u7684\u5386\u53f2\u8bb0\u5f55\u4e0a\u9650\u3002";
    }
    return false;
  }

  wchar_t* end = nullptr;
  const long value = std::wcstol(history_text.c_str(), &end, 10);
  if (end == history_text.c_str() || *end != L'\0' || value <= 0 || value > 500000) {
    if (error_message != nullptr) {
      *error_message = L"\u5386\u53f2\u8bb0\u5f55\u4e0a\u9650\u5fc5\u987b\u662f 1 \u5230 500000 \u4e4b\u95f4\u7684\u6574\u6570\u3002";
    }
    return false;
  }

  const std::wstring retention_text = common::TrimWide(draft_.history_retention_days_text);
  if (retention_text.empty()) {
    if (error_message != nullptr) {
      *error_message = L"\u8bf7\u8f93\u5165\u5408\u6cd5\u7684\u5386\u53f2\u4fdd\u7559\u5929\u6570\u3002";
    }
    return false;
  }
  end = nullptr;
  const long retention_value = std::wcstol(retention_text.c_str(), &end, 10);
  if (end == retention_text.c_str() || *end != L'\0' || retention_value < 0 || retention_value > 3650) {
    if (error_message != nullptr) {
      *error_message = L"\u5386\u53f2\u4fdd\u7559\u5929\u6570\u5fc5\u987b\u662f 0 \u5230 3650 \u4e4b\u95f4\u7684\u6574\u6570\u3002"; // 历史保留天数必须是 0 到 3650 之间的整数。
    }
    return false;
  }

  const std::wstring text_limit_text = common::TrimWide(draft_.max_text_size_kb_text);
  if (draft_.ignore_large_text && text_limit_text.empty()) {
    if (error_message != nullptr) {
      *error_message = L"\u8bf7\u8f93\u5165\u6700\u5927\u6587\u672c\u5927\u5c0f\u9650\u5236\u3002"; // 请输入最大文本大小限制。
    }
    return false;
  }
  if (draft_.ignore_large_text) {
    end = nullptr;
    const long limit_value = std::wcstol(text_limit_text.c_str(), &end, 10);
    if (end == text_limit_text.c_str() || *end != L'\0' || limit_value < 1 || limit_value > 102400) {
      if (error_message != nullptr) {
        *error_message = L"\u6587\u672c\u5927\u5c0f\u9650\u5236\u5fc5\u987b\u662f 1 \u5230 102400 KB \u4e4b\u95f4\u7684\u6574\u6570\u3002"; // 文本大小限制必须是 1 到 102400 KB 之间的整数。
      }
      return false;
    }
  }

  const std::wstring image_limit_text = common::TrimWide(draft_.max_image_size_kb_text);
  if (draft_.ignore_large_images && image_limit_text.empty()) {
    if (error_message != nullptr) {
      *error_message = L"\u8bf7\u8f93\u5165\u6700\u5927\u56fe\u7247\u5927\u5c0f\u9650\u5236\u3002"; // 请输入最大图片大小限制。
    }
    return false;
  }
  if (draft_.ignore_large_images) {
    end = nullptr;
    const long limit_value = std::wcstol(image_limit_text.c_str(), &end, 10);
    if (end == image_limit_text.c_str() || *end != L'\0' || limit_value < 1 || limit_value > 102400) {
      if (error_message != nullptr) {
        *error_message = L"\u56fe\u7247\u5927\u5c0f\u9650\u5236\u5fc5\u987b\u662f 1 \u5230 102400 KB \u4e4b\u95f4\u7684\u6574\u6570\u3002"; // 图片大小限制必须是 1 到 102400 KB 之间的整数。
      }
      return false;
    }
  }

  platform::win32::HotkeySpec global_hotkey;
  if (!platform::win32::ParseHotkeyText(common::TrimWide(draft_.hotkey), &global_hotkey).ok()) {
    if (error_message != nullptr) {
      *error_message = L"\u9762\u677f\u70ed\u952e\u683c\u5f0f\u65e0\u6548\uff0c\u793a\u4f8b\uff1aCtrl+Shift+V";
    }
    return false;
  }

  if (!platform::win32::ParseHotkeyText(common::TrimWide(draft_.quick_paste_hotkey), &global_hotkey).ok()) {
    if (error_message != nullptr) {
      *error_message = L"\u5feb\u6377\u7c98\u8d34\u70ed\u952e\u65e0\u6548\uff0c\u793a\u4f8b\uff1aAlt+~";
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
      *error_message = L"\u805a\u7126\u641c\u7d22\u6846\u5feb\u6377\u952e\u65e0\u6548\uff0c\u793a\u4f8b\uff1aTab \u6216 Ctrl+F";
    }
    return false;
  }

  const std::wstring sticker_hotkey_text = common::TrimWide(draft_.third_party_sticker_hotkey);
  if (draft_.third_party_sticker_enabled && sticker_hotkey_text.empty()) {
    if (error_message != nullptr) {
      *error_message = L"\u8bf7\u914d\u7f6e\u7b2c\u4e09\u65b9\u8d34\u56fe\u5feb\u6377\u952e\u3002";
    }
    return false;
  }
  if (!sticker_hotkey_text.empty() &&
      !platform::win32::ParseLocalHotkeyText(sticker_hotkey_text, &local_hotkey).ok()) {
    if (error_message != nullptr) {
      *error_message = L"\u7b2c\u4e09\u65b9\u8d34\u56fe\u5feb\u6377\u952e\u65e0\u6548\uff0c\u793a\u4f8b\uff1aF2 \u6216 Ctrl+Shift+P";
    }
    return false;
  }

  return true;
}

bool SettingsWindow::IsDraftDirty() const {
  if (draft_.history_limit_text != std::to_wstring(current_settings_.history_limit)) {
    return true;
  }
  if (draft_.history_retention_days_text != std::to_wstring(current_settings_.history_retention_days)) {
    return true;
  }
  if (draft_.start_with_windows != current_settings_.start_with_windows ||
      draft_.open_panel_on_first_run != current_settings_.open_panel_on_first_run ||
      draft_.show_source_icon != current_settings_.show_source_icon ||
      draft_.monitoring_enabled != current_settings_.monitoring_enabled ||
      draft_.queue_auto_pop != current_settings_.queue_auto_pop ||
      draft_.enable_win_v_override != current_settings_.enable_win_v_override ||
      draft_.list_density != current_settings_.list_density ||
      draft_.window_position_mode != current_settings_.window_position_mode ||
      common::TrimWide(draft_.hotkey) != common::TrimWide(current_settings_.hotkey) ||
      common::TrimWide(draft_.quick_paste_hotkey) != common::TrimWide(current_settings_.quick_paste_hotkey) ||
      draft_.focus_search_on_open != current_settings_.focus_search_on_open ||
      common::TrimWide(draft_.search_focus_hotkey) != common::TrimWide(current_settings_.search_focus_hotkey) ||
      draft_.third_party_sticker_enabled != current_settings_.third_party_sticker_enabled ||
      common::TrimWide(draft_.third_party_sticker_hotkey) != common::TrimWide(current_settings_.third_party_sticker_hotkey) ||
      draft_.click_paste_mode != current_settings_.click_paste_mode ||
      draft_.promote_pasted_clip_on_reuse != current_settings_.promote_pasted_clip_on_reuse ||
      common::TrimWide(draft_.external_editor_path) != common::TrimWide(current_settings_.external_editor_path) ||
      draft_.image_ocr_enabled != current_settings_.image_ocr_enabled ||
      common::TrimWide(draft_.ocr_executable_path) != common::TrimWide(current_settings_.ocr_executable_path) ||
      common::TrimWide(draft_.ocr_language_hint) != common::TrimWide(current_settings_.ocr_language_hint) ||
      draft_.blacklist != current_settings_.blacklist ||
      draft_.screenshot_enabled != current_settings_.screenshot_enabled ||
      draft_.screenshot_prewarm != current_settings_.screenshot_prewarm ||
      common::TrimWide(draft_.screenshot_hotkey) != common::TrimWide(current_settings_.screenshot_hotkey)) {
    return true;
  }
  return false;
}

void SettingsWindow::RefreshStats() {
  stats_ = {};
  if (host_actions_.query_data_stats) {
    stats_ = host_actions_.query_data_stats();
  }
}

void SettingsWindow::SetStatus(std::wstring text, bool error) {
  status_text_ = std::move(text);
  status_error_ = error;
  status_persistent_ = true;
  InvalidateRect(hwnd_, &bottom_bar_rect_, FALSE);
}

void SettingsWindow::ClearStatus() {
  status_text_.clear();
  status_error_ = false;
  status_persistent_ = false;
}

void SettingsWindow::UpdateSearchSelection() {
  const std::wstring query = common::TrimWide(search_text_);
  if (query.empty()) {
    return;
  }

  constexpr std::array<Section, 7> kSections = {
      Section::General,
      Section::Appearance,
      Section::Hotkey,
      Section::Clipboard,
      Section::Filter,
      Section::Data,
      Section::About};
  constexpr std::array<const wchar_t*, 7> kKeywords = {
      L"\u5e38\u89c4 \u5386\u53f2 \u5f00\u673a",
      L"\u5916\u89c2 \u5bc6\u5ea6 \u56fe\u6807",
      L"\u5feb\u6377\u952e \u70ed\u952e \u641c\u7d22",
      L"\u526a\u8d34\u677f \u76d1\u542c \u4f4d\u7f6e \u7c98\u8d34",
      L"\u8fc7\u6ee4 \u9ed1\u540d\u5355",
      L"\u6570\u636e \u6e05\u7a7a \u76ee\u5f55 \u5927\u5c0f",
      L"\u5173\u4e8e \u7248\u672c \u6e32\u67d3"};

  for (size_t index = 0; index < kSections.size(); ++index) {
    if (ContainsInsensitive(SectionTitle(kSections[index]), query) || ContainsInsensitive(kKeywords[index], query)) {
      selected_section_ = kSections[index];
      return;
    }
  }
}

void SettingsWindow::OpenSelectField(SelectField field) {
  if (field == SelectField::None) {
    CloseSelectField();
    return;
  }

  EndEditing(true);
  if (active_select_field_ == field) {
    CloseSelectField();
    return;
  }

  active_select_field_ = field;
  active_select_hover_index_ = GetSelectSelectedIndex(field);
  UpdateSelectMenuLayout();
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::CloseSelectField() {
  if (active_select_field_ == SelectField::None) {
    return;
  }
  active_select_field_ = SelectField::None;
  active_select_hover_index_ = -1;
  SetRectEmpty(&select_menu_rect_);
  select_option_rects_.fill(RECT{});
  InvalidateRect(hwnd_, nullptr, FALSE);
}

int SettingsWindow::GetSelectOptionCount(SelectField field) const {
  switch (field) {
    case SelectField::ListDensity:
    case SelectField::WindowPositionMode:
    case SelectField::ClickPasteMode:
      return 2;
    case SelectField::WindowBlurType:
      return 3;
    case SelectField::None:
      break;
  }
  return 0;
}

int SettingsWindow::GetSelectSelectedIndex(SelectField field) const {
  switch (field) {
    case SelectField::ListDensity:
      return draft_.list_density == core::store::ListDensity::Compact ? 0 : 1;
    case SelectField::WindowPositionMode:
      return draft_.window_position_mode == core::store::WindowPositionMode::Remember ? 0 : 1;
    case SelectField::ClickPasteMode:
      return draft_.click_paste_mode == core::store::ClickPasteMode::Single ? 0 : 1;
    case SelectField::WindowBlurType:
      if (draft_.window_blur_type == core::store::WindowBlurType::None) return 0;
      if (draft_.window_blur_type == core::store::WindowBlurType::Mica) return 1;
      return 2;
    case SelectField::None:
      break;
  }
  return 0;
}

std::wstring SettingsWindow::GetSelectLabel(SelectField field) const {
  switch (field) {
    case SelectField::ListDensity:
      return draft_.list_density == core::store::ListDensity::Compact ? L"紧凑" : L"舒适";
    case SelectField::WindowPositionMode:
      return draft_.window_position_mode == core::store::WindowPositionMode::Remember ? L"记住位置" : L"跟随鼠标";
    case SelectField::ClickPasteMode:
      return draft_.click_paste_mode == core::store::ClickPasteMode::Single ? L"单击粘贴" : L"双击粘贴";
    case SelectField::WindowBlurType:
      return draft_.window_blur_type == core::store::WindowBlurType::Mica ? L"Mica" : (draft_.window_blur_type == core::store::WindowBlurType::Acrylic ? L"Acrylic" : L"无");
    case SelectField::None:
      break;
  }
  return {};
}

std::wstring SettingsWindow::GetSelectOptionLabel(SelectField field, int option) const {
  switch (field) {
    case SelectField::ListDensity:
      return option == 0 ? L"紧凑" : L"舒适";
    case SelectField::WindowPositionMode:
      return option == 0 ? L"记住位置" : L"跟随鼠标";
    case SelectField::ClickPasteMode:
      return option == 0 ? L"单击粘贴" : L"双击粘贴";
    case SelectField::WindowBlurType:
      if (option == 0) return L"无";
      if (option == 1) return L"Mica";
      return L"Acrylic";
    case SelectField::None:
      break;
  }
  return {};
}

void SettingsWindow::SetSelectOption(SelectField field, int option) {
  switch (field) {
    case SelectField::ListDensity:
      draft_.list_density = option == 0 ? core::store::ListDensity::Compact : core::store::ListDensity::Comfortable;
      break;
    case SelectField::WindowPositionMode:
      draft_.window_position_mode =
          option == 0 ? core::store::WindowPositionMode::Remember : core::store::WindowPositionMode::FollowMouse;
      break;
    case SelectField::ClickPasteMode:
      draft_.click_paste_mode =
          option == 0 ? core::store::ClickPasteMode::Single : core::store::ClickPasteMode::Double;
      break;
    case SelectField::WindowBlurType:
      if (option == 0) draft_.window_blur_type = core::store::WindowBlurType::None;
      else if (option == 1) draft_.window_blur_type = core::store::WindowBlurType::Mica;
      else draft_.window_blur_type = core::store::WindowBlurType::Acrylic;
      break;
    case SelectField::None:
      break;
  }
}

void SettingsWindow::UpdateSelectMenuLayout() {
  select_option_rects_.fill(RECT{});
  SetRectEmpty(&select_menu_rect_);
  if (active_select_field_ == SelectField::None || hwnd_ == nullptr) {
    return;
  }

  if (active_select_field_ == SelectField::None) return;
  auto it = select_rects_.find(active_select_field_);
  if (it == select_rects_.end()) return;
  RECT trigger = it->second;

  const int option_count = GetSelectOptionCount(active_select_field_);
  if (option_count <= 0) {
    return;
  }

  const int option_height = DipToPx(36.0f);
  const int trigger_width = static_cast<int>(trigger.right - trigger.left);
  const int menu_width = std::max(trigger_width, DipToPx(172.0f));
  const int menu_height = option_count * option_height + DipToPx(8.0f);
  RECT client{};
  GetClientRect(hwnd_, &client);

  int left = trigger.right - menu_width;
  int top = trigger.bottom + DipToPx(8.0f);
  if (left + menu_width > client.right - DipToPx(16.0f)) {
    left = client.right - DipToPx(16.0f) - menu_width;
  }
  if (left < DipToPx(16.0f)) {
    left = DipToPx(16.0f);
  }
  if (top + menu_height > client.bottom - DipToPx(16.0f)) {
    top = trigger.top - DipToPx(8.0f) - menu_height;
  }

  select_menu_rect_ = {left, top, left + menu_width, top + menu_height};
  for (int index = 0; index < option_count; ++index) {
    select_option_rects_[index] = {
        select_menu_rect_.left + DipToPx(4.0f), select_menu_rect_.top + DipToPx(4.0f) + index * option_height, select_menu_rect_.right - DipToPx(4.0f), select_menu_rect_.top + DipToPx(4.0f) + (index + 1) * option_height};
  }
}

void SettingsWindow::BeginEditing(FieldId field) {
  if (field == FieldId::None) {
    return;
  }
  if (IsHotkeyField(field) && active_field_ == field) {
    SetFocus(hwnd_);
    InvalidateRect(hwnd_, nullptr, FALSE);
    return;
  }
  if (active_field_ == field) {
    UpdateProxyLayout();
    D2DWindowlessTextBox* target = (field == FieldId::Blacklist || field == FieldId::CustomBlacklist) ? &multi_line_textbox_ : &single_line_textbox_;
    target->SetFocused(true, hwnd_);
    return;
  }

  CloseSelectField();
  EndEditing(true);
  if (active_field_ == FieldId::None && host_actions_.on_begin_input_focus) {
    host_actions_.on_begin_input_focus();
  }
  active_field_ = field;
  SetWindowAllowsActivation(true);
  ShowWindow(hwnd_, SW_SHOWNORMAL);
  BringWindowToTop(hwnd_);
  SetForegroundWindow(hwnd_);
  SetActiveWindow(hwnd_);
  if (IsHotkeyField(field)) {
    selected_section_ = Section::Hotkey;
    SetFocus(hwnd_);
    InvalidateRect(hwnd_, nullptr, FALSE);
    return;
  }
  D2DWindowlessTextBox* target = (field == FieldId::Blacklist || field == FieldId::CustomBlacklist) ? &multi_line_textbox_ : &single_line_textbox_;

  std::wstring text;
  switch (field) {
    case FieldId::Search:
      text = search_text_;
      break;
    case FieldId::HistoryLimit:
      text = draft_.history_limit_text;
      selected_section_ = Section::General;
      break;
    case FieldId::HistoryRetentionDays:
      text = draft_.history_retention_days_text;
      selected_section_ = Section::General;
      break;
    case FieldId::Hotkey:
      text = draft_.hotkey;
      selected_section_ = Section::Hotkey;
      break;
    case FieldId::QuickPasteHotkey:
      text = draft_.quick_paste_hotkey;
      selected_section_ = Section::Hotkey;
      break;
    case FieldId::SearchFocusHotkey:
      text = draft_.search_focus_hotkey;
      selected_section_ = Section::Hotkey;
      break;
    case FieldId::ThirdPartyStickerHotkey:
      text = draft_.third_party_sticker_hotkey;
      selected_section_ = Section::Hotkey;
      break;
    case FieldId::ExternalEditorPath:
      text = draft_.external_editor_path;
      selected_section_ = Section::Data;
      break;
    case FieldId::OcrExecutablePath:
      text = draft_.ocr_executable_path;
      selected_section_ = Section::Data;
      break;
    case FieldId::OcrLanguageHint:
      text = draft_.ocr_language_hint;
      selected_section_ = Section::Data;
      break;
    case FieldId::Blacklist:
      text = draft_.blacklist;
      selected_section_ = Section::Filter;
      break;
    case FieldId::CustomBlacklist:
      text = draft_.custom_blacklist_apps;
      selected_section_ = Section::Filter;
      break;
    case FieldId::MaxTextSize:
      text = draft_.max_text_size_kb_text;
      selected_section_ = Section::Filter;
      break;
    case FieldId::MaxImageSize:
      text = draft_.max_image_size_kb_text;
      selected_section_ = Section::Filter;
      break;
    case FieldId::None:
      break;
  }

  target->SetText(text);
  UpdateProxyLayout();
  target->SetFocused(true, hwnd_);
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::EndEditing(bool commit) {
  if (active_field_ == FieldId::None) {
    return;
  }
  if (commit) {
    SyncActiveEditToState();
  }
  active_field_ = FieldId::None;
  single_line_textbox_.SetFocused(false);
  multi_line_textbox_.SetFocused(false);
  SetWindowAllowsActivation(false);
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::SyncActiveEditToState() {
  if (active_field_ == FieldId::None) {
    return;
  }
  if (IsHotkeyField(active_field_)) {
    return;
  }
  D2DWindowlessTextBox* source = (active_field_ == FieldId::Blacklist || active_field_ == FieldId::CustomBlacklist) ? &multi_line_textbox_ : &single_line_textbox_;
  const std::wstring text = source->GetText();
  switch (active_field_) {
    case FieldId::Search:
      search_text_ = text;
      UpdateSearchSelection();
      break;
    case FieldId::HistoryLimit:
      draft_.history_limit_text = text;
      break;
    case FieldId::HistoryRetentionDays:
      draft_.history_retention_days_text = text;
      break;
    case FieldId::Hotkey:
      draft_.hotkey = text;
      break;
    case FieldId::QuickPasteHotkey:
      draft_.quick_paste_hotkey = text;
      break;
    case FieldId::SearchFocusHotkey:
      draft_.search_focus_hotkey = text;
      break;
    case FieldId::ThirdPartyStickerHotkey:
      draft_.third_party_sticker_hotkey = text;
      break;
    case FieldId::ExternalEditorPath:
      draft_.external_editor_path = text;
      break;
    case FieldId::OcrExecutablePath:
      draft_.ocr_executable_path = text;
      break;
    case FieldId::OcrLanguageHint:
      draft_.ocr_language_hint = text;
      break;
    case FieldId::Blacklist:
      draft_.blacklist = text;
      break;
    case FieldId::CustomBlacklist:
      draft_.custom_blacklist_apps = text;
      break;
    case FieldId::MaxTextSize:
      draft_.max_text_size_kb_text = text;
      break;
    case FieldId::MaxImageSize:
      draft_.max_image_size_kb_text = text;
      break;
    case FieldId::None:
      break;
  }
}

void SettingsWindow::UpdateProxyLayout() {
  if (active_field_ == FieldId::None) {

    return;
  }

  RECT rect{};
  auto it = field_rects_.find(active_field_);
  if (it != field_rects_.end()) {
    rect = it->second;
  } else if (active_field_ == FieldId::Search) {
    rect = search_rect_;
  } else {
    return;
  }

  D2DWindowlessTextBox* target = (active_field_ == FieldId::CustomBlacklist || active_field_ == FieldId::Blacklist) ? &multi_line_textbox_ : &single_line_textbox_;

  RECT target_rect = rect;
  if (active_field_ == FieldId::Search) {
    target_rect.left = rect.left + DipToPx(36.0f);
    target_rect.top = rect.top + DipToPx(2.0f);
    target_rect.right = rect.right - DipToPx(12.0f);
    target_rect.bottom = rect.bottom - DipToPx(2.0f);
    
    target->SetFontSize(12.0f);
    target->SetRect(ToRectF(target_rect));
  } else if (active_field_ == FieldId::CustomBlacklist) {
    target_rect.left = rect.left + DipToPx(16.0f);
    target_rect.top = rect.top + DipToPx(16.0f);
    target_rect.right = rect.right - DipToPx(16.0f);
    target_rect.bottom = rect.bottom - DipToPx(16.0f);
    
    target->SetFontSize(12.0f);
    target->SetRect(ToRectF(target_rect));
  } else {
    int horizontal_padding_dip = 10;
    if (active_field_ == FieldId::MaxTextSize || active_field_ == FieldId::MaxImageSize) {
      horizontal_padding_dip = 8;
    }
    
    target_rect.left = rect.left + DipToPx(static_cast<float>(horizontal_padding_dip));
    target_rect.right = rect.right - DipToPx(static_cast<float>(horizontal_padding_dip));
    target_rect.top = rect.top + DipToPx(2.0f);
    target_rect.bottom = rect.bottom - DipToPx(2.0f);
    
    target->SetFontSize(12.0f);
    target->SetRect(ToRectF(target_rect));
  }

  InvalidateRect(hwnd_, &rect, FALSE);
}

void SettingsWindow::FocusNextField(bool reverse) {
  constexpr std::array<FieldId, 11> kFields = {
      FieldId::Search,
      FieldId::HistoryLimit,
      FieldId::HistoryRetentionDays,
      FieldId::Hotkey,
      FieldId::QuickPasteHotkey,
      FieldId::SearchFocusHotkey,
      FieldId::ThirdPartyStickerHotkey,
      FieldId::ExternalEditorPath,
      FieldId::OcrExecutablePath,
      FieldId::OcrLanguageHint,
      FieldId::Blacklist};
  auto it = std::find(kFields.begin(), kFields.end(), active_field_);
  size_t index = it == kFields.end() ? 0 : static_cast<size_t>(std::distance(kFields.begin(), it));
  if (reverse) {
    index = index == 0 ? kFields.size() - 1 : index - 1;
  } else {
    index = (index + 1) % kFields.size();
  }
  BeginEditing(kFields[index]);
}

void SettingsWindow::HandlePrimaryAction(HitKind kind, int option) {
  if (kind != HitKind::Field) {
    EndEditing(true);
  }
  if (kind != HitKind::SelectTrigger && kind != HitKind::SelectOption) {
    CloseSelectField();
  }

  switch (kind) {
    case HitKind::Nav:
      selected_section_ = static_cast<Section>(option);
      scroll_y_ = 0.0f;
      ClearStatus();
      break;
    case HitKind::Field:
      BeginEditing(static_cast<FieldId>(option));
      return;
    case HitKind::Toggle:
      switch (option) {
        case 1:
          draft_.start_with_windows = !draft_.start_with_windows;
          break;
        case 2:
          draft_.open_panel_on_first_run = !draft_.open_panel_on_first_run;
          break;
        case 3:
          draft_.show_source_icon = !draft_.show_source_icon;
          break;
        case 4:
          draft_.monitoring_enabled = !draft_.monitoring_enabled;
          break;
        case 5:
          draft_.focus_search_on_open = !draft_.focus_search_on_open;
          break;
        case 6:
          draft_.queue_auto_pop = !draft_.queue_auto_pop;
          break;
        case 7:
          draft_.enable_win_v_override = !draft_.enable_win_v_override;
          break;
        case 8:
          draft_.third_party_sticker_enabled = !draft_.third_party_sticker_enabled;
          break;
        case 9:
          draft_.image_ocr_enabled = !draft_.image_ocr_enabled;
          break;
        case 10:
          draft_.promote_pasted_clip_on_reuse = !draft_.promote_pasted_clip_on_reuse;
          break;
        case 11:
          draft_.auto_ignore_password_managers = !draft_.auto_ignore_password_managers;
          break;
        case 12:
          draft_.ignore_large_text = !draft_.ignore_large_text;
          break;
        case 13:
          draft_.ignore_large_images = !draft_.ignore_large_images;
          break;
        case 14:
          draft_.ignore_otp_codes = !draft_.ignore_otp_codes;
          break;
        case 15:
          draft_.ignore_complex_passwords = !draft_.ignore_complex_passwords;
          break;
        case 16:
          draft_.screenshot_enabled = !draft_.screenshot_enabled;
          break;
        case 17:
          draft_.screenshot_prewarm = !draft_.screenshot_prewarm;
          break;
      }
      break;
    case HitKind::SelectTrigger:
      OpenSelectField(static_cast<SelectField>(option));
      return;
    case HitKind::SelectOption:
      SetSelectOption(active_select_field_, option);
      CloseSelectField();
      break;
    case HitKind::ActionClearHistory:
      if (MessageBoxW(
              hwnd_,
              L"\u786e\u8ba4\u6e05\u7a7a\u6240\u6709\u5386\u53f2\u533a\u6761\u76ee\u5417\uff1f",
              L"Scraps",
              MB_ICONWARNING | MB_YESNO) == IDYES) {
        const bool ok = host_actions_.on_clear_history ? host_actions_.on_clear_history() : false;
        SetStatus(ok ? L"\u5386\u53f2\u5df2\u6e05\u7a7a" : L"\u6e05\u7a7a\u5386\u53f2\u5931\u8d25", !ok);
        RefreshStats();
      }
      break;
    case HitKind::ActionClearImageCache: {
      const bool ok = host_actions_.on_clear_image_cache ? host_actions_.on_clear_image_cache() : false;
      SetStatus(ok ? L"\u56fe\u7247\u7f13\u5b58\u5df2\u6e05\u7406" : L"\u6e05\u7406\u56fe\u7247\u7f13\u5b58\u5931\u8d25", !ok);
      RefreshStats();
      break;
    }
    case HitKind::ActionOpenDataDir:
      if (host_actions_.on_open_data_directory) {
        host_actions_.on_open_data_directory();
      }
      SetStatus(L"\u5df2\u6253\u5f00\u6570\u636e\u76ee\u5f55", false);
      break;
    case HitKind::Save: {
      std::wstring error;
      if (!ValidateDraft(&error)) {
        SetStatus(error, true);
        break;
      }
      const core::store::AppSettings settings = BuildSettingsFromDraft();
      const bool ok = host_actions_.on_save ? host_actions_.on_save(settings) : false;
      if (ok) {
        current_settings_ = settings;
        LoadDraftFromSettings(settings);
        RefreshStats();
        SetStatus(L"\u8bbe\u7f6e\u5df2\u4fdd\u5b58", false);
      } else {
        SetStatus(L"\u4fdd\u5b58\u5931\u8d25", true);
      }
      break;
    }
    case HitKind::Cancel:
      HandleCloseRequest();
      return;
    case HitKind::None:
      break;
  }

  InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::HandleCloseRequest() {
  CloseSelectField();
  EndEditing(true);
  if (!ConfirmDiscardChanges()) {
    return;
  }
  ClearStatus();
  search_text_.clear();
  ShowWindow(hwnd_, SW_HIDE);
  if (host_actions_.on_end_input_focus) {
    host_actions_.on_end_input_focus();
  }
  SetInteractionGuardActive(false);
}

bool SettingsWindow::ConfirmDiscardChanges() {
  if (!IsDraftDirty()) {
    return true;
  }
  const int result = MessageBoxW(
      hwnd_,
      L"\u5f53\u524d\u6709\u672a\u4fdd\u5b58\u7684\u4fee\u6539\uff0c\u786e\u5b9a\u8981\u653e\u5f03\u5417\uff1f",
      L"Scraps",
      MB_ICONQUESTION | MB_YESNO);
  return result == IDYES;
}

const HitRegion* SettingsWindow::HitTest(POINT point) const {
  for (auto it = hit_regions_.rbegin(); it != hit_regions_.rend(); ++it) {
    if (PtInRect(&it->rect, point)) {
      return &(*it);
    }
  }
  return nullptr;
}


void SettingsWindow::LayoutWindow() {
  RECT client{};
  GetClientRect(hwnd_, &client);
  const int outer = DipToPx(24.0f);
  const int nav_width = DipToPx(200.0f);
  const int search_height = DipToPx(36.0f);
  const int nav_item_height = DipToPx(40.0f);
  const int logo_height = DipToPx(32.0f);

  search_rect_ = {outer, outer + logo_height + DipToPx(20.0f), outer + nav_width, outer + logo_height + DipToPx(20.0f) + search_height};

  content_rect_ = {
      outer + nav_width + DipToPx(32.0f), DipToPx(16.0f), client.right - outer, client.bottom - DipToPx(46.0f)};

  const int pill_width = DipToPx(320.0f);
  const int pill_height = DipToPx(44.0f);
  bottom_bar_rect_ = {
      client.right - outer - pill_width, client.bottom - DipToPx(1.0f) - pill_height, client.right - outer, client.bottom - DipToPx(1.0f)};

  save_button_rect_ = {bottom_bar_rect_.right - DipToPx(90.0f), bottom_bar_rect_.top + DipToPx(8.0f), bottom_bar_rect_.right - DipToPx(8.0f), bottom_bar_rect_.bottom - DipToPx(8.0f)};
  cancel_button_rect_ = {save_button_rect_.left - DipToPx(90.0f), save_button_rect_.top, save_button_rect_.left - DipToPx(8.0f), save_button_rect_.bottom};
  status_rect_ = {bottom_bar_rect_.left + DipToPx(16.0f), bottom_bar_rect_.top, cancel_button_rect_.left - DipToPx(16.0f), bottom_bar_rect_.bottom};

  int nav_top = search_rect_.bottom + DipToPx(24.0f);
  for (size_t index = 0; index < nav_rects_.size(); ++index) {
    nav_rects_[index] = {
        outer, nav_top + static_cast<int>(index) * (nav_item_height + DipToPx(4.0f)), outer + nav_width, nav_top + static_cast<int>(index) * (nav_item_height + DipToPx(4.0f)) + nav_item_height};
  }
}

void SettingsWindow::RenderFrame() {
  ID2D1HwndRenderTarget* target = d2d_shell_.BeginFrame();
  if (target == nullptr) {
    return;
  }

  target->Clear(D2D1::ColorF(0xF9F9F9)); // slightly lighter background for whole window
  hit_regions_.clear();
select_option_rects_.fill(RECT{});
  SetRectEmpty(&select_menu_rect_);

  RECT client{};
  GetClientRect(hwnd_, &client);
  const int outer = DipToPx(24.0f);
  const int nav_width = DipToPx(200.0f);

  const float divider_x = static_cast<float>(outer + nav_width + DipToPx(16.0f));
  target->DrawLine(
      D2D1::Point2F(divider_x, 0.0f),
      D2D1::Point2F(divider_x, static_cast<float>(client.bottom)),
      d2d_shell_.border_brush(),
      0.5f);

  const RECT title_rect = {outer, outer, outer + nav_width, outer + DipToPx(26.0f)};
  DrawTextLine(target, d2d_shell_.section_format(), d2d_shell_.text_brush(), title_rect, L"Scraps");

  FillRoundedPanel(target, search_rect_, 14.0f, d2d_shell_.surface_brush(), d2d_shell_.border_brush(), 0.5f);
  const RECT search_icon_rect = {search_rect_.left + DipToPx(12.0f), search_rect_.top + DipToPx(8.0f), search_rect_.left + DipToPx(28.0f), search_rect_.top + DipToPx(23.0f)};
  DrawFluentIcon(d2d_shell_.d2d_factory(), target, FluentIcon::Search, ToRectF(search_icon_rect), d2d_shell_.muted_text_brush(), 0.35f);
  AddHitRegion(clipd2d::ui::HitRegion{search_rect_, HitKind::Field, selected_section_, FieldId::Search, static_cast<int>(FieldId::Search)});
  if (active_field_ != FieldId::Search) {
    const RECT search_text_rect = {search_rect_.left + DipToPx(36.0f), search_rect_.top + DipToPx(2.0f), search_rect_.right - DipToPx(12.0f), search_rect_.bottom - DipToPx(2.0f)};
    DrawTextLine(
        target,
        d2d_shell_.small_format(),
        search_text_.empty() ? d2d_shell_.subtle_text_brush() : d2d_shell_.text_brush(),
        search_text_rect,
        search_text_.empty() ? L"\u641c\u7d22\u8bbe\u7f6e\u9879" : search_text_);
  }

  RenderNavigation(target);
  RenderContent(target);
  
  if (active_field_ != FieldId::None) {
    D2DWindowlessTextBox* active_textbox = (active_field_ == FieldId::Blacklist || active_field_ == FieldId::CustomBlacklist) ? &multi_line_textbox_ : &single_line_textbox_;
    active_textbox->Draw(target);
  }

  RenderBottomBar(target);
  if (active_select_field_ != SelectField::None) {
    UpdateSelectMenuLayout();
    FillRoundedPanel(target, select_menu_rect_, 14.0f, d2d_shell_.surface_brush(), d2d_shell_.border_brush(), 0.5f);
    const int option_count = GetSelectOptionCount(active_select_field_);
    for (int index = 0; index < option_count; ++index) {
      const bool selected = index == GetSelectSelectedIndex(active_select_field_);
      const bool hovered = index == active_select_hover_index_;
      FillRoundedPanel(
          target,
          select_option_rects_[index],
          10.0f,
          selected ? d2d_shell_.accent_soft_brush() : (hovered ? d2d_shell_.surface_hover_brush() : d2d_shell_.surface_brush()),
          nullptr,
          0.0f);
      RECT option_text_rect = select_option_rects_[index];
      option_text_rect.left += DipToPx(12.0f);
      option_text_rect.right -= DipToPx(12.0f);
      DrawTextLine(
          target,
          d2d_shell_.small_format(),
          selected ? d2d_shell_.accent_brush() : d2d_shell_.text_brush(),
          option_text_rect,
          GetSelectOptionLabel(active_select_field_, index),
          DWRITE_TEXT_ALIGNMENT_LEADING);
      AddHitRegion(clipd2d::ui::HitRegion{select_option_rects_[index], HitKind::SelectOption, selected_section_, FieldId::None, index});
    }
  }
  if (d2d_shell_.EndFrame()) {
    InvalidateRect(hwnd_, nullptr, FALSE);
  }
}

void SettingsWindow::RenderNavigation(ID2D1HwndRenderTarget* target) {
  for (size_t index = 0; index < nav_rects_.size(); ++index) {
    const Section section = static_cast<Section>(index);
    const bool selected = section == selected_section_;
    const bool hovered = static_cast<int>(index) == active_nav_hover_index_;

    FillRoundedPanel(
        target,
        nav_rects_[index],
        12.0f,
        selected ? d2d_shell_.accent_brush() : (hovered ? d2d_shell_.surface_brush() : nullptr),
        nullptr,
        0.0f);

    const RECT icon_rect = {nav_rects_[index].left + DipToPx(12.0f), nav_rects_[index].top + DipToPx(12.0f), nav_rects_[index].left + DipToPx(28.0f), nav_rects_[index].top + DipToPx(28.0f)};
    DrawFluentIcon(
        d2d_shell_.d2d_factory(),
        target,
        SectionIcon(section),
        ToRectF(icon_rect),
        selected ? d2d_shell_.white_brush() : d2d_shell_.text_brush(),
        selected ? 0.35f : 0.0f);

    const RECT text_rect = {nav_rects_[index].left + DipToPx(36.0f), nav_rects_[index].top, nav_rects_[index].right - DipToPx(8.0f), nav_rects_[index].bottom};
    DrawTextLine(
        target,
        d2d_shell_.small_format(),
        selected ? d2d_shell_.white_brush() : d2d_shell_.text_brush(),
        text_rect,
        SectionTitle(section));
    AddHitRegion(clipd2d::ui::HitRegion{nav_rects_[index], HitKind::Nav, section, FieldId::None, static_cast<int>(index)});
  }
}

void SettingsWindow::RenderContent(ID2D1HwndRenderTarget* target) {
  field_rects_.clear();
  select_rects_.clear();
  
  if (auto& panel = panels_[static_cast<int>(selected_section_)]) {
    max_scroll_y_ = panel->Render(target, draft_, this, content_rect_);
  }
}

void SettingsWindow::RenderBottomBar(ID2D1HwndRenderTarget* target) {
  FillRoundedPanel(
      target,
      bottom_bar_rect_,
      16.0f,
      d2d_shell_.surface_brush(),
      d2d_shell_.border_brush(),
      0.5f);

  FillRoundedPanel(
      target,
      cancel_button_rect_,
      6.0f,
      d2d_shell_.surface_alt_brush(),
      d2d_shell_.border_brush(),
      0.5f);
  FillRoundedPanel(
      target,
      save_button_rect_,
      6.0f,
      d2d_shell_.accent_brush(),
      d2d_shell_.accent_brush(),
      0.5f);
  DrawTextLine(target, d2d_shell_.body_format(), d2d_shell_.text_brush(), cancel_button_rect_, L"\u53d6\u6d88", DWRITE_TEXT_ALIGNMENT_CENTER);
  DrawTextLine(target, d2d_shell_.body_format(), d2d_shell_.white_brush(), save_button_rect_, L"\u4fdd\u5b58", DWRITE_TEXT_ALIGNMENT_CENTER);
  AddHitRegion(clipd2d::ui::HitRegion{cancel_button_rect_, HitKind::Cancel});
  AddHitRegion(clipd2d::ui::HitRegion{save_button_rect_, HitKind::Save});

  DrawTextLine(
      target, d2d_shell_.small_format(), status_error_ ? d2d_shell_.danger_brush() : d2d_shell_.muted_text_brush(),
      status_rect_,
      status_text_.empty() ? L"\u4fee\u6539\u540e\u70b9\u51fb\u4fdd\u5b58\u751f\u6548" : status_text_);
}

void DrawThinRowDivider(ID2D1HwndRenderTarget* target, D2dShell& shell, const RECT& row_rect) {
  float dpi_x = 96.0f, dpi_y = 96.0f;
  target->GetDpi(&dpi_x, &dpi_y);
  const float scale = dpi_x / 96.0f;
  const float y = (std::round(static_cast<float>(row_rect.bottom) * scale) - 0.5f) / scale;
  
  target->DrawLine(
      D2D1::Point2F(static_cast<float>(row_rect.left), y),
      D2D1::Point2F(static_cast<float>(row_rect.right), y),
      shell.border_brush(),
      1.0f / scale);
}
















LRESULT SettingsWindow::WindowProc(UINT message, WPARAM w_param, LPARAM l_param) {
  switch (message) {
    case WM_ERASEBKGND:
      return 1;
    case WM_SIZE:
      LayoutWindow();
      d2d_shell_.Resize(LOWORD(l_param), HIWORD(l_param));
      UpdateProxyLayout();
      InvalidateRect(hwnd_, nullptr, FALSE);
      return 0;
    case WM_DPICHANGED: {
      d2d_shell_.UpdateDpi(HIWORD(w_param));
      const RECT* suggested = reinterpret_cast<const RECT*>(l_param);
      SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top, suggested->right - suggested->left, suggested->bottom - suggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
      DestroyInputProxies();
      CreateInputProxies();
      LayoutWindow();
      UpdateProxyLayout();
      InvalidateRect(hwnd_, nullptr, FALSE);
      return 0;
    }
    case WM_PAINT: {
      PAINTSTRUCT paint{};
      BeginPaint(hwnd_, &paint);
      RenderFrame();
      EndPaint(hwnd_, &paint);
      return 0;
    }
    case WM_COMMAND:
      break;

    case WM_MOUSEACTIVATE: {
      return MA_NOACTIVATE;
    }
    case WM_MOUSEWHEEL: {
      if (max_scroll_y_ > 0.0f) {
        float delta = static_cast<float>(GET_WHEEL_DELTA_WPARAM(w_param)) / WHEEL_DELTA;
        scroll_y_ -= delta * 40.0f;
        if (scroll_y_ < 0.0f) scroll_y_ = 0.0f;
        if (scroll_y_ > max_scroll_y_) scroll_y_ = max_scroll_y_;
        InvalidateRect(hwnd_, &content_rect_, FALSE);
        if (active_field_ != FieldId::None) {
          UpdateProxyLayout();
        }
      }
      return 0;
    }
    case WM_MOUSEMOVE: {
      if (!is_tracking_mouse_leave_) {
        TRACKMOUSEEVENT tme = {sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd_, 0};
        TrackMouseEvent(&tme);
        is_tracking_mouse_leave_ = true;
      }
      POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
      
      if (active_field_ != FieldId::None && !IsHotkeyField(active_field_)) {
        D2DWindowlessTextBox* target = (active_field_ == FieldId::Blacklist || active_field_ == FieldId::CustomBlacklist) ? &multi_line_textbox_ : &single_line_textbox_;
        if (target->HandleMouseInput(hwnd_, message, D2D1::Point2F(static_cast<float>(point.x), static_cast<float>(point.y)))) {
          if (w_param & MK_LBUTTON) {
            InvalidateRect(hwnd_, nullptr, FALSE);
          }
        }
      }

      int nav_hover = -1;
      for (size_t index = 0; index < nav_rects_.size(); ++index) {
        if (PtInRect(&nav_rects_[index], point)) {
          nav_hover = static_cast<int>(index);
          break;
        }
      }
      if (nav_hover != active_nav_hover_index_) {
        active_nav_hover_index_ = nav_hover;
        InvalidateRect(hwnd_, nullptr, FALSE);
      }

      if (is_dragging_scrollbar_) {
        float view_h = static_cast<float>(content_rect_.bottom - content_rect_.top);
        float total_h = view_h + max_scroll_y_;
        float thumb_h = std::max(20.0f, view_h * (view_h / total_h));
        float max_thumb_y = view_h - thumb_h;
        if (max_thumb_y > 0.0f) {
          float dy = static_cast<float>(point.y - drag_start_y_);
          scroll_y_ = drag_start_scroll_y_ + (dy / max_thumb_y) * max_scroll_y_;
          if (scroll_y_ < 0.0f) scroll_y_ = 0.0f;
          if (scroll_y_ > max_scroll_y_) scroll_y_ = max_scroll_y_;
          InvalidateRect(hwnd_, &content_rect_, FALSE);
        }
        return 0;
      }
      
      if (is_dragging_slider_) {
        // Find the HitRegion for the slider to get the rect
        const HitRegion* region = nullptr;
        for (const auto& hit : hit_regions_) {
           if (hit.kind == HitKind::SliderDrag && hit.option == active_slider_index_) {
              region = &hit;
              break;
           }
        }
        if (region) {
          float width = static_cast<float>(region->rect.right - region->rect.left);
          float cx = static_cast<float>(region->rect.left);
          float percent = (static_cast<float>(point.x) - cx) / width;
          percent = std::clamp(percent, 0.0f, 1.0f);
          if (active_slider_index_ == 18) {
             draft_.window_blur_opacity = static_cast<int>(percent * 100.0f);
          }
          InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return 0;
      }
      if (active_select_field_ != SelectField::None) {
        int hover_index = -1;
        for (int index = 0; index < GetSelectOptionCount(active_select_field_); ++index) {
          if (PtInRect(&select_option_rects_[index], point)) {
            hover_index = index;
            break;
          }
        }
        if (hover_index != active_select_hover_index_) {
          active_select_hover_index_ = hover_index;
          InvalidateRect(hwnd_, &select_menu_rect_, FALSE);
        }
      }
      return 0;
    }
    case WM_MOUSELEAVE: {
      is_tracking_mouse_leave_ = false;
      bool needed = false;
      if (active_nav_hover_index_ != -1) {
        active_nav_hover_index_ = -1;
        needed = true;
      }
      if (active_select_hover_index_ != -1) {
        active_select_hover_index_ = -1;
        needed = true;
      }
      if (needed) {
        InvalidateRect(hwnd_, nullptr, FALSE);
      }
      return 0;
    }
    case WM_CAPTURECHANGED:
      if (reinterpret_cast<HWND>(l_param) != hwnd_) {
        is_dragging_scrollbar_ = false;
      }
      return 0;
    case WM_LBUTTONDBLCLK:
    case WM_LBUTTONDOWN: {
      POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
      
      if (active_field_ != FieldId::None && !IsHotkeyField(active_field_)) {
        D2DWindowlessTextBox* target = (active_field_ == FieldId::Blacklist || active_field_ == FieldId::CustomBlacklist) ? &multi_line_textbox_ : &single_line_textbox_;
        if (target->HandleMouseInput(hwnd_, message, D2D1::Point2F(static_cast<float>(point.x), static_cast<float>(point.y)))) {
          InvalidateRect(hwnd_, nullptr, FALSE);
          return 0;
        }
      }
      
      if (message == WM_LBUTTONDBLCLK) {
          return 0;
      }

      if (max_scroll_y_ > 0.0f) {
        RECT client;
        GetClientRect(hwnd_, &client);
        float view_h = static_cast<float>(content_rect_.bottom - content_rect_.top);
        float total_h = view_h + max_scroll_y_;
        float thumb_h = std::max(20.0f, view_h * (view_h / total_h));
        float thumb_y = static_cast<float>(content_rect_.top) + (scroll_y_ / max_scroll_y_) * (view_h - thumb_h);
        float thumb_x = static_cast<float>(client.right) - 5.0f;
        
        if (static_cast<float>(point.x) >= thumb_x - 4.0f && static_cast<float>(point.x) <= thumb_x + 8.0f &&
            static_cast<float>(point.y) >= thumb_y && static_cast<float>(point.y) <= thumb_y + thumb_h) {
          is_dragging_scrollbar_ = true;
          drag_start_y_ = point.y;
          drag_start_scroll_y_ = scroll_y_;
          SetCapture(hwnd_);
          return 0;
        }
      }

      const HitRegion* region = HitTest(point);
      if (region != nullptr) {
        if (region->kind == HitKind::SliderDrag) {
          is_dragging_slider_ = true;
          active_slider_index_ = region->option;
          SetCapture(hwnd_);
          
          float width = static_cast<float>(region->rect.right - region->rect.left);
          float cx = static_cast<float>(region->rect.left);
          float percent = (static_cast<float>(point.x) - cx) / width;
          percent = std::clamp(percent, 0.0f, 1.0f);
          if (active_slider_index_ == 18) { // window_blur_opacity
             draft_.window_blur_opacity = static_cast<int>(percent * 100.0f);
          }
          InvalidateRect(hwnd_, nullptr, FALSE);
        } else {
          HandlePrimaryAction(region->kind, region->kind == HitKind::Field ? static_cast<int>(region->field) : region->option);
        }
      } else {
        CloseSelectField();
        EndEditing(true);
      }
      return 0;
    }
    case WM_LBUTTONUP: {
      POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
      if (active_field_ != FieldId::None && !IsHotkeyField(active_field_)) {
        D2DWindowlessTextBox* target = (active_field_ == FieldId::Blacklist || active_field_ == FieldId::CustomBlacklist) ? &multi_line_textbox_ : &single_line_textbox_;
        if (target->HandleMouseInput(hwnd_, message, D2D1::Point2F(static_cast<float>(point.x), static_cast<float>(point.y)))) {
          return 0;
        }
      }

      if (is_dragging_scrollbar_) {
        is_dragging_scrollbar_ = false;
        ReleaseCapture();
        return 0;
      }
      
      if (is_dragging_slider_) {
        is_dragging_slider_ = false;
        ReleaseCapture();
        return 0;
      }
      break;
    }
    case WM_SYSKEYDOWN:
    case WM_KEYDOWN:
    case WM_CHAR:
    case WM_IME_SETCONTEXT:
    case WM_IME_STARTCOMPOSITION:
    case WM_IME_COMPOSITION:
    case WM_IME_ENDCOMPOSITION:
    case WM_IME_NOTIFY:
    case WM_INPUTLANGCHANGE: {
      if (active_field_ != FieldId::None && !IsHotkeyField(active_field_)) {
        D2DWindowlessTextBox* target = (active_field_ == FieldId::Blacklist || active_field_ == FieldId::CustomBlacklist) ? &multi_line_textbox_ : &single_line_textbox_;
        if (message == WM_KEYDOWN) {
          if (w_param == VK_ESCAPE) {
             EndEditing(true);
             return 0;
          }
          if (w_param == VK_TAB) {
             SyncActiveEditToState();
             FocusNextField((GetKeyState(VK_SHIFT) & 0x8000) != 0);
             return 0;
          }
          if (w_param == VK_RETURN && !target->IsMultiLine()) {
             EndEditing(true);
             return 0;
          }
        }
        if (target->HandleMessage(hwnd_, message, w_param, l_param)) {
          if (message == WM_KEYDOWN || message == WM_CHAR || message == WM_IME_COMPOSITION || message == WM_IME_ENDCOMPOSITION) {
            SyncActiveEditToState();
            InvalidateRect(hwnd_, nullptr, FALSE);
          }
          return 0;
        }
      }
      if (message != WM_KEYDOWN && message != WM_SYSKEYDOWN) {
          break;
      }
      if (IsHotkeyField(active_field_)) {
        if (w_param == VK_ESCAPE) {
          EndEditing(false);
          return 0;
        }
        CommitCapturedHotkey(static_cast<UINT>(w_param));
        return 0;
      }
      if (active_select_field_ != SelectField::None) {
        switch (w_param) {
          case VK_ESCAPE:
            CloseSelectField();
            return 0;
          case VK_UP:
          case VK_LEFT: {
            const int option_count = GetSelectOptionCount(active_select_field_);
            if (option_count > 0) {
              active_select_hover_index_ = active_select_hover_index_ <= 0 ? option_count - 1 : active_select_hover_index_ - 1;
              InvalidateRect(hwnd_, &select_menu_rect_, FALSE);
            }
            return 0;
          }
          case VK_DOWN:
          case VK_RIGHT: {
            const int option_count = GetSelectOptionCount(active_select_field_);
            if (option_count > 0) {
              active_select_hover_index_ = active_select_hover_index_ < 0
                  ? GetSelectSelectedIndex(active_select_field_)
                  : (active_select_hover_index_ + 1) % option_count;
              InvalidateRect(hwnd_, &select_menu_rect_, FALSE);
            }
            return 0;
          }
          case VK_RETURN:
          case VK_SPACE:
            SetSelectOption(
                active_select_field_,
                active_select_hover_index_ >= 0 ? active_select_hover_index_ : GetSelectSelectedIndex(active_select_field_));
            CloseSelectField();
            return 0;
        }
      }
      if (w_param == VK_ESCAPE) {
        HandleCloseRequest();
        return 0;
      }
      if (w_param == VK_TAB && active_field_ == FieldId::None) {
        BeginEditing(FieldId::Search);
        return 0;
      }
      if ((GetKeyState(VK_CONTROL) & 0x8000) != 0 && w_param == 'F') {
        BeginEditing(FieldId::Search);
        return 0;
      }
      if (w_param == VK_RETURN && active_field_ == FieldId::None) {
        HandlePrimaryAction(HitKind::Save, 0);
        return 0;
      }
    }
    break;
    case kProxyTabMessage:
      SyncActiveEditToState();
      FocusNextField(w_param != 0);
      return 0;
    case kProxyEscapeMessage:
      EndEditing(true);
      return 0;
    case kProxyCommitMessage:
      EndEditing(true);
      return 0;
    case WM_CLOSE:
      HandleCloseRequest();
      return 0;
    case WM_DESTROY:
      hwnd_ = nullptr;
      return 0;
  }
  return DefWindowProcW(hwnd_, message, w_param, l_param);
}


bool SettingsWindow::IsHotkeyField(FieldId field) const {
  return field == FieldId::Hotkey ||
      field == FieldId::QuickPasteHotkey ||
      field == FieldId::SearchFocusHotkey ||
      field == FieldId::ThirdPartyStickerHotkey ||
      field == FieldId::ScreenshotHotkey;
}

void SettingsWindow::CommitCapturedHotkey(UINT virtual_key) {
  if (!IsHotkeyField(active_field_) || IsModifierVirtualKey(virtual_key)) {
    return;
  }

  const UINT modifiers = platform::win32::GetPressedHotkeyModifiers();
  if (active_field_ == FieldId::Hotkey && modifiers == 0) {
    SetStatus(L"\u9762\u677f\u70ed\u952e\u81f3\u5c11\u9700\u8981\u4e00\u4e2a\u4fee\u9970\u952e", true);
    return;
  }
  if (active_field_ == FieldId::QuickPasteHotkey && modifiers == 0) {
    SetStatus(L"\u5feb\u6377\u7c98\u8d34\u70ed\u952e\u81f3\u5c11\u9700\u8981\u4e00\u4e2a\u4fee\u9970\u952e", true);
    return;
  }
  if (active_field_ == FieldId::ScreenshotHotkey && modifiers == 0) {
    SetStatus(L"截图快捷键至少需要一个修饰键", true);
    return;
  }

  const std::wstring hotkey_text = platform::win32::FormatHotkeyText(modifiers, virtual_key);
  if (hotkey_text.empty()) {
    SetStatus(L"\u65e0\u6cd5\u8bc6\u522b\u8be5\u5feb\u6377\u952e", true);
    return;
  }

  if (active_field_ == FieldId::Hotkey) {
    draft_.hotkey = hotkey_text;
  } else if (active_field_ == FieldId::QuickPasteHotkey) {
    draft_.quick_paste_hotkey = hotkey_text;
  } else if (active_field_ == FieldId::SearchFocusHotkey) {
    draft_.search_focus_hotkey = hotkey_text;
  } else if (active_field_ == FieldId::ThirdPartyStickerHotkey) {
    draft_.third_party_sticker_hotkey = hotkey_text;
  } else if (active_field_ == FieldId::ScreenshotHotkey) {
    draft_.screenshot_hotkey = hotkey_text;
  }

  EndEditing(true);
}

}  // namespace clipd2d::ui

