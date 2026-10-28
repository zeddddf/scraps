#include "core/store/ClipRepository.Internal.h"

namespace clipd2d::core::store {

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

}  // namespace clipd2d::core::store
