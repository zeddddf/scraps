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

}  // namespace clipd2d::core::store
