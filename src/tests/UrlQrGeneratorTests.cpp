#include "core/qr/UrlQrGenerator.h"

#include <filesystem>

#include "common/Paths.h"
#include "tests/TestSupport.h"

namespace {

using clipd2d::common::AppPaths;
using clipd2d::common::EnsureDirectory;

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

}  // namespace

namespace clipd2d::tests {

CLIPD2D_TEST(UrlQrGeneratorCreatesCachedPngForUrl) {
  const auto temp_dir = CreateUniqueTempDirectory(L"url-qr");
  auto cleanup = [&]() { DeleteDirectoryIfExists(temp_dir); };
  const AppPaths paths = MakeTempAppPaths(temp_dir);

  std::wstring output_path;
  const auto status = clipd2d::core::qr::GenerateUrlQrPng(
      paths,
      L"https://example.com/docs?id=1",
      &output_path);

  if (!status.ok()) {
    throw std::runtime_error("GenerateUrlQrPng failed: " + status.message);
  }
  CLIPD2D_CHECK(!output_path.empty());
  CLIPD2D_CHECK(std::filesystem::exists(output_path));
  CLIPD2D_CHECK_EQ(std::filesystem::path(output_path).extension().wstring(), std::wstring(L".png"));
  cleanup();
}

}  // namespace clipd2d::tests
