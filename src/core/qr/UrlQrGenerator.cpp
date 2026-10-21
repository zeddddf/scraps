#include "core/qr/UrlQrGenerator.h"

#include <windows.h>
#include <wincodec.h>

#include <algorithm>
#include <filesystem>
#include <vector>

#include "common/FileHash.h"
#include "common/StringUtil.h"
#include "common/Utf8.h"

extern "C" {
#include "qrcodegen.h"
}

namespace clipd2d::core::qr {

namespace {

constexpr int kQuietZoneModules = 4;
constexpr UINT kPreferredQrPixelSize = 384;

class ScopedComInit {
 public:
  ScopedComInit() {
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    should_uninitialize_ = hr == S_OK || hr == S_FALSE;
  }
  ~ScopedComInit() {
    if (should_uninitialize_) {
      CoUninitialize();
    }
  }

 private:
  bool should_uninitialize_ = false;
};

common::Status EnsureQrCacheDirectory(const common::AppPaths& paths, std::wstring* directory) {
  if (directory == nullptr) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "QR cache directory output cannot be null.");
  }
  const std::wstring derived_dir = paths.cache_dir + L"\\derived";
  common::Status status = common::EnsureDirectory(derived_dir);
  if (!status.ok()) {
    return status;
  }
  const std::wstring qr_dir = derived_dir + L"\\qr";
  status = common::EnsureDirectory(qr_dir);
  if (!status.ok()) {
    return status;
  }
  *directory = qr_dir;
  return common::Status::Ok();
}

common::Status WriteBgraPng(
    const std::wstring& path,
    UINT width,
    UINT height,
    const std::vector<std::uint8_t>& pixels) {
  ScopedComInit com_init;
  IWICImagingFactory* factory = nullptr;
  HRESULT hr = CoCreateInstance(
      CLSID_WICImagingFactory,
      nullptr,
      CLSCTX_INPROC_SERVER,
      IID_PPV_ARGS(&factory));
  if (FAILED(hr)) {
    return common::Status::Error(common::StatusCode::PlatformError, "Create WIC factory failed.");
  }

  IWICBitmap* bitmap = nullptr;
  hr = factory->CreateBitmapFromMemory(
      width,
      height,
      GUID_WICPixelFormat32bppBGRA,
      width * 4,
      static_cast<UINT>(pixels.size()),
      const_cast<BYTE*>(pixels.data()),
      &bitmap);
  if (FAILED(hr)) {
    factory->Release();
    return common::Status::Error(common::StatusCode::PlatformError, "Create QR bitmap failed.");
  }

  IWICStream* stream = nullptr;
  hr = factory->CreateStream(&stream);
  if (FAILED(hr)) {
    bitmap->Release();
    factory->Release();
    return common::Status::Error(common::StatusCode::PlatformError, "Create QR WIC stream failed.");
  }

  hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
  if (FAILED(hr)) {
    stream->Release();
    bitmap->Release();
    factory->Release();
    return common::Status::Error(common::StatusCode::PlatformError, "Open QR PNG output failed.");
  }

  IWICBitmapEncoder* encoder = nullptr;
  hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
  if (FAILED(hr)) {
    stream->Release();
    bitmap->Release();
    factory->Release();
    return common::Status::Error(common::StatusCode::PlatformError, "Create QR encoder failed.");
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
  if (SUCCEEDED(hr)) hr = frame->WriteSource(bitmap, nullptr);
  if (SUCCEEDED(hr)) hr = frame->Commit();
  if (SUCCEEDED(hr)) hr = encoder->Commit();

  if (property_bag != nullptr) property_bag->Release();
  if (frame != nullptr) frame->Release();
  encoder->Release();
  stream->Release();
  bitmap->Release();
  factory->Release();

  if (FAILED(hr)) {
    return common::Status::Error(common::StatusCode::PlatformError, "Write QR PNG failed.");
  }
  return common::Status::Ok();
}

}  // namespace

common::Status GenerateUrlQrPng(
    const common::AppPaths& paths,
    std::wstring_view url,
    std::wstring* output_path) {
  if (output_path == nullptr) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "QR output path cannot be null.");
  }
  const std::wstring trimmed = common::TrimWide(url);
  if (trimmed.empty()) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "QR source URL is empty.");
  }

  std::wstring qr_directory;
  common::Status status = EnsureQrCacheDirectory(paths, &qr_directory);
  if (!status.ok()) {
    return status;
  }

  const common::Utf8Conversion utf8 = common::WideToUtf8(trimmed);
  if (!utf8.status.ok()) {
    return utf8.status;
  }

  const std::wstring output = qr_directory + L"\\" + common::Utf8ToWide(common::HashBytes(utf8.utf8)).wide + L".png";
  if (std::filesystem::exists(output)) {
    *output_path = output;
    return common::Status::Ok();
  }

  uint8_t qrcode[qrcodegen_BUFFER_LEN_FOR_VERSION(qrcodegen_VERSION_MAX)];
  uint8_t temp_buffer[qrcodegen_BUFFER_LEN_FOR_VERSION(qrcodegen_VERSION_MAX)];
  const bool encoded = qrcodegen_encodeText(
      utf8.utf8.c_str(),
      temp_buffer,
      qrcode,
      qrcodegen_Ecc_MEDIUM,
      qrcodegen_VERSION_MIN,
      qrcodegen_VERSION_MAX,
      qrcodegen_Mask_AUTO,
      true);
  if (!encoded) {
    return common::Status::Error(common::StatusCode::InternalError, "Generate QR code failed.");
  }

  const int qr_width = qrcodegen_getSize(qrcode);
  const int module_count = qr_width + kQuietZoneModules * 2;
  const UINT scale = std::max<UINT>(4, kPreferredQrPixelSize / static_cast<UINT>(std::max(1, module_count)));
  const UINT image_size = static_cast<UINT>(module_count) * scale;
  std::vector<std::uint8_t> pixels(static_cast<size_t>(image_size) * static_cast<size_t>(image_size) * 4u, 0xFF);

  for (int y = 0; y < qr_width; ++y) {
    for (int x = 0; x < qr_width; ++x) {
      if (!qrcodegen_getModule(qrcode, x, y)) {
        continue;
      }
      const UINT left = static_cast<UINT>(x + kQuietZoneModules) * scale;
      const UINT top = static_cast<UINT>(y + kQuietZoneModules) * scale;
      for (UINT dy = 0; dy < scale; ++dy) {
        for (UINT dx = 0; dx < scale; ++dx) {
          const size_t index = (static_cast<size_t>(top + dy) * image_size + static_cast<size_t>(left + dx)) * 4u;
          pixels[index + 0] = 0x00;
          pixels[index + 1] = 0x00;
          pixels[index + 2] = 0x00;
          pixels[index + 3] = 0xFF;
        }
      }
    }
  }

  status = WriteBgraPng(output, image_size, image_size, pixels);
  if (!status.ok()) {
    return status;
  }
  *output_path = output;
  return common::Status::Ok();
}

}  // namespace clipd2d::core::qr
