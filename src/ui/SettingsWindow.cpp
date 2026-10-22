#include "ui/SettingsWindow.h"
#include "ui/PanelWindowChrome.h"
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

#include "common/AppIcon.h"
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

// 搜索结果下拉的可见行数上限（超出时窗口随 ↑↓ 滚动）。
constexpr int kMaxVisibleSearchHits = 6;

// B1 导航分组：Section 索引 -> 组号；rail 模式下组间只留间距（无组标签）。
constexpr int kNavGroupOf[7] = {0, 0, 1, 1, 1, 2, 2};  // 常规/外观 | 快捷键/剪贴板/过滤 | 数据/关于

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
  window_class.hIcon = clipd2d::common::LoadAppIcon();
  window_class.hIconSm = clipd2d::common::LoadAppIcon();
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
  ApplyDarkModeAttribute(hwnd_, clipd2d::ui::IsDarkThemeActive());
  hover_hint_.Create(instance_, hwnd_);

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
  CloseSearchResults();
  pending_scroll_title_.clear();
  confirm_discard_ = false;
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
  hover_hint_.Destroy();
  d2d_shell_.Shutdown();
  if (hwnd_ != nullptr) {
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
}

void SettingsWindow::ApplyThemeToTextboxes() {
  const D2D1_COLOR_F text_color = d2d_shell_.ColorText();
  const D2D1_COLOR_F placeholder_color = d2d_shell_.ColorSubtleText();
  single_line_textbox_.SetTextColor(text_color);
  single_line_textbox_.SetPlaceholderColor(placeholder_color);
  multi_line_textbox_.SetTextColor(text_color);
  multi_line_textbox_.SetPlaceholderColor(placeholder_color);
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

  ApplyThemeToTextboxes();

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
  settings.theme = draft_.theme;
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

bool SettingsWindow::IsSectionDirty(Section section, const core::store::AppSettings& next) const {
  const core::store::AppSettings& cur = current_settings_;
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

unsigned SettingsWindow::ComputeDirtyMask() const {
  const core::store::AppSettings next = BuildSettingsFromDraft();
  unsigned mask = 0;
  for (int index = 0; index < 7; ++index) {
    if (IsSectionDirty(static_cast<Section>(index), next)) {
      mask |= (1u << index);
    }
  }
  return mask;
}

bool SettingsWindow::IsDraftDirty() const {
  return ComputeDirtyMask() != 0;
}

void SettingsWindow::ResetSectionToDefaults(Section section) {
  EndEditing(true);
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

// ── 跨节搜索：行级索引 / 预览 / 跳转 ─────────────────────────────────

void SettingsWindow::RebuildSearchHits() {
  search_hits_.clear();
  search_hit_rects_.clear();
  search_hover_index_ = -1;
  search_window_offset_ = 0;
  SetRectEmpty(&search_results_rect_);

  const std::wstring query = common::TrimWide(search_text_);
  if (query.empty()) {
    return;
  }

  for (size_t index = 0; index < panels_.size(); ++index) {
    ISettingsPanel* panel = panels_[index].get();
    if (panel == nullptr) {
      continue;
    }
    const std::vector<SettingRow> rows = panel->BuildRows(draft_, this);
    for (const SettingRow& row : rows) {
      if (row.title_text == nullptr || row.title_text[0] == L'\0') {
        continue;
      }
      if (!SettingRowMatches(row, query)) {
        continue;
      }
      SearchHit hit;
      hit.section = static_cast<Section>(index);
      hit.title = row.title_text;
      hit.subtitle = row.subtitle_text != nullptr ? row.subtitle_text : L"";
      hit.hit_kind = row.hit_kind;
      hit.field_id = row.field_id;
      hit.select_field = row.select_field;
      hit.hit_option = row.hit_option;
      search_hits_.push_back(std::move(hit));
    }
  }

  if (search_hits_.empty()) {
    // 没有行级命中时退回章节关键词匹配，「外观」「快捷键」这类节名仍然可用。
    UpdateSearchSelection();
    return;
  }
  search_hover_index_ = 0;
  PreviewSearchHit(0);
}

void SettingsWindow::PreviewSearchHit(int index) {
  if (index < 0 || index >= static_cast<int>(search_hits_.size())) {
    return;
  }
  const SearchHit& hit = search_hits_[static_cast<size_t>(index)];
  selected_section_ = hit.section;
  pending_scroll_title_ = hit.title;
  pending_scroll_section_ = hit.section;
}

void SettingsWindow::ActivateSearchHit(int index) {
  if (index < 0 || index >= static_cast<int>(search_hits_.size())) {
    return;
  }
  const SearchHit hit = search_hits_[static_cast<size_t>(index)];
  // 跳转即消费查询词：清空搜索框，否则目标节会被继续过滤成「只剩一行」甚至空白。
  single_line_textbox_.SetText(L"");
  search_text_.clear();
  CloseSearchResults();
  selected_section_ = hit.section;
  pending_scroll_title_ = hit.title;
  pending_scroll_section_ = hit.section;
  EndEditing(true);
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::MoveSearchHover(int delta) {
  if (search_hits_.empty()) {
    return;
  }
  const int total = static_cast<int>(search_hits_.size());
  int next = search_hover_index_ < 0 ? 0 : search_hover_index_ + delta;
  if (next < 0) {
    next = total - 1;
  }
  if (next >= total) {
    next = 0;
  }
  search_hover_index_ = next;
  if (next < search_window_offset_) {
    search_window_offset_ = next;
  }
  if (next >= search_window_offset_ + kMaxVisibleSearchHits) {
    search_window_offset_ = next - kMaxVisibleSearchHits + 1;
  }
  PreviewSearchHit(next);
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void SettingsWindow::CloseSearchResults() {
  search_hits_.clear();
  search_hit_rects_.clear();
  search_hover_index_ = -1;
  search_window_offset_ = 0;
  SetRectEmpty(&search_results_rect_);
}

void SettingsWindow::UpdateSearchResultsLayout() {
  search_hit_rects_.clear();
  SetRectEmpty(&search_results_rect_);
  // 有查询词但无行级命中时仍显示下拉（无匹配提示），不再把主页面过滤成空态。
  const bool no_results =
      search_hits_.empty() && !common::TrimWide(search_text_).empty();
  if (active_field_ != FieldId::Search || (search_hits_.empty() && !no_results)) {
    return;
  }

  RECT client{};
  GetClientRect(hwnd_, &client);
  const int outer = DipToPx(24.0f);
  const int header_h = DipToPx(26.0f);
  const int item_h = DipToPx(42.0f);
  const int footer_h = DipToPx(22.0f);
  const int total = static_cast<int>(search_hits_.size());
  const int visible = no_results ? 1 : std::min(kMaxVisibleSearchHits, total);
  const int left = search_rect_.left;
  const int width = std::max(DipToPx(220.0f), std::min(DipToPx(360.0f), static_cast<int>(client.right) - outer - left));
  const int top = search_rect_.bottom + DipToPx(6.0f);
  const bool has_more = !no_results && total > visible;
  const int height = header_h + visible * item_h + (has_more ? footer_h : 0);
  search_results_rect_ = {left, top, left + width, top + height};

  int y = top + header_h;
  for (int slot = 0; slot < visible; ++slot) {
    search_hit_rects_.push_back({left + DipToPx(6.0f), y, left + width - DipToPx(6.0f), y + item_h});
    y += item_h;
  }
}

int SettingsWindow::SearchHitAtPoint(POINT point) const {
  for (size_t slot = 0; slot < search_hit_rects_.size(); ++slot) {
    RECT rect = search_hit_rects_[slot];
    if (PtInRect(&rect, point)) {
      return search_window_offset_ + static_cast<int>(slot);
    }
  }
  return -1;
}

void SettingsWindow::RenderSearchResults(ID2D1HwndRenderTarget* target) {
  UpdateSearchResultsLayout();
  if (search_results_rect_.right <= search_results_rect_.left) {
    return;
  }

  FillRoundedPanel(target, search_results_rect_, 12.0f, d2d_shell_.surface_brush(), d2d_shell_.border_brush(), 0.5f);
  // 面板空白区吞掉点击，避免穿透到下面的设置行（命中后保持输入焦点）。
  AddHitRegion(clipd2d::ui::HitRegion{search_results_rect_, HitKind::None, selected_section_, FieldId::None, 0});

  const bool no_results =
      search_hits_.empty() && !common::TrimWide(search_text_).empty();
  if (no_results) {
    RECT empty_title = search_results_rect_;
    empty_title.left += DipToPx(12.0f);
    empty_title.right -= DipToPx(12.0f);
    empty_title.top += DipToPx(5.0f);
    empty_title.bottom = empty_title.top + DipToPx(18.0f);
    DrawTextLine(target, d2d_shell_.small_format(), d2d_shell_.muted_text_brush(),
                 empty_title, L"\u65e0\u5339\u914d\u7684\u8bbe\u7f6e\u9879");
    RECT empty_hint = search_hit_rects_.front();
    empty_hint.left += DipToPx(10.0f);
    empty_hint.right -= DipToPx(10.0f);
    DrawTextLine(target, d2d_shell_.small_format(), d2d_shell_.subtle_text_brush(),
                 empty_hint, L"\u6362\u4e2a\u66f4\u77ed\u7684\u5173\u952e\u8bcd\u8bd5\u8bd5", DWRITE_TEXT_ALIGNMENT_CENTER);
    return;
  }

  const std::wstring header_text =
      L"\u5171 " + std::to_wstring(search_hits_.size()) + L" \u9879\u5339\u914d";
  RECT header_rect = search_results_rect_;
  header_rect.left += DipToPx(12.0f);
  header_rect.right -= DipToPx(12.0f);
  header_rect.top += DipToPx(5.0f);
  header_rect.bottom = header_rect.top + DipToPx(18.0f);
  DrawTextLine(target, d2d_shell_.small_format(), d2d_shell_.subtle_text_brush(), header_rect, header_text);

  for (size_t slot = 0; slot < search_hit_rects_.size(); ++slot) {
    const int index = search_window_offset_ + static_cast<int>(slot);
    if (index >= static_cast<int>(search_hits_.size())) {
      break;
    }
    const SearchHit& hit = search_hits_[static_cast<size_t>(index)];
    const bool active = index == search_hover_index_;
    RECT item = search_hit_rects_[slot];
    if (active) {
      FillRoundedPanel(target, item, 8.0f, d2d_shell_.accent_soft_brush(), nullptr, 0.0f);
    }
    RECT title_rect = item;
    title_rect.left += DipToPx(10.0f);
    title_rect.right -= DipToPx(76.0f);
    DrawTextLine(target,
                 d2d_shell_.small_format(),
                 active ? d2d_shell_.accent_brush() : d2d_shell_.text_brush(),
                 title_rect,
                 hit.title);
    RECT section_rect = item;
    section_rect.left = item.right - DipToPx(72.0f);
    section_rect.right = item.right - DipToPx(10.0f);
    DrawTextLine(target,
                 d2d_shell_.small_format(),
                 d2d_shell_.subtle_text_brush(),
                 section_rect,
                 SectionTitle(hit.section),
                 DWRITE_TEXT_ALIGNMENT_TRAILING);
    AddHitRegion(clipd2d::ui::HitRegion{item, HitKind::SearchResult, hit.section, hit.field_id, index});
  }

  if (static_cast<int>(search_hits_.size()) > kMaxVisibleSearchHits) {
    RECT footer_rect = search_results_rect_;
    footer_rect.left += DipToPx(12.0f);
    footer_rect.right -= DipToPx(12.0f);
    footer_rect.top = footer_rect.bottom - DipToPx(21.0f);
    footer_rect.bottom -= DipToPx(4.0f);
    DrawTextLine(target,
                 d2d_shell_.small_format(),
                 d2d_shell_.subtle_text_brush(),
                 footer_rect,
                 L"\u2191\u2193 \u9009\u62e9\uff0cEnter \u8df3\u8f6c");
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
    case SelectField::Theme:
      return 3;
    case SelectField::None:
      break;
  }
  return 0;
}

int SettingsWindow::GetSelectSelectedIndex(SelectField field) const {
  switch (field) {
    case SelectField::Theme:
      return draft_.theme == core::store::ThemeMode::System ? 0
          : (draft_.theme == core::store::ThemeMode::Light ? 1 : 2);
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
    case SelectField::Theme:
      return draft_.theme == core::store::ThemeMode::System ? L"跟随系统"
          : (draft_.theme == core::store::ThemeMode::Light ? L"浅色" : L"深色");
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
    case SelectField::Theme:
      if (option == 0) return L"跟随系统";
      if (option == 1) return L"浅色";
      return L"深色";
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
    case SelectField::Theme:
      if (option == 0) draft_.theme = core::store::ThemeMode::System;
      else if (option == 1) draft_.theme = core::store::ThemeMode::Light;
      else draft_.theme = core::store::ThemeMode::Dark;
      break;
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
      PreviewAppearanceChanges();
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
      RebuildSearchHits();
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
  if (kind != HitKind::Field && kind != HitKind::SearchResult && kind != HitKind::None) {
    EndEditing(true);
  }
  if (kind != HitKind::SelectTrigger && kind != HitKind::SelectOption) {
    CloseSelectField();
  }

  switch (kind) {
    case HitKind::SearchResult:
      ActivateSearchHit(option);
      return;
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
    case HitKind::ActionResetSection:
      ResetSectionToDefaults(static_cast<Section>(option));
      SetStatus(L"\u5df2\u6062\u590d\u672c\u8282\u9ed8\u8ba4\uff0c\u70b9\u51fb\u4fdd\u5b58\u751f\u6548", false);
      break;
    case HitKind::ConfirmDiscard:
      confirm_discard_ = false;
      CloseWindowDiscardingChanges();
      return;
    case HitKind::ConfirmKeep:
      confirm_discard_ = false;
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
  if (IsDraftDirty() && !confirm_discard_) {
    // A3：有未保存修改时改用窗内确认浮层，不再弹系统 MessageBox。
    confirm_discard_ = true;
    InvalidateRect(hwnd_, nullptr, FALSE);
    return;
  }
  confirm_discard_ = false;
  CloseWindowDiscardingChanges();
}

void SettingsWindow::CloseWindowDiscardingChanges() {
  ClearStatus();
  search_text_.clear();
  CloseSearchResults();
  pending_scroll_title_.clear();
  confirm_discard_ = false;
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
  confirm_discard_ = true;
  InvalidateRect(hwnd_, nullptr, FALSE);
  return false;
}

void SettingsWindow::RenderConfirmDiscardOverlay(ID2D1HwndRenderTarget* target) {
  if (!confirm_discard_) {
    return;
  }
  RECT client{};
  GetClientRect(hwnd_, &client);
  ID2D1SolidColorBrush* mask_brush = d2d_shell_.solid_brush(target, D2D1::ColorF(0x000000, 0.35f));
  if (mask_brush != nullptr) {
    target->FillRectangle(D2D1::RectF(0.0f, 0.0f, static_cast<FLOAT>(client.right),
                                      static_cast<FLOAT>(client.bottom)),
                          mask_brush);
  }
  // 遮罩先吃掉全部点击（后添加的命中区域优先），按钮区域再覆盖其上。
  AddHitRegion(clipd2d::ui::HitRegion{{0, 0, client.right, client.bottom}, HitKind::None,
                                      selected_section_, FieldId::None, 0});

  const int card_w = DipToPx(360.0f);
  const int card_h = DipToPx(152.0f);
  const int left = (client.right - card_w) / 2;
  const int top = (client.bottom - card_h) / 2 - DipToPx(12.0f);
  confirm_card_rect_ = {left, top, left + card_w, top + card_h};
  FillRoundedPanel(target, confirm_card_rect_, 14.0f, d2d_shell_.surface_brush(),
                   d2d_shell_.border_brush(), 0.5f);

  const RECT title_rect = {left + DipToPx(20.0f), top + DipToPx(18.0f), left + card_w - DipToPx(20.0f),
                           top + DipToPx(40.0f)};
  DrawTextLine(target, d2d_shell_.body_bold_format(), d2d_shell_.text_brush(), title_rect,
               L"\u653e\u5f03\u672a\u4fdd\u5b58\u7684\u4fee\u6539\uff1f");
  const RECT text_rect = {left + DipToPx(20.0f), top + DipToPx(44.0f), left + card_w - DipToPx(20.0f),
                          top + DipToPx(66.0f)};
  DrawTextLine(target, d2d_shell_.small_format(), d2d_shell_.muted_text_brush(), text_rect,
               L"\u5173\u95ed\u540e\u5c06\u4e22\u5931\u672c\u6b21\u6253\u5f00\u4ee5\u6765\u7684\u5168\u90e8\u6539\u52a8\u3002");

  confirm_discard_rect_ = {left + DipToPx(20.0f), top + card_h - DipToPx(52.0f),
                           left + DipToPx(20.0f) + DipToPx(96.0f), top + card_h - DipToPx(16.0f)};
  confirm_keep_rect_ = {left + card_w - DipToPx(20.0f) - DipToPx(96.0f), top + card_h - DipToPx(52.0f),
                        left + card_w - DipToPx(20.0f), top + card_h - DipToPx(16.0f)};
  FillRoundedPanel(target, confirm_discard_rect_, 6.0f, d2d_shell_.surface_alt_brush(),
                   d2d_shell_.danger_brush(), 0.5f);
  FillRoundedPanel(target, confirm_keep_rect_, 6.0f, d2d_shell_.accent_brush(),
                   d2d_shell_.accent_brush(), 0.5f);
  DrawTextLine(target, d2d_shell_.small_format(), d2d_shell_.danger_brush(), confirm_discard_rect_,
               L"\u653e\u5f03\u66f4\u6539", DWRITE_TEXT_ALIGNMENT_CENTER);
  DrawTextLine(target, d2d_shell_.small_format(), d2d_shell_.white_brush(), confirm_keep_rect_,
               L"\u7ee7\u7eed\u7f16\u8f91", DWRITE_TEXT_ALIGNMENT_CENTER);
  AddHitRegion(clipd2d::ui::HitRegion{confirm_discard_rect_, HitKind::ConfirmDiscard});
  AddHitRegion(clipd2d::ui::HitRegion{confirm_keep_rect_, HitKind::ConfirmKeep});
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
  // C2 rail 导航：宽度可拖拽（默认 56dip 图标列），节名靠悬停提示。
  const int nav_width = DipToPx(nav_width_dip_);
  const int search_height = DipToPx(36.0f);
  const int nav_item_height = DipToPx(40.0f);

  RECT content = {
      outer + nav_width + DipToPx(16.0f), DipToPx(16.0f) + search_height + DipToPx(12.0f),
      client.right - outer, client.bottom - DipToPx(46.0f)};

  // B2 内容列限宽居中：宽窗口下设置列不至于拉得太开。
  {
    const int available = content.right - content.left;
    const int max_content_w = DipToPx(560.0f);
    if (available > max_content_w) {
      const int centered_left = content.left + (available - max_content_w) / 2;
      content = {centered_left, content.top, centered_left + max_content_w,
                       content.bottom};
    }
  }
  search_rect_ = {content.left, DipToPx(16.0f), content.right, DipToPx(16.0f) + search_height};
  content_rect_ = content;

  const int pill_width = DipToPx(320.0f);
  const int pill_height = DipToPx(44.0f);
  bottom_bar_rect_ = {
      client.right - outer - pill_width, client.bottom - DipToPx(1.0f) - pill_height, client.right - outer, client.bottom - DipToPx(1.0f)};

  save_button_rect_ = {bottom_bar_rect_.right - DipToPx(90.0f), bottom_bar_rect_.top + DipToPx(8.0f), bottom_bar_rect_.right - DipToPx(8.0f), bottom_bar_rect_.bottom - DipToPx(8.0f)};
  cancel_button_rect_ = {save_button_rect_.left - DipToPx(90.0f), save_button_rect_.top, save_button_rect_.left - DipToPx(8.0f), save_button_rect_.bottom};
  status_rect_ = {bottom_bar_rect_.left + DipToPx(16.0f), bottom_bar_rect_.top, cancel_button_rect_.left - DipToPx(16.0f), bottom_bar_rect_.bottom};

  // C2 rail 导航：组间只留小间距（无组标签行）。宽 rail 顶部要给 logo 字标留位。
  int nav_top = outer + DipToPx(NavWide() ? 24.0f : 4.0f);
  int last_group = -1;
  for (size_t index = 0; index < nav_rects_.size(); ++index) {
    const int group = kNavGroupOf[index];
    if (group != last_group) {
      if (last_group != -1) {
        nav_top += DipToPx(10.0f);
      }
      last_group = group;
    }
    nav_rects_[index] = {outer, nav_top, outer + nav_width, nav_top + nav_item_height};
    nav_top += nav_item_height + DipToPx(4.0f);
  }
}

void SettingsWindow::RenderFrame() {
  // C1 暗色模式：主题纪元变化时重建刷子，并同步文本框颜色与 DWM 暗色属性。
  if (d2d_shell_.SyncTheme()) {
    ApplyThemeToTextboxes();
    ApplyDarkModeAttribute(hwnd_, clipd2d::ui::IsDarkThemeActive());
  }
  ID2D1HwndRenderTarget* target = d2d_shell_.BeginFrame();
  if (target == nullptr) {
    return;
  }

  target->Clear(d2d_shell_.ColorBackground());
  hit_regions_.clear();
  dirty_mask_ = ComputeDirtyMask();
select_option_rects_.fill(RECT{});
  SetRectEmpty(&select_menu_rect_);

  RECT client{};
  GetClientRect(hwnd_, &client);
  const int outer = DipToPx(24.0f);
  const int nav_width = DipToPx(nav_width_dip_);  // rail 宽度可拖拽

  const float divider_x = static_cast<float>(outer + nav_width + DipToPx(8.0f));
  target->DrawLine(
      D2D1::Point2F(divider_x, 0.0f),
      D2D1::Point2F(divider_x, static_cast<float>(client.bottom)),
      d2d_shell_.border_brush(),
      0.5f);
  RenderNavSplitter(target);

  // rail 顶部 logo：窄 rail 只画圆点占位；宽 rail 圆点左对齐 +「Scraps」字标。
  const int logo_dot = DipToPx(10.0f);
  const int logo_left = NavWide() ? outer + DipToPx(12.0f) : outer + (nav_width - logo_dot) / 2;
  const RECT logo_rect = {logo_left, outer, logo_left + logo_dot, outer + logo_dot};
  ID2D1SolidColorBrush* logo_brush = d2d_shell_.solid_brush(target, d2d_shell_.accent_brush()->GetColor());
  if (logo_brush != nullptr) {
    target->FillEllipse(
        D2D1::Ellipse(
            D2D1::Point2F(
                static_cast<FLOAT>(logo_rect.left + logo_rect.right) / 2.0f,
                static_cast<FLOAT>(logo_rect.top + logo_rect.bottom) / 2.0f),
            static_cast<FLOAT>(logo_dot) / 2.0f, static_cast<FLOAT>(logo_dot) / 2.0f),
        logo_brush);
    if (NavWide()) {
      const RECT wordmark_rect = {
          logo_rect.right + DipToPx(8.0f),
          outer - DipToPx(2.0f),
          outer + nav_width - DipToPx(4.0f),
          outer + DipToPx(20.0f)};
      DrawTextLine(
          target,
          d2d_shell_.badge_format(),
          d2d_shell_.text_brush(),
          wordmark_rect,
          L"Scraps",
          DWRITE_TEXT_ALIGNMENT_LEADING);
    }
    AddHitRegion(clipd2d::ui::HitRegion{logo_rect, HitKind::None, selected_section_, FieldId::None, 0});
  }

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
  // 内容裁剪到视口：滚动后行不得盖过搜索框/底栏（叠加层在裁剪外绘制）。
  target->PushAxisAlignedClip(
      D2D1::RectF(
          static_cast<float>(content_rect_.left), static_cast<float>(content_rect_.top),
          static_cast<float>(content_rect_.right), static_cast<float>(content_rect_.bottom)),
      D2D1_ANTIALIAS_MODE_ALIASED);
  RenderContent(target);
  target->PopAxisAlignedClip();
  RenderScrollbar(target);
  FlushStickyTitles(target);
  
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
  // 搜索结果最后画：作为浮层压在内容之上，命中区域也因此优先于下面的行。
  RebuildFocusableRegions();
  DrawKeyboardFocusRing(target);
  RenderSearchResults(target);
  RenderConfirmDiscardOverlay(target);
  if (d2d_shell_.EndFrame()) {
    InvalidateRect(hwnd_, nullptr, FALSE);
  }
}

void SettingsWindow::PreviewAppearanceChanges() {
  // C3：毛玻璃类型/透明度草稿值实时套用（仅预览，不落库）。
  if (host_actions_.on_preview_appearance == nullptr) {
    return;
  }
  core::store::AppSettings preview = current_settings_;
  preview.window_blur_type = draft_.window_blur_type;
  preview.window_blur_opacity = draft_.window_blur_opacity;
  host_actions_.on_preview_appearance(preview);
}

void SettingsWindow::UpdateNavHoverHint(int nav_hover, POINT client_point) {
  // 宽 rail 已显示节名文字，悬停提示仅窄 rail 需要。
  if (NavWide()) {
    if (nav_hover_hint_index_ != -1) {
      nav_hover_hint_index_ = -1;
      hover_hint_.Hide();
    }
    return;
  }
  // C2 窄 rail：图标不足以自述，悬停时在指针旁显示节名。
  if (nav_hover < 0 || nav_hover >= static_cast<int>(nav_rects_.size())) {
    if (nav_hover_hint_index_ != -1) {
      nav_hover_hint_index_ = -1;
      hover_hint_.Hide();
    }
    return;
  }
  if (nav_hover == nav_hover_hint_index_) {
    return;
  }
  nav_hover_hint_index_ = nav_hover;
  POINT screen = client_point;
  ClientToScreen(hwnd_, &screen);
  hover_hint_.ShowText(
      SectionTitle(static_cast<Section>(nav_hover)),
      POINT{screen.x + DipToPx(10.0f), screen.y + DipToPx(14.0f)});
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

    // 宽 rail：图标左对齐 + 节名文字；窄 rail：图标居中，节名靠悬停提示。
    const int icon_size = DipToPx(16.0f);
    const int item_height = nav_rects_[index].bottom - nav_rects_[index].top;
    const int icon_left = NavWide()
        ? nav_rects_[index].left + DipToPx(12.0f)
        : nav_rects_[index].left + ((nav_rects_[index].right - nav_rects_[index].left) - icon_size) / 2;
    const int icon_top = nav_rects_[index].top + (item_height - icon_size) / 2;
    const RECT icon_rect = {icon_left, icon_top, icon_left + icon_size, icon_top + icon_size};
    DrawFluentIcon(
        d2d_shell_.d2d_factory(),
        target,
        SectionIcon(section),
        ToRectF(icon_rect),
        selected ? d2d_shell_.white_brush() : d2d_shell_.text_brush(),
        selected ? 0.35f : 0.0f);
    if (NavWide()) {
      const RECT label_rect = {
          icon_rect.right + DipToPx(10.0f),
          nav_rects_[index].top + (item_height - DipToPx(18.0f)) / 2,
          nav_rects_[index].right - DipToPx(8.0f),
          nav_rects_[index].top + (item_height + DipToPx(18.0f)) / 2};
      DrawTextLine(
          target,
          d2d_shell_.body_format(),
          selected ? d2d_shell_.white_brush() : d2d_shell_.text_brush(),
          label_rect,
          SectionTitle(section),
          DWRITE_TEXT_ALIGNMENT_LEADING);
    }
    AddHitRegion(clipd2d::ui::HitRegion{nav_rects_[index], HitKind::Nav, section, FieldId::None, static_cast<int>(index)});

    // A2 脏标记：该节有未保存修改时在导航项右上角画一个圆点。
    if ((dirty_mask_ & (1u << index)) != 0) {
      const int dot = DipToPx(6.0f);
      const RECT dot_rect = {
          nav_rects_[index].right - DipToPx(10.0f),
          nav_rects_[index].top + DipToPx(6.0f),
          nav_rects_[index].right - DipToPx(10.0f) + dot,
          nav_rects_[index].top + DipToPx(6.0f) + dot};
      const D2D1_RECT_F dot_rf = D2D1::RectF(
          static_cast<FLOAT>(dot_rect.left), static_cast<FLOAT>(dot_rect.top),
          static_cast<FLOAT>(dot_rect.right), static_cast<FLOAT>(dot_rect.bottom));
      ID2D1SolidColorBrush* dot_brush = d2d_shell_.solid_brush(
          target, selected ? D2D1::ColorF(0xFFFFFF) : D2D1::ColorF(0xF59E0B));
      if (dot_brush != nullptr) {
        target->FillEllipse(
            D2D1::Ellipse(D2D1::Point2F(
                              (static_cast<FLOAT>(dot_rect.left) + static_cast<FLOAT>(dot_rect.right)) / 2.0f,
                              (static_cast<FLOAT>(dot_rect.top) + static_cast<FLOAT>(dot_rect.bottom)) / 2.0f),
                          static_cast<FLOAT>(dot) / 2.0f, static_cast<FLOAT>(dot) / 2.0f),
            dot_brush);
      }
    }
  }
}

void SettingsWindow::RenderContent(ID2D1HwndRenderTarget* target) {
  field_rects_.clear();
  select_rects_.clear();
  
  if (auto& panel = panels_[static_cast<int>(selected_section_)]) {
    max_scroll_y_ = panel->Render(target, draft_, this, content_rect_);
    // rail 拖宽等导致内容重排后，滚动位置不得越过新上界。
    if (scroll_y_ > max_scroll_y_) {
      scroll_y_ = max_scroll_y_;
    }

    // 搜索跳转：等本节布局真正建好后再取行坐标，避免对空布局测量。
    if (!pending_scroll_title_.empty() && pending_scroll_section_ == selected_section_) {
      const float row_top = panel->FindRowTopByTitle(pending_scroll_title_);
      pending_scroll_title_.clear();
      if (row_top >= 0.0f) {
        const float viewport = static_cast<float>(content_rect_.bottom - content_rect_.top);
        scroll_y_ = std::clamp(row_top - viewport * 0.32f, 0.0f, max_scroll_y_);
        InvalidateRect(hwnd_, nullptr, FALSE);
      }
    }
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
  // A2：无修改时保存按钮降为次要态（仍可点击，保存是幂等的）。
  const bool dirty = dirty_mask_ != 0;
  FillRoundedPanel(
      target,
      save_button_rect_,
      6.0f,
      dirty ? d2d_shell_.accent_brush() : d2d_shell_.surface_alt_brush(),
      dirty ? d2d_shell_.accent_brush() : d2d_shell_.border_brush(),
      0.5f);
  DrawTextLine(target, d2d_shell_.body_format(), d2d_shell_.text_brush(), cancel_button_rect_, L"\u53d6\u6d88", DWRITE_TEXT_ALIGNMENT_CENTER);
  DrawTextLine(target, d2d_shell_.body_format(),
               dirty ? d2d_shell_.white_brush() : d2d_shell_.muted_text_brush(),
               save_button_rect_, L"\u4fdd\u5b58", DWRITE_TEXT_ALIGNMENT_CENTER);
  AddHitRegion(clipd2d::ui::HitRegion{cancel_button_rect_, HitKind::Cancel});
  AddHitRegion(clipd2d::ui::HitRegion{save_button_rect_, HitKind::Save});

  DrawTextLine(
      target, d2d_shell_.small_format(), status_error_ ? d2d_shell_.danger_brush() : d2d_shell_.muted_text_brush(),
      status_rect_,
      status_text_.empty()
          ? (dirty_mask_ != 0 ? L"\u6709\u672a\u4fdd\u5b58\u7684\u4fee\u6539" : L"\u4fee\u6539\u540e\u70b9\u51fb\u4fdd\u5b58\u751f\u6548")
          : status_text_);
}

// A5 细滚动条：统一计算 thumb 矩形，命中测试/拖拽/绘制共用。
D2D1_RECT_F SettingsWindow::ScrollbarThumbRect() const {
  RECT client{};
  GetClientRect(hwnd_, &client);
  const float view_h = static_cast<float>(content_rect_.bottom - content_rect_.top);
  const float total_h = std::max(view_h + max_scroll_y_, 1.0f);
  const float track_h = std::max(0.0f, view_h - DipToPx(16.0f));
  const float thumb_h = std::max(28.0f, track_h * (view_h / total_h));
  const float progress = max_scroll_y_ > 0.0f ? (scroll_y_ / max_scroll_y_) : 0.0f;
  const float thumb_y = static_cast<float>(content_rect_.top) + DipToPx(8.0f) +
                        progress * std::max(0.0f, track_h - thumb_h);
  const float x = static_cast<float>(client.right) - DipToPx(5.0f);
  return D2D1::RectF(x - 1.5f, thumb_y, x + 1.5f, thumb_y + thumb_h);
}

void SettingsWindow::RenderScrollbar(ID2D1HwndRenderTarget* target) {
  if (max_scroll_y_ <= 0.0f) {
    return;
  }
  const D2D1_RECT_F thumb = ScrollbarThumbRect();
  // 拖块随主题：浅色黑系（历史值），深色白系——黑 16% 在深色底上不可见。
  const float alpha = is_dragging_scrollbar_ ? 0.45f : 0.22f;
  ID2D1SolidColorBrush* brush = d2d_shell_.solid_brush(
      target, IsDarkThemeActive() ? D2D1::ColorF(0xFFFFFF, alpha)
                                  : D2D1::ColorF(0x000000, is_dragging_scrollbar_ ? 0.35f : 0.16f));
  if (brush != nullptr) {
    target->FillRoundedRectangle(D2D1::RoundedRect(thumb, 1.5f, 1.5f), brush);
  }
}

// rail 分割栏拖拽把手：命中区是分割线两侧各 4px 的全高窄条。
RECT SettingsWindow::NavSplitterRect() const {
  RECT client{};
  GetClientRect(hwnd_, &client);
  const int outer = DipToPx(24.0f);
  const int divider_x = outer + DipToPx(nav_width_dip_) + DipToPx(8.0f);
  const int half = DipToPx(4.0f);
  return {divider_x - half, 0, divider_x + half, client.bottom};
}

void SettingsWindow::RenderNavSplitter(ID2D1HwndRenderTarget* target) {
  if (target == nullptr) {
    return;
  }
  const RECT zone = NavSplitterRect();
  const bool hot = nav_splitter_hover_ || is_dragging_nav_splitter_;
  if (!hot) {
    return;
  }
  // 悬停/拖拽时在分割线上叠一条 3px 强调条，提示可拖拽。
  const float cx = static_cast<float>(zone.left + zone.right) / 2.0f;
  const D2D1_RECT_F bar = D2D1::RectF(cx - 1.5f, 0.0f, cx + 1.5f, static_cast<float>(zone.bottom));
  ID2D1SolidColorBrush* brush = d2d_shell_.solid_brush(
      target, d2d_shell_.accent_brush()->GetColor());
  if (brush != nullptr) {
    target->FillRoundedRectangle(D2D1::RoundedRect(bar, 1.5f, 1.5f), brush);
  }
}

void SettingsWindow::QueueStickyTitle(const FlexNode* node, const wchar_t* text) {
  sticky_titles_.emplace_back(node, text);
}

// B3 粘性节标题：行绘制完成后统一画在最上层。
// 标题滚到视口顶后钉住并垫不透明底，直到下一节标题接管。
void SettingsWindow::FlushStickyTitles(ID2D1HwndRenderTarget* target) {
  if (sticky_titles_.empty()) {
    return;
  }
  const float cx = static_cast<float>(content_rect_.left);
  const float cy = static_cast<float>(content_rect_.top) - scroll_y_;
  const size_t count = sticky_titles_.size();
  for (size_t index = 0; index < count; ++index) {
    const FlexNode* node = sticky_titles_[index].first;
    const wchar_t* text = sticky_titles_[index].second;
    if (node == nullptr || text == nullptr || text[0] == L'\0') {
      continue;
    }
    RECT rect = node->GetAbsoluteRect(cx, cy);
    const float next_top = index + 1 < count
        ? static_cast<float>(sticky_titles_[index + 1].first->GetAbsoluteRect(cx, cy).top)
        : static_cast<float>(content_rect_.bottom);
    if (next_top <= static_cast<float>(content_rect_.top)) {
      continue;  // 下一节标题已抵达视口顶，本节滚出
    }
    const bool stuck = rect.top < content_rect_.top;
    if (stuck) {
      const int height = rect.bottom - rect.top;
      rect.top = content_rect_.top;
      rect.bottom = std::min(rect.top + height, static_cast<LONG>(next_top));
      if (rect.bottom <= rect.top) {
        continue;
      }
      ID2D1SolidColorBrush* bg = d2d_shell_.solid_brush(target, d2d_shell_.ColorBackground());
      if (bg != nullptr) {
        target->FillRectangle(
            D2D1::RectF(static_cast<FLOAT>(content_rect_.left),
                        static_cast<FLOAT>(rect.top),
                        static_cast<FLOAT>(content_rect_.right),
                        static_cast<FLOAT>(rect.bottom + DipToPx(4.0f))),
            bg);
      }
    }
    DrawTextLine(target, d2d_shell_.section_format(), d2d_shell_.text_brush(), rect, text);
  }
  sticky_titles_.clear();
}

// B4 键盘可达：按渲染顺序收集可聚焦命中区域（即 Tab 序）。
void SettingsWindow::RebuildFocusableRegions() {
  focusable_regions_.clear();
  for (const auto& region : hit_regions_) {
    switch (region.kind) {
      case HitKind::Nav:
      case HitKind::Field:
      case HitKind::Toggle:
      case HitKind::SelectTrigger:
      case HitKind::SliderDrag:
      case HitKind::ActionClearHistory:
      case HitKind::ActionClearImageCache:
      case HitKind::ActionOpenDataDir:
      case HitKind::ActionResetSection:
      case HitKind::Save:
      case HitKind::Cancel:
        focusable_regions_.push_back(region);
        break;
      case HitKind::None:
      case HitKind::SelectOption:
      case HitKind::SearchResult:
      case HitKind::ConfirmDiscard:
      case HitKind::ConfirmKeep:
        break;
    }
  }
  if (keyboard_focus_ >= static_cast<int>(focusable_regions_.size())) {
    keyboard_focus_ = -1;
  }
}

void SettingsWindow::DrawKeyboardFocusRing(ID2D1HwndRenderTarget* target) {
  if (keyboard_focus_ < 0 || keyboard_focus_ >= static_cast<int>(focusable_regions_.size())) {
    return;
  }
  const RECT& rect = focusable_regions_[static_cast<size_t>(keyboard_focus_)].rect;
  const float inset = static_cast<float>(DipToPx(2.0f));
  const D2D1_RECT_F rf = D2D1::RectF(
      static_cast<FLOAT>(rect.left) - inset, static_cast<FLOAT>(rect.top) - inset,
      static_cast<FLOAT>(rect.right) + inset, static_cast<FLOAT>(rect.bottom) + inset);
  const float radius = static_cast<float>(DipToPx(8.0f));
  target->DrawRoundedRectangle(D2D1::RoundedRect(rf, radius, radius), d2d_shell_.accent_brush(),
                               2.0f);
}

void SettingsWindow::ScrollFocusIntoView() {
  if (keyboard_focus_ < 0 || keyboard_focus_ >= static_cast<int>(focusable_regions_.size())) {
    return;
  }
  const RECT& rect = focusable_regions_[static_cast<size_t>(keyboard_focus_)].rect;
  const int pad = DipToPx(12.0f);
  if (rect.top < content_rect_.top) {
    scroll_y_ = std::max(0.0f, scroll_y_ - static_cast<float>(content_rect_.top - rect.top + pad));
    InvalidateRect(hwnd_, nullptr, FALSE);
  } else if (rect.bottom > content_rect_.bottom) {
    scroll_y_ = std::min(max_scroll_y_,
                         scroll_y_ + static_cast<float>(rect.bottom - content_rect_.bottom + pad));
    InvalidateRect(hwnd_, nullptr, FALSE);
  }
}

void SettingsWindow::MoveKeyboardFocus(int delta) {
  if (confirm_discard_) {
    return;
  }
  const int count = static_cast<int>(focusable_regions_.size());
  if (count == 0) {
    keyboard_focus_ = -1;
    return;
  }
  keyboard_focus_ = keyboard_focus_ < 0
      ? (delta > 0 ? 0 : count - 1)
      : (keyboard_focus_ + delta + count) % count;
  ScrollFocusIntoView();
  InvalidateRect(hwnd_, nullptr, FALSE);
}

// B4：Space/Enter 激活焦点控件；←→ 微调滑杆。
void SettingsWindow::ActivateKeyboardFocus(UINT virtual_key) {
  if (confirm_discard_) {
    return;
  }
  if (keyboard_focus_ < 0 || keyboard_focus_ >= static_cast<int>(focusable_regions_.size())) {
    return;
  }
  const HitRegion region = focusable_regions_[static_cast<size_t>(keyboard_focus_)];
  if (region.kind == HitKind::SliderDrag && (virtual_key == VK_LEFT || virtual_key == VK_RIGHT)) {
    if (region.option == 18) {  // window_blur_opacity
      const int step = virtual_key == VK_RIGHT ? 5 : -5;
      draft_.window_blur_opacity = std::clamp(draft_.window_blur_opacity + step, 0, 100);
      PreviewAppearanceChanges();
      InvalidateRect(hwnd_, nullptr, FALSE);
    }
    return;
  }
  if (virtual_key != VK_RETURN && virtual_key != VK_SPACE) {
    return;
  }
  if (region.kind == HitKind::Field) {
    BeginEditing(region.field);
    return;
  }
  HandlePrimaryAction(region.kind, region.option);
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
    case WM_GETMINMAXINFO: {
      // B2：设置窗最小可用尺寸，避免内容列被压缩到不可读。
      auto* info = reinterpret_cast<MINMAXINFO*>(l_param);
      info->ptMinTrackSize.x = DipToPx(640.0f);
      info->ptMinTrackSize.y = DipToPx(520.0f);
      return 0;
    }
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

      // rail 分割栏：拖拽优先于一切悬停交互。
      if (is_dragging_nav_splitter_) {
        const float scale = static_cast<float>(DipToPx(100)) / 100.0f;
        const float dip = static_cast<float>(point.x - DipToPx(24.0f)) / scale;
        nav_width_dip_ = std::clamp(dip, 56.0f, 220.0f);
        LayoutWindow();
        UpdateProxyLayout();
        InvalidateRect(hwnd_, nullptr, FALSE);
        SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
        return 0;
      }
      {
        RECT zone = NavSplitterRect();
        const bool over = PtInRect(&zone, point) != FALSE;
        if (over != nav_splitter_hover_) {
          nav_splitter_hover_ = over;
          InvalidateRect(hwnd_, nullptr, FALSE);
        }
        if (over) {
          SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
        }
      }

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
      UpdateNavHoverHint(nav_hover, point);

      if (!search_hit_rects_.empty()) {
        // 只在指针落在条目上时改选中项；移开后保留键盘选中的那条。
        const int search_hover = SearchHitAtPoint(point);
        if (search_hover >= 0 && search_hover != search_hover_index_) {
          search_hover_index_ = search_hover;
          InvalidateRect(hwnd_, &search_results_rect_, FALSE);
        }
      }

      if (is_dragging_scrollbar_) {
        // 与 ScrollbarThumbRect 同一套轨道几何：拖拽位移映射到滚动距离。
        RECT client{};
        GetClientRect(hwnd_, &client);
        const float view_h = static_cast<float>(content_rect_.bottom - content_rect_.top);
        const float total_h = std::max(view_h + max_scroll_y_, 1.0f);
        const float track_h = std::max(0.0f, view_h - DipToPx(16.0f));
        const float thumb_h = std::max(28.0f, track_h * (view_h / total_h));
        const float max_thumb_y = std::max(0.0f, track_h - thumb_h);
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
             PreviewAppearanceChanges();
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
      hover_hint_.Hide();
      nav_hover_hint_index_ = -1;
      if (nav_splitter_hover_) {
        nav_splitter_hover_ = false;
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
        is_dragging_nav_splitter_ = false;
      }
      return 0;
    case WM_LBUTTONDBLCLK:
    case WM_LBUTTONDOWN: {
      POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
      // 鼠标点击即让位给指针交互，清除键盘焦点环。
      keyboard_focus_ = -1;
      
      if (active_field_ != FieldId::None && !IsHotkeyField(active_field_)) {
        D2DWindowlessTextBox* target = (active_field_ == FieldId::Blacklist || active_field_ == FieldId::CustomBlacklist) ? &multi_line_textbox_ : &single_line_textbox_;
        if (target->HandleMouseInput(hwnd_, message, D2D1::Point2F(static_cast<float>(point.x), static_cast<float>(point.y)))) {
          InvalidateRect(hwnd_, nullptr, FALSE);
          return 0;
        }
      }
      
      if (message == WM_LBUTTONDBLCLK) {
        // 双击分割栏复位为默认窄 rail。
        RECT splitter_zone = NavSplitterRect();
        if (PtInRect(&splitter_zone, point)) {
          nav_width_dip_ = 56.0f;
          LayoutWindow();
          UpdateProxyLayout();
          InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return 0;
      }

      // rail 分割栏拖拽起手。
      {
        RECT splitter_zone = NavSplitterRect();
        if (PtInRect(&splitter_zone, point)) {
          is_dragging_nav_splitter_ = true;
          nav_splitter_hover_ = true;
          SetCapture(hwnd_);
          SetCursor(LoadCursorW(nullptr, IDC_SIZEWE));
          return 0;
        }
      }

      if (max_scroll_y_ > 0.0f) {
        const D2D1_RECT_F thumb = ScrollbarThumbRect();
        if (static_cast<float>(point.x) >= thumb.left - 4.0f &&
            static_cast<float>(point.x) <= thumb.right + 6.0f &&
            static_cast<float>(point.y) >= thumb.top &&
            static_cast<float>(point.y) <= thumb.bottom) {
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
             PreviewAppearanceChanges();
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

      if (is_dragging_nav_splitter_) {
        is_dragging_nav_splitter_ = false;
        ReleaseCapture();
        InvalidateRect(hwnd_, nullptr, FALSE);
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
          const bool is_search = (active_field_ == FieldId::Search);
          if (is_search && !search_hits_.empty() && (w_param == VK_DOWN || w_param == VK_UP)) {
            MoveSearchHover(w_param == VK_DOWN ? 1 : -1);
            return 0;
          }
          if (w_param == VK_ESCAPE) {
             if (is_search && (!search_text_.empty() || !search_hits_.empty())) {
               // 第一次 Esc 只清空关键词并收起结果，保留输入焦点。
               single_line_textbox_.SetText(L"");
               search_text_.clear();
               CloseSearchResults();
               pending_scroll_title_.clear();
               InvalidateRect(hwnd_, nullptr, FALSE);
               return 0;
             }
             EndEditing(true);
             return 0;
          }
          if (w_param == VK_TAB) {
             SyncActiveEditToState();
             FocusNextField((GetKeyState(VK_SHIFT) & 0x8000) != 0);
             return 0;
          }
          if (w_param == VK_RETURN && !target->IsMultiLine()) {
             if (is_search && !search_hits_.empty()) {
               ActivateSearchHit(search_hover_index_ >= 0 ? search_hover_index_ : 0);
               return 0;
             }
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
        if (confirm_discard_) {
          // Esc 先取消确认浮层，不直接关窗。
          confirm_discard_ = false;
          InvalidateRect(hwnd_, nullptr, FALSE);
          return 0;
        }
        HandleCloseRequest();
        return 0;
      }
      if (w_param == VK_TAB && active_field_ == FieldId::None) {
        // B4：Tab 在全部可聚焦控件间循环（Shift+Tab 反向）；首站是搜索框。
        MoveKeyboardFocus((GetKeyState(VK_SHIFT) & 0x8000) != 0 ? -1 : 1);
        return 0;
      }
      if ((GetKeyState(VK_CONTROL) & 0x8000) != 0 && w_param == 'F') {
        BeginEditing(FieldId::Search);
        return 0;
      }
      if ((GetKeyState(VK_CONTROL) & 0x8000) != 0 && w_param == VK_HOME) {
        // A5：Ctrl+Home 回到列表顶部。
        scroll_y_ = 0.0f;
        InvalidateRect(hwnd_, &content_rect_, FALSE);
        return 0;
      }
      if (active_field_ == FieldId::None && keyboard_focus_ >= 0 &&
          (w_param == VK_RETURN || w_param == VK_SPACE || w_param == VK_LEFT ||
           w_param == VK_RIGHT)) {
        ActivateKeyboardFocus(static_cast<UINT>(w_param));
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

