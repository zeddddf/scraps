#include "ui/ClipVisualAssets.h"

#include <commctrl.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <wincodec.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <mutex>
#include <unordered_map>

#include <wrl/client.h>

#include "common/StringUtil.h"
#include "common/Utf8.h"
#include "common/Paths.h"
#include "common/FileHash.h"

namespace clipd2d::ui::visual_assets {

namespace {

std::wstring ResolveCachePathForKey(const std::wstring& path, int target_width, int target_height) {
  common::AppPaths app_paths;
  if (!common::ResolveAppPaths(&app_paths).ok()) {
    return {};
  }
  std::wstring derived_dir = app_paths.cache_dir + L"\\derived";
  common::EnsureDirectory(derived_dir);
  std::wstring thumb_dir = derived_dir + L"\\thumbnails";
  common::EnsureDirectory(thumb_dir);

  const std::wstring input = path + L"|" + std::to_wstring(target_width) + L"x" + std::to_wstring(target_height);
  const std::string utf8_input = common::WideToUtf8(input).utf8;
  const std::string hash = common::HashBytes(utf8_input);
  return thumb_dir + L"\\" + common::Utf8ToWide(hash).wide + L"_thumb.png";
}

bool SaveBgraPixelsToPng(
    const std::wstring& path,
    UINT width,
    UINT height,
    const std::vector<std::uint8_t>& pixels) {
  if (path.empty() || width == 0 || height == 0 || pixels.empty()) {
    return false;
  }
  
  IWICImagingFactory* factory = nullptr;
  HRESULT hr = CoCreateInstance(
      CLSID_WICImagingFactory,
      nullptr,
      CLSCTX_INPROC_SERVER,
      IID_PPV_ARGS(&factory));
  if (FAILED(hr)) {
    return false;
  }

  IWICStream* stream = nullptr;
  hr = factory->CreateStream(&stream);
  if (FAILED(hr)) {
    factory->Release();
    return false;
  }

  hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
  if (FAILED(hr)) {
    stream->Release();
    factory->Release();
    return false;
  }

  IWICBitmapEncoder* encoder = nullptr;
  hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
  if (FAILED(hr)) {
    stream->Release();
    factory->Release();
    return false;
  }

  hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
  IWICBitmapFrameEncode* frame = nullptr;
  IPropertyBag2* property_bag = nullptr;
  if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(&frame, &property_bag);
  if (SUCCEEDED(hr)) hr = frame->Initialize(property_bag);
  if (SUCCEEDED(hr)) hr = frame->SetSize(width, height);
  if (SUCCEEDED(hr)) {
    WICPixelFormatGUID pixel_format = GUID_WICPixelFormat32bppBGRA;
    hr = frame->SetPixelFormat(&pixel_format);
  }
  if (SUCCEEDED(hr)) {
    hr = frame->WritePixels(height, width * 4, static_cast<UINT>(pixels.size()), const_cast<BYTE*>(pixels.data()));
  }
  if (SUCCEEDED(hr)) hr = frame->Commit();
  if (SUCCEEDED(hr)) hr = encoder->Commit();

  if (property_bag != nullptr) property_bag->Release();
  if (frame != nullptr) frame->Release();
  encoder->Release();
  stream->Release();
  factory->Release();

  return SUCCEEDED(hr);
}

bool LoadBgraPixelsFromPng(
    const std::wstring& path,
    UINT* width,
    UINT* height,
    std::vector<std::uint8_t>* pixels) {
  if (path.empty() || width == nullptr || height == nullptr || pixels == nullptr) {
    return false;
  }

  IWICImagingFactory* factory = nullptr;
  HRESULT hr = CoCreateInstance(
      CLSID_WICImagingFactory,
      nullptr,
      CLSCTX_INPROC_SERVER,
      IID_PPV_ARGS(&factory));
  if (FAILED(hr)) {
    return false;
  }

  IWICBitmapDecoder* decoder = nullptr;
  hr = factory->CreateDecoderFromFilename(
      path.c_str(),
      nullptr,
      GENERIC_READ,
      WICDecodeMetadataCacheOnDemand,
      &decoder);
  if (FAILED(hr)) {
    factory->Release();
    return false;
  }

  IWICBitmapFrameDecode* frame = nullptr;
  hr = decoder->GetFrame(0, &frame);
  IWICFormatConverter* converter = nullptr;
  if (SUCCEEDED(hr)) {
    hr = factory->CreateFormatConverter(&converter);
  }
  if (SUCCEEDED(hr)) {
    hr = converter->Initialize(
        frame,
        GUID_WICPixelFormat32bppBGRA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0,
        WICBitmapPaletteTypeCustom);
  }

  if (SUCCEEDED(hr)) {
    hr = converter->GetSize(width, height);
  }

  if (SUCCEEDED(hr) && *width > 0 && *height > 0) {
    const UINT stride = *width * 4;
    pixels->resize(static_cast<size_t>(stride) * *height);
    hr = converter->CopyPixels(nullptr, stride, static_cast<UINT>(pixels->size()), pixels->data());
  }

  if (converter != nullptr) converter->Release();
  if (frame != nullptr) frame->Release();
  decoder->Release();
  factory->Release();

  return SUCCEEDED(hr);
}

const IID kIImageListIid = {
    0x46EB5926,
    0x582E,
    0x4017,
    {0x9F, 0xDF, 0xE8, 0x99, 0x8D, 0xAA, 0x09, 0x50}};

struct ShellIconPixelCacheEntry {
  ShellIconPixels pixels;
  std::uint64_t last_used = 0;
};

std::unordered_map<std::wstring, ShellIconPixelCacheEntry> g_shell_icon_pixel_cache;
std::mutex g_shell_icon_pixel_cache_mutex;
std::uint64_t g_shell_icon_pixel_cache_tick = 0;
constexpr std::size_t kShellIconPixelCacheLimit = 192;

std::wstring LowercaseWide(std::wstring text) {
  std::transform(
      text.begin(),
      text.end(),
      text.begin(),
      [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
      });
  return text;
}

bool IsVideoFilePath(const std::wstring& path) {
  if (path.empty()) {
    return false;
  }

  const std::wstring extension = LowercaseWide(std::filesystem::path(path).extension().wstring());
  return extension == L".mp4" || extension == L".mov" || extension == L".m4v" ||
         extension == L".avi" || extension == L".mkv" || extension == L".webm" ||
         extension == L".wmv";
}

std::wstring ExtractFirstLine(std::string_view text) {
  const auto conversion = clipd2d::common::Utf8ToWide(text);
  if (!conversion.status.ok()) {
    return {};
  }

  const std::wstring wide = conversion.wide;
  const size_t newline = wide.find_first_of(L"\r\n");
  return clipd2d::common::TrimWide(newline == std::wstring::npos ? wide : wide.substr(0, newline));
}

std::wstring NormalizeShellIconPath(std::wstring path) {
  path = common::TrimWide(path);
  std::replace(path.begin(), path.end(), L'/', L'\\');
  return path;
}

int ResolveShellImageListKind(int target_extent) {
  if (target_extent <= 16) {
    return SHIL_SMALL;
  }
  if (target_extent <= 32) {
    return SHIL_LARGE;
  }
  if (target_extent <= 48) {
    return SHIL_EXTRALARGE;
  }
  return SHIL_JUMBO;
}

bool ShouldUseFileAttributes(
    const std::wstring& path,
    bool path_may_not_exist) {
  if (path_may_not_exist) {
    return true;
  }
  std::error_code error;
  return !std::filesystem::exists(path, error);
}

DWORD ResolveFileAttributes(bool is_directory_hint) {
  return is_directory_hint ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
}

bool RasterizeIconHandle(
    HICON icon,
    int target_extent,
    UINT* width,
    UINT* height,
    std::vector<std::uint8_t>* pixels) {
  if (icon == nullptr || target_extent <= 0 || width == nullptr || height == nullptr ||
      pixels == nullptr) {
    return false;
  }

  HDC screen_dc = GetDC(nullptr);
  HDC memory_dc = CreateCompatibleDC(screen_dc);
  BITMAPINFO bitmap_info{};
  bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bitmap_info.bmiHeader.biWidth = target_extent;
  bitmap_info.bmiHeader.biHeight = -target_extent;
  bitmap_info.bmiHeader.biPlanes = 1;
  bitmap_info.bmiHeader.biBitCount = 32;
  bitmap_info.bmiHeader.biCompression = BI_RGB;

  void* raw_pixels = nullptr;
  HBITMAP dib = CreateDIBSection(screen_dc, &bitmap_info, DIB_RGB_COLORS, &raw_pixels, nullptr, 0);
  ReleaseDC(nullptr, screen_dc);
  if (dib == nullptr || raw_pixels == nullptr) {
    DeleteDC(memory_dc);
    return false;
  }

  HGDIOBJ old_bitmap = SelectObject(memory_dc, dib);
  std::memset(raw_pixels, 0, static_cast<size_t>(target_extent) * static_cast<size_t>(target_extent) * 4u);
  DrawIconEx(memory_dc, 0, 0, icon, target_extent, target_extent, 0, nullptr, DI_NORMAL);

  pixels->resize(static_cast<size_t>(target_extent) * static_cast<size_t>(target_extent) * 4u);
  std::memcpy(pixels->data(), raw_pixels, pixels->size());
  *width = static_cast<UINT>(target_extent);
  *height = static_cast<UINT>(target_extent);

  SelectObject(memory_dc, old_bitmap);
  DeleteObject(dib);
  DeleteDC(memory_dc);
  return true;
}

HICON LoadShellIconHandleFromImageList(
    const ShellIconRequest& request,
    const std::wstring& normalized_path,
    int target_extent) {
  SHFILEINFOW info{};
  const bool use_file_attributes = ShouldUseFileAttributes(
      normalized_path,
      request.path_may_not_exist);
  UINT flags = SHGFI_SYSICONINDEX;
  if (use_file_attributes) {
    flags |= SHGFI_USEFILEATTRIBUTES;
  }
  const DWORD attributes = use_file_attributes
      ? ResolveFileAttributes(request.is_directory_hint)
      : 0;
  if (SHGetFileInfoW(
          normalized_path.c_str(),
          attributes,
          &info,
          sizeof(info),
          flags) == 0) {
    return nullptr;
  }

  IUnknown* image_list = nullptr;
  const int image_list_kind = ResolveShellImageListKind(target_extent);
  if (FAILED(SHGetImageList(
          image_list_kind,
          kIImageListIid,
          reinterpret_cast<void**>(&image_list))) ||
      image_list == nullptr) {
    return nullptr;
  }
  HICON icon = ImageList_GetIcon(
      reinterpret_cast<HIMAGELIST>(image_list),
      info.iIcon,
      ILD_TRANSPARENT);
  image_list->Release();
  return icon;
}

HICON LoadDirectShellIconHandle(
    const ShellIconRequest& request,
    const std::wstring& normalized_path,
    int target_extent) {
  SHFILEINFOW info{};
  const bool use_file_attributes = ShouldUseFileAttributes(
      normalized_path,
      request.path_may_not_exist);
  UINT flags = SHGFI_ICON | (target_extent > 20 ? SHGFI_LARGEICON : SHGFI_SMALLICON);
  if (use_file_attributes) {
    flags |= SHGFI_USEFILEATTRIBUTES;
  }
  const DWORD attributes = use_file_attributes
      ? ResolveFileAttributes(request.is_directory_hint)
      : 0;
  if (SHGetFileInfoW(
          normalized_path.c_str(),
          attributes,
          &info,
          sizeof(info),
          flags) == 0) {
    return nullptr;
  }
  return info.hIcon;
}

bool TryGetCachedShellIconPixels(
    const std::wstring& cache_key,
    ShellIconPixels* out) {
  if (cache_key.empty() || out == nullptr) {
    return false;
  }

  std::scoped_lock lock(g_shell_icon_pixel_cache_mutex);
  const auto it = g_shell_icon_pixel_cache.find(cache_key);
  if (it == g_shell_icon_pixel_cache.end()) {
    return false;
  }
  it->second.last_used = ++g_shell_icon_pixel_cache_tick;
  *out = it->second.pixels;
  return true;
}

void TrimShellIconPixelCacheLocked() {
  while (g_shell_icon_pixel_cache.size() > kShellIconPixelCacheLimit) {
    auto candidate = g_shell_icon_pixel_cache.end();
    std::uint64_t oldest_tick = UINT64_MAX;
    for (auto it = g_shell_icon_pixel_cache.begin(); it != g_shell_icon_pixel_cache.end(); ++it) {
      if (candidate == g_shell_icon_pixel_cache.end() || it->second.last_used < oldest_tick) {
        candidate = it;
        oldest_tick = it->second.last_used;
      }
    }
    if (candidate == g_shell_icon_pixel_cache.end()) {
      break;
    }
    g_shell_icon_pixel_cache.erase(candidate);
  }
}

void TrimShellIconPixelCacheToLimitLocked(std::size_t max_entries) {
  while (g_shell_icon_pixel_cache.size() > max_entries) {
    auto candidate = g_shell_icon_pixel_cache.end();
    std::uint64_t oldest_tick = UINT64_MAX;
    for (auto it = g_shell_icon_pixel_cache.begin(); it != g_shell_icon_pixel_cache.end(); ++it) {
      if (candidate == g_shell_icon_pixel_cache.end() || it->second.last_used < oldest_tick) {
        candidate = it;
        oldest_tick = it->second.last_used;
      }
    }
    if (candidate == g_shell_icon_pixel_cache.end()) {
      break;
    }
    g_shell_icon_pixel_cache.erase(candidate);
  }
}

void StoreShellIconPixelsInCache(const ShellIconPixels& pixels) {
  if (pixels.cache_key.empty() || pixels.pixels.empty()) {
    return;
  }

  std::scoped_lock lock(g_shell_icon_pixel_cache_mutex);
  auto& entry = g_shell_icon_pixel_cache[pixels.cache_key];
  entry.pixels = pixels;
  entry.last_used = ++g_shell_icon_pixel_cache_tick;
  TrimShellIconPixelCacheLocked();
}

bool LoadShellIconPixelsUncached(
    const ShellIconRequest& request,
    ShellIconPixels* out) {
  if (out == nullptr) {
    return false;
  }

  const std::wstring normalized_path = NormalizeShellIconPath(request.path);
  if (normalized_path.empty()) {
    return false;
  }

  const int target_extent = ResolveShellIconDecodeExtent(
      request.purpose,
      request.display_extent,
      request.dpi);
  HICON icon = LoadShellIconHandleFromImageList(
      request,
      normalized_path,
      target_extent);
  if (icon == nullptr) {
    icon = LoadDirectShellIconHandle(
        request,
        normalized_path,
        target_extent);
  }
  if (icon == nullptr) {
    return false;
  }

  out->pixels.clear();
  const bool loaded = RasterizeIconHandle(
      icon,
      target_extent,
      &out->width,
      &out->height,
      &out->pixels);
  DestroyIcon(icon);
  return loaded;
}

}  // namespace

bool IsImageFilePath(const std::wstring& path) {
  if (path.empty()) {
    return false;
  }

  const std::wstring extension = LowercaseWide(std::filesystem::path(path).extension().wstring());
  return extension == L".png" || extension == L".jpg" || extension == L".jpeg" ||
         extension == L".bmp" || extension == L".gif" || extension == L".webp" ||
         extension == L".tif" || extension == L".tiff" || extension == L".ico" ||
         extension == L".heic";
}

bool SupportsSystemThumbnailPreviewPath(const std::wstring& path) {
  return IsImageFilePath(path) || IsVideoFilePath(path);
}

std::uint64_t TryFileSize(const std::wstring& path) {
  std::error_code error;
  const auto size = std::filesystem::file_size(path, error);
  if (error) {
    return 0;
  }
  return static_cast<std::uint64_t>(size);
}

std::wstring FirstFilePathFromSummary(const core::store::ClipSummary& summary) {
  if (summary.content_type == core::store::ClipContentType::Files && !summary.storage_path.empty()) {
    return common::TrimWide(summary.storage_path);
  }
  if (summary.content_type == core::store::ClipContentType::Files && !summary.secondary_path.empty()) {
    return common::TrimWide(summary.secondary_path);
  }
  return common::TrimWide(ExtractFirstLine(summary.snippet));
}

std::wstring ThumbnailKeyFromSummary(const core::store::ClipSummary& summary) {
  if (summary.content_type == core::store::ClipContentType::Image) {
    if (!summary.thumbnail_path.empty()) {
      return summary.thumbnail_path;
    }
    return summary.storage_path;
  }
  if (summary.content_type == core::store::ClipContentType::Files) {
    if (!summary.thumbnail_path.empty()) {
      return summary.thumbnail_path;
    }
    const std::wstring first_path = FirstFilePathFromSummary(summary);
    if (SupportsSystemThumbnailPreviewPath(first_path)) {
      return first_path;
    }
  }
  return L"";
}

ShellIconRequest BuildSourceAppIconRequest(
    const std::wstring& path,
    int displayed_extent,
    UINT dpi) {
  ShellIconRequest request;
  request.path = NormalizeShellIconPath(path);
  request.purpose = ShellIconPurpose::SourceAppBadge;
  request.display_extent = std::max(1, displayed_extent);
  request.dpi = dpi == 0 ? 96u : dpi;
  return request;
}

ShellIconRequest BuildFileIconRequest(
    const std::wstring& path,
    int displayed_extent,
    UINT dpi,
    bool path_may_not_exist,
    bool is_directory_hint) {
  ShellIconRequest request;
  request.path = NormalizeShellIconPath(path);
  request.purpose = ShellIconPurpose::FilePrimary;
  request.display_extent = std::max(1, displayed_extent);
  request.dpi = dpi == 0 ? 96u : dpi;
  request.path_may_not_exist = path_may_not_exist;
  request.is_directory_hint = is_directory_hint;
  return request;
}

int ResolveShellIconDecodeExtent(
    ShellIconPurpose purpose,
    int displayed_extent,
    UINT dpi) {
  const UINT effective_dpi = dpi == 0 ? 96u : dpi;
  const int physical_extent = std::max(
      1,
      MulDiv(std::max(1, displayed_extent), static_cast<int>(effective_dpi), 96));
  if (purpose == ShellIconPurpose::SourceAppBadge) {
    return std::clamp(physical_extent * 2, 16, 48);
  }
  return std::clamp(physical_extent * 3, 64, 128);
}

std::wstring BuildShellIconCacheKey(const ShellIconRequest& request) {
  const std::wstring normalized_path = NormalizeShellIconPath(request.path);
  if (normalized_path.empty()) {
    return {};
  }

  const std::wstring purpose_token =
      request.purpose == ShellIconPurpose::SourceAppBadge ? L"source" : L"file";
  const int decode_extent = ResolveShellIconDecodeExtent(
      request.purpose,
      request.display_extent,
      request.dpi);
  return purpose_token + L"|" +
      std::to_wstring(decode_extent) + L"|" +
      (request.is_directory_hint ? L"d" : L"f") + L"|" +
      (request.path_may_not_exist ? L"m" : L"e") + L"|" +
      LowercaseWide(normalized_path);
}

int ResolveFileThumbnailDecodeExtent(int displayed_extent) {
  return std::clamp(std::max(1, displayed_extent) * 3, 96, 192);
}

int ResolveFileIconDecodeExtent(int displayed_extent) {
  return ResolveShellIconDecodeExtent(
      ShellIconPurpose::FilePrimary,
      displayed_extent,
      96);
}

ThumbnailLoadRequest BuildClipThumbnailLoadRequest(
    const core::store::ClipSummary& summary,
    int file_thumbnail_target_extent) {
  ThumbnailLoadRequest request;
  request.key = ThumbnailKeyFromSummary(summary);
  if (request.key.empty()) {
    return request;
  }

  if (summary.content_type == core::store::ClipContentType::Files && summary.thumbnail_path.empty()) {
    request.kind = ThumbnailLoadKind::SystemThumbnail;
    request.max_width = file_thumbnail_target_extent;
    request.max_height = file_thumbnail_target_extent;
    request.allow_fallback_decode =
        IsImageFilePath(request.key) && TryFileSize(request.key) <= 10ull * 1024ull * 1024ull;
    return request;
  }

  request.kind = ThumbnailLoadKind::ScaledBitmap;
  request.max_width = 240;
  request.max_height = 160;
  return request;
}

bool LoadSystemThumbnailPixels(
    const std::wstring& path,
    int target_width,
    int target_height,
    UINT* width,
    UINT* height,
    std::vector<std::uint8_t>* pixels) {
  if (path.empty() || target_width <= 0 || target_height <= 0 || width == nullptr || height == nullptr || pixels == nullptr) {
    return false;
  }

  *width = 0;
  *height = 0;
  pixels->clear();

  const std::wstring cache_file = ResolveCachePathForKey(path, target_width, target_height);
  if (!cache_file.empty() && std::filesystem::exists(cache_file)) {
    if (LoadBgraPixelsFromPng(cache_file, width, height, pixels)) {
      return true;
    }
  }

  const HRESULT init_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  const bool need_uninit = SUCCEEDED(init_result);

  Microsoft::WRL::ComPtr<IShellItemImageFactory> image_factory;
  const HRESULT create_result = SHCreateItemFromParsingName(
      path.c_str(),
      nullptr,
      IID_PPV_ARGS(&image_factory));
  if (FAILED(create_result) || image_factory == nullptr) {
    if (need_uninit) {
      CoUninitialize();
    }
    return false;
  }

  SIZE requested_size{target_width, target_height};
  HBITMAP bitmap = nullptr;
  const HRESULT image_result = image_factory->GetImage(
      requested_size,
      SIIGBF_BIGGERSIZEOK | SIIGBF_RESIZETOFIT | SIIGBF_THUMBNAILONLY,
      &bitmap);
  if (FAILED(image_result) || bitmap == nullptr) {
    if (bitmap != nullptr) {
      DeleteObject(bitmap);
    }
    if (need_uninit) {
      CoUninitialize();
    }
    return false;
  }

  BITMAP info{};
  if (GetObjectW(bitmap, sizeof(info), &info) == 0 || info.bmWidth <= 0 || info.bmHeight <= 0) {
    DeleteObject(bitmap);
    if (need_uninit) {
      CoUninitialize();
    }
    return false;
  }

  *width = static_cast<UINT>(info.bmWidth);
  *height = static_cast<UINT>(info.bmHeight);

  BITMAPINFO bitmap_info{};
  bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bitmap_info.bmiHeader.biWidth = info.bmWidth;
  bitmap_info.bmiHeader.biHeight = -info.bmHeight;
  bitmap_info.bmiHeader.biPlanes = 1;
  bitmap_info.bmiHeader.biBitCount = 32;
  bitmap_info.bmiHeader.biCompression = BI_RGB;

  const size_t pixel_bytes = static_cast<size_t>(info.bmWidth) * static_cast<size_t>(info.bmHeight) * 4u;
  pixels->resize(pixel_bytes);

  HDC screen_dc = GetDC(nullptr);
  const int copied = GetDIBits(
      screen_dc,
      bitmap,
      0,
      static_cast<UINT>(info.bmHeight),
      pixels->data(),
      &bitmap_info,
      DIB_RGB_COLORS);
  ReleaseDC(nullptr, screen_dc);
  DeleteObject(bitmap);

  if (need_uninit) {
    CoUninitialize();
  }

  if (copied == 0) {
    pixels->clear();
    *width = 0;
    *height = 0;
    return false;
  }

  if (!cache_file.empty()) {
    SaveBgraPixelsToPng(cache_file, *width, *height, *pixels);
  }
  return true;
}

bool ExtractIconPixels(const std::wstring& path, int size, UINT* width, UINT* height, std::vector<std::uint8_t>* pixels) {
  if (width == nullptr || height == nullptr || pixels == nullptr || path.empty()) {
    return false;
  }

  const ShellIconRequest request = BuildFileIconRequest(path, size, 96, false, false);
  ShellIconPixels shell_icon;
  if (!LoadShellIconPixels(request, &shell_icon)) {
    return false;
  }
  *width = shell_icon.width;
  *height = shell_icon.height;
  *pixels = std::move(shell_icon.pixels);
  return true;
}

bool LoadShellIconPixels(
    const ShellIconRequest& request,
    ShellIconPixels* out) {
  if (out == nullptr) {
    return false;
  }

  out->cache_key = BuildShellIconCacheKey(request);
  out->width = 0;
  out->height = 0;
  out->pixels.clear();
  if (out->cache_key.empty()) {
    return false;
  }

  static std::mutex s_shell_api_mutex;
  std::scoped_lock lock(s_shell_api_mutex);

  if (TryGetCachedShellIconPixels(out->cache_key, out)) {
    return true;
  }

  const HRESULT init_hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
  const bool need_uninit = SUCCEEDED(init_hr);

  ShellIconPixels generated;
  generated.cache_key = out->cache_key;
  const bool loaded = LoadShellIconPixelsUncached(request, &generated);

  if (need_uninit) {
    CoUninitialize();
  }

  if (!loaded) {
    return false;
  }

  StoreShellIconPixelsInCache(generated);
  *out = std::move(generated);
  return true;
}

bool DrawShellIcon(
    HDC dc,
    const ShellIconRequest& request,
    const RECT& rect) {
  if (dc == nullptr) {
    return false;
  }

  const std::wstring normalized_path = NormalizeShellIconPath(request.path);
  const int width = std::max<LONG>(0, rect.right - rect.left);
  const int height = std::max<LONG>(0, rect.bottom - rect.top);
  if (normalized_path.empty() || width <= 0 || height <= 0) {
    return false;
  }

  SHFILEINFOW info{};
  const bool use_file_attributes = ShouldUseFileAttributes(
      normalized_path,
      request.path_may_not_exist);
  UINT flags = SHGFI_SYSICONINDEX;
  if (use_file_attributes) {
    flags |= SHGFI_USEFILEATTRIBUTES;
  }
  const DWORD attributes = use_file_attributes
      ? ResolveFileAttributes(request.is_directory_hint)
      : 0;
  if (SHGetFileInfoW(
          normalized_path.c_str(),
          attributes,
          &info,
          sizeof(info),
          flags) == 0) {
    return false;
  }

  IUnknown* image_list = nullptr;
  const int slot_extent = std::max(1, std::min(width, height));
  int image_list_kind = SHIL_SMALL;
  int native_extent = 16;
  if (slot_extent >= 48) {
    image_list_kind = SHIL_EXTRALARGE;
    native_extent = 48;
  } else if (slot_extent >= 32) {
    image_list_kind = SHIL_LARGE;
    native_extent = 32;
  }
  if (FAILED(SHGetImageList(
          image_list_kind,
          kIImageListIid,
          reinterpret_cast<void**>(&image_list))) ||
      image_list == nullptr) {
    return false;
  }

  const int draw_extent = std::min(slot_extent, native_extent);
  const int draw_left = rect.left + std::max(0, (width - draw_extent) / 2);
  const int draw_top = rect.top + std::max(0, (height - draw_extent) / 2);
  const BOOL draw_result = ImageList_DrawEx(
             reinterpret_cast<HIMAGELIST>(image_list),
             info.iIcon,
             dc,
             draw_left,
             draw_top,
             0,
             0,
             CLR_NONE,
             CLR_NONE,
             ILD_TRANSPARENT);
  image_list->Release();
  return draw_result != FALSE;
}

void TrimShellIconPixelCache(std::size_t max_entries) {
  std::scoped_lock lock(g_shell_icon_pixel_cache_mutex);
  TrimShellIconPixelCacheToLimitLocked(max_entries);
}

common::Status LoadScaledBitmapPixels(
    const std::wstring& path,
    int max_width,
    int max_height,
    UINT* width,
    UINT* height,
    std::vector<std::uint8_t>* pixels) {
  if (width == nullptr || height == nullptr || pixels == nullptr) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "Invalid image decode output.");
  }

  *width = 0;
  *height = 0;
  pixels->clear();

  const std::wstring cache_file = ResolveCachePathForKey(path, max_width, max_height);
  if (!cache_file.empty() && std::filesystem::exists(cache_file)) {
    if (LoadBgraPixelsFromPng(cache_file, width, height, pixels)) {
      return common::Status::Ok();
    }
  }

  const HRESULT init_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  const bool need_uninit = SUCCEEDED(init_result);

  IWICImagingFactory* factory = nullptr;
  HRESULT hr = CoCreateInstance(
      CLSID_WICImagingFactory,
      nullptr,
      CLSCTX_INPROC_SERVER,
      IID_PPV_ARGS(&factory));
  if (FAILED(hr)) {
    if (need_uninit) {
      CoUninitialize();
    }
    return common::Status::Error(common::StatusCode::PlatformError, "Create WIC factory failed.");
  }

  IWICBitmapDecoder* decoder = nullptr;
  hr = factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
  if (FAILED(hr)) {
    factory->Release();
    if (need_uninit) {
      CoUninitialize();
    }
    return common::Status::Error(common::StatusCode::NotFound, "Open image file failed.");
  }

  IWICBitmapFrameDecode* frame = nullptr;
  hr = decoder->GetFrame(0, &frame);
  if (FAILED(hr)) {
    decoder->Release();
    factory->Release();
    if (need_uninit) {
      CoUninitialize();
    }
    return common::Status::Error(common::StatusCode::PlatformError, "Read image frame failed.");
  }

  UINT source_width = 0;
  UINT source_height = 0;
  hr = frame->GetSize(&source_width, &source_height);
  if (FAILED(hr) || source_width == 0 || source_height == 0) {
    frame->Release();
    decoder->Release();
    factory->Release();
    if (need_uninit) {
      CoUninitialize();
    }
    return common::Status::Error(common::StatusCode::PlatformError, "Read image size failed.");
  }

  const double scale_x = max_width > 0 ? static_cast<double>(max_width) / source_width : 1.0;
  const double scale_y = max_height > 0 ? static_cast<double>(max_height) / source_height : 1.0;
  const double scale = std::min(1.0, std::min(scale_x, scale_y));
  *width = std::max<UINT>(1, static_cast<UINT>(std::round(source_width * scale)));
  *height = std::max<UINT>(1, static_cast<UINT>(std::round(source_height * scale)));

  IWICBitmapScaler* scaler = nullptr;
  hr = factory->CreateBitmapScaler(&scaler);
  if (SUCCEEDED(hr)) {
    hr = scaler->Initialize(frame, *width, *height, WICBitmapInterpolationModeFant);
  }

  IWICFormatConverter* converter = nullptr;
  if (SUCCEEDED(hr)) {
    hr = factory->CreateFormatConverter(&converter);
  }
  if (SUCCEEDED(hr)) {
    hr = converter->Initialize(
        scaler,
        GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0,
        WICBitmapPaletteTypeCustom);
  }

  if (FAILED(hr)) {
    if (converter != nullptr) {
      converter->Release();
    }
    if (scaler != nullptr) {
      scaler->Release();
    }
    frame->Release();
    decoder->Release();
    factory->Release();
    if (need_uninit) {
      CoUninitialize();
    }
    return common::Status::Error(common::StatusCode::PlatformError, "Convert image pixels failed.");
  }

  const UINT stride = *width * 4;
  pixels->resize(static_cast<size_t>(stride) * static_cast<size_t>(*height));
  hr = converter->CopyPixels(nullptr, stride, static_cast<UINT>(pixels->size()), pixels->data());

  converter->Release();
  scaler->Release();
  frame->Release();
  decoder->Release();
  factory->Release();
  if (need_uninit) {
    CoUninitialize();
  }

  if (FAILED(hr)) {
    pixels->clear();
    *width = 0;
    *height = 0;
    return common::Status::Error(common::StatusCode::PlatformError, "Copy image pixels failed.");
  }

  if (!cache_file.empty()) {
    SaveBgraPixelsToPng(cache_file, *width, *height, *pixels);
  }

  return common::Status::Ok();
}

#ifdef SCRAPS_ENABLE_TEST_HOOKS
void ClearShellIconPixelCacheForTests() {
  std::scoped_lock lock(g_shell_icon_pixel_cache_mutex);
  g_shell_icon_pixel_cache.clear();
  g_shell_icon_pixel_cache_tick = 0;
}
#endif

}  // namespace clipd2d::ui::visual_assets
