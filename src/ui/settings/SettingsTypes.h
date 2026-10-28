#pragma once

#include <cstdint>
#include <string>
#include <windows.h>

#include "core/store/ClipTypes.h"

namespace clipd2d::ui {

struct DataStats {
  std::uint64_t database_bytes = 0;
  std::uint64_t image_cache_bytes = 0;
  std::wstring data_dir;
  std::wstring database_path;
};

enum class Section {
  General = 0,
  Appearance,
  Hotkey,
  Clipboard,
  Filter,
  Data,
  About,
};

// 节标题（导航 / 搜索结果 / 悬停提示共用）。
inline const wchar_t* SectionTitle(Section section) {
  switch (section) {
    case Section::General:
      return L"常规";
    case Section::Appearance:
      return L"外观";
    case Section::Hotkey:
      return L"快捷键";
    case Section::Clipboard:
      return L"剪贴板";
    case Section::Filter:
      return L"过滤";
    case Section::Data:
      return L"数据管理";
    case Section::About:
      return L"关于";
  }
  return L"";
}

enum class FieldId {
  None = 0,
  Search,
  HistoryLimit,
  HistoryRetentionDays,
  Hotkey,
  QuickPasteHotkey,
  SearchFocusHotkey,
  ThirdPartyStickerHotkey,
  ScreenshotHotkey,
  ExternalEditorPath,
  OcrExecutablePath,
  OcrLanguageHint,
  Blacklist,
  CustomBlacklist,
  MaxTextSize,
  MaxImageSize,
  EnableWindowBlur,
};

enum class SelectField {
  None = 0,
  ListDensity,
  WindowPositionMode,
  ClickPasteMode,
  WindowBlurType,
  Theme,
};

enum class HitKind {
  None = 0,
  Nav,
  Field,
  Toggle,
  SelectTrigger,
  SelectOption,
  SliderDrag,
  ActionClearHistory,
  ActionClearImageCache,
  ActionOpenDataDir,
  Save,
  Cancel,
  // 跨节搜索结果条目（option = search_hits_ 下标）
  SearchResult,
  // 恢复本节默认（option = (int)Section）
  ActionResetSection,
  // 未保存修改确认浮层
  ConfirmDiscard,
  ConfirmKeep,
};

struct DraftState {
  std::wstring history_limit_text;
  std::wstring history_retention_days_text;
  bool start_with_windows = false;
  bool open_panel_on_first_run = true;
  bool show_source_icon = true;
  bool monitoring_enabled = true;
  bool queue_auto_pop = true;
  bool enable_win_v_override = false;
  core::store::ListDensity list_density = core::store::ListDensity::Comfortable;
  core::store::WindowPositionMode window_position_mode = core::store::WindowPositionMode::Remember;
  core::store::WindowBlurType window_blur_type = core::store::WindowBlurType::Mica;
  int window_blur_opacity = 50;
  core::store::ThemeMode theme = core::store::ThemeMode::System;
  std::wstring hotkey;
  std::wstring quick_paste_hotkey;
  bool focus_search_on_open = false;
  std::wstring search_focus_hotkey;
  bool third_party_sticker_enabled = false;
  std::wstring third_party_sticker_hotkey;
  core::store::ClickPasteMode click_paste_mode = core::store::ClickPasteMode::Single;
  bool promote_pasted_clip_on_reuse = false;
  std::wstring external_editor_path;
  bool image_ocr_enabled = true;
  std::wstring ocr_executable_path;
  std::wstring ocr_language_hint;
  std::wstring blacklist;
  bool auto_ignore_password_managers = true;
  std::wstring custom_blacklist_apps;
  bool ignore_large_text = true;
  std::wstring max_text_size_kb_text;
  bool ignore_large_images = true;
  std::wstring max_image_size_kb_text;
  bool ignore_otp_codes = true;
  bool ignore_complex_passwords = false;
  bool screenshot_enabled = true;
  bool screenshot_prewarm = true;
  std::wstring screenshot_hotkey;
};

struct HitRegion {
  RECT rect{};
  HitKind kind = HitKind::None;
  Section section = Section::General;
  FieldId field = FieldId::None;
  int option = 0;
};

}  // namespace clipd2d::ui
