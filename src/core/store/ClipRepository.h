#pragma once

#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "common/Paths.h"
#include "common/Status.h"
#include "core/store/ClipTypes.h"

struct sqlite3;

namespace clipd2d::core::store {

class ClipRepository {
 public:
#if defined(SCRAPS_ENABLE_TEST_HOOKS)
  enum class TestFailPoint {
    None,
    AfterClipWriteBeforeFormats,
    BeforeFinalizePendingImageCommit,
    BeforePromoteSnapshotCommit,
  };
#endif

  ClipRepository() = default;
  ~ClipRepository();

  common::Status Initialize(const common::AppPaths& paths);
  void Shutdown();

  common::Status InsertClip(
      const CapturedClip& clip,
      ReuseTouchPolicy touch_policy,
      bool* inserted,
      std::int64_t* clip_id = nullptr);
  common::Status CountClips(const ClipQuery& query, std::int64_t* count) const;
  common::Status ListClips(
      const ClipQuery& query,
      int offset,
      int limit,
      std::vector<ClipSummary>* clips) const;
  common::Status GetClipDetailsById(std::int64_t id, ClipRecord* clip) const;
  common::Status GetClipById(std::int64_t id, ClipRecord* clip) const;
  common::Status GetClipFormatsById(std::int64_t id, std::vector<ClipFormatPayload>* formats) const;
  common::Status GetLatestClip(ClipRecord* clip) const;
  common::Status LoadBootstrapState(BootstrapState* state) const;
  common::Status HasAnyClips(bool* has_any_clips) const;
  common::Status HasAnySettingsData(bool* has_any_settings) const;
  common::Status FinalizePendingImageClip(
      std::int64_t id,
      const std::string& content_hash,
      const std::wstring& storage_path,
      const std::wstring& thumbnail_path,
      const std::wstring& preview_path,
      int image_width,
      int image_height) ;
  common::Status MarkImageClipFailed(std::int64_t id, const std::string& error_message);
  common::Status QueueImageOcr(std::int64_t id);
  common::Status FinalizeImageOcr(
      std::int64_t id,
      const std::string& ocr_text,
      const std::string& ocr_engine,
      const std::string& ocr_language);
  common::Status MarkImageOcrFailed(std::int64_t id, const std::string& error_message);
  common::Status PromoteFileClipToSnapshot(std::int64_t id);
  common::Status SetPinned(std::int64_t id, bool pinned);
  common::Status SetFavorite(std::int64_t id, bool favorite);
  common::Status SetStorageTier(std::int64_t id, StorageTier tier);
  common::Status SetGroup(std::int64_t id, std::int64_t group_id);
  common::Status AddTag(std::int64_t clip_id, std::int64_t tag_id);
  common::Status RemoveTag(std::int64_t clip_id, std::int64_t tag_id);
  common::Status EnsureTag(const std::wstring& name, const std::wstring& color, std::int64_t* tag_id);
  common::Status RenameTag(std::int64_t tag_id, const std::wstring& name, const std::wstring& color);
  common::Status DeleteTag(std::int64_t tag_id);
  common::Status ListTags(std::vector<TagRecord>* tags) const;
  common::Status ListClipTags(std::int64_t clip_id, std::vector<TagRecord>* tags) const;
  common::Status AddToQueue(std::int64_t clip_id, bool auto_pop);
  common::Status RemoveFromQueue(std::int64_t clip_id);
  common::Status ListQueue(std::vector<QueueEntry>* entries) const;
  common::Status ReorderQueue(std::int64_t clip_id, int new_sort_order);
  common::Status ReorderFavorites(std::int64_t clip_id, int new_sort_order);
  common::Status ReorderPinned(std::int64_t clip_id, int new_sort_order);
  common::Status ReorderTagItems(std::int64_t tag_id, std::int64_t clip_id, int new_sort_order);
  common::Status MoveQueueToFront(std::int64_t clip_id, bool auto_pop);
  common::Status MarkPasted(std::int64_t id, bool promote_on_paste);
  common::Status DeleteClip(std::int64_t id);
  common::Status ClearHistory(int keep_limit, int retention_days = 0);
  common::Status ClearHistoryArea();
  common::Status ExportClipToFile(std::int64_t id, const std::wstring& destination_path, std::wstring* exported_path);

  common::Status ListGroups(std::vector<GroupRecord>* groups) const;
  common::Status EnsureGroup(const std::wstring& name, std::int64_t* group_id);
  common::Status RenameGroup(std::int64_t group_id, const std::wstring& name);
  common::Status DeleteGroup(std::int64_t group_id);
  common::Status ReorderGroup(std::int64_t group_id, int new_sort_order);
  common::Status SetGroupTabPinned(std::int64_t group_id, bool pinned, int tab_sort_order);
  common::Status ReorderGroupTab(std::int64_t group_id, int new_tab_sort_order);

  common::Status LoadSettings(AppSettings* settings) const;
  common::Status SaveSettings(const AppSettings& settings);
  common::Status MaybeTriggerBackup(bool periodic);

#if defined(SCRAPS_ENABLE_TEST_HOOKS)
  static void SetTestFailPoint(TestFailPoint fail_point);
#endif

 private:
  common::Status OpenDatabase(const std::wstring& path);
  common::Status OpenAndCheckDatabase(const std::wstring& path);
  bool VerifyDatabaseIntegrity(const std::wstring& path) const;
  void BackupCorruptDatabase(const std::wstring& database_path);
  common::Status RestoreFromLatestBackup(bool* restored, std::wstring* corrupt_backup_path);
  common::Status BackupDatabase(const std::wstring& destination_path) const;
  common::Status CreateSchema();
  common::Status CreateFreshSchema();
  common::Status MigrateSchema(int from_version);
  common::Status MigrateSchemaFrom4To5();
  common::Status EnsureSeedData();
  common::Status ReadSchemaVersion(int* version) const;
  common::Status WriteSchemaVersion(int version) const;
  common::Status Execute(const char* sql) const;
  common::Status UpsertFts(
      std::int64_t clip_id,
      const std::string& title,
      const std::string& plain_text,
      const std::string& ocr_text = {});
  std::optional<std::int64_t> FindExistingByHash(const std::string& hash) const;
  common::Status UpdateStorageTierForClip(std::int64_t id);

  common::AppPaths app_paths_;
  sqlite3* db_ = nullptr;
};

}  // namespace clipd2d::core::store
