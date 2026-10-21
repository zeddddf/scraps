#include "ui/HistoryDataController.h"

#include <memory>

#include "app/StartupTrace.h"
#include "ui/MainWindowInternal.h"

namespace clipd2d::ui {

HistoryDataController::HistoryDataController(MainWindow& owner) : owner_(owner) {}

void HistoryDataController::RefreshGroups() {
  if (owner_.controller_ == nullptr) {
    return;
  }
  std::vector<core::store::GroupRecord> groups;
  const common::Status status = owner_.controller_->ListGroups(&groups);
  if (status.ok()) {
    owner_.groups_ = std::move(groups);
  }
}

void HistoryDataController::RefreshTags() {
  if (owner_.controller_ == nullptr) {
    return;
  }
  std::vector<core::store::TagRecord> tags;
  const common::Status status = owner_.controller_->ListTags(&tags);
  if (status.ok()) {
    tags.erase(
        std::remove_if(
            tags.begin(),
            tags.end(),
            [](const core::store::TagRecord& tag) {
              return tag.is_system;
            }),
        tags.end());
    owner_.tags_ = std::move(tags);
  }
}

void HistoryDataController::RefreshData(const core::commands::RefreshRequest& request, RefreshDataMode mode) {
  owner_.RefreshSettingsCache();
  if (mode == RefreshDataMode::Full || mode == RefreshDataMode::FastStartup) {
    RefreshGroups();
    RefreshTags();
  }
  owner_.pending_selected_clip_id_.reset();
  owner_.pending_refresh_request_ = request;

  const bool preserve_existing_selection =
      request.selection_policy != core::commands::RefreshSelectionPolicy::SelectTargetClip;
  if (core::commands::ShouldSelectTargetClip(request)) {
    owner_.pending_selected_clip_id_ = request.target_clip_id;
  } else if (preserve_existing_selection || request.selection_policy == core::commands::RefreshSelectionPolicy::SelectFirstIfNone) {
    if (owner_.explicit_selected_clip_id_.has_value()) {
      owner_.pending_selected_clip_id_ = owner_.explicit_selected_clip_id_;
    } else if (const auto* selected_summary = owner_.GetSelectedSummary(); selected_summary != nullptr) {
      owner_.pending_selected_clip_id_ = selected_summary->id;
    } else if (owner_.selected_clip_.has_value()) {
      owner_.pending_selected_clip_id_ = owner_.selected_clip_->summary.id;
    }
  }
  if (!preserve_existing_selection) {
    owner_.selected_index_ = -1;
    owner_.selected_clip_.reset();
    owner_.actions_.clear();
    owner_.preview_bitmap_.bitmap.Reset();
  }

  const std::uint64_t generation = ++owner_.refresh_generation_;
  owner_.staging_snapshot_ = MainWindow::StagingSnapshot{};
  owner_.staging_snapshot_.generation = generation;
  owner_.staging_snapshot_.query = owner_.BuildCurrentQuery();
  owner_.staging_snapshot_.request = request;
  owner_.staging_snapshot_.active = true;
  owner_.staging_snapshot_.target_scroll_offset =
      core::commands::ResolveInitialRefreshScrollTarget(request, owner_.scroll_controller_->GetTargetScrollOffset());

  if (mode == RefreshDataMode::Full || mode == RefreshDataMode::FastStartup) {
    std::vector<core::store::QueueEntry> queue_entries;
    if (owner_.controller_ != nullptr && owner_.controller_->ListQueue(&queue_entries).ok()) {
      owner_.queue_count_ = static_cast<int>(queue_entries.size());
    } else {
      owner_.queue_count_ = 0;
    }
    if (mode == RefreshDataMode::FastStartup) {
      owner_.startup_metadata_warmup_completed_ = true;
      owner_.startup_metadata_warmup_requested_ = true;
    }
  } else {
    owner_.queue_count_ = 0;
  }

  if (request.scroll_policy == core::commands::RefreshScrollPolicy::JumpToTop) {
    owner_.scroll_controller_->GetWheelDeltaAccumulator() = 0;
    owner_.scroll_controller_->GetManualScrollDirection() = 0;
  }

  owner_.pending_thumbnail_generations_.clear();
  owner_.active_thumbnail_request_epoch_.store(++owner_.thumbnail_request_epoch_, std::memory_order_relaxed);
  owner_.last_thumbnail_request_first_index_ = -1;
  owner_.last_thumbnail_request_last_index_ = -1;
  owner_.dirty_rows_.clear();

  if (mode == RefreshDataMode::FastStartup) {
    if (owner_.controller_ != nullptr) {
      std::int64_t count = 0;
      const common::Status status = owner_.controller_->CountClips(owner_.staging_snapshot_.query, &count);
      if (status.ok()) {
        owner_.staging_snapshot_.count_ready = true;
        owner_.staging_snapshot_.item_count = static_cast<int>(count);
        owner_.staging_snapshot_.required_pages.clear();
        if (owner_.staging_snapshot_.item_count > 0 && owner_.hwnd_ != nullptr) {
          RECT client{};
          GetClientRect(owner_.hwnd_, &client);
          const PanelLayout layout = ComputeLayout(client.right, client.bottom);
          const int item_height = owner_.GetItemHeight();
          const int viewport_height = std::max(1, static_cast<int>(layout.list.bottom - layout.list.top));
          const int clamped_target = std::clamp(
              owner_.staging_snapshot_.target_scroll_offset,
              0,
              std::max(0, owner_.staging_snapshot_.item_count * item_height - viewport_height));

          std::unordered_set<int> pages = owner_.BuildColdStartPresentationPages(
              owner_.staging_snapshot_.item_count,
              clamped_target,
              viewport_height);

          for (int page : pages) {
            if (page >= 0) {
              owner_.staging_snapshot_.required_pages.insert(page);
              const int offset = page * owner_.page_size_;
              const int limit = owner_.page_size_;
              std::vector<core::store::ClipSummary> clips;
              const common::Status list_status = owner_.controller_->ListClips(
                  owner_.staging_snapshot_.query,
                  offset,
                  limit,
                  &clips);
              if (list_status.ok()) {
                owner_.staging_snapshot_.page_cache[page] = std::move(clips);
                for (const auto& summary : owner_.staging_snapshot_.page_cache[page]) {
                  owner_.RequestSourceIconAsync(summary);
                  if (summary.content_type == core::store::ClipContentType::Files) {
                    owner_.RequestFileItemIconAsync(summary);
                  }
                }
              }
            }
          }
        }
      }
    }
    owner_.CommitStagingSnapshotIfReady();
  } else {
    RequestCountAsync(owner_.staging_snapshot_.query, generation);
    owner_.ScheduleListPresent();
  }
}

void HistoryDataController::RequestCountAsync(core::store::ClipQuery query, std::uint64_t generation) {
  if (owner_.controller_ == nullptr || owner_.hwnd_ == nullptr) {
    return;
  }
  auto* controller = owner_.controller_;
  const HWND hwnd = owner_.hwnd_;
  // Q3:工作线程只携带快照(控制器指针、目标窗口、generation 原子指针),
  // 不持 this、不跨线程读窗口状态;结果经 PostMessage 回 UI 线程。
  const auto* refresh_generation = &owner_.refresh_generation_;
  owner_.thread_pool_.Post([controller, hwnd, generation, query = std::move(query), refresh_generation]() mutable {
    if (generation != refresh_generation->load(std::memory_order_relaxed)) return;
    auto payload = std::make_unique<CountReadyPayload>();
    payload->generation = generation;
    payload->status = controller->CountClips(query, &payload->count);
    (void)PostOwnedMessage(hwnd, MainWindow::kCountReadyMessage, 0, std::move(payload));
  });
}

void HistoryDataController::RequestPageAsync(int page_index) {
  if (owner_.controller_ == nullptr || owner_.hwnd_ == nullptr || page_index < 0) {
    return;
  }
  const core::store::ClipQuery query = owner_.BuildCurrentQuery();
  RequestPageAsync(page_index, owner_.display_generation_, query);
}

void HistoryDataController::RequestPageAsync(int page_index, std::uint64_t generation, const core::store::ClipQuery& query, bool urgent) {
  if (owner_.controller_ == nullptr || owner_.hwnd_ == nullptr || page_index < 0 || generation == 0) {
    return;
  }

  if (owner_.staging_snapshot_.active && generation == owner_.staging_snapshot_.generation) {
    if (owner_.staging_snapshot_.page_cache.contains(page_index)) {
      return;
    }
  } else if (generation != owner_.display_generation_ || owner_.page_cache_.contains(page_index)) {
    return;
  }

  if (generation == owner_.display_generation_) {
    const std::uint64_t now = GetTickCount64();
    const float velocity = owner_.scroll_controller_->GetScrollVelocity();
    const bool is_fast_scrolling = owner_.drag_scrollbar_ || std::fabs(velocity) > 800.0f;
    // Throttle only prefetch (non-urgent) requests during fast scroll / thumb
    // drag. Pages covering the rows the user is looking at right now must load
    // immediately — throttling them is what turns every drag into skeleton
    // (降级) frames.
    if (is_fast_scrolling && !urgent) {
      if (now - owner_.last_page_request_time_ms_ < 100) {
        ::SetTimer(owner_.hwnd_, MainWindow::kDeferredPageRequestTimerId, 100, nullptr);
        // L6:延迟页请求挂起进 has_pending_work 清单。
        owner_.deferred_page_request_pending_ = true;
        return;
      }
    }
    ::KillTimer(owner_.hwnd_, MainWindow::kDeferredPageRequestTimerId);
    owner_.deferred_page_request_pending_ = false;
    owner_.last_page_request_time_ms_ = now;
  }

  const std::uint64_t request_key = MakePageRequestKey(generation, page_index);
  if (owner_.pending_page_request_keys_.contains(request_key)) {
    return;
  }
  owner_.pending_page_request_keys_.insert(request_key);
  auto* controller = owner_.controller_;
  const HWND hwnd = owner_.hwnd_;
  const int offset = page_index * owner_.page_size_;
  const int limit = owner_.page_size_;
  // Q3:文本布局预热所需的一切在 UI 线程快照后交给工作线程,任务不再触碰
  // owner_/窗口状态(DWrite 格式指针随快照 AddRef 保活;TextLayoutCache 自带
  // 互斥锁,且 thread_pool_.Shutdown() 先于其析构,生命周期安全)。
  struct PrewarmSnapshot {
    Microsoft::WRL::ComPtr<IDWriteFactory> dwrite_factory;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> small_format;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> meta_format;
    float list_width = 0.0f;
    float time_width = 0.0f;
    float item_height = 0.0f;
    TextLayoutCache* text_cache = nullptr;
  } snapshot;
  snapshot.text_cache = &owner_.text_layout_cache_;
  if (owner_.d2d_shell_.dwrite_factory() != nullptr) {
    snapshot.dwrite_factory = owner_.d2d_shell_.dwrite_factory();
    snapshot.small_format = owner_.d2d_shell_.small_format();
    snapshot.meta_format = owner_.d2d_shell_.meta_format();
    RECT client_rect{};
    ::GetClientRect(hwnd, &client_rect);
    const PanelLayout layout = ComputeLayout(client_rect.right, client_rect.bottom);
    snapshot.list_width = static_cast<float>(layout.list.right - layout.list.left - 2 * kRowPaddingX);
    snapshot.time_width = snapshot.list_width;
    snapshot.item_height = static_cast<float>(owner_.GetItemHeight());
  }
  const auto* display_generation = &owner_.display_generation_;
  const auto* refresh_generation = &owner_.refresh_generation_;
  owner_.thread_pool_.Post([controller, hwnd, generation, page_index, query, offset, limit,
                            snapshot, display_generation, refresh_generation]() {
    const std::uint64_t display_gen = display_generation->load(std::memory_order_relaxed);
    const std::uint64_t refresh_gen = refresh_generation->load(std::memory_order_relaxed);
    if (generation != display_gen && generation != refresh_gen) return;
    auto payload = std::make_unique<PageReadyPayload>();
    payload->generation = generation;
    payload->page_index = page_index;
    payload->status = controller->ListClips(query, offset, limit, &payload->clips);

    if (payload->status.ok() && snapshot.dwrite_factory != nullptr) {
      for (const auto& summary : payload->clips) {
        const std::wstring time_text = FormatMetaLine(summary);
        if (!time_text.empty() && snapshot.meta_format != nullptr) {
          const auto key = MakeTextLayoutKey(summary, CachedTextRole::Meta, snapshot.time_width);
          snapshot.text_cache->GetOrCreate(
              snapshot.dwrite_factory.Get(),
              key,
              time_text,
              snapshot.meta_format.Get(),
              static_cast<float>(WidthBucket(snapshot.time_width)),
              static_cast<float>(kRowHeaderHeight),
              true);
        }

        if (summary.content_type != core::store::ClipContentType::Image &&
            summary.content_type != core::store::ClipContentType::Files) {
          const std::wstring content_text = RowContentText(summary);
          if (!content_text.empty() && snapshot.small_format != nullptr) {
            const auto key = MakeTextLayoutKey(summary, CachedTextRole::Content, snapshot.list_width);
            snapshot.text_cache->GetOrCreate(
                snapshot.dwrite_factory.Get(),
                key,
                content_text,
                snapshot.small_format.Get(),
                static_cast<float>(WidthBucket(snapshot.list_width)),
                snapshot.item_height - static_cast<float>(kRowHeaderHeight) - 8.0f,
                false);
          }
        }
      }
    }

    (void)PostOwnedMessage(hwnd, MainWindow::kPageReadyMessage, 0, std::move(payload));
  });
}

void HistoryDataController::RequestSelectedClipAsync(std::int64_t clip_id) {
  if (owner_.controller_ == nullptr || owner_.hwnd_ == nullptr || clip_id <= 0) {
    return;
  }
  const std::uint64_t generation = ++owner_.clip_generation_;
  auto* controller = owner_.controller_;
  const HWND hwnd = owner_.hwnd_;
  // Q3:只携带快照(原子 generation 指针随任务),不持 this。
  const auto* clip_generation = &owner_.clip_generation_;
  owner_.thread_pool_.Post([controller, hwnd, generation, clip_id, clip_generation]() {
    if (generation != clip_generation->load(std::memory_order_relaxed)) return;
    auto payload = std::make_unique<ClipReadyPayload>();
    payload->generation = generation;
    payload->clip_id = clip_id;
    payload->clip.emplace();
    payload->status = controller->GetClipDetailsById(clip_id, &payload->clip.value());
    if (!payload->status.ok()) {
      payload->clip.reset();
    }
    (void)PostOwnedMessage(hwnd, MainWindow::kClipReadyMessage, 0, std::move(payload));
  });
}

void HistoryDataController::RequestStartupMetadataAsync() {
  if (owner_.controller_ == nullptr || owner_.hwnd_ == nullptr || owner_.startup_metadata_warmup_completed_) {
    return;
  }
  auto* controller = owner_.controller_;
  const HWND hwnd = owner_.hwnd_;
  owner_.thread_pool_.Post([controller, hwnd]() {
    auto payload = std::make_unique<StartupMetadataReadyPayload>();
    payload->groups_status = controller->ListGroups(&payload->groups);
    payload->tags_status = controller->ListTags(&payload->tags);
    std::vector<core::store::QueueEntry> queue_entries;
    payload->queue_status = controller->ListQueue(&queue_entries);
    if (payload->queue_status.ok()) {
      payload->queue_count = static_cast<int>(queue_entries.size());
    }
    (void)PostOwnedMessage(hwnd, MainWindow::kStartupMetadataReadyMessage, 0, std::move(payload));
  });
}

void HistoryDataController::HandleCountReady(LPARAM l_param) {
  auto payload = TakeOwnedMessagePayload<CountReadyPayload>(l_param);
  if (payload == nullptr || !owner_.staging_snapshot_.active || payload->generation != owner_.staging_snapshot_.generation) {
    return;
  }
  if (!payload->status.ok()) {
    owner_.RefreshStatus(L"加载条目总数失败");
    return;
  }
  clipd2d::app::MarkStartupPhase("first_count_ready");
  owner_.staging_snapshot_.count_ready = true;
  owner_.staging_snapshot_.item_count = static_cast<int>(payload->count);
  owner_.staging_snapshot_.required_pages.clear();
  if (owner_.staging_snapshot_.item_count > 0 && owner_.hwnd_ != nullptr) {
    RECT client{};
    GetClientRect(owner_.hwnd_, &client);
    const PanelLayout layout = ComputeLayout(client.right, client.bottom);
    const int item_height = owner_.GetItemHeight();
    const int viewport_height = std::max(1, static_cast<int>(layout.list.bottom - layout.list.top));
    const int clamped_target = std::clamp(
        owner_.staging_snapshot_.target_scroll_offset,
        0,
        std::max(0, owner_.staging_snapshot_.item_count * item_height - viewport_height));
    int first_index = 0;
    int last_index = -1;
    std::unordered_set<int> pages;
    if (owner_.IsColdStartPresentationPending()) {
      pages = owner_.BuildColdStartPresentationPages(
          owner_.staging_snapshot_.item_count,
          clamped_target,
          viewport_height);
    } else {
      ComputeRequestRange(
          owner_.staging_snapshot_.item_count,
          item_height,
          viewport_height,
          clamped_target,
          clamped_target,
          0.0f,
          &first_index,
          &last_index);
      if (last_index >= first_index) {
        const int first_page = first_index / owner_.page_size_;
        const int last_page = last_index / owner_.page_size_;
        for (int page = first_page; page <= last_page; ++page) {
          pages.insert(page);
        }
      }
    }
    for (int page : pages) {
      if (page >= 0) {
        owner_.staging_snapshot_.required_pages.insert(page);
        RequestPageAsync(page, owner_.staging_snapshot_.generation, owner_.staging_snapshot_.query);
      }
    }
  }
  owner_.CommitStagingSnapshotIfReady();
}

void HistoryDataController::HandlePageReady(LPARAM l_param) {
  auto payload = TakeOwnedMessagePayload<PageReadyPayload>(l_param);
  if (payload == nullptr) {
    return;
  }
  owner_.pending_page_request_keys_.erase(MakePageRequestKey(payload->generation, payload->page_index));
  if (owner_.staging_snapshot_.active && payload->generation == owner_.staging_snapshot_.generation) {
    if (!payload->status.ok()) {
      owner_.RefreshStatus(L"加载列表失败");
      return;
    }
    clipd2d::app::MarkStartupPhase("first_page_ready");
    owner_.staging_snapshot_.page_cache[payload->page_index] = payload->clips;
    owner_.CommitStagingSnapshotIfReady();
    return;
  }
  if (payload->generation != owner_.display_generation_) {
    return;
  }
  if (!payload->status.ok()) {
    owner_.RefreshStatus(L"加载列表失败");
    return;
  }
  owner_.page_cache_[payload->page_index] = payload->clips;
  owner_.TouchPage(payload->page_index);
  owner_.TrimPageCache();
  owner_.SyncViewportHeightsForPage(payload->page_index);
  owner_.RequestVisibleThumbnails();
  if (owner_.selected_index_ >= 0) {
    const int page_begin = payload->page_index * owner_.page_size_;
    const int page_end = page_begin + static_cast<int>(payload->clips.size());
    if (owner_.selected_index_ >= page_begin && owner_.selected_index_ < page_end) {
      owner_.LoadSelection();
    }
  }
  owner_.ScheduleListPresent();
}

void HistoryDataController::HandleClipReady(LPARAM l_param) {
  auto payload = TakeOwnedMessagePayload<ClipReadyPayload>(l_param);
  if (payload == nullptr || payload->generation != owner_.clip_generation_) {
    return;
  }
  if (!payload->status.ok() || !payload->clip.has_value()) {
    owner_.RefreshStatus(L"加载条目详情失败");
    return;
  }
  owner_.selected_clip_ = std::move(payload->clip);
  owner_.actions_ = owner_.controller_->ResolveActions(*owner_.selected_clip_);
  SortActions(&owner_.actions_);
  owner_.show_html_source_ = false;
  owner_.preview_bitmap_.bitmap.Reset();
  // L6 脏类型核对补漏:详情/操作列表就绪必须置脏,否则"数据到了但界面不刷新"。
  owner_.MarkFrameDirty();
  if (owner_.preview_window_.hwnd() != nullptr &&
      IsWindowVisible(owner_.preview_window_.hwnd()) &&
      owner_.preview_window_.ShowingClip(owner_.selected_clip_->summary.id)) {
    owner_.preview_window_.ShowClip(*owner_.selected_clip_, owner_.BuildPreviewHostActions());
  }
}

void HistoryDataController::HandleStartupMetadataReady(LPARAM l_param) {
  auto payload = TakeOwnedMessagePayload<StartupMetadataReadyPayload>(l_param);
  if (payload == nullptr || owner_.startup_metadata_warmup_completed_) {
    return;
  }
  if (payload->groups_status.ok()) {
    owner_.groups_ = std::move(payload->groups);
  }
  if (payload->tags_status.ok()) {
    payload->tags.erase(
        std::remove_if(
            payload->tags.begin(),
            payload->tags.end(),
            [](const core::store::TagRecord& tag) {
              return tag.is_system;
            }),
        payload->tags.end());
    owner_.tags_ = std::move(payload->tags);
  }
  if (payload->queue_status.ok()) {
    owner_.queue_count_ = payload->queue_count;
  }
  owner_.startup_metadata_warmup_completed_ = true;
  owner_.ScheduleListPresent();
}

}  // namespace clipd2d::ui
