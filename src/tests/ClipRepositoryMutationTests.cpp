#include "tests/TestSupport.h"

#include <chrono>
#include <cstring>
#include <fstream>
#include <thread>

#include "common/FileHash.h"
#include "common/Paths.h"
#include "common/Utf8.h"
#include "core/clipboard/ClipboardCapture.h"
#include "core/qr/UrlQrGenerator.h"
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
using clipd2d::core::store::StorageTier;

std::vector<std::uint8_t> ReadFileBytes(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return std::vector<std::uint8_t>(
      std::istreambuf_iterator<char>(input),
      std::istreambuf_iterator<char>());
}

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

CapturedClip MakeEmptyTextClip() {
  CapturedClip clip;
  clip.content_type = ClipContentType::Text;
  clip.title = "Text";
  clip.content_hash = clipd2d::common::HashBytes("");
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

CapturedClip MakePendingImageClipFromPng(
    const AppPaths& paths,
    const std::vector<std::uint8_t>& png_bytes,
    std::string content_hash,
    int image_width,
    int image_height) {
  CapturedClip clip;
  clip.content_type = ClipContentType::Image;
  clip.title = "Image";
  clip.content_hash = std::move(content_hash);
  clip.storage_path = (std::filesystem::path(paths.image_dir) / (clipd2d::common::Utf8ToWide(clip.content_hash).wide + L".png")).wstring();
  clip.thumbnail_path = (std::filesystem::path(paths.image_dir) / (clipd2d::common::Utf8ToWide(clip.content_hash).wide + L"_thumb.png")).wstring();
  clip.preview_path = (std::filesystem::path(paths.image_dir) / (clipd2d::common::Utf8ToWide(clip.content_hash).wide + L"_preview.png")).wstring();
  clip.ingest_state = "pending";
  clip.ingest_format = "PNG";
  clip.ingest_bytes = png_bytes;
  clip.image_width = image_width;
  clip.image_height = image_height;
  clip.estimated_decoded_bytes = static_cast<std::int64_t>(image_width) * static_cast<std::int64_t>(image_height) * 4;
  return clip;
}

std::vector<std::uint8_t> MakeTopDown32BitDib(
    int width,
    int height,
    const std::vector<std::uint8_t>& bgra_pixels) {
  BITMAPINFOHEADER header{};
  header.biSize = sizeof(BITMAPINFOHEADER);
  header.biWidth = width;
  header.biHeight = -height;
  header.biPlanes = 1;
  header.biBitCount = 32;
  header.biCompression = BI_RGB;
  header.biSizeImage = static_cast<DWORD>(bgra_pixels.size());

  std::vector<std::uint8_t> dib(sizeof(BITMAPINFOHEADER) + bgra_pixels.size());
  std::memcpy(dib.data(), &header, sizeof(header));
  std::memcpy(dib.data() + sizeof(header), bgra_pixels.data(), bgra_pixels.size());
  return dib;
}

}  // namespace

CLIPD2D_TEST(ClipRepositoryRollsBackFailedInsertBeforeFormats) {
  const auto temp_dir = clipd2d::tests::CreateUniqueTempDirectory(L"cliprepo-insert");
  auto cleanup = [&]() { clipd2d::tests::DeleteDirectoryIfExists(temp_dir); };
  ClipRepository repo;
  CLIPD2D_CHECK(repo.Initialize(MakeTempAppPaths(temp_dir)).ok());
  ClipRepository::SetTestFailPoint(ClipRepository::TestFailPoint::AfterClipWriteBeforeFormats);
  bool inserted = false;
  const auto status = repo.InsertClip(MakeTextClip("rollback-text"), ReuseTouchPolicy::PromoteOnReuse, &inserted, nullptr);
  CLIPD2D_CHECK(!status.ok());
  ClipRepository::SetTestFailPoint(ClipRepository::TestFailPoint::None);
  std::int64_t count = -1;
  CLIPD2D_CHECK(repo.CountClips(ClipQuery{}, &count).ok());
  cleanup();
  CLIPD2D_CHECK_EQ(count, 0);
}

CLIPD2D_TEST(ClipRepositoryRejectsEmptyTextPayloadClip) {
  const auto temp_dir = clipd2d::tests::CreateUniqueTempDirectory(L"cliprepo-empty-text");
  auto cleanup = [&]() { clipd2d::tests::DeleteDirectoryIfExists(temp_dir); };
  ClipRepository repo;
  CLIPD2D_CHECK(repo.Initialize(MakeTempAppPaths(temp_dir)).ok());
  bool inserted = true;
  const auto status = repo.InsertClip(MakeEmptyTextClip(), ReuseTouchPolicy::PromoteOnReuse, &inserted, nullptr);
  CLIPD2D_CHECK(!status.ok());
  CLIPD2D_CHECK_EQ(status.code, clipd2d::common::StatusCode::InvalidArgument);
  cleanup();
}

CLIPD2D_TEST(ClipRepositoryRollsBackFailedFinalizePendingImage) {
  const auto temp_dir = clipd2d::tests::CreateUniqueTempDirectory(L"cliprepo-finalize");
  auto cleanup = [&]() { clipd2d::tests::DeleteDirectoryIfExists(temp_dir); };
  ClipRepository repo;
  const AppPaths paths = MakeTempAppPaths(temp_dir);
  CLIPD2D_CHECK(repo.Initialize(paths).ok());
  bool inserted = false;
  std::int64_t clip_id = 0;
  CLIPD2D_CHECK(repo.InsertClip(MakePendingImageClip(temp_dir / L"storage" / L"images"), ReuseTouchPolicy::PromoteOnReuse, &inserted, &clip_id).ok());
  ClipRepository::SetTestFailPoint(ClipRepository::TestFailPoint::BeforeFinalizePendingImageCommit);
  const auto status = repo.FinalizePendingImageClip(
      clip_id,
      "readyhash",
      (temp_dir / L"storage" / L"images" / L"ready.png").wstring(),
      (temp_dir / L"storage" / L"images" / L"ready_thumb.png").wstring(),
      (temp_dir / L"storage" / L"images" / L"ready_preview.png").wstring(),
      20,
      20);
  CLIPD2D_CHECK(!status.ok());
  ClipRepository::SetTestFailPoint(ClipRepository::TestFailPoint::None);
  ClipRecord clip;
  CLIPD2D_CHECK(repo.GetClipDetailsById(clip_id, &clip).ok());
  cleanup();
  CLIPD2D_CHECK_EQ(clip.ingest_state, std::string("pending"));
}

CLIPD2D_TEST(ClipRepositoryRollsBackFailedPromoteSnapshot) {
  const auto temp_dir = clipd2d::tests::CreateUniqueTempDirectory(L"cliprepo-promote");
  auto cleanup = [&]() { clipd2d::tests::DeleteDirectoryIfExists(temp_dir); };
  const std::filesystem::path source_file = temp_dir / L"source.png";
  {
    std::ofstream out(source_file, std::ios::binary | std::ios::trunc);
    out << "png-data";
  }
  ClipRepository repo;
  const AppPaths paths = MakeTempAppPaths(temp_dir);
  CLIPD2D_CHECK(repo.Initialize(paths).ok());
  bool inserted = false;
  std::int64_t clip_id = 0;
  const CapturedClip file_clip = MakeFileClip(source_file);
  CLIPD2D_CHECK(repo.InsertClip(file_clip, ReuseTouchPolicy::PromoteOnReuse, &inserted, &clip_id).ok());
  ClipRepository::SetTestFailPoint(ClipRepository::TestFailPoint::BeforePromoteSnapshotCommit);
  const auto status = repo.PromoteFileClipToSnapshot(clip_id);
  CLIPD2D_CHECK(!status.ok());
  ClipRepository::SetTestFailPoint(ClipRepository::TestFailPoint::None);
  ClipRecord clip;
  CLIPD2D_CHECK(repo.GetClipDetailsById(clip_id, &clip).ok());
  cleanup();
  CLIPD2D_CHECK_EQ(clip.plain_text, file_clip.plain_text);
}

CLIPD2D_TEST(ClipRepositoryMarkPastedCanKeepOriginalUpdatedAt) {
  const auto temp_dir = clipd2d::tests::CreateUniqueTempDirectory(L"cliprepo-mark-pasted-keep");
  auto cleanup = [&]() { clipd2d::tests::DeleteDirectoryIfExists(temp_dir); };
  ClipRepository repo;
  CLIPD2D_CHECK(repo.Initialize(MakeTempAppPaths(temp_dir)).ok());

  bool inserted = false;
  std::int64_t clip_id = 0;
  CLIPD2D_CHECK(repo.InsertClip(MakeTextClip("keep-position"), ReuseTouchPolicy::PromoteOnReuse, &inserted, &clip_id).ok());
  ClipRecord before;
  CLIPD2D_CHECK(repo.GetClipDetailsById(clip_id, &before).ok());
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  CLIPD2D_CHECK(repo.MarkPasted(clip_id, false).ok());
  ClipRecord after;
  CLIPD2D_CHECK(repo.GetClipDetailsById(clip_id, &after).ok());

  cleanup();
  CLIPD2D_CHECK_EQ(after.summary.updated_at, before.summary.updated_at);
  CLIPD2D_CHECK(after.summary.last_pasted_at >= before.summary.updated_at);
  CLIPD2D_CHECK_EQ(after.summary.paste_count, before.summary.paste_count + 1);
}

CLIPD2D_TEST(ClipRepositoryMarkPastedCanPromoteUpdatedAt) {
  const auto temp_dir = clipd2d::tests::CreateUniqueTempDirectory(L"cliprepo-mark-pasted-promote");
  auto cleanup = [&]() { clipd2d::tests::DeleteDirectoryIfExists(temp_dir); };
  ClipRepository repo;
  CLIPD2D_CHECK(repo.Initialize(MakeTempAppPaths(temp_dir)).ok());

  bool inserted = false;
  std::int64_t clip_id = 0;
  CLIPD2D_CHECK(repo.InsertClip(MakeTextClip("promote-position"), ReuseTouchPolicy::PromoteOnReuse, &inserted, &clip_id).ok());
  ClipRecord before;
  CLIPD2D_CHECK(repo.GetClipDetailsById(clip_id, &before).ok());
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
  CLIPD2D_CHECK(repo.MarkPasted(clip_id, true).ok());
  ClipRecord after;
  CLIPD2D_CHECK(repo.GetClipDetailsById(clip_id, &after).ok());

  cleanup();
  CLIPD2D_CHECK(after.summary.updated_at > before.summary.updated_at);
  CLIPD2D_CHECK_EQ(after.summary.updated_at, after.summary.last_pasted_at);
  CLIPD2D_CHECK_EQ(after.summary.paste_count, before.summary.paste_count + 1);
}

CLIPD2D_TEST(ClipRepositoryQueueImageOcrClearsPreviousOcrTextAndSearchIndex) {
  const auto temp_dir = clipd2d::tests::CreateUniqueTempDirectory(L"cliprepo-queue-ocr");
  auto cleanup = [&]() { clipd2d::tests::DeleteDirectoryIfExists(temp_dir); };
  ClipRepository repo;
  CLIPD2D_CHECK(repo.Initialize(MakeTempAppPaths(temp_dir)).ok());

  bool inserted = false;
  std::int64_t clip_id = 0;
  CLIPD2D_CHECK(repo.InsertClip(MakePendingImageClip(temp_dir / L"storage" / L"images"), ReuseTouchPolicy::PromoteOnReuse, &inserted, &clip_id).ok());
  CLIPD2D_CHECK(repo.FinalizeImageOcr(clip_id, "stale ocr text", "tesseract", "eng").ok());

  clipd2d::core::store::ClipQuery query;
  query.search_text = "stale";
  std::vector<clipd2d::core::store::ClipSummary> before_results;
  CLIPD2D_CHECK(repo.ListClips(query, 0, 10, &before_results).ok());
  CLIPD2D_CHECK_EQ(before_results.size(), static_cast<size_t>(1));

  CLIPD2D_CHECK(repo.QueueImageOcr(clip_id).ok());

  ClipRecord clip;
  CLIPD2D_CHECK(repo.GetClipDetailsById(clip_id, &clip).ok());
  std::vector<clipd2d::core::store::ClipSummary> after_results;
  CLIPD2D_CHECK(repo.ListClips(query, 0, 10, &after_results).ok());

  cleanup();
  CLIPD2D_CHECK_EQ(clip.ocr_text, std::string());
  CLIPD2D_CHECK_EQ(clip.ocr_state, std::string("pending"));
  CLIPD2D_CHECK_EQ(after_results.size(), static_cast<size_t>(0));
}

CLIPD2D_TEST(ClipRepositoryDedupeReuseKeepsOcrTextAndSearchIndex) {
  const auto temp_dir = clipd2d::tests::CreateUniqueTempDirectory(L"cliprepo-ocr-reuse");
  auto cleanup = [&]() { clipd2d::tests::DeleteDirectoryIfExists(temp_dir); };
  ClipRepository repo;
  CLIPD2D_CHECK(repo.Initialize(MakeTempAppPaths(temp_dir)).ok());

  bool inserted = false;
  std::int64_t clip_id = 0;
  CLIPD2D_CHECK(repo.InsertClip(MakePendingImageClip(temp_dir / L"storage" / L"images"), ReuseTouchPolicy::PromoteOnReuse, &inserted, &clip_id).ok());
  CLIPD2D_CHECK(repo.FinalizeImageOcr(clip_id, "seahorse ocr marker", "tesseract", "eng").ok());

  clipd2d::core::store::ClipQuery query;
  query.search_text = "seahorse";
  std::vector<clipd2d::core::store::ClipSummary> before_results;
  CLIPD2D_CHECK(repo.ListClips(query, 0, 10, &before_results).ok());
  CLIPD2D_CHECK_EQ(before_results.size(), static_cast<size_t>(1));

  // 复制同一内容 → 去重复用路径不得清掉已建的 OCR 全文索引
  std::int64_t reused_clip_id = 0;
  CLIPD2D_CHECK(repo.InsertClip(MakePendingImageClip(temp_dir / L"storage" / L"images"), ReuseTouchPolicy::PromoteOnReuse, &inserted, &reused_clip_id).ok());

  ClipRecord clip;
  CLIPD2D_CHECK(repo.GetClipDetailsById(reused_clip_id, &clip).ok());
  std::vector<clipd2d::core::store::ClipSummary> after_results;
  CLIPD2D_CHECK(repo.ListClips(query, 0, 10, &after_results).ok());

  cleanup();
  CLIPD2D_CHECK_EQ(reused_clip_id, clip_id);
  CLIPD2D_CHECK_EQ(clip.ocr_text, std::string("seahorse ocr marker"));
  CLIPD2D_CHECK_EQ(after_results.size(), static_cast<size_t>(1));
}

CLIPD2D_TEST(ClipboardImageCanonicalHashIncludesImageDimensions) {
  const std::vector<std::uint8_t> same_bgra_pixel_stream = {
      0x10, 0x20, 0x30, 0xFF,
      0x40, 0x50, 0x60, 0xFF,
      0x70, 0x80, 0x90, 0xFF,
      0xA0, 0xB0, 0xC0, 0xFF};

  const std::vector<std::uint8_t> one_by_four_dib =
      MakeTopDown32BitDib(1, 4, same_bgra_pixel_stream);
  const std::vector<std::uint8_t> two_by_two_dib =
      MakeTopDown32BitDib(2, 2, same_bgra_pixel_stream);

  std::string one_by_four_hash;
  int one_by_four_width = 0;
  int one_by_four_height = 0;
  CLIPD2D_CHECK(clipd2d::core::clipboard::ComputeCanonicalImageContentHashFromBytes(
      "CF_DIB",
      one_by_four_dib,
      &one_by_four_hash,
      &one_by_four_width,
      &one_by_four_height)
                    .ok());

  std::string two_by_two_hash;
  int two_by_two_width = 0;
  int two_by_two_height = 0;
  CLIPD2D_CHECK(clipd2d::core::clipboard::ComputeCanonicalImageContentHashFromBytes(
      "CF_DIB",
      two_by_two_dib,
      &two_by_two_hash,
      &two_by_two_width,
      &two_by_two_height)
                    .ok());

  CLIPD2D_CHECK_EQ(one_by_four_width, 1);
  CLIPD2D_CHECK_EQ(one_by_four_height, 4);
  CLIPD2D_CHECK_EQ(two_by_two_width, 2);
  CLIPD2D_CHECK_EQ(two_by_two_height, 2);
  CLIPD2D_CHECK(!one_by_four_hash.empty());
  CLIPD2D_CHECK(!two_by_two_hash.empty());
  CLIPD2D_CHECK(one_by_four_hash != two_by_two_hash);
}

CLIPD2D_TEST(ClipboardImageCanonicalHashMatchesNormalizedImageAndDedupesOnReinsert) {
  const auto temp_dir = clipd2d::tests::CreateUniqueTempDirectory(L"cliprepo-image-canonical");
  auto cleanup = [&]() { clipd2d::tests::DeleteDirectoryIfExists(temp_dir); };
  const AppPaths paths = MakeTempAppPaths(temp_dir);

  std::wstring qr_path;
  CLIPD2D_CHECK(clipd2d::core::qr::GenerateUrlQrPng(paths, L"https://example.com/canonical-image", &qr_path).ok());
  const auto png_bytes = ReadFileBytes(qr_path);
  CLIPD2D_CHECK(!png_bytes.empty());

  std::string canonical_hash;
  int capture_width = 0;
  int capture_height = 0;
  CLIPD2D_CHECK(clipd2d::core::clipboard::ComputeCanonicalImageContentHashFromBytes(
      "PNG",
      png_bytes,
      &canonical_hash,
      &capture_width,
      &capture_height)
                    .ok());
  CLIPD2D_CHECK(!canonical_hash.empty());
  CLIPD2D_CHECK_EQ(canonical_hash.size(), static_cast<size_t>(64));
  CLIPD2D_CHECK(capture_width > 0);
  CLIPD2D_CHECK(capture_height > 0);

  ClipRepository repo;
  CLIPD2D_CHECK(repo.Initialize(paths).ok());

  const CapturedClip first_clip =
      MakePendingImageClipFromPng(paths, png_bytes, canonical_hash, capture_width, capture_height);
  bool inserted = false;
  std::int64_t first_clip_id = 0;
  CLIPD2D_CHECK(repo.InsertClip(first_clip, ReuseTouchPolicy::PromoteOnReuse, &inserted, &first_clip_id).ok());

  std::string normalized_hash;
  int normalized_width = 0;
  int normalized_height = 0;
  CLIPD2D_CHECK(clipd2d::core::clipboard::NormalizePendingImage(
      paths,
      first_clip,
      &normalized_hash,
      &normalized_width,
      &normalized_height)
                    .ok());
  CLIPD2D_CHECK_EQ(normalized_hash, canonical_hash);
  CLIPD2D_CHECK_EQ(normalized_width, capture_width);
  CLIPD2D_CHECK_EQ(normalized_height, capture_height);
  CLIPD2D_CHECK(repo.FinalizePendingImageClip(
      first_clip_id,
      normalized_hash,
      first_clip.storage_path,
      first_clip.thumbnail_path,
      first_clip.preview_path,
      normalized_width,
      normalized_height)
                    .ok());

  const CapturedClip second_clip =
      MakePendingImageClipFromPng(paths, png_bytes, canonical_hash, capture_width, capture_height);
  std::int64_t second_clip_id = 0;
  CLIPD2D_CHECK(repo.InsertClip(second_clip, ReuseTouchPolicy::PromoteOnReuse, &inserted, &second_clip_id).ok());

  std::int64_t count = 0;
  CLIPD2D_CHECK(repo.CountClips(ClipQuery{}, &count).ok());

  cleanup();
  CLIPD2D_CHECK_EQ(second_clip_id, first_clip_id);
  CLIPD2D_CHECK_EQ(count, static_cast<std::int64_t>(1));
}

CLIPD2D_TEST(ClipRepositoryStorageTierIsExplicitNotDerivedFromFavoriteOrTags) {
  const auto temp_dir = clipd2d::tests::CreateUniqueTempDirectory(L"cliprepo-storage-tier-explicit");
  auto cleanup = [&]() { clipd2d::tests::DeleteDirectoryIfExists(temp_dir); };
  ClipRepository repo;
  CLIPD2D_CHECK(repo.Initialize(MakeTempAppPaths(temp_dir)).ok());

  bool inserted = false;
  std::int64_t clip_id = 0;
  CLIPD2D_CHECK(repo.InsertClip(MakeTextClip("library-explicit"), ReuseTouchPolicy::PromoteOnReuse, &inserted, &clip_id).ok());

  ClipRecord clip;
  CLIPD2D_CHECK(repo.GetClipDetailsById(clip_id, &clip).ok());
  CLIPD2D_CHECK_EQ(clip.summary.storage_tier, StorageTier::History);

  CLIPD2D_CHECK(repo.SetFavorite(clip_id, true).ok());
  CLIPD2D_CHECK(repo.GetClipDetailsById(clip_id, &clip).ok());
  CLIPD2D_CHECK_EQ(clip.summary.storage_tier, StorageTier::History);

  std::int64_t tag_id = 0;
  CLIPD2D_CHECK(repo.EnsureTag(L"Work", L"#4455FF", &tag_id).ok());
  CLIPD2D_CHECK(repo.AddTag(clip_id, tag_id).ok());
  CLIPD2D_CHECK(repo.GetClipDetailsById(clip_id, &clip).ok());
  CLIPD2D_CHECK_EQ(clip.summary.storage_tier, StorageTier::History);

  CLIPD2D_CHECK(repo.SetStorageTier(clip_id, StorageTier::Library).ok());
  CLIPD2D_CHECK(repo.GetClipDetailsById(clip_id, &clip).ok());
  CLIPD2D_CHECK_EQ(clip.summary.storage_tier, StorageTier::Library);

  cleanup();
}

CLIPD2D_TEST(ClipRepositoryGroupAssignmentPromotesLibraryAndHistoryClearsGroup) {
  const auto temp_dir = clipd2d::tests::CreateUniqueTempDirectory(L"cliprepo-storage-tier-group");
  auto cleanup = [&]() { clipd2d::tests::DeleteDirectoryIfExists(temp_dir); };
  ClipRepository repo;
  CLIPD2D_CHECK(repo.Initialize(MakeTempAppPaths(temp_dir)).ok());

  bool inserted = false;
  std::int64_t clip_id = 0;
  CLIPD2D_CHECK(repo.InsertClip(MakeTextClip("grouped"), ReuseTouchPolicy::PromoteOnReuse, &inserted, &clip_id).ok());

  std::int64_t group_id = 0;
  CLIPD2D_CHECK(repo.EnsureGroup(L"Work", &group_id).ok());
  CLIPD2D_CHECK(repo.SetGroup(clip_id, group_id).ok());

  ClipRecord clip;
  CLIPD2D_CHECK(repo.GetClipDetailsById(clip_id, &clip).ok());
  CLIPD2D_CHECK_EQ(clip.summary.group_id, group_id);
  CLIPD2D_CHECK_EQ(clip.summary.storage_tier, StorageTier::Library);

  CLIPD2D_CHECK(repo.SetStorageTier(clip_id, StorageTier::History).ok());
  CLIPD2D_CHECK(repo.GetClipDetailsById(clip_id, &clip).ok());
  CLIPD2D_CHECK_EQ(clip.summary.storage_tier, StorageTier::History);
  CLIPD2D_CHECK_EQ(clip.summary.group_id, 0);

  cleanup();
}

CLIPD2D_TEST(ClipRepositoryRenameAndDeleteGroupManageAssignments) {
  const auto temp_dir = clipd2d::tests::CreateUniqueTempDirectory(L"cliprepo-group-manage");
  auto cleanup = [&]() { clipd2d::tests::DeleteDirectoryIfExists(temp_dir); };
  ClipRepository repo;
  CLIPD2D_CHECK(repo.Initialize(MakeTempAppPaths(temp_dir)).ok());

  bool inserted = false;
  std::int64_t clip_id = 0;
  std::int64_t group_id = 0;
  CLIPD2D_CHECK(repo.InsertClip(MakeTextClip("manage-group"), ReuseTouchPolicy::PromoteOnReuse, &inserted, &clip_id).ok());
  CLIPD2D_CHECK(repo.EnsureGroup(L"Work", &group_id).ok());
  CLIPD2D_CHECK(repo.SetGroup(clip_id, group_id).ok());
  CLIPD2D_CHECK(repo.RenameGroup(group_id, L"Project A").ok());

  std::vector<clipd2d::core::store::GroupRecord> groups;
  CLIPD2D_CHECK(repo.ListGroups(&groups).ok());
  CLIPD2D_CHECK(groups.size() == 1);
  CLIPD2D_CHECK_EQ(groups.front().name, std::wstring(L"Project A"));

  CLIPD2D_CHECK(repo.DeleteGroup(group_id).ok());
  groups.clear();
  CLIPD2D_CHECK(repo.ListGroups(&groups).ok());

  ClipRecord clip;
  CLIPD2D_CHECK(repo.GetClipDetailsById(clip_id, &clip).ok());

  cleanup();
  CLIPD2D_CHECK(groups.empty());
  CLIPD2D_CHECK_EQ(clip.summary.group_id, 0);
  CLIPD2D_CHECK_EQ(clip.summary.storage_tier, StorageTier::Library);
}

CLIPD2D_TEST(ClipRepositoryGroupOrderAndTabPinStatePersist) {
  const auto temp_dir = clipd2d::tests::CreateUniqueTempDirectory(L"cliprepo-group-order");
  auto cleanup = [&]() { clipd2d::tests::DeleteDirectoryIfExists(temp_dir); };
  ClipRepository repo;
  CLIPD2D_CHECK(repo.Initialize(MakeTempAppPaths(temp_dir)).ok());

  std::int64_t alpha_id = 0;
  std::int64_t beta_id = 0;
  std::int64_t gamma_id = 0;
  CLIPD2D_CHECK(repo.EnsureGroup(L"Alpha", &alpha_id).ok());
  CLIPD2D_CHECK(repo.EnsureGroup(L"Beta", &beta_id).ok());
  CLIPD2D_CHECK(repo.EnsureGroup(L"Gamma", &gamma_id).ok());

  CLIPD2D_CHECK(repo.ReorderGroup(gamma_id, 10).ok());
  CLIPD2D_CHECK(repo.ReorderGroup(alpha_id, 20).ok());
  CLIPD2D_CHECK(repo.ReorderGroup(beta_id, 30).ok());
  CLIPD2D_CHECK(repo.SetGroupTabPinned(beta_id, true, 20).ok());
  CLIPD2D_CHECK(repo.ReorderGroupTab(beta_id, 10).ok());
  CLIPD2D_CHECK(repo.SetGroupTabPinned(alpha_id, true, 20).ok());

  std::vector<clipd2d::core::store::GroupRecord> groups;
  CLIPD2D_CHECK(repo.ListGroups(&groups).ok());
  CLIPD2D_CHECK_EQ(groups.size(), static_cast<size_t>(3));
  CLIPD2D_CHECK_EQ(groups[0].id, gamma_id);
  CLIPD2D_CHECK_EQ(groups[1].id, alpha_id);
  CLIPD2D_CHECK_EQ(groups[2].id, beta_id);
  CLIPD2D_CHECK(!groups[0].tab_pinned);
  CLIPD2D_CHECK(groups[1].tab_pinned);
  CLIPD2D_CHECK_EQ(groups[1].tab_sort_order, 20);
  CLIPD2D_CHECK(groups[2].tab_pinned);
  CLIPD2D_CHECK_EQ(groups[2].tab_sort_order, 10);

  cleanup();
}

CLIPD2D_TEST(ClipRepositorySelfHealingOnCorruption) {
  const auto temp_dir = clipd2d::tests::CreateUniqueTempDirectory(L"cliprepo-self-healing");
  auto cleanup = [&]() { clipd2d::tests::DeleteDirectoryIfExists(temp_dir); };
  
  AppPaths paths = MakeTempAppPaths(temp_dir);
  
  // 1. Initialize repository and insert some data
  {
    ClipRepository repo;
    CLIPD2D_CHECK(repo.Initialize(paths).ok());
    bool inserted = false;
    std::int64_t clip_id = 0;
    CLIPD2D_CHECK(repo.InsertClip(MakeTextClip("valuable-user-data"), ReuseTouchPolicy::PromoteOnReuse, &inserted, &clip_id).ok());
    
    // Trigger a backup manually
    CLIPD2D_CHECK(repo.MaybeTriggerBackup(true).ok()); // forced backup to bak1
  }
  
  // Verify backup exists
  std::wstring bak1 = paths.database_path + L".bak1";
  CLIPD2D_CHECK(std::filesystem::exists(bak1));
  
  // 2. Corrupt the database file
  {
    std::ofstream db_file(paths.database_path, std::ios::binary | std::ios::trunc);
    db_file << "This is definitely not a SQLite database file anymore! Corrupted bytes here.";
    db_file.close();
  }
  
  // 3. Re-initialize the repository
  {
    ClipRepository repo;
    CLIPD2D_CHECK(repo.Initialize(paths).ok()); // Should detect corruption, restore from backup, and succeed!
    
    // Check that our data is successfully recovered
    std::int64_t count = -1;
    CLIPD2D_CHECK(repo.CountClips(ClipQuery{}, &count).ok());
    CLIPD2D_CHECK_EQ(count, 1);
    
    ClipRecord clip;
    CLIPD2D_CHECK(repo.GetLatestClip(&clip).ok());
    CLIPD2D_CHECK_EQ(clip.plain_text, std::string("valuable-user-data"));
  }
  
  // 4. Verify corrupt database was archived
  bool corrupt_found = false;
  for (const auto& entry : std::filesystem::directory_iterator(temp_dir)) {
    if (entry.path().wstring().find(L".corrupt.") != std::wstring::npos) {
      corrupt_found = true;
      break;
    }
  }
  CLIPD2D_CHECK(corrupt_found);

  cleanup();
}
