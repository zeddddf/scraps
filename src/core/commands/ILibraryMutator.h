#pragma once

// ILibraryMutator —— 剪贴库写路径面。
// 覆盖「改库结构」这一用例：标签/分组/队列/收藏/置顶的增删改与重排、存储层级迁移、
// 批量移动、清空历史。读路径见 IHistoryFeed，粘贴见 IPasteService。
// 变更本接口 = 改"UI 能对库做什么写操作"。

#include <cstdint>
#include <string>
#include <vector>

#include "common/Status.h"
#include "core/store/ClipTypes.h"

namespace clipd2d::core::commands {

class ILibraryMutator {
 public:
  virtual ~ILibraryMutator() = default;

  // 单条属性
  virtual common::Status TogglePinned(std::int64_t id, bool pinned) = 0;
  virtual common::Status ToggleFavorite(std::int64_t id, bool favorite) = 0;
  virtual common::Status SetStorageTier(std::int64_t id, store::StorageTier tier) = 0;
  virtual common::Status DeleteClip(std::int64_t id) = 0;
  virtual common::Status ClearHistory() = 0;

  // 标签
  virtual common::Status EnsureTag(const std::wstring& name, const std::wstring& color, std::int64_t* tag_id) = 0;
  virtual common::Status AddTag(std::int64_t id, std::int64_t tag_id) = 0;
  virtual common::Status RemoveTag(std::int64_t id, std::int64_t tag_id) = 0;
  virtual common::Status RenameTag(std::int64_t tag_id, const std::wstring& name, const std::wstring& color) = 0;
  virtual common::Status DeleteTag(std::int64_t tag_id) = 0;
  virtual common::Status ReorderTagItems(std::int64_t tag_id, std::int64_t id, int new_sort_order) = 0;

  // 队列
  virtual common::Status AddToQueue(std::int64_t id) = 0;
  virtual common::Status RemoveFromQueue(std::int64_t id) = 0;
  virtual common::Status MoveQueueToFront(std::int64_t id) = 0;
  virtual common::Status ReorderQueue(std::int64_t id, int new_sort_order) = 0;
  virtual common::Status ReorderFavorites(std::int64_t id, int new_sort_order) = 0;
  virtual common::Status ReorderPinned(std::int64_t id, int new_sort_order) = 0;
  // 批量重排：拖拽重排一次提交，单事务（store::ReorderEntry 见 ClipTypes.h）。
  virtual common::Status ReorderQueueBatch(const std::vector<store::ReorderEntry>& entries) = 0;
  virtual common::Status ReorderFavoritesBatch(const std::vector<store::ReorderEntry>& entries) = 0;
  virtual common::Status ReorderPinnedBatch(const std::vector<store::ReorderEntry>& entries) = 0;
  virtual common::Status ReorderTagItemsBatch(std::int64_t tag_id, const std::vector<store::ReorderEntry>& entries) = 0;

  // 分组与归属
  virtual common::Status EnsureGroup(const std::wstring& name, std::int64_t* group_id) = 0;
  virtual common::Status RenameGroup(std::int64_t group_id, const std::wstring& name) = 0;
  virtual common::Status DeleteGroup(std::int64_t group_id) = 0;
  virtual common::Status ReorderGroup(std::int64_t group_id, int new_sort_order) = 0;
  virtual common::Status SetGroupTabPinned(std::int64_t group_id, bool pinned, int tab_sort_order) = 0;
  virtual common::Status ReorderGroupTab(std::int64_t group_id, int new_tab_sort_order) = 0;
  virtual common::Status SetGroup(std::int64_t id, std::int64_t group_id) = 0;
  virtual common::Status MoveClipsToGroup(const std::vector<std::int64_t>& ids, std::int64_t group_id) = 0;
  virtual common::Status MoveClipsToLibraryRoot(const std::vector<std::int64_t>& ids) = 0;
  virtual common::Status MoveClipsToHistory(const std::vector<std::int64_t>& ids) = 0;
};

}  // namespace clipd2d::core::commands
