#pragma once

// HistoryViewState: MainWindow 的数据侧状态（纯数据 struct + 派生 getter，无副作用方法）。
// 禁止依赖 D2D 设备/HWND——只存放剪贴板历史的缓存、选择、代次等状态值。

#include <atomic>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <d2d1.h>
#include <wrl/client.h>

#include "common/GenerationToken.h"
#include "core/actions/ActionDescriptor.h"
#include "core/browser/BrowserState.h"
#include "core/commands/RefreshRequest.h"
#include "core/store/ClipTypes.h"

namespace clipd2d::ui {

struct BitmapEntry {
  std::uint64_t generation = 0;
  UINT width = 0;
  UINT height = 0;
  std::uint64_t last_used = 0;
  Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmap;
};

struct StagingSnapshot {
  std::uint64_t generation = 0;
  core::store::ClipQuery query;
  core::commands::RefreshRequest request{};
  bool active = false;
  bool count_ready = false;
  int item_count = 0;
  int target_scroll_offset = 0;
  std::unordered_map<int, std::vector<core::store::ClipSummary>> page_cache;
  std::unordered_set<int> required_pages;
};

struct HistoryViewState {
  // 浏览态（浏览状态机 + 当前视图 + 过滤器缓存）
  core::browser::BrowserState browser_state_;
  core::store::ClipQuery display_query_;
  std::vector<std::wstring> display_query_tokens_;
  core::store::ClipView current_view_ = core::store::ClipView::All;
  std::wstring current_tag_name_;
  int queue_count_ = 0;
  std::wstring status_text_ = L"准备就绪";
  std::optional<core::commands::RefreshRequest> pending_refresh_request_;
  std::vector<core::store::GroupRecord> groups_;
  std::vector<core::store::TagRecord> tags_;
  std::vector<core::actions::ActionDescriptor> actions_;

  // 派生读取（副本字段的替代）：从 browser_state_ 直接推导
  std::int64_t current_group_id() const { return browser_state_.group_id.value_or(0); }
  const std::wstring& current_group_name() const { return browser_state_.group_name; }
  std::int64_t current_tag_id() const {
    return browser_state_.filters.tag_ids.empty() ? 0 : browser_state_.filters.tag_ids.front();
  }
  core::store::DateRangeFilter current_date_range() const { return browser_state_.filters.date_range; }

  std::unordered_map<std::wstring, std::uint64_t> pending_thumbnail_generations_;
  std::unordered_map<std::wstring, BitmapEntry> thumbnail_bitmaps_;
  BitmapEntry preview_bitmap_;
  std::unordered_map<int, std::uint64_t> page_access_ticks_;
  std::uint64_t page_access_tick_ = 0;
  std::uint64_t bitmap_access_tick_ = 0;
  std::unordered_set<int> dirty_rows_;
  std::unordered_map<int, std::vector<core::store::ClipSummary>> page_cache_;
  std::unordered_set<std::uint64_t> pending_page_request_keys_;
  StagingSnapshot staging_snapshot_;
  std::optional<std::int64_t> pending_selected_clip_id_;
  std::optional<std::int64_t> explicit_selected_clip_id_;
  std::optional<core::store::ClipRecord> selected_clip_;
  // 代次签发器（单调递增，经 common::GenerationToken 统一抽象）。
  common::GenerationToken refresh_generation_;
  common::GenerationToken clip_generation_;
  common::GenerationToken preview_generation_;
  common::GenerationToken thumbnail_request_epoch_;
  // 水位型代次（写入外部签发值，非自签发计数器，不适用 GenerationToken，见批次 B 报告）：
  // display_generation_：已提交到显示的 refresh 代次；
  // active_thumbnail_request_epoch_：已发布的缩略图请求 epoch（工作线程跨线程读）；
  // bitmap_generation_：d2d 设备代次镜像（= target_generation()，可复位为 0）。
  std::atomic<std::uint64_t> display_generation_{0};
  std::atomic<std::uint64_t> active_thumbnail_request_epoch_{0};
  std::uint64_t bitmap_generation_ = 0;
  int item_count_ = 0;
  int page_size_ = 128;
  int selected_index_ = -1;
};

}  // namespace clipd2d::ui
