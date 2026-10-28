#include "core/clipboard/ImagePng.h"

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
#include "core/clipboard/ImageIdentity.h"
#include "common/FileHash.h"

namespace clipd2d::core::clipboard {

bool WriteBytesToFile(const std::wstring& path, const std::vector<std::uint8_t>& bytes) {
  if (path.empty() || bytes.empty()) {
    return false;
  }

  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output.is_open()) {
    return false;
  }
  output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  return output.good();
}

// INVARIANT: 全模块唯一的 PNG 编码入口。
// 输入必须是 **straight BGRA**（32bppBGRA，非预乘）——与身份哈希（ImageIdentity）
// 和 DIB 像素提取（ImageDib::ExtractBitmapPixels）是同一种像素布局，
// 保证「算身份用的像素」与「落盘用的像素」永远一致。
common::Status EncodePngFromPixels(
    const std::uint8_t* pixels,
    int width,
    int height,
    const std::wstring& path) {
  if (pixels == nullptr || width <= 0 || height <= 0) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "Invalid pixel buffer for PNG encode.");
  }

  IWICImagingFactory* factory = nullptr;
  HRESULT hr = CoCreateInstance(
      CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
  if (FAILED(hr)) {
    return common::Status::Error(common::StatusCode::PlatformError, "Create WIC factory failed.");
  }

  IWICBitmap* wic_bitmap = nullptr;
  hr = factory->CreateBitmapFromMemory(
      static_cast<UINT>(width),
      static_cast<UINT>(height),
      GUID_WICPixelFormat32bppBGRA,
      static_cast<UINT>(width) * 4u,
      static_cast<UINT>(width) * 4u * static_cast<UINT>(height),
      const_cast<BYTE*>(pixels),
      &wic_bitmap);
  if (FAILED(hr)) {
    factory->Release();
    return common::Status::Error(common::StatusCode::PlatformError, "Create WIC bitmap from pixels failed.");
  }

  IWICStream* stream = nullptr;
  if (SUCCEEDED(hr)) hr = factory->CreateStream(&stream);
  if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
  IWICBitmapEncoder* encoder = nullptr;
  if (SUCCEEDED(hr)) hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
  if (SUCCEEDED(hr)) hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
  IWICBitmapFrameEncode* frame = nullptr;
  IPropertyBag2* props = nullptr;
  if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(&frame, &props);
  if (SUCCEEDED(hr)) hr = frame->Initialize(props);
  if (SUCCEEDED(hr)) hr = frame->WriteSource(wic_bitmap, nullptr);
  if (SUCCEEDED(hr)) hr = frame->Commit();
  if (SUCCEEDED(hr)) hr = encoder->Commit();

  if (props != nullptr) props->Release();
  if (frame != nullptr) frame->Release();
  if (encoder != nullptr) encoder->Release();
  if (stream != nullptr) stream->Release();
  wic_bitmap->Release();
  factory->Release();

  if (FAILED(hr)) {
    return common::Status::Error(common::StatusCode::PlatformError, "Write PNG file failed.");
  }
  return common::Status::Ok();
}

common::Status SaveBitmapToPng(
    HBITMAP bitmap,
    const std::wstring& path,
    std::string* hash_out,
    int* width,
    int* height) {
  if (hash_out == nullptr || width == nullptr || height == nullptr) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "Invalid bitmap PNG output.");
  }

  std::vector<std::uint8_t> pixels;
  common::Status status = ExtractBitmapPixels(bitmap, &pixels, width, height);
  if (!status.ok()) {
    return status;
  }
  // 第三方截图工具普遍把 DIB 保留位 / PNG alpha 写成 0；不归一化会导出全透明 PNG。
  ForceOpaqueIfAlphaDegenerate(*width, *height, pixels.data(), static_cast<size_t>(*width) * 4u);

  status = ComputeImageContentHashFromCanonicalPixels(
      *width,
      *height,
      std::string_view(reinterpret_cast<const char*>(pixels.data()), pixels.size()),
      hash_out);
  if (!status.ok()) {
    return status;
  }
  return EncodePngFromPixels(pixels.data(), *width, *height, path);
}


common::Status SaveDibToPng(
    const std::vector<std::uint8_t>& dib_bytes,
    const std::wstring& path,
    std::string* hash_out,
    int* width,
    int* height) {
  if (dib_bytes.size() < sizeof(BITMAPINFOHEADER)) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "DIB data is too small.");
  }

  const auto* header = reinterpret_cast<const BITMAPINFOHEADER*>(dib_bytes.data());
  if (header->biSize < sizeof(BITMAPINFOHEADER) || dib_bytes.size() < header->biSize) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "DIB header is invalid.");
  }

  const size_t pixel_offset = static_cast<size_t>(header->biSize) + DibColorTableBytes(*header);
  if (pixel_offset >= dib_bytes.size()) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "DIB payload is incomplete.");
  }

  const auto* bitmap_info = reinterpret_cast<const BITMAPINFO*>(dib_bytes.data());
  const void* pixels = dib_bytes.data() + pixel_offset;

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

  const common::Status status = SaveBitmapToPng(bitmap, path, hash_out, width, height);
  DeleteObject(bitmap);
  return status;
}

common::Status SaveImageFileToPng(
    const std::wstring& source_path,
    const std::wstring& output_path,
    std::string* hash_out,
    int* width,
    int* height) {
  IWICImagingFactory* factory = nullptr;
  HRESULT hr = CoCreateInstance(
      CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
  if (FAILED(hr)) {
    return common::Status::Error(common::StatusCode::PlatformError, "Create WIC factory failed.");
  }

  IWICBitmapDecoder* decoder = nullptr;
  hr = factory->CreateDecoderFromFilename(
      source_path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);
  IWICBitmapFrameDecode* frame = nullptr;
  if (SUCCEEDED(hr)) hr = decoder->GetFrame(0, &frame);
  IWICFormatConverter* converter = nullptr;
  if (SUCCEEDED(hr)) hr = factory->CreateFormatConverter(&converter);
  if (SUCCEEDED(hr)) {
    // 必须转成 straight BGRA（不是 PBGRA）：与 DIB 路径的像素布局一致，
    // 否则带透明的图会在「身份哈希」和「落盘」两侧得到不同像素。
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
    factory->Release();
    return common::Status::Error(common::StatusCode::PlatformError, "Decode image file failed.");
  }

  UINT source_width = 0;
  UINT source_height = 0;
  hr = converter->GetSize(&source_width, &source_height);
  if (FAILED(hr) || source_width == 0 || source_height == 0) {
    converter->Release();
    frame->Release();
    decoder->Release();
    factory->Release();
    return common::Status::Error(common::StatusCode::PlatformError, "Read image size failed.");
  }

  if (width != nullptr) {
    *width = static_cast<int>(source_width);
  }
  if (height != nullptr) {
    *height = static_cast<int>(source_height);
  }

  const UINT stride = source_width * 4u;
  const UINT image_size = stride * source_height;
  std::vector<std::uint8_t> pixels(image_size);
  hr = converter->CopyPixels(nullptr, stride, image_size, pixels.data());
  converter->Release();
  frame->Release();
  decoder->Release();
  factory->Release();
  if (FAILED(hr)) {
    return common::Status::Error(common::StatusCode::PlatformError, "Copy image pixels failed.");
  }

  // 关键：剪贴板里来的 PNG 可能整张 alpha=0（生成方的保留位 bug）。
  // 不归一化就会把「空白图」原样存进库里。
  ForceOpaqueIfAlphaDegenerate(
      static_cast<int>(source_width),
      static_cast<int>(source_height),
      pixels.data(),
      stride);

  if (hash_out != nullptr) {
    const common::Status hash_status = ComputeImageContentHashFromCanonicalPixels(
        static_cast<int>(source_width),
        static_cast<int>(source_height),
        std::string_view(reinterpret_cast<const char*>(pixels.data()), pixels.size()),
        hash_out);
    if (!hash_status.ok()) {
      return hash_status;
    }
  }

  return EncodePngFromPixels(
      pixels.data(),
      static_cast<int>(source_width),
      static_cast<int>(source_height),
      output_path);
}


common::Status SaveScaledImageFileToPng(
    const std::wstring& source_path,
    const std::wstring& output_path,
    int max_width,
    int max_height) {
  IWICImagingFactory* factory = nullptr;
  HRESULT hr = CoCreateInstance(
      CLSID_WICImagingFactory,
      nullptr,
      CLSCTX_INPROC_SERVER,
      IID_PPV_ARGS(&factory));
  if (FAILED(hr)) {
    return common::Status::Error(common::StatusCode::PlatformError, "Create WIC factory failed.");
  }

  IWICBitmapDecoder* decoder = nullptr;
  hr = factory->CreateDecoderFromFilename(
      source_path.c_str(),
      nullptr,
      GENERIC_READ,
      WICDecodeMetadataCacheOnLoad,
      &decoder);
  if (FAILED(hr)) {
    factory->Release();
    return common::Status::Error(common::StatusCode::NotFound, "Open image file failed.");
  }

  IWICBitmapFrameDecode* frame = nullptr;
  hr = decoder->GetFrame(0, &frame);
  UINT source_width = 0;
  UINT source_height = 0;
  if (SUCCEEDED(hr)) {
    hr = frame->GetSize(&source_width, &source_height);
  }
  if (FAILED(hr) || source_width == 0 || source_height == 0) {
    if (frame != nullptr) frame->Release();
    decoder->Release();
    factory->Release();
    return common::Status::Error(common::StatusCode::PlatformError, "Read image size failed.");
  }

  const double scale_x = max_width > 0 ? static_cast<double>(max_width) / static_cast<double>(source_width) : 1.0;
  const double scale_y = max_height > 0 ? static_cast<double>(max_height) / static_cast<double>(source_height) : 1.0;
  const double scale = std::min(1.0, std::min(scale_x, scale_y));
  const UINT scaled_width = std::max<UINT>(1, static_cast<UINT>(std::round(source_width * scale)));
  const UINT scaled_height = std::max<UINT>(1, static_cast<UINT>(std::round(source_height * scale)));

  IWICBitmapScaler* scaler = nullptr;
  hr = factory->CreateBitmapScaler(&scaler);
  if (SUCCEEDED(hr)) {
    hr = scaler->Initialize(frame, scaled_width, scaled_height, WICBitmapInterpolationModeFant);
  }

  IWICStream* stream = nullptr;
  if (SUCCEEDED(hr)) {
    hr = factory->CreateStream(&stream);
  }
  if (SUCCEEDED(hr)) {
    hr = stream->InitializeFromFilename(output_path.c_str(), GENERIC_WRITE);
  }

  IWICBitmapEncoder* encoder = nullptr;
  if (SUCCEEDED(hr)) {
    hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
  }
  if (SUCCEEDED(hr)) {
    hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
  }

  IWICBitmapFrameEncode* encoded_frame = nullptr;
  IPropertyBag2* property_bag = nullptr;
  if (SUCCEEDED(hr)) {
    hr = encoder->CreateNewFrame(&encoded_frame, &property_bag);
  }
  if (SUCCEEDED(hr)) {
    hr = encoded_frame->Initialize(property_bag);
  }
  if (SUCCEEDED(hr)) {
    hr = encoded_frame->SetSize(scaled_width, scaled_height);
  }
  WICPixelFormatGUID pixel_format = GUID_WICPixelFormat32bppBGRA;
  if (SUCCEEDED(hr)) {
    hr = encoded_frame->SetPixelFormat(&pixel_format);
  }
  if (SUCCEEDED(hr)) {
    hr = encoded_frame->WriteSource(scaler, nullptr);
  }
  if (SUCCEEDED(hr)) {
    hr = encoded_frame->Commit();
  }
  if (SUCCEEDED(hr)) {
    hr = encoder->Commit();
  }

  if (property_bag != nullptr) property_bag->Release();
  if (encoded_frame != nullptr) encoded_frame->Release();
  if (encoder != nullptr) encoder->Release();
  if (stream != nullptr) stream->Release();
  if (scaler != nullptr) scaler->Release();
  if (frame != nullptr) frame->Release();
  decoder->Release();
  factory->Release();

  if (FAILED(hr)) {
    DeleteFileW(output_path.c_str());
    return common::Status::Error(common::StatusCode::PlatformError, "Write scaled png failed.");
  }
  return common::Status::Ok();
}

common::Status SaveClipboardPngToPng(
    const std::vector<std::uint8_t>& png_bytes,
    const std::wstring& source_path,
    const std::wstring& output_path,
    std::string* hash_out,
    int* width,
    int* height) {
  if (png_bytes.empty()) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "PNG clipboard payload is empty.");
  }
  if (!WriteBytesToFile(source_path, png_bytes)) {
    return common::Status::Error(common::StatusCode::PlatformError, "Write temporary png source failed.");
  }

  const common::Status status = SaveImageFileToPng(source_path, output_path, hash_out, width, height);
  DeleteFileW(source_path.c_str());
  return status;
}

}  // namespace clipd2d::core::clipboard
