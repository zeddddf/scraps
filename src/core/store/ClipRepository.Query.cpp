#include "core/store/ClipRepository.Internal.h"

namespace clipd2d::core::store {

common::Status ClipRepository::CountClips(const ClipQuery& query, std::int64_t* count) const {
  if (count == nullptr) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "输出数量对象不能为空。");
  }

  std::string sql = "SELECT COUNT(*) FROM clips c";
  if (!common::TrimAscii(query.search_text).empty()) {
    sql += " JOIN clips_fts f ON f.rowid = c.id";
  }
  sql += " WHERE 1=1";

  std::vector<std::function<void(sqlite3_stmt*, int*)>> binders;
  AppendQuerySql(query, &sql, &binders);

  Statement statement(db_, sql.c_str());
  int bind_index = 1;
  for (const auto& binder : binders) {
    binder(statement.get(), &bind_index);
  }

  if (sqlite3_step(statement.get()) != SQLITE_ROW) {
    return SqliteError(db_, "统计条目数量失败");
  }
  *count = sqlite3_column_int64(statement.get(), 0);
  return common::Status::Ok();
}

common::Status ClipRepository::ListClips(
    const ClipQuery& query,
    int offset,
    int limit,
    std::vector<ClipSummary>* clips) const {
  if (clips == nullptr) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "输出列表不能为空。");
  }

  std::string sql =
      "SELECT c.id, c.title, c.plain_text, c.content_type, c.storage_tier, c.is_pinned, c.is_favorite,"
      " c.group_id, c.source_app_name, c.source_app_path, c.updated_at, c.last_pasted_at,"
      " c.storage_path, c.secondary_path, c.thumbnail_path, c.preview_path,"
      " c.image_width, c.image_height, c.estimated_decoded_bytes, c.is_large_image, c.ingest_state, c.ingest_error,"
      " c.ocr_state, c.ocr_text, c.ocr_error,"
      " c.paste_count"
      " FROM clips c";
  if (!common::TrimAscii(query.search_text).empty()) {
    sql += " JOIN clips_fts f ON f.rowid = c.id";
  }
  if (query.view == ClipView::Queue || query.queue_only) {
    sql += " LEFT JOIN paste_queue q ON q.clip_id = c.id";
  }
  sql += " WHERE 1=1";

  std::vector<std::function<void(sqlite3_stmt*, int*)>> binders;
  AppendQuerySql(query, &sql, &binders);
  const bool use_search = !common::TrimAscii(query.search_text).empty();
  const bool use_single_tag_sort =
      !use_search &&
      query.tag_ids.size() == 1 &&
      !(query.view == ClipView::Queue || query.queue_only) &&
      query.view != ClipView::Favorites &&
      !(query.view == ClipView::Frequent || query.frequent_only);
  const bool use_group = query.view == ClipView::Group && (query.group_id.has_value() || !query.group_name.empty());
  const std::string first_search_token = FirstSearchToken(query.search_text);
  const std::string rank_pattern = first_search_token.empty() ? std::string{} : Utf8LikePattern(first_search_token);
  if (query.view == ClipView::Queue || query.queue_only) {
    sql += " ORDER BY q.sort_order ASC, c.updated_at DESC";
  } else if (use_search) {
    sql +=
        " ORDER BY c.is_pinned DESC,"
        " c.pin_sort_order ASC,"
        " CASE"
        "  WHEN c.title LIKE ? ESCAPE '\\' THEN 0"
        "  WHEN c.plain_text LIKE ? ESCAPE '\\' THEN 1"
        "  WHEN c.source_app_name LIKE ? ESCAPE '\\' THEN 2"
        "  WHEN c.ocr_text LIKE ? ESCAPE '\\' THEN 3"
        "  ELSE 4"
        " END,";
    if (query.view == ClipView::Frequent || query.frequent_only) {
      sql += " c.paste_count DESC, c.last_pasted_at DESC,";
    } else if (query.view == ClipView::Favorites) {
      sql += " c.favorite_sort_order ASC,";
    }
    sql += " c.updated_at DESC";
  } else if (query.view == ClipView::Favorites) {
    sql += " ORDER BY c.favorite_sort_order ASC, c.updated_at DESC";
  } else if (use_group) {
    sql += " ORDER BY c.group_sort_order ASC, c.updated_at DESC";
  } else if (query.view == ClipView::Frequent || query.frequent_only) {
    sql += " ORDER BY c.paste_count DESC, c.last_pasted_at DESC, c.updated_at DESC";
  } else if (use_single_tag_sort) {
    sql += " ORDER BY (SELECT ct.sort_order FROM clip_tags ct WHERE ct.clip_id = c.id AND ct.tag_id = ?) ASC, c.updated_at DESC";
  } else {
    sql += " ORDER BY c.is_pinned DESC, c.pin_sort_order ASC, c.updated_at DESC";
  }
  sql += " LIMIT ? OFFSET ?";

  Statement statement(db_, sql.c_str());
  int bind_index = 1;
  for (const auto& binder : binders) {
    binder(statement.get(), &bind_index);
  }
  if (use_search) {
    BindText(statement.get(), bind_index++, rank_pattern);
    BindText(statement.get(), bind_index++, rank_pattern);
    BindText(statement.get(), bind_index++, rank_pattern);
    BindText(statement.get(), bind_index++, rank_pattern);
  } else if (use_single_tag_sort) {
    sqlite3_bind_int64(statement.get(), bind_index++, query.tag_ids.front());
  }
  sqlite3_bind_int(statement.get(), bind_index++, limit);
  sqlite3_bind_int(statement.get(), bind_index++, offset);

  clips->clear();
  while (sqlite3_step(statement.get()) == SQLITE_ROW) {
    ClipSummary summary;
    summary.id = sqlite3_column_int64(statement.get(), 0);
    summary.title = ColumnString(statement.get(), 1);
    summary.snippet = ColumnString(statement.get(), 2).substr(0, 180);
    summary.content_type = ParseSqlContentType(ColumnString(statement.get(), 3));
    summary.storage_tier = ParseStorageTier(ColumnString(statement.get(), 4));
    summary.is_pinned = sqlite3_column_int(statement.get(), 5) != 0;
    summary.is_favorite = sqlite3_column_int(statement.get(), 6) != 0;
    summary.group_id = sqlite3_column_int64(statement.get(), 7);
    summary.source_app_name = ToWideOrEmpty(ColumnString(statement.get(), 8).c_str());
    summary.source_app_path = ToWideOrEmpty(ColumnString(statement.get(), 9).c_str());
    summary.updated_at = sqlite3_column_int64(statement.get(), 10);
    summary.last_pasted_at = sqlite3_column_int64(statement.get(), 11);
    summary.storage_path = ToWideOrEmpty(ColumnString(statement.get(), 12).c_str());
    summary.secondary_path = ToWideOrEmpty(ColumnString(statement.get(), 13).c_str());
    summary.thumbnail_path = ToWideOrEmpty(ColumnString(statement.get(), 14).c_str());
    summary.preview_path = ToWideOrEmpty(ColumnString(statement.get(), 15).c_str());
    summary.image_width = sqlite3_column_int(statement.get(), 16);
    summary.image_height = sqlite3_column_int(statement.get(), 17);
    summary.estimated_decoded_bytes = sqlite3_column_int64(statement.get(), 18);
    summary.is_large_image = sqlite3_column_int(statement.get(), 19) != 0;
    summary.ingest_state = ColumnString(statement.get(), 20);
    summary.ingest_error = ColumnString(statement.get(), 21);
    summary.ocr_state = ColumnString(statement.get(), 22);
    summary.ocr_text_snippet = ColumnString(statement.get(), 23).substr(0, 180);
    summary.ocr_error = ColumnString(statement.get(), 24);
    summary.paste_count = sqlite3_column_int(statement.get(), 25);
    summary.is_frequent = IsFrequentClip(summary.paste_count, summary.last_pasted_at);

    const std::wstring wide_snippet = common::Utf8ToWide(summary.snippet).wide;
    if (summary.content_type == ClipContentType::Text ||
        summary.content_type == ClipContentType::Html ||
        summary.content_type == ClipContentType::Rtf) {
      summary.text_length = static_cast<int>(wide_snippet.size());
    }
    if (summary.content_type == ClipContentType::Files) {
      summary.file_count = 0;
      for (const auto& part : common::SplitWide(wide_snippet, L'\n')) {
        if (!common::TrimWide(part).empty()) {
          ++summary.file_count;
        }
      }
      const std::wstring first_path = FirstFilePathFromText(summary.snippet);
      summary.storage_path = first_path;
      std::wstring display_name = FileDisplayName(first_path);
      if (summary.file_count > 1 && !display_name.empty()) {
        display_name += L" 等";
      }
      summary.title = common::WideToUtf8(display_name).utf8;
      summary.snippet = common::WideToUtf8(FileSecondaryText(first_path, summary.file_count)).utf8;
    }
    std::vector<TagRecord> tags;
    common::Status tag_status = LoadClipTags(db_, summary.id, &tags);
    if (!tag_status.ok()) {
      return tag_status;
    }
    for (const auto& tag : tags) {
      summary.tag_ids.push_back(tag.id);
      summary.tag_names.push_back(tag.name);
      summary.tag_colors.push_back(tag.color);
    }
    clips->push_back(std::move(summary));
  }
  return common::Status::Ok();
}

common::Status ClipRepository::GetClipDetailsById(std::int64_t id, ClipRecord* clip) const {
  if (clip == nullptr) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "输出条目不能为空。");
  }

  Statement statement(
      db_,
      "SELECT id, title, plain_text, html_text, rtf_text, content_type, storage_tier, storage_path, secondary_path,"
      " thumbnail_path, preview_path, is_pinned, is_favorite, group_id, source_app_name, source_app_path, updated_at, last_pasted_at,"
      " paste_count, image_width, image_height, estimated_decoded_bytes, is_large_image, ingest_state, ingest_error,"
      " ocr_text, ocr_state, ocr_error, ocr_engine, ocr_language, ocr_updated_at"
      " FROM clips WHERE id = ?1");
  sqlite3_bind_int64(statement.get(), 1, id);
  if (sqlite3_step(statement.get()) != SQLITE_ROW) {
    return common::Status::Error(common::StatusCode::NotFound, "条目不存在。");
  }

  clip->summary.id = sqlite3_column_int64(statement.get(), 0);
  clip->summary.title = ColumnString(statement.get(), 1);
  clip->plain_text = ColumnString(statement.get(), 2);
  clip->html_text = ColumnString(statement.get(), 3);
  clip->rtf_text = ColumnString(statement.get(), 4);
  clip->summary.content_type = ParseSqlContentType(ColumnString(statement.get(), 5));
  clip->summary.storage_tier = ParseStorageTier(ColumnString(statement.get(), 6));
  clip->storage_path = ToWideOrEmpty(ColumnString(statement.get(), 7).c_str());
  clip->secondary_path = ToWideOrEmpty(ColumnString(statement.get(), 8).c_str());
  clip->thumbnail_path = ToWideOrEmpty(ColumnString(statement.get(), 9).c_str());
  clip->preview_path = ToWideOrEmpty(ColumnString(statement.get(), 10).c_str());
  clip->summary.is_pinned = sqlite3_column_int(statement.get(), 11) != 0;
  clip->summary.is_favorite = sqlite3_column_int(statement.get(), 12) != 0;
  clip->summary.group_id = sqlite3_column_int64(statement.get(), 13);
  clip->summary.source_app_name = ToWideOrEmpty(ColumnString(statement.get(), 14).c_str());
  clip->summary.source_app_path = ToWideOrEmpty(ColumnString(statement.get(), 15).c_str());
  clip->summary.updated_at = sqlite3_column_int64(statement.get(), 16);
  clip->summary.last_pasted_at = sqlite3_column_int64(statement.get(), 17);
  clip->summary.paste_count = sqlite3_column_int(statement.get(), 18);
  clip->image_width = sqlite3_column_int(statement.get(), 19);
  clip->image_height = sqlite3_column_int(statement.get(), 20);
  clip->estimated_decoded_bytes = sqlite3_column_int64(statement.get(), 21);
  clip->is_large_image = sqlite3_column_int(statement.get(), 22) != 0;
  clip->ingest_state = ColumnString(statement.get(), 23);
  clip->ingest_error = ColumnString(statement.get(), 24);
  clip->ocr_text = ColumnString(statement.get(), 25);
  clip->ocr_state = ColumnString(statement.get(), 26);
  clip->ocr_error = ColumnString(statement.get(), 27);
  clip->ocr_engine = ColumnString(statement.get(), 28);
  clip->ocr_language = ColumnString(statement.get(), 29);
  clip->ocr_updated_at = sqlite3_column_int64(statement.get(), 30);
  clip->summary.storage_path = clip->storage_path;
  clip->summary.secondary_path = clip->secondary_path;
  clip->summary.thumbnail_path = clip->thumbnail_path;
  clip->summary.preview_path = clip->preview_path;
  clip->summary.image_width = clip->image_width;
  clip->summary.image_height = clip->image_height;
  clip->summary.estimated_decoded_bytes = clip->estimated_decoded_bytes;
  clip->summary.is_large_image = clip->is_large_image;
  clip->summary.ingest_state = clip->ingest_state;
  clip->summary.ingest_error = clip->ingest_error;
  clip->summary.ocr_state = clip->ocr_state;
  clip->summary.ocr_text_snippet = clip->ocr_text.substr(0, 180);
  clip->summary.ocr_error = clip->ocr_error;
  clip->summary.is_frequent = IsFrequentClip(clip->summary.paste_count, clip->summary.last_pasted_at);
  clip->summary.text_length = static_cast<int>(common::Utf8ToWide(clip->plain_text).wide.size());
  clip->summary.snippet = clip->plain_text.empty() ? clip->ocr_text.substr(0, 180) : clip->plain_text.substr(0, 180);

  std::vector<TagRecord> tags;
  common::Status status = LoadClipTags(db_, id, &tags);
  if (!status.ok()) {
    return status;
  }
  for (const auto& tag : tags) {
    clip->summary.tag_ids.push_back(tag.id);
    clip->summary.tag_names.push_back(tag.name);
    clip->summary.tag_colors.push_back(tag.color);
  }
  if (clip->summary.content_type == ClipContentType::Files) {
    clip->summary.file_count = 0;
    for (const auto& part : common::SplitWide(common::Utf8ToWide(clip->plain_text).wide, L'\n')) {
      if (!common::TrimWide(part).empty()) {
        ++clip->summary.file_count;
      }
    }
  }
  return common::Status::Ok();
}

common::Status ClipRepository::GetClipFormatsById(std::int64_t id, std::vector<ClipFormatPayload>* formats) const {
  return LoadClipFormats(db_, id, formats);
}

common::Status ClipRepository::GetClipById(std::int64_t id, ClipRecord* clip) const {
  common::Status status = GetClipDetailsById(id, clip);
  if (!status.ok()) {
    return status;
  }

  status = LoadClipFormats(db_, id, &clip->formats);
  if (!status.ok()) {
    return status;
  }
  for (const auto& format : clip->formats) {
    if (clip->html_text.empty() && format.format_name == "HTML Format") {
      clip->html_text = format.text_data;
    } else if (clip->rtf_text.empty() && format.format_name == "Rich Text Format") {
      clip->rtf_text = format.text_data;
    }
  }
  return common::Status::Ok();
}

common::Status ClipRepository::GetLatestClip(ClipRecord* clip) const {
  Statement statement(db_, "SELECT id FROM clips ORDER BY is_pinned DESC, updated_at DESC LIMIT 1");
  if (sqlite3_step(statement.get()) != SQLITE_ROW) {
    return common::Status::Error(common::StatusCode::NotFound, "没有可用条目。");
  }
  return GetClipById(sqlite3_column_int64(statement.get(), 0), clip);
}

common::Status ClipRepository::LoadBootstrapState(BootstrapState* state) const {
  if (state == nullptr) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "输出启动状态对象不能为空。");
  }
  Statement statement(
      db_,
      "SELECT "
      " EXISTS(SELECT 1 FROM clips LIMIT 1),"
      " EXISTS(SELECT 1 FROM settings LIMIT 1)");
  if (sqlite3_step(statement.get()) != SQLITE_ROW) {
    return SqliteError(db_, "读取启动状态失败");
  }
  state->has_any_clips = sqlite3_column_int(statement.get(), 0) != 0;
  state->has_any_settings = sqlite3_column_int(statement.get(), 1) != 0;
  return common::Status::Ok();
}

common::Status ClipRepository::HasAnyClips(bool* has_any_clips) const {
  if (has_any_clips == nullptr) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "输出参数不能为空。");
  }
  Statement statement(db_, "SELECT 1 FROM clips LIMIT 1");
  *has_any_clips = sqlite3_step(statement.get()) == SQLITE_ROW;
  return common::Status::Ok();
}

common::Status ClipRepository::HasAnySettingsData(bool* has_any_settings) const {
  if (has_any_settings == nullptr) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "输出参数不能为空。");
  }
  Statement statement(db_, "SELECT 1 FROM settings LIMIT 1");
  *has_any_settings = sqlite3_step(statement.get()) == SQLITE_ROW;
  return common::Status::Ok();
}



common::Status ClipRepository::ListTags(std::vector<TagRecord>* tags) const {
  if (tags == nullptr) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "标签输出列表不能为空。");
  }
  tags->clear();
  Statement statement(db_, "SELECT id, name, color, is_system, sort_order FROM tags ORDER BY is_system ASC, sort_order ASC, name ASC");
  while (sqlite3_step(statement.get()) == SQLITE_ROW) {
    TagRecord tag;
    tag.id = sqlite3_column_int64(statement.get(), 0);
    tag.name = ToWideOrEmpty(ColumnString(statement.get(), 1).c_str());
    tag.color = ToWideOrEmpty(ColumnString(statement.get(), 2).c_str());
    tag.is_system = sqlite3_column_int(statement.get(), 3) != 0;
    tag.sort_order = sqlite3_column_int(statement.get(), 4);
    tags->push_back(std::move(tag));
  }
  return common::Status::Ok();
}

common::Status ClipRepository::ListClipTags(std::int64_t clip_id, std::vector<TagRecord>* tags) const {
  return LoadClipTags(db_, clip_id, tags);
}


common::Status ClipRepository::ListQueue(std::vector<QueueEntry>* entries) const {
  if (entries == nullptr) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "队列输出列表不能为空。");
  }
  entries->clear();
  Statement statement(
      db_,
      "SELECT id, clip_id, sort_order, auto_pop, created_at FROM paste_queue ORDER BY sort_order ASC, created_at ASC");
  while (sqlite3_step(statement.get()) == SQLITE_ROW) {
    QueueEntry entry;
    entry.id = sqlite3_column_int64(statement.get(), 0);
    entry.clip_id = sqlite3_column_int64(statement.get(), 1);
    entry.sort_order = sqlite3_column_int(statement.get(), 2);
    entry.auto_pop = sqlite3_column_int(statement.get(), 3) != 0;
    entry.created_at = sqlite3_column_int64(statement.get(), 4);
    entries->push_back(std::move(entry));
  }
  return common::Status::Ok();
}


common::Status ClipRepository::ListGroups(std::vector<GroupRecord>* groups) const {
  if (groups == nullptr) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "输出分组列表不能为空。");
  }
  groups->clear();

  Statement statement(
      db_,
      "SELECT id, name, sort_order, tab_pinned, tab_sort_order"
      " FROM groups"
      " ORDER BY sort_order ASC, id ASC");
  while (sqlite3_step(statement.get()) == SQLITE_ROW) {
    GroupRecord group;
    group.id = sqlite3_column_int64(statement.get(), 0);
    group.name = ToWideOrEmpty(ColumnString(statement.get(), 1).c_str());
    group.sort_order = sqlite3_column_int(statement.get(), 2);
    group.tab_pinned = sqlite3_column_int(statement.get(), 3) != 0;
    group.tab_sort_order = sqlite3_column_int(statement.get(), 4);
    groups->push_back(std::move(group));
  }
  return common::Status::Ok();
}


common::Status ClipRepository::LoadSettings(AppSettings* settings) const {
  if (settings == nullptr) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "输出设置对象不能为空。");
  }

  Statement statement(db_, "SELECT key, value FROM settings");
  while (sqlite3_step(statement.get()) == SQLITE_ROW) {
    const std::string key = ColumnString(statement.get(), 0);
    const std::string value = ColumnString(statement.get(), 1);
    if (key == "history_limit") {
      settings->history_limit = static_cast<int>(common::ParseInt64(value).value_or(settings->history_limit));
    } else if (key == "history_retention_days") {
      settings->history_retention_days = static_cast<int>(common::ParseInt64(value).value_or(settings->history_retention_days));
    } else if (key == "start_with_windows") {
      settings->start_with_windows = value == "1";
    } else if (key == "open_panel_on_first_run") {
      settings->open_panel_on_first_run = value != "0";
    } else if (key == "has_completed_first_run") {
      settings->has_completed_first_run = value == "1";
    } else if (key == "show_source_icon") {
      settings->show_source_icon = value != "0";
    } else if (key == "monitoring_enabled") {
      settings->monitoring_enabled = value != "0";
    } else if (key == "queue_auto_pop") {
      settings->queue_auto_pop = value != "0";
    } else if (key == "enable_win_v_override") {
      settings->enable_win_v_override = value == "1";
    } else if (key == "enable_window_blur") {
      if (value == "0") settings->window_blur_type = WindowBlurType::None;
    } else if (key == "window_blur_type") {
      try { settings->window_blur_type = static_cast<WindowBlurType>(std::stoi(value)); } catch (...) {}
    } else if (key == "window_blur_opacity") {
      try { settings->window_blur_opacity = std::stoi(value); } catch (...) {}
    } else if (key == "theme") {
      try {
        const int parsed = std::stoi(value);
        if (parsed >= 0 && parsed <= 2) settings->theme = static_cast<ThemeMode>(parsed);
      } catch (...) {}
    } else if (key == "list_density") {
      settings->list_density = ParseListDensity(value);
    } else if (key == "hotkey") {
      settings->hotkey = ToWideOrEmpty(value.c_str());
    } else if (key == "quick_paste_hotkey") {
      settings->quick_paste_hotkey = ToWideOrEmpty(value.c_str());
    } else if (key == "focus_search_on_open") {
      settings->focus_search_on_open = value == "1";
    } else if (key == "search_focus_hotkey") {
      settings->search_focus_hotkey = ToWideOrEmpty(value.c_str());
    } else if (key == "third_party_sticker_enabled") {
      settings->third_party_sticker_enabled = value == "1";
    } else if (key == "third_party_sticker_hotkey") {
      settings->third_party_sticker_hotkey = ToWideOrEmpty(value.c_str());
    } else if (key == "click_paste_mode") {
      settings->click_paste_mode = ParseClickPasteMode(value);
    } else if (key == "promote_pasted_clip_on_reuse") {
      settings->promote_pasted_clip_on_reuse = value == "1";
    } else if (key == "external_editor_path") {
      settings->external_editor_path = ToWideOrEmpty(value.c_str());
    } else if (key == "blacklist") {
      settings->blacklist = ToWideOrEmpty(value.c_str());
    } else if (key == "image_ocr_enabled") {
      settings->image_ocr_enabled = value != "0";
    } else if (key == "ocr_executable_path") {
      settings->ocr_executable_path = ToWideOrEmpty(value.c_str());
    } else if (key == "ocr_language_hint") {
      settings->ocr_language_hint = ToWideOrEmpty(value.c_str());
    } else if (key == "window_position_mode") {
      settings->window_position_mode = ParseWindowPositionMode(value);
    } else if (key == "saved_window_x") {
      settings->saved_window_x = static_cast<int>(common::ParseInt64(value).value_or(settings->saved_window_x));
    } else if (key == "saved_window_y") {
      settings->saved_window_y = static_cast<int>(common::ParseInt64(value).value_or(settings->saved_window_y));
    } else if (key == "saved_window_width") {
      settings->saved_window_width = static_cast<int>(common::ParseInt64(value).value_or(settings->saved_window_width));
    } else if (key == "saved_window_height") {
      settings->saved_window_height = static_cast<int>(common::ParseInt64(value).value_or(settings->saved_window_height));
    } else if (key == "has_saved_window_position") {
      settings->has_saved_window_position = value == "1";
    } else if (key == "has_saved_window_size") {
      settings->has_saved_window_size = value == "1";
    } else if (key == "auto_ignore_password_managers") {
      settings->auto_ignore_password_managers = value != "0";
    } else if (key == "custom_blacklist_apps") {
      settings->custom_blacklist_apps = ToWideOrEmpty(value.c_str());
    } else if (key == "ignore_large_text") {
      settings->ignore_large_text = value != "0";
    } else if (key == "max_text_size_kb") {
      settings->max_text_size_kb = static_cast<int>(common::ParseInt64(value).value_or(settings->max_text_size_kb));
    } else if (key == "ignore_large_images") {
      settings->ignore_large_images = value != "0";
    } else if (key == "max_image_size_kb") {
      settings->max_image_size_kb = static_cast<int>(common::ParseInt64(value).value_or(settings->max_image_size_kb));
    } else if (key == "ignore_otp_codes") {
      settings->ignore_otp_codes = value != "0";
    } else if (key == "ignore_complex_passwords") {
      settings->ignore_complex_passwords = value == "1";
    } else if (key == "screenshot_enabled") {
      settings->screenshot_enabled = value != "0";
    } else if (key == "screenshot_prewarm") {
      settings->screenshot_prewarm = value != "0";
    } else if (key == "screenshot_hotkey") {
      settings->screenshot_hotkey = ToWideOrEmpty(value.c_str());
    } else if (key == "enable_scroll_blur") {
      settings->enable_scroll_blur = value == "1";
    }
  }
  return common::Status::Ok();
}

}  // namespace clipd2d::core::store
