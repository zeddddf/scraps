#include "core/store/ClipRepository.Internal.h"

namespace clipd2d::core::store {

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

common::Status ClipRepository::ReorderQueueBatch(const std::vector<ReorderEntry>& entries) {
  // 单事务 + 复用一条 prepared 语句：N 条 UPDATE 一次提交，任一条失败整体回滚。
  ScopedTransaction transaction(db_);
  if (!transaction.status().ok()) {
    return transaction.status();
  }
  Statement statement(db_, "UPDATE paste_queue SET sort_order = ?1 WHERE clip_id = ?2");
  for (const ReorderEntry& entry : entries) {
    sqlite3_reset(statement.get());
    sqlite3_clear_bindings(statement.get());
    sqlite3_bind_int(statement.get(), 1, entry.sort_order);
    sqlite3_bind_int64(statement.get(), 2, entry.id);
    if (sqlite3_step(statement.get()) != SQLITE_DONE) {
      return SqliteError(db_, "批量调整队列顺序失败");  // 析构回滚
    }
  }
  return transaction.Commit();
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

}  // namespace clipd2d::core::store
