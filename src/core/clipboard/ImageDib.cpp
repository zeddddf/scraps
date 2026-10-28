#include "core/clipboard/ImageDib.h"
#include "core/clipboard/ImageIdentity.h"

#include <windows.h>
#include <shellapi.h>
#include <wincodec.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include "common/FileHash.h"

namespace clipd2d::core::clipboard {

size_t DibColorTableBytes(const BITMAPINFOHEADER& header) {
  if (header.biClrUsed != 0) {
    return static_cast<size_t>(header.biClrUsed) * sizeof(RGBQUAD);
  }
  if (header.biBitCount <= 8) {
    return (static_cast<size_t>(1) << header.biBitCount) * sizeof(RGBQUAD);
  }
  if (header.biCompression == BI_BITFIELDS &&
      header.biSize == sizeof(BITMAPINFOHEADER) &&
      (header.biBitCount == 16 || header.biBitCount == 32)) {
    return 3u * sizeof(DWORD);
  }
  return 0;
}

bool ParseDibSize(const std::vector<std::uint8_t>& dib_bytes, int* width, int* height) {
  if (width == nullptr || height == nullptr || dib_bytes.size() < sizeof(BITMAPINFOHEADER)) {
    return false;
  }
  const auto* header = reinterpret_cast<const BITMAPINFOHEADER*>(dib_bytes.data());
  if (header->biSize < sizeof(BITMAPINFOHEADER) || dib_bytes.size() < header->biSize) {
    return false;
  }
  *width = static_cast<int>(header->biWidth);
  *height = static_cast<int>(header->biHeight < 0 ? -header->biHeight : header->biHeight);
  return *width > 0 && *height > 0;
}

common::Status SnapshotBitmapToDibBytes(
    HBITMAP bitmap,
    std::vector<std::uint8_t>* dib_bytes,
    int* width,
    int* height) {
  if (dib_bytes == nullptr || width == nullptr || height == nullptr) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "Invalid bitmap snapshot output.");
  }

  dib_bytes->clear();
  *width = 0;
  *height = 0;

  BITMAP info{};
  if (GetObjectW(bitmap, sizeof(info), &info) == 0) {
    return common::Status::Error(common::StatusCode::PlatformError, "Read bitmap info failed.");
  }
  if (info.bmWidth <= 0 || info.bmHeight <= 0) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "Invalid bitmap size.");
  }

  *width = info.bmWidth;
  *height = info.bmHeight;

  BITMAPINFO bitmap_info{};
  bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bitmap_info.bmiHeader.biWidth = info.bmWidth;
  bitmap_info.bmiHeader.biHeight = -info.bmHeight;
  bitmap_info.bmiHeader.biPlanes = 1;
  bitmap_info.bmiHeader.biBitCount = 32;
  bitmap_info.bmiHeader.biCompression = BI_RGB;

  const size_t pixel_bytes =
      static_cast<size_t>(info.bmWidth) * static_cast<size_t>(info.bmHeight) * 4u;
  dib_bytes->resize(sizeof(BITMAPINFOHEADER) + pixel_bytes);
  memcpy(dib_bytes->data(), &bitmap_info.bmiHeader, sizeof(BITMAPINFOHEADER));

  HDC screen = GetDC(nullptr);
  if (screen == nullptr) {
    dib_bytes->clear();
    return common::Status::Error(common::StatusCode::PlatformError, "GetDC failed.");
  }
  if (GetDIBits(
          screen,
          bitmap,
          0,
          static_cast<UINT>(info.bmHeight),
          dib_bytes->data() + sizeof(BITMAPINFOHEADER),
          &bitmap_info,
          DIB_RGB_COLORS) == 0) {
    ReleaseDC(nullptr, screen);
    dib_bytes->clear();
    return common::Status::Error(common::StatusCode::PlatformError, "Extract bitmap pixels failed.");
  }
  ReleaseDC(nullptr, screen);
  return common::Status::Ok();
}

common::Status ExtractBitmapPixels(
    HBITMAP bitmap,
    std::vector<std::uint8_t>* bgra,
    int* width,
    int* height) {
  if (bgra == nullptr || width == nullptr || height == nullptr) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "Invalid bitmap pixel output.");
  }

  bgra->clear();
  *width = 0;
  *height = 0;
  if (bitmap == nullptr) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "Bitmap is null.");
  }

  BITMAP info{};
  if (GetObjectW(bitmap, sizeof(info), &info) == 0) {
    return common::Status::Error(common::StatusCode::PlatformError, "Read bitmap info failed.");
  }
  if (info.bmWidth <= 0 || info.bmHeight <= 0) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "Invalid bitmap size.");
  }

  *width = info.bmWidth;
  *height = info.bmHeight;

  BITMAPINFO bitmap_info{};
  bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bitmap_info.bmiHeader.biWidth = info.bmWidth;
  bitmap_info.bmiHeader.biHeight = -info.bmHeight;
  bitmap_info.bmiHeader.biPlanes = 1;
  bitmap_info.bmiHeader.biBitCount = 32;
  bitmap_info.bmiHeader.biCompression = BI_RGB;

  bgra->resize(static_cast<size_t>(info.bmWidth) * static_cast<size_t>(info.bmHeight) * 4u);
  HDC screen = GetDC(nullptr);
  if (screen == nullptr) {
    return common::Status::Error(common::StatusCode::PlatformError, "GetDC failed.");
  }
  if (GetDIBits(
          screen,
          bitmap,
          0,
          static_cast<UINT>(info.bmHeight),
          bgra->data(),
          &bitmap_info,
          DIB_RGB_COLORS) == 0) {
    ReleaseDC(nullptr, screen);
    bgra->clear();
    return common::Status::Error(common::StatusCode::PlatformError, "Extract bitmap pixels failed.");
  }
  ReleaseDC(nullptr, screen);
  return common::Status::Ok();
}

common::Status ComputeBitmapPixelHash(
    HBITMAP bitmap,
    std::string* hash_out,
    int* width,
    int* height) {
  if (hash_out == nullptr || width == nullptr || height == nullptr) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "Invalid bitmap hash output.");
  }
  std::vector<std::uint8_t> pixels;
  const common::Status status = ExtractBitmapPixels(bitmap, &pixels, width, height);
  if (!status.ok()) {
    return status;
  }
  return ComputeImageContentHashFromCanonicalPixels(
      *width,
      *height,
      std::string_view(reinterpret_cast<const char*>(pixels.data()), pixels.size()),
      hash_out);
}
// INVARIANT: CF_DIB（BI_RGB, 32bpp）的第 4 字节按 Win32 规范是保留位，GDI 与大量
// 第三方截图工具（Snipaste / PixPin 等）统一把它填 0。原样当 alpha 编码会得到整张
// 全透明的 PNG —— 用户看到「截图是空白的」。只在「未声明 alpha 掩码 + 全零」时
// 判定该通道不是 alpha，强制不透明；真实带 alpha 的 DIBV5 不受影响。幂等。
bool ForceOpaqueIfAlphaDegenerate(int width, int height, std::uint8_t* bgra, size_t stride) {
  if (bgra == nullptr || width <= 0 || height <= 0 || stride < static_cast<size_t>(width) * 4u) {
    return false;
  }
  const size_t pixel_count = static_cast<size_t>(width) * static_cast<size_t>(height);
  for (size_t index = 0; index < pixel_count; ++index) {
    if (bgra[index * 4u + 3u] != 0) {
      return false;
    }
  }
  for (size_t index = 0; index < pixel_count; ++index) {
    bgra[index * 4u + 3u] = 0xFF;
  }
  return true;
}

void NormalizeDibAlphaIfDegenerate(std::vector<std::uint8_t>* dib_bytes) {
  if (dib_bytes == nullptr || dib_bytes->size() < sizeof(BITMAPINFOHEADER)) {
    return;
  }
  const auto* header = reinterpret_cast<const BITMAPINFOHEADER*>(dib_bytes->data());
  if (header->biSize < sizeof(BITMAPINFOHEADER) || dib_bytes->size() < header->biSize) {
    return;
  }
  if (header->biBitCount != 32 || header->biCompression != BI_RGB) {
    return;
  }
  // DIBV5 显式声明了 alpha 掩码时，第 4 字节确实可能是 alpha，不要动。
  if (header->biSize >= sizeof(BITMAPV5HEADER)) {
    const auto* v5 = reinterpret_cast<const BITMAPV5HEADER*>(header);
    if (v5->bV5AlphaMask != 0) {
      return;
    }
  }
  const size_t pixel_offset = static_cast<size_t>(header->biSize) + DibColorTableBytes(*header);
  if (pixel_offset >= dib_bytes->size()) {
    return;
  }
  const size_t pixel_count = (dib_bytes->size() - pixel_offset) / 4u;
  if (pixel_count == 0) {
    return;
  }
  for (size_t index = 0; index < pixel_count; ++index) {
    if ((*dib_bytes)[pixel_offset + index * 4u + 3u] != 0) {
      return;
    }
  }
  for (size_t index = 0; index < pixel_count; ++index) {
    (*dib_bytes)[pixel_offset + index * 4u + 3u] = 0xFF;
  }
}

}  // namespace clipd2d::core::clipboard
