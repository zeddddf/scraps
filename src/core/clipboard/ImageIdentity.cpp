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

#include "core/clipboard/ImageDib.h"
#include "core/clipboard/ClipboardFormat.h"
#include "common/FileHash.h"

namespace clipd2d::core::clipboard {

void AppendUint32LittleEndian(std::string* payload, std::uint32_t value) {
  payload->push_back(static_cast<char>(value & 0xFFu));
  payload->push_back(static_cast<char>((value >> 8u) & 0xFFu));
  payload->push_back(static_cast<char>((value >> 16u) & 0xFFu));
  payload->push_back(static_cast<char>((value >> 24u) & 0xFFu));
}

common::Status ComputeImageContentHashFromCanonicalPixels(
    int width,
    int height,
    std::string_view pixel_bytes,
    std::string* hash_out) {
  if (hash_out == nullptr) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "Invalid image hash output.");
  }
  if (width <= 0 || height <= 0 || pixel_bytes.empty()) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "Invalid image hash payload.");
  }

  // CONSTRAINT: Image identity v4 includes dimensions before pixels - otherwise
  // distinct images such as 1x4 and 2x2 can share the same raw pixel byte stream.
  std::string payload;
  payload.reserve(32u + pixel_bytes.size());
  payload.append("clipd2d:image:v4");
  payload.push_back('\0');
  AppendUint32LittleEndian(&payload, static_cast<std::uint32_t>(width));
  AppendUint32LittleEndian(&payload, static_cast<std::uint32_t>(height));
  payload.append(pixel_bytes.data(), pixel_bytes.size());

  const std::string hash = common::Sha256Hex(payload);
  if (hash.empty()) {
    return common::Status::Error(common::StatusCode::PlatformError, "Compute SHA-256 image hash failed.");
  }
  *hash_out = hash;
  return common::Status::Ok();
}

common::Status ReadImageBytesMetadata(
    const std::vector<std::uint8_t>& encoded_bytes,
    int* width,
    int* height) {
  if (width == nullptr || height == nullptr) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "Invalid image metadata output.");
  }
  if (encoded_bytes.empty()) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "Encoded image bytes are empty.");
  }

  *width = 0;
  *height = 0;

  IWICImagingFactory* factory = nullptr;
  HRESULT hr = CoCreateInstance(
      CLSID_WICImagingFactory,
      nullptr,
      CLSCTX_INPROC_SERVER,
      IID_PPV_ARGS(&factory));
  if (FAILED(hr)) {
    return common::Status::Error(common::StatusCode::PlatformError, "Create WIC factory failed.");
  }

  IWICStream* stream = nullptr;
  hr = factory->CreateStream(&stream);
  if (FAILED(hr) || stream == nullptr) {
    factory->Release();
    return common::Status::Error(common::StatusCode::PlatformError, "Create WIC stream failed.");
  }

  hr = stream->InitializeFromMemory(
      const_cast<BYTE*>(encoded_bytes.data()),
      static_cast<DWORD>(encoded_bytes.size()));
  if (FAILED(hr)) {
    stream->Release();
    factory->Release();
    return common::Status::Error(common::StatusCode::PlatformError, "Initialize WIC stream failed.");
  }

  IWICBitmapDecoder* decoder = nullptr;
  hr = factory->CreateDecoderFromStream(
      stream,
      nullptr,
      WICDecodeMetadataCacheOnDemand,
      &decoder);
  if (FAILED(hr)) {
    stream->Release();
    factory->Release();
    return common::Status::Error(common::StatusCode::NotFound, "Open image stream failed.");
  }

  IWICBitmapFrameDecode* frame = nullptr;
  hr = decoder->GetFrame(0, &frame);
  if (SUCCEEDED(hr)) {
    UINT file_width = 0;
    UINT file_height = 0;
    hr = frame->GetSize(&file_width, &file_height);
    if (SUCCEEDED(hr)) {
      *width = static_cast<int>(file_width);
      *height = static_cast<int>(file_height);
    }
  }

  if (frame != nullptr) {
    frame->Release();
  }
  decoder->Release();
  stream->Release();
  factory->Release();

  if (FAILED(hr) || *width <= 0 || *height <= 0) {
    return common::Status::Error(common::StatusCode::PlatformError, "Read image metadata failed.");
  }
  return common::Status::Ok();
}

common::Status ComputeCanonicalImageContentHashFromBytesImpl(
    std::string_view format_name,
    const std::vector<std::uint8_t>& image_bytes,
    std::string* content_hash,
    int* width,
    int* height) {
  if (content_hash == nullptr || width == nullptr || height == nullptr) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "Invalid canonical image hash output.");
  }
  if (image_bytes.empty()) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "Encoded image bytes are empty.");
  }

  *content_hash = {};
  *width = 0;
  *height = 0;

  if (format_name == "PNG") {
    const HRESULT init_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool need_uninit = init_result == S_OK || init_result == S_FALSE;
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

    IWICStream* stream = nullptr;
    hr = factory->CreateStream(&stream);
    if (FAILED(hr) || stream == nullptr) {
      factory->Release();
      if (need_uninit) {
        CoUninitialize();
      }
      return common::Status::Error(common::StatusCode::PlatformError, "Create WIC stream failed.");
    }

    hr = stream->InitializeFromMemory(
        const_cast<BYTE*>(image_bytes.data()),
        static_cast<DWORD>(image_bytes.size()));
    if (FAILED(hr)) {
      stream->Release();
      factory->Release();
      if (need_uninit) {
        CoUninitialize();
      }
      return common::Status::Error(common::StatusCode::PlatformError, "Initialize WIC stream failed.");
    }

    IWICBitmapDecoder* decoder = nullptr;
    hr = factory->CreateDecoderFromStream(
        stream,
        nullptr,
        WICDecodeMetadataCacheOnDemand,
        &decoder);
    if (FAILED(hr) || decoder == nullptr) {
      stream->Release();
      factory->Release();
      if (need_uninit) {
        CoUninitialize();
      }
      return common::Status::Error(common::StatusCode::NotFound, "Open image stream failed.");
    }

    IWICBitmapFrameDecode* frame = nullptr;
    hr = decoder->GetFrame(0, &frame);
    IWICFormatConverter* converter = nullptr;
    if (SUCCEEDED(hr)) {
      hr = factory->CreateFormatConverter(&converter);
    }
    if (SUCCEEDED(hr)) {
      // straight BGRA（非预乘）：与 DIB 路径（GetDIBits）的像素布局一致，
      // 否则同一张图在 PNG 来源与 DIB 来源会算出不同身份。
      hr = converter->Initialize(
          frame,
          GUID_WICPixelFormat32bppBGRA,
          WICBitmapDitherTypeNone,
          nullptr,
          0.0,
          WICBitmapPaletteTypeCustom);
    }
    if (FAILED(hr)) {
      if (converter != nullptr) converter->Release();
      if (frame != nullptr) frame->Release();
      decoder->Release();
      stream->Release();
      factory->Release();
      if (need_uninit) {
        CoUninitialize();
      }
      return common::Status::Error(common::StatusCode::PlatformError, "Decode image stream failed.");
    }

    UINT source_width = 0;
    UINT source_height = 0;
    hr = converter->GetSize(&source_width, &source_height);
    if (FAILED(hr) || source_width == 0 || source_height == 0) {
      converter->Release();
      frame->Release();
      decoder->Release();
      stream->Release();
      factory->Release();
      if (need_uninit) {
        CoUninitialize();
      }
      return common::Status::Error(common::StatusCode::PlatformError, "Read image size failed.");
    }

    *width = static_cast<int>(source_width);
    *height = static_cast<int>(source_height);
    const UINT stride = source_width * 4;
    const UINT image_size = stride * source_height;
    std::vector<std::byte> pixels(image_size);
    hr = converter->CopyPixels(nullptr, stride, image_size, reinterpret_cast<BYTE*>(pixels.data()));
    // 与落盘路径同一份归一化：alpha 全 0 的 PNG 一律视为不透明。
    // 若只在落盘侧归一化，采集侧身份与落盘侧身份会分叉，写回后被当成新条目。
    ForceOpaqueIfAlphaDegenerate(
        static_cast<int>(source_width),
        static_cast<int>(source_height),
        reinterpret_cast<std::uint8_t*>(pixels.data()),
        stride);
    converter->Release();
    frame->Release();
    decoder->Release();
    stream->Release();
    factory->Release();
    if (FAILED(hr)) {
      if (need_uninit) {
        CoUninitialize();
      }
      return common::Status::Error(common::StatusCode::PlatformError, "Copy image pixels failed.");
    }

    const common::Status hash_status = ComputeImageContentHashFromCanonicalPixels(
        *width,
        *height,
        std::string_view(reinterpret_cast<const char*>(pixels.data()), pixels.size()),
        content_hash);
    if (!hash_status.ok()) {
      if (need_uninit) {
        CoUninitialize();
      }
      return hash_status;
    }
    if (need_uninit) {
      CoUninitialize();
    }
    return common::Status::Ok();
  }

  if (format_name == "CF_DIB" || format_name == "CF_DIBV5" || format_name == "CF_BITMAP_DIB") {
    if (image_bytes.size() < sizeof(BITMAPINFOHEADER)) {
      return common::Status::Error(common::StatusCode::InvalidArgument, "DIB data is too small.");
    }

    const auto* header = reinterpret_cast<const BITMAPINFOHEADER*>(image_bytes.data());
    if (header->biSize < sizeof(BITMAPINFOHEADER) || image_bytes.size() < header->biSize) {
      return common::Status::Error(common::StatusCode::InvalidArgument, "DIB header is invalid.");
    }

    const size_t pixel_offset = static_cast<size_t>(header->biSize) + DibColorTableBytes(*header);
    if (pixel_offset >= image_bytes.size()) {
      return common::Status::Error(common::StatusCode::InvalidArgument, "DIB payload is incomplete.");
    }

    const auto* bitmap_info = reinterpret_cast<const BITMAPINFO*>(image_bytes.data());
    const void* pixels = image_bytes.data() + pixel_offset;

    HDC screen = GetDC(nullptr);
    if (screen == nullptr) {
      return common::Status::Error(common::StatusCode::PlatformError, "GetDC failed.");
    }
    HBITMAP bitmap = CreateDIBitmap(
        screen,
        &bitmap_info->bmiHeader,
        CBM_INIT,
        pixels,
        bitmap_info,
        DIB_RGB_COLORS);
    ReleaseDC(nullptr, screen);
    if (bitmap == nullptr) {
      return common::Status::Error(common::StatusCode::PlatformError, "CreateDIBitmap failed.");
    }

    const common::Status status = ComputeBitmapPixelHash(bitmap, content_hash, width, height);
    DeleteObject(bitmap);
    return status;
  }

  return common::Status::Error(common::StatusCode::NotImplemented, "Unsupported canonical image format.");
}

common::Status ComputeCanonicalImageContentHashFromBytes(
    std::string_view format_name,
    const std::vector<std::uint8_t>& image_bytes,
    std::string* content_hash,
    int* width,
    int* height) {
  return ComputeCanonicalImageContentHashFromBytesImpl(
      format_name,
      image_bytes,
      content_hash,
      width,
      height);
}

}  // namespace clipd2d::core::clipboard
