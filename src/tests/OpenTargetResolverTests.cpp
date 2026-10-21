#include "core/clipboard/OpenTargetResolver.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

#include "common/Utf8.h"
#include "tests/TestSupport.h"

namespace clipd2d::tests {

namespace {

std::wstring ToFileUri(const std::filesystem::path& path) {
  std::wstring normalized = path.wstring();
  std::replace(normalized.begin(), normalized.end(), L'\\', L'/');
  return L"file:///" + normalized;
}

}  // namespace

CLIPD2D_TEST(OpenTargetResolverRecognizesTrimmedUrl) {
  const auto target =
      clipd2d::core::clipboard::ResolveOpenTargetFromText("\r\n  https://example.com/a?b=1  \n");
  CLIPD2D_CHECK_EQ(target.kind, clipd2d::core::clipboard::OpenTargetKind::Url);
  CLIPD2D_CHECK_EQ(target.value, L"https://example.com/a?b=1");
}

CLIPD2D_TEST(OpenTargetResolverStripsOuterQuotes) {
  const auto target =
      clipd2d::core::clipboard::ResolveOpenTargetFromText("\"https://example.com/path\"");
  CLIPD2D_CHECK_EQ(target.kind, clipd2d::core::clipboard::OpenTargetKind::Url);
  CLIPD2D_CHECK_EQ(target.value, L"https://example.com/path");
}

CLIPD2D_TEST(OpenTargetResolverRecognizesExistingFilePath) {
  const std::filesystem::path temp_dir = CreateUniqueTempDirectory(L"open-target-file");
  const std::filesystem::path file_path = temp_dir / L"sample.txt";
  std::ofstream output(file_path);
  output << "sample";
  output.close();

  const std::string utf8_path = clipd2d::common::WideToUtf8(file_path.wstring()).utf8;
  const auto target = clipd2d::core::clipboard::ResolveOpenTargetFromText(utf8_path);
  CLIPD2D_CHECK_EQ(target.kind, clipd2d::core::clipboard::OpenTargetKind::File);
  CLIPD2D_CHECK(!target.value.empty());

  DeleteDirectoryIfExists(temp_dir);
}

CLIPD2D_TEST(OpenTargetResolverRecognizesExistingDirectoryPath) {
  const std::filesystem::path temp_dir = CreateUniqueTempDirectory(L"open-target-dir");

  const std::string utf8_path = clipd2d::common::WideToUtf8(temp_dir.wstring()).utf8;
  const auto target = clipd2d::core::clipboard::ResolveOpenTargetFromText(utf8_path);
  CLIPD2D_CHECK_EQ(target.kind, clipd2d::core::clipboard::OpenTargetKind::Directory);
  CLIPD2D_CHECK(!target.value.empty());

  DeleteDirectoryIfExists(temp_dir);
}

CLIPD2D_TEST(OpenTargetResolverRecognizesFileUri) {
  const std::filesystem::path temp_dir = CreateUniqueTempDirectory(L"open-target-uri");
  const std::string utf8_uri = clipd2d::common::WideToUtf8(ToFileUri(temp_dir)).utf8;

  const auto target = clipd2d::core::clipboard::ResolveOpenTargetFromText(utf8_uri);
  CLIPD2D_CHECK_EQ(target.kind, clipd2d::core::clipboard::OpenTargetKind::Directory);
  CLIPD2D_CHECK(!target.value.empty());

  DeleteDirectoryIfExists(temp_dir);
}

CLIPD2D_TEST(OpenTargetResolverRejectsBareDomain) {
  const auto target = clipd2d::core::clipboard::ResolveOpenTargetFromText("example.com");
  CLIPD2D_CHECK_EQ(target.kind, clipd2d::core::clipboard::OpenTargetKind::None);
}

CLIPD2D_TEST(OpenTargetResolverRejectsParagraphContainingUrl) {
  const auto target = clipd2d::core::clipboard::ResolveOpenTargetFromText("hello https://example.com world");
  CLIPD2D_CHECK_EQ(target.kind, clipd2d::core::clipboard::OpenTargetKind::None);
}

CLIPD2D_TEST(OpenTargetResolverRejectsMultipleTargets) {
  const auto target =
      clipd2d::core::clipboard::ResolveOpenTargetFromText("https://example.com\r\nhttps://example.org");
  CLIPD2D_CHECK_EQ(target.kind, clipd2d::core::clipboard::OpenTargetKind::None);
}

CLIPD2D_TEST(OpenTargetResolverRejectsMissingPath) {
  const auto target =
      clipd2d::core::clipboard::ResolveOpenTargetFromText("C:\\definitely-not-exist-12345\\missing.txt");
  CLIPD2D_CHECK_EQ(target.kind, clipd2d::core::clipboard::OpenTargetKind::None);
}

}  // namespace clipd2d::tests
