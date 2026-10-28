#include "core/clipboard/ImageWriteback.h"

#include <windows.h>
#include <wincodec.h>

#include <filesystem>
#include <fstream>
#include <vector>

#include "common/Status.h"
#include "common/StringUtil.h"
#include "core/clipboard/ClipboardFormat.h"
#include "core/clipboard/ClipboardSession.h"

namespace clipd2d::core::clipboard {

namespace {

common::Status LoadBitmapFromPng(const std::wstring& path, HBITMAP* bitmap) {
  if (bitmap == nullptr) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "Bitmap output cannot be null.");
  }
  *bitmap = nullptr;

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
      path.c_str(),
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
  IWICFormatConverter* converter = nullptr;
  if (SUCCEEDED(hr)) {
    hr = factory->CreateFormatConverter(&converter);
  }
  if (SUCCEEDED(hr)) {
    hr = converter->Initialize(
        frame,
        GUID_WICPixelFormat32bppPBGRA,
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

  UINT width = 0;
  UINT height = 0;
  hr = converter->GetSize(&width, &height);
  if (FAILED(hr) || width == 0 || height == 0) {
    converter->Release();
    frame->Release();
    decoder->Release();
    factory->Release();
    return common::Status::Error(common::StatusCode::PlatformError, "Read image size failed.");
  }

  BITMAPINFO bitmap_info{};
  bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bitmap_info.bmiHeader.biWidth = static_cast<LONG>(width);
  bitmap_info.bmiHeader.biHeight = -static_cast<LONG>(height);
  bitmap_info.bmiHeader.biPlanes = 1;
  bitmap_info.bmiHeader.biBitCount = 32;
  bitmap_info.bmiHeader.biCompression = BI_RGB;

  void* dib_pixels = nullptr;
  HDC screen = GetDC(nullptr);
  HBITMAP created_bitmap = CreateDIBSection(
      screen,
      &bitmap_info,
      DIB_RGB_COLORS,
      &dib_pixels,
      nullptr,
      0);
  ReleaseDC(nullptr, screen);
  if (created_bitmap == nullptr || dib_pixels == nullptr) {
    converter->Release();
    frame->Release();
    decoder->Release();
    factory->Release();
    return common::Status::Error(common::StatusCode::PlatformError, "Create DIB section failed.");
  }

  const UINT stride = width * 4;
  const UINT image_size = stride * height;
  hr = converter->CopyPixels(nullptr, stride, image_size, static_cast<BYTE*>(dib_pixels));
  converter->Release();
  frame->Release();
  decoder->Release();
  factory->Release();
  if (FAILED(hr)) {
    DeleteObject(created_bitmap);
    return common::Status::Error(common::StatusCode::PlatformError, "Copy image pixels failed.");
  }

  *bitmap = created_bitmap;
  return common::Status::Ok();
}

common::Status ExtractBitmapPixels(
    HBITMAP bitmap,
    UINT* width,
    UINT* height,
    std::vector<std::uint8_t>* pixels) {
  if (bitmap == nullptr || width == nullptr || height == nullptr || pixels == nullptr) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "Bitmap extract output is invalid.");
  }

  *width = 0;
  *height = 0;
  pixels->clear();

  BITMAP info{};
  if (GetObjectW(bitmap, sizeof(info), &info) == 0 || info.bmWidth <= 0 || info.bmHeight <= 0) {
    return common::Status::Error(common::StatusCode::PlatformError, "Read bitmap metadata failed.");
  }

  BITMAPINFO bitmap_info{};
  bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bitmap_info.bmiHeader.biWidth = info.bmWidth;
  bitmap_info.bmiHeader.biHeight = -info.bmHeight;
  bitmap_info.bmiHeader.biPlanes = 1;
  bitmap_info.bmiHeader.biBitCount = 32;
  bitmap_info.bmiHeader.biCompression = BI_RGB;

  pixels->resize(static_cast<size_t>(info.bmWidth) * static_cast<size_t>(info.bmHeight) * 4u);
  HDC screen = GetDC(nullptr);
  if (screen == nullptr) {
    pixels->clear();
    return common::Status::Error(common::StatusCode::PlatformError, "GetDC failed.");
  }

  const int result = GetDIBits(
      screen,
      bitmap,
      0,
      static_cast<UINT>(info.bmHeight),
      pixels->data(),
      &bitmap_info,
      DIB_RGB_COLORS);
  ReleaseDC(nullptr, screen);
  if (result == 0) {
    pixels->clear();
    return common::Status::Error(common::StatusCode::PlatformError, "Extract bitmap pixels failed.");
  }

  *width = static_cast<UINT>(info.bmWidth);
  *height = static_cast<UINT>(info.bmHeight);
  return common::Status::Ok();
}

std::vector<std::uint8_t> BuildDibPayload(
    UINT width,
    UINT height,
    const std::vector<std::uint8_t>& pixels,
    bool v5_header) {
  if (width == 0 || height == 0 || pixels.empty()) {
    return {};
  }

  const size_t pixel_bytes = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
  if (pixels.size() < pixel_bytes) {
    return {};
  }

  if (v5_header) {
    std::vector<std::uint8_t> payload(sizeof(BITMAPV5HEADER) + pixel_bytes);
    auto* header = reinterpret_cast<BITMAPV5HEADER*>(payload.data());
    ZeroMemory(header, sizeof(BITMAPV5HEADER));
    header->bV5Size = sizeof(BITMAPV5HEADER);
    header->bV5Width = static_cast<LONG>(width);
    header->bV5Height = -static_cast<LONG>(height);
    header->bV5Planes = 1;
    header->bV5BitCount = 32;
    header->bV5Compression = BI_BITFIELDS;
    header->bV5RedMask = 0x00FF0000;
    header->bV5GreenMask = 0x0000FF00;
    header->bV5BlueMask = 0x000000FF;
    header->bV5AlphaMask = 0xFF000000;
    header->bV5CSType = LCS_sRGB;
    memcpy(payload.data() + sizeof(BITMAPV5HEADER), pixels.data(), pixel_bytes);
    return payload;
  }

  std::vector<std::uint8_t> payload(sizeof(BITMAPINFOHEADER) + pixel_bytes);
  auto* header = reinterpret_cast<BITMAPINFOHEADER*>(payload.data());
  ZeroMemory(header, sizeof(BITMAPINFOHEADER));
  header->biSize = sizeof(BITMAPINFOHEADER);
  header->biWidth = static_cast<LONG>(width);
  header->biHeight = -static_cast<LONG>(height);
  header->biPlanes = 1;
  header->biBitCount = 32;
  header->biCompression = BI_RGB;
  memcpy(payload.data() + sizeof(BITMAPINFOHEADER), pixels.data(), pixel_bytes);
  return payload;
}

// 图片写回的全部格式 payload。先在内存里备齐（WIC 解码、文件读取等重活
// 都不得在持有剪贴板打开状态时进行），再交给单次剪贴板会话一次性写入。
struct ImageFormatPayloads {
  std::vector<std::uint8_t> png_bytes;
  std::vector<std::uint8_t> dibv5_bytes;
  std::vector<std::uint8_t> dib_bytes;
  HBITMAP bitmap = nullptr;
};

// 内部原语：把备好的图片格式集合写入“当前已打开”的剪贴板会话。
// 绝不调用 OpenClipboard/EmptyClipboard/CloseClipboard——会话由调用方持有，
// 保证一次图片写回只含一次会话（系统 sequence 只因唯一的 EmptyClipboard +1）。
// CF_BITMAP 成功写入后所有权移交剪贴板（bitmap 置空），遗留句柄由调用方释放。
void WriteImagePayloadsToOpenSession(ImageFormatPayloads* payloads, bool* wrote_any) {
  if (!payloads->dibv5_bytes.empty()) {
    const common::Status dibv5_status = SetGlobalBinaryData(CF_DIBV5, payloads->dibv5_bytes);
    if (dibv5_status.ok()) {
      *wrote_any = true;
    }
  }
  if (!payloads->dib_bytes.empty()) {
    const common::Status dib_status = SetGlobalBinaryData(CF_DIB, payloads->dib_bytes);
    if (dib_status.ok()) {
      *wrote_any = true;
    }
  }
  if (!payloads->png_bytes.empty()) {
    const common::Status png_status = SetGlobalBinaryData(GetPngClipboardFormat(), payloads->png_bytes);
    if (png_status.ok()) {
      *wrote_any = true;
    }
  }
  if (payloads->bitmap != nullptr && SetClipboardData(CF_BITMAP, payloads->bitmap) != nullptr) {
    *wrote_any = true;
    payloads->bitmap = nullptr;
  }
}

// 图片写回公共实现：一次 OpenClipboard → 一次 EmptyClipboard →
// 全部格式 SetClipboardData → 一次 CloseClipboard。
common::Status WriteGeneratedImageFormatsImpl(const std::wstring& path, bool include_png_payload) {
  ImageFormatPayloads payloads;
  common::Status status = LoadBitmapFromPng(path, &payloads.bitmap);
  if (!status.ok()) {
    return status;
  }

  UINT width = 0;
  UINT height = 0;
  std::vector<std::uint8_t> pixels;
  status = ExtractBitmapPixels(payloads.bitmap, &width, &height, &pixels);
  if (!status.ok()) {
    DeleteObject(payloads.bitmap);
    return status;
  }

  payloads.dibv5_bytes = BuildDibPayload(width, height, pixels, true);
  payloads.dib_bytes = BuildDibPayload(width, height, pixels, false);
  if (include_png_payload) {
    std::ifstream input(path, std::ios::binary);
    if (input.is_open()) {
      input.seekg(0, std::ios::end);
      const std::streamoff size = input.tellg();
      if (size > 0) {
        input.seekg(0, std::ios::beg);
        payloads.png_bytes.resize(static_cast<size_t>(size));
        input.read(reinterpret_cast<char*>(payloads.png_bytes.data()), size);
        if (!input.good() && !input.eof()) {
          payloads.png_bytes.clear();
        }
      }
    }
  }

  ClipboardScope clipboard;
  if (!clipboard.opened()) {
    DeleteObject(payloads.bitmap);
    return common::Status::Error(common::StatusCode::Unavailable, "Open clipboard failed.");
  }

  EmptyClipboard();
  bool wrote_any = false;
  WriteImagePayloadsToOpenSession(&payloads, &wrote_any);

  if (payloads.bitmap != nullptr) {
    DeleteObject(payloads.bitmap);
    payloads.bitmap = nullptr;
  }
  if (!wrote_any) {
    return common::Status::Error(common::StatusCode::PlatformError, "Write generated image formats failed.");
  }
  if (!path.empty()) {
    SetClipboardFilesData(path);
  }
  return common::Status::Ok();
}

bool IsImageFormatName(std::string_view format_name) {
  return format_name == "CF_DIB" || format_name == "CF_DIBV5" || format_name == "PNG";
}

}  // namespace

common::Status SetClipboardImageFromPng(const std::wstring& path) {
  return WriteGeneratedImageFormatsImpl(path, true);
}

common::Status SetClipboardImageFromFile(const std::wstring& path) {
  const std::wstring extension = common::ToLowerWide(std::filesystem::path(path).extension().wstring());
  return WriteGeneratedImageFormatsImpl(path, extension == L".png");
}

common::Status SetClipboardImageClip(
    const std::vector<store::ClipFormatPayload>& formats,
    const std::wstring& fallback_png_path) {
  // 第一阶段（未持有剪贴板）：在内存中备齐全部 payload——解析图片格式 ID、
  // 预解码 fallback 位图。WIC 解码是重活，不得在剪贴板打开期间进行。
  struct PendingBinaryWrite {
    UINT format_id;
    const std::vector<std::uint8_t>* bytes;
  };
  std::vector<PendingBinaryWrite> pending_writes;
  for (const auto& format : formats) {
    if (!IsImageFormatName(format.format_name) || format.binary_data.empty()) {
      continue;
    }
    UINT format_id = 0;
    if (format.format_name == "CF_DIB") {
      format_id = CF_DIB;
    } else if (format.format_name == "CF_DIBV5") {
      format_id = CF_DIBV5;
    } else {
      format_id = GetPngClipboardFormat();
    }
    pending_writes.push_back({format_id, &format.binary_data});
  }

  ImageFormatPayloads fallback;
  common::Status fallback_status = common::Status::Ok();
  bool fallback_decode_failed = false;
  if (!fallback_png_path.empty()) {
    fallback_status = LoadBitmapFromPng(fallback_png_path, &fallback.bitmap);
    fallback_decode_failed = !fallback_status.ok();
  }

  // 第二阶段：单次剪贴板会话——一次 Open → 一次 Empty → 全部写入 → 一次 Close。
  ClipboardScope clipboard;
  if (!clipboard.opened()) {
    if (fallback.bitmap != nullptr) {
      DeleteObject(fallback.bitmap);
    }
    return common::Status::Error(common::StatusCode::Unavailable, "Open clipboard failed.");
  }

  EmptyClipboard();

  bool wrote_image = false;
  for (const PendingBinaryWrite& write : pending_writes) {
    const common::Status status = SetGlobalBinaryData(write.format_id, *write.bytes);
    if (!status.ok()) {
      if (fallback.bitmap != nullptr) {
        DeleteObject(fallback.bitmap);
      }
      return status;
    }
    wrote_image = true;
  }

  if (fallback.bitmap != nullptr && SetClipboardData(CF_BITMAP, fallback.bitmap) == nullptr) {
    DeleteObject(fallback.bitmap);
    fallback.bitmap = nullptr;
    if (!wrote_image) {
      return common::Status::Error(common::StatusCode::PlatformError, "Write image bitmap failed.");
    }
  } else if (fallback.bitmap != nullptr) {
    wrote_image = true;
  }

  if (!wrote_image) {
    if (fallback_decode_failed) {
      return fallback_status;
    }
    return common::Status::Error(common::StatusCode::NotFound, "No image payload available.");
  }
  return common::Status::Ok();
}

}  // namespace clipd2d::core::clipboard
