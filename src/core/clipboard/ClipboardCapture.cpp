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
#include "core/clipboard/ClipboardDiagnostics.h"
#include "core/clipboard/ClipboardFormat.h"
#include "core/clipboard/ClipboardRead.h"
#include "core/clipboard/ImageDib.h"
#include "core/clipboard/ImageIdentity.h"
#include "core/clipboard/ImagePng.h"
#include "core/clipboard/ProcessInfo.h"
#include "core/policies/ClipboardRetryPolicy.h"
#include "core/policies/ImageIngestPolicy.h"

namespace clipd2d::core::clipboard {

namespace {

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

size_t DibColorTableBytes(const BITMAPINFOHEADER& header);

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
    // 归一化在前：让「身份哈希」和「落盘 PNG」看到同一份像素数据，避免同一张图
    // 因为 alpha 通道被误判而在哈希/落盘两条路径上产生不同结果。
    NormalizeDibAlphaIfDegenerate(&dibv5_bytes);
    NormalizeDibAlphaIfDegenerate(&dib_bytes);
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
      // CF_BITMAP 是 DDB，GetDIBits 出来的 32bpp BI_RGB 第 4 字节恒为 0（GDI 不填）。
      // 先归一化，再按「同一份字节」算身份哈希，保证采集侧与落盘侧看到同样的像素。
      NormalizeDibAlphaIfDegenerate(&bitmap_dib_bytes);
      status = ComputeCanonicalImageContentHashFromBytesImpl(
          "CF_BITMAP_DIB", bitmap_dib_bytes, &hash, &width, &height);
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
    clip->content_hash = common::ContentIdentityHex(clip->plain_text);
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

    clip->content_hash = common::ContentIdentityHex(
        clip->plain_text + "\nHTML\n" + clip->html_text + "\nRTF\n" + clip->rtf_text);
    return common::Status::Ok();
  }

  LogClipboardFormatsAtCaptureFailure();
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
    // 采集侧已经归一化过；这里再做一次是幂等的硬防线，防止将来新增的 DIB 来源绕过。
    std::vector<std::uint8_t> dib_bytes = pending_clip.ingest_bytes;
    NormalizeDibAlphaIfDegenerate(&dib_bytes);
    status = SaveDibToPng(
        dib_bytes,
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
