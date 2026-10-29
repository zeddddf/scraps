#include "core/store/ClipRepository.Internal.h"

namespace clipd2d::core::store {

common::Status ClipRepository::SaveSettings(const AppSettings& settings) {
  const auto save_one = [this](const char* key, const std::string& value) -> common::Status {
    Statement statement(
        db_,
        "INSERT INTO settings(key, value) VALUES(?1, ?2)"
        " ON CONFLICT(key) DO UPDATE SET value = excluded.value");
    BindText(statement.get(), 1, key);
    BindText(statement.get(), 2, value);
    if (sqlite3_step(statement.get()) != SQLITE_DONE) {
      return SqliteError(db_, "保存设置失败");
    }
    return common::Status::Ok();
  };

  common::Status status = save_one("history_limit", std::to_string(settings.history_limit));
  if (!status.ok()) return status;
  status = save_one("history_retention_days", std::to_string(settings.history_retention_days));
  if (!status.ok()) return status;
  status = save_one("start_with_windows", settings.start_with_windows ? "1" : "0");
  if (!status.ok()) return status;
  status = save_one("open_panel_on_first_run", settings.open_panel_on_first_run ? "1" : "0");
  if (!status.ok()) return status;
  status = save_one("has_completed_first_run", settings.has_completed_first_run ? "1" : "0");
  if (!status.ok()) return status;
  status = save_one("show_source_icon", settings.show_source_icon ? "1" : "0");
  if (!status.ok()) return status;
  status = save_one("monitoring_enabled", settings.monitoring_enabled ? "1" : "0");
  if (!status.ok()) return status;
  status = save_one("queue_auto_pop", settings.queue_auto_pop ? "1" : "0");
  if (!status.ok()) return status;
  status = save_one("enable_win_v_override", settings.enable_win_v_override ? "1" : "0");
  if (!status.ok()) return status;
  status = save_one("window_blur_type", std::to_string(static_cast<int>(settings.window_blur_type)));
  if (!status.ok()) return status;
  status = save_one("window_blur_opacity", std::to_string(settings.window_blur_opacity));
  if (!status.ok()) return status;
  status = save_one("theme", std::to_string(static_cast<int>(settings.theme)));
  if (!status.ok()) return status;
  status = save_one("list_density", ToSettingsValue(settings.list_density));
  if (!status.ok()) return status;
  status = save_one("window_position_mode", ToSettingsValue(settings.window_position_mode));
  if (!status.ok()) return status;
  status = save_one("hotkey", common::WideToUtf8(settings.hotkey).utf8);
  if (!status.ok()) return status;
  status = save_one("quick_paste_hotkey", common::WideToUtf8(settings.quick_paste_hotkey).utf8);
  if (!status.ok()) return status;
  status = save_one("focus_search_on_open", settings.focus_search_on_open ? "1" : "0");
  if (!status.ok()) return status;
  status = save_one("search_focus_hotkey", common::WideToUtf8(settings.search_focus_hotkey).utf8);
  if (!status.ok()) return status;
  status = save_one("hover_preview_enabled", settings.hover_preview_enabled ? "1" : "0");
  if (!status.ok()) return status;
  status = save_one("third_party_sticker_enabled", settings.third_party_sticker_enabled ? "1" : "0");
  if (!status.ok()) return status;
  status = save_one("third_party_sticker_hotkey", common::WideToUtf8(settings.third_party_sticker_hotkey).utf8);
  if (!status.ok()) return status;
  status = save_one("click_paste_mode", ToSettingsValue(settings.click_paste_mode));
  if (!status.ok()) return status;
  status = save_one("promote_pasted_clip_on_reuse", settings.promote_pasted_clip_on_reuse ? "1" : "0");
  if (!status.ok()) return status;
  status = save_one("external_editor_path", common::WideToUtf8(settings.external_editor_path).utf8);
  if (!status.ok()) return status;
  status = save_one("blacklist", common::WideToUtf8(settings.blacklist).utf8);
  if (!status.ok()) return status;
  status = save_one("image_ocr_enabled", settings.image_ocr_enabled ? "1" : "0");
  if (!status.ok()) return status;
  status = save_one("ocr_executable_path", common::WideToUtf8(settings.ocr_executable_path).utf8);
  if (!status.ok()) return status;
  status = save_one("ocr_language_hint", common::WideToUtf8(settings.ocr_language_hint).utf8);
  if (!status.ok()) return status;
  status = save_one("saved_window_x", std::to_string(settings.saved_window_x));
  if (!status.ok()) return status;
  status = save_one("saved_window_y", std::to_string(settings.saved_window_y));
  if (!status.ok()) return status;
  status = save_one("saved_window_width", std::to_string(settings.saved_window_width));
  if (!status.ok()) return status;
  status = save_one("saved_window_height", std::to_string(settings.saved_window_height));
  if (!status.ok()) return status;
  status = save_one("has_saved_window_position", settings.has_saved_window_position ? "1" : "0");
  if (!status.ok()) return status;
  status = save_one("has_saved_window_size", settings.has_saved_window_size ? "1" : "0");
  if (!status.ok()) return status;
  status = save_one("auto_ignore_password_managers", settings.auto_ignore_password_managers ? "1" : "0");
  if (!status.ok()) return status;
  status = save_one("custom_blacklist_apps", common::WideToUtf8(settings.custom_blacklist_apps).utf8);
  if (!status.ok()) return status;
  status = save_one("ignore_large_text", settings.ignore_large_text ? "1" : "0");
  if (!status.ok()) return status;
  status = save_one("max_text_size_kb", std::to_string(settings.max_text_size_kb));
  if (!status.ok()) return status;
  status = save_one("ignore_large_images", settings.ignore_large_images ? "1" : "0");
  if (!status.ok()) return status;
  status = save_one("max_image_size_kb", std::to_string(settings.max_image_size_kb));
  if (!status.ok()) return status;
  status = save_one("ignore_otp_codes", settings.ignore_otp_codes ? "1" : "0");
  if (!status.ok()) return status;
  status = save_one("ignore_complex_passwords", settings.ignore_complex_passwords ? "1" : "0");
  if (!status.ok()) return status;
  status = save_one("screenshot_enabled", settings.screenshot_enabled ? "1" : "0");
  if (!status.ok()) return status;
  status = save_one("screenshot_prewarm", settings.screenshot_prewarm ? "1" : "0");
  if (!status.ok()) return status;
  status = save_one("screenshot_hotkey", common::WideToUtf8(settings.screenshot_hotkey).utf8);
  return save_one("enable_scroll_blur", settings.enable_scroll_blur ? "1" : "0");
}

}  // namespace clipd2d::core::store
