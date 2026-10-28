#pragma once

// 跨 TU 交接的异步消息 payload（OwnedMessage 用）。
// 必须是具名实体：匿名命名空间里的类型会分裂成"同名不同类型"，
// 跨 TU 的 new/delete 交接（工作线程构造 → UI 线程析构）类型身份不一致。

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "common/Status.h"
#include "core/store/ClipTypes.h"
#include "ui/ClipVisualAssets.h"

namespace clipd2d::ui {

struct CountReadyPayload {
  std::uint64_t generation = 0;
  common::Status status;
  std::int64_t count = 0;
};

struct PageReadyPayload {
  std::uint64_t generation = 0;
  int page_index = 0;
  common::Status status;
  std::vector<core::store::ClipSummary> clips;
};

struct ClipReadyPayload {
  std::uint64_t generation = 0;
  std::int64_t clip_id = 0;
  common::Status status;
  std::optional<core::store::ClipRecord> clip;
};

struct StartupMetadataReadyPayload {
  common::Status groups_status;
  common::Status tags_status;
  common::Status queue_status;
  std::vector<core::store::GroupRecord> groups;
  std::vector<core::store::TagRecord> tags;
  int queue_count = 0;
};

struct BitmapReadyPayload {
  std::uint64_t generation = 0;
  std::wstring key;
  std::wstring source_path;
  visual_assets::ShellIconPurpose shell_icon_purpose = visual_assets::ShellIconPurpose::FilePrimary;
  UINT width = 0;
  UINT height = 0;
  std::vector<std::uint8_t> pixels;
};

}  // namespace clipd2d::ui
