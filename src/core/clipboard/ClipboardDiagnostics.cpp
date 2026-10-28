#include "core/clipboard/ClipboardDiagnostics.h"

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

#include "core/clipboard/ProcessInfo.h"
#include "core/clipboard/ClipboardFormat.h"
#include "common/Log.h"
#include "common/Utf8.h"

namespace clipd2d::core::clipboard {

std::string ClipboardStandardFormatName(UINT format_id) {
  switch (format_id) {
    case CF_TEXT: return "CF_TEXT";
    case CF_BITMAP: return "CF_BITMAP";
    case CF_METAFILEPICT: return "CF_METAFILEPICT";
    case CF_SYLK: return "CF_SYLK";
    case CF_DIF: return "CF_DIF";
    case CF_TIFF: return "CF_TIFF";
    case CF_OEMTEXT: return "CF_OEMTEXT";
    case CF_DIB: return "CF_DIB";
    case CF_PALETTE: return "CF_PALETTE";
    case CF_PENDATA: return "CF_PENDATA";
    case CF_RIFF: return "CF_RIFF";
    case CF_WAVE: return "CF_WAVE";
    case CF_UNICODETEXT: return "CF_UNICODETEXT";
    case CF_ENHMETAFILE: return "CF_ENHMETAFILE";
    case CF_HDROP: return "CF_HDROP";
    case CF_LOCALE: return "CF_LOCALE";
    case CF_DIBV5: return "CF_DIBV5";
    default: return {};
  }
}

// 诊断（只读，不改捕获/重试逻辑）：枚举当前剪贴板全部已注册格式并记录每个
// IsClipboardFormatAvailable 结果。必须持有已打开的剪贴板会话调用。
// 用途：PixPin 等截图软件捕获失败（status=7 "Clipboard does not contain
// supported content."）时，用格式列表定位其放置的真实格式。
// 用于定位「谁把剪贴板弄空」：EnumClipboardFormats 返回 0 说明此刻剪贴板上确实
// 没有任何格式（不是「格式不支持」）。记录 owner 窗口所属进程，才能判断是第三方
// 截图工具写了一半，还是别的进程抢了所有权。
std::string DescribeClipboardOwnerForDiagnostics() {
  const HWND owner = GetClipboardOwner();
  if (owner == nullptr) {
    return "none";
  }
  const std::wstring path = GetProcessPath(owner);
  const std::wstring name = path.empty() ? std::wstring(L"?") : BaseName(path);
  return common::WideToUtf8(name).utf8 + "(hwnd=" +
      std::to_string(reinterpret_cast<std::uintptr_t>(owner)) + ")";
}

std::string DescribeClipboardFormatsForDiagnostics() {
  std::string formats_text;
  int enumerated = 0;
  UINT format_id = 0;
  while ((format_id = EnumClipboardFormats(format_id)) != 0) {
    std::string name = ClipboardStandardFormatName(format_id);
    if (name.empty() && format_id >= 0xC000) {
      char registered_name[256]{};
      if (GetClipboardFormatNameA(format_id, registered_name, sizeof(registered_name)) > 0) {
        name = registered_name;
      }
    }
    if (name.empty()) {
      name = "format#" + std::to_string(format_id);
    }
    formats_text += " [" + std::to_string(format_id) + ":" + name +
        ":avail=" + (IsClipboardFormatAvailable(format_id) == TRUE ? "1" : "0") + "]";
    ++enumerated;
  }

  const auto availability = [](UINT format_id) {
    return IsClipboardFormatAvailable(format_id) == TRUE ? "1" : "0";
  };
  const UINT png_format = GetPngClipboardFormat();
  const UINT html_format = GetHtmlClipboardFormat();
  const UINT rtf_format = GetRtfClipboardFormat();
  return "enumerated=" + std::to_string(enumerated) +
      " png=" + std::to_string(png_format) + "/" + availability(png_format) +
      " html=" + std::to_string(html_format) + "/" + availability(html_format) +
      " rtf=" + std::to_string(rtf_format) + "/" + availability(rtf_format) +
      " owner=" + DescribeClipboardOwnerForDiagnostics() +
      " seq=" + std::to_string(GetClipboardSequenceNumber()) +
      " list=" + (formats_text.empty() ? "(empty)" : formats_text);
}

void LogClipboardFormatsAtCaptureFailure() {
  common::WriteLog(
      common::LogLevel::Warning,
      "clipboard.formats",
      "capture unsupported-content failure " + DescribeClipboardFormatsForDiagnostics());
}

}  // namespace clipd2d::core::clipboard
