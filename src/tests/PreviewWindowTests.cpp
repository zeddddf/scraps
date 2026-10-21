#include "tests/TestSupport.h"

#include <fstream>

#include "common/Utf8.h"
#include "ui/PreviewWindow.h"

namespace {

using clipd2d::core::store::ClipContentType;
using clipd2d::core::store::ClipRecord;

ClipRecord MakeImageClip(const std::filesystem::path& storage_path, const std::filesystem::path& preview_path) {
  ClipRecord clip;
  clip.summary.content_type = ClipContentType::Image;
  clip.storage_path = storage_path.wstring();
  clip.preview_path = preview_path.wstring();
  clip.summary.storage_path = clip.storage_path;
  clip.summary.preview_path = clip.preview_path;
  return clip;
}

void WriteBinaryFile(const std::filesystem::path& path, std::string_view bytes) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

}  // namespace

namespace clipd2d::tests {

CLIPD2D_TEST(PreviewWindowResolvePreviewImagePathFallsBackToStorageWhenPreviewMissing) {
  const auto temp_dir = CreateUniqueTempDirectory(L"preview-path-fallback");
  auto cleanup = [&]() { DeleteDirectoryIfExists(temp_dir); };
  const auto storage_path = temp_dir / L"image.png";
  const auto preview_path = temp_dir / L"image_preview.png";
  WriteBinaryFile(storage_path, "image-bytes");

  const ClipRecord clip = MakeImageClip(storage_path, preview_path);
  const std::wstring resolved = clipd2d::ui::detail::ResolvePreviewImagePath(clip);

  cleanup();
  CLIPD2D_CHECK_EQ(resolved, storage_path.wstring());
}

CLIPD2D_TEST(PreviewWindowResolveExternalImageOpenPathPrefersStorageFile) {
  const auto temp_dir = CreateUniqueTempDirectory(L"preview-open-path");
  auto cleanup = [&]() { DeleteDirectoryIfExists(temp_dir); };
  const auto storage_path = temp_dir / L"image.png";
  const auto preview_path = temp_dir / L"image_preview.png";
  WriteBinaryFile(storage_path, "storage-bytes");
  WriteBinaryFile(preview_path, "preview-bytes");

  const ClipRecord clip = MakeImageClip(storage_path, preview_path);
  const std::wstring resolved = clipd2d::ui::detail::ResolveExternalImageOpenPath(clip);

  cleanup();
  CLIPD2D_CHECK_EQ(resolved, storage_path.wstring());
}

}  // namespace clipd2d::tests
