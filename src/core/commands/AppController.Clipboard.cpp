#include "core/commands/AppController.Internal.h"

#include <chrono>

namespace clipd2d::core::commands {

AppController::AppController() : settings_repository_(&repository_) {}

AppController::~AppController() {
  Shutdown();
}

common::Status AppController::Initialize(const common::AppPaths& paths) {
  std::scoped_lock lock(mutex_);
  if (clipboard_worker_.joinable() || paste_worker_.joinable() || ingest_worker_.joinable() ||
      ocr_worker_.joinable() || backup_worker_.joinable()) {
    return common::Status::Ok();
  }
  paths_ = paths;
  common::Status status = repository_.Initialize(paths_);
  if (!status.ok()) {
    return status;
  }
  status = settings_repository_.Load(&settings_);
  if (!status.ok()) {
    return status;
  }
  PublishSettingsCacheLocked();
  shutting_down_ = false;
  paste_queue_.Reset();  // 与 shutting_down_ 同步开闸（Initialize 再启动路径）
  clipboard_worker_ = std::thread([this]() {
    ClipboardWorkerLoop();
  });
  paste_worker_ = std::thread([this]() {
    PasteWorkerLoop();
  });
  ingest_worker_ = std::thread([this]() {
    IngestWorkerLoop();
  });
  ocr_worker_ = std::thread([this]() {
    OcrWorkerLoop();
  });
  backup_worker_ = std::thread([this]() {
    BackupWorkerLoop();
  });
  return common::Status::Ok();
}

void AppController::Shutdown() {
  {
    std::scoped_lock lock(mutex_);
    shutting_down_ = true;
    pending_clipboard_events_.clear();
    pending_arrival_reason_.reset();
    pending_arrival_sequence_ = 0;
    paste_queue_.Close();   // 对应 shutting_down_：拒新
    paste_queue_.Clear();   // 丢弃未执行请求（与旧 pending_paste_requests_.clear() 同语义）
    pending_image_ingests_.clear();
    active_image_ingests_.clear();
    pending_ocr_jobs_.clear();
    active_ocr_jobs_.clear();
    clipboard_capture_in_flight_ = false;
    in_flight_clipboard_sequence_ = 0;
  }
  clipboard_worker_cv_.notify_all();
  paste_worker_cv_.notify_all();
  ingest_worker_cv_.notify_all();
  ocr_worker_cv_.notify_all();
  backup_worker_cv_.notify_all();
  if (clipboard_worker_.joinable()) {
    clipboard_worker_.join();
  }
  if (paste_worker_.joinable()) {
    paste_worker_.join();
  }
  if (ingest_worker_.joinable()) {
    ingest_worker_.join();
  }
  if (ocr_worker_.joinable()) {
    ocr_worker_.join();
  }
  if (backup_worker_.joinable()) {
    backup_worker_.join();
  }
}

void AppController::SetRefreshDispatcher(std::function<void(const RefreshRequest&)> dispatcher) {
  std::scoped_lock lock(mutex_);
  refresh_dispatcher_ = std::move(dispatcher);
}

std::optional<AppController::PendingClipboardEvent> AppController::TakeNextPendingClipboardEvent() {
  const std::uint64_t now_tick_ms = GetTickCount64();
  for (auto it = pending_clipboard_events_.begin(); it != pending_clipboard_events_.end();) {
    if (it->sequence == 0 || it->sequence <= last_processed_clipboard_sequence_) {
      it = pending_clipboard_events_.erase(it);
      continue;
    }
    if (it->not_before_tick_ms > now_tick_ms) {
      ++it;
      continue;
    }
    const PendingClipboardEvent event = *it;
    pending_clipboard_events_.erase(it);
    return event;
  }
  return std::nullopt;
}

store::ReuseTouchPolicy AppController::ConsumeClipboardTouchPolicy(DWORD sequence, bool internal_writeback) {
  bool should_clear_state = false;
  const store::ReuseTouchPolicy policy = detail::ResolveClipboardTouchPolicy(
      pending_arrival_reason_,
      internal_writeback,
      settings_.promote_pasted_clip_on_reuse,
      &should_clear_state);
  if (should_clear_state) {
    if (pending_arrival_reason_.has_value() && pending_arrival_sequence_ != 0 && !internal_writeback) {
      common::WriteLog(
          common::LogLevel::Info,
          "clipboard.sequence",
          "arrival-sequence expired expected=" + std::to_string(pending_arrival_sequence_) +
              " actual=" + std::to_string(sequence));
    }
    pending_arrival_reason_.reset();
    pending_arrival_sequence_ = 0;
    internal_writeback_baseline_sequence_ = 0;
  }
  if (policy == store::ReuseTouchPolicy::KeepPositionOnReuse) {
    common::WriteLog(
        common::LogLevel::Info,
        "clipboard.sequence",
        "arrival-sequence matched internal writeback sequence=" + std::to_string(sequence));
  }
  return policy;
}

bool AppController::IsSuppressedClipboardUpdate() const {
  const std::int64_t now_ms = NowEpochMs();
  return now_ms < suppress_until_ms_ ||
      now_ms < common::GetGlobalClipboardSuppressUntil();
}

void AppController::SuppressClipboardMonitor() {
  suppress_until_ms_ = NowEpochMs() + 300;
  common::SetGlobalClipboardSuppressUntil(suppress_until_ms_);
}

bool AppController::IsBlockedByFilter(const store::CapturedClip& clip) const {
  if (settings_.ignore_large_text && clip.content_type == store::ClipContentType::Text) {
    const size_t text_kb = clip.plain_text.size() / 1024;
    if (text_kb > static_cast<size_t>(settings_.max_text_size_kb)) {
      return true;
    }
  }

  if (settings_.ignore_large_images && clip.content_type == store::ClipContentType::Image) {
    const size_t image_kb = clip.ingest_bytes.size() / 1024;
    if (image_kb > static_cast<size_t>(settings_.max_image_size_kb)) {
      return true;
    }
  }

  if (settings_.auto_ignore_password_managers) {
    const std::wstring lower_path = common::ToLowerWide(clip.source_app.process_path);
    if (lower_path.find(L"1password.exe") != std::wstring::npos ||
        lower_path.find(L"keepass.exe") != std::wstring::npos ||
        lower_path.find(L"keepassxc.exe") != std::wstring::npos ||
        lower_path.find(L"bitwarden.exe") != std::wstring::npos ||
        lower_path.find(L"enpass.exe") != std::wstring::npos) {
      return true;
    }
  }

  if (clip.content_type == store::ClipContentType::Text) {
    const std::string& text = clip.plain_text;
    const size_t len = text.size();

    if (settings_.ignore_otp_codes && detail::IsLikelyOtpCode(text)) {
      return true;
    }

    if (settings_.ignore_complex_passwords) {
      if (len >= 8 && len <= 64) {
        bool has_upper = false;
        bool has_lower = false;
        bool has_digit = false;
        bool has_special = false;
        bool has_space = false;
        for (char c : text) {
          if (std::isupper(static_cast<unsigned char>(c))) has_upper = true;
          else if (std::islower(static_cast<unsigned char>(c))) has_lower = true;
          else if (std::isdigit(static_cast<unsigned char>(c))) has_digit = true;
          else if (std::isspace(static_cast<unsigned char>(c))) has_space = true;
          else has_special = true;
        }
        if (!has_space && has_upper && has_lower && has_digit && has_special) {
          return true;
        }
      }
    }
  }

  const std::wstring custom_blacklist = common::ToLowerWide(settings_.custom_blacklist_apps);
  const std::wstring blacklist = common::ToLowerWide(settings_.blacklist);
  
  const std::wstring combined_blacklist = blacklist.empty() ? custom_blacklist : (custom_blacklist.empty() ? blacklist : (blacklist + L"\n" + custom_blacklist));

  if (!combined_blacklist.empty()) {
    const std::wstring source_text =
        common::ToLowerWide(clip.source_app.process_name + L"\n" + clip.source_app.process_path + L"\n" + clip.source_app.window_title);
    for (const std::wstring& token : common::SplitWide(combined_blacklist, L'\n')) {
      const std::wstring trimmed = common::TrimWide(token);
      if (!trimmed.empty() && source_text.find(trimmed) != std::wstring::npos) {
        return true;
      }
    }
  }
  return false;
}

common::Status AppController::OnClipboardChanged(HWND source_window, DWORD sequence, bool* inserted, std::int64_t* clip_id) {
  if (inserted != nullptr) {
    *inserted = false;
  }
  if (clip_id != nullptr) {
    *clip_id = 0;
  }

  {
    std::scoped_lock lock(mutex_);
    const bool matches_pending_internal_writeback =
        sequence != 0 &&
        pending_arrival_reason_.has_value() &&
        pending_arrival_sequence_ != 0 &&
        internal_writeback_baseline_sequence_ != 0 &&
        sequence > internal_writeback_baseline_sequence_ &&
        sequence <= pending_arrival_sequence_;
    if (shutting_down_ || !settings_.monitoring_enabled ||
        (IsSuppressedClipboardUpdate() && !matches_pending_internal_writeback)) {
      return common::Status::Ok();
    }
    if (sequence != 0) {
      if (sequence <= last_processed_clipboard_sequence_) {
        return common::Status::Ok();
      }
      const bool already_pending = std::any_of(
          pending_clipboard_events_.begin(),
          pending_clipboard_events_.end(),
          [sequence](const PendingClipboardEvent& event) {
            return event.sequence == sequence;
          });
      if (already_pending || sequence == in_flight_clipboard_sequence_) {
        return common::Status::Ok();
      }
      pending_clipboard_events_.push_back(PendingClipboardEvent{
          sequence, source_window, 0, 0, matches_pending_internal_writeback});
      common::WriteLog(
          common::LogLevel::Info,
          "clipboard.sequence",
          "enqueue sequence=" + std::to_string(sequence) +
              " queue=" + std::to_string(pending_clipboard_events_.size()) +
              " internal_match=" + std::string(matches_pending_internal_writeback ? "true" : "false"));
    }
  }
  clipboard_worker_cv_.notify_one();
  return common::Status::Ok();
}

void AppController::RunClipboardCaptureLoop() {
  for (;;) {
    DWORD current_sequence = 0;
    HWND current_source_window = nullptr;
    int current_deferred_attempt = 0;
    bool current_internal_writeback = false;
    store::AppSettings settings_snapshot;
    {
      std::unique_lock lock(mutex_);
      const auto next_event = TakeNextPendingClipboardEvent();
      if (!next_event.has_value()) {
        clipboard_capture_in_flight_ = false;
        in_flight_clipboard_sequence_ = 0;
        break;
      }
      current_sequence = next_event->sequence;
      current_source_window = next_event->source_window;
      current_deferred_attempt = next_event->deferred_attempt;
      current_internal_writeback = next_event->internal_writeback;
      in_flight_clipboard_sequence_ = current_sequence;
      clipboard_capture_in_flight_ = true;
      settings_snapshot = settings_;
      common::WriteLog(
          common::LogLevel::Info,
          "clipboard.sequence",
          "dequeue sequence=" + std::to_string(current_sequence) + " remaining=" + std::to_string(pending_clipboard_events_.size()));
    }

    bool local_inserted = false;
    std::int64_t local_clip_id = 0;
    common::Status status = common::Status::Ok();
    store::ReuseTouchPolicy touch_policy = store::ReuseTouchPolicy::PromoteOnReuse;
    RefreshRequest refresh_request = MakePreserveViewportRefreshRequest();
    bool exhausted_unavailable = false;
    bool skip_internal_sticker_capture = false;
    {
      std::scoped_lock lock(mutex_);
      if (detail::ShouldSkipClipboardCapture(
              pending_arrival_reason_,
              current_internal_writeback)) {
        pending_arrival_reason_.reset();
        pending_arrival_sequence_ = 0;
        skip_internal_sticker_capture = true;
      }
    }
    if (skip_internal_sticker_capture) {
      common::WriteLog(
          common::LogLevel::Info,
          "clipboard.sequence",
          "skip internal sticker capture sequence=" + std::to_string(current_sequence));
    } else if (current_source_window != nullptr) {
      for (size_t attempt = 0; attempt < kClipboardRetryDelaysMs.size(); ++attempt) {
        const DWORD delay_ms = kClipboardRetryDelaysMs[attempt];
        if (delay_ms > 0) {
          Sleep(delay_ms);
        }

        {
          std::scoped_lock lock(mutex_);
          if (shutting_down_) {
            break;
          }
        }

        store::CapturedClip clip;
        status = clipboard::CaptureClipboard(paths_, current_source_window, &clip);
        if (!status.ok()) {
          common::WriteLog(
              common::LogLevel::Info,
              "clipboard.capture",
              "sequence=" + std::to_string(current_sequence) +
                  " attempt=" + std::to_string(attempt + 1) +
                  " status=" + std::to_string(static_cast<int>(status.code)) +
                  " message=" + status.message);
          if (status.message.find(" png=") != std::string::npos) {
            common::WriteLog(
                common::LogLevel::Info,
                "clipboard.capture.image_formats",
                "sequence=" + std::to_string(current_sequence) +
                    " attempt=" + std::to_string(attempt + 1) +
                    " " + status.message);
          }
          if (ShouldRetryClipboardCapture(status) && attempt + 1 < kClipboardRetryDelaysMs.size()) {
            continue;
          }
          exhausted_unavailable = ShouldRetryClipboardCapture(status);
          break;
        }

        common::WriteLog(
            common::LogLevel::Info,
            "clipboard.capture",
            "sequence=" + std::to_string(current_sequence) +
                " attempt=" + std::to_string(attempt + 1) +
                " success " + BuildClipDebugSummary(clip));

        // 1x1/2x2 这类图片是垃圾条目：没有任何可用内容，却会占一个历史位置，
        // 而且和正常图片混在一起让"粘贴后列表变了"更难判断。它们多由后台进程写入，
        // 前台窗口归属（source_app）会指错，所以这里直接问剪贴板当前的**所有者窗口**。
        if (clip.content_type == store::ClipContentType::Image &&
            clip.image_width <= 2 && clip.image_height <= 2) {
          const HWND clipboard_owner = GetClipboardOwner();
          const std::wstring owner_process =
              clipboard_owner != nullptr ? clipboard::GetWindowProcessNameLower(clipboard_owner) : std::wstring();
          common::WriteLog(
              common::LogLevel::Warning,
              "clipboard.capture.tiny_image",
              "sequence=" + std::to_string(current_sequence) +
                  " size=" + std::to_string(clip.image_width) + "x" + std::to_string(clip.image_height) +
                  " hash=" + clip.content_hash.substr(0, std::min<size_t>(clip.content_hash.size(), 12)) +
                  " owner_process=" + common::WideToUtf8(owner_process).utf8 +
                  " foreground_source=" + common::WideToUtf8(clip.source_app.process_name).utf8 +
                  " window=" + common::WideToUtf8(clip.source_app.window_title).utf8);
        }

        bool blocked_by_filter = false;
        {
          std::scoped_lock lock(mutex_);
          blocked_by_filter = shutting_down_ || IsBlockedByFilter(clip);
          // 身份兜底：序列窗口判定会被「派生格式合成」击穿——写回 PNG+DIB+DIBV5 后，
          // 目标应用（或我们自己）第一次打开剪贴板时 Windows 会惰性合成派生格式，
          // 每次合成推进一个 sequence。实测图片写回一次推进 7 个，事件到达时
          // sequence 已越过登记的 final（日志 arrival-sequence expired
          // expected=23013 actual=23018），于是写回被当成外部复制 → PromoteOnReuse
          // → MakeJumpToTopRefreshRequest，条目跳到首位。
          // 内容身份（图片按归一化像素算）跨格式稳定，用它把漏判补回来。
          if (!current_internal_writeback &&
              pending_arrival_reason_.has_value() &&
              detail::MatchesInternalWritebackIdentity(
                  clip.identity_key,
                  last_internal_writeback_identity_key_,
                  GetTickCount64() - last_internal_writeback_tick_ms_,
                  kInternalWritebackEchoWindowMs)) {
            current_internal_writeback = true;
            common::WriteLog(
                common::LogLevel::Info,
                "clipboard.sequence",
                "writeback identity matched (sequence window missed) sequence=" +
                    std::to_string(current_sequence) +
                    " identity_key=" + clip.identity_key +
                    " age_ms=" + std::to_string(GetTickCount64() - last_internal_writeback_tick_ms_));
          }
          touch_policy = ConsumeClipboardTouchPolicy(current_sequence, current_internal_writeback);
          // 回声吸收：内部写回后短时间内同 identity_key 的再捕获（第三方剪贴板
          // 增强——如 PixPin——接力改写剪贴板所致）不提升位置，视为写回的回声。
          // 与上面的兜底的区别：这里不要求 pending_arrival_reason_ 仍在（窗口可能
          // 已被前一个事件消费掉）。
          if (!current_internal_writeback &&
              touch_policy == store::ReuseTouchPolicy::PromoteOnReuse &&
              detail::MatchesInternalWritebackIdentity(
                  clip.identity_key,
                  last_internal_writeback_identity_key_,
                  GetTickCount64() - last_internal_writeback_tick_ms_,
                  kInternalWritebackEchoWindowMs)) {
            touch_policy = store::ReuseTouchPolicy::KeepPositionOnReuse;
            common::WriteLog(
                common::LogLevel::Info,
                "clipboard.sequence",
                "echo absorbed identity_key=" + clip.identity_key +
                    " age_ms=" + std::to_string(GetTickCount64() - last_internal_writeback_tick_ms_));
          }
        }
        refresh_request = detail::BuildClipboardRefreshRequest(
            touch_policy,
            local_clip_id > 0 ? std::optional<std::int64_t>(local_clip_id) : std::nullopt);
        if (!blocked_by_filter) {
          {
            std::scoped_lock lock(mutex_);
            if (!shutting_down_) {
              status = repository_.InsertClip(clip, touch_policy, &local_inserted, &local_clip_id);
            }
          }
          refresh_request = detail::BuildClipboardRefreshRequest(
              touch_policy,
              local_clip_id > 0 ? std::optional<std::int64_t>(local_clip_id) : std::nullopt);
          if (status.ok() && NeedsPendingImageIngest(clip) && local_clip_id > 0) {
            StartPendingImageIngest(local_clip_id, std::move(clip));
          }
          if (status.ok()) {
            std::scoped_lock lock(mutex_);
            if (!shutting_down_) {
              status = repository_.ClearHistory(settings_snapshot.history_limit, settings_snapshot.history_retention_days);
            }
          }
          if (status.ok()) {
            common::WriteLog(
                common::LogLevel::Info,
                "clipboard.capture",
                "sequence=" + std::to_string(current_sequence) +
                    " stored clip_id=" + std::to_string(local_clip_id));
          }
        }
        break;
      }
    }

    bool deferred_requeued = false;
    DWORD deferred_delay_ms = 0;
    {
      std::scoped_lock lock(mutex_);
      if (exhausted_unavailable && current_sequence != 0 && !shutting_down_) {
        if (const auto delay_ms = detail::DeferredClipboardRetryDelayMs(current_deferred_attempt); delay_ms.has_value()) {
          deferred_delay_ms = *delay_ms;
          pending_clipboard_events_.push_back(PendingClipboardEvent{
              current_sequence,
              current_source_window,
              current_deferred_attempt + 1,
              GetTickCount64() + static_cast<std::uint64_t>(*delay_ms),
              current_internal_writeback});
          deferred_requeued = true;
        }
      }
      if (current_sequence != 0 && status.ok()) {
        last_processed_clipboard_sequence_ = current_sequence;
        common::WriteLog(
            common::LogLevel::Info,
            "clipboard.sequence",
            "commit sequence=" + std::to_string(current_sequence));
      }
      clipboard_capture_in_flight_ = false;
      in_flight_clipboard_sequence_ = 0;
    }

    if (deferred_requeued) {
      common::WriteLog(
          common::LogLevel::Info,
          "clipboard.capture",
          "deferred_requeue sequence=" + std::to_string(current_sequence) +
              " deferred_attempt=" + std::to_string(current_deferred_attempt + 1) +
              " delay_ms=" + std::to_string(deferred_delay_ms));
      clipboard_worker_cv_.notify_one();
    }
    if (exhausted_unavailable && current_sequence != 0) {
      common::WriteLog(
          common::LogLevel::Info,
          "clipboard.capture",
          deferred_requeued
              ? "Clipboard capture unavailable after immediate retries; deferred sequence " + std::to_string(current_sequence)
              : "Clipboard capture unavailable after all retries; sequence not committed " + std::to_string(current_sequence));
    }
    if (local_inserted) {
      NotifyRefreshFromWorker(refresh_request);
    }
  }
}

void AppController::ClipboardWorkerLoop() {
  for (;;) {
    std::unique_lock lock(mutex_);
    for (;;) {
      if (shutting_down_) {
        break;
      }
      const std::uint64_t now_tick_ms = GetTickCount64();
      bool has_ready_event = false;
      std::optional<std::uint64_t> next_due_tick_ms;
      for (const auto& event : pending_clipboard_events_) {
        if (event.sequence == 0 || event.sequence <= last_processed_clipboard_sequence_) {
          continue;
        }
        if (event.not_before_tick_ms <= now_tick_ms) {
          has_ready_event = true;
          break;
        }
        if (!next_due_tick_ms.has_value() || event.not_before_tick_ms < *next_due_tick_ms) {
          next_due_tick_ms = event.not_before_tick_ms;
        }
      }
      if (has_ready_event) {
        break;
      }
      if (!next_due_tick_ms.has_value()) {
        clipboard_worker_cv_.wait(lock, [this]() {
          if (shutting_down_) {
            return true;
          }
          return std::any_of(
              pending_clipboard_events_.begin(),
              pending_clipboard_events_.end(),
              [this](const PendingClipboardEvent& event) {
                return event.sequence > last_processed_clipboard_sequence_;
              });
        });
        continue;
      }
      const std::uint64_t now_for_wait_ms = GetTickCount64();
      const std::uint64_t wait_ms_u64 =
          *next_due_tick_ms > now_for_wait_ms ? (*next_due_tick_ms - now_for_wait_ms) : 0;
      clipboard_worker_cv_.wait_for(lock, std::chrono::milliseconds(wait_ms_u64));
    }
    if (shutting_down_) {
      break;
    }
    lock.unlock();
    RunClipboardCaptureLoop();
  }
}

void AppController::BackupWorkerLoop() {
  // Wait 10 seconds after startup before checking startup backup to avoid impacting startup time
  {
    std::unique_lock lock(mutex_);
    backup_worker_cv_.wait_for(lock, std::chrono::seconds(10), [this]() { return shutting_down_; });
    if (shutting_down_) {
      return;
    }
  }

  // Trigger startup backup check (non-periodic, skips if bak1 is less than 24 hours old)
  repository_.MaybeTriggerBackup(false);

  // Periodic backup cycle checking hourly, triggering backup every 4 hours
  int elapsed_hours = 0;
  while (true) {
    std::unique_lock lock(mutex_);
    backup_worker_cv_.wait_for(lock, std::chrono::hours(1), [this]() { return shutting_down_; });
    if (shutting_down_) {
      break;
    }

    elapsed_hours++;
    if (elapsed_hours >= 4) {
      elapsed_hours = 0;
      lock.unlock();
      repository_.MaybeTriggerBackup(true);
      lock.lock();
    }
  }
}

}  // namespace clipd2d::core::commands
