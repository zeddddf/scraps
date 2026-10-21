#include "core/clipboard/ClipboardCapture.h"

#include <windows.h>
#include <shellapi.h>
#include <wincodec.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <vector>

#include "common/FileHash.h"
#include "common/Log.h"
#include "common/StringUtil.h"
#include "common/Utf8.h"
#include "core/clipboard/ClipboardFormat.h"
#include "core/policies/ImageIngestPolicy.h"
#include "core/policies/ClipboardRetryPolicy.h"

namespace clipd2d::core::clipboard {

namespace {

class ClipboardScope {
 public:
  explicit ClipboardScope(HWND owner) {
    for (const DWORD delay_ms : policies::ClipboardOpenRetryDelaysMs()) {
      if (delay_ms > 0) {
        Sleep(delay_ms);
      }
      if (OpenClipboard(owner) == TRUE) {
        opened_ = true;
        break;
      }
    }
  }
  ~ClipboardScope() {
    if (opened_) {
      CloseClipboard();
    }
  }
  bool opened() const { return opened_; }

 private:
  bool opened_ = false;
};

std::wstring MakeFileName(const common::AppPaths& paths, const std::string& hash) {
  return paths.image_dir + L"\\" + common::Utf8ToWide(hash).wide + L".png";
}

std::wstring MakeDerivedFileName(const common::AppPaths& paths, const std::string& hash, const wchar_t* suffix) {
  return paths.image_dir + L"\\" + common::Utf8ToWide(hash).wide + suffix + L".png";
}

common::Status MakeClipboardImageNotReadyStatus(
    bool has_png,
    size_t png_size,
    bool has_dibv5,
    size_t dibv5_size,
    bool has_dib,
    size_t dib_size,
    bool has_bitmap,
    bool bitmap_handle_ready,
    std::string_view reason) {
  std::string message(reason);
  message += " png=" + std::string(has_png ? "1" : "0") + "/" + std::to_string(png_size);
  message += " dibv5=" + std::string(has_dibv5 ? "1" : "0") + "/" + std::to_string(dibv5_size);
  message += " dib=" + std::string(has_dib ? "1" : "0") + "/" + std::to_string(dib_size);
  message += " bitmap=" + std::string(has_bitmap ? "1" : "0") + "/" + std::string(bitmap_handle_ready ? "1" : "0");
  return common::Status::Error(common::StatusCode::Unavailable, std::move(message));
}

common::Status PromoteClipboardImageCaptureStatusForRetry(
    const common::Status& status,
    bool has_png,
    size_t png_size,
    bool has_dibv5,
    size_t dibv5_size,
    bool has_dib,
    size_t dib_size,
    bool has_bitmap,
    bool bitmap_handle_ready) {
  if (status.ok() || status.code == common::StatusCode::Unavailable) {
    return status;
  }
  switch (status.code) {
    case common::StatusCode::InvalidArgument:
    case common::StatusCode::NotFound:
    case common::StatusCode::PlatformError:
      return MakeClipboardImageNotReadyStatus(
          has_png,
          png_size,
          has_dibv5,
          dibv5_size,
          has_dib,
          dib_size,
          has_bitmap,
          bitmap_handle_ready,
          status.message);
    default:
      return status;
  }
}

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

std::vector<std::uint8_t> ReadGlobalBytes(HGLOBAL handle) {
  if (handle == nullptr) {
    return {};
  }
  const SIZE_T size = GlobalSize(handle);
  if (size == 0) {
    return {};
  }
  const auto* bytes = static_cast<const std::uint8_t*>(GlobalLock(handle));
  if (bytes == nullptr) {
    return {};
  }
  std::vector<std::uint8_t> copy(bytes, bytes + size);
  GlobalUnlock(handle);
  return copy;
}

std::wstring GetProcessPath(HWND window) {
  DWORD process_id = 0;
  GetWindowThreadProcessId(window, &process_id);
  if (process_id == 0) {
    return {};
  }
  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
  if (process == nullptr) {
    return {};
  }
  std::wstring path(4096, L'\0');
  DWORD size = static_cast<DWORD>(path.size());
  if (!QueryFullProcessImageNameW(process, 0, path.data(), &size)) {
    CloseHandle(process);
    return {};
  }
  CloseHandle(process);
  path.resize(size);
  return path;
}

std::wstring BaseName(std::wstring_view path) {
  const size_t pos = path.find_last_of(L"\\/");
  if (pos == std::wstring_view::npos) {
    return std::wstring(path);
  }
  return std::wstring(path.substr(pos + 1));
}

std::wstring WindowTitle(HWND window) {
  std::wstring title(512, L'\0');
  const int length = GetWindowTextW(window, title.data(), static_cast<int>(title.size()));
  if (length <= 0) {
    return {};
  }
  title.resize(length);
  return title;
}

std::string MakeTextTitle(std::string_view plain_text) {
  const std::string trimmed = common::TrimAscii(plain_text);
  if (trimmed.empty()) {
    return "Text";
  }
  const size_t newline = trimmed.find_first_of("\r\n");
  return trimmed.substr(0, newline == std::string::npos ? 80 : std::min<size_t>(newline, 80));
}

std::string MakeFilesTitle(const std::vector<std::wstring>& files) {
  if (files.empty()) {
    return "Files";
  }
  if (files.size() == 1) {
    return common::WideToUtf8(BaseName(files.front())).utf8;
  }
  return std::to_string(files.size()) + " files";
}

std::vector<std::wstring> ReadFiles(HDROP drop) {
  std::vector<std::wstring> files;
  const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
  for (UINT index = 0; index < count; ++index) {
    std::wstring path(4096, L'\0');
    const UINT written = DragQueryFileW(drop, index, path.data(), static_cast<UINT>(path.size()));
    path.resize(written);
    files.push_back(std::move(path));
  }
  return files;
}

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

common::Status ComputeBitmapPixelHash(
    HBITMAP bitmap,
    std::string* hash_out,
    int* width,
    int* height) {
  if (bitmap == nullptr || hash_out == nullptr || width == nullptr || height == nullptr) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "Invalid bitmap hash output.");
  }

  *hash_out = {};
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

  std::vector<std::byte> pixels(static_cast<size_t>(info.bmWidth) * static_cast<size_t>(info.bmHeight) * 4u);
  HDC screen = GetDC(nullptr);
  if (screen == nullptr) {
    return common::Status::Error(common::StatusCode::PlatformError, "GetDC failed.");
  }
  if (GetDIBits(
          screen,
          bitmap,
          0,
          static_cast<UINT>(info.bmHeight),
          pixels.data(),
          &bitmap_info,
          DIB_RGB_COLORS) == 0) {
    ReleaseDC(nullptr, screen);
    return common::Status::Error(common::StatusCode::PlatformError, "Extract bitmap pixels failed.");
  }
  ReleaseDC(nullptr, screen);

  return ComputeImageContentHashFromCanonicalPixels(
      *width,
      *height,
      std::string_view(reinterpret_cast<const char*>(pixels.data()), pixels.size()),
      hash_out);
}

size_t DibColorTableBytes(const BITMAPINFOHEADER& header);

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

common::Status SaveBitmapToPng(
    HBITMAP bitmap,
    const std::wstring& path,
    std::string* hash_out,
    int* width,
    int* height) {
  std::string computed_hash;
  int bitmap_width = 0;
  int bitmap_height = 0;
  common::Status status = ComputeBitmapPixelHash(bitmap, &computed_hash, &bitmap_width, &bitmap_height);
  if (!status.ok()) {
    return status;
  }
  if (hash_out != nullptr) {
    *hash_out = computed_hash;
  }
  if (width != nullptr) {
    *width = bitmap_width;
  }
  if (height != nullptr) {
    *height = bitmap_height;
  }

  IWICImagingFactory* factory = nullptr;
  HRESULT hr = CoCreateInstance(
      CLSID_WICImagingFactory,
      nullptr,
      CLSCTX_INPROC_SERVER,
      IID_PPV_ARGS(&factory));
  if (FAILED(hr)) {
    return common::Status::Error(common::StatusCode::PlatformError, "Create WIC factory failed.");
  }

  IWICBitmap* wic_bitmap = nullptr;
  hr = factory->CreateBitmapFromHBITMAP(bitmap, nullptr, WICBitmapUseAlpha, &wic_bitmap);
  if (FAILED(hr)) {
    factory->Release();
    return common::Status::Error(common::StatusCode::PlatformError, "Create WIC bitmap failed.");
  }

  IWICStream* stream = nullptr;
  hr = factory->CreateStream(&stream);
  if (FAILED(hr)) {
    wic_bitmap->Release();
    factory->Release();
    return common::Status::Error(common::StatusCode::PlatformError, "Create WIC stream failed.");
  }

  hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
  if (FAILED(hr)) {
    stream->Release();
    wic_bitmap->Release();
    factory->Release();
    return common::Status::Error(common::StatusCode::PlatformError, "Open PNG output file failed.");
  }

  IWICBitmapEncoder* encoder = nullptr;
  hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);
  if (FAILED(hr)) {
    stream->Release();
    wic_bitmap->Release();
    factory->Release();
    return common::Status::Error(common::StatusCode::PlatformError, "Create PNG encoder failed.");
  }

  hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
  IWICBitmapFrameEncode* frame = nullptr;
  IPropertyBag2* props = nullptr;
  if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(&frame, &props);
  if (SUCCEEDED(hr)) hr = frame->Initialize(props);
  if (SUCCEEDED(hr)) hr = frame->WriteSource(wic_bitmap, nullptr);
  if (SUCCEEDED(hr)) hr = frame->Commit();
  if (SUCCEEDED(hr)) hr = encoder->Commit();

  if (props != nullptr) props->Release();
  if (frame != nullptr) frame->Release();
  encoder->Release();
  stream->Release();
  wic_bitmap->Release();
  factory->Release();

  if (FAILED(hr)) {
    return common::Status::Error(common::StatusCode::PlatformError, "Write PNG file failed.");
  }
  return common::Status::Ok();
}

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

  const UINT stride = source_width * 4;
  const UINT image_size = stride * source_height;
  std::vector<std::byte> pixels(image_size);
  hr = converter->CopyPixels(nullptr, stride, image_size, reinterpret_cast<BYTE*>(pixels.data()));
  if (FAILED(hr)) {
    converter->Release();
    frame->Release();
    decoder->Release();
    factory->Release();
    return common::Status::Error(common::StatusCode::PlatformError, "Copy image pixels failed.");
  }

  if (hash_out != nullptr) {
    const common::Status hash_status = ComputeImageContentHashFromCanonicalPixels(
        static_cast<int>(source_width),
        static_cast<int>(source_height),
        std::string_view(reinterpret_cast<const char*>(pixels.data()), pixels.size()),
        hash_out);
    if (!hash_status.ok()) {
      converter->Release();
      frame->Release();
      decoder->Release();
      factory->Release();
      return hash_status;
    }
  }

  IWICStream* stream = nullptr;
  hr = factory->CreateStream(&stream);
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
  IWICBitmapFrameEncode* output_frame = nullptr;
  IPropertyBag2* props = nullptr;
  if (SUCCEEDED(hr)) {
    hr = encoder->CreateNewFrame(&output_frame, &props);
  }
  if (SUCCEEDED(hr)) {
    hr = output_frame->Initialize(props);
  }
  if (SUCCEEDED(hr)) {
    hr = output_frame->WriteSource(converter, nullptr);
  }
  if (SUCCEEDED(hr)) {
    hr = output_frame->Commit();
  }
  if (SUCCEEDED(hr)) {
    hr = encoder->Commit();
  }

  if (props != nullptr) props->Release();
  if (output_frame != nullptr) output_frame->Release();
  if (encoder != nullptr) encoder->Release();
  if (stream != nullptr) stream->Release();
  converter->Release();
  frame->Release();
  decoder->Release();
  factory->Release();

  if (FAILED(hr)) {
    return common::Status::Error(common::StatusCode::PlatformError, "Write image file png failed.");
  }
  return common::Status::Ok();
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

void AddTextFormat(
    std::vector<store::ClipFormatPayload>* formats,
    int* order,
    std::string name,
    const std::string& value) {
  if (formats == nullptr || order == nullptr || value.empty()) {
    return;
  }
  formats->push_back(store::ClipFormatPayload{
      std::move(name),
      store::ClipFormatPayloadKind::Text,
      value,
      {},
      {},
      (*order)++});
}

void AddBinaryFormat(
    std::vector<store::ClipFormatPayload>* formats,
    int* order,
    std::string name,
    std::vector<std::uint8_t> bytes) {
  if (formats == nullptr || order == nullptr || bytes.empty()) {
    return;
  }
  formats->push_back(store::ClipFormatPayload{
      std::move(name),
      store::ClipFormatPayloadKind::Binary,
      {},
      std::move(bytes),
      {},
      (*order)++});
}

}  // namespace

ReadGlobalTextResult ReadGlobalText(HGLOBAL handle) {
  ReadGlobalTextResult result;
  if (handle == nullptr) {
    result.status = common::Status::Error(common::StatusCode::Unavailable, "CF_UNICODETEXT handle is null.");
    return result;
  }
  const SIZE_T size = GlobalSize(handle);
  if (size == 0) {
    result.status = common::Status::Error(common::StatusCode::Unavailable, "CF_UNICODETEXT memory block is empty.");
    return result;
  }
  const wchar_t* text = static_cast<const wchar_t*>(GlobalLock(handle));
  if (text == nullptr) {
    result.status = common::Status::Error(common::StatusCode::Unavailable, "GlobalLock failed for CF_UNICODETEXT.");
    return result;
  }
  // CONSTRAINT: the clipboard payload may come from a hostile process and might
  // not be NUL-terminated - never scan past the HGLOBAL block size.
  const size_t max_chars = static_cast<size_t>(size / sizeof(wchar_t));
  size_t length = 0;
  while (length < max_chars && text[length] != L'\0') {
    ++length;
  }
  const std::wstring wide_text(text, length);
  GlobalUnlock(handle);
  const common::Utf8Conversion utf8 = common::WideToUtf8(wide_text);
  if (!utf8.status.ok()) {
    result.status = utf8.status;
    return result;
  }
  result.text = utf8.utf8;
  return result;
}

ReadGlobalTextResult ReadAnsiGlobal(HGLOBAL handle) {
  ReadGlobalTextResult result;
  if (handle == nullptr) {
    result.status = common::Status::Error(common::StatusCode::Unavailable, "ANSI clipboard handle is null.");
    return result;
  }
  const SIZE_T size = GlobalSize(handle);
  if (size == 0) {
    result.status = common::Status::Error(common::StatusCode::Unavailable, "ANSI clipboard memory block is empty.");
    return result;
  }
  const char* text = static_cast<const char*>(GlobalLock(handle));
  if (text == nullptr) {
    result.status = common::Status::Error(common::StatusCode::Unavailable, "GlobalLock failed for ANSI clipboard text.");
    return result;
  }
  // CONSTRAINT: the clipboard payload may come from a hostile process and might
  // not be NUL-terminated - never scan past the HGLOBAL block size.
  size_t length = 0;
  while (length < static_cast<size_t>(size) && text[length] != '\0') {
    ++length;
  }
  result.text.assign(text, length);
  GlobalUnlock(handle);
  return result;
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

common::Status CaptureClipboard(
    const common::AppPaths& paths,
    HWND source_window,
    store::CapturedClip* clip) {
  if (clip == nullptr) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "Output clip cannot be null.");
  }

  ClipboardScope clipboard_scope(nullptr);
  if (!clipboard_scope.opened()) {
    return common::Status::Error(common::StatusCode::Unavailable, "Open clipboard failed.");
  }

  clip->source_app.process_path = GetProcessPath(source_window);
  clip->source_app.process_name = BaseName(clip->source_app.process_path);
  clip->source_app.window_title = WindowTitle(source_window);
  clip->formats.clear();

  std::vector<std::wstring> dropped_files;
  if (IsClipboardFormatAvailable(CF_HDROP)) {
    HDROP drop = static_cast<HDROP>(GetClipboardData(CF_HDROP));
    if (drop != nullptr) {
      dropped_files = ReadFiles(drop);
    }
  }

  const bool has_dibv5 = IsClipboardFormatAvailable(CF_DIBV5);
  const bool has_dib = IsClipboardFormatAvailable(CF_DIB);
  const bool has_bitmap = IsClipboardFormatAvailable(CF_BITMAP);
  const bool has_png = IsClipboardFormatAvailable(GetPngClipboardFormat());
  if (has_bitmap || has_dibv5 || has_dib || has_png) {
    std::vector<std::uint8_t> dibv5_bytes;
    std::vector<std::uint8_t> dib_bytes;
    std::vector<std::uint8_t> png_bytes;
    if (has_dibv5) {
      dibv5_bytes = ReadGlobalBytes(static_cast<HGLOBAL>(GetClipboardData(CF_DIBV5)));
    }
    if (has_dib) {
      dib_bytes = ReadGlobalBytes(static_cast<HGLOBAL>(GetClipboardData(CF_DIB)));
    }
    if (has_png) {
      png_bytes = ReadGlobalBytes(static_cast<HGLOBAL>(GetClipboardData(GetPngClipboardFormat())));
    }
    HBITMAP bitmap = has_bitmap ? static_cast<HBITMAP>(GetClipboardData(CF_BITMAP)) : nullptr;
    const bool bitmap_handle_ready = bitmap != nullptr;

    clip->content_type = store::ClipContentType::Image;
    clip->title = "Image";
    clip->plain_text.clear();
    if (!dropped_files.empty()) {
      clip->secondary_path = dropped_files.front();
    }

    if (!png_bytes.empty() || !dibv5_bytes.empty() || !dib_bytes.empty()) {
      int width = 0;
      int height = 0;
      std::string hash;
      if (!png_bytes.empty()) {
        const common::Status status = ComputeCanonicalImageContentHashFromBytesImpl("PNG", png_bytes, &hash, &width, &height);
        if (!status.ok()) {
          return PromoteClipboardImageCaptureStatusForRetry(
              status,
              has_png,
              png_bytes.size(),
              has_dibv5,
              dibv5_bytes.size(),
              has_dib,
              dib_bytes.size(),
              has_bitmap,
              bitmap_handle_ready);
        }
        clip->ingest_bytes = std::move(png_bytes);
        clip->ingest_format = "PNG";
      } else if (!dibv5_bytes.empty()) {
        const common::Status status = ComputeCanonicalImageContentHashFromBytesImpl("CF_DIBV5", dibv5_bytes, &hash, &width, &height);
        if (!status.ok()) {
          return PromoteClipboardImageCaptureStatusForRetry(
              status,
              has_png,
              png_bytes.size(),
              has_dibv5,
              dibv5_bytes.size(),
              has_dib,
              dib_bytes.size(),
              has_bitmap,
              bitmap_handle_ready);
        }
        clip->ingest_bytes = std::move(dibv5_bytes);
        clip->ingest_format = "CF_DIBV5";
      } else {
        const common::Status status = ComputeCanonicalImageContentHashFromBytesImpl("CF_DIB", dib_bytes, &hash, &width, &height);
        if (!status.ok()) {
          return PromoteClipboardImageCaptureStatusForRetry(
              status,
              has_png,
              png_bytes.size(),
              has_dibv5,
              dibv5_bytes.size(),
              has_dib,
              dib_bytes.size(),
              has_bitmap,
              bitmap_handle_ready);
        }
        clip->ingest_bytes = std::move(dib_bytes);
        clip->ingest_format = "CF_DIB";
      }

      clip->content_hash = hash;
      clip->storage_path = MakeFileName(paths, hash);
      clip->thumbnail_path = MakeDerivedFileName(paths, hash, L"_thumb");
      clip->preview_path = MakeDerivedFileName(paths, hash, L"_preview");
      clip->image_width = width;
      clip->image_height = height;
      clip->estimated_decoded_bytes = policies::EstimatedDecodedBytes(width, height);
      clip->is_large_image = policies::IsLargeImageEstimate(clip->estimated_decoded_bytes);
      clip->ingest_state = "pending";
      return common::Status::Ok();
    }

    if (has_bitmap) {
      std::string hash;
      int width = 0;
      int height = 0;
      if (bitmap == nullptr) {
        return MakeClipboardImageNotReadyStatus(
            has_png,
            png_bytes.size(),
            has_dibv5,
            dibv5_bytes.size(),
            has_dib,
            dib_bytes.size(),
            has_bitmap,
            false,
            "CF_BITMAP handle is not ready.");
      }
      std::vector<std::uint8_t> bitmap_dib_bytes;
      common::Status status = SnapshotBitmapToDibBytes(bitmap, &bitmap_dib_bytes, &width, &height);
      if (!status.ok()) {
        return PromoteClipboardImageCaptureStatusForRetry(
            status,
            has_png,
            png_bytes.size(),
            has_dibv5,
            dibv5_bytes.size(),
            has_dib,
            dib_bytes.size(),
            has_bitmap,
            true);
      }
      status = ComputeBitmapPixelHash(bitmap, &hash, &width, &height);
      if (!status.ok()) {
        return PromoteClipboardImageCaptureStatusForRetry(
            status,
            has_png,
            png_bytes.size(),
            has_dibv5,
            dibv5_bytes.size(),
            has_dib,
            dib_bytes.size(),
            has_bitmap,
            true);
      }
      clip->storage_path = MakeFileName(paths, hash);
      clip->thumbnail_path = MakeDerivedFileName(paths, hash, L"_thumb");
      clip->preview_path = MakeDerivedFileName(paths, hash, L"_preview");
      clip->image_width = width;
      clip->image_height = height;
      clip->estimated_decoded_bytes = policies::EstimatedDecodedBytes(width, height);
      clip->is_large_image = policies::IsLargeImageEstimate(clip->estimated_decoded_bytes);
      clip->content_hash = hash;
      clip->ingest_bytes = std::move(bitmap_dib_bytes);
      clip->ingest_format = "CF_BITMAP_DIB";
      clip->ingest_state = "pending";
      return common::Status::Ok();
    }
    return MakeClipboardImageNotReadyStatus(
        has_png,
        png_bytes.size(),
        has_dibv5,
        dibv5_bytes.size(),
        has_dib,
        dib_bytes.size(),
        has_bitmap,
        bitmap_handle_ready,
        "Image formats were advertised but no payload was ready.");
  }

  if (!dropped_files.empty()) {
    clip->content_type = store::ClipContentType::Files;
    clip->plain_text = common::WideToUtf8(common::JoinWide(dropped_files, L"\n")).utf8;
    clip->title = MakeFilesTitle(dropped_files);
    clip->secondary_path = dropped_files.front();
    clip->content_hash = common::HashBytes(clip->plain_text);
    int order = 0;
    AddTextFormat(&clip->formats, &order, "CF_HDROP", clip->plain_text);
    return common::Status::Ok();
  }

  const bool has_text = IsClipboardFormatAvailable(CF_UNICODETEXT);
  const bool has_html = IsClipboardFormatAvailable(GetHtmlClipboardFormat());
  const bool has_rtf = IsClipboardFormatAvailable(GetRtfClipboardFormat());
  if (has_text || has_html || has_rtf) {
    bool text_payload_read_failed = false;
    if (has_text) {
      const ReadGlobalTextResult text_result =
          ReadGlobalText(static_cast<HGLOBAL>(GetClipboardData(CF_UNICODETEXT)));
      if (text_result.status.ok()) {
        clip->plain_text = text_result.text;
      } else {
        text_payload_read_failed = true;
      }
    }
    if (has_html) {
      const ReadGlobalTextResult html_result =
          ReadAnsiGlobal(static_cast<HGLOBAL>(GetClipboardData(GetHtmlClipboardFormat())));
      if (html_result.status.ok()) {
        clip->html_text = html_result.text;
      } else {
        text_payload_read_failed = true;
      }
    }
    if (has_rtf) {
      const ReadGlobalTextResult rtf_result =
          ReadAnsiGlobal(static_cast<HGLOBAL>(GetClipboardData(GetRtfClipboardFormat())));
      if (rtf_result.status.ok()) {
        clip->rtf_text = rtf_result.text;
      } else {
        text_payload_read_failed = true;
      }
    }

    if (clip->plain_text.empty() && clip->html_text.empty() && clip->rtf_text.empty()) {
      common::WriteLog(
          text_payload_read_failed ? common::LogLevel::Warning : common::LogLevel::Info,
          "clipboard.capture",
          "text payloads empty has_text=" + std::string(has_text ? "true" : "false") +
              " has_html=" + std::string(has_html ? "true" : "false") +
              " has_rtf=" + std::string(has_rtf ? "true" : "false") +
              " read_failed=" + std::string(text_payload_read_failed ? "true" : "false"));
      if (text_payload_read_failed) {
        return common::Status::Error(
            common::StatusCode::Unavailable,
            "Clipboard text payloads were advertised but could not be read.");
      }
    }

    if (!clip->html_text.empty()) {
      clip->content_type = store::ClipContentType::Html;
    } else if (!clip->rtf_text.empty()) {
      clip->content_type = store::ClipContentType::Rtf;
    } else {
      clip->content_type = store::ClipContentType::Text;
    }

    if (!clip->plain_text.empty()) {
      clip->title = MakeTextTitle(clip->plain_text);
    } else if (clip->content_type == store::ClipContentType::Html) {
      clip->title = "HTML";
    } else if (clip->content_type == store::ClipContentType::Rtf) {
      clip->title = "Rich text";
    } else {
      clip->title = "Text";
    }

    int order = 0;
    AddTextFormat(&clip->formats, &order, "CF_UNICODETEXT", clip->plain_text);
    AddTextFormat(&clip->formats, &order, "HTML Format", clip->html_text);
    AddTextFormat(&clip->formats, &order, "Rich Text Format", clip->rtf_text);

    clip->content_hash = common::HashBytes(
        clip->plain_text + "\nHTML\n" + clip->html_text + "\nRTF\n" + clip->rtf_text);
    return common::Status::Ok();
  }

  return common::Status::Error(common::StatusCode::Unavailable, "Clipboard does not contain supported content.");
}

common::Status NormalizePendingImage(
    const common::AppPaths& paths,
    const store::CapturedClip& pending_clip,
    std::string* content_hash,
    int* image_width,
    int* image_height) {
  if (content_hash == nullptr || image_width == nullptr || image_height == nullptr) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "Invalid pending image output.");
  }
  if (pending_clip.ingest_bytes.empty() || pending_clip.ingest_format.empty()) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "Pending image payload is empty.");
  }
  if (pending_clip.storage_path.empty() || pending_clip.thumbnail_path.empty()) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "Pending image target path is empty.");
  }

  *content_hash = pending_clip.content_hash;
  *image_width = pending_clip.image_width;
  *image_height = pending_clip.image_height;

  const HRESULT init_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  const bool need_uninit = SUCCEEDED(init_result);

  common::Status status = common::Status::Ok();
  std::string generated_hash;
  if (pending_clip.ingest_format == "PNG") {
    const std::wstring temp_source_path =
        paths.image_dir + L"\\" + common::Utf8ToWide(pending_clip.content_hash).wide + L"_source.png";
    status = SaveClipboardPngToPng(
        pending_clip.ingest_bytes,
        temp_source_path,
        pending_clip.storage_path,
        &generated_hash,
        image_width,
        image_height);
  } else if (
      pending_clip.ingest_format == "CF_DIB" ||
      pending_clip.ingest_format == "CF_DIBV5" ||
      pending_clip.ingest_format == "CF_BITMAP_DIB") {
    status = SaveDibToPng(
        pending_clip.ingest_bytes,
        pending_clip.storage_path,
        &generated_hash,
        image_width,
        image_height);
  } else if (pending_clip.ingest_format == "CANONICAL_PNG") {
    if (!std::filesystem::exists(pending_clip.storage_path)) {
      status = common::Status::Error(common::StatusCode::NotFound, "Pending canonical png not found.");
    }
  } else {
    status = common::Status::Error(common::StatusCode::NotImplemented, "Unsupported pending image format.");
  }
  if (status.ok()) {
    status = SaveScaledImageFileToPng(pending_clip.storage_path, pending_clip.thumbnail_path, 240, 160);
  }
  if (status.ok()) {
    if (generated_hash.empty()) {
      *content_hash = pending_clip.content_hash;
    } else {
      *content_hash = generated_hash;
    }
  }
  if (need_uninit) {
    CoUninitialize();
  }
  return status;
}

}  // namespace clipd2d::core::clipboard
