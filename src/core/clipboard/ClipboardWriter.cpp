#include "core/clipboard/ClipboardWriter.h"

#include <windows.h>

#include <optional>
#include <string_view>
#include <vector>

#include "common/Status.h"
#include "common/Utf8.h"
#include "core/clipboard/ClipboardFormat.h"
#include "core/clipboard/ClipboardSession.h"
#include "core/clipboard/ImageWriteback.h"
#include "core/clipboard/PasteDispatch.h"
#include "core/clipboard/PasteTarget.h"
#include "core/store/ClipTypes.h"

// 能力边界：**写回编排**——决定「这个条目该以哪些格式写回剪贴板」。
// 图片 payload 合成 → ImageWriteback；粘贴按键派发 → PasteDispatch；
// 目标窗口定位 → PasteTarget；会话与全局内存原语 → ClipboardSession。
// 本文件只保留标准格式（文本/HTML/RTF/文件）写入与 WriteClipToClipboard 分发。
namespace clipd2d::core::clipboard {

namespace {

std::optional<UINT> ResolveClipboardFormat(std::string_view format_name) {
  if (format_name == "CF_UNICODETEXT") {
    return CF_UNICODETEXT;
  }
  if (format_name == "HTML Format") {
    return GetHtmlClipboardFormat();
  }
  if (format_name == "Rich Text Format") {
    return GetRtfClipboardFormat();
  }
  if (format_name == "CF_HDROP") {
    return CF_HDROP;
  }
  if (format_name == "CF_DIB") {
    return CF_DIB;
  }
  if (format_name == "CF_DIBV5") {
    return CF_DIBV5;
  }
  if (format_name == "PNG") {
    return GetPngClipboardFormat();
  }
  return std::nullopt;
}

common::Status WriteStandardFormat(const store::ClipFormatPayload& format) {
  const auto format_id = ResolveClipboardFormat(format.format_name);
  if (!format_id.has_value()) {
    return common::Status::Ok();
  }

  if (*format_id == CF_UNICODETEXT) {
    return SetGlobalTextData(CF_UNICODETEXT, common::Utf8ToWide(format.text_data).wide);
  }
  if (*format_id == CF_HDROP) {
    return SetClipboardFilesData(common::Utf8ToWide(format.text_data).wide);
  }
  if (*format_id == GetHtmlClipboardFormat() || *format_id == GetRtfClipboardFormat()) {
    return SetGlobalAnsiData(*format_id, format.text_data);
  }
  return SetGlobalBinaryData(*format_id, format.binary_data);
}

// 内部原语：把标准格式集合写入“当前已打开”的剪贴板会话，不做 Open/Empty/Close。
common::Status WriteStandardFormatsToOpenSession(const std::vector<store::ClipFormatPayload>& formats) {
  for (const auto& format : formats) {
    const common::Status status = WriteStandardFormat(format);
    if (!status.ok()) {
      return status;
    }
  }
  return common::Status::Ok();
}

common::Status WriteStandardFormatsToClipboard(const std::vector<store::ClipFormatPayload>& formats) {
  ClipboardScope clipboard;
  if (!clipboard.opened()) {
    return common::Status::Error(common::StatusCode::Unavailable, "Open clipboard failed.");
  }

  EmptyClipboard();
  return WriteStandardFormatsToOpenSession(formats);
}

}  // namespace

common::Status SetClipboardText(
    std::string_view text,
    std::string_view html_text,
    std::string_view rtf_text) {
  ClipboardScope clipboard;
  if (!clipboard.opened()) {
    return common::Status::Error(common::StatusCode::Unavailable, "Open clipboard failed.");
  }

  EmptyClipboard();

  common::Status status = SetGlobalTextData(CF_UNICODETEXT, common::Utf8ToWide(text).wide);
  if (!status.ok()) {
    return status;
  }
  if (!html_text.empty()) {
    status = SetGlobalAnsiData(GetHtmlClipboardFormat(), std::string(html_text));
    if (!status.ok()) {
      return status;
    }
  }
  if (!rtf_text.empty()) {
    status = SetGlobalAnsiData(GetRtfClipboardFormat(), std::string(rtf_text));
  }
  return status;
}

common::Status SetClipboardFiles(const std::wstring& joined_paths) {
  ClipboardScope clipboard;
  if (!clipboard.opened()) {
    return common::Status::Error(common::StatusCode::Unavailable, "Open clipboard failed.");
  }

  EmptyClipboard();
  return SetClipboardFilesData(joined_paths);
}

common::Status WriteClipToClipboard(
    const store::ClipRecord& clip,
    std::string_view transformed_text,
    bool use_transformed_text) {
  if (use_transformed_text) {
    return SetClipboardText(transformed_text, {}, {});
  }

  if (clip.summary.content_type == store::ClipContentType::Image) {
    if (!clip.storage_path.empty()) {
      const common::Status png_status = SetClipboardImageFromPng(clip.storage_path);
      if (png_status.ok()) {
        return png_status;
      }
    }
    const common::Status image_status = SetClipboardImageClip(clip.formats, clip.storage_path);
    if (image_status.ok()) {
      return image_status;
    }
  }

  if (!clip.formats.empty()) {
    const common::Status status = WriteStandardFormatsToClipboard(clip.formats);
    if (status.ok()) {
      return status;
    }
  }

  switch (clip.summary.content_type) {
    case store::ClipContentType::Text:
      return SetClipboardText(clip.plain_text, {}, {});
    case store::ClipContentType::Html:
      return SetClipboardText(clip.plain_text, clip.html_text, {});
    case store::ClipContentType::Rtf:
      return SetClipboardText(clip.plain_text, {}, clip.rtf_text);
    case store::ClipContentType::Files:
      return SetClipboardFiles(common::Utf8ToWide(clip.plain_text).wide);
    case store::ClipContentType::Image:
      return SetClipboardImageFromPng(clip.storage_path);
  }

  return common::Status::Error(common::StatusCode::NotImplemented, "Unsupported clip content type.");
}

}  // namespace clipd2d::core::clipboard
