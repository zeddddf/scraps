#include "core/store/ClipRepository.Internal.h"

namespace clipd2d::core::store {

std::optional<std::int64_t> ClipRepository::FindExistingByHash(const std::string& hash) const {
  Statement statement(db_, "SELECT id FROM clips WHERE content_hash = ?1 ORDER BY id DESC LIMIT 1");
  BindText(statement.get(), 1, hash);
  if (sqlite3_step(statement.get()) == SQLITE_ROW) {
    return sqlite3_column_int64(statement.get(), 0);
  }
  return std::nullopt;
}

common::Status ClipRepository::UpsertFts(
    std::int64_t clip_id,
    const std::string& title,
    const std::string& plain_text,
    const std::string& ocr_text) {
  Statement insert_stmt(db_, "INSERT OR REPLACE INTO clips_fts(rowid, title, plain_text, ocr_text) VALUES (?1, ?2, ?3, ?4)");
  sqlite3_bind_int64(insert_stmt.get(), 1, clip_id);
  BindText(insert_stmt.get(), 2, title);
  BindText(insert_stmt.get(), 3, plain_text);
  BindText(insert_stmt.get(), 4, ocr_text);
  if (sqlite3_step(insert_stmt.get()) != SQLITE_DONE) {
    return SqliteError(db_, "更新搜索索引失败");
  }
  return common::Status::Ok();
}

const char* ToSqlContentType(ClipContentType content_type) {
  switch (content_type) {
    case ClipContentType::Text:
      return "text";
    case ClipContentType::Html:
      return "html";
    case ClipContentType::Rtf:
      return "rtf";
    case ClipContentType::Image:
      return "image";
    case ClipContentType::Files:
      return "files";
  }
  return "text";
}

ClipContentType ParseSqlContentType(const std::string& value) {
  if (value == "html") {
    return ClipContentType::Html;
  }
  if (value == "rtf") {
    return ClipContentType::Rtf;
  }
  if (value == "image") {
    return ClipContentType::Image;
  }
  if (value == "files") {
    return ClipContentType::Files;
  }
  return ClipContentType::Text;
}

const wchar_t* ToDisplayLabel(ClipContentType content_type) {
  switch (content_type) {
    case ClipContentType::Text:
      return L"文本";
    case ClipContentType::Html:
      return L"HTML";
    case ClipContentType::Rtf:
      return L"RTF";
    case ClipContentType::Image:
      return L"图片";
    case ClipContentType::Files:
      return L"文件";
  }
  return L"文本";
}

const char* ToSettingsValue(StorageTier tier) {
  switch (tier) {
    case StorageTier::Library:
      return "library";
    case StorageTier::History:
    default:
      return "history";
  }
}

StorageTier ParseStorageTier(std::string_view value) {
  if (value == "library") {
    return StorageTier::Library;
  }
  return StorageTier::History;
}

const wchar_t* ToDisplayLabel(StorageTier tier) {
  switch (tier) {
    case StorageTier::Library:
      return L"资料库";
    case StorageTier::History:
    default:
      return L"历史";
  }
}

const wchar_t* ToDisplayLabel(DateRangeFilter range) {
  switch (range) {
    case DateRangeFilter::Today:
      return L"今天";
    case DateRangeFilter::Last7Days:
      return L"近 7 天";
    case DateRangeFilter::Last30Days:
      return L"近 30 天";
    case DateRangeFilter::All:
    default:
      return L"全部";
  }
}

const char* ToSqlPayloadKind(ClipFormatPayloadKind kind) {
  switch (kind) {
    case ClipFormatPayloadKind::Text:
      return "text";
    case ClipFormatPayloadKind::Binary:
      return "binary";
  }
  return "text";
}

ClipFormatPayloadKind ParseSqlPayloadKind(std::string_view value) {
  if (value == "binary") {
    return ClipFormatPayloadKind::Binary;
  }
  return ClipFormatPayloadKind::Text;
}

const char* ToSqlIdentityKind(ClipIdentityKind kind) {
  return policies::ToSqlIdentityKind(kind);
}

ClipIdentityKind ParseSqlIdentityKind(std::string_view value) {
  return policies::ParseSqlIdentityKind(value);
}

const char* ToSettingsValue(ListDensity density) {
  switch (density) {
    case ListDensity::Compact:
      return "compact";
    case ListDensity::Comfortable:
      return "comfortable";
  }
  return "comfortable";
}

ListDensity ParseListDensity(std::string_view value) {
  if (value == "compact") {
    return ListDensity::Compact;
  }
  return ListDensity::Comfortable;
}

const wchar_t* ToDisplayLabel(ListDensity density) {
  switch (density) {
    case ListDensity::Compact:
      return L"紧凑";
    case ListDensity::Comfortable:
      return L"舒适";
  }
  return L"舒适";
}

const char* ToSettingsValue(WindowPositionMode mode) {
  switch (mode) {
    case WindowPositionMode::Remember:
      return "remember";
    case WindowPositionMode::FollowMouse:
      return "follow_mouse";
  }
  return "remember";
}

WindowPositionMode ParseWindowPositionMode(std::string_view value) {
  if (value == "follow_mouse") {
    return WindowPositionMode::FollowMouse;
  }
  return WindowPositionMode::Remember;
}

const wchar_t* ToDisplayLabel(WindowPositionMode mode) {
  switch (mode) {
    case WindowPositionMode::Remember:
      return L"记住位置";
    case WindowPositionMode::FollowMouse:
      return L"跟随鼠标";
  }
  return L"记住位置";
}

const char* ToSettingsValue(ClickPasteMode mode) {
  switch (mode) {
    case ClickPasteMode::Single:
      return "single";
    case ClickPasteMode::Double:
      return "double";
  }
  return "single";
}

ClickPasteMode ParseClickPasteMode(std::string_view value) {
  if (value == "double") {
    return ClickPasteMode::Double;
  }
  return ClickPasteMode::Single;
}

const wchar_t* ToDisplayLabel(ClickPasteMode mode) {
  switch (mode) {
    case ClickPasteMode::Single:
      return L"单击粘贴";
    case ClickPasteMode::Double:
      return L"双击粘贴";
  }
  return L"单击粘贴";
}

}  // namespace clipd2d::core::store
