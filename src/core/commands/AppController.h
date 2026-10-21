#pragma once

#include <windows.h>

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_set>
#include <vector>

#include "common/Paths.h"
#include "common/Status.h"
#include "core/clipboard/ClipboardWriter.h"
#include "core/actions/ActionDescriptor.h"
#include "core/actions/ActionResolver.h"
#include "core/commands/RefreshRequest.h"
#include "core/ocr/ImageOcrProvider.h"
#include "core/paste/TextTransformEngine.h"
#include "core/qr/UrlQrGenerator.h"
#include "core/store/ClipRepository.h"
#include "core/store/SettingsRepository.h"

namespace clipd2d::core::commands {

class AppController {
 public:
  struct PendingClipboardEvent {
    DWORD sequence = 0;
    HWND source_window = nullptr;
    int deferred_attempt = 0;
    std::uint64_t not_before_tick_ms = 0;
  };

  AppController();
  ~AppController();

  common::Status Initialize(const common::AppPaths& paths);
  void Shutdown();

  common::Status OnClipboardChanged(HWND source_window, DWORD sequence, bool* inserted, std::int64_t* clip_id = nullptr);

  common::Status CountClips(const store::ClipQuery& query, std::int64_t* count);
  common::Status ListClips(
      const store::ClipQuery& query,
      int offset,
      int limit,
      std::vector<store::ClipSummary>* clips);
  common::Status GetClipDetailsById(std::int64_t id, store::ClipRecord* clip);
  common::Status GetClipById(std::int64_t id, store::ClipRecord* clip);
  common::Status GetLatestClip(store::ClipRecord* clip);
  common::Status LoadBootstrapState(store::BootstrapState* state);
  common::Status HasAnyClips(bool* has_any_clips);
  common::Status HasAnySettingsData(bool* has_any_settings);

  common::Status TogglePinned(std::int64_t id, bool pinned);
  common::Status ToggleFavorite(std::int64_t id, bool favorite);
  common::Status SetStorageTier(std::int64_t id, store::StorageTier tier);
  common::Status DeleteClip(std::int64_t id);
  common::Status EnsureTag(const std::wstring& name, const std::wstring& color, std::int64_t* tag_id);
  common::Status AddTag(std::int64_t id, std::int64_t tag_id);
  common::Status RemoveTag(std::int64_t id, std::int64_t tag_id);
  common::Status RenameTag(std::int64_t tag_id, const std::wstring& name, const std::wstring& color);
  common::Status DeleteTag(std::int64_t tag_id);
  common::Status ListTags(std::vector<store::TagRecord>* tags);
  common::Status ListClipTags(std::int64_t id, std::vector<store::TagRecord>* tags);
  common::Status AddToQueue(std::int64_t id);
  common::Status RemoveFromQueue(std::int64_t id);
  common::Status ListQueue(std::vector<store::QueueEntry>* entries);
  common::Status ReorderQueue(std::int64_t id, int new_sort_order);
  common::Status ReorderFavorites(std::int64_t id, int new_sort_order);
  common::Status ReorderPinned(std::int64_t id, int new_sort_order);
  common::Status ReorderTagItems(std::int64_t tag_id, std::int64_t id, int new_sort_order);
  common::Status MoveQueueToFront(std::int64_t id);
  common::Status ExportClipToFile(std::int64_t id, const std::wstring& destination_path, std::wstring* exported_path);
  common::Status GenerateQrCodeForUrl(std::wstring_view url, std::wstring* output_path);
  common::Status EnsureGroup(const std::wstring& name, std::int64_t* group_id);
  common::Status RenameGroup(std::int64_t group_id, const std::wstring& name);
  common::Status DeleteGroup(std::int64_t group_id);
  common::Status ReorderGroup(std::int64_t group_id, int new_sort_order);
  common::Status SetGroupTabPinned(std::int64_t group_id, bool pinned, int tab_sort_order);
  common::Status ReorderGroupTab(std::int64_t group_id, int new_tab_sort_order);
  common::Status SetGroup(std::int64_t id, std::int64_t group_id);
  common::Status MoveClipsToGroup(const std::vector<std::int64_t>& ids, std::int64_t group_id);
  common::Status MoveClipsToLibraryRoot(const std::vector<std::int64_t>& ids);
  common::Status MoveClipsToHistory(const std::vector<std::int64_t>& ids);
  common::Status ListGroups(std::vector<store::GroupRecord>* groups);

  common::Status LoadSettings(store::AppSettings* settings);
  common::Status SaveSettings(const store::AppSettings& settings);
  store::AppSettings current_settings() const;
  void SetRefreshDispatcher(std::function<void(const RefreshRequest&)> dispatcher);
  common::Status RunStartupMaintenance();

  common::Status PasteLatest(HWND target_window = nullptr);
  common::Status PasteLatest(const clipboard::PasteTargetContext& target_context, clipboard::PasteRoute route);
  common::Status CopyClipForStickerById(std::int64_t id);
  common::Status PasteById(std::int64_t id, HWND target_window = nullptr);
  common::Status PasteById(std::int64_t id, const clipboard::PasteTargetContext& target_context, clipboard::PasteRoute route);
  common::Status PastePlainTextById(std::int64_t id, HWND target_window = nullptr);
  common::Status PastePlainTextById(
      std::int64_t id,
      const clipboard::PasteTargetContext& target_context,
      clipboard::PasteRoute route);
  common::Status TransformPasteById(std::int64_t id, std::string_view action_id, HWND target_window = nullptr);
  common::Status TransformPasteById(
      std::int64_t id,
      std::string_view action_id,
      const clipboard::PasteTargetContext& target_context,
      clipboard::PasteRoute route);
  common::Status PasteNextInQueue(const clipboard::PasteTargetContext& target_context, clipboard::PasteRoute route);
  common::Status ClearHistory();
  common::Status RequestImageOcr(std::int64_t id, bool force_rerun);
  common::Status GetImageOcrProviderStatus(std::wstring* message) const;
  common::Status CopyOcrTextById(std::int64_t id);
  common::Status PasteOcrTextById(
      std::int64_t id,
      const clipboard::PasteTargetContext& target_context,
      clipboard::PasteRoute route);

  std::vector<actions::ActionDescriptor> ResolveActions(const store::ClipRecord& clip) const;
  std::string BuildSearchJson(const std::vector<store::ClipSummary>& clips) const;

 private:
  common::Status ExecutePasteTransaction(
      const store::ClipRecord& clip,
      const clipboard::PasteTargetContext* target_context,
      HWND target_window,
      clipboard::PasteRoute route,
      std::string_view override_text,
      bool use_override_text);
  bool IsSuppressedClipboardUpdate() const;
  void SuppressClipboardMonitor();
  bool IsBlockedByFilter(const store::CapturedClip& clip) const;
  void RunClipboardCaptureLoop();
  void ClipboardWorkerLoop();
  void IngestWorkerLoop();
  void OcrWorkerLoop();
  void BackupWorkerLoop();
  void StartPendingImageIngest(std::int64_t clip_id, store::CapturedClip clip);
  common::Status QueueImageOcrJob(std::int64_t clip_id, bool force_rerun, bool allow_when_disabled);
  void NotifyRefreshFromWorker(const RefreshRequest& request);
  store::ReuseTouchPolicy ConsumeClipboardTouchPolicy(DWORD sequence);
  std::optional<PendingClipboardEvent> TakeNextPendingClipboardEvent();

  mutable std::mutex mutex_;
  std::condition_variable clipboard_worker_cv_;
  std::condition_variable ingest_worker_cv_;
  std::condition_variable ocr_worker_cv_;
  std::condition_variable backup_worker_cv_;
  common::AppPaths paths_;
  store::ClipRepository repository_;
  store::SettingsRepository settings_repository_;
  store::AppSettings settings_;
  actions::ActionResolver action_resolver_;
  paste::TextTransformEngine text_transform_engine_;
  std::int64_t suppress_until_ms_ = 0;
  std::function<void(const RefreshRequest&)> refresh_dispatcher_;
  std::unordered_set<std::int64_t> active_image_ingests_;
  DWORD last_processed_clipboard_sequence_ = 0;
  DWORD in_flight_clipboard_sequence_ = 0;
  std::deque<PendingClipboardEvent> pending_clipboard_events_;
  bool clipboard_capture_in_flight_ = false;
  bool shutting_down_ = false;
  std::thread clipboard_worker_;
  std::thread ingest_worker_;
  std::thread ocr_worker_;
  std::thread backup_worker_;
  std::deque<std::pair<std::int64_t, store::CapturedClip>> pending_image_ingests_;
  struct PendingOcrJob {
    std::int64_t clip_id = 0;
    bool force_rerun = false;
  };
  std::deque<PendingOcrJob> pending_ocr_jobs_;
  std::unordered_set<std::int64_t> active_ocr_jobs_;
  std::optional<store::ClipArrivalReason> pending_arrival_reason_;
  DWORD pending_arrival_sequence_ = 0;
};

}  // namespace clipd2d::core::commands
