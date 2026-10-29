#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <windows.h>

namespace clipd2d::core::store {

enum class ClipContentType {
  Text,
  Html,
  Rtf,
  Image,
  Files,
};

enum class ClipView {
  All,
  Favorites,
  Pinned,
  Text,
  Html,
  Image,
  Files,
  Group,
  Frequent,
  Queue,
};

enum class ListDensity {
  Compact,
  Comfortable,
};

enum class StorageTier {
  History,
  Library,
};

enum class DateRangeFilter {
  All,
  Today,
  Last7Days,
  Last30Days,
};

enum class WindowPositionMode {
  Remember,
  FollowMouse,
};

enum class ClickPasteMode {
  Single,
  Double,
};

enum class ClipIdentityKind {
  LegacyPayload,
  TextTitlePlain,
  TextPayloadSet,
  ImageFilePath,
  ImageFileSnapshot,
  ImageContentHash,
};

enum class ReuseTouchPolicy {
  PromoteOnReuse,
  KeepPositionOnReuse,
};

enum class ClipArrivalReason {
  ExternalCapture,
  InternalStickerWriteback,
  InternalClipboardWriteback,
};

struct SourceAppInfo {
  std::wstring process_path;
  std::wstring process_name;
  std::wstring window_title;
};

enum class ClipFormatPayloadKind {
  Text,
  Binary,
};

struct ClipFormatPayload {
  std::string format_name;
  ClipFormatPayloadKind payload_kind = ClipFormatPayloadKind::Text;
  std::string text_data;
  std::vector<std::uint8_t> binary_data;
  std::wstring storage_path;
  int format_order = 0;
};

struct CapturedClip {
  ClipContentType content_type = ClipContentType::Text;
  ClipIdentityKind identity_kind = ClipIdentityKind::LegacyPayload;
  std::string identity_key;
  int identity_version = 3;
  std::string title;
  std::string plain_text;
  std::string html_text;
  std::string rtf_text;
  std::wstring storage_path;
  std::wstring thumbnail_path;
  std::wstring preview_path;
  std::wstring secondary_path;
  std::string content_hash;
  std::int64_t estimated_decoded_bytes = 0;
  bool is_large_image = false;
  std::string ingest_state = "ready";
  std::string ingest_error;
  SourceAppInfo source_app;
  int image_width = 0;
  int image_height = 0;
  std::vector<ClipFormatPayload> formats;
  std::vector<std::uint8_t> ingest_bytes;
  std::string ingest_format;
};

struct ClipSummary {
  std::int64_t id = 0;
  ClipIdentityKind identity_kind = ClipIdentityKind::LegacyPayload;
  std::string identity_key;
  int identity_version = 3;
  std::string title;
  std::string snippet;
  ClipContentType content_type = ClipContentType::Text;
  StorageTier storage_tier = StorageTier::History;
  bool is_pinned = false;
  bool is_favorite = false;
  std::int64_t group_id = 0;
  std::wstring source_app_name;
  std::wstring source_app_path;
  std::wstring storage_path;
  std::wstring secondary_path;
  std::wstring thumbnail_path;
  std::wstring preview_path;
  std::int64_t updated_at = 0;
  std::int64_t last_pasted_at = 0;
  std::int64_t estimated_decoded_bytes = 0;
  int image_width = 0;
  int image_height = 0;
  int text_length = 0;
  int file_count = 0;
  int paste_count = 0;
  bool is_frequent = false;
  bool is_large_image = false;
  std::string ingest_state = "ready";
  std::string ingest_error;
  std::string ocr_state = "none";
  std::string ocr_text_snippet;
  std::string ocr_error;
  std::vector<std::int64_t> tag_ids;
  std::vector<std::wstring> tag_names;
  std::vector<std::wstring> tag_colors;
};

struct ClipRecord {
  ClipSummary summary;
  ClipIdentityKind identity_kind = ClipIdentityKind::LegacyPayload;
  std::string identity_key;
  int identity_version = 3;
  std::string plain_text;
  std::string html_text;
  std::string rtf_text;
  std::wstring storage_path;
  std::wstring thumbnail_path;
  std::wstring preview_path;
  std::wstring secondary_path;
  std::int64_t estimated_decoded_bytes = 0;
  int image_width = 0;
  int image_height = 0;
  bool is_large_image = false;
  std::string ingest_state = "ready";
  std::string ingest_error;
  std::string ocr_text;
  std::string ocr_state = "none";
  std::string ocr_error;
  std::string ocr_engine;
  std::string ocr_language;
  std::int64_t ocr_updated_at = 0;
  std::vector<ClipFormatPayload> formats;
};

struct GroupRecord {
  std::int64_t id = 0;
  std::wstring name;
  int sort_order = 0;
  bool tab_pinned = false;
  int tab_sort_order = 0;
};

struct TagRecord {
  std::int64_t id = 0;
  std::wstring name;
  std::wstring color;
  bool is_system = false;
  int sort_order = 0;
};

struct QueueEntry {
  std::int64_t id = 0;
  std::int64_t clip_id = 0;
  int sort_order = 0;
  bool auto_pop = true;
  std::int64_t created_at = 0;
};

struct BootstrapState {
  bool has_any_clips = false;
  bool has_any_settings = false;
};



struct ClipQuery {
  ClipView view = ClipView::All;
  std::optional<std::int64_t> group_id;
  std::wstring group_name;
  std::string search_text;
  std::optional<ClipContentType> type_filter;
  std::optional<StorageTier> storage_tier_filter;
  std::wstring source_app_name;
  bool favorite_only = false;
  bool pinned_only = false;
  bool frequent_only = false;
  bool queue_only = false;
  int days = 0;
  DateRangeFilter date_range = DateRangeFilter::All;
  std::vector<std::int64_t> tag_ids;
};

enum class WindowBlurType {
  None = 0,
  Mica = 1,
  Acrylic = 2,
};

// 主题模式：System 跟随系统深浅色，Light/Dark 强制指定。
enum class ThemeMode {
  System = 0,
  Light = 1,
  Dark = 2,
};

struct AppSettings {
  int history_limit = 10000;
  int history_retention_days = 30;
  bool start_with_windows = false;
  bool open_panel_on_first_run = true;
  bool has_completed_first_run = false;
  bool show_source_icon = true;
  bool monitoring_enabled = true;
  bool queue_auto_pop = true;
  bool enable_win_v_override = false;
  ListDensity list_density = ListDensity::Comfortable;
  WindowPositionMode window_position_mode = WindowPositionMode::Remember;
  WindowBlurType window_blur_type = WindowBlurType::Mica;
  int window_blur_opacity = 50;
  ThemeMode theme = ThemeMode::System;
  std::wstring hotkey = L"Ctrl+Shift+V";
  std::wstring quick_paste_hotkey = L"Alt+~";
  bool focus_search_on_open = false;
  std::wstring search_focus_hotkey = L"Tab";
  // 悬停预览：列表行上定格弹出紧凑预览窗（PreviewWindow 悬停路径）。
  bool hover_preview_enabled = true;
  bool third_party_sticker_enabled = false;
  std::wstring third_party_sticker_hotkey;
  ClickPasteMode click_paste_mode = ClickPasteMode::Single;
  bool promote_pasted_clip_on_reuse = false;
  std::wstring external_editor_path;
  std::wstring blacklist;
  bool image_ocr_enabled = true;
  std::wstring ocr_executable_path;
  std::wstring ocr_language_hint;
  int saved_window_x = CW_USEDEFAULT;
  int saved_window_y = CW_USEDEFAULT;
  int saved_window_width = 460;
  int saved_window_height = 720;
  bool has_saved_window_position = false;
  bool has_saved_window_size = false;

  bool auto_ignore_password_managers = true;
  std::wstring custom_blacklist_apps;
  bool ignore_large_text = true;
  int max_text_size_kb = 1024;
  bool ignore_large_images = true;
  int max_image_size_kb = 5120;
  bool ignore_otp_codes = true;
  bool ignore_complex_passwords = false;
  bool screenshot_enabled = true;
  bool screenshot_prewarm = true;
  std::wstring screenshot_hotkey = L"Win+Shift+S";
  bool enable_scroll_blur = false;
};

const char* ToSqlContentType(ClipContentType content_type);
ClipContentType ParseSqlContentType(const std::string& value);
const wchar_t* ToDisplayLabel(ClipContentType content_type);

const char* ToSettingsValue(StorageTier tier);
StorageTier ParseStorageTier(std::string_view value);
const wchar_t* ToDisplayLabel(StorageTier tier);

const wchar_t* ToDisplayLabel(DateRangeFilter range);

const char* ToSqlPayloadKind(ClipFormatPayloadKind kind);
ClipFormatPayloadKind ParseSqlPayloadKind(std::string_view value);

const char* ToSqlIdentityKind(ClipIdentityKind kind);
ClipIdentityKind ParseSqlIdentityKind(std::string_view value);

const char* ToSettingsValue(ListDensity density);
ListDensity ParseListDensity(std::string_view value);
const wchar_t* ToDisplayLabel(ListDensity density);

const char* ToSettingsValue(WindowPositionMode mode);
WindowPositionMode ParseWindowPositionMode(std::string_view value);
const wchar_t* ToDisplayLabel(WindowPositionMode mode);

const char* ToSettingsValue(ClickPasteMode mode);
ClickPasteMode ParseClickPasteMode(std::string_view value);
const wchar_t* ToDisplayLabel(ClickPasteMode mode);

}  // namespace clipd2d::core::store
