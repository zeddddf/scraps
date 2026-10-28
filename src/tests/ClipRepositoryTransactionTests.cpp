#include "tests/TestSupport.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

#include "common/FileHash.h"
#include "common/Paths.h"
#include "common/Utf8.h"
#include "core/store/ClipRepository.h"

namespace {

using clipd2d::common::AppPaths;
using clipd2d::common::EnsureDirectory;
using clipd2d::core::store::CapturedClip;
using clipd2d::core::store::ClipContentType;
using clipd2d::core::store::ClipQuery;
using clipd2d::core::store::ClipRecord;
using clipd2d::core::store::ClipRepository;
using clipd2d::core::store::ReuseTouchPolicy;

AppPaths MakeTempAppPaths(const std::filesystem::path& root) {
  AppPaths paths;
  paths.root_dir = root.wstring();
  paths.storage_dir = (root / L"storage").wstring();
  paths.image_dir = (root / L"storage" / L"images").wstring();
  paths.cache_dir = (root / L"cache").wstring();
  paths.database_path = (root / L"clipboard.db").wstring();
  EnsureDirectory(paths.root_dir);
  EnsureDirectory(paths.storage_dir);
  EnsureDirectory(paths.image_dir);
  EnsureDirectory(paths.cache_dir);
  return paths;
}

CapturedClip MakeTextClip(std::string text) {
  CapturedClip clip;
  clip.content_type = ClipContentType::Text;
  clip.title = text;
  clip.plain_text = std::move(text);
  clip.content_hash = clipd2d::common::HashBytes(clip.plain_text);
  return clip;
}

CapturedClip MakePendingImageClip(const std::filesystem::path& image_root) {
  CapturedClip clip;
  clip.content_type = ClipContentType::Image;
  clip.title = "Image";
  clip.content_hash = "pendinghash";
  clip.storage_path = (image_root / L"pending.png").wstring();
  clip.thumbnail_path = (image_root / L"pending_thumb.png").wstring();
  clip.preview_path = (image_root / L"pending_preview.png").wstring();
  clip.ingest_state = "pending";
  clip.ingest_format = "PNG";
  clip.image_width = 10;
  clip.image_height = 10;
  return clip;
}

CapturedClip MakeFileClip(const std::filesystem::path& file_path) {
  CapturedClip clip;
  clip.content_type = ClipContentType::Files;
  clip.title = file_path.filename().string();
  clip.plain_text = clipd2d::common::WideToUtf8(file_path.wstring()).utf8;
  clip.secondary_path = file_path.wstring();
  clip.content_hash = clipd2d::common::HashBytes(clip.plain_text);
  return clip;
}

std::int64_t CountClipsOrFail(const ClipRepository& repo) {
  std::int64_t count = -1;
  if (!repo.CountClips(ClipQuery{}, &count).ok()) {
    throw std::runtime_error("Check failed: CountClips");
  }
  return count;
}

}  // namespace

namespace clipd2d::tests {

// 前置：所有失败点测试都先在 failpoint == None 的正常路径下完成一次成功写入，
// 再挂失败点制造失败，最后断言库不处于半写状态且重试可成功。

CLIPD2D_TEST(ClipRepositoryInsertFailPointLeavesNoPartialClip) {
  const auto temp_dir = clipd2d::tests::CreateUniqueTempDirectory(L"cliprepo-tx-insert");
  auto cleanup = [&]() { clipd2d::tests::DeleteDirectoryIfExists(temp_dir); };
  const AppPaths paths = MakeTempAppPaths(temp_dir);

  ClipRepository repo;
  CLIPD2D_CHECK(repo.Initialize(paths).ok());

  // 前置正常路径（failpoint 为 None）。
  bool inserted = false;
  CLIPD2D_CHECK(repo.InsertClip(MakeTextClip("tx-seed"), ReuseTouchPolicy::PromoteOnReuse, &inserted, nullptr).ok());
  CLIPD2D_CHECK_EQ(CountClipsOrFail(repo), static_cast<std::int64_t>(1));

  // 失败点：clip 行已写入、formats 未写 → 事务必须整体回滚。
  ClipRepository::SetTestFailPoint(ClipRepository::TestFailPoint::AfterClipWriteBeforeFormats);
  const auto status = repo.InsertClip(MakeTextClip("tx-victim"), ReuseTouchPolicy::PromoteOnReuse, &inserted, nullptr);
  ClipRepository::SetTestFailPoint(ClipRepository::TestFailPoint::None);
  CLIPD2D_CHECK(!status.ok());
  CLIPD2D_CHECK_EQ(CountClipsOrFail(repo), static_cast<std::int64_t>(1));

  // 重新 Initialize 到同一库，正常路径重试可成功，计数一致。
  ClipRepository retry_repo;
  CLIPD2D_CHECK(retry_repo.Initialize(paths).ok());
  bool retry_inserted = false;
  std::int64_t retry_clip_id = 0;
  CLIPD2D_CHECK(retry_repo.InsertClip(MakeTextClip("tx-victim"), ReuseTouchPolicy::PromoteOnReuse, &retry_inserted, &retry_clip_id).ok());
  CLIPD2D_CHECK(retry_inserted);
  CLIPD2D_CHECK_EQ(CountClipsOrFail(retry_repo), static_cast<std::int64_t>(2));

  cleanup();
}

CLIPD2D_TEST(ClipRepositoryFinalizeFailPointKeepsPendingState) {
  const auto temp_dir = clipd2d::tests::CreateUniqueTempDirectory(L"cliprepo-tx-finalize");
  auto cleanup = [&]() { clipd2d::tests::DeleteDirectoryIfExists(temp_dir); };
  const AppPaths paths = MakeTempAppPaths(temp_dir);

  ClipRepository repo;
  CLIPD2D_CHECK(repo.Initialize(paths).ok());

  // 前置正常路径：先写入一条 pending 图片记录。
  bool inserted = false;
  std::int64_t clip_id = 0;
  CLIPD2D_CHECK(repo.InsertClip(
      MakePendingImageClip(temp_dir / L"storage" / L"images"),
      ReuseTouchPolicy::PromoteOnReuse,
      &inserted,
      &clip_id).ok());
  CLIPD2D_CHECK_EQ(CountClipsOrFail(repo), static_cast<std::int64_t>(1));

  // 失败点：finalize 的 UPDATE 已执行但事务未提交 → 必须整体回滚为 pending。
  ClipRepository::SetTestFailPoint(ClipRepository::TestFailPoint::BeforeFinalizePendingImageCommit);
  const auto status = repo.FinalizePendingImageClip(
      clip_id,
      "readyhash",
      (temp_dir / L"storage" / L"images" / L"ready.png").wstring(),
      (temp_dir / L"storage" / L"images" / L"ready_thumb.png").wstring(),
      (temp_dir / L"storage" / L"images" / L"ready_preview.png").wstring(),
      20,
      20);
  ClipRepository::SetTestFailPoint(ClipRepository::TestFailPoint::None);
  CLIPD2D_CHECK(!status.ok());

  ClipRecord after_fail;
  CLIPD2D_CHECK(repo.GetClipDetailsById(clip_id, &after_fail).ok());
  CLIPD2D_CHECK_EQ(after_fail.ingest_state, std::string("pending"));

  // 同库重试正常 finalize 可成功，状态推进为 ready。
  CLIPD2D_CHECK(repo.FinalizePendingImageClip(
      clip_id,
      "readyhash",
      (temp_dir / L"storage" / L"images" / L"ready.png").wstring(),
      (temp_dir / L"storage" / L"images" / L"ready_thumb.png").wstring(),
      (temp_dir / L"storage" / L"images" / L"ready_preview.png").wstring(),
      20,
      20).ok());
  ClipRecord after_retry;
  CLIPD2D_CHECK(repo.GetClipDetailsById(clip_id, &after_retry).ok());
  CLIPD2D_CHECK_EQ(after_retry.ingest_state, std::string("ready"));
  CLIPD2D_CHECK_EQ(
      after_retry.storage_path,
      (temp_dir / L"storage" / L"images" / L"ready.png").wstring());
  CLIPD2D_CHECK_EQ(CountClipsOrFail(repo), static_cast<std::int64_t>(1));

  cleanup();
}

CLIPD2D_TEST(ClipRepositoryPromoteSnapshotFailPointKeepsOriginalClip) {
  const auto temp_dir = clipd2d::tests::CreateUniqueTempDirectory(L"cliprepo-tx-promote");
  auto cleanup = [&]() { clipd2d::tests::DeleteDirectoryIfExists(temp_dir); };
  const std::filesystem::path source_file = temp_dir / L"source.png";
  {
    std::ofstream out(source_file, std::ios::binary | std::ios::trunc);
    out << "png-data";
  }
  const AppPaths paths = MakeTempAppPaths(temp_dir);

  ClipRepository repo;
  CLIPD2D_CHECK(repo.Initialize(paths).ok());

  // 前置正常路径：插入一条文件型 clip。
  bool inserted = false;
  std::int64_t clip_id = 0;
  const CapturedClip file_clip = MakeFileClip(source_file);
  CLIPD2D_CHECK(repo.InsertClip(file_clip, ReuseTouchPolicy::PromoteOnReuse, &inserted, &clip_id).ok());
  CLIPD2D_CHECK_EQ(CountClipsOrFail(repo), static_cast<std::int64_t>(1));

  // 失败点：promote 的 UPDATE/FTS 已执行但事务未提交 → 原记录必须原样保留。
  ClipRepository::SetTestFailPoint(ClipRepository::TestFailPoint::BeforePromoteSnapshotCommit);
  const auto status = repo.PromoteFileClipToSnapshot(clip_id);
  ClipRepository::SetTestFailPoint(ClipRepository::TestFailPoint::None);
  CLIPD2D_CHECK(!status.ok());

  ClipRecord after_fail;
  CLIPD2D_CHECK(repo.GetClipDetailsById(clip_id, &after_fail).ok());
  CLIPD2D_CHECK_EQ(after_fail.plain_text, file_clip.plain_text);

  // 重试 promote 可成功：plain_text 与 storage_path 同时指向受管快照副本。
  CLIPD2D_CHECK(repo.PromoteFileClipToSnapshot(clip_id).ok());
  ClipRecord after_retry;
  CLIPD2D_CHECK(repo.GetClipDetailsById(clip_id, &after_retry).ok());
  CLIPD2D_CHECK(!after_retry.storage_path.empty());
  CLIPD2D_CHECK_EQ(
      after_retry.plain_text,
      clipd2d::common::WideToUtf8(after_retry.storage_path).utf8);
  const std::wstring promoted_path = after_retry.storage_path;
  CLIPD2D_CHECK(promoted_path.find(paths.image_dir) == 0);
  CLIPD2D_CHECK_EQ(CountClipsOrFail(repo), static_cast<std::int64_t>(1));

  cleanup();
}

}  // namespace clipd2d::tests
