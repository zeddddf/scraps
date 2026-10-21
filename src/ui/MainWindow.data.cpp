#include "ui/MainWindowInternal.h"
#include "app/StartupTrace.h"
#include "ui/ClipVisualFileContent.h"
#include "ui/vlist/RowHeightModel.h"
#include "ui/vlist/RowHeightModel.h"

#include <psapi.h>

namespace clipd2d::ui {

void MainWindow::EnsureBitmapCachesValid() {
  history_asset_loader_.EnsureBitmapCachesValid();
  return;
}



void MainWindow::TouchPage(int page_index) {
  page_access_ticks_[page_index] = ++page_access_tick_;
}

void MainWindow::TrimPageCache() {
  if (page_cache_.size() <= static_cast<std::size_t>(kPageCacheLimit)) {
    return;
  }

  RECT client{};
  GetClientRect(hwnd_, &client);
  const PanelLayout layout = ComputeLayout(client.right, client.bottom);
  const int item_height = GetItemHeight();
  const int viewport_height = std::max(1, static_cast<int>(layout.list.bottom - layout.list.top));
  int first_index = 0;
  int last_index = -1;
  ComputeRequestRange(
      item_count_,
      item_height,
      viewport_height,
      scroll_controller_->CurrentScrollOffset(),
      scroll_controller_->GetTargetScrollOffset(),
      scroll_controller_->GetScrollVelocity(),
      &first_index,
      &last_index,
      &viewport_.heights());

  std::unordered_set<int> protected_pages;
  if (last_index >= first_index) {
    const int first_page = first_index / page_size_;
    const int last_page = last_index / page_size_;
    for (int page = first_page; page <= last_page; ++page) {
      protected_pages.insert(page);
    }
  }
  if (selected_index_ >= 0) {
    protected_pages.insert(selected_index_ / page_size_);
  }

  // Evict by distance from the viewport first (scroll locality), falling back
  // to LRU age. Same cache budget — pages near where the user is browsing
  // survive, so scroll-backs and short drags stop re-triggering loads.
  const int center_row = (first_index + std::max(first_index, last_index)) / 2;
  const int center_page = center_row / page_size_;
  while (page_cache_.size() > static_cast<std::size_t>(kPageCacheLimit)) {
    auto candidate = page_cache_.end();
    int farthest_distance = -1;
    std::uint64_t oldest_tick = UINT64_MAX;
    for (auto it = page_cache_.begin(); it != page_cache_.end(); ++it) {
      if (protected_pages.contains(it->first)) {
        continue;
      }
      const auto access_it = page_access_ticks_.find(it->first);
      const std::uint64_t tick = access_it != page_access_ticks_.end() ? access_it->second : 0;
      const int distance = std::abs(it->first - center_page);
      if (candidate == page_cache_.end() || distance > farthest_distance ||
          (distance == farthest_distance && tick < oldest_tick)) {
        candidate = it;
        farthest_distance = distance;
        oldest_tick = tick;
      }
    }
    if (candidate == page_cache_.end()) {
      break;
    }
    page_access_ticks_.erase(candidate->first);
    page_cache_.erase(candidate);
  }
}

void MainWindow::TouchBitmap(BitmapEntry* entry) {
  if (entry == nullptr) {
    return;
  }
  entry->last_used = ++bitmap_access_tick_;
}

void MainWindow::TrimBitmapCaches() {
  const auto protected_thumbnail_keys = BuildProtectedHiddenThumbnailKeys();
  while (thumbnail_bitmaps_.size() > kThumbnailCacheLimit) {
    auto candidate = thumbnail_bitmaps_.end();
    std::uint64_t oldest = UINT64_MAX;
    for (auto it = thumbnail_bitmaps_.begin(); it != thumbnail_bitmaps_.end(); ++it) {
      if (protected_thumbnail_keys.contains(it->first)) {
        continue;
      }
      if (it->first.starts_with(L"source|") || it->first.starts_with(L"file|")) {
        continue;
      }
      if (candidate == thumbnail_bitmaps_.end() || it->second.last_used < oldest) {
        candidate = it;
        oldest = it->second.last_used;
      }
    }
    if (candidate == thumbnail_bitmaps_.end()) {
      break;
    }
    thumbnail_bitmaps_.erase(candidate);
  }

  text_layout_cache_.Trim(kTextLayoutCacheLimit);
}

void MainWindow::ScheduleDeferredHiddenCacheTrim() {
  if (hwnd_ == nullptr) {
    return;
  }
  SetTimer(hwnd_, kHiddenCacheTrimTimerId, kHiddenCacheTrimDelayMs, nullptr);
}

void MainWindow::CancelDeferredHiddenCacheTrim() {
  if (hwnd_ == nullptr) {
    return;
  }
  KillTimer(hwnd_, kHiddenCacheTrimTimerId);
}

void MainWindow::ScheduleDeferredHiddenWorkingSetTrim() {
  if (hwnd_ == nullptr) {
    return;
  }
  SetTimer(hwnd_, kHiddenWorkingSetTrimTimerId, kHiddenWorkingSetTrimDelayMs, nullptr);
}

void MainWindow::CancelDeferredHiddenWorkingSetTrim() {
  if (hwnd_ == nullptr) {
    return;
  }
  KillTimer(hwnd_, kHiddenWorkingSetTrimTimerId);
}

void MainWindow::ScheduleDeferredHiddenWarmup() {
  if (hwnd_ == nullptr) {
    return;
  }
  SetTimer(hwnd_, kHiddenWarmupTimerId, kHiddenWarmupDelayMs, nullptr);
}

void MainWindow::CancelDeferredHiddenWarmup() {
  if (hwnd_ == nullptr) {
    return;
  }
  KillTimer(hwnd_, kHiddenWarmupTimerId);
}

std::unordered_set<int> MainWindow::BuildProtectedHiddenCachePages() const {
  std::unordered_set<int> protected_pages;
  const int keep_latest_count = std::min(item_count_, 10);
  for (int index = 0; index < keep_latest_count; ++index) {
    protected_pages.insert(index / page_size_);
  }
  if (selected_index_ >= 0) {
    protected_pages.insert(selected_index_ / page_size_);
  }
  const PanelLayout layout = ComputeLayout(window_width_, window_height_);
  const int item_height = GetItemHeight();
  const int viewport_height = std::max(1, static_cast<int>(layout.list.bottom - layout.list.top));
  int first_index = 0;
  int last_index = -1;
  ComputeRequestRange(
      item_count_,
      item_height,
      viewport_height,
      scroll_controller_->CurrentScrollOffset(),
      scroll_controller_->GetTargetScrollOffset(),
      scroll_controller_->GetScrollVelocity(),
      &first_index,
      &last_index,
      &viewport_.heights());
  if (last_index >= first_index) {
    const int first_page = first_index / page_size_;
    const int last_page = last_index / page_size_;
    for (int page = first_page; page <= last_page; ++page) {
      protected_pages.insert(page);
    }
  }
  return protected_pages;
}

std::unordered_set<std::wstring> MainWindow::BuildProtectedHiddenThumbnailKeys() const {
  std::unordered_set<std::wstring> protected_keys;
  const auto protected_pages = BuildProtectedHiddenCachePages();
  for (const auto& [page_index, clips] : page_cache_) {
    if (!protected_pages.contains(page_index)) {
      continue;
    }
    for (size_t offset = 0; offset < clips.size(); ++offset) {
      const int absolute_index = page_index * page_size_ + static_cast<int>(offset);
      const bool keep_for_latest = absolute_index < 10;
      const bool keep_for_page = protected_pages.contains(page_index);
      if (!keep_for_latest && !keep_for_page) {
        continue;
      }
      const std::wstring key = ThumbnailKeyFromSummary(clips[offset]);
      if (!key.empty()) {
        protected_keys.insert(key);
      }
    }
  }
  return protected_keys;
}

void MainWindow::RunDeferredHiddenCacheTrim() {
  CancelDeferredHiddenCacheTrim();
  if (hwnd_ == nullptr || IsWindowVisible(hwnd_)) {
    return;
  }

  const std::size_t page_cache_before = page_cache_.size();
  const std::size_t thumbnail_cache_before = thumbnail_bitmaps_.size();
const auto protected_pages = BuildProtectedHiddenCachePages();
  for (auto it = page_cache_.begin(); it != page_cache_.end();) {
    if (protected_pages.contains(it->first)) {
      ++it;
      continue;
    }
    page_access_ticks_.erase(it->first);
    it = page_cache_.erase(it);
  }
  for (auto it = page_access_ticks_.begin(); it != page_access_ticks_.end();) {
    if (page_cache_.contains(it->first)) {
      ++it;
    } else {
      it = page_access_ticks_.erase(it);
    }
  }

  const auto protected_thumbnail_keys = BuildProtectedHiddenThumbnailKeys();
  for (auto it = thumbnail_bitmaps_.begin(); it != thumbnail_bitmaps_.end();) {
    if (protected_thumbnail_keys.contains(it->first)) {
      ++it;
    } else {
      it = thumbnail_bitmaps_.erase(it);
    }
  }

  constexpr std::size_t kHiddenTextLayoutCacheLimit = 128;
  constexpr std::size_t kHiddenShellIconPixelCacheLimit = 64;
  text_layout_cache_.Trim(kHiddenTextLayoutCacheLimit);
  visual_assets::TrimShellIconPixelCache(kHiddenShellIconPixelCacheLimit);

  common::WriteLog(
      common::LogLevel::Info,
      "ui.hidden_cache_trim",
      "page_cache " + std::to_string(page_cache_before) + "->" + std::to_string(page_cache_.size()) +
          " thumbnail_cache " + std::to_string(thumbnail_cache_before) + "->" + std::to_string(thumbnail_bitmaps_.size()) +
          " kept_latest=10 selected_page=" + std::to_string(selected_index_ >= 0 ? selected_index_ / page_size_ : -1) +
          " visible_page=" + std::to_string(scroll_controller_->CurrentScrollOffset() / std::max(1, GetItemHeight()) / page_size_));
}

void MainWindow::RunDeferredHiddenWorkingSetTrim() {
  CancelDeferredHiddenWorkingSetTrim();
  if (hwnd_ == nullptr || IsWindowVisible(hwnd_)) {
    return;
  }
  PROCESS_MEMORY_COUNTERS_EX counters{};
  counters.cb = sizeof(counters);
  if (!GetProcessMemoryInfo(
          GetCurrentProcess(),
          reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
          sizeof(counters))) {
    common::WriteLog(
        common::LogLevel::Warning,
        "ui.hidden_working_set_trim",
        "result=skip get_process_memory_info_failed");
    return;
  }
  if (counters.WorkingSetSize <= kHiddenWorkingSetTrimThresholdBytes) {
    common::WriteLog(
        common::LogLevel::Info,
        "ui.hidden_working_set_trim",
        "result=skip threshold_bytes=" + std::to_string(kHiddenWorkingSetTrimThresholdBytes) +
            " working_set=" + std::to_string(static_cast<std::uint64_t>(counters.WorkingSetSize)));
    return;
  }
  const BOOL trimmed = SetProcessWorkingSetSize(
      GetCurrentProcess(),
      static_cast<SIZE_T>(-1),
      static_cast<SIZE_T>(-1));
  common::WriteLog(
      trimmed ? common::LogLevel::Info : common::LogLevel::Warning,
      "ui.hidden_working_set_trim",
      std::string("result=") + (trimmed ? "success" : "failed") +
          " threshold_bytes=" + std::to_string(kHiddenWorkingSetTrimThresholdBytes) +
          " working_set_before=" + std::to_string(static_cast<std::uint64_t>(counters.WorkingSetSize)));
  if (trimmed) {
    ScheduleDeferredHiddenWarmup();
  }
}

void MainWindow::RunDeferredHiddenWarmup() {
  CancelDeferredHiddenWarmup();
  if (hwnd_ == nullptr || IsWindowVisible(hwnd_)) {
    return;
  }
  if (display_generation_ == 0 || item_count_ <= 0) {
    return;
  }

  PrepareVisibleContentForPresentation();
  common::WriteLog(
      common::LogLevel::Info,
      "ui.hidden_warmup",
      "result=success page_count=" + std::to_string(page_cache_.size()) +
          " thumbnail_cache=" + std::to_string(thumbnail_bitmaps_.size()));
}

void MainWindow::MarkAllVisibleRowsDirty() {
  RECT client{};
  GetClientRect(hwnd_, &client);
  const PanelLayout layout = ComputeLayout(client.right, client.bottom);
  const float visual_scroll_offset = scroll_controller_->VisualScrollOffset();
  const int viewport_height = std::max(1L, layout.list.bottom - layout.list.top);
  const int overscan = GetItemHeight() * 2;
  const vlist::VisibleRange range =
      viewport_.RangeAt(visual_scroll_offset, viewport_height, overscan, overscan);
  MarkRangeDirty(range.first, range.last);
}

// ---- Variable-height virtual list (vlist::VirtualListSource) ----

int MainWindow::ItemCount() const {
  return item_count_;
}

std::int64_t MainWindow::StableIdAt(int index) const {
  if (index < 0 || index >= item_count_) {
    return 0;
  }
  const int page = index / page_size_;
  const int offset = index % page_size_;
  const auto it = page_cache_.find(page);
  if (it == page_cache_.end() || offset >= static_cast<int>(it->second.size())) {
    return 0;
  }
  return it->second[static_cast<size_t>(offset)].id;
}

int MainWindow::MeasureHeightAt(int index) const {
  if (index < 0 || index >= item_count_) {
    return 0;
  }
  const int page = index / page_size_;
  const int offset = index % page_size_;
  const auto it = page_cache_.find(page);
  if (it == page_cache_.end() || offset >= static_cast<int>(it->second.size())) {
    return 0;  // Unloaded rows keep the default estimate (O(1), no I/O).
  }
  const bool compact =
      controller_ != nullptr &&
      controller_->current_settings().list_density == core::store::ListDensity::Compact;
  return vlist::RowHeightModel::Measure(
      it->second[static_cast<size_t>(offset)], viewport_content_width_, compact);
}

void MainWindow::RebuildViewportHeights() {
  RECT client{};
  if (hwnd_ != nullptr && GetClientRect(hwnd_, &client)) {
    const PanelLayout layout = ComputeLayout(client.right, client.bottom);
    viewport_content_width_ = std::max(
        80, static_cast<int>(layout.list.right - layout.list.left) - 2 * kRowPaddingX);
  }
  viewport_.set_default_height(GetItemHeight());
  viewport_.SetSource(this);
  viewport_.Rebuild();
}

void MainWindow::SyncViewportHeightsForPage(int page_index) {
  const auto it = page_cache_.find(page_index);
  if (it == page_cache_.end() || it->second.empty()) {
    return;
  }
  const int page_begin = page_index * page_size_;
  const int page_end = page_begin + static_cast<int>(it->second.size()) - 1;
  const int anchor = viewport_.IndexAtOffset(scroll_controller_->CurrentScrollOffset());
  const int delta = viewport_.UpdateRange(page_begin, page_end, anchor);
  ApplyViewportScrollAnchorDelta(delta);
  MarkAllVisibleRowsDirty();
}

void MainWindow::ApplyViewportScrollAnchorDelta(int delta) {
  if (delta == 0 || scroll_controller_ == nullptr) {
    return;
  }
  // Keep the anchor row visually stable when heights above it change.
  scroll_controller_->SetTargetScrollOffset(scroll_controller_->GetTargetScrollOffset() + delta);
  scroll_controller_->SetAnimatedScrollOffset(scroll_controller_->GetAnimatedScrollOffset() + delta);
}

void MainWindow::MarkRowDirty(int index) {
  if (index < 0 || index >= item_count_) {
    return;
  }
  dirty_rows_.insert(index);
  // L5:行脏即帧脏——经帧泵渲染;行脏集合在 RenderFrame 呈现后清空。
  MarkFrameDirty();
}

void MainWindow::MarkRangeDirty(int start_index, int end_index) {
  if (end_index < start_index) {
    return;
  }
  for (int index = start_index; index <= end_index; ++index) {
    MarkRowDirty(index);
  }
}

void MainWindow::MarkDirtyRowsForImageKey(const std::wstring& key) {
  if (key.empty()) {
    return;
  }

  RECT client{};
  GetClientRect(hwnd_, &client);
  const PanelLayout layout = ComputeLayout(client.right, client.bottom);
  const int item_height = GetItemHeight();
  const float visual_scroll_offset = scroll_controller_->VisualScrollOffset();
  const vlist::VisibleRange visible = viewport_.RangeAt(
      visual_scroll_offset,
      std::max(1, static_cast<int>(layout.list.bottom - layout.list.top)),
      item_height,
      item_height);
  const int first_index = std::max(0, visible.first - 1);
  const int last_index = std::min(item_count_ - 1, visible.last + 1);

  for (int index = first_index; index <= last_index; ++index) {
    const auto* summary = GetSummaryAt(index);
    if (summary == nullptr) {
      continue;
    }
    // 1. Match source app icon cache key
    if (!summary->source_app_path.empty()) {
      if (visual_assets::BuildShellIconCacheKey(BuildSourceIconRequest(*summary)) == key) {
        MarkRowDirty(index);
        continue;
      }
    }
    // 2. Match image/video thumbnail key
    if ((summary->content_type == core::store::ClipContentType::Image ||
         summary->content_type == core::store::ClipContentType::Files) &&
        ThumbnailKeyFromSummary(*summary) == key) {
      MarkRowDirty(index);
      continue;
    }
    // 3. Match file shell icon key
    if (summary->content_type == core::store::ClipContentType::Files) {
      const auto file_req = BuildFileItemIconRequest(*summary);
      if (file_req.has_value() && visual_assets::BuildShellIconCacheKey(*file_req) == key) {
        MarkRowDirty(index);
        continue;
      }
    }
  }
}

int MainWindow::RowIndexAtPoint(POINT point) const {
  RECT client{};
  GetClientRect(hwnd_, &client);
  const PanelLayout layout = ComputeLayout(client.right, client.bottom);
  if (!RectContains(layout.list, point)) {
    return -1;
  }
  const float visual_scroll_offset = scroll_controller_->VisualScrollOffset();
  const int content_offset =
      static_cast<int>(visual_scroll_offset) + (point.y - layout.list.top);
  const int index = viewport_.IndexAtOffset(std::max(0, content_offset));
  if (index < 0 || index >= item_count_) {
    return -1;
  }
  return index;
}

void MainWindow::RefreshGroups() {
  history_data_controller_.RefreshGroups();
}

void MainWindow::RefreshTags() {
  history_data_controller_.RefreshTags();
}

void MainWindow::RefreshData(bool preserve_selection, RefreshDataMode mode) {
  const auto request = preserve_selection
      ? core::commands::MakePreserveViewportRefreshRequest(std::nullopt, core::commands::RefreshSelectionPolicy::PreserveCurrent)
      : core::commands::MakeJumpToTopRefreshRequest(std::nullopt, core::commands::RefreshSelectionPolicy::SelectFirstIfNone);
  RefreshData(request, mode);
}

void MainWindow::RefreshData(const core::commands::RefreshRequest& request, RefreshDataMode mode) {
  history_data_controller_.RefreshData(request, mode);
}

void MainWindow::UpdateScrollBar() {
  RECT client{};
  GetClientRect(hwnd_, &client);
  const PanelLayout layout = ComputeLayout(client.right, client.bottom);
  const int content_height = GetTotalContentHeight();
  const int viewport = std::max(1, static_cast<int>(layout.list.bottom - layout.list.top));
  const int max_scroll = std::max(0, content_height - viewport);
  scroll_controller_->SetTargetScrollOffset( std::clamp(scroll_controller_->GetTargetScrollOffset(), 0, max_scroll));
  scroll_controller_->SetAnimatedScrollOffset( std::clamp(scroll_controller_->GetAnimatedScrollOffset(), 0.0f, static_cast<float>(max_scroll)));
  if (!scroll_controller_->IsListScrollAnimating()) {
    scroll_controller_->SetAnimatedScrollOffset( static_cast<float>(scroll_controller_->GetTargetScrollOffset()));
  }
  
  SCROLLINFO info{};
  info.cbSize = sizeof(info);
  info.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
  info.nMin = 0;
  info.nMax = std::max(0, content_height - 1);
  info.nPage = static_cast<UINT>(viewport);
  info.nPos = scroll_controller_->CurrentScrollOffset();
  SetScrollInfo(hwnd_, SB_VERT, &info, TRUE);
}

void MainWindow::EnsureSelectionVisible() {
  if (selected_index_ < 0) {
    return;
  }
  RECT client{};
  GetClientRect(hwnd_, &client);
  const PanelLayout layout = ComputeLayout(client.right, client.bottom);
  const int item_top = viewport_.OffsetOf(selected_index_);
  const int item_bottom = item_top + viewport_.heights().HeightAt(selected_index_);
  const int viewport_top = scroll_controller_->CurrentScrollOffset();
  const int viewport_bottom = viewport_top + (layout.list.bottom - layout.list.top);
  if (item_top < viewport_top) {
    scroll_controller_->SetScrollOffsetTarget(item_top, false);
  } else if (item_bottom > viewport_bottom) {
    scroll_controller_->SetScrollOffsetTarget(item_bottom - (layout.list.bottom - layout.list.top), false);
  }
}

bool MainWindow::IsColdStartPresentationPending() const {
  return pending_initial_show_;
}

std::unordered_set<int> MainWindow::BuildColdStartPresentationPages(
    int item_count,
    int scroll_target_offset,
    int viewport_height) const {
  std::unordered_set<int> pages;
  if (item_count <= 0 || page_size_ <= 0) {
    return pages;
  }

  const int item_height = GetItemHeight();
  const int max_scroll = std::max(0, item_count * item_height - std::max(1, viewport_height));
  const int clamped_scroll = std::clamp(scroll_target_offset, 0, max_scroll);
  const int current_index = std::clamp(clamped_scroll / std::max(1, item_height), 0, item_count - 1);
  pages.insert(0);
  pages.insert(current_index / page_size_);
  return pages;
}

void MainWindow::RequestVisiblePages(bool include_thumbnails) {
  if (display_generation_ == 0) {
    return;
  }
  RECT client{};
  GetClientRect(hwnd_, &client);
  const PanelLayout layout = ComputeLayout(client.right, client.bottom);
  const int item_height = GetItemHeight();
  const int viewport_height = std::max(1, static_cast<int>(layout.list.bottom - layout.list.top));
  int first_index = 0;
  int last_index = -1;
  ComputeRequestRange(
      item_count_,
      item_height,
      viewport_height,
      scroll_controller_->CurrentScrollOffset(),
      scroll_controller_->GetTargetScrollOffset(),
      scroll_controller_->GetScrollVelocity(),
      &first_index,
      &last_index,
      &viewport_.heights());
  if (last_index < first_index) {
    return;
  }
  std::unordered_set<int> pages;
  if (IsColdStartPresentationPending()) {
    pages = BuildColdStartPresentationPages(item_count_, scroll_controller_->GetTargetScrollOffset(), viewport_height);
  } else {
    const int first_page = first_index / page_size_;
    const int last_page = last_index / page_size_;
    for (int page = first_page; page <= last_page; ++page) {
      pages.insert(page);
    }
  }
  // Pages covering the rows actually in view must bypass the fast-scroll
  // request throttle; only prefetch (overscan) pages are deferred while the
  // user scrolls or drags the thumb.
  const vlist::VisibleRange visible = viewport_.RangeAt(
      scroll_controller_->VisualScrollOffset(), viewport_height, 0, 0);
  const int visible_first_page =
      visible.last >= visible.first ? std::max(0, visible.first) / page_size_ : -1;
  const int visible_last_page =
      visible.last >= visible.first ? std::min(item_count_ - 1, visible.last) / page_size_ : -2;
  for (int page : pages) {
    const bool urgent =
        visible_first_page >= 0 && page >= visible_first_page && page <= visible_last_page;
    RequestPageAsync(page, display_generation_, display_query_, urgent);
  }
  if (include_thumbnails) {
    RequestVisibleThumbnails();
  }
}

void MainWindow::WarmNeighborhoodPages() {
  // Zero-extra-memory warm-up: pull pages nearest the viewport into the fixed
  // page cache budget (kPageCacheLimit pages) ahead of user scrolls, so that
  // opening the panel or scrolling rarely lands on an uncached (skeleton) page.
  if (display_generation_ == 0 || item_count_ <= 0 || page_size_ <= 0) {
    return;
  }
  const int page_count = (item_count_ + page_size_ - 1) / page_size_;
  if (page_count <= 1) {
    return;
  }
  RECT client{};
  GetClientRect(hwnd_, &client);
  const PanelLayout layout = ComputeLayout(client.right, client.bottom);
  const int viewport_height = std::max(1, static_cast<int>(layout.list.bottom - layout.list.top));
  const vlist::VisibleRange visible = viewport_.RangeAt(
      scroll_controller_->VisualScrollOffset(), viewport_height, 0, 0);
  const int center_row =
      visible.last >= visible.first ? std::max(0, (visible.first + visible.last) / 2) : 0;
  const int center_page = center_row / page_size_;

  std::vector<int> order;
  order.reserve(static_cast<size_t>(page_count));
  for (int page = 0; page < page_count; ++page) {
    order.push_back(page);
  }
  std::sort(order.begin(), order.end(), [center_page](int left, int right) {
    return std::abs(left - center_page) < std::abs(right - center_page);
  });
  const int budget = std::min(page_count, kPageCacheLimit);
  for (int i = 0; i < budget; ++i) {
    history_data_controller_.RequestPageAsync(order[static_cast<size_t>(i)], display_generation_, display_query_, false);
  }
}

void MainWindow::RequestVisibleThumbnails() {
  if (display_generation_ == 0 || item_count_ <= 0) {
    return;
  }

  RECT client{};
  GetClientRect(hwnd_, &client);
  const PanelLayout layout = ComputeLayout(client.right, client.bottom);
  const int item_height = GetItemHeight();
  const int viewport_height = std::max(1, static_cast<int>(layout.list.bottom - layout.list.top));
  int first_index = 0;
  int last_index = -1;
  ComputeRequestRange(
      item_count_,
      item_height,
      viewport_height,
      scroll_controller_->CurrentScrollOffset(),
      scroll_controller_->GetTargetScrollOffset(),
      scroll_controller_->GetScrollVelocity(),
      &first_index,
      &last_index,
      &viewport_.heights());
  if (last_index < first_index) {
    return;
  }

  if (first_index != last_thumbnail_request_first_index_ ||
      last_index != last_thumbnail_request_last_index_) {
    last_thumbnail_request_first_index_ = first_index;
    last_thumbnail_request_last_index_ = last_index;
    active_thumbnail_request_epoch_.store(++thumbnail_request_epoch_, std::memory_order_relaxed);
  }

  for (int index = first_index; index <= last_index; ++index) {
    const auto* summary = GetSummaryAt(index);
    if (summary == nullptr) {
      continue;
    }
    RequestThumbnailAsync(*summary);
  }
}

void MainWindow::PrepareVisibleContentForPresentation() {
  if (display_generation_ == 0) {
    return;
  }
  RequestVisiblePages();
  WarmNeighborhoodPages();
  dirty_rows_.clear();
}

void MainWindow::SelectFirstItemIfNeeded() {
  if (selected_index_ >= 0 || item_count_ <= 0) {
    return;
  }
  SelectIndex(0, false);
}

void MainWindow::CommitStagingSnapshotIfReady() {
  if (!staging_snapshot_.active || !staging_snapshot_.count_ready) {
    return;
  }
  for (int page : staging_snapshot_.required_pages) {
    if (!staging_snapshot_.page_cache.contains(page)) {
      return;
    }
  }

  const core::commands::RefreshRequest request = staging_snapshot_.request;
  const int base_scroll_target = item_count_ > 0 ? staging_snapshot_.target_scroll_offset : 0;

  item_count_ = staging_snapshot_.item_count;
  page_cache_ = std::move(staging_snapshot_.page_cache);
  page_access_ticks_.clear();
  for (const auto& [page_index, _] : page_cache_) {
    TouchPage(page_index);
  }
  RebuildViewportHeights();
  RebuildViewportHeights();
  display_query_ = staging_snapshot_.query;
  display_query_tokens_ = TokenizeSearchQuery(ToWide(display_query_.search_text));
  display_generation_ = staging_snapshot_.generation;

  int resolved_selected_index = selected_index_;
  bool matched_pending_selection = false;
  if (pending_selected_clip_id_.has_value()) {
    for (const auto& [page_index, clips] : page_cache_) {
      (void)page_index;
      for (size_t offset = 0; offset < clips.size(); ++offset) {
        if (clips[offset].id == *pending_selected_clip_id_) {
          resolved_selected_index = page_index * page_size_ + static_cast<int>(offset);
          matched_pending_selection = true;
          break;
        }
      }
      if (matched_pending_selection) {
        break;
      }
    }
  }
  if (!matched_pending_selection) {
    switch (request.selection_policy) {
      case core::commands::RefreshSelectionPolicy::SelectTargetClip:
        resolved_selected_index = -1;
        break;
      case core::commands::RefreshSelectionPolicy::SelectFirstIfNone:
        if (resolved_selected_index >= item_count_) {
          resolved_selected_index = -1;
        }
        if (resolved_selected_index < 0 && item_count_ > 0) {
          resolved_selected_index = 0;
        }
        break;
      case core::commands::RefreshSelectionPolicy::PreserveCurrent:
      default:
        if (resolved_selected_index >= item_count_) {
          resolved_selected_index = -1;
        }
        break;
    }
  }

  selected_index_ = resolved_selected_index;
  if (selected_index_ < 0) {
    selected_clip_.reset();
    actions_.clear();
    preview_bitmap_.bitmap.Reset();
  }

  staging_snapshot_ = StagingSnapshot{};
  explicit_selected_clip_id_.reset();
  pending_selected_clip_id_.reset();
  pending_refresh_request_.reset();

  int final_scroll_target = base_scroll_target;
  if (item_count_ > 0 &&
      selected_index_ >= 0 &&
      core::commands::ShouldRevealSelectionAfterRefresh(request) &&
      hwnd_ != nullptr) {
    RECT client{};
    GetClientRect(hwnd_, &client);
    const PanelLayout layout = ComputeLayout(client.right, client.bottom);
    const int item_top = viewport_.OffsetOf(selected_index_);
    const int item_bottom = item_top + viewport_.heights().HeightAt(selected_index_);
    const int viewport_height = std::max(1, static_cast<int>(layout.list.bottom - layout.list.top));
    final_scroll_target = core::commands::ResolveRevealSelectionScrollTarget(
        base_scroll_target,
        item_top,
        item_bottom,
        viewport_height);
  }

  scroll_controller_->SetScrollOffsetTarget(item_count_ > 0 ? final_scroll_target : 0, true);
  if (selected_index_ >= 0) {
    LoadSelection();
  }
  UpdateScrollBar();
  PrepareVisibleContentForPresentation();
  const bool should_present_after_prepare = pending_initial_show_ || pending_show_after_refresh_;
  if (!should_present_after_prepare) {
    ScheduleListPresent();
  }
  if (should_present_after_prepare) {
    pending_show_after_refresh_ = false;
    pending_initial_show_ = false;
    const core::store::AppSettings settings =
        controller_ != nullptr ? controller_->current_settings() : core::store::AppSettings{};
    PrepareWindowPlacementForShow();
    PresentPreparedWindow(settings.focus_search_on_open);
  }
  clipd2d::app::MarkStartupPhase("initial_panel_presented");
}

void MainWindow::ScheduleListPresent() {
  if (hwnd_ == nullptr || list_present_scheduled_) {
    return;
  }
  list_present_scheduled_ = true;
  PostMessageW(hwnd_, kPresentListMessage, 0, 0);
}

void MainWindow::MarkFrameDirty() {
  // L5:置脏 + 合并调度帧泵。frame_render_scheduled_ 保证一次脏状态只投递一个
  // 渲染消息(不重复帧);frame_dirty_ 保证每个被 WM_PAINT 记录的脏状态最终都会
  // 被渲染(不漏帧)。WM_PAINT 只负责调用本函数,绘制在 kFrameRenderMessage 里做。
  frame_dirty_ = true;
  if (hwnd_ == nullptr || frame_render_scheduled_) {
    return;
  }
  frame_render_scheduled_ = true;
  PostMessageW(hwnd_, kFrameRenderMessage, 0, 0);
}

bool MainWindow::has_pending_work() const {
  // L6 硬规则 3:统一"是否需要下一帧"清单。新增脏类型必须同步补到这里,
  // 漏一项会导致"数据到了但列表不刷新"(历史实发事故)。
  // 1) 内容脏行 / 整帧脏 / 列表呈现待处理。
  if (frame_dirty_ || !dirty_rows_.empty() || list_present_scheduled_) {
    return true;
  }
  // 2) 滚动物理 / 拖拽衰减:滚动动画、惯性滑行、行拖拽自动滚动与位移补间。
  if (scroll_controller_ != nullptr &&
      (scroll_controller_->IsListScrollAnimating() ||
       scroll_controller_->IsInertialScrollActive() ||
       std::fabs(scroll_controller_->GetScrollVelocity()) > 0.0f)) {
    return true;
  }
  if (row_reorder_animation_active_ ||
      row_drag_gap_step_.has_value() ||
      !row_drag_visual_rows_.empty() ||
      (row_drag_active_ &&
       (std::fabs(row_drag_auto_scroll_velocity_) > 1.0f ||
        RowDragSessionHasActiveMotion()))) {
    return true;
  }
  // 3) 动画 / 骨架 shimmer 在途(时间驱动的 shimmer 每拍都需重绘)。
  if (skeleton_animating_) {
    return true;
  }
  // 4) 预览 / 缩略图加载中(结果经 PostMessage 回 UI 线程后置脏)。
  if (preview_load_pending_ || !pending_thumbnail_generations_.empty()) {
    return true;
  }
  // 5) 延迟页请求挂起(kDeferredPageRequestTimerId 触发前)。
  if (deferred_page_request_pending_) {
    return true;
  }
  // 6) 拖拽滚动条的滚动模糊残影衰减。
  if (drag_scrollbar_ && last_rendered_blur_strength_ > 0.0f) {
    return true;
  }
  return false;
}

void MainWindow::InvalidateListRegion() {
  if (hwnd_ == nullptr) {
    return;
  }
  // L5/L6:列表脏区直接进帧泵(置脏 + 合并调度),不再依赖 WM_PAINT 的派发时序
  // ——WM_PAINT 按消息队列空闲才派发,输入洪峰下会饿死数据更新(即历史上的
  // "数据到了但列表不刷新"事故形态)。绘制统一在 kFrameRenderMessage 里做。
  MarkFrameDirty();
}

void MainWindow::RequestCountAsync(core::store::ClipQuery query, std::uint64_t generation) {
  history_data_controller_.RequestCountAsync(std::move(query), generation);
}

void MainWindow::RequestPageAsync(int page_index) {
  history_data_controller_.RequestPageAsync(page_index);
}

void MainWindow::RequestPageAsync(int page_index, std::uint64_t generation, const core::store::ClipQuery& query, bool urgent) {
  history_data_controller_.RequestPageAsync(page_index, generation, query, urgent);
}

void MainWindow::RequestSelectedClipAsync(std::int64_t clip_id) {
  history_data_controller_.RequestSelectedClipAsync(clip_id);
}

void MainWindow::RequestStartupMetadataAsync() {
  history_data_controller_.RequestStartupMetadataAsync();
}

void MainWindow::RequestSourceIconAsync(const core::store::ClipSummary& summary) {
  history_asset_loader_.RequestSourceIconAsync(summary);
}

void MainWindow::RequestFileItemIconAsync(const core::store::ClipSummary& summary) {
  history_asset_loader_.RequestFileItemIconAsync(summary);
}

void MainWindow::RequestThumbnailAsync(const core::store::ClipSummary& summary) {
  history_asset_loader_.RequestThumbnailAsync(summary);
}

void MainWindow::RequestPreviewAsync(const core::store::ClipRecord& clip) {
  history_asset_loader_.RequestPreviewAsync(clip);
}

visual_assets::ShellIconRequest MainWindow::BuildSourceIconRequest(
    const core::store::ClipSummary& summary) const {
  return visual_assets::BuildSourceAppIconRequest(
      summary.source_app_path,
      kRowHeaderIconSize,
      d2d_shell_.dpi());
}

std::optional<visual_assets::ShellIconRequest> MainWindow::BuildFileItemIconRequest(
    const core::store::ClipSummary& summary) const {
  if (summary.content_type != core::store::ClipContentType::Files) {
    return std::nullopt;
  }

  const std::wstring path = visual_assets::FirstFilePathFromSummary(summary);
  if (path.empty()) {
    return std::nullopt;
  }

  const int content_height = GetItemHeight() - kRowHeaderHeight - 6;
  const int displayed_extent = summary.file_count <= 1
      ? file_content_preview::ResolveSingleFileIconRenderExtent(content_height)
      : file_content_preview::ResolveIconOnlyRenderExtent(content_height);
  std::error_code error;
  const bool is_directory = std::filesystem::is_directory(path, error);
  return visual_assets::BuildFileIconRequest(
      path,
      displayed_extent,
      d2d_shell_.dpi(),
      false,
      is_directory);
}

void MainWindow::HandleCountReady(WPARAM, LPARAM l_param) {
  history_data_controller_.HandleCountReady(l_param);
}

void MainWindow::HandlePageReady(WPARAM, LPARAM l_param) {
  history_data_controller_.HandlePageReady(l_param);
}

void MainWindow::HandleClipReady(WPARAM, LPARAM l_param) {
  history_data_controller_.HandleClipReady(l_param);
}

void MainWindow::HandleStartupMetadataReady(WPARAM, LPARAM l_param) {
  history_data_controller_.HandleStartupMetadataReady(l_param);
}

void MainWindow::HandleIconReady(WPARAM, LPARAM l_param) {
  history_asset_loader_.HandleIconReady(l_param);
}

void MainWindow::HandlePreviewReady(WPARAM, LPARAM l_param) {
  history_asset_loader_.HandlePreviewReady(l_param);
}

void MainWindow::HandleThumbnailReady(WPARAM, LPARAM l_param) {
  history_asset_loader_.HandleThumbnailReady(l_param);
}

core::store::ClipQuery MainWindow::BuildCurrentQuery() const {
  return browser_service_.BuildQuery(BuildBrowserState());
}

core::browser::BrowserState MainWindow::BuildBrowserState() const {
  core::browser::BrowserState state = browser_state_;
  switch (current_view_) {
    case core::store::ClipView::Favorites:
      state.scope = core::browser::BrowserScope::Favorites;
      break;
    case core::store::ClipView::Group:
      state.scope = core::browser::BrowserScope::Library;
      state.group_id = current_group_id_;
      state.group_name = current_group_name_;
      break;
    case core::store::ClipView::Queue:
      state.scope = core::browser::BrowserScope::Queue;
      break;
    default:
      state.scope = core::browser::BrowserScope::History;
      break;
  }

  state.search_text = search_textbox_.GetText();
  state.filters.date_range = current_date_range_;
  switch (current_view_) {
    case core::store::ClipView::Text:
      state.filters.type = core::store::ClipContentType::Text;
      break;
    case core::store::ClipView::Html:
      state.filters.type = core::store::ClipContentType::Html;
      break;
    case core::store::ClipView::Image:
      state.filters.type = core::store::ClipContentType::Image;
      break;
    case core::store::ClipView::Files:
      state.filters.type = core::store::ClipContentType::Files;
      break;
    default:
      break;
  }
  if (current_view_ == core::store::ClipView::Frequent) {
    state.filters.frequent_only = true;
  }
  if (current_view_ == core::store::ClipView::Pinned) {
    state.filters.pinned_only = true;
  }
  if (current_view_ == core::store::ClipView::Group) {
    state.group_id = current_group_id_;
    state.group_name = current_group_name_;
  }
  if (current_tag_id_ > 0) {
    state.filters.tag_ids.push_back(current_tag_id_);
  }
  return state;
}

void MainWindow::ApplyBrowserCommand(const core::browser::BrowserCommand& command) {
  browser_state_ = browser_service_.ApplyCommand(BuildBrowserState(), command);
  ShowFilterChipBarFlag() = !browser_service_.BuildPresentation(browser_state_).chips.empty();

  switch (browser_state_.scope) {
    case core::browser::BrowserScope::History:
      if (browser_state_.filters.type.has_value()) {
        switch (*browser_state_.filters.type) {
          case core::store::ClipContentType::Text: current_view_ = core::store::ClipView::Text; break;
          case core::store::ClipContentType::Html: current_view_ = core::store::ClipView::Html; break;
          case core::store::ClipContentType::Image: current_view_ = core::store::ClipView::Image; break;
          case core::store::ClipContentType::Files: current_view_ = core::store::ClipView::Files; break;
          default: current_view_ = core::store::ClipView::All; break;
        }
      } else if (browser_state_.filters.frequent_only) {
        current_view_ = core::store::ClipView::Frequent;
      } else if (browser_state_.filters.pinned_only) {
        current_view_ = core::store::ClipView::Pinned;
      } else {
        current_view_ = core::store::ClipView::All;
      }
      break;
    case core::browser::BrowserScope::Favorites:
      current_view_ = core::store::ClipView::Favorites;
      break;
    case core::browser::BrowserScope::Library:
      current_view_ = core::store::ClipView::Group;
      break;
    case core::browser::BrowserScope::Queue:
      current_view_ = core::store::ClipView::Queue;
      break;
  }

  current_group_id_ = browser_state_.group_id.value_or(0);
  current_group_name_ = browser_state_.group_name;
  current_date_range_ = browser_state_.filters.date_range;
  current_tag_id_ = browser_state_.filters.tag_ids.empty() ? 0 : browser_state_.filters.tag_ids.front();
  current_tag_name_.clear();
  if (current_tag_id_ > 0) {
    const auto it = std::find_if(tags_.begin(), tags_.end(), [this](const core::store::TagRecord& tag) {
      return tag.id == current_tag_id_;
    });
    if (it != tags_.end()) {
      current_tag_name_ = it->name;
    }
  }
}

void MainWindow::MarkDirtyRowsForShellIcon(
    visual_assets::ShellIconPurpose purpose,
    const std::wstring& source_path) {
  if (source_path.empty()) {
    return;
  }

  RECT client{};
  GetClientRect(hwnd_, &client);
  const PanelLayout layout = ComputeLayout(client.right, client.bottom);
  const int item_height = GetItemHeight();
  const float visual_scroll_offset = scroll_controller_->VisualScrollOffset();
  const vlist::VisibleRange visible = viewport_.RangeAt(
      visual_scroll_offset,
      std::max(1, static_cast<int>(layout.list.bottom - layout.list.top)),
      item_height,
      item_height);
  const int first_index = std::max(0, visible.first - 1);
  const int last_index = std::min(item_count_ - 1, visible.last + 1);

  for (int index = first_index; index <= last_index; ++index) {
    const auto* summary = GetSummaryAt(index);
    if (summary == nullptr) {
      continue;
    }
    if (purpose == visual_assets::ShellIconPurpose::SourceAppBadge) {
      if (summary->source_app_path == source_path) {
        MarkRowDirty(index);
      }
      continue;
    }
    if (summary->content_type == core::store::ClipContentType::Files &&
        visual_assets::FirstFilePathFromSummary(*summary) == source_path) {
      MarkRowDirty(index);
    }
  }
}


}  // namespace clipd2d::ui
