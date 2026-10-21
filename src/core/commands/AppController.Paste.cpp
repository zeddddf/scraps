#include "core/commands/AppController.Internal.h"

namespace clipd2d::core::commands {

common::Status AppController::PasteLatest(HWND target_window) {
  store::ClipRecord clip;
  {
    std::scoped_lock lock(mutex_);
    common::Status status = repository_.GetLatestClip(&clip);
    if (!status.ok()) {
      return status;
    }
  }
  return ExecutePasteTransaction(clip, nullptr, target_window, clipboard::PasteRoute::Recovery, {}, false);
}

common::Status AppController::PasteLatest(
    const clipboard::PasteTargetContext& target_context,
    clipboard::PasteRoute route) {
  store::ClipRecord clip;
  {
    std::scoped_lock lock(mutex_);
    common::Status status = repository_.GetLatestClip(&clip);
    if (!status.ok()) {
      return status;
    }
  }
  return ExecutePasteTransaction(clip, &target_context, nullptr, route, {}, false);
}

common::Status AppController::CopyClipForStickerById(std::int64_t id) {
  store::ClipRecord clip;
  {
    std::scoped_lock lock(mutex_);
    common::Status status = repository_.GetClipById(id, &clip);
    if (!status.ok()) {
      return status;
    }
    if (!IsImageStickerCandidate(clip)) {
      return common::Status::Error(common::StatusCode::InvalidArgument, "Sticker requires a single image clip.");
    }
    SuppressClipboardMonitor();
  }
  if (clip.summary.content_type == store::ClipContentType::Files) {
    const std::wstring path = FirstFilePathFromClip(clip);
    if (path.empty()) {
      return common::Status::Error(common::StatusCode::NotFound, "Sticker image path not found.");
    }
    common::Status status = clipboard::SetClipboardImageFromFile(path);
    if (status.ok()) {
      std::scoped_lock lock(mutex_);
      pending_arrival_reason_ = store::ClipArrivalReason::InternalStickerWriteback;
      pending_arrival_sequence_ = GetClipboardSequenceNumber();
    }
    return status;
  }

  common::Status status = clipboard::WriteClipToClipboard(clip, {}, false);
  if (status.ok()) {
    std::scoped_lock lock(mutex_);
    pending_arrival_reason_ = store::ClipArrivalReason::InternalStickerWriteback;
    pending_arrival_sequence_ = GetClipboardSequenceNumber();
  }
  return status;
}

common::Status AppController::PasteById(std::int64_t id, HWND target_window) {
  store::ClipRecord clip;
  {
    std::scoped_lock lock(mutex_);
    common::Status status = repository_.GetClipById(id, &clip);
    if (!status.ok()) {
      return status;
    }
  }
  return ExecutePasteTransaction(clip, nullptr, target_window, clipboard::PasteRoute::Recovery, {}, false);
}

common::Status AppController::PasteById(
    std::int64_t id,
    const clipboard::PasteTargetContext& target_context,
    clipboard::PasteRoute route) {
  store::ClipRecord clip;
  {
    std::scoped_lock lock(mutex_);
    common::Status status = repository_.GetClipById(id, &clip);
    if (!status.ok()) {
      return status;
    }
  }
  return ExecutePasteTransaction(clip, &target_context, nullptr, route, {}, false);
}

common::Status AppController::PastePlainTextById(std::int64_t id, HWND target_window) {
  store::ClipRecord clip;
  {
    std::scoped_lock lock(mutex_);
    common::Status status = repository_.GetClipById(id, &clip);
    if (!status.ok()) {
      return status;
    }
  }
  return ExecutePasteTransaction(
      clip,
      nullptr,
      target_window,
      clipboard::PasteRoute::Recovery,
      clip.plain_text,
      true);
}

common::Status AppController::PastePlainTextById(
    std::int64_t id,
    const clipboard::PasteTargetContext& target_context,
    clipboard::PasteRoute route) {
  store::ClipRecord clip;
  {
    std::scoped_lock lock(mutex_);
    common::Status status = repository_.GetClipById(id, &clip);
    if (!status.ok()) {
      return status;
    }
  }
  return ExecutePasteTransaction(clip, &target_context, nullptr, route, clip.plain_text, true);
}

common::Status AppController::TransformPasteById(std::int64_t id, std::string_view action_id, HWND target_window) {
  store::ClipRecord clip;
  std::string transformed;
  {
    std::scoped_lock lock(mutex_);
    common::Status status = repository_.GetClipById(id, &clip);
    if (!status.ok()) {
      return status;
    }
    status = text_transform_engine_.Transform(action_id, clip.plain_text, &transformed);
    if (!status.ok()) {
      return status;
    }
  }
  return ExecutePasteTransaction(
      clip,
      nullptr,
      target_window,
      clipboard::PasteRoute::Recovery,
      transformed,
      true);
}

common::Status AppController::TransformPasteById(
    std::int64_t id,
    std::string_view action_id,
    const clipboard::PasteTargetContext& target_context,
    clipboard::PasteRoute route) {
  store::ClipRecord clip;
  std::string transformed;
  {
    std::scoped_lock lock(mutex_);
    common::Status status = repository_.GetClipById(id, &clip);
    if (!status.ok()) {
      return status;
    }
    status = text_transform_engine_.Transform(action_id, clip.plain_text, &transformed);
    if (!status.ok()) {
      return status;
    }
  }
  return ExecutePasteTransaction(clip, &target_context, nullptr, route, transformed, true);
}

common::Status AppController::PasteNextInQueue(
    const clipboard::PasteTargetContext& target_context,
    clipboard::PasteRoute route) {
  std::vector<store::QueueEntry> queue_entries;
  {
    std::scoped_lock lock(mutex_);
    common::Status status = repository_.ListQueue(&queue_entries);
    if (!status.ok()) {
      return status;
    }
  }

  for (const auto& entry : queue_entries) {
    store::ClipRecord clip;
    bool entry_available = false;
    {
      std::scoped_lock lock(mutex_);
      entry_available = repository_.GetClipById(entry.clip_id, &clip).ok();
      if (!entry_available) {
        repository_.RemoveFromQueue(entry.clip_id);
      }
    }
    if (!entry_available) {
      continue;
    }
    common::Status status = ExecutePasteTransaction(clip, &target_context, nullptr, route, {}, false);
    if (!status.ok()) {
      return status;
    }
    if (entry.auto_pop) {
      std::scoped_lock lock(mutex_);
      repository_.RemoveFromQueue(entry.clip_id);
    }
    return common::Status::Ok();
  }

  return common::Status::Error(common::StatusCode::NotFound, "队列中没有可粘贴条目。");
}


// REQUIRES: 调用方不得持有 mutex_。粘贴事务内的剪贴板写入与 SendInput 全程
// 不占控制器总锁，共享状态只在短临界区里读写，避免卡住剪贴板采集工作线程。
common::Status AppController::ExecutePasteTransaction(
    const store::ClipRecord& clip,
    const clipboard::PasteTargetContext* target_context,
    HWND target_window,
    clipboard::PasteRoute route,
    std::string_view override_text,
    bool use_override_text) {
  clipboard::PasteTargetContext paste_target =
      target_context != nullptr ? *target_context : clipboard::CapturePasteTargetContext(target_window);
  if (!clipboard::IsValidPasteTargetContext(paste_target) && target_window != nullptr && IsWindow(target_window)) {
    paste_target = clipboard::CapturePasteTargetContext(target_window);
  }
  {
    std::scoped_lock lock(mutex_);
    SuppressClipboardMonitor();
  }

  common::Status status = clipboard::WriteClipToClipboard(clip, override_text, use_override_text);
  if (!status.ok()) {
    return status;
  }
  {
    std::scoped_lock lock(mutex_);
    pending_arrival_reason_ = store::ClipArrivalReason::InternalClipboardWriteback;
    pending_arrival_sequence_ = GetClipboardSequenceNumber();
    common::WriteLog(
        common::LogLevel::Info,
        "clipboard.sequence",
        "arrival-sequence armed reason=internal-writeback sequence=" + std::to_string(pending_arrival_sequence_));
  }

  const std::string text_fallback =
      use_override_text ? std::string(override_text) : BuildPasteTextFallback(clip);
  status = clipboard::PerformPasteToTarget(paste_target, route, text_fallback);
  if (!status.ok()) {
    return status;
  }
  std::scoped_lock lock(mutex_);
  return repository_.MarkPasted(clip.summary.id, settings_.promote_pasted_clip_on_reuse);
}

}  // namespace clipd2d::core::commands
