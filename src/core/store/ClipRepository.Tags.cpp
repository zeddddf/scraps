#include "core/store/ClipRepository.Internal.h"

namespace clipd2d::core::store {

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

}  // namespace clipd2d::core::store
