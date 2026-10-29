#include "core/store/ClipRepository.Internal.h"

namespace clipd2d::core::store {

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

common::Status ClipRepository::ReorderFavoritesBatch(const std::vector<ReorderEntry>& entries) {
  ScopedTransaction transaction(db_);
  if (!transaction.status().ok()) {
    return transaction.status();
  }
  Statement statement(db_, "UPDATE clips SET favorite_sort_order = ?1 WHERE id = ?2 AND is_favorite = 1");
  for (const ReorderEntry& entry : entries) {
    sqlite3_reset(statement.get());
    sqlite3_clear_bindings(statement.get());
    sqlite3_bind_int(statement.get(), 1, entry.sort_order);
    sqlite3_bind_int64(statement.get(), 2, entry.id);
    if (sqlite3_step(statement.get()) != SQLITE_DONE) {
      return SqliteError(db_, "批量调整收藏顺序失败");
    }
  }
  return transaction.Commit();
}

common::Status ClipRepository::ReorderPinnedBatch(const std::vector<ReorderEntry>& entries) {
  ScopedTransaction transaction(db_);
  if (!transaction.status().ok()) {
    return transaction.status();
  }
  Statement statement(db_, "UPDATE clips SET pin_sort_order = ?1 WHERE id = ?2 AND is_pinned = 1");
  for (const ReorderEntry& entry : entries) {
    sqlite3_reset(statement.get());
    sqlite3_clear_bindings(statement.get());
    sqlite3_bind_int(statement.get(), 1, entry.sort_order);
    sqlite3_bind_int64(statement.get(), 2, entry.id);
    if (sqlite3_step(statement.get()) != SQLITE_DONE) {
      return SqliteError(db_, "批量调整置顶顺序失败");
    }
  }
  return transaction.Commit();
}

}  // namespace clipd2d::core::store
