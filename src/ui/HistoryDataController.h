#pragma once

#include <cstdint>

#include "core/commands/RefreshRequest.h"
#include "core/store/ClipTypes.h"

namespace clipd2d::ui {

class MainWindow;

enum class RefreshDataMode {
  Full,
  FastStartup,
};

// Responsibility: Own MainWindow's history-query and detail-loading workflow entrypoints.
// Inputs: MainWindow state, AppController query/detail APIs, and async result payload messages.
// Outputs: Updated MainWindow list/detail state ready for rendering.
// Forbidden dependencies: Direct rendering code, D2D bitmap creation, or repository SQL.

class HistoryDataController {
 public:
  explicit HistoryDataController(MainWindow& owner);

  void RefreshGroups();
  void RefreshTags();
  void RefreshData(const core::commands::RefreshRequest& request, RefreshDataMode mode);
  void RequestCountAsync(core::store::ClipQuery query, std::uint64_t generation);
  void RequestPageAsync(int page_index);
  void RequestPageAsync(int page_index, std::uint64_t generation, const core::store::ClipQuery& query, bool urgent = false);
  void RequestSelectedClipAsync(std::int64_t clip_id);
  void RequestStartupMetadataAsync();
  // P0-2:groups/tags/queue 元数据三件套异步加载。generation != 0 时结果按
  // refresh_generation_ 门控(过期丢弃);0 保留启动一次性语义。
  void RequestMetadataAsync(std::uint64_t generation);
  void HandleCountReady(LPARAM l_param);
  void HandlePageReady(LPARAM l_param);
  void HandleClipReady(LPARAM l_param);
  void HandleStartupMetadataReady(LPARAM l_param);

 private:
  MainWindow& owner_;
};

}  // namespace clipd2d::ui
