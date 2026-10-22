#include "core/store/ClipRepository.Internal.h"

namespace clipd2d::core::store {

common::Status ClipRepository::InsertClip(
    const CapturedClip& clip,
    ReuseTouchPolicy touch_policy,
    bool* inserted,
    std::int64_t* clip_id) {
  if (inserted != nullptr) {
    *inserted = false;
  }
  if (clip_id != nullptr) {
    *clip_id = 0;
  }
  const bool is_text_like =
      clip.content_type == ClipContentType::Text ||
      clip.content_type == ClipContentType::Html ||
      clip.content_type == ClipContentType::Rtf;
  if (is_text_like && clip.plain_text.empty() && clip.html_text.empty() && clip.rtf_text.empty()) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "Empty text payload clip is invalid.");
  }
  const std::int64_t now = NowEpochMs();
  const std::vector<ClipFormatPayload> formats = FormatsForPersistence(clip);
  const ClipIdentityData identity = BuildClipIdentityData(clip);
  ScopedTransaction transaction(db_);
  if (!transaction.status().ok()) {
    return transaction.status();
  }

  auto resolve_existing_id = [&]() -> std::optional<std::int64_t> {
    const auto candidates = FindCandidatesByIdentity(db_, identity.kind, identity.key);
    if (!candidates.empty()) {
      return candidates.front().id;
    }

    switch (identity.kind) {
      case ClipIdentityKind::TextTitlePlain:
      case ClipIdentityKind::TextPayloadSet:
        return FindLegacyTextCandidate(db_, clip, identity);
      case ClipIdentityKind::ImageFilePath:
      case ClipIdentityKind::ImageFileSnapshot:
        return FindLegacyImageFileCandidate(db_, clip, identity);
      case ClipIdentityKind::ImageContentHash:
      case ClipIdentityKind::LegacyPayload:
        if (!clip.content_hash.empty()) {
          const auto existing_id = FindExistingByHash(clip.content_hash);
          if (existing_id.has_value() &&
              (identity.kind != ClipIdentityKind::LegacyPayload || !identity.key.empty())) {
            const common::Status backfill_status = UpdateClipIdentity(db_, *existing_id, identity.kind, identity.key, identity.version);
            if (backfill_status.ok()) {
              return existing_id;
            }
          }
          return existing_id;
        }
        return std::nullopt;
    }
    return std::nullopt;
  };

  if (const auto existing_id = resolve_existing_id(); existing_id.has_value()) {
    if (touch_policy == ReuseTouchPolicy::PromoteOnReuse) {
      Statement update_statement(
          db_,
          "UPDATE clips SET"
          " title = ?1,"
          " plain_text = ?2,"
          " html_text = ?3,"
          " rtf_text = ?4,"
          " content_type = ?5,"
          " storage_path = ?6,"
          " secondary_path = ?7,"
          " source_app_name = ?8,"
          " source_app_path = ?9,"
          " source_window_title = ?10,"
          " image_width = ?11,"
          " image_height = ?12,"
          " estimated_decoded_bytes = ?13,"
          " is_large_image = ?14,"
          " ingest_state = ?15,"
          " ingest_error = ?16,"
          " thumbnail_path = ?17,"
          " preview_path = ?18,"
          " content_hash = ?19,"
          " identity_kind = ?20,"
          " identity_key = ?21,"
          " identity_version = ?22,"
          " updated_at = ?23"
          " WHERE id = ?24");
      BindText(update_statement.get(), 1, clip.title);
      BindText(update_statement.get(), 2, clip.plain_text);
      BindText(update_statement.get(), 3, clip.html_text);
      BindText(update_statement.get(), 4, clip.rtf_text);
      BindText(update_statement.get(), 5, ToSqlContentType(clip.content_type));
      BindWide(update_statement.get(), 6, clip.storage_path);
      BindWide(update_statement.get(), 7, clip.secondary_path);
      BindWide(update_statement.get(), 8, clip.source_app.process_name);
      BindWide(update_statement.get(), 9, clip.source_app.process_path);
      BindWide(update_statement.get(), 10, clip.source_app.window_title);
      sqlite3_bind_int(update_statement.get(), 11, clip.image_width);
      sqlite3_bind_int(update_statement.get(), 12, clip.image_height);
      sqlite3_bind_int64(update_statement.get(), 13, clip.estimated_decoded_bytes);
      sqlite3_bind_int(update_statement.get(), 14, clip.is_large_image ? 1 : 0);
      BindText(update_statement.get(), 15, clip.ingest_state);
      BindText(update_statement.get(), 16, clip.ingest_error);
      BindWide(update_statement.get(), 17, clip.thumbnail_path);
      BindWide(update_statement.get(), 18, clip.preview_path);
      BindText(update_statement.get(), 19, clip.content_hash);
      BindText(update_statement.get(), 20, ToSqlIdentityKind(identity.kind));
      BindText(update_statement.get(), 21, identity.key);
      sqlite3_bind_int(update_statement.get(), 22, identity.version);
      sqlite3_bind_int64(update_statement.get(), 23, now);
      sqlite3_bind_int64(update_statement.get(), 24, *existing_id);
      if (sqlite3_step(update_statement.get()) != SQLITE_DONE) {
        return SqliteError(db_, "更新重复剪贴板条目失败。");
      }
    } else {
      Statement update_statement(
          db_,
          "UPDATE clips SET"
          " title = ?1,"
          " plain_text = ?2,"
          " html_text = ?3,"
          " rtf_text = ?4,"
          " content_type = ?5,"
          " storage_path = ?6,"
          " secondary_path = ?7,"
          " source_app_name = ?8,"
          " source_app_path = ?9,"
          " source_window_title = ?10,"
          " image_width = ?11,"
          " image_height = ?12,"
          " estimated_decoded_bytes = ?13,"
          " is_large_image = ?14,"
          " ingest_state = ?15,"
          " ingest_error = ?16,"
          " thumbnail_path = ?17,"
          " preview_path = ?18,"
          " content_hash = ?19,"
          " identity_kind = ?20,"
          " identity_key = ?21,"
          " identity_version = ?22"
          " WHERE id = ?23");
      BindText(update_statement.get(), 1, clip.title);
      BindText(update_statement.get(), 2, clip.plain_text);
      BindText(update_statement.get(), 3, clip.html_text);
      BindText(update_statement.get(), 4, clip.rtf_text);
      BindText(update_statement.get(), 5, ToSqlContentType(clip.content_type));
      BindWide(update_statement.get(), 6, clip.storage_path);
      BindWide(update_statement.get(), 7, clip.secondary_path);
      BindWide(update_statement.get(), 8, clip.source_app.process_name);
      BindWide(update_statement.get(), 9, clip.source_app.process_path);
      BindWide(update_statement.get(), 10, clip.source_app.window_title);
      sqlite3_bind_int(update_statement.get(), 11, clip.image_width);
      sqlite3_bind_int(update_statement.get(), 12, clip.image_height);
      sqlite3_bind_int64(update_statement.get(), 13, clip.estimated_decoded_bytes);
      sqlite3_bind_int(update_statement.get(), 14, clip.is_large_image ? 1 : 0);
      BindText(update_statement.get(), 15, clip.ingest_state);
      BindText(update_statement.get(), 16, clip.ingest_error);
      BindWide(update_statement.get(), 17, clip.thumbnail_path);
      BindWide(update_statement.get(), 18, clip.preview_path);
      BindText(update_statement.get(), 19, clip.content_hash);
      BindText(update_statement.get(), 20, ToSqlIdentityKind(identity.kind));
      BindText(update_statement.get(), 21, identity.key);
      sqlite3_bind_int(update_statement.get(), 22, identity.version);
      sqlite3_bind_int64(update_statement.get(), 23, *existing_id);
      if (sqlite3_step(update_statement.get()) != SQLITE_DONE) {
        return SqliteError(db_, "更新重复剪贴板条目失败。");
      }
    }
#if defined(SCRAPS_ENABLE_TEST_HOOKS)
    if (const common::Status fail_status = MaybeFailForTests(
            TestFailPoint::AfterClipWriteBeforeFormats,
            "test failpoint after duplicate clip write");
        !fail_status.ok()) {
      return fail_status;
    }
#endif

    Statement delete_formats(db_, "DELETE FROM clip_formats WHERE clip_id = ?1");
    sqlite3_bind_int64(delete_formats.get(), 1, *existing_id);
    if (sqlite3_step(delete_formats.get()) != SQLITE_DONE) {
      return SqliteError(db_, "删除旧格式集合失败。");
    }

    common::Status status = InsertClipFormats(db_, *existing_id, formats);
    if (!status.ok()) {
      return status;
    }
    // CONSTRAINT: UpsertFts replaces the whole clips_fts row - carry the stored
    // OCR text forward so a dedupe reuse does not wipe the OCR search index.
    std::string existing_ocr_text;
    {
      Statement ocr_statement(db_, "SELECT ocr_text FROM clips WHERE id = ?1");
      sqlite3_bind_int64(ocr_statement.get(), 1, *existing_id);
      if (sqlite3_step(ocr_statement.get()) == SQLITE_ROW) {
        existing_ocr_text = ColumnString(ocr_statement.get(), 0);
      }
    }
    status = UpsertFts(*existing_id, clip.title, SearchableProjection(clip), existing_ocr_text);
    if (!status.ok()) {
      return status;
    }
    status = UpdateStorageTierForClip(*existing_id);
    if (!status.ok()) {
      return status;
    }
    if (clip_id != nullptr) {
      *clip_id = *existing_id;
    }
    if (inserted != nullptr) {
      *inserted = true;
    }
    const common::Status commit_status = transaction.Commit();
    if (!commit_status.ok()) {
      return commit_status;
    }
    return common::Status::Ok();
  }

  Statement statement(
      db_,
      "INSERT INTO clips("
      " title, plain_text, html_text, rtf_text, content_type, storage_path, secondary_path,"
      " source_app_name, source_app_path, source_window_title, is_pinned, is_favorite,"
      " group_id, storage_tier, paste_count, last_pasted_at, favorite_sort_order,"
      " image_width, image_height, estimated_decoded_bytes, is_large_image, ingest_state, ingest_error,"
      " thumbnail_path, preview_path, identity_kind, identity_key, identity_version, content_hash, created_at, updated_at"
      " ) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, 0, 0, 0, 'history', 0, 0, 0, ?11, ?12, ?13, ?14, ?15, ?16, ?17, ?18, ?19, ?20, ?21, ?22, ?23, ?23)");
  BindText(statement.get(), 1, clip.title);
  BindText(statement.get(), 2, clip.plain_text);
  BindText(statement.get(), 3, clip.html_text);
  BindText(statement.get(), 4, clip.rtf_text);
  BindText(statement.get(), 5, ToSqlContentType(clip.content_type));
  BindWide(statement.get(), 6, clip.storage_path);
  BindWide(statement.get(), 7, clip.secondary_path);
  BindWide(statement.get(), 8, clip.source_app.process_name);
  BindWide(statement.get(), 9, clip.source_app.process_path);
  BindWide(statement.get(), 10, clip.source_app.window_title);
  sqlite3_bind_int(statement.get(), 11, clip.image_width);
  sqlite3_bind_int(statement.get(), 12, clip.image_height);
  sqlite3_bind_int64(statement.get(), 13, clip.estimated_decoded_bytes);
  sqlite3_bind_int(statement.get(), 14, clip.is_large_image ? 1 : 0);
  BindText(statement.get(), 15, clip.ingest_state);
  BindText(statement.get(), 16, clip.ingest_error);
  BindWide(statement.get(), 17, clip.thumbnail_path);
  BindWide(statement.get(), 18, clip.preview_path);
  BindText(statement.get(), 19, ToSqlIdentityKind(identity.kind));
  BindText(statement.get(), 20, identity.key);
  sqlite3_bind_int(statement.get(), 21, identity.version);
  BindText(statement.get(), 22, clip.content_hash);
  sqlite3_bind_int64(statement.get(), 23, now);
  if (sqlite3_step(statement.get()) != SQLITE_DONE) {
    return SqliteError(db_, "插入剪贴板记录失败。");
  }
#if defined(SCRAPS_ENABLE_TEST_HOOKS)
  if (const common::Status fail_status = MaybeFailForTests(
          TestFailPoint::AfterClipWriteBeforeFormats,
          "test failpoint after clip insert");
      !fail_status.ok()) {
    return fail_status;
  }
#endif

  const std::int64_t inserted_clip_id = sqlite3_last_insert_rowid(db_);
  common::Status status = InsertClipFormats(db_, inserted_clip_id, formats);
  if (!status.ok()) {
    return status;
  }
  status = UpsertFts(inserted_clip_id, clip.title, SearchableProjection(clip), {});
  if (!status.ok()) {
    return status;
  }
  status = UpdateStorageTierForClip(inserted_clip_id);
  if (!status.ok()) {
    return status;
  }
  if (clip_id != nullptr) {
    *clip_id = inserted_clip_id;
  }
  if (inserted != nullptr) {
    *inserted = true;
  }
  const common::Status commit_status = transaction.Commit();
  if (!commit_status.ok()) {
    return commit_status;
  }
  return common::Status::Ok();
}


common::Status ClipRepository::SetPinned(std::int64_t id, bool pinned) {
  int next_sort_order = 0;
  if (pinned) {
    Statement max_stmt(db_, "SELECT COALESCE(MAX(pin_sort_order), 0) FROM clips WHERE is_pinned = 1");
    if (sqlite3_step(max_stmt.get()) == SQLITE_ROW) {
      next_sort_order = sqlite3_column_int(max_stmt.get(), 0) + 1;
    }
  }
  Statement statement(
      db_,
      "UPDATE clips SET is_pinned = ?1, pin_sort_order = CASE WHEN ?1 = 1 THEN ?2 ELSE 0 END WHERE id = ?3");
  sqlite3_bind_int(statement.get(), 1, pinned ? 1 : 0);
  sqlite3_bind_int(statement.get(), 2, next_sort_order);
  sqlite3_bind_int64(statement.get(), 3, id);
  if (sqlite3_step(statement.get()) != SQLITE_DONE) {
    return SqliteError(db_, "更新置顶状态失败。");
  }
  common::Status status = UpdateStorageTierForClip(id);
  if (!status.ok()) {
    return status;
  }
  return pinned ? PromoteFileClipToSnapshot(id) : common::Status::Ok();
}


common::Status ClipRepository::SetFavorite(std::int64_t id, bool favorite) {
  int next_sort_order = 0;
  if (favorite) {
    Statement min_stmt(db_, "SELECT COALESCE(MIN(favorite_sort_order), 0) FROM clips WHERE is_favorite = 1");
    if (sqlite3_step(min_stmt.get()) == SQLITE_ROW) {
      next_sort_order = sqlite3_column_int(min_stmt.get(), 0) - 1;
    }
  }
  Statement statement(
      db_,
      "UPDATE clips SET is_favorite = ?1, favorite_sort_order = CASE WHEN ?1 = 1 THEN ?2 ELSE 0 END WHERE id = ?3");
  sqlite3_bind_int(statement.get(), 1, favorite ? 1 : 0);
  sqlite3_bind_int(statement.get(), 2, next_sort_order);
  sqlite3_bind_int64(statement.get(), 3, id);
  if (sqlite3_step(statement.get()) != SQLITE_DONE) {
    return SqliteError(db_, "更新收藏状态失败");
  }
  common::Status status = UpdateStorageTierForClip(id);
  if (!status.ok()) {
    return status;
  }
  return favorite ? PromoteFileClipToSnapshot(id) : common::Status::Ok();
}

common::Status ClipRepository::SetStorageTier(std::int64_t id, StorageTier tier) {
  Statement statement(
      db_,
      tier == StorageTier::History
          ? "UPDATE clips SET storage_tier = ?1, group_id = 0, group_sort_order = 0 WHERE id = ?2"
          : "UPDATE clips SET storage_tier = ?1 WHERE id = ?2");
  BindText(statement.get(), 1, ToSettingsValue(tier));
  sqlite3_bind_int64(statement.get(), 2, id);
  if (sqlite3_step(statement.get()) != SQLITE_DONE) {
    return SqliteError(db_, "Update storage tier failed");
  }
  return UpdateStorageTierForClip(id);
}

common::Status ClipRepository::SetGroup(std::int64_t id, std::int64_t group_id) {
  int next_group_sort_order = 0;
  if (group_id > 0) {
    Statement min_stmt(db_, "SELECT COALESCE(MIN(group_sort_order), 0) FROM clips WHERE group_id = ?1");
    sqlite3_bind_int64(min_stmt.get(), 1, group_id);
    if (sqlite3_step(min_stmt.get()) == SQLITE_ROW) {
      next_group_sort_order = sqlite3_column_int(min_stmt.get(), 0) - 1;
    }
  }
  Statement statement(
      db_,
      "UPDATE clips SET group_id = ?1, group_sort_order = ?2, storage_tier = CASE WHEN ?1 > 0 THEN 'library' ELSE storage_tier END WHERE id = ?3");
  sqlite3_bind_int64(statement.get(), 1, group_id);
  sqlite3_bind_int(statement.get(), 2, group_id > 0 ? next_group_sort_order : 0);
  sqlite3_bind_int64(statement.get(), 3, id);
  if (sqlite3_step(statement.get()) != SQLITE_DONE) {
    return SqliteError(db_, "更新分组失败");
  }
  common::Status status = UpdateStorageTierForClip(id);
  if (!status.ok()) {
    return status;
  }
  return group_id > 0 ? PromoteFileClipToSnapshot(id) : common::Status::Ok();
}

common::Status ClipRepository::EnsureTag(
    const std::wstring& name,
    const std::wstring& color,
    std::int64_t* tag_id) {
  const std::wstring trimmed = common::TrimWide(name);
  if (trimmed.empty()) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "标签名称不能为空。");
  }
  {
    Statement select_stmt(db_, "SELECT id FROM tags WHERE name = ?1");
    BindWide(select_stmt.get(), 1, trimmed);
    if (sqlite3_step(select_stmt.get()) == SQLITE_ROW) {
      if (tag_id != nullptr) {
        *tag_id = sqlite3_column_int64(select_stmt.get(), 0);
      }
      return common::Status::Ok();
    }
  }

  int next_sort_order = 1;
  {
    Statement max_stmt(db_, "SELECT COALESCE(MAX(sort_order), 0) FROM tags");
    if (sqlite3_step(max_stmt.get()) == SQLITE_ROW) {
      next_sort_order = sqlite3_column_int(max_stmt.get(), 0) + 1;
    }
  }
  Statement insert_stmt(
      db_,
      "INSERT INTO tags(name, color, is_system, sort_order, created_at) VALUES(?1, ?2, 0, ?3, ?4)");
  BindWide(insert_stmt.get(), 1, trimmed);
  BindWide(insert_stmt.get(), 2, color.empty() ? std::wstring(L"#4F8CFF") : color);
  sqlite3_bind_int(insert_stmt.get(), 3, next_sort_order);
  sqlite3_bind_int64(insert_stmt.get(), 4, NowEpochMs());
  if (sqlite3_step(insert_stmt.get()) != SQLITE_DONE) {
    return SqliteError(db_, "创建标签失败");
  }
  if (tag_id != nullptr) {
    *tag_id = sqlite3_last_insert_rowid(db_);
  }
  return common::Status::Ok();
}

common::Status ClipRepository::RenameTag(
    std::int64_t tag_id,
    const std::wstring& name,
    const std::wstring& color) {
  const std::wstring trimmed = common::TrimWide(name);
  if (trimmed.empty()) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "标签名称不能为空。");
  }
  Statement statement(db_, "UPDATE tags SET name = ?1, color = ?2 WHERE id = ?3 AND is_system = 0");
  BindWide(statement.get(), 1, trimmed);
  BindWide(statement.get(), 2, color.empty() ? L"#3B82F6" : color);
  sqlite3_bind_int64(statement.get(), 3, tag_id);
  if (sqlite3_step(statement.get()) != SQLITE_DONE) {
    return SqliteError(db_, "重命名标签失败");
  }
  if (sqlite3_changes(db_) == 0) {
    return common::Status::Error(common::StatusCode::NotFound, "标签不存在。");
  }
  return common::Status::Ok();
}

common::Status ClipRepository::DeleteTag(std::int64_t tag_id) {
  std::vector<std::int64_t> clip_ids;
  {
    Statement select_stmt(db_, "SELECT clip_id FROM clip_tags WHERE tag_id = ?1");
    sqlite3_bind_int64(select_stmt.get(), 1, tag_id);
    while (sqlite3_step(select_stmt.get()) == SQLITE_ROW) {
      clip_ids.push_back(sqlite3_column_int64(select_stmt.get(), 0));
    }
  }

  Statement delete_links(db_, "DELETE FROM clip_tags WHERE tag_id = ?1");
  sqlite3_bind_int64(delete_links.get(), 1, tag_id);
  if (sqlite3_step(delete_links.get()) != SQLITE_DONE) {
    return SqliteError(db_, "删除标签关联失败");
  }

  Statement delete_tag(db_, "DELETE FROM tags WHERE id = ?1 AND is_system = 0");
  sqlite3_bind_int64(delete_tag.get(), 1, tag_id);
  if (sqlite3_step(delete_tag.get()) != SQLITE_DONE) {
    return SqliteError(db_, "删除标签失败");
  }

  for (const auto clip_id : clip_ids) {
    common::Status status = UpdateStorageTierForClip(clip_id);
    if (!status.ok()) {
      return status;
    }
  }
  return common::Status::Ok();
}


common::Status ClipRepository::AddTag(std::int64_t clip_id, std::int64_t tag_id) {
  int next_sort_order = 1;
  {
    Statement min_stmt(db_, "SELECT COALESCE(MIN(sort_order), 0) FROM clip_tags WHERE tag_id = ?1");
    sqlite3_bind_int64(min_stmt.get(), 1, tag_id);
    if (sqlite3_step(min_stmt.get()) == SQLITE_ROW) {
      next_sort_order = sqlite3_column_int(min_stmt.get(), 0) - 1;
    }
  }
  Statement statement(
      db_,
      "INSERT INTO clip_tags(clip_id, tag_id, sort_order, created_at)"
      " VALUES(?1, ?2, ?3, ?4)"
      " ON CONFLICT(clip_id, tag_id) DO UPDATE SET sort_order = excluded.sort_order");
  sqlite3_bind_int64(statement.get(), 1, clip_id);
  sqlite3_bind_int64(statement.get(), 2, tag_id);
  sqlite3_bind_int(statement.get(), 3, next_sort_order);
  sqlite3_bind_int64(statement.get(), 4, NowEpochMs());
  if (sqlite3_step(statement.get()) != SQLITE_DONE) {
    return SqliteError(db_, "添加标签失败");
  }
  return UpdateStorageTierForClip(clip_id);
}

common::Status ClipRepository::RemoveTag(std::int64_t clip_id, std::int64_t tag_id) {
  Statement statement(db_, "DELETE FROM clip_tags WHERE clip_id = ?1 AND tag_id = ?2");
  sqlite3_bind_int64(statement.get(), 1, clip_id);
  sqlite3_bind_int64(statement.get(), 2, tag_id);
  if (sqlite3_step(statement.get()) != SQLITE_DONE) {
    return SqliteError(db_, "移除标签失败");
  }
  return UpdateStorageTierForClip(clip_id);
}

common::Status ClipRepository::AddToQueue(std::int64_t clip_id, bool auto_pop) {
  int next_sort_order = 1;
  {
    Statement max_stmt(db_, "SELECT COALESCE(MAX(sort_order), 0) FROM paste_queue");
    if (sqlite3_step(max_stmt.get()) == SQLITE_ROW) {
      next_sort_order = sqlite3_column_int(max_stmt.get(), 0) + 1;
    }
  }
  Statement statement(
      db_,
      "INSERT INTO paste_queue(clip_id, sort_order, auto_pop, created_at)"
      " VALUES(?1, ?2, ?3, ?4)"
      " ON CONFLICT(clip_id) DO UPDATE SET sort_order = excluded.sort_order, auto_pop = excluded.auto_pop");
  sqlite3_bind_int64(statement.get(), 1, clip_id);
  sqlite3_bind_int(statement.get(), 2, next_sort_order);
  sqlite3_bind_int(statement.get(), 3, auto_pop ? 1 : 0);
  sqlite3_bind_int64(statement.get(), 4, NowEpochMs());
  if (sqlite3_step(statement.get()) != SQLITE_DONE) {
    return SqliteError(db_, "加入队列失败");
  }
  return common::Status::Ok();
}

common::Status ClipRepository::RemoveFromQueue(std::int64_t clip_id) {
  Statement statement(db_, "DELETE FROM paste_queue WHERE clip_id = ?1");
  sqlite3_bind_int64(statement.get(), 1, clip_id);
  if (sqlite3_step(statement.get()) != SQLITE_DONE) {
    return SqliteError(db_, "移出队列失败");
  }
  return common::Status::Ok();
}


common::Status ClipRepository::ReorderQueue(std::int64_t clip_id, int new_sort_order) {
  Statement statement(db_, "UPDATE paste_queue SET sort_order = ?1 WHERE clip_id = ?2");
  sqlite3_bind_int(statement.get(), 1, new_sort_order);
  sqlite3_bind_int64(statement.get(), 2, clip_id);
  if (sqlite3_step(statement.get()) != SQLITE_DONE) {
    return SqliteError(db_, "调整队列顺序失败");
  }
  return common::Status::Ok();
}

common::Status ClipRepository::ReorderFavorites(std::int64_t clip_id, int new_sort_order) {
  Statement statement(db_, "UPDATE clips SET favorite_sort_order = ?1 WHERE id = ?2 AND is_favorite = 1");
  sqlite3_bind_int(statement.get(), 1, new_sort_order);
  sqlite3_bind_int64(statement.get(), 2, clip_id);
  if (sqlite3_step(statement.get()) != SQLITE_DONE) {
    return SqliteError(db_, "调整收藏顺序失败");
  }
  return common::Status::Ok();
}

common::Status ClipRepository::ReorderPinned(std::int64_t clip_id, int new_sort_order) {
  Statement statement(db_, "UPDATE clips SET pin_sort_order = ?1 WHERE id = ?2 AND is_pinned = 1");
  sqlite3_bind_int(statement.get(), 1, new_sort_order);
  sqlite3_bind_int64(statement.get(), 2, clip_id);
  if (sqlite3_step(statement.get()) != SQLITE_DONE) {
    return SqliteError(db_, "调整置顶顺序失败");
  }
  return common::Status::Ok();
}

common::Status ClipRepository::ReorderTagItems(std::int64_t tag_id, std::int64_t clip_id, int new_sort_order) {
  Statement statement(db_, "UPDATE clip_tags SET sort_order = ?1 WHERE tag_id = ?2 AND clip_id = ?3");
  sqlite3_bind_int(statement.get(), 1, new_sort_order);
  sqlite3_bind_int64(statement.get(), 2, tag_id);
  sqlite3_bind_int64(statement.get(), 3, clip_id);
  if (sqlite3_step(statement.get()) != SQLITE_DONE) {
    return SqliteError(db_, "调整标签排序失败");
  }
  return common::Status::Ok();
}

common::Status ClipRepository::MoveQueueToFront(std::int64_t clip_id, bool auto_pop) {
  int min_sort_order = 0;
  {
    Statement min_stmt(db_, "SELECT COALESCE(MIN(sort_order), 0) FROM paste_queue");
    if (sqlite3_step(min_stmt.get()) == SQLITE_ROW) {
      min_sort_order = sqlite3_column_int(min_stmt.get(), 0);
    }
  }

  Statement statement(
      db_,
      "INSERT INTO paste_queue(clip_id, sort_order, auto_pop, created_at)"
      " VALUES(?1, ?2, ?3, ?4)"
      " ON CONFLICT(clip_id) DO UPDATE SET sort_order = excluded.sort_order, auto_pop = excluded.auto_pop");
  sqlite3_bind_int64(statement.get(), 1, clip_id);
  sqlite3_bind_int(statement.get(), 2, min_sort_order - 1);
  sqlite3_bind_int(statement.get(), 3, auto_pop ? 1 : 0);
  sqlite3_bind_int64(statement.get(), 4, NowEpochMs());
  if (sqlite3_step(statement.get()) != SQLITE_DONE) {
    return SqliteError(db_, "设置下一项失败");
  }
  return common::Status::Ok();
}

common::Status ClipRepository::MarkPasted(std::int64_t id, bool promote_on_paste) {
  const std::int64_t now = NowEpochMs();
  Statement statement(
      db_,
      promote_on_paste
          ? "UPDATE clips SET paste_count = paste_count + 1, last_pasted_at = ?1, updated_at = ?1 WHERE id = ?2"
          : "UPDATE clips SET paste_count = paste_count + 1, last_pasted_at = ?1 WHERE id = ?2");
  sqlite3_bind_int64(statement.get(), 1, now);
  sqlite3_bind_int64(statement.get(), 2, id);
  if (sqlite3_step(statement.get()) != SQLITE_DONE) {
    return SqliteError(db_, "更新粘贴统计失败");
  }
  return common::Status::Ok();
}

common::Status ClipRepository::DeleteClip(std::int64_t id) {
  const auto storage_paths = CollectStoragePaths(db_, id);

  Statement delete_fts(db_, "DELETE FROM clips_fts WHERE rowid = ?1");
  sqlite3_bind_int64(delete_fts.get(), 1, id);
  sqlite3_step(delete_fts.get());

  Statement delete_clip(db_, "DELETE FROM clips WHERE id = ?1");
  sqlite3_bind_int64(delete_clip.get(), 1, id);
  if (sqlite3_step(delete_clip.get()) != SQLITE_DONE) {
    return SqliteError(db_, "删除条目失败");
  }

  for (const auto& path : storage_paths) {
    if (IsManagedAssetPath(app_paths_, path)) {
      DeleteStorageFile(path);
    }
  }
  return common::Status::Ok();
}


common::Status ClipRepository::ClearHistory(int keep_limit, int retention_days) {
  std::set<std::int64_t> clip_ids_to_delete;

  if (keep_limit > 0) {
    Statement keep_stmt(
        db_,
        "SELECT id FROM clips "
        "WHERE is_pinned = 0 AND storage_tier = 'history' "
        "AND id NOT IN ("
        "  SELECT id FROM clips "
        "  WHERE is_pinned = 0 AND storage_tier = 'history' "
        "  ORDER BY updated_at DESC LIMIT ?1)");
    sqlite3_bind_int(keep_stmt.get(), 1, keep_limit);
    while (sqlite3_step(keep_stmt.get()) == SQLITE_ROW) {
      clip_ids_to_delete.insert(sqlite3_column_int64(keep_stmt.get(), 0));
    }
  }

  if (retention_days > 0) {
    const std::int64_t retention_since =
        NowEpochMs() - static_cast<std::int64_t>(retention_days) * 24LL * 60LL * 60LL * 1000LL;
    Statement retention_stmt(
        db_,
        "SELECT id FROM clips "
        "WHERE is_pinned = 0 AND storage_tier = 'history' AND updated_at < ?1");
    sqlite3_bind_int64(retention_stmt.get(), 1, retention_since);
    while (sqlite3_step(retention_stmt.get()) == SQLITE_ROW) {
      clip_ids_to_delete.insert(sqlite3_column_int64(retention_stmt.get(), 0));
    }
  }

  for (const auto clip_id : clip_ids_to_delete) {
    common::Status status = DeleteClip(clip_id);
    if (!status.ok()) {
      return status;
    }
  }

  return common::Status::Ok();
}

common::Status ClipRepository::ClearHistoryArea() {
  std::vector<std::int64_t> clip_ids;
  {
    Statement select_stmt(
        db_,
        "SELECT id FROM clips WHERE is_pinned = 0 AND storage_tier = 'history'");
    while (sqlite3_step(select_stmt.get()) == SQLITE_ROW) {
      clip_ids.push_back(sqlite3_column_int64(select_stmt.get(), 0));
    }
  }

  for (const auto clip_id : clip_ids) {
    common::Status status = DeleteClip(clip_id);
    if (!status.ok()) {
      return status;
    }
  }
  return common::Status::Ok();
}


common::Status ClipRepository::EnsureGroup(const std::wstring& name, std::int64_t* group_id) {
  const std::wstring trimmed = common::TrimWide(name);
  if (trimmed.empty()) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "分组名不能为空。");
  }

  {
    Statement select_stmt(db_, "SELECT id FROM groups WHERE name = ?1");
    BindWide(select_stmt.get(), 1, trimmed);
    if (sqlite3_step(select_stmt.get()) == SQLITE_ROW) {
      if (group_id != nullptr) {
        *group_id = sqlite3_column_int64(select_stmt.get(), 0);
      }
      return common::Status::Ok();
    }
  }

  int next_sort_order = 10;
  {
    Statement max_stmt(db_, "SELECT COALESCE(MAX(sort_order), 0) FROM groups");
    if (sqlite3_step(max_stmt.get()) == SQLITE_ROW) {
      next_sort_order = sqlite3_column_int(max_stmt.get(), 0) + 10;
    }
  }

  Statement insert_stmt(db_, "INSERT INTO groups(name, sort_order) VALUES(?1, ?2)");
  BindWide(insert_stmt.get(), 1, trimmed);
  sqlite3_bind_int(insert_stmt.get(), 2, next_sort_order);
  if (sqlite3_step(insert_stmt.get()) != SQLITE_DONE) {
    return SqliteError(db_, "创建分组失败");
  }
  if (group_id != nullptr) {
    *group_id = sqlite3_last_insert_rowid(db_);
  }
  return common::Status::Ok();
}

common::Status ClipRepository::RenameGroup(std::int64_t group_id, const std::wstring& name) {
  if (group_id <= 0) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "Invalid group id.");
  }
  const std::wstring trimmed = common::TrimWide(name);
  if (trimmed.empty()) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "Group name cannot be empty.");
  }

  Statement update_stmt(db_, "UPDATE groups SET name = ?1 WHERE id = ?2");
  BindWide(update_stmt.get(), 1, trimmed);
  sqlite3_bind_int64(update_stmt.get(), 2, group_id);
  if (sqlite3_step(update_stmt.get()) != SQLITE_DONE) {
    return SqliteError(db_, "Rename group failed.");
  }
  return common::Status::Ok();
}

common::Status ClipRepository::DeleteGroup(std::int64_t group_id) {
  if (group_id <= 0) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "Invalid group id.");
  }

  ScopedTransaction transaction(db_);
  if (!transaction.status().ok()) {
    return transaction.status();
  }

  Statement clear_group_stmt(
      db_,
      "UPDATE clips SET group_id = 0, group_sort_order = 0 WHERE group_id = ?1");
  sqlite3_bind_int64(clear_group_stmt.get(), 1, group_id);
  if (sqlite3_step(clear_group_stmt.get()) != SQLITE_DONE) {
    return SqliteError(db_, "Clear clips from group failed.");
  }

  Statement delete_group_stmt(db_, "DELETE FROM groups WHERE id = ?1");
  sqlite3_bind_int64(delete_group_stmt.get(), 1, group_id);
  if (sqlite3_step(delete_group_stmt.get()) != SQLITE_DONE) {
    return SqliteError(db_, "Delete group failed.");
  }

  return transaction.Commit();
}

common::Status ClipRepository::ReorderGroup(std::int64_t group_id, int new_sort_order) {
  if (group_id <= 0) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "Invalid group id.");
  }

  Statement statement(db_, "UPDATE groups SET sort_order = ?1 WHERE id = ?2");
  sqlite3_bind_int(statement.get(), 1, new_sort_order);
  sqlite3_bind_int64(statement.get(), 2, group_id);
  if (sqlite3_step(statement.get()) != SQLITE_DONE) {
    return SqliteError(db_, "Reorder group failed.");
  }
  return common::Status::Ok();
}

common::Status ClipRepository::SetGroupTabPinned(std::int64_t group_id, bool pinned, int tab_sort_order) {
  if (group_id <= 0) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "Invalid group id.");
  }

  int resolved_tab_sort_order = pinned ? tab_sort_order : 0;
  if (pinned && resolved_tab_sort_order <= 0) {
    Statement max_stmt(db_, "SELECT COALESCE(MAX(tab_sort_order), 0) FROM groups WHERE tab_pinned = 1");
    if (sqlite3_step(max_stmt.get()) == SQLITE_ROW) {
      resolved_tab_sort_order = sqlite3_column_int(max_stmt.get(), 0) + 10;
    } else {
      resolved_tab_sort_order = 10;
    }
  }

  Statement statement(
      db_,
      "UPDATE groups"
      " SET tab_pinned = ?1, tab_sort_order = ?2"
      " WHERE id = ?3");
  sqlite3_bind_int(statement.get(), 1, pinned ? 1 : 0);
  sqlite3_bind_int(statement.get(), 2, resolved_tab_sort_order);
  sqlite3_bind_int64(statement.get(), 3, group_id);
  if (sqlite3_step(statement.get()) != SQLITE_DONE) {
    return SqliteError(db_, "Update group tab pin state failed.");
  }
  return common::Status::Ok();
}

common::Status ClipRepository::ReorderGroupTab(std::int64_t group_id, int new_tab_sort_order) {
  if (group_id <= 0) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "Invalid group id.");
  }

  Statement statement(
      db_,
      "UPDATE groups"
      " SET tab_pinned = 1, tab_sort_order = ?1"
      " WHERE id = ?2");
  sqlite3_bind_int(statement.get(), 1, new_tab_sort_order);
  sqlite3_bind_int64(statement.get(), 2, group_id);
  if (sqlite3_step(statement.get()) != SQLITE_DONE) {
    return SqliteError(db_, "Reorder group tab failed.");
  }
  return common::Status::Ok();
}


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
