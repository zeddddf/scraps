#include "ui/HistoryAssetLoader.h"

#include "ui/ClipVisualFileContent.h"
#include "ui/ClipVisualAssets.h"
#include "ui/MainWindowInternal.h"

namespace clipd2d::ui {

HistoryAssetLoader::HistoryAssetLoader(MainWindow& owner) : owner_(owner) {}

void HistoryAssetLoader::EnsureBitmapCachesValid() {
  // d2d generation 判断保留在 AssetLoader（A2b 设计决策）。
  if (owner_.history_state().bitmap_generation_ == owner_.d2d_shell().target_generation()) {
    return;
  }
  owner_.history_state().bitmap_generation_ = owner_.d2d_shell().target_generation();
  owner_.history_state().thumbnail_bitmaps_.clear();
  owner_.history_state().preview_bitmap_.bitmap.Reset();
  owner_.ResetBitmapCaches();
  owner_.history_state().page_access_ticks_.clear();
  owner_.history_state().dirty_rows_.clear();
  owner_.RequestVisiblePages();
}

void HistoryAssetLoader::RequestSourceIconAsync(const core::store::ClipSummary& summary) {
  std::uint64_t generation = owner_.history_state().active_thumbnail_request_epoch_.load(std::memory_order_relaxed);
  if (generation == 0) {
    generation = owner_.history_state().thumbnail_request_epoch_.Next();
    owner_.history_state().active_thumbnail_request_epoch_.store(generation, std::memory_order_relaxed);
  }
  const auto request = owner_.BuildSourceIconRequest(summary);
  const std::wstring key = visual_assets::BuildShellIconCacheKey(request);
  if (key.empty()) {
    return;
  }
  if (owner_.history_state().thumbnail_bitmaps_.contains(key)) {
    return;
  }
  const auto pending_it = owner_.history_state().pending_thumbnail_generations_.find(key);
  if (pending_it != owner_.history_state().pending_thumbnail_generations_.end() &&
      pending_it->second == generation) {
    return;
  }
  owner_.history_state().pending_thumbnail_generations_[key] = generation;
  const HWND hwnd = owner_.hwnd();
  // Q3:任务只携带快照(epoch 原子指针、请求、键),不持 this、不碰窗口状态。
  const auto* request_epoch = &owner_.history_state().active_thumbnail_request_epoch_;
  owner_.history_pool().Post([hwnd, request, key, generation, request_epoch]() {
    if (request_epoch->load(std::memory_order_relaxed) != generation) {
      return;
    }
    auto payload = std::make_unique<BitmapReadyPayload>();
    payload->generation = generation;
    payload->key = key;
    visual_assets::ShellIconPixels shell_icon;
    if (visual_assets::LoadShellIconPixels(request, &shell_icon)) {
      payload->width = shell_icon.width;
      payload->height = shell_icon.height;
      payload->pixels = std::move(shell_icon.pixels);
    }
    if (hwnd != nullptr &&
        request_epoch->load(std::memory_order_relaxed) == generation) {
      (void)PostOwnedMessage(hwnd, MainWindow::kThumbnailReadyMessage, 0, std::move(payload));
    }
  });
}

void HistoryAssetLoader::RequestFileItemIconAsync(const core::store::ClipSummary& summary) {
  std::uint64_t generation = owner_.history_state().active_thumbnail_request_epoch_.load(std::memory_order_relaxed);
  if (generation == 0) {
    generation = owner_.history_state().thumbnail_request_epoch_.Next();
    owner_.history_state().active_thumbnail_request_epoch_.store(generation, std::memory_order_relaxed);
  }
  const auto request_opt = owner_.BuildFileItemIconRequest(summary);
  if (!request_opt.has_value()) {
    return;
  }
  const auto request = *request_opt;
  const std::wstring key = visual_assets::BuildShellIconCacheKey(request);
  if (key.empty()) {
    return;
  }
  if (owner_.history_state().thumbnail_bitmaps_.contains(key)) {
    return;
  }
  const auto pending_it = owner_.history_state().pending_thumbnail_generations_.find(key);
  if (pending_it != owner_.history_state().pending_thumbnail_generations_.end() &&
      pending_it->second == generation) {
    return;
  }
  owner_.history_state().pending_thumbnail_generations_[key] = generation;
  const HWND hwnd = owner_.hwnd();
  const auto* request_epoch = &owner_.history_state().active_thumbnail_request_epoch_;
  owner_.history_pool().Post([hwnd, request, key, generation, request_epoch]() {
    if (request_epoch->load(std::memory_order_relaxed) != generation) {
      return;
    }
    auto payload = std::make_unique<BitmapReadyPayload>();
    payload->generation = generation;
    payload->key = key;
    visual_assets::ShellIconPixels shell_icon;
    if (visual_assets::LoadShellIconPixels(request, &shell_icon)) {
      payload->width = shell_icon.width;
      payload->height = shell_icon.height;
      payload->pixels = std::move(shell_icon.pixels);
    }
    if (hwnd != nullptr &&
        request_epoch->load(std::memory_order_relaxed) == generation) {
      (void)PostOwnedMessage(hwnd, MainWindow::kThumbnailReadyMessage, 0, std::move(payload));
    }
  });
}

void HistoryAssetLoader::RequestThumbnailAsync(const core::store::ClipSummary& summary) {
  std::uint64_t generation = owner_.history_state().active_thumbnail_request_epoch_.load(std::memory_order_relaxed);
  if (generation == 0) {
    generation = owner_.history_state().thumbnail_request_epoch_.Next();
    owner_.history_state().active_thumbnail_request_epoch_.store(generation, std::memory_order_relaxed);
  }
  const auto request = visual_assets::BuildClipThumbnailLoadRequest(
      summary,
      owner_.GetFileThumbnailTargetExtent());
  const std::wstring key = request.key;
  if (key.empty()) {
    return;
  }
  if (owner_.history_state().thumbnail_bitmaps_.contains(key)) {
    return;
  }
  const auto pending_it = owner_.history_state().pending_thumbnail_generations_.find(key);
  if (pending_it != owner_.history_state().pending_thumbnail_generations_.end() &&
      pending_it->second == generation) {
    return;
  }
  owner_.history_state().pending_thumbnail_generations_[key] = generation;
  const HWND hwnd = owner_.hwnd();
  if (request.kind == visual_assets::ThumbnailLoadKind::SystemThumbnail) {
    const int target_extent = request.max_width;
    const bool allow_fallback_decode = request.allow_fallback_decode;
    const auto* request_epoch = &owner_.history_state().active_thumbnail_request_epoch_;
    owner_.history_pool().Post([hwnd, key, generation, allow_fallback_decode, target_extent, request_epoch]() {
      if (request_epoch->load(std::memory_order_relaxed) != generation) {
        return;
      }
      auto payload = std::make_unique<BitmapReadyPayload>();
      payload->generation = generation;
      payload->key = key;
      if (visual_assets::LoadSystemThumbnailPixels(key, target_extent, target_extent, &payload->width, &payload->height, &payload->pixels)) {
        if (hwnd != nullptr &&
            request_epoch->load(std::memory_order_relaxed) == generation) {
          (void)PostOwnedMessage(hwnd, MainWindow::kThumbnailReadyMessage, 0, std::move(payload));
        }
        return;
      }

      // Q3:回退解码在本任务内联执行(不嵌套 Post 触碰 owner_/线程池)。
      if (allow_fallback_decode) {
        auto fallback_payload = std::make_unique<BitmapReadyPayload>();
        fallback_payload->generation = generation;
        fallback_payload->key = key;
        const common::Status status = visual_assets::LoadScaledBitmapPixels(
            key,
            target_extent,
            target_extent,
            &fallback_payload->width,
            &fallback_payload->height,
            &fallback_payload->pixels);
        if (hwnd != nullptr &&
            request_epoch->load(std::memory_order_relaxed) == generation) {
          if (!status.ok()) {
            fallback_payload->pixels.clear();
          }
          (void)PostOwnedMessage(hwnd, MainWindow::kThumbnailReadyMessage, 0, std::move(fallback_payload));
        }
        return;
      }

      if (hwnd != nullptr &&
          request_epoch->load(std::memory_order_relaxed) == generation) {
        (void)PostOwnedMessage(hwnd, MainWindow::kThumbnailReadyMessage, 0, std::move(payload));
      }
    });
    return;
  }

  const auto* request_epoch = &owner_.history_state().active_thumbnail_request_epoch_;
  owner_.history_pool().Post([hwnd, key, generation, max_width = request.max_width, max_height = request.max_height, request_epoch]() {
    if (request_epoch->load(std::memory_order_relaxed) != generation) {
      return;
    }
    auto payload = std::make_unique<BitmapReadyPayload>();
    payload->generation = generation;
    payload->key = key;
    (void)visual_assets::LoadScaledBitmapPixels(key, max_width, max_height, &payload->width, &payload->height, &payload->pixels);
    if (hwnd != nullptr &&
        request_epoch->load(std::memory_order_relaxed) == generation) {
      (void)PostOwnedMessage(hwnd, MainWindow::kThumbnailReadyMessage, 0, std::move(payload));
    }
  });
}

void HistoryAssetLoader::RequestPreviewAsync(const core::store::ClipRecord& clip) {
  const std::wstring key = !clip.preview_path.empty() ? clip.preview_path : clip.storage_path;
  if (clip.summary.content_type != core::store::ClipContentType::Image || key.empty()) {
    owner_.set_preview_load_pending(false);
    owner_.history_state().preview_bitmap_.bitmap.Reset();
    return;
  }

  const std::uint64_t generation = owner_.history_state().preview_generation_.Next();
  const HWND hwnd = owner_.hwnd();
  // L6:加载在途标记进 has_pending_work 清单;结果(成功或失败)一律回投,
  // 由 HandlePreviewReady 清标记,避免"失败即永久挂起"。
  owner_.set_preview_load_pending(true);
  owner_.history_pool().Post([hwnd, generation, key]() {
    auto payload = std::make_unique<BitmapReadyPayload>();
    payload->generation = generation;
    payload->key = key;
    (void)visual_assets::LoadScaledBitmapPixels(key, 344, 420, &payload->width, &payload->height, &payload->pixels);
    (void)PostOwnedMessage(hwnd, MainWindow::kPreviewReadyMessage, 0, std::move(payload));
  });
}

void HistoryAssetLoader::HandleIconReady(LPARAM l_param) {
  (void)l_param;
}

void HistoryAssetLoader::HandlePreviewReady(LPARAM l_param) {
  auto payload = TakeOwnedMessagePayload<BitmapReadyPayload>(l_param);
  if (payload == nullptr || owner_.history_state().preview_generation_.IsStale(payload->generation)) {
    return;
  }
  // L6:最新一代预览加载结束(无论成败),清除在途标记。
  owner_.set_preview_load_pending(false);
  owner_.history_state().preview_bitmap_.generation = owner_.d2d_shell().target_generation();
  owner_.history_state().preview_bitmap_.width = payload->width;
  owner_.history_state().preview_bitmap_.height = payload->height;
  owner_.history_state().preview_bitmap_.bitmap.Reset();
  if (!payload->pixels.empty()) {
    owner_.d2d_shell().CreateBitmapFromBgra(payload->width, payload->height, payload->pixels, owner_.history_state().preview_bitmap_.bitmap.ReleaseAndGetAddressOf());
  }
  // L6 脏类型核对补漏:预览位图就绪必须置脏,否则"数据到了但界面不刷新"。
  owner_.MarkFrameDirty();
}

void HistoryAssetLoader::HandleThumbnailReady(LPARAM l_param) {
  auto payload = TakeOwnedMessagePayload<BitmapReadyPayload>(l_param);
  if (payload == nullptr) {
    return;
  }
  const auto pending_it = owner_.history_state().pending_thumbnail_generations_.find(payload->key);
  if (pending_it == owner_.history_state().pending_thumbnail_generations_.end() ||
      pending_it->second != payload->generation) {
    return;
  }
  owner_.history_state().pending_thumbnail_generations_.erase(pending_it);
  if (payload->pixels.empty()) {
    return;
  }
  BitmapEntry entry;
  entry.generation = owner_.d2d_shell().target_generation();
  entry.width = payload->width;
  entry.height = payload->height;
  owner_.d2d_shell().CreateBitmapFromBgra(payload->width, payload->height, payload->pixels, entry.bitmap.ReleaseAndGetAddressOf());
  if (entry.bitmap != nullptr) {
    owner_.TouchBitmap(&entry);
    owner_.history_state().thumbnail_bitmaps_[payload->key] = std::move(entry);
  }
  owner_.TrimBitmapCaches();
  owner_.MarkDirtyRowsForImageKey(payload->key);
  owner_.ScheduleListPresent();
}

}  // namespace clipd2d::ui
