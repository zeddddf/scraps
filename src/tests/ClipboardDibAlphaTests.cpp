#include "tests/TestSupport.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <vector>

#include "common/Paths.h"
#include <objbase.h>
#include <wincodec.h>

#include "core/clipboard/ClipboardCapture.h"

namespace {

using clipd2d::core::clipboard::NormalizeDibAlphaIfDegenerate;

// 造一份 32bpp BI_RGB 的 CF_DIB 负载（top-down，无颜色表）。
std::vector<std::uint8_t> Make32bppDib(int width, int height, std::uint8_t fourth_byte) {
  BITMAPINFOHEADER header{};
  header.biSize = sizeof(BITMAPINFOHEADER);
  header.biWidth = width;
  header.biHeight = -height;
  header.biPlanes = 1;
  header.biBitCount = 32;
  header.biCompression = BI_RGB;

  std::vector<std::uint8_t> bytes(sizeof(header) + static_cast<size_t>(width) * height * 4u);
  std::memcpy(bytes.data(), &header, sizeof(header));
  std::uint8_t* pixels = bytes.data() + sizeof(header);
  for (int index = 0; index < width * height; ++index) {
    pixels[index * 4 + 0] = 0x10;
    pixels[index * 4 + 1] = 0x20;
    pixels[index * 4 + 2] = 0x30;
    pixels[index * 4 + 3] = fourth_byte;
  }
  return bytes;
}

std::uint8_t FourthByteAt(const std::vector<std::uint8_t>& bytes, int pixel_index) {
  return bytes[sizeof(BITMAPINFOHEADER) + static_cast<size_t>(pixel_index) * 4u + 3u];
}

clipd2d::common::AppPaths MakeTempAppPaths(const std::filesystem::path& root) {
  clipd2d::common::AppPaths paths;
  paths.root_dir = root.wstring();
  paths.storage_dir = (root / L"storage").wstring();
  paths.image_dir = (root / L"storage" / L"images").wstring();
  paths.cache_dir = (root / L"cache").wstring();
  paths.database_path = (root / L"clipboard.db").wstring();
  return paths;
}

}  // namespace

// 回归：Snipaste / PixPin 等工具的 32bpp BI_RGB DIB 把保留位统一填 0。
// 若当成 alpha 送去编码，PNG 整张全透明——用户看到「截图是空白的」。
CLIPD2D_TEST(ClipboardDibAlphaForcesOpaqueWhenReservedByteIsZero) {
  auto dib = Make32bppDib(3, 2, 0x00);
  NormalizeDibAlphaIfDegenerate(&dib);
  for (int index = 0; index < 6; ++index) {
    CLIPD2D_CHECK_EQ(static_cast<int>(FourthByteAt(dib, index)), 0xFF);
  }
  // RGB 通道不得被改写。
  CLIPD2D_CHECK_EQ(static_cast<int>(dib[sizeof(BITMAPINFOHEADER) + 0]), 0x10);
  CLIPD2D_CHECK_EQ(static_cast<int>(dib[sizeof(BITMAPINFOHEADER) + 1]), 0x20);
  CLIPD2D_CHECK_EQ(static_cast<int>(dib[sizeof(BITMAPINFOHEADER) + 2]), 0x30);
}

CLIPD2D_TEST(ClipboardDibAlphaKeepsRealAlphaChannel) {
  auto dib = Make32bppDib(2, 2, 0x00);
  dib[sizeof(BITMAPINFOHEADER) + 3u] = 0x80;  // 至少一个像素带真实 alpha
  const std::vector<std::uint8_t> before = dib;
  NormalizeDibAlphaIfDegenerate(&dib);
  CLIPD2D_CHECK(before == dib);
}

CLIPD2D_TEST(ClipboardDibAlphaIsIdempotent) {
  auto dib = Make32bppDib(2, 2, 0x00);
  NormalizeDibAlphaIfDegenerate(&dib);
  const std::vector<std::uint8_t> once = dib;
  NormalizeDibAlphaIfDegenerate(&dib);
  CLIPD2D_CHECK(once == dib);
  CLIPD2D_CHECK_EQ(static_cast<int>(FourthByteAt(dib, 0)), 0xFF);
}

CLIPD2D_TEST(ClipboardDibAlphaSkipsNon32bppPayload) {
  BITMAPINFOHEADER header{};
  header.biSize = sizeof(BITMAPINFOHEADER);
  header.biWidth = 2;
  header.biHeight = -2;
  header.biPlanes = 1;
  header.biBitCount = 24;
  header.biCompression = BI_RGB;
  std::vector<std::uint8_t> bytes(sizeof(header) + 2u * 2u * 3u, 0xAB);
  std::memcpy(bytes.data(), &header, sizeof(header));
  const std::vector<std::uint8_t> before = bytes;
  NormalizeDibAlphaIfDegenerate(&bytes);
  CLIPD2D_CHECK(before == bytes);
}

CLIPD2D_TEST(ClipboardDibAlphaSkipsDeclaredAlphaMask) {
  BITMAPV5HEADER header{};
  header.bV5Size = sizeof(BITMAPV5HEADER);
  header.bV5Width = 2;
  header.bV5Height = -2;
  header.bV5Planes = 1;
  header.bV5BitCount = 32;
  header.bV5Compression = BI_BITFIELDS;
  header.bV5AlphaMask = 0xFF000000;
  std::vector<std::uint8_t> bytes(sizeof(header) + 2u * 2u * 4u, 0x00);
  std::memcpy(bytes.data(), &header, sizeof(header));
  const std::vector<std::uint8_t> before = bytes;
  NormalizeDibAlphaIfDegenerate(&bytes);
  CLIPD2D_CHECK(before == bytes);
}

CLIPD2D_TEST(ClipboardDibAlphaRejectsTruncatedPayload) {
  std::vector<std::uint8_t> bytes(sizeof(BITMAPINFOHEADER) + 3u, 0x00);
  BITMAPINFOHEADER header{};
  header.biSize = sizeof(BITMAPINFOHEADER);
  header.biWidth = 100;
  header.biHeight = -100;
  header.biPlanes = 1;
  header.biBitCount = 32;
  header.biCompression = BI_RGB;
  std::memcpy(bytes.data(), &header, sizeof(header));
  const std::vector<std::uint8_t> before = bytes;
  NormalizeDibAlphaIfDegenerate(&bytes);
  CLIPD2D_CHECK(before == bytes);
}

// 端到端：走完整的 ingest 落盘路径。保留位为 0 的 DIB 与显式不透明的 DIB 必须产出
// 完全相同的像素结果——否则就是「截图存成一张全透明 PNG」的老问题。
CLIPD2D_TEST(ClipboardDibIngestNormalizesReservedByteEndToEnd) {
  const auto temp_dir = clipd2d::tests::CreateUniqueTempDirectory(L"clipdib-alpha-ingest");
  auto cleanup = [&]() { clipd2d::tests::DeleteDirectoryIfExists(temp_dir); };
  const auto paths = MakeTempAppPaths(temp_dir);
  std::filesystem::create_directories(paths.image_dir);

  const auto run_ingest = [&](std::uint8_t fourth_byte, const wchar_t* file_name, std::string* hash) {
    clipd2d::core::store::CapturedClip clip;
    clip.content_type = clipd2d::core::store::ClipContentType::Image;
    clip.title = "Image";
    clip.content_hash = "pendinghash";
    clip.ingest_bytes = Make32bppDib(4, 4, fourth_byte);
    clip.ingest_format = "CF_DIB";
    clip.storage_path = (std::filesystem::path(paths.image_dir) / file_name).wstring();
    clip.thumbnail_path = (std::filesystem::path(paths.image_dir) / (std::wstring(file_name) + L"_thumb.png")).wstring();
    clip.preview_path = clip.storage_path;
    clip.image_width = 4;
    clip.image_height = 4;

    std::string normalized_hash;
    int width = 0;
    int height = 0;
    const auto status = clipd2d::core::clipboard::NormalizePendingImage(
        paths, clip, &normalized_hash, &width, &height);
    *hash = normalized_hash;
    return status.ok() && std::filesystem::exists(clip.storage_path);
  };

  std::string reserved_zero_hash;
  std::string opaque_hash;
  const bool zero_ok = run_ingest(0x00, L"reserved_zero.png", &reserved_zero_hash);
  const bool opaque_ok = run_ingest(0xFF, L"opaque.png", &opaque_hash);
  cleanup();

  CLIPD2D_CHECK(zero_ok);
  CLIPD2D_CHECK(opaque_ok);
  CLIPD2D_CHECK(!reserved_zero_hash.empty());
  CLIPD2D_CHECK_EQ(reserved_zero_hash, opaque_hash);
}

// --- WIC 编码辅助：把 BGRA 像素编码成 PNG 字节（模拟第三方工具放进剪贴板的负载）---
namespace {

std::vector<std::uint8_t> EncodePngBytes(
    const std::vector<std::uint8_t>& bgra,
    int width,
    int height) {
  IWICImagingFactory* factory = nullptr;
  if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
    return {};
  }
  if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) {
    CoUninitialize();
    return {};
  }
  IWICBitmap* bitmap = nullptr;
  std::vector<std::uint8_t> png;
  if (SUCCEEDED(factory->CreateBitmapFromMemory(
          static_cast<UINT>(width), static_cast<UINT>(height), GUID_WICPixelFormat32bppBGRA,
          static_cast<UINT>(width) * 4u, bgra.size(), const_cast<BYTE*>(bgra.data()), &bitmap))) {
    IWICStream* stream = nullptr;
    if (SUCCEEDED(factory->CreateStream(&stream))) {
      // 编码到内存：用 std::string 承载 IStream
      struct MemStream : IStream {
        std::string data;
        ULONG ref_count = 1;
        STDMETHODIMP QueryInterface(REFIID riid, void** out) override {
          if (riid == IID_IUnknown || riid == IID_IStream || riid == IID_ISequentialStream) {
            *out = static_cast<IStream*>(this);
            AddRef();
            return S_OK;
          }
          return E_NOINTERFACE;
        }
        STDMETHODIMP_(ULONG) AddRef() override { return ++ref_count; }
        STDMETHODIMP_(ULONG) Release() override { return --ref_count; }
        STDMETHODIMP Read(void*, ULONG, ULONG*) override { return E_NOTIMPL; }
        STDMETHODIMP Write(const void* pv, ULONG cb, ULONG* written) override {
          data.append(static_cast<const char*>(pv), cb);
          if (written != nullptr) *written = cb;
          return S_OK;
        }
        STDMETHODIMP Seek(LARGE_INTEGER, DWORD, ULARGE_INTEGER*) override { return S_OK; }
        STDMETHODIMP SetSize(ULARGE_INTEGER) override { return S_OK; }
        STDMETHODIMP CopyTo(IStream*, ULARGE_INTEGER, ULARGE_INTEGER*, ULARGE_INTEGER*) override { return E_NOTIMPL; }
        STDMETHODIMP Commit(DWORD) override { return S_OK; }
        STDMETHODIMP Revert() override { return S_OK; }
        STDMETHODIMP LockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return E_NOTIMPL; }
        STDMETHODIMP UnlockRegion(ULARGE_INTEGER, ULARGE_INTEGER, DWORD) override { return E_NOTIMPL; }
        STDMETHODIMP Stat(STATSTG*, DWORD) override { return E_NOTIMPL; }
        STDMETHODIMP Clone(IStream**) override { return E_NOTIMPL; }
      };
      MemStream* mem = new MemStream();
      IStream* stream_iface = static_cast<IStream*>(mem);
      IWICBitmapEncoder* encoder = nullptr;
      if (SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) &&
          SUCCEEDED(encoder->Initialize(stream_iface, WICBitmapEncoderNoCache))) {
        IWICBitmapFrameEncode* frame = nullptr;
        IPropertyBag2* props = nullptr;
        if (SUCCEEDED(encoder->CreateNewFrame(&frame, &props)) && SUCCEEDED(frame->Initialize(props)) &&
            SUCCEEDED(frame->SetSize(static_cast<UINT>(width), static_cast<UINT>(height)))) {
          WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
          if (SUCCEEDED(frame->SetPixelFormat(&fmt)) &&
              SUCCEEDED(frame->WritePixels(
                  static_cast<UINT>(height),
                  static_cast<UINT>(width) * 4u,
                  static_cast<UINT>(bgra.size()),
                  const_cast<BYTE*>(bgra.data()))) &&
              SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit())) {
            png.assign(mem->data.begin(), mem->data.end());
          }
        }
        if (frame != nullptr) frame->Release();
        if (props != nullptr) props->Release();
        encoder->Release();
      }
      stream_iface->Release();
    }
    bitmap->Release();
  }
  factory->Release();
  CoUninitialize();
  return png;
}

}  // namespace

// 回归（PNG 路径）：Snipaste / PixPin 这类工具放进剪贴板的 PNG 本身就是全零 alpha。
// 修复前只有 DIB 分支归一化，PNG 分支原样落盘，于是库里出现整张透明的「空白图」，
// 且采集侧身份（含 alpha=0）与落盘侧身份分叉，写回后被当成新条目跳到首位。
CLIPD2D_TEST(ClipboardPngIngestNormalizesDegenerateAlphaEndToEnd) {
  const std::vector<std::uint8_t> base = {
      0x10, 0x20, 0x30, 0x00, 0x40, 0x50, 0x60, 0x00,
      0x70, 0x80, 0x90, 0x00, 0xA0, 0xB0, 0xC0, 0x00};  // 2x2，alpha 全 0

  const auto transparent_png = EncodePngBytes(base, 2, 2);
  CLIPD2D_CHECK(!transparent_png.empty());
  if (transparent_png.empty()) {
    return;
  }

  auto opaque = base;
  for (size_t index = 0; index < opaque.size(); index += 4) {
    opaque[index + 3] = 0xFF;
  }
  const auto opaque_png = EncodePngBytes(opaque, 2, 2);
  CLIPD2D_CHECK(!opaque_png.empty());

  // 身份层：两个 PNG 必须算出同一个身份（归一化必须发生在哈希之前）。
  std::string transparent_hash;
  std::string opaque_hash;
  int width = 0;
  int height = 0;
  CLIPD2D_CHECK(clipd2d::core::clipboard::ComputeCanonicalImageContentHashFromBytes(
      "PNG", transparent_png, &transparent_hash, &width, &height).ok());
  CLIPD2D_CHECK(clipd2d::core::clipboard::ComputeCanonicalImageContentHashFromBytes(
      "PNG", opaque_png, &opaque_hash, &width, &height).ok());
  CLIPD2D_CHECK_EQ(transparent_hash, opaque_hash);

  // 落盘层：走完整 ingest，落盘产物身份必须与「显式不透明版」一致。
  const auto temp_dir = clipd2d::tests::CreateUniqueTempDirectory(L"clippng-alpha-ingest");
  auto cleanup = [&]() { clipd2d::tests::DeleteDirectoryIfExists(temp_dir); };
  const auto paths = MakeTempAppPaths(temp_dir);
  std::filesystem::create_directories(paths.image_dir);

  const auto run_ingest = [&](const std::vector<std::uint8_t>& png_bytes, const wchar_t* name) {
    clipd2d::core::store::CapturedClip clip;
    clip.content_type = clipd2d::core::store::ClipContentType::Image;
    clip.title = "Image";
    clip.content_hash = "pendinghash";
    clip.ingest_bytes = png_bytes;
    clip.ingest_format = "PNG";
    clip.storage_path = (std::filesystem::path(paths.image_dir) / name).wstring();
    clip.thumbnail_path =
        (std::filesystem::path(paths.image_dir) / (std::wstring(name) + L"_thumb.png")).wstring();
    clip.preview_path = clip.storage_path;
    clip.image_width = 2;
    clip.image_height = 2;
    std::string hash;
    int w = 0;
    int h = 0;
    const bool ok = clipd2d::core::clipboard::NormalizePendingImage(paths, clip, &hash, &w, &h).ok();
    return ok ? hash : std::string();
  };

  const std::string stored_transparent = run_ingest(transparent_png, L"transparent.png");
  const std::string stored_opaque = run_ingest(opaque_png, L"opaque.png");
  cleanup();

  CLIPD2D_CHECK(!stored_transparent.empty());
  CLIPD2D_CHECK(!stored_opaque.empty());
  CLIPD2D_CHECK_EQ(stored_transparent, stored_opaque);
}
