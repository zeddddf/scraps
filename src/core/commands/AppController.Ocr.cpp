#include "core/commands/AppController.Internal.h"

#include <filesystem>
#include <winrt/Windows.Foundation.h>

namespace clipd2d::core::commands {

namespace {

std::wstring ResolveOcrInputPath(const store::ClipRecord& clip) {
  if (clip.summary.content_type != store::ClipContentType::Image) {
    return {};
  }
  if (clip.is_large_image && !clip.preview_path.empty() && std::filesystem::exists(clip.preview_path)) {
    return clip.preview_path;
  }
  if (!clip.storage_path.empty() && std::filesystem::exists(clip.storage_path)) {
    return clip.storage_path;
  }
  if (!clip.preview_path.empty() && std::filesystem::exists(clip.preview_path)) {
    return clip.preview_path;
  }
  return {};
}

}  // namespace

common::Status AppController::QueueImageOcrJob(std::int64_t id, bool force_rerun, bool allow_when_disabled) {
  std::scoped_lock lock(mutex_);
  if (shutting_down_) {
    return common::Status::Error(common::StatusCode::Unavailable, "OCR worker is shutting down.");
  }
  if (!allow_when_disabled && !settings_.image_ocr_enabled) {
    return common::Status::Ok();
  }
  store::ClipRecord clip;
  common::Status status = repository_.GetClipById(id, &clip);
  if (!status.ok()) {
    return status;
  }
  if (clip.summary.content_type != store::ClipContentType::Image) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "OCR only supports image clips.");
  }
  if (!force_rerun && (clip.ocr_state == "pending" || clip.ocr_state == "ready")) {
    return common::Status::Ok();
  }
  if (active_ocr_jobs_.contains(id)) {
    return common::Status::Ok();
  }

  status = repository_.QueueImageOcr(id);
  if (!status.ok()) {
    return status;
  }
  active_ocr_jobs_.insert(id);
  pending_ocr_jobs_.push_back(PendingOcrJob{id, force_rerun});
  ocr_worker_cv_.notify_one();
  return common::Status::Ok();
}

common::Status AppController::RequestImageOcr(std::int64_t id, bool force_rerun) {
  return QueueImageOcrJob(id, force_rerun, force_rerun);
}

common::Status AppController::GetImageOcrProviderStatus(std::wstring* message) const {
  std::scoped_lock lock(mutex_);
  if (message != nullptr) {
    *message = L"Windows OCR";
  }
  return common::Status::Ok();
}

common::Status AppController::CopyOcrTextById(std::int64_t id) {
  std::scoped_lock lock(mutex_);
  store::ClipRecord clip;
  common::Status status = repository_.GetClipById(id, &clip);
  if (!status.ok()) {
    return status;
  }
  if (clip.ocr_text.empty()) {
    return common::Status::Error(common::StatusCode::NotFound, "OCR text is empty.");
  }
  SuppressClipboardMonitor();
  status = clipboard::SetClipboardText(clip.ocr_text, {}, {});
  if (!status.ok()) {
    return status;
  }
  pending_arrival_reason_ = store::ClipArrivalReason::InternalClipboardWriteback;
  pending_arrival_sequence_ = GetClipboardSequenceNumber();
  return common::Status::Ok();
}

common::Status AppController::PasteOcrTextById(
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
    if (clip.ocr_text.empty()) {
      return common::Status::Error(common::StatusCode::NotFound, "OCR text is empty.");
    }
  }
  return ExecutePasteTransaction(clip, &target_context, nullptr, route, clip.ocr_text, true);
}

void AppController::OcrWorkerLoop() {
  winrt::init_apartment(winrt::apartment_type::multi_threaded);
  for (;;) {
    PendingOcrJob job;
    store::AppSettings settings_snapshot;
    {
      std::unique_lock lock(mutex_);
      ocr_worker_cv_.wait(lock, [this]() {
        return shutting_down_ || !pending_ocr_jobs_.empty();
      });
      if (shutting_down_ && pending_ocr_jobs_.empty()) {
        break;
      }
      job = pending_ocr_jobs_.front();
      pending_ocr_jobs_.pop_front();
      settings_snapshot = settings_;
    }

    store::ClipRecord clip;
    common::Status status;
    {
      std::scoped_lock lock(mutex_);
      if (shutting_down_) {
        active_ocr_jobs_.erase(job.clip_id);
        continue;
      }
      status = repository_.GetClipById(job.clip_id, &clip);
    }
    if (!status.ok()) {
      std::scoped_lock lock(mutex_);
      active_ocr_jobs_.erase(job.clip_id);
      continue;
    }

    const std::wstring input_path = ResolveOcrInputPath(clip);
    if (input_path.empty()) {
      status = common::Status::Error(common::StatusCode::Unavailable, "OCR input image is unavailable.");
    } else {
      auto provider = core::ocr::CreateWindowsOcrProvider();
      core::ocr::ImageOcrResult result;
      status = provider->Recognize(
          core::ocr::ImageOcrRequest{
              .image_path = input_path,
              .language_hint = settings_snapshot.ocr_language_hint,
          },
          &result);
      if (status.ok()) {
        std::scoped_lock lock(mutex_);
        if (!shutting_down_) {
          status = repository_.FinalizeImageOcr(job.clip_id, result.utf8_text, result.engine_name, result.language_used);
        }
      }
    }

    {
      std::scoped_lock lock(mutex_);
      if (!status.ok() && !shutting_down_) {
        repository_.MarkImageOcrFailed(job.clip_id, status.message);
      }
      active_ocr_jobs_.erase(job.clip_id);
    }
    NotifyRefreshFromWorker(MakePreserveViewportRefreshRequest(job.clip_id, RefreshSelectionPolicy::PreserveCurrent));
  }
}

}  // namespace clipd2d::core::commands
