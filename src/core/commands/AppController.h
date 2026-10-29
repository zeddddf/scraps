#pragma once

#include <windows.h>

#include <condition_variable>
#include <atomic>
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
#include "core/commands/AppController.Interfaces.h"
#include "core/commands/PasteQueueModel.h"
#include "core/commands/RefreshRequest.h"
#include "core/ocr/ImageOcrProvider.h"
#include "core/paste/TextTransformEngine.h"
#include "core/qr/UrlQrGenerator.h"
#include "core/store/ClipRepository.h"
#include "core/store/SettingsRepository.h"

namespace clipd2d::core::commands {

// 上帝类，但对外只经 5 个窄接口暴露（AppController.Interfaces.h）。
// 进程装配面（Initialize/Shutdown/OnClipboardChanged/SetRefreshDispatcher）
// 不进接口，由 app 层直接调用。UI 消费方请 include 对应接口头，别拉这颗头。
class AppController : public IHistoryFeed,
                      public IPasteService,
                      public ILibraryMutator,
                      public ISettingsStore,
                      public IMaintenance {
 public:
  // 一次取齐 5 个窄接口（MainWindow 装配用）。
  AppServiceRefs service_refs() {
    return AppServiceRefs{this, this, this, this, this};
  }

  struct PendingClipboardEvent {
    DWORD sequence = 0;
    HWND source_window = nullptr;
    int deferred_attempt = 0;
    std::uint64_t not_before_tick_ms = 0;
    // 入队时按 [internal_writeback_baseline_sequence_, pending_arrival_sequence_]
    // 区间判定；随事件走，worker 消费时不再比对 sequence（写回期间 sequence
    // 会因 EmptyClipboard 与 CF_DIB 派生格式合成多次推进，相等判定必被击穿）。
    bool internal_writeback = false;
  };

  AppController();
  ~AppController();

  common::Status Initialize(const common::AppPaths& paths);
  void Shutdown();

  common::Status OnClipboardChanged(HWND source_window, DWORD sequence, bool* inserted, std::int64_t* clip_id = nullptr);

  // ---- IHistoryFeed ----
  common::Status CountClips(const store::ClipQuery& query, std::int64_t* count) override;
  common::Status ListClips(
      const store::ClipQuery& query,
      int offset,
      int limit,
      std::vector<store::ClipSummary>* clips) override;
  common::Status GetClipDetailsById(std::int64_t id, store::ClipRecord* clip) override;
  common::Status GetClipById(std::int64_t id, store::ClipRecord* clip) override;
  common::Status GetLatestClip(store::ClipRecord* clip) override;
  common::Status LoadBootstrapState(store::BootstrapState* state) override;
  common::Status HasAnyClips(bool* has_any_clips) override;
  common::Status HasAnySettingsData(bool* has_any_settings) override;
  common::Status ListTags(std::vector<store::TagRecord>* tags) override;
  common::Status ListClipTags(std::int64_t id, std::vector<store::TagRecord>* tags) override;
  common::Status ListQueue(std::vector<store::QueueEntry>* entries) override;
  common::Status ListGroups(std::vector<store::GroupRecord>* groups) override;
  std::vector<actions::ActionDescriptor> ResolveActions(const store::ClipRecord& clip) const override;
  std::string BuildSearchJson(const std::vector<store::ClipSummary>& clips) const override;

  // ---- ILibraryMutator ----
  common::Status TogglePinned(std::int64_t id, bool pinned) override;
  common::Status ToggleFavorite(std::int64_t id, bool favorite) override;
  common::Status SetStorageTier(std::int64_t id, store::StorageTier tier) override;
  common::Status DeleteClip(std::int64_t id) override;
  common::Status EnsureTag(const std::wstring& name, const std::wstring& color, std::int64_t* tag_id) override;
  common::Status AddTag(std::int64_t id, std::int64_t tag_id) override;
  common::Status RemoveTag(std::int64_t id, std::int64_t tag_id) override;
  common::Status RenameTag(std::int64_t tag_id, const std::wstring& name, const std::wstring& color) override;
  common::Status DeleteTag(std::int64_t tag_id) override;
  common::Status AddToQueue(std::int64_t id) override;
  common::Status RemoveFromQueue(std::int64_t id) override;
  common::Status ReorderQueue(std::int64_t id, int new_sort_order) override;
  common::Status ReorderFavorites(std::int64_t id, int new_sort_order) override;
  common::Status ReorderPinned(std::int64_t id, int new_sort_order) override;
  common::Status ReorderTagItems(std::int64_t tag_id, std::int64_t id, int new_sort_order) override;
  common::Status ReorderQueueBatch(const std::vector<store::ReorderEntry>& entries) override;
  common::Status ReorderFavoritesBatch(const std::vector<store::ReorderEntry>& entries) override;
  common::Status ReorderPinnedBatch(const std::vector<store::ReorderEntry>& entries) override;
  common::Status ReorderTagItemsBatch(std::int64_t tag_id, const std::vector<store::ReorderEntry>& entries) override;
  common::Status MoveQueueToFront(std::int64_t id) override;
  common::Status EnsureGroup(const std::wstring& name, std::int64_t* group_id) override;
  common::Status RenameGroup(std::int64_t group_id, const std::wstring& name) override;
  common::Status DeleteGroup(std::int64_t group_id) override;
  common::Status ReorderGroup(std::int64_t group_id, int new_sort_order) override;
  common::Status SetGroupTabPinned(std::int64_t group_id, bool pinned, int tab_sort_order) override;
  common::Status ReorderGroupTab(std::int64_t group_id, int new_tab_sort_order) override;
  common::Status SetGroup(std::int64_t id, std::int64_t group_id) override;
  common::Status MoveClipsToGroup(const std::vector<std::int64_t>& ids, std::int64_t group_id) override;
  common::Status MoveClipsToLibraryRoot(const std::vector<std::int64_t>& ids) override;
  common::Status MoveClipsToHistory(const std::vector<std::int64_t>& ids) override;
  common::Status ClearHistory() override;

  // ---- ISettingsStore ----
  common::Status LoadSettings(store::AppSettings* settings) override;
  common::Status SaveSettings(const store::AppSettings& settings) override;
  store::AppSettings current_settings() const override;

  // ---- IMaintenance ----
  common::Status RunStartupMaintenance() override;
  common::Status ExportClipToFile(std::int64_t id, const std::wstring& destination_path, std::wstring* exported_path) override;
  common::Status GenerateQrCodeForUrl(std::wstring_view url, std::wstring* output_path) override;
  common::Status RequestImageOcr(std::int64_t id, bool force_rerun) override;
  common::Status GetImageOcrProviderStatus(std::wstring* message) const override;

  // ---- IPasteService ----
  common::Status PasteLatest(HWND target_window = nullptr) override;
  common::Status PasteLatest(const clipboard::PasteTargetContext& target_context, clipboard::PasteRoute route) override;
  common::Status PasteById(std::int64_t id, HWND target_window = nullptr) override;
  common::Status PasteById(std::int64_t id, const clipboard::PasteTargetContext& target_context, clipboard::PasteRoute route) override;
  common::Status CopyClipForStickerById(std::int64_t id) override;
  common::Status PastePlainTextById(std::int64_t id, HWND target_window = nullptr) override;
  common::Status PastePlainTextById(
      std::int64_t id,
      const clipboard::PasteTargetContext& target_context,
      clipboard::PasteRoute route) override;
  common::Status TransformPasteById(std::int64_t id, std::string_view action_id, HWND target_window = nullptr) override;
  common::Status TransformPasteById(
      std::int64_t id,
      std::string_view action_id,
      const clipboard::PasteTargetContext& target_context,
      clipboard::PasteRoute route) override;
  common::Status PasteNextInQueue(const clipboard::PasteTargetContext& target_context, clipboard::PasteRoute route) override;
  common::Status CopyOcrTextById(std::int64_t id) override;
  common::Status PasteOcrTextById(
      std::int64_t id,
      const clipboard::PasteTargetContext& target_context,
      clipboard::PasteRoute route) override;

  // —— 异步粘贴（UI 路径专用）——
  // 粘贴事务含 WIC 解码、全部像素拷贝、前台恢复轮询与修饰键等待，单项可达数百毫秒。
  // 同步跑在 UI 线程会让面板隐藏后整窗无响应。这里的契约：
  //   * Queue* 只入队并立即返回（返回值仅表示是否入队成功，不代表粘贴结果）；
  //   * 事务在专用 paste_worker_ 线程**串行**执行，保证两次快速粘贴不会交错写剪贴板；
  //   * 执行结果（仅失败时非 Ok）经 SetPasteResultDispatcher 注册的回调回抛，
  //     由 UI 决定是否重新显示面板/提示。
  // 请求体 QueuedPasteRequest/QueuedPasteKind 已提到命名空间作用域（IPasteService.h）。
  using QueuedPasteKind = ::clipd2d::core::commands::QueuedPasteKind;
  using QueuedPasteRequest = ::clipd2d::core::commands::QueuedPasteRequest;
  bool QueuePaste(QueuedPasteRequest request) override;
  bool HasPendingPaste() const override;
  void SetPasteResultDispatcher(std::function<void(const common::Status&)> dispatcher) override;

  // 进程装配面（app 层专用，不进窄接口）。
  void SetRefreshDispatcher(std::function<void(const RefreshRequest&)> dispatcher);

 private:
  // P2-5:读路径统一入口。只读连接可用 → 零大锁（repository 内部 read_db_mutex_
  // 串行读，WAL 下与写事务并发）；不可用 → 退回大锁旧路径。仅限"纯读且不触碰
  // controller 状态"的操作；调用方不得已持有 mutex_（fallback 路径会重锁）。
  template <typename Fn>
  auto RunRepositoryRead(Fn&& fn) -> decltype(fn()) {
    if (repository_.HasReadConnection()) {
      return fn();
    }
    std::scoped_lock lock(mutex_);
    return fn();
  }

  common::Status ExecutePasteTransaction(
      const store::ClipRecord& clip,
      const clipboard::PasteTargetContext* target_context,
      HWND target_window,
      clipboard::PasteRoute route,
      std::string_view override_text,
      bool use_override_text);
  bool IsSuppressedClipboardUpdate() const;
  bool IsSuppressedClipboardUpdateLocked() const;
  void SuppressClipboardMonitor();
  bool IsBlockedByFilter(const store::CapturedClip& clip) const;
  void RunClipboardCaptureLoop();
  void ClipboardWorkerLoop();
  void PasteWorkerLoop();
  void IngestWorkerLoop();
  void OcrWorkerLoop();
  // OCR worker 启动时的一次性自愈：为缺失缩略预览的大图批量生成 1024 缩略
  // 版并回写 preview_path（历史大图原图解码可达数百毫秒，悬停预览/OCR 吃缩略版）。
  void BackfillLargeImagePreviewsOnWorker();
  void BackupWorkerLoop();
  void StartPendingImageIngest(std::int64_t clip_id, store::CapturedClip clip);
  common::Status QueueImageOcrJob(std::int64_t clip_id, bool force_rerun, bool allow_when_disabled);
  void NotifyRefreshFromWorker(const RefreshRequest& request);
  store::ReuseTouchPolicy ConsumeClipboardTouchPolicy(DWORD sequence, bool internal_writeback);
  std::optional<PendingClipboardEvent> TakeNextPendingClipboardEvent();
  // 刷新 settings_cache_。调用前必须已持有 mutex_（读 settings_）。
  void PublishSettingsCacheLocked();

  mutable std::mutex mutex_;
  // P2-5:剪贴板序列账本专用小锁。WM_CLIPBOARDUPDATE 在 UI 线程派发,
  // OnClipboardChanged 只碰下面这组字段——它们脱离 mutex_ 后,UI 线程的事件
  // 入队永远不排队等业务大锁(图片 ingest 写库/粘贴事务期间的 mutex_ 争用
  // 不再打断 UI)。锁序固定 mutex_ -> clipboard_gate_mutex_(worker 侧嵌套),
  // 任何路径不得反向。守卫字段:
  //   pending_clipboard_events_ / last_processed_clipboard_sequence_ /
  //   in_flight_clipboard_sequence_ / clipboard_capture_in_flight_ /
  //   suppress_until_ms_ / pending_arrival_reason_ / pending_arrival_sequence_ /
  //   internal_writeback_baseline_sequence_ /
  //   last_internal_writeback_identity_key_ / last_internal_writeback_tick_ms_
  mutable std::mutex clipboard_gate_mutex_;
  // 设置快照专用锁：settings_cache_ 是 settings_ 的只读副本，只由 SaveSettings /
  // Initialize 在持 mutex_ 时同步刷新。current_settings() 走这把锁而不再抢 mutex_，
  // 否则 UI 线程每帧两次取设置都会和粘贴 worker 的 SQLite 写入撞在一起
  // （实测 RenderFrame 的 draw 段因此涨到 22~48ms，整段都是等锁）。
  // 锁序固定为 mutex_ -> settings_cache_mutex_，反向没有任何路径，无死锁风险。
  mutable std::mutex settings_cache_mutex_;
  std::condition_variable clipboard_worker_cv_;
  std::condition_variable ingest_worker_cv_;
  std::condition_variable ocr_worker_cv_;
  std::condition_variable backup_worker_cv_;
  common::AppPaths paths_;
  store::ClipRepository repository_;
  store::SettingsRepository settings_repository_;
  store::AppSettings settings_;
  // settings_ 的只读快照，受 settings_cache_mutex_ 保护（见该成员的说明）。
  store::AppSettings settings_cache_;
  actions::ActionResolver action_resolver_;
  paste::TextTransformEngine text_transform_engine_;
  std::int64_t suppress_until_ms_ = 0;  // P2-5:clipboard_gate_mutex_
  std::function<void(const RefreshRequest&)> refresh_dispatcher_;
  std::unordered_set<std::int64_t> active_image_ingests_;
  DWORD last_processed_clipboard_sequence_ = 0;  // P2-5:clipboard_gate_mutex_
  DWORD in_flight_clipboard_sequence_ = 0;  // P2-5:clipboard_gate_mutex_
  std::deque<PendingClipboardEvent> pending_clipboard_events_;  // P2-5:clipboard_gate_mutex_
  bool clipboard_capture_in_flight_ = false;  // P2-5:clipboard_gate_mutex_
  std::atomic<bool> shutting_down_{false};
  // P2-6:上次保留期清理时刻(mutex_ 下,仅采集 worker 触碰)。节流 ClearHistory。
  std::uint64_t last_history_trim_tick_ms_ = 0;
  std::thread clipboard_worker_;
  std::thread paste_worker_;
  std::thread ingest_worker_;
  std::thread ocr_worker_;
  std::thread backup_worker_;
  std::deque<std::pair<std::int64_t, store::CapturedClip>> pending_image_ingests_;
  // 异步粘贴队列（串行执行）与失败回抛通道。
  // 队列纪律（入队闸门/FIFO/Close 排空/Clear）在 PasteQueueModel（业务核抽取计划 §4）；
  // 本壳只负责锁、线程等待唤醒与事务执行。
  std::condition_variable paste_worker_cv_;
  PasteQueueModel paste_queue_;
  std::function<void(const common::Status&)> paste_result_dispatcher_;
  struct PendingOcrJob {
    std::int64_t clip_id = 0;
    bool force_rerun = false;
  };
  std::deque<PendingOcrJob> pending_ocr_jobs_;
  std::unordered_set<std::int64_t> active_ocr_jobs_;
  std::optional<store::ClipArrivalReason> pending_arrival_reason_;  // P2-5:clipboard_gate_mutex_
  DWORD pending_arrival_sequence_ = 0;  // P2-5:clipboard_gate_mutex_
  // 内部写回区间判定（见 PendingClipboardEvent::internal_writeback 注释）。
  DWORD internal_writeback_baseline_sequence_ = 0;  // P2-5:clipboard_gate_mutex_
  // 回声吸收：内部写回后短时间内同 identity_key 的再捕获（第三方剪贴板增强
  // 接力改写剪贴板）按 KeepPositionOnReuse 处理，不提升位置。
  std::string last_internal_writeback_identity_key_;  // P2-5:clipboard_gate_mutex_
  std::uint64_t last_internal_writeback_tick_ms_ = 0;  // P2-5:clipboard_gate_mutex_
  static constexpr std::uint64_t kInternalWritebackEchoWindowMs = 5000;
};

}  // namespace clipd2d::core::commands
