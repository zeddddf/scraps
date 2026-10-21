#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <windows.h>

#include "common/Status.h"
#include "core/store/ClipTypes.h"

namespace clipd2d::ui::visual_assets {

enum class ShellIconPurpose {
  None = -1,
  SourceAppBadge = 0,
  FilePrimary,
};

struct ShellIconRequest {
  std::wstring path;
  ShellIconPurpose purpose = ShellIconPurpose::FilePrimary;
  int display_extent = 16;
  UINT dpi = 96;
  bool path_may_not_exist = false;
  bool is_directory_hint = false;
};

struct ShellIconPixels {
  std::wstring cache_key;
  UINT width = 0;
  UINT height = 0;
  std::vector<std::uint8_t> pixels;
};

enum class ThumbnailLoadKind {
  None = 0,
  ScaledBitmap,
  SystemThumbnail,
};

struct ThumbnailLoadRequest {
  ThumbnailLoadKind kind = ThumbnailLoadKind::None;
  std::wstring key;
  int max_width = 0;
  int max_height = 0;
  bool allow_fallback_decode = false;
};

bool IsImageFilePath(const std::wstring& path);
bool SupportsSystemThumbnailPreviewPath(const std::wstring& path);
std::uint64_t TryFileSize(const std::wstring& path);
std::wstring FirstFilePathFromSummary(const core::store::ClipSummary& summary);
std::wstring ThumbnailKeyFromSummary(const core::store::ClipSummary& summary);
ShellIconRequest BuildSourceAppIconRequest(
    const std::wstring& path,
    int displayed_extent,
    UINT dpi = 96);
ShellIconRequest BuildFileIconRequest(
    const std::wstring& path,
    int displayed_extent,
    UINT dpi = 96,
    bool path_may_not_exist = false,
    bool is_directory_hint = false);
int ResolveShellIconDecodeExtent(
    ShellIconPurpose purpose,
    int displayed_extent,
    UINT dpi = 96);
std::wstring BuildShellIconCacheKey(const ShellIconRequest& request);
int ResolveFileThumbnailDecodeExtent(int displayed_extent);
int ResolveFileIconDecodeExtent(int displayed_extent);
ThumbnailLoadRequest BuildClipThumbnailLoadRequest(
    const core::store::ClipSummary& summary,
    int file_thumbnail_target_extent);

bool LoadSystemThumbnailPixels(
    const std::wstring& path,
    int target_width,
    int target_height,
    UINT* width,
    UINT* height,
    std::vector<std::uint8_t>* pixels);

bool ExtractIconPixels(
    const std::wstring& path,
    int size,
    UINT* width,
    UINT* height,
    std::vector<std::uint8_t>* pixels);
bool LoadShellIconPixels(
    const ShellIconRequest& request,
    ShellIconPixels* pixels);
bool DrawShellIcon(
    HDC dc,
    const ShellIconRequest& request,
    const RECT& rect);
void TrimShellIconPixelCache(std::size_t max_entries);

common::Status LoadScaledBitmapPixels(
    const std::wstring& path,
    int max_width,
    int max_height,
    UINT* width,
    UINT* height,
    std::vector<std::uint8_t>* pixels);

#ifdef SCRAPS_ENABLE_TEST_HOOKS
void ClearShellIconPixelCacheForTests();
#endif

}  // namespace clipd2d::ui::visual_assets
