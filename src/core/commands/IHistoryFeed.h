#pragma once

// IHistoryFeed —— 历史列表读取/查询面。
// 覆盖「让 UI 看见剪贴库」这一用例：列表、详情、标签、队列、分组、动作解析。
// 变更本接口 = 改"UI 能读到什么"；写路径见 ILibraryMutator，粘贴见 IPasteService。

#include <cstdint>
#include <string>
#include <vector>

#include "common/Status.h"
#include "core/actions/ActionDescriptor.h"
#include "core/store/ClipTypes.h"

namespace clipd2d::core::commands {

class IHistoryFeed {
 public:
  virtual ~IHistoryFeed() = default;

  virtual common::Status CountClips(const store::ClipQuery& query, std::int64_t* count) = 0;
  virtual common::Status ListClips(
      const store::ClipQuery& query,
      int offset,
      int limit,
      std::vector<store::ClipSummary>* clips) = 0;
  virtual common::Status GetClipDetailsById(std::int64_t id, store::ClipRecord* clip) = 0;
  virtual common::Status GetClipById(std::int64_t id, store::ClipRecord* clip) = 0;
  virtual common::Status GetLatestClip(store::ClipRecord* clip) = 0;
  virtual common::Status LoadBootstrapState(store::BootstrapState* state) = 0;
  virtual common::Status HasAnyClips(bool* has_any_clips) = 0;
  virtual common::Status HasAnySettingsData(bool* has_any_settings) = 0;

  virtual common::Status ListTags(std::vector<store::TagRecord>* tags) = 0;
  virtual common::Status ListClipTags(std::int64_t id, std::vector<store::TagRecord>* tags) = 0;
  virtual common::Status ListQueue(std::vector<store::QueueEntry>* entries) = 0;
  virtual common::Status ListGroups(std::vector<store::GroupRecord>* groups) = 0;

  virtual std::vector<actions::ActionDescriptor> ResolveActions(const store::ClipRecord& clip) const = 0;
  virtual std::string BuildSearchJson(const std::vector<store::ClipSummary>& clips) const = 0;
};

}  // namespace clipd2d::core::commands
