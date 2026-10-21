#include "core/commands/AppController.Internal.h"
#include "core/policies/ImageIngestPolicy.h"

namespace clipd2d::core::commands {

void AppController::IngestWorkerLoop() {
  for (;;) {
    std::pair<std::int64_t, store::CapturedClip> job;
    {
      std::unique_lock lock(mutex_);
      ingest_worker_cv_.wait(lock, [this]() {
        return shutting_down_ || !pending_image_ingests_.empty();
      });
      if (shutting_down_ && pending_image_ingests_.empty()) {
        break;
      }
      job = std::move(pending_image_ingests_.front());
      pending_image_ingests_.pop_front();
    }

    const std::int64_t clip_id = job.first;
    store::CapturedClip clip = std::move(job.second);
    std::string content_hash;
    int image_width = clip.image_width;
    int image_height = clip.image_height;
    common::Status status = clipboard::NormalizePendingImage(paths_, clip, &content_hash, &image_width, &image_height);
    bool schedule_ocr = false;
    {
      std::scoped_lock lock(mutex_);
      if (!shutting_down_) {
        if (status.ok()) {
          const std::wstring preview_path = policies::ResolvePreviewPath(clip.storage_path, clip.is_large_image);
          status = repository_.FinalizePendingImageClip(
              clip_id,
              content_hash,
              clip.storage_path,
              clip.thumbnail_path,
              preview_path,
              image_width,
              image_height);
          schedule_ocr = status.ok();
        } else {
          repository_.MarkImageClipFailed(clip_id, status.message);
        }
      }
      active_image_ingests_.erase(clip_id);
    }
    if (schedule_ocr) {
      QueueImageOcrJob(clip_id, false, false);
    }
    NotifyRefreshFromWorker(MakePreserveViewportRefreshRequest(clip_id, RefreshSelectionPolicy::PreserveCurrent));
  }
}

void AppController::NotifyRefreshFromWorker(const RefreshRequest& request) {
  std::function<void(const RefreshRequest&)> dispatcher;
  {
    std::scoped_lock lock(mutex_);
    dispatcher = refresh_dispatcher_;
  }
  if (dispatcher) {
    dispatcher(request);
  }
}

void AppController::StartPendingImageIngest(std::int64_t clip_id, store::CapturedClip clip) {
  {
    std::scoped_lock lock(mutex_);
    if (shutting_down_ || active_image_ingests_.contains(clip_id)) {
      return;
    }
    active_image_ingests_.insert(clip_id);
    pending_image_ingests_.emplace_back(clip_id, std::move(clip));
  }
  ingest_worker_cv_.notify_one();
}

}  // namespace clipd2d::core::commands
