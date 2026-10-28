#include "core/commands/AppController.Internal.h"

namespace clipd2d::core::commands {

bool AppController::QueuePaste(QueuedPasteRequest request) {
  {
    std::scoped_lock lock(mutex_);
    // 入队闸门（clip_id>0 / Close 后拒新）在 PasteQueueModel；壳只做锁与唤醒。
    if (!paste_queue_.Enqueue(std::move(request))) {
      return false;
    }
  }
  paste_worker_cv_.notify_one();
  return true;
}

bool AppController::HasPendingPaste() const {
  std::scoped_lock lock(mutex_);
  return paste_queue_.HasPending();
}

void AppController::SetPasteResultDispatcher(std::function<void(const common::Status&)> dispatcher) {
  std::scoped_lock lock(mutex_);
  paste_result_dispatcher_ = std::move(dispatcher);
}

// 串行执行排队的粘贴请求。事务内部的剪贴板写回、前台恢复与按键派发全部发生在本线程，
// UI 线程不再承担这些阻塞工作（这也是「粘贴不再卡面板」的机制所在）。
// 失败时经 paste_result_dispatcher_ 回抛；回调由 UI 注册，负责 PostMessage 回主线程。
void AppController::PasteWorkerLoop() {
  common::WriteLog(
      common::LogLevel::Info, "clipboard.paste", "paste worker started tid=" + std::to_string(GetCurrentThreadId()));
  for (;;) {
    QueuedPasteRequest request;
    bool has_request = false;
    {
      std::unique_lock lock(mutex_);
      paste_worker_cv_.wait(lock, [this]() {
        return shutting_down_ || paste_queue_.HasPending();
      });
      if (shutting_down_ && !paste_queue_.HasPending()) {
        break;
      }
      has_request = paste_queue_.Pop(&request);
    }
    if (!has_request) {
      continue;
    }

    common::Stopwatch request_stopwatch;
    common::Status status = common::Status::Ok();
    // kind → 执行面映射固化在 PasteQueueModel::DispatchFor（有单测）；分发闭包仍在本壳。
    switch (DispatchFor(request.kind)) {
      case PasteDispatchTarget::ById:
        status = PasteById(request.clip_id, request.target_context, request.route);
        break;
      case PasteDispatchTarget::PlainText:
        status = PastePlainTextById(request.clip_id, request.target_context, request.route);
        break;
      case PasteDispatchTarget::Action:
        status = TransformPasteById(request.clip_id, request.action_id, request.target_context, request.route);
        break;
    }
    common::WriteLog(
        common::LogLevel::Info,
        "clipboard.paste",
        "request done kind=" + std::to_string(static_cast<int>(request.kind)) +
            " clip_id=" + std::to_string(request.clip_id) +
            " status=" + std::to_string(static_cast<int>(status.code)) +
            " elapsed_ms=" + common::FormatMs(request_stopwatch.ElapsedUs()) +
            " tid=" + std::to_string(GetCurrentThreadId()));

    if (!status.ok()) {
      std::function<void(const common::Status&)> dispatcher;
      {
        std::scoped_lock lock(mutex_);
        dispatcher = paste_result_dispatcher_;
      }
      if (dispatcher != nullptr) {
        dispatcher(status);
      }
    }
  }
  common::WriteLog(common::LogLevel::Info, "clipboard.paste", "paste worker stopped");
}

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
    {
      std::scoped_lock lock(mutex_);
      internal_writeback_baseline_sequence_ = GetClipboardSequenceNumber();
    }
    common::Status status = clipboard::SetClipboardImageFromFile(path);
    if (status.ok()) {
      std::scoped_lock lock(mutex_);
      pending_arrival_reason_ = store::ClipArrivalReason::InternalStickerWriteback;
      pending_arrival_sequence_ = GetClipboardSequenceNumber();
    }
    return status;
  }

  {
    std::scoped_lock lock(mutex_);
    internal_writeback_baseline_sequence_ = GetClipboardSequenceNumber();
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
  // 阶段计时：写入 / 派发两段是历史上 UI 卡顿的全部来源（WIC 解码、像素拷贝、
  // 修饰键等待、前台恢复轮询）。现在它们在 paste worker 线程里跑，这里的数字
  // 只用于验证「UI 不再等待」与后续优化，不参与任何决策。
  common::Stopwatch txn_stopwatch;
  {
    std::scoped_lock lock(mutex_);
    SuppressClipboardMonitor();
    // 区间判定下界：写回期间 EmptyClipboard 与 CF_DIB 派生格式合成会多次推进
    // sequence，[baseline, final] 内的事件都算内部写回。
    internal_writeback_baseline_sequence_ = GetClipboardSequenceNumber();
  }

  common::Status status = clipboard::WriteClipToClipboard(clip, override_text, use_override_text);
  const std::int64_t writeback_us = txn_stopwatch.ElapsedUs();
  if (!status.ok()) {
    return status;
  }
  {
    std::scoped_lock lock(mutex_);
    pending_arrival_reason_ = store::ClipArrivalReason::InternalClipboardWriteback;
    pending_arrival_sequence_ = GetClipboardSequenceNumber();
    last_internal_writeback_identity_key_ = clip.summary.identity_key;
    last_internal_writeback_tick_ms_ = GetTickCount64();
    common::WriteLog(
        common::LogLevel::Info,
        "clipboard.sequence",
        "arrival-sequence armed reason=internal-writeback baseline=" +
            std::to_string(internal_writeback_baseline_sequence_) +
            " final=" + std::to_string(pending_arrival_sequence_));
  }

  const std::string text_fallback =
      use_override_text ? std::string(override_text) : BuildPasteTextFallback(clip);
  status = clipboard::PerformPasteToTarget(paste_target, route, text_fallback);
  const std::int64_t dispatch_us = txn_stopwatch.ElapsedUs() - writeback_us;
  if (!status.ok()) {
    return status;
  }
  {
    std::scoped_lock lock(mutex_);
    status = repository_.MarkPasted(clip.summary.id, settings_.promote_pasted_clip_on_reuse);
  }
  common::WriteLog(
      common::LogLevel::Info,
      "clipboard.paste",
      "transaction clip_id=" + std::to_string(clip.summary.id) +
          " type=" + std::to_string(static_cast<int>(clip.summary.content_type)) +
          " writeback_ms=" + common::FormatMs(writeback_us) +
          " dispatch_ms=" + common::FormatMs(dispatch_us) +
          " total_ms=" + common::FormatMs(txn_stopwatch.ElapsedUs()) +
          " tid=" + std::to_string(GetCurrentThreadId()));
  return status;
}

}  // namespace clipd2d::core::commands
