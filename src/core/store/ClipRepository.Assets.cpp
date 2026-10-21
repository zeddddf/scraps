#include "core/store/ClipRepository.Internal.h"

namespace clipd2d::core::store {

common::Status ClipRepository::UpdateStorageTierForClip(std::int64_t id) {
  Statement check_stmt(
      db_,
      "SELECT storage_tier, group_id FROM clips WHERE id = ?1");
  sqlite3_bind_int64(check_stmt.get(), 1, id);
  if (sqlite3_step(check_stmt.get()) != SQLITE_ROW) {
    return common::Status::Error(common::StatusCode::NotFound, "Clip not found.");
  }
  StorageTier tier = ParseStorageTier(ColumnString(check_stmt.get(), 0));
  const bool grouped = sqlite3_column_int64(check_stmt.get(), 1) > 0;
  if (grouped) {
    tier = StorageTier::Library;
  }

  Statement update_stmt(db_, "UPDATE clips SET storage_tier = ?1 WHERE id = ?2");
  BindText(update_stmt.get(), 1, ToSettingsValue(tier));
  sqlite3_bind_int64(update_stmt.get(), 2, id);
  if (sqlite3_step(update_stmt.get()) != SQLITE_DONE) {
    return SqliteError(db_, "Update storage tier failed");
  }
  return common::Status::Ok();
}

common::Status ClipRepository::FinalizePendingImageClip(
    std::int64_t id,
    const std::string& content_hash,
    const std::wstring& storage_path,
    const std::wstring& thumbnail_path,
    const std::wstring& preview_path,
    int image_width,
    int image_height) {
  ScopedTransaction transaction(db_);
  if (!transaction.status().ok()) {
    return transaction.status();
  }
  Statement statement(
      db_,
      "UPDATE clips SET storage_path = ?1, thumbnail_path = ?2, preview_path = ?3,"
      " image_width = ?4, image_height = ?5, ingest_state = 'ready', ingest_error = '',"
      " updated_at = ?6,"
      " content_hash = CASE WHEN ?7 <> '' THEN ?7 ELSE content_hash END,"
      " identity_kind = CASE WHEN ?7 <> '' THEN ?8 ELSE identity_kind END,"
      " identity_key = CASE WHEN ?7 <> '' THEN ?7 ELSE identity_key END,"
      " identity_version = CASE WHEN ?7 <> '' THEN ?9 ELSE identity_version END"
      " WHERE id = ?10");
  BindWide(statement.get(), 1, storage_path);
  BindWide(statement.get(), 2, thumbnail_path);
  BindWide(statement.get(), 3, preview_path);
  sqlite3_bind_int(statement.get(), 4, image_width);
  sqlite3_bind_int(statement.get(), 5, image_height);
  sqlite3_bind_int64(statement.get(), 6, NowEpochMs());
  BindText(statement.get(), 7, content_hash);
  BindText(statement.get(), 8, ToSqlIdentityKind(ClipIdentityKind::ImageContentHash));
  sqlite3_bind_int(statement.get(), 9, 4);
  sqlite3_bind_int64(statement.get(), 10, id);
  if (sqlite3_step(statement.get()) != SQLITE_DONE) {
    return SqliteError(db_, "鏇存柊鍥剧墖娲剧敓璧勪骇澶辫触");
  }
#if defined(SCRAPS_ENABLE_TEST_HOOKS)
  if (const common::Status fail_status = MaybeFailForTests(
          TestFailPoint::BeforeFinalizePendingImageCommit,
          "test failpoint before finalize pending image commit");
      !fail_status.ok()) {
    return fail_status;
  }
#endif
  return transaction.Commit();
}

common::Status ClipRepository::MarkImageClipFailed(std::int64_t id, const std::string& error_message) {
  Statement statement(
      db_,
      "UPDATE clips SET ingest_state = 'failed', ingest_error = ?1, updated_at = ?2 WHERE id = ?3");
  BindText(statement.get(), 1, error_message);
  sqlite3_bind_int64(statement.get(), 2, NowEpochMs());
  sqlite3_bind_int64(statement.get(), 3, id);
  if (sqlite3_step(statement.get()) != SQLITE_DONE) {
    return SqliteError(db_, "Mark image clip failed state update failed");
  }
  return common::Status::Ok();
}

common::Status ClipRepository::QueueImageOcr(std::int64_t id) {
  ScopedTransaction transaction(db_);
  if (!transaction.status().ok()) {
    return transaction.status();
  }

  Statement select_stmt(db_, "SELECT title, plain_text FROM clips WHERE id = ?1");
  sqlite3_bind_int64(select_stmt.get(), 1, id);
  if (sqlite3_step(select_stmt.get()) != SQLITE_ROW) {
    return common::Status::Error(common::StatusCode::NotFound, "Image OCR clip not found.");
  }
  const std::string title = ColumnString(select_stmt.get(), 0);
  const std::string plain_text = ColumnString(select_stmt.get(), 1);

  Statement statement(
      db_,
      "UPDATE clips SET ocr_text = '', ocr_state = 'pending', ocr_error = '', ocr_engine = '', ocr_language = '' WHERE id = ?1");
  sqlite3_bind_int64(statement.get(), 1, id);
  if (sqlite3_step(statement.get()) != SQLITE_DONE) {
    return SqliteError(db_, "Queue image OCR failed");
  }

  common::Status status = UpsertFts(id, title, plain_text, {});
  if (!status.ok()) {
    return status;
  }
  status = transaction.Commit();
  if (!status.ok()) {
    return status;
  }
  return common::Status::Ok();
}

common::Status ClipRepository::FinalizeImageOcr(
    std::int64_t id,
    const std::string& ocr_text,
    const std::string& ocr_engine,
    const std::string& ocr_language) {
  ScopedTransaction transaction(db_);
  if (!transaction.status().ok()) {
    return transaction.status();
  }

  Statement select_stmt(db_, "SELECT title, plain_text FROM clips WHERE id = ?1");
  sqlite3_bind_int64(select_stmt.get(), 1, id);
  if (sqlite3_step(select_stmt.get()) != SQLITE_ROW) {
    return common::Status::Error(common::StatusCode::NotFound, "Image OCR clip not found.");
  }
  const std::string title = ColumnString(select_stmt.get(), 0);
  const std::string plain_text = ColumnString(select_stmt.get(), 1);

  Statement statement(
      db_,
      "UPDATE clips SET ocr_text = ?1, ocr_state = 'ready', ocr_error = '', ocr_engine = ?2, ocr_language = ?3, ocr_updated_at = ?4 WHERE id = ?5");
  BindText(statement.get(), 1, ocr_text);
  BindText(statement.get(), 2, ocr_engine);
  BindText(statement.get(), 3, ocr_language);
  sqlite3_bind_int64(statement.get(), 4, NowEpochMs());
  sqlite3_bind_int64(statement.get(), 5, id);
  if (sqlite3_step(statement.get()) != SQLITE_DONE) {
    return SqliteError(db_, "Finalize image OCR failed");
  }

  common::Status fts_status = UpsertFts(id, title, plain_text, ocr_text);
  if (!fts_status.ok()) {
    return fts_status;
  }
  return transaction.Commit();
}

common::Status ClipRepository::MarkImageOcrFailed(std::int64_t id, const std::string& error_message) {
  Statement statement(
      db_,
      "UPDATE clips SET ocr_state = 'failed', ocr_error = ?1, ocr_updated_at = ?2 WHERE id = ?3");
  BindText(statement.get(), 1, error_message);
  sqlite3_bind_int64(statement.get(), 2, NowEpochMs());
  sqlite3_bind_int64(statement.get(), 3, id);
  if (sqlite3_step(statement.get()) != SQLITE_DONE) {
    return SqliteError(db_, "Mark image OCR failed");
  }
  return common::Status::Ok();
}

common::Status ClipRepository::PromoteFileClipToSnapshot(std::int64_t id) {
  ClipRecord clip;
  common::Status status = GetClipDetailsById(id, &clip);
  if (!status.ok()) {
    return status;
  }
  if (clip.summary.content_type != ClipContentType::Files) {
    return common::Status::Ok();
  }

  const auto file_paths = ParseFilePathsFromTextImpl(clip.plain_text);
  if (file_paths.size() != 1 || !IsImageExtension(file_paths.front())) {
    return common::Status::Ok();
  }

  const std::filesystem::path source_path(file_paths.front());
  const std::filesystem::path managed_root(app_paths_.image_dir);
  std::error_code ec;
  const std::filesystem::path normalized_source = std::filesystem::weakly_canonical(source_path, ec);
  const std::filesystem::path normalized_managed_root = std::filesystem::weakly_canonical(managed_root, ec);
  if (!ec && !normalized_managed_root.empty()) {
    const std::wstring source_text = normalized_source.wstring();
    const std::wstring root_text = normalized_managed_root.wstring();
    if (!source_text.empty() && source_text.starts_with(root_text)) {
      return common::Status::Ok();
    }
  }
  if (!std::filesystem::exists(source_path)) {
    return MarkImageClipFailed(id, "Promote snapshot source file not found.");
  }

  common::EnsureDirectory(app_paths_.image_dir);
  const std::string hash = common::HashBytes(common::WideToUtf8(source_path.wstring()).utf8);
  std::filesystem::path target = managed_root / (common::Utf8ToWide(hash).wide + source_path.extension().wstring());
  if (!std::filesystem::exists(target)) {
    std::error_code copy_error;
    std::filesystem::copy_file(source_path, target, std::filesystem::copy_options::overwrite_existing, copy_error);
    if (copy_error) {
      return common::Status::Error(common::StatusCode::InternalError, "澶嶅埗 promoted 蹇収澶辫触: " + copy_error.message());
    }
  }

  const auto target_utf8 = common::WideToUtf8(target.wstring()).utf8;
  const auto target_snapshot = ReadFileSnapshotIdentityData(target.wstring());
  if (!target_snapshot.has_value()) {
    return common::Status::Error(common::StatusCode::InternalError, "Read promoted snapshot metadata failed.");
  }
  const std::string target_content_hash = common::ContentIdentityHex(target_utf8);
  const std::string target_identity_key = BuildImageFileSnapshotIdentityKey(*target_snapshot);

  ScopedTransaction transaction(db_);
  if (!transaction.status().ok()) {
    return transaction.status();
  }

  Statement update_stmt(
      db_,
      "UPDATE clips SET plain_text = ?1, storage_path = ?2, secondary_path = CASE WHEN secondary_path = '' THEN ?3 ELSE secondary_path END,"
      " content_hash = ?4, identity_kind = ?5, identity_key = ?6, identity_version = ?7,"
      " updated_at = ?8 WHERE id = ?9");
  BindText(update_stmt.get(), 1, target_utf8);
  BindWide(update_stmt.get(), 2, target.wstring());
  BindWide(update_stmt.get(), 3, source_path.wstring());
  BindText(update_stmt.get(), 4, target_content_hash);
  BindText(update_stmt.get(), 5, ToSqlIdentityKind(ClipIdentityKind::ImageFileSnapshot));
  BindText(update_stmt.get(), 6, target_identity_key);
  sqlite3_bind_int(update_stmt.get(), 7, 3);
  sqlite3_bind_int64(update_stmt.get(), 8, NowEpochMs());
  sqlite3_bind_int64(update_stmt.get(), 9, id);
  if (sqlite3_step(update_stmt.get()) != SQLITE_DONE) {
    return SqliteError(db_, "Promote clip to snapshot failed");
  }

  Statement update_format_stmt(
      db_,
      "UPDATE clip_formats SET format_text = ?1, storage_path = ?2 WHERE clip_id = ?3 AND format_name = 'CF_HDROP'");
  BindText(update_format_stmt.get(), 1, target_utf8);
  BindWide(update_format_stmt.get(), 2, target.wstring());
  sqlite3_bind_int64(update_format_stmt.get(), 3, id);
  if (sqlite3_step(update_format_stmt.get()) != SQLITE_DONE) {
    return SqliteError(db_, "鏇存柊蹇収鏂囦欢鏍煎紡澶辫触");
  }

  common::Status fts_status = UpsertFts(id, clip.summary.title, target_utf8, clip.ocr_text);
  if (!fts_status.ok()) {
    return fts_status;
  }
#if defined(SCRAPS_ENABLE_TEST_HOOKS)
  if (const common::Status fail_status = MaybeFailForTests(
          TestFailPoint::BeforePromoteSnapshotCommit,
          "test failpoint before promote snapshot commit");
      !fail_status.ok()) {
    return fail_status;
  }
#endif
  return transaction.Commit();
}

common::Status ClipRepository::ExportClipToFile(
    std::int64_t id,
    const std::wstring& destination_path,
    std::wstring* exported_path) {
  ClipRecord clip;
  common::Status status = GetClipById(id, &clip);
  if (!status.ok()) {
    return status;
  }

  std::filesystem::path output(destination_path);
  if (output.empty()) {
    output = std::filesystem::path(clip.summary.title.empty() ? "clip" : clip.summary.title);
  }
  if (!output.has_extension()) {
    switch (clip.summary.content_type) {
      case ClipContentType::Html:
        output.replace_extension(L".html");
        break;
      case ClipContentType::Image:
        output.replace_extension(L".png");
        break;
      default:
        output.replace_extension(L".txt");
        break;
    }
  }

  if (clip.summary.content_type == ClipContentType::Image && !clip.storage_path.empty()) {
    std::filesystem::copy_file(clip.storage_path, output, std::filesystem::copy_options::overwrite_existing);
  } else {
    std::string text_data = clip.plain_text;
    if (clip.summary.content_type == ClipContentType::Html && !clip.html_text.empty()) {
      text_data = clip.html_text;
    }
    std::ofstream stream(output, std::ios::binary | std::ios::trunc);
    stream.write(text_data.data(), static_cast<std::streamsize>(text_data.size()));
    stream.close();
  }

  if (exported_path != nullptr) {
    *exported_path = output.wstring();
  }
  return common::Status::Ok();
}

namespace {

uint64_t GetFileLastWriteTimeMs(const std::wstring& path) {
  WIN32_FILE_ATTRIBUTE_DATA data;
  if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
    ULARGE_INTEGER uli;
    uli.LowPart = data.ftLastWriteTime.dwLowDateTime;
    uli.HighPart = data.ftLastWriteTime.dwHighDateTime;
    uint64_t filetime_ns100 = uli.QuadPart;
    uint64_t unix_epoch_ns100 = 11644473600LL * 10000000LL;
    if (filetime_ns100 >= unix_epoch_ns100) {
      return (filetime_ns100 - unix_epoch_ns100) / 10000;
    }
  }
  return 0;
}

} // namespace

common::Status ClipRepository::MaybeTriggerBackup(bool periodic) {
  std::wstring db_path = app_paths_.database_path;
  std::wstring bak1 = db_path + L".bak1";
  
  std::error_code ec;
  if (!periodic && std::filesystem::exists(bak1, ec)) {
    uint64_t last_write = GetFileLastWriteTimeMs(bak1);
    uint64_t now = NowEpochMs();
    
    // Check if 24 hours (86,400,000 milliseconds) have passed
    if (now > last_write && (now - last_write) < 86400000ULL) {
      common::WriteLog(common::LogLevel::Info, "store", "Startup backup skipped; less than 24 hours since last backup.");
      return common::Status::Ok();
    }
  }

  common::WriteLog(common::LogLevel::Info, "store", "Rotating database backups...");
  
  std::wstring bak2 = db_path + L".bak2";
  std::wstring bak3 = db_path + L".bak3";
  
  if (std::filesystem::exists(bak3, ec)) {
    std::filesystem::remove(bak3, ec);
  }
  if (std::filesystem::exists(bak2, ec)) {
    std::filesystem::rename(bak2, bak3, ec);
  }
  if (std::filesystem::exists(bak1, ec)) {
    std::filesystem::rename(bak1, bak2, ec);
  }

  common::WriteLog(common::LogLevel::Info, "store", "Creating database backup copy to bak1...");
  common::Status status = BackupDatabase(bak1);
  if (status.ok()) {
    common::WriteLog(common::LogLevel::Info, "store", "Database backup completed successfully.");
  } else {
    common::WriteLog(common::LogLevel::Error, "store", "Database backup failed: " + status.message);
  }
  
  return status;
}

common::Status ClipRepository::BackupDatabase(const std::wstring& destination_path) const {
  if (db_ == nullptr) {
    return common::Status::Error(common::StatusCode::InternalError, "Database is not open.");
  }

  sqlite3* dest_db = nullptr;
  const auto dest_converted = common::WideToUtf8(destination_path);
  int rc = sqlite3_open_v2(
      dest_converted.utf8.c_str(),
      &dest_db,
      SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
      nullptr);
  if (rc != SQLITE_OK) {
    if (dest_db != nullptr) {
      sqlite3_close(dest_db);
    }
    return common::Status::Error(common::StatusCode::InternalError, "Failed to open destination database for backup.");
  }

  // Turn off journaling on the backup connection to optimize speed
  sqlite3_exec(dest_db, "PRAGMA journal_mode=OFF;", nullptr, nullptr, nullptr);

  sqlite3_backup* backup = sqlite3_backup_init(dest_db, "main", db_, "main");
  if (backup == nullptr) {
    std::string err_msg = sqlite3_errmsg(dest_db);
    sqlite3_close(dest_db);
    return common::Status::Error(common::StatusCode::InternalError, "Failed to initialize SQLite backup: " + err_msg);
  }

  rc = sqlite3_backup_step(backup, -1);
  sqlite3_backup_finish(backup);

  if (rc != SQLITE_DONE) {
    std::string err_msg = sqlite3_errmsg(dest_db);
    sqlite3_close(dest_db);
    return common::Status::Error(common::StatusCode::InternalError, "SQLite backup step failed: " + err_msg);
  }

  sqlite3_close(dest_db);
  return common::Status::Ok();
}

}  // namespace clipd2d::core::store
