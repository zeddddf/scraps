#include "core/store/ClipRepository.Internal.h"

#include <cstdio>

namespace clipd2d::core::store {

namespace {

constexpr int kCurrentSchemaVersion = 5;

constexpr const char* kFreshSchemaSql =
    "CREATE TABLE IF NOT EXISTS clips ("
    " id INTEGER PRIMARY KEY AUTOINCREMENT,"
    " title TEXT NOT NULL,"
    " plain_text TEXT NOT NULL DEFAULT '',"
    " html_text TEXT NOT NULL DEFAULT '',"
    " rtf_text TEXT NOT NULL DEFAULT '',"
    " content_type TEXT NOT NULL,"
    " storage_path TEXT NOT NULL DEFAULT '',"
    " secondary_path TEXT NOT NULL DEFAULT '',"
    " source_app_name TEXT NOT NULL DEFAULT '',"
    " source_app_path TEXT NOT NULL DEFAULT '',"
    " source_window_title TEXT NOT NULL DEFAULT '',"
    " is_pinned INTEGER NOT NULL DEFAULT 0,"
    " is_favorite INTEGER NOT NULL DEFAULT 0,"
    " group_id INTEGER NOT NULL DEFAULT 0,"
    " group_sort_order INTEGER NOT NULL DEFAULT 0,"
    " storage_tier TEXT NOT NULL DEFAULT 'history',"
    " paste_count INTEGER NOT NULL DEFAULT 0,"
    " last_pasted_at INTEGER NOT NULL DEFAULT 0,"
    " favorite_sort_order INTEGER NOT NULL DEFAULT 0,"
    " pin_sort_order INTEGER NOT NULL DEFAULT 0,"
    " image_width INTEGER NOT NULL DEFAULT 0,"
    " image_height INTEGER NOT NULL DEFAULT 0,"
    " estimated_decoded_bytes INTEGER NOT NULL DEFAULT 0,"
    " is_large_image INTEGER NOT NULL DEFAULT 0,"
    " ingest_state TEXT NOT NULL DEFAULT 'ready',"
    " ingest_error TEXT NOT NULL DEFAULT '',"
    " thumbnail_path TEXT NOT NULL DEFAULT '',"
    " preview_path TEXT NOT NULL DEFAULT '',"
    " ocr_text TEXT NOT NULL DEFAULT '',"
    " ocr_state TEXT NOT NULL DEFAULT 'none',"
    " ocr_error TEXT NOT NULL DEFAULT '',"
    " ocr_engine TEXT NOT NULL DEFAULT '',"
    " ocr_language TEXT NOT NULL DEFAULT '',"
    " ocr_updated_at INTEGER NOT NULL DEFAULT 0,"
    " identity_kind TEXT NOT NULL DEFAULT 'legacy_payload',"
    " identity_key TEXT NOT NULL DEFAULT '',"
    " identity_version INTEGER NOT NULL DEFAULT 3,"
    " content_hash TEXT NOT NULL DEFAULT '',"
    " created_at INTEGER NOT NULL,"
    " updated_at INTEGER NOT NULL"
    ");"
    "CREATE TABLE IF NOT EXISTS clip_formats ("
    " id INTEGER PRIMARY KEY AUTOINCREMENT,"
    " clip_id INTEGER NOT NULL,"
    " format_name TEXT NOT NULL,"
    " format_text TEXT NOT NULL DEFAULT '',"
    " payload_kind TEXT NOT NULL DEFAULT 'text',"
    " blob_data BLOB,"
    " storage_path TEXT NOT NULL DEFAULT '',"
    " format_order INTEGER NOT NULL DEFAULT 0,"
    " FOREIGN KEY(clip_id) REFERENCES clips(id) ON DELETE CASCADE"
    ");"
    "CREATE TABLE IF NOT EXISTS groups ("
    " id INTEGER PRIMARY KEY AUTOINCREMENT,"
    " name TEXT NOT NULL UNIQUE,"
    " sort_order INTEGER NOT NULL DEFAULT 0,"
    " tab_pinned INTEGER NOT NULL DEFAULT 0,"
    " tab_sort_order INTEGER NOT NULL DEFAULT 0"
    ");"
    "CREATE TABLE IF NOT EXISTS tags ("
    " id INTEGER PRIMARY KEY AUTOINCREMENT,"
    " name TEXT NOT NULL UNIQUE,"
    " color TEXT NOT NULL DEFAULT '#4F8CFF',"
    " is_system INTEGER NOT NULL DEFAULT 0,"
    " sort_order INTEGER NOT NULL DEFAULT 0,"
    " created_at INTEGER NOT NULL DEFAULT 0"
    ");"
    "CREATE TABLE IF NOT EXISTS clip_tags ("
    " clip_id INTEGER NOT NULL,"
    " tag_id INTEGER NOT NULL,"
    " sort_order INTEGER NOT NULL DEFAULT 0,"
    " created_at INTEGER NOT NULL DEFAULT 0,"
    " PRIMARY KEY(clip_id, tag_id),"
    " FOREIGN KEY(clip_id) REFERENCES clips(id) ON DELETE CASCADE,"
    " FOREIGN KEY(tag_id) REFERENCES tags(id) ON DELETE CASCADE"
    ");"
    "CREATE TABLE IF NOT EXISTS paste_queue ("
    " id INTEGER PRIMARY KEY AUTOINCREMENT,"
    " clip_id INTEGER NOT NULL UNIQUE,"
    " sort_order INTEGER NOT NULL DEFAULT 0,"
    " auto_pop INTEGER NOT NULL DEFAULT 1,"
    " created_at INTEGER NOT NULL DEFAULT 0,"
    " FOREIGN KEY(clip_id) REFERENCES clips(id) ON DELETE CASCADE"
    ");"
    "CREATE TABLE IF NOT EXISTS settings ("
    " key TEXT PRIMARY KEY,"
    " value TEXT NOT NULL"
    ");"
    "CREATE VIRTUAL TABLE IF NOT EXISTS clips_fts USING fts5(title, plain_text, ocr_text);";

constexpr const char* kIndexSql =
    "CREATE INDEX IF NOT EXISTS idx_clips_updated_at ON clips(updated_at);"
    "CREATE INDEX IF NOT EXISTS idx_clips_hash ON clips(content_hash);"
    "CREATE INDEX IF NOT EXISTS idx_clips_group ON clips(group_id);"
    "CREATE INDEX IF NOT EXISTS idx_clips_type ON clips(content_type);"
    "CREATE INDEX IF NOT EXISTS idx_clips_source_app ON clips(source_app_name);"
    "CREATE INDEX IF NOT EXISTS idx_clips_favorite ON clips(is_favorite);"
    "CREATE INDEX IF NOT EXISTS idx_clips_pinned ON clips(is_pinned);"
    "CREATE INDEX IF NOT EXISTS idx_clips_storage_tier ON clips(storage_tier);"
    "CREATE INDEX IF NOT EXISTS idx_clips_last_pasted ON clips(last_pasted_at);"
    "CREATE INDEX IF NOT EXISTS idx_clips_ingest_state ON clips(ingest_state);"
    "CREATE INDEX IF NOT EXISTS idx_clips_ocr_state ON clips(ocr_state);"
    "CREATE INDEX IF NOT EXISTS idx_clips_pin_sort_order ON clips(pin_sort_order);"
    "CREATE INDEX IF NOT EXISTS idx_clips_identity ON clips(identity_kind, identity_key);"
    "CREATE INDEX IF NOT EXISTS idx_clip_tags_tag ON clip_tags(tag_id);"
    "CREATE INDEX IF NOT EXISTS idx_paste_queue_sort ON paste_queue(sort_order);"
    "CREATE INDEX IF NOT EXISTS idx_clips_pinned_sort ON clips(is_pinned DESC, pin_sort_order ASC, updated_at DESC);";

constexpr const char* kSeedSql =
    "INSERT OR IGNORE INTO tags(name, color, is_system, sort_order, created_at)"
    " VALUES('favorite', '#F6C453', 1, 0, 0);"
    "INSERT OR IGNORE INTO tags(name, color, is_system, sort_order, created_at)"
    " VALUES('frequent', '#65B86E', 1, 1, 0);"
    "INSERT OR IGNORE INTO tags(name, color, is_system, sort_order, created_at)"
    " VALUES('edited', '#8B6EF3', 1, 2, 0);"
    "INSERT OR IGNORE INTO tags(name, color, is_system, sort_order, created_at)"
    " VALUES('queued', '#4F8CFF', 1, 3, 0);";

// 删除 SQLite 的旁路文件（-wal / -shm）。只在"主库文件被替换"的语境下调用：
// 此时残留的 WAL 属于另一份主文件，SQLite 打开时会把它的页面回放到新主库上，
// 直接判定 "file is not a database"，于是又触发一次自愈——死循环就成型了。
void RemoveDatabaseSidecarFiles(const std::wstring& database_path) {
  std::error_code ec;
  for (const wchar_t* suffix : {L"-wal", L"-shm"}) {
    const std::wstring sidecar = database_path + suffix;
    if (std::filesystem::exists(sidecar, ec)) {
      if (!std::filesystem::remove(sidecar, ec)) {
        common::WriteLog(
            common::LogLevel::Warning,
            "store",
            "Failed to remove database sidecar: " + common::WideToUtf8(sidecar).utf8);
      }
    }
  }
}

// 打开失败时把"这个文件到底是什么"记下来：0 字节（被截断）与非法魔数（被别的
// 内容覆写）是完全不同的两种事故，缺了这一行只能靠猜。
std::string DescribeDatabaseFile(const std::wstring& database_path) {
  std::error_code ec;
  const auto size = std::filesystem::file_size(database_path, ec);
  if (ec) {
    return "size=missing";
  }
  std::ifstream input(std::filesystem::path(database_path), std::ios::binary);
  char magic[16]{};
  input.read(magic, sizeof(magic));
  const std::streamsize read = input.gcount();

  std::string hex;
  hex.reserve(static_cast<std::size_t>(read) * 2);
  for (std::streamsize i = 0; i < read; ++i) {
    char buffer[4]{};
    std::snprintf(buffer, sizeof(buffer), "%02X", static_cast<unsigned char>(magic[i]));
    hex += buffer;
  }
  return "size=" + std::to_string(size) + " magic=" + hex;
}

}  // namespace

#if defined(SCRAPS_ENABLE_TEST_HOOKS)
ClipRepository::TestFailPoint g_clip_repository_test_fail_point = ClipRepository::TestFailPoint::None;

common::Status MaybeFailForTests(ClipRepository::TestFailPoint fail_point, const char* message) {
  if (g_clip_repository_test_fail_point == fail_point) {
    return common::Status::Error(common::StatusCode::InternalError, message);
  }
  return common::Status::Ok();
}
#endif

ClipRepository::~ClipRepository() {
  Shutdown();
}

common::Status ClipRepository::Initialize(const common::AppPaths& paths) {
  app_paths_ = paths;
  
  // Open directly without the sync quick_check on the startup path (cold start performance).
  common::Status status = OpenDatabase(paths.database_path);
  if (status.ok()) {
    status = CreateSchema();
    if (status.ok()) {
      return common::Status::Ok();
    }
    common::WriteLog(common::LogLevel::Error, "store", "Schema creation failed: " + status.message);
  } else {
    common::WriteLog(
        common::LogLevel::Error,
        "store",
        "Database open failed: " + status.message + " (" +
            DescribeDatabaseFile(paths.database_path) + ")");
  }

  // If opening or schema creation failed, database is likely corrupt!
  common::WriteLog(common::LogLevel::Error, "store", "Database corruption or error detected. Attempting self-healing recovery...");
  
  Shutdown();

  bool restored = false;
  std::wstring corrupt_backup_path;
  common::Status restore_status = RestoreFromLatestBackup(&restored, &corrupt_backup_path);
  
  if (restore_status.ok() && restored) {
    common::WriteLog(common::LogLevel::Info, "store", "Database successfully restored from backup. Corrupt file moved to: " + common::WideToUtf8(corrupt_backup_path).utf8);
    // Re-attempt opening and checking the restored database
    status = OpenAndCheckDatabase(paths.database_path);
    if (status.ok()) {
      status = CreateSchema();
      if (status.ok()) {
        return common::Status::Ok();
      }
      common::WriteLog(common::LogLevel::Error, "store", "Restored database failed schema creation: " + status.message);
    } else {
      common::WriteLog(common::LogLevel::Error, "store", "Restored database is also corrupted or failed to open: " + status.message);
    }
    Shutdown();
  }

  // If backups are missing or also corrupt, perform full crash self-healing: recreate fresh database from scratch
  common::WriteLog(common::LogLevel::Warning, "store", "No valid backups available. Re-initializing fresh database from scratch to guarantee application startup.");
  
  // Backup the corrupted files one last time before destroying them
  BackupCorruptDatabase(paths.database_path);
  // 同 restore：重建前必须清掉旁路文件，否则新建的空库会读到旧 WAL。
  RemoveDatabaseSidecarFiles(paths.database_path);

  // Open a fresh database
  status = OpenDatabase(paths.database_path);
  if (!status.ok()) {
    return status;
  }
  return CreateSchema();
}

common::Status ClipRepository::OpenAndCheckDatabase(const std::wstring& path) {
  common::Status status = OpenDatabase(path);
  if (!status.ok()) {
    return status;
  }

  // Run a quick check
  Statement stmt(db_, "PRAGMA quick_check");
  if (stmt.get() == nullptr) {
    return common::Status::Error(common::StatusCode::InternalError, "Failed to prepare quick_check statement");
  }
  
  if (sqlite3_step(stmt.get()) != SQLITE_ROW) {
    return common::Status::Error(common::StatusCode::InternalError, "quick_check query failed to step");
  }
  
  const char* result = reinterpret_cast<const char*>(sqlite3_column_text(stmt.get(), 0));
  if (result == nullptr || std::string(result) != "ok") {
    std::string err = result ? result : "unknown";
    return common::Status::Error(common::StatusCode::InternalError, "quick_check returned: " + err);
  }

  return common::Status::Ok();
}

bool ClipRepository::VerifyDatabaseIntegrity(const std::wstring& path) const {
  sqlite3* test_db = nullptr;
  const auto converted = common::WideToUtf8(path);
  if (sqlite3_open_v2(
          converted.utf8.c_str(),
          &test_db,
          SQLITE_OPEN_READONLY,
          nullptr) != SQLITE_OK) {
    if (test_db != nullptr) {
      sqlite3_close(test_db);
    }
    return false;
  }

  sqlite3_stmt* stmt = nullptr;
  int rc = sqlite3_prepare_v2(test_db, "PRAGMA quick_check", -1, &stmt, nullptr);
  if (rc != SQLITE_OK) {
    sqlite3_close(test_db);
    return false;
  }

  bool is_ok = false;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    const char* result = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    if (result != nullptr && std::string(result) == "ok") {
      is_ok = true;
    }
  }

  sqlite3_finalize(stmt);
  sqlite3_close(test_db);
  return is_ok;
}

void ClipRepository::BackupCorruptDatabase(const std::wstring& database_path) {
  std::error_code ec;
  std::wstring corrupt_suffix = L".corrupt." + std::to_wstring(NowEpochMs());

  std::filesystem::path db_file(database_path);
  if (std::filesystem::exists(db_file, ec)) {
    std::filesystem::rename(db_file, db_file.wstring() + corrupt_suffix, ec);
  }

  std::filesystem::path wal_file(database_path + L"-wal");
  if (std::filesystem::exists(wal_file, ec)) {
    std::filesystem::rename(wal_file, wal_file.wstring() + corrupt_suffix, ec);
  }

  std::filesystem::path shm_file(database_path + L"-shm");
  if (std::filesystem::exists(shm_file, ec)) {
    std::filesystem::rename(shm_file, shm_file.wstring() + corrupt_suffix, ec);
  }
}

common::Status ClipRepository::RestoreFromLatestBackup(bool* restored, std::wstring* corrupt_backup_path) {
  *restored = false;
  std::error_code ec;
  std::wstring db_path = app_paths_.database_path;
  
  std::vector<std::wstring> backup_candidates = {
    db_path + L".bak1",
    db_path + L".bak2",
    db_path + L".bak3"
  };

  for (const auto& backup_path : backup_candidates) {
    if (!std::filesystem::exists(backup_path, ec)) {
      continue;
    }

    if (VerifyDatabaseIntegrity(backup_path)) {
      if (corrupt_backup_path != nullptr) {
        *corrupt_backup_path = db_path + L".corrupt." + std::to_wstring(NowEpochMs());
      }
      BackupCorruptDatabase(db_path);
      // 复制前清一次旁路文件：BackupCorruptDatabase 已移走，这里兜住"移走失败"。
      RemoveDatabaseSidecarFiles(db_path);

      // 不能直接 copy_file 到主库：它会先把目标截断成 0 字节再写 149MB，任何在
      // 这个窗口里打开数据库的进程（第二个实例、scraps-cli、截图工具）都会读到
      // "file is not a database"，然后各自再触发一轮自愈——一天 185 次的来源。
      // 改成"先写暂存文件，再原子替换"：外部要么看到旧文件，要么看到完整新文件。
      const std::wstring staging_path = db_path + L".restore.tmp";
      std::filesystem::remove(staging_path, ec);
      std::filesystem::copy_file(
          backup_path,
          staging_path,
          std::filesystem::copy_options::overwrite_existing,
          ec);
      if (!ec && MoveFileExW(
                     staging_path.c_str(),
                     db_path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0) {
        // 替换后再清一次：复制期间若有别的连接针对旧主库写出过 -wal/-shm，
        // 重新打开时 SQLite 会把它们的页面回放到刚换上的备份上。
        RemoveDatabaseSidecarFiles(db_path);
        *restored = true;
        return common::Status::Ok();
      }
      std::filesystem::remove(staging_path, ec);
    } else {
      common::WriteLog(common::LogLevel::Warning, "store", "Backup file is corrupt: " + common::WideToUtf8(backup_path).utf8);
    }
  }

  return common::Status::Ok();
}

void ClipRepository::Shutdown() {
  if (db_ != nullptr) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
}

common::Status ClipRepository::OpenDatabase(const std::wstring& path) {
  const auto converted = common::WideToUtf8(path);
  if (sqlite3_open_v2(
          converted.utf8.c_str(),
          &db_,
          SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE,
          nullptr) != SQLITE_OK) {
    if (db_ != nullptr) {
      sqlite3_close(db_);
      db_ = nullptr;
    }
    return common::Status::Error(common::StatusCode::InternalError, "Open SQLite failed.");
  }
  // Cap lock waits at 5s to prevent indefinite hangs under WAL contention.
  sqlite3_busy_timeout(db_, 5000);
  return Execute(
      "PRAGMA journal_mode=WAL;"
      "PRAGMA synchronous=NORMAL;"
      "PRAGMA foreign_keys=ON;"
      "PRAGMA temp_store=MEMORY;");
}

common::Status ClipRepository::CreateSchema() {
  int schema_version = 0;
  common::Status status = ReadSchemaVersion(&schema_version);
  if (!status.ok()) {
    return status;
  }
  if (schema_version == 0) {
    Statement clips_probe(db_, "SELECT name FROM sqlite_master WHERE type='table' AND name='clips'");
    const bool has_clips_table = sqlite3_step(clips_probe.get()) == SQLITE_ROW;
    if (!has_clips_table) {
      status = CreateFreshSchema();
      if (!status.ok()) {
        return status;
      }
      return WriteSchemaVersion(kCurrentSchemaVersion);
    }
    return common::Status::Error(
        common::StatusCode::InternalError,
        "Older database schemas are no longer supported by this build.");
  }
  if (schema_version > kCurrentSchemaVersion) {
    return common::Status::Error(common::StatusCode::InternalError, "Database schema version is newer than this build supports.");
  }
  if (schema_version < kCurrentSchemaVersion) {
    status = MigrateSchema(schema_version);
    if (!status.ok()) {
      return status;
    }
    return WriteSchemaVersion(kCurrentSchemaVersion);
  }
  return common::Status::Ok();
}

common::Status ClipRepository::CreateFreshSchema() {
  common::Status status = Execute(kFreshSchemaSql);
  if (!status.ok()) {
    return status;
  }
  status = Execute(kIndexSql);
  if (!status.ok()) {
    return status;
  }
  return EnsureSeedData();
}

common::Status ClipRepository::EnsureSeedData() {
  return Execute(kSeedSql);
}

common::Status ClipRepository::MigrateSchema(int from_version) {
  if (from_version == 4) {
    return MigrateSchemaFrom4To5();
  }
  return common::Status::Error(
      common::StatusCode::InternalError,
      "Older database schemas are no longer supported by this build.");
}

common::Status ClipRepository::MigrateSchemaFrom4To5() {
  ScopedTransaction transaction(db_);
  if (!transaction.status().ok()) {
    return transaction.status();
  }

  common::Status status = EnsureColumn(db_, "groups", "sort_order", "sort_order INTEGER NOT NULL DEFAULT 0");
  if (!status.ok()) {
    return status;
  }
  status = EnsureColumn(db_, "groups", "tab_pinned", "tab_pinned INTEGER NOT NULL DEFAULT 0");
  if (!status.ok()) {
    return status;
  }
  status = EnsureColumn(db_, "groups", "tab_sort_order", "tab_sort_order INTEGER NOT NULL DEFAULT 0");
  if (!status.ok()) {
    return status;
  }

  Statement load_groups(db_, "SELECT id FROM groups ORDER BY name COLLATE NOCASE ASC, id ASC");
  std::vector<std::int64_t> group_ids;
  while (sqlite3_step(load_groups.get()) == SQLITE_ROW) {
    group_ids.push_back(sqlite3_column_int64(load_groups.get(), 0));
  }

  Statement update_sort(db_, "UPDATE groups SET sort_order = ?1 WHERE id = ?2");
  for (size_t index = 0; index < group_ids.size(); ++index) {
    sqlite3_reset(update_sort.get());
    sqlite3_clear_bindings(update_sort.get());
    sqlite3_bind_int(update_sort.get(), 1, static_cast<int>((index + 1) * 10));
    sqlite3_bind_int64(update_sort.get(), 2, group_ids[index]);
    if (sqlite3_step(update_sort.get()) != SQLITE_DONE) {
      return SqliteError(db_, "Migrate groups sort order failed.");
    }
  }

  return transaction.Commit();
}

common::Status ClipRepository::ReadSchemaVersion(int* version) const {
  if (version == nullptr) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "Schema version output cannot be null.");
  }
  Statement statement(db_, "PRAGMA user_version");
  if (sqlite3_step(statement.get()) != SQLITE_ROW) {
    return SqliteError(db_, "Read schema version failed");
  }
  *version = sqlite3_column_int(statement.get(), 0);
  return common::Status::Ok();
}

common::Status ClipRepository::WriteSchemaVersion(int version) const {
  return Execute(("PRAGMA user_version=" + std::to_string(version)).c_str());
}

common::Status ClipRepository::Execute(const char* sql) const {
  char* error_message = nullptr;
  const int result = sqlite3_exec(db_, sql, nullptr, nullptr, &error_message);
  if (result != SQLITE_OK) {
    const std::string message = error_message != nullptr ? error_message : "unknown";
    sqlite3_free(error_message);
    return common::Status::Error(common::StatusCode::InternalError, message);
  }
  return common::Status::Ok();
}

#if defined(SCRAPS_ENABLE_TEST_HOOKS)
void ClipRepository::SetTestFailPoint(TestFailPoint fail_point) {
  g_clip_repository_test_fail_point = fail_point;
}
#endif

}  // namespace clipd2d::core::store
