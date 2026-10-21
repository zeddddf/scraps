#pragma once

// Responsibility: Shared ClipRepository-only helpers, row mappers, SQL utilities, and transaction primitives.
// Inputs: SQLite handles, Clip types, filesystem metadata, and policy helpers.
// Outputs: Internal helper utilities consumed by ClipRepository implementation units only.
// Forbidden dependencies: UI/window code, AppController orchestration, or direct platform message handling.

#include "core/store/ClipRepository.h"

#include <windows.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "common/FileHash.h"
#include "common/Log.h"
#include "common/StringUtil.h"
#include "common/Utf8.h"
#include "core/policies/ClipIdentityPolicy.h"
#include "sqlite3.h"

namespace clipd2d::core::store {

namespace {

class Statement {
 public:
  Statement(sqlite3* db, const char* sql) : db_(db) {
    const int result = sqlite3_prepare_v2(db_, sql, -1, &stmt_, nullptr);
    if (result != SQLITE_OK) {
      clipd2d::common::WriteLog(
          clipd2d::common::LogLevel::Error,
          "sqlite.prepare",
          "Failed to prepare SQL: " + std::string(sql) + " error: " + sqlite3_errmsg(db_));
    }
  }

  ~Statement() {
    if (stmt_ != nullptr) {
      sqlite3_finalize(stmt_);
    }
  }

  sqlite3_stmt* get() const { return stmt_; }

 private:
  sqlite3* db_ = nullptr;
  sqlite3_stmt* stmt_ = nullptr;
};

std::int64_t NowEpochMs() {
  FILETIME file_time{};
  GetSystemTimeAsFileTime(&file_time);
  ULARGE_INTEGER ticks{};
  ticks.LowPart = file_time.dwLowDateTime;
  ticks.HighPart = file_time.dwHighDateTime;
  constexpr std::uint64_t kEpochDiff = 116444736000000000ull;
  return static_cast<std::int64_t>((ticks.QuadPart - kEpochDiff) / 10000ull);
}
std::string MakeSnippet(const CapturedClip& clip) {
  if (!clip.plain_text.empty()) {
    return common::TrimAscii(clip.plain_text).substr(0, 180);
  }
  if (clip.content_type == ClipContentType::Image) {
    return "Image";
  }
  if (clip.content_type == ClipContentType::Files) {
    return clip.title;
  }
  if (clip.content_type == ClipContentType::Rtf) {
    return "Rich text";
  }
  return clip.title;
}

// FTS5 string literals escape an embedded double-quote by doubling it. Without
// this a token containing '"' can break out of the quoted phrase and inject
// MATCH operators (syntax error or altered query semantics). Everything inside
// the quoted phrase is literal, so NEAR/OR/AND/*/: become inert automatically.
std::string EscapeFts5PhraseToken(const std::string& token) {
  std::string escaped;
  escaped.reserve(token.size() + 2);
  for (const char ch : token) {
    if (ch == '"') {
      escaped += "\"\"";
    } else {
      escaped.push_back(ch);
    }
  }
  return escaped;
}

std::string BuildMatchQuery(const std::string& search_text) {
  const std::string trimmed = common::TrimAscii(search_text);
  if (trimmed.empty()) {
    return {};
  }
  std::vector<std::string> tokens;
  std::string current;
  for (const char ch : trimmed) {
    if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
      if (!current.empty()) {
        tokens.push_back(current);
        current.clear();
      }
    } else {
      current.push_back(ch);
    }
  }
  if (!current.empty()) {
    tokens.push_back(current);
  }
  if (tokens.empty()) {
    return {};
  }
  std::string query;
  for (size_t index = 0; index < tokens.size(); ++index) {
    if (index > 0) {
      query += " AND ";
    }
    query += "\"";
    query += EscapeFts5PhraseToken(tokens[index]);
    query += "\"*";
  }
  return query;
}

std::string FirstSearchToken(const std::string& search_text) {
  const std::string trimmed = common::TrimAscii(search_text);
  std::string token;
  for (const char ch : trimmed) {
    if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
      if (!token.empty()) {
        break;
      }
    } else {
      token.push_back(ch);
    }
  }
  return token;
}

std::string EscapeLikePatternUtf8(std::string_view text) {
  std::string escaped;
  escaped.reserve(text.size() + 4);
  for (const char ch : text) {
    if (ch == '%' || ch == '_' || ch == '\\') {
      escaped.push_back('\\');
    }
    escaped.push_back(ch);
  }
  return escaped;
}

std::string Utf8LikePattern(std::string_view text) {
  std::string pattern = "%";
  pattern += EscapeLikePatternUtf8(text);
  pattern += "%";
  return pattern;
}

std::wstring ToWideOrEmpty(const char* utf8_text) {
  if (utf8_text == nullptr) {
    return {};
  }
  return clipd2d::common::Utf8ToWide(utf8_text).wide;
}

void BindText(sqlite3_stmt* stmt, int index, const std::string& value) {
  if (stmt != nullptr) {
    sqlite3_bind_text(stmt, index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
  }
}

void BindWide(sqlite3_stmt* stmt, int index, const std::wstring& value) {
  if (stmt != nullptr) {
    const auto converted = clipd2d::common::WideToUtf8(value);
    sqlite3_bind_text(stmt, index, converted.utf8.c_str(), static_cast<int>(converted.utf8.size()), SQLITE_TRANSIENT);
  }
}

void BindBlob(sqlite3_stmt* stmt, int index, const std::vector<std::uint8_t>& value) {
  if (stmt != nullptr) {
    if (value.empty()) {
      sqlite3_bind_null(stmt, index);
      return;
    }
    sqlite3_bind_blob(stmt, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
  }
}

std::string ColumnString(sqlite3_stmt* stmt, int index) {
  const unsigned char* text = sqlite3_column_text(stmt, index);
  if (text == nullptr) {
    return {};
  }
  return std::string(reinterpret_cast<const char*>(text));
}

std::vector<std::uint8_t> ColumnBlob(sqlite3_stmt* stmt, int index) {
  const void* blob = sqlite3_column_blob(stmt, index);
  const int bytes = sqlite3_column_bytes(stmt, index);
  if (blob == nullptr || bytes <= 0) {
    return {};
  }
  const auto* begin = static_cast<const std::uint8_t*>(blob);
  return std::vector<std::uint8_t>(begin, begin + bytes);
}

common::Status SqliteError(sqlite3* db, const std::string& prefix) {
  return common::Status::Error(
      common::StatusCode::InternalError,
      prefix + ": " + sqlite3_errmsg(db));
}

class ScopedTransaction {
 public:
  explicit ScopedTransaction(sqlite3* db) : db_(db) {
    char* error_message = nullptr;
    const int result = sqlite3_exec(db_, "BEGIN IMMEDIATE TRANSACTION", nullptr, nullptr, &error_message);
    if (result != SQLITE_OK) {
      const std::string message = error_message != nullptr ? error_message : "unknown";
      sqlite3_free(error_message);
      status_ = common::Status::Error(common::StatusCode::InternalError, "Begin transaction failed: " + message);
    }
  }

  ScopedTransaction(const ScopedTransaction&) = delete;
  ScopedTransaction& operator=(const ScopedTransaction&) = delete;

  ~ScopedTransaction() {
    if (db_ == nullptr || !status_.ok() || committed_) {
      return;
    }
    sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
  }

  common::Status status() const {
    return status_;
  }

  common::Status Commit() {
    if (!status_.ok()) {
      return status_;
    }
    if (committed_) {
      return common::Status::Ok();
    }
    char* error_message = nullptr;
    const int result = sqlite3_exec(db_, "COMMIT", nullptr, nullptr, &error_message);
    if (result != SQLITE_OK) {
      const std::string message = error_message != nullptr ? error_message : "unknown";
      sqlite3_free(error_message);
      return common::Status::Error(common::StatusCode::InternalError, "Commit transaction failed: " + message);
    }
    committed_ = true;
    return common::Status::Ok();
  }

 private:
  sqlite3* db_ = nullptr;
  bool committed_ = false;
  common::Status status_ = common::Status::Ok();
};

void DeleteStorageFile(const std::wstring& path) {
  if (!path.empty()) {
    DeleteFileW(path.c_str());
  }
}

bool IsManagedAssetPath(const common::AppPaths& app_paths, const std::wstring& path) {
  if (path.empty()) {
    return false;
  }
  const std::filesystem::path candidate(path);
  std::error_code ec;
  const std::filesystem::path normalized_candidate = std::filesystem::weakly_canonical(candidate, ec);
  if (ec) {
    return false;
  }
  const std::filesystem::path normalized_image_dir = std::filesystem::weakly_canonical(std::filesystem::path(app_paths.image_dir), ec);
  if (!ec) {
    const std::wstring candidate_text = normalized_candidate.wstring();
    const std::wstring image_root = normalized_image_dir.wstring();
    if (!candidate_text.empty() && !image_root.empty() && candidate_text.starts_with(image_root)) {
      return true;
    }
  }
  ec.clear();
  const std::filesystem::path normalized_cache_dir = std::filesystem::weakly_canonical(std::filesystem::path(app_paths.cache_dir), ec);
  if (!ec) {
    const std::wstring candidate_text = normalized_candidate.wstring();
    const std::wstring cache_root = normalized_cache_dir.wstring();
    if (!candidate_text.empty() && !cache_root.empty() && candidate_text.starts_with(cache_root)) {
      return true;
    }
  }
  return false;
}

bool TableHasColumn(sqlite3* db, const char* table, const char* column) {
  const std::string sql = "PRAGMA table_info(" + std::string(table) + ")";
  Statement statement(db, sql.c_str());
  while (sqlite3_step(statement.get()) == SQLITE_ROW) {
    if (ColumnString(statement.get(), 1) == column) {
      return true;
    }
  }
  return false;
}

common::Status EnsureColumn(sqlite3* db, const char* table, const char* column, const char* definition) {
  if (TableHasColumn(db, table, column)) {
    return common::Status::Ok();
  }
  const std::string sql = "ALTER TABLE " + std::string(table) + " ADD COLUMN " + definition;
  char* error_message = nullptr;
  if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &error_message) != SQLITE_OK) {
    const std::string message = error_message != nullptr ? error_message : "unknown";
    sqlite3_free(error_message);
    return common::Status::Error(common::StatusCode::InternalError, message);
  }
  return common::Status::Ok();
}

std::optional<ClipContentType> ViewContentType(ClipView view) {
  switch (view) {
    case ClipView::Text:
      return ClipContentType::Text;
    case ClipView::Html:
      return ClipContentType::Html;
    case ClipView::Image:
      return ClipContentType::Image;
    case ClipView::Files:
      return ClipContentType::Files;
    default:
      return std::nullopt;
  }
}

std::optional<StorageTier> ViewStorageTier(ClipView) {
  return std::nullopt;
}

DateRangeFilter NormalizeDateRange(const ClipQuery& query) {
  if (query.days >= 30) {
    return DateRangeFilter::Last30Days;
  }
  if (query.days >= 7) {
    return DateRangeFilter::Last7Days;
  }
  if (query.days > 0) {
    return DateRangeFilter::Today;
  }
  return query.date_range;
}

std::int64_t DateRangeSince(DateRangeFilter range) {
  switch (range) {
    case DateRangeFilter::Today:
      return NowEpochMs() - 24LL * 60LL * 60LL * 1000LL;
    case DateRangeFilter::Last7Days:
      return NowEpochMs() - 7LL * 24LL * 60LL * 60LL * 1000LL;
    case DateRangeFilter::Last30Days:
      return NowEpochMs() - 30LL * 24LL * 60LL * 60LL * 1000LL;
    case DateRangeFilter::All:
    default:
      return 0;
  }
}

std::string SearchableProjection(const CapturedClip& clip) {
  if (!clip.plain_text.empty()) {
    return clip.plain_text;
  }
  return MakeSnippet(clip);
}

std::vector<ClipFormatPayload> BuildFallbackFormats(const CapturedClip& clip) {
  std::vector<ClipFormatPayload> formats;
  int order = 0;
  if (!clip.plain_text.empty()) {
    formats.push_back(ClipFormatPayload{"CF_UNICODETEXT", ClipFormatPayloadKind::Text, clip.plain_text, {}, {}, order++});
  }
  if (!clip.html_text.empty()) {
    formats.push_back(ClipFormatPayload{"HTML Format", ClipFormatPayloadKind::Text, clip.html_text, {}, {}, order++});
  }
  if (!clip.rtf_text.empty()) {
    formats.push_back(ClipFormatPayload{"Rich Text Format", ClipFormatPayloadKind::Text, clip.rtf_text, {}, {}, order++});
  }
  if (clip.content_type == ClipContentType::Files && !clip.plain_text.empty()) {
    formats.push_back(ClipFormatPayload{"CF_HDROP", ClipFormatPayloadKind::Text, clip.plain_text, {}, {}, order++});
  }
  return formats;
}

std::vector<ClipFormatPayload> FormatsForPersistence(const CapturedClip& clip) {
  std::vector<ClipFormatPayload> formats = clip.formats.empty() ? BuildFallbackFormats(clip) : clip.formats;
  if (clip.content_type != ClipContentType::Image) {
    return formats;
  }

  std::vector<ClipFormatPayload> filtered;
  filtered.reserve(formats.size());
  for (auto& format : formats) {
    if (format.format_name == "CF_HDROP") {
      filtered.push_back(std::move(format));
    }
  }
  return filtered;
}

std::wstring FirstFilePathFromText(std::string_view utf8_text) {
  const std::wstring wide = common::Utf8ToWide(utf8_text).wide;
  const auto parts = common::SplitWide(wide, L'\n');
  for (const auto& part : parts) {
    const std::wstring trimmed = common::TrimWide(part);
    if (!trimmed.empty()) {
      return trimmed;
    }
  }
  return {};
}

std::wstring FileDisplayName(std::wstring_view path) {
  if (path.empty()) {
    return L"";
  }
  const size_t pos = path.find_last_of(L"\\/");
  if (pos == std::wstring_view::npos) {
    return std::wstring(path);
  }
  return std::wstring(path.substr(pos + 1));
}

std::wstring DirectoryFromPath(std::wstring_view path) {
  if (path.empty()) {
    return L"";
  }
  std::filesystem::path fs_path(path);
  if (fs_path.has_parent_path()) {
    return fs_path.parent_path().wstring();
  }
  return std::wstring(path);
}

std::wstring FileSecondaryText(std::wstring_view path, int file_count) {
  if (file_count > 1) {
    return DirectoryFromPath(path);
  }
  return std::wstring(path);
}

std::wstring EscapeLikePattern(std::wstring_view text) {
  std::wstring escaped;
  escaped.reserve(text.size() + 4);
  for (wchar_t ch : text) {
    if (ch == L'%' || ch == L'_' || ch == L'\\') {
      escaped.push_back(L'\\');
    }
    escaped.push_back(ch);
  }
  return escaped;
}

std::wstring LikePattern(const std::wstring& text) {
  std::wstring pattern = L"%";
  pattern += text;
  pattern += L"%";
  return pattern;
}

std::vector<std::wstring> ParseFilePathsFromTextImpl(std::string_view utf8_text) {
  std::vector<std::wstring> paths;
  const std::wstring wide = common::Utf8ToWide(utf8_text).wide;
  const auto parts = common::SplitWide(wide, L'\n');
  for (const auto& part : parts) {
    const std::wstring trimmed = common::TrimWide(part);
    if (!trimmed.empty()) {
      paths.push_back(trimmed);
    }
  }
  return paths;
}

bool IsImageExtension(std::wstring_view path) {
  const std::wstring lowered = common::ToLowerWide(std::wstring(path));
  return lowered.ends_with(L".png") || lowered.ends_with(L".jpg") || lowered.ends_with(L".jpeg") ||
      lowered.ends_with(L".bmp") || lowered.ends_with(L".gif") || lowered.ends_with(L".webp") ||
      lowered.ends_with(L".tif") || lowered.ends_with(L".tiff");
}

bool IsProtectedStorageTier(StorageTier tier) {
  return tier == StorageTier::Library;
}

bool IsFrequentClip(int paste_count, std::int64_t last_pasted_at) {
  if (paste_count >= 5) {
    return true;
  }
  if (paste_count >= 3 && last_pasted_at > 0) {
    const std::int64_t since = NowEpochMs() - 14LL * 24LL * 60LL * 60LL * 1000LL;
    return last_pasted_at >= since;
  }
  return false;
}

struct ClipIdentityData {
  ClipIdentityKind kind = ClipIdentityKind::LegacyPayload;
  std::string key;
  int version = 3;
};

struct IdentityCandidate {
  std::int64_t id = 0;
};

std::string NormalizeLineEndings(std::string_view text) {
  return policies::NormalizeLineEndings(text);
}

std::string NormalizeTextIdentityField(std::string_view text) {
  return policies::NormalizeTextIdentityField(text);
}

std::string NormalizeStructuredTextIdentityField(std::string_view text) {
  return policies::NormalizeStructuredTextIdentityField(text);
}

std::wstring NormalizeWindowsPathForIdentity(std::wstring_view path) {
  return policies::NormalizeWindowsPathForIdentity(path);
}

struct FileSnapshotIdentityData {
  std::wstring normalized_path;
  std::uint64_t file_size = 0;
  std::uint64_t last_write_time_utc = 0;
};

std::optional<FileSnapshotIdentityData> ReadFileSnapshotIdentityData(std::wstring_view path) {
  if (const auto snapshot = policies::ReadFileSnapshotIdentityData(path); snapshot.has_value()) {
    return FileSnapshotIdentityData{
        snapshot->normalized_path,
        snapshot->file_size,
        snapshot->last_write_time_utc};
  }
  return std::nullopt;
}

std::string BuildTextPayloadSetIdentityKey(const CapturedClip& clip) {
  return policies::BuildTextPayloadSetIdentityKey(clip);
}

std::string BuildImageFileSnapshotIdentityKey(const FileSnapshotIdentityData& snapshot) {
  policies::FileSnapshotIdentityData policy_snapshot;
  policy_snapshot.normalized_path = snapshot.normalized_path;
  policy_snapshot.file_size = snapshot.file_size;
  policy_snapshot.last_write_time_utc = snapshot.last_write_time_utc;
  return policies::BuildImageFileSnapshotIdentityKey(policy_snapshot);
}

ClipIdentityData BuildClipIdentityData(const CapturedClip& clip) {
  ClipIdentityData identity;
  identity.version = 3;
  identity.kind = policies::ResolveIdentityKind(clip);
  if (identity.kind == ClipIdentityKind::ImageContentHash) {
    identity.version = 4;
  }
  identity.key = policies::ResolveIdentityKey(clip, identity.kind);
  return identity;
}

std::vector<IdentityCandidate> FindCandidatesByIdentity(
    sqlite3* db,
    ClipIdentityKind kind,
    const std::string& key) {
  std::vector<IdentityCandidate> candidates;
  if (key.empty()) {
    return candidates;
  }

  Statement statement(
      db,
      "SELECT id"
      " FROM clips WHERE identity_kind = ?1 AND identity_key = ?2"
      " ORDER BY updated_at DESC, id DESC");
  BindText(statement.get(), 1, ToSqlIdentityKind(kind));
  BindText(statement.get(), 2, key);
  while (sqlite3_step(statement.get()) == SQLITE_ROW) {
    IdentityCandidate candidate;
    candidate.id = sqlite3_column_int64(statement.get(), 0);
    candidates.push_back(std::move(candidate));
  }
  return candidates;
}

common::Status UpdateClipIdentity(
    sqlite3* db,
    std::int64_t clip_id,
    ClipIdentityKind kind,
    const std::string& key,
    int version) {
  Statement statement(
      db,
      "UPDATE clips SET identity_kind = ?1, identity_key = ?2, identity_version = ?3 WHERE id = ?4");
  BindText(statement.get(), 1, ToSqlIdentityKind(kind));
  BindText(statement.get(), 2, key);
  sqlite3_bind_int(statement.get(), 3, version);
  sqlite3_bind_int64(statement.get(), 4, clip_id);
  if (sqlite3_step(statement.get()) != SQLITE_DONE) {
    return SqliteError(db, "更新条目标识失败");
  }
  return common::Status::Ok();
}

std::optional<std::int64_t> FindLegacyTextCandidate(
    sqlite3* db,
    const CapturedClip& clip,
    const ClipIdentityData& identity) {
  Statement statement(
      db,
      "SELECT id FROM clips"
      " WHERE content_type = ?1 AND plain_text = ?2 AND html_text = ?3 AND rtf_text = ?4"
      " AND (identity_key = '' OR identity_kind IN ('legacy_payload', 'text_title_plain'))"
      " ORDER BY updated_at DESC, id DESC LIMIT 1");
  BindText(statement.get(), 1, ToSqlContentType(clip.content_type));
  BindText(statement.get(), 2, clip.plain_text);
  BindText(statement.get(), 3, clip.html_text);
  BindText(statement.get(), 4, clip.rtf_text);
  if (sqlite3_step(statement.get()) == SQLITE_ROW) {
    const std::int64_t id = sqlite3_column_int64(statement.get(), 0);
    const common::Status status = UpdateClipIdentity(db, id, identity.kind, identity.key, identity.version);
    if (status.ok()) {
      return id;
    }
  }
  return std::nullopt;
}

std::optional<std::int64_t> FindLegacyImageFileCandidate(
    sqlite3* db,
    const CapturedClip& clip,
    const ClipIdentityData& identity) {
  const auto current_paths = ParseFilePathsFromTextImpl(clip.plain_text);
  if (current_paths.size() != 1 || !IsImageExtension(current_paths.front())) {
    return std::nullopt;
  }
  if (identity.kind != ClipIdentityKind::ImageFileSnapshot || identity.key.empty()) {
    return std::nullopt;
  }

  Statement statement(
      db,
      "SELECT id, secondary_path, plain_text FROM clips"
      " WHERE content_type = 'files'"
      " AND (identity_key = '' OR identity_kind IN ('legacy_payload', 'image_file_path'))"
      " ORDER BY updated_at DESC, id DESC LIMIT 256");
  while (sqlite3_step(statement.get()) == SQLITE_ROW) {
    const std::int64_t id = sqlite3_column_int64(statement.get(), 0);
    std::wstring legacy_path = ToWideOrEmpty(ColumnString(statement.get(), 1).c_str());
    if (legacy_path.empty()) {
      const auto legacy_paths = ParseFilePathsFromTextImpl(ColumnString(statement.get(), 2));
      if (legacy_paths.size() != 1 || !IsImageExtension(legacy_paths.front())) {
        continue;
      }
      legacy_path = legacy_paths.front();
    }
    const auto legacy_snapshot = ReadFileSnapshotIdentityData(legacy_path);
    if (!legacy_snapshot.has_value()) {
      continue;
    }
    if (BuildImageFileSnapshotIdentityKey(*legacy_snapshot) != identity.key) {
      continue;
    }
    const common::Status status = UpdateClipIdentity(db, id, identity.kind, identity.key, identity.version);
    if (status.ok()) {
      return id;
    }
  }
  return std::nullopt;
}

common::Status InsertClipFormats(
    sqlite3* db,
    std::int64_t clip_id,
    const std::vector<ClipFormatPayload>& formats) {
  Statement statement(
      db,
      "INSERT INTO clip_formats("
      " clip_id, format_name, format_text, payload_kind, blob_data, storage_path, format_order"
      " ) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)");
  for (const auto& format : formats) {
    sqlite3_reset(statement.get());
    sqlite3_clear_bindings(statement.get());
    sqlite3_bind_int64(statement.get(), 1, clip_id);
    BindText(statement.get(), 2, format.format_name);
    BindText(statement.get(), 3, format.text_data);
    BindText(statement.get(), 4, ToSqlPayloadKind(format.payload_kind));
    BindBlob(statement.get(), 5, format.binary_data);
    BindWide(statement.get(), 6, format.storage_path);
    sqlite3_bind_int(statement.get(), 7, format.format_order);
    if (sqlite3_step(statement.get()) != SQLITE_DONE) {
      return SqliteError(db, "插入标准格式失败");
    }
  }
  return common::Status::Ok();
}

common::Status LoadClipFormats(
    sqlite3* db,
    std::int64_t clip_id,
    std::vector<ClipFormatPayload>* formats) {
  if (formats == nullptr) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "输出格式集合不能为空。");
  }
  formats->clear();

  Statement statement(
      db,
      "SELECT format_name, format_text, payload_kind, blob_data, storage_path, format_order"
      " FROM clip_formats WHERE clip_id = ?1 ORDER BY format_order ASC, id ASC");
  sqlite3_bind_int64(statement.get(), 1, clip_id);
  while (sqlite3_step(statement.get()) == SQLITE_ROW) {
    ClipFormatPayload payload;
    payload.format_name = ColumnString(statement.get(), 0);
    payload.text_data = ColumnString(statement.get(), 1);
    payload.payload_kind = ParseSqlPayloadKind(ColumnString(statement.get(), 2));
    payload.binary_data = ColumnBlob(statement.get(), 3);
    payload.storage_path = ToWideOrEmpty(ColumnString(statement.get(), 4).c_str());
    payload.format_order = sqlite3_column_int(statement.get(), 5);
    formats->push_back(std::move(payload));
  }
  return common::Status::Ok();
}

common::Status LoadClipTags(
    sqlite3* db,
    std::int64_t clip_id,
    std::vector<TagRecord>* tags) {
  if (tags == nullptr) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "标签输出对象不能为空。");
  }
  tags->clear();
  Statement statement(
      db,
      "SELECT t.id, t.name, t.color, t.is_system, ct.sort_order"
      " FROM clip_tags ct"
      " JOIN tags t ON t.id = ct.tag_id"
      " WHERE ct.clip_id = ?1"
      " ORDER BY ct.sort_order ASC, t.sort_order ASC, t.name ASC");
  sqlite3_bind_int64(statement.get(), 1, clip_id);
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

void AppendQuerySql(
    const ClipQuery& query,
    std::string* sql,
    std::vector<std::function<void(sqlite3_stmt*, int*)>>* binders) {
  const std::optional<ClipContentType> view_type = ViewContentType(query.view);
  const std::optional<StorageTier> view_tier = ViewStorageTier(query.view);
  const bool use_group = query.view == ClipView::Group && (query.group_id.has_value() || !query.group_name.empty());
  const bool use_queue = query.view == ClipView::Queue || query.queue_only;
  const bool use_frequent = query.view == ClipView::Frequent || query.frequent_only;
  const bool use_search = !common::TrimAscii(query.search_text).empty();
  const DateRangeFilter date_range = NormalizeDateRange(query);

  if (query.view == ClipView::Favorites) {
    *sql += " AND c.is_favorite = 1";
  } else if (query.view == ClipView::Pinned) {
    *sql += " AND c.is_pinned = 1";
  } else if (view_type.has_value()) {
    *sql += " AND c.content_type = ?";
    binders->push_back([view_type](sqlite3_stmt* stmt, int* index) {
      BindText(stmt, (*index)++, ToSqlContentType(*view_type));
    });
  } else if (use_group) {
    if (query.group_id.has_value()) {
      *sql += " AND c.group_id = ?";
      binders->push_back([group_id = *query.group_id](sqlite3_stmt* stmt, int* index) {
        sqlite3_bind_int64(stmt, (*index)++, group_id);
      });
    } else {
      *sql += " AND c.group_id = (SELECT id FROM groups WHERE name = ?)";
      binders->push_back([group_name = query.group_name](sqlite3_stmt* stmt, int* index) {
        BindWide(stmt, (*index)++, group_name);
      });
    }
  }
  if (view_tier.has_value()) {
    *sql += " AND c.storage_tier = ?";
    binders->push_back([tier = *view_tier](sqlite3_stmt* stmt, int* index) {
      BindText(stmt, (*index)++, ToSettingsValue(tier));
    });
  }
  if (use_queue) {
    *sql += " AND EXISTS (SELECT 1 FROM paste_queue q WHERE q.clip_id = c.id)";
  }
  if (use_frequent) {
    *sql += " AND (c.paste_count >= 5 OR (c.paste_count >= 3 AND c.last_pasted_at >= ?))";
    const std::int64_t frequent_since = NowEpochMs() - 14LL * 24LL * 60LL * 60LL * 1000LL;
    binders->push_back([frequent_since](sqlite3_stmt* stmt, int* index) {
      sqlite3_bind_int64(stmt, (*index)++, frequent_since);
    });
  }

  if (query.favorite_only) {
    *sql += " AND c.is_favorite = 1";
  }
  if (query.pinned_only) {
    *sql += " AND c.is_pinned = 1";
  }
  if (query.type_filter.has_value()) {
    *sql += " AND c.content_type = ?";
    binders->push_back([type = *query.type_filter](sqlite3_stmt* stmt, int* index) {
      BindText(stmt, (*index)++, ToSqlContentType(type));
    });
  }
  if (query.storage_tier_filter.has_value()) {
    *sql += " AND c.storage_tier = ?";
    binders->push_back([tier = *query.storage_tier_filter](sqlite3_stmt* stmt, int* index) {
      BindText(stmt, (*index)++, ToSettingsValue(tier));
    });
  }
  if (!query.source_app_name.empty()) {
    *sql += " AND c.source_app_name LIKE ? ESCAPE '\\'";
    binders->push_back([value = LikePattern(EscapeLikePattern(query.source_app_name))](sqlite3_stmt* stmt, int* index) {
      BindWide(stmt, (*index)++, value);
    });
  }
  const std::int64_t range_since = DateRangeSince(date_range);
  if (range_since > 0) {
    *sql += " AND c.updated_at >= ?";
    binders->push_back([range_since](sqlite3_stmt* stmt, int* index) {
      sqlite3_bind_int64(stmt, (*index)++, range_since);
    });
  }
  if (!query.tag_ids.empty()) {
    *sql += " AND EXISTS (SELECT 1 FROM clip_tags ct WHERE ct.clip_id = c.id AND ct.tag_id IN (";
    for (size_t i = 0; i < query.tag_ids.size(); ++i) {
      if (i > 0) {
        *sql += ",";
      }
      *sql += "?";
    }
    *sql += "))";
    binders->push_back([ids = query.tag_ids](sqlite3_stmt* stmt, int* index) {
      for (const auto id : ids) {
        sqlite3_bind_int64(stmt, (*index)++, id);
      }
    });
  }
  if (use_search) {
    *sql += " AND f.clips_fts MATCH ?";
    binders->push_back([query_text = BuildMatchQuery(query.search_text)](sqlite3_stmt* stmt, int* index) {
      BindText(stmt, (*index)++, query_text);
    });
  }
}

std::set<std::wstring> CollectStoragePaths(sqlite3* db, std::int64_t clip_id) {
  std::set<std::wstring> paths;
  {
    Statement statement(
        db,
        "SELECT storage_path, thumbnail_path, preview_path FROM clips WHERE id = ?1");
    sqlite3_bind_int64(statement.get(), 1, clip_id);
    while (sqlite3_step(statement.get()) == SQLITE_ROW) {
      for (int column = 0; column < 3; ++column) {
        const std::wstring path = ToWideOrEmpty(ColumnString(statement.get(), column).c_str());
        if (!path.empty()) {
          paths.insert(path);
        }
      }
    }
  }
  {
    Statement statement(db, "SELECT storage_path FROM clip_formats WHERE clip_id = ?1 AND storage_path <> ''");
    sqlite3_bind_int64(statement.get(), 1, clip_id);
    while (sqlite3_step(statement.get()) == SQLITE_ROW) {
      const std::wstring path = ToWideOrEmpty(ColumnString(statement.get(), 0).c_str());
      if (!path.empty()) {
        paths.insert(path);
      }
    }
  }
  return paths;
}

std::set<std::wstring> CollectStaleStoragePaths(sqlite3* db, int keep_limit) {
  std::set<std::wstring> paths;
  if (keep_limit <= 0) {
    return paths;
  }
  Statement statement(
      db,
      "SELECT id FROM clips WHERE id NOT IN ("
      " SELECT id FROM clips ORDER BY is_pinned DESC, updated_at DESC LIMIT ?1"
      " ) AND is_pinned = 0");
  sqlite3_bind_int(statement.get(), 1, keep_limit);
  while (sqlite3_step(statement.get()) == SQLITE_ROW) {
    const auto clip_paths = CollectStoragePaths(db, sqlite3_column_int64(statement.get(), 0));
    paths.insert(clip_paths.begin(), clip_paths.end());
  }
  return paths;
}

}  // namespace

#if defined(SCRAPS_ENABLE_TEST_HOOKS)
extern ClipRepository::TestFailPoint g_clip_repository_test_fail_point;
common::Status MaybeFailForTests(ClipRepository::TestFailPoint fail_point, const char* message);
#endif

}  // namespace clipd2d::core::store
