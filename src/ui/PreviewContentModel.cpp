#include "ui/PreviewContentModel.h"

#include <windows.h>

#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>

#include "common/StringUtil.h"
#include "common/Utf8.h"

namespace clipd2d::ui {

namespace {

// NavigateToString 内部构造 data URL（上限约 2MB 字符），512KB UTF-8 片段留足余量。
constexpr size_t kMaxHtmlFragmentBytes = 512 * 1024;

// 主题色与 D2dShell 调色板对齐（surface/text 层级，见 PreviewWindow 注释）。
// padding 收窄到 8/10：WebView bounds 已全出血贴窗缘，网页自身留白是用户
// 能看到的唯一边距，必须紧凑；滚动条细窄化（Chromium 伪元素）。
constexpr wchar_t kDocStyleLight[] =
    L"html,body{margin:0;padding:8px 10px;background:#f7f9fd;color:#23262d;"
    L"font-family:'Segoe UI',system-ui,sans-serif;font-size:14px;line-height:1.5;word-break:break-word}"
    L"::-webkit-scrollbar{width:6px;height:6px}"
    L"::-webkit-scrollbar-thumb{background:#c4cad6;border-radius:3px}"
    L"::-webkit-scrollbar-track{background:transparent}"
    L"img,video{max-width:100%;height:auto}"
    L"a{color:#0066cc}"
    L"table{border-collapse:collapse}td,th{border:1px solid #d9dee8;padding:4px 8px}"
    L"blockquote{border-left:3px solid #d9dee8;margin:8px 0;padding:2px 10px;color:#5a6070}";
constexpr wchar_t kDocStyleDark[] =
    L"html,body{margin:0;padding:8px 10px;background:#2c2c2e;color:#e8e8ea;"
    L"font-family:'Segoe UI',system-ui,sans-serif;font-size:14px;line-height:1.5;word-break:break-word}"
    L"::-webkit-scrollbar{width:6px;height:6px}"
    L"::-webkit-scrollbar-thumb{background:#4a4a4c;border-radius:3px}"
    L"::-webkit-scrollbar-track{background:transparent}"
    L"img,video{max-width:100%;height:auto}"
    L"a{color:#4da3ff}"
    L"table{border-collapse:collapse}td,th{border:1px solid #3a3a3c;padding:4px 8px}"
    L"blockquote{border-left:3px solid #3a3a3c;margin:8px 0;padding:2px 10px;color:#a8adb8}";

}  // namespace

PreviewFileList BuildPreviewFileList(const std::string& paths_utf8) {
  PreviewFileList result;
  const std::wstring wide = common::Utf8ToWide(paths_utf8).wide;
  for (const auto& part : common::SplitWide(wide, L'\n')) {
    const std::wstring trimmed = common::TrimWide(part);
    if (trimmed.empty()) {
      continue;
    }
    PreviewFileEntry entry;
    entry.name = trimmed;
    std::error_code ec;
    const std::filesystem::directory_entry fs_entry(trimmed, ec);
    if (!ec && fs_entry.exists(ec)) {
      entry.exists = true;
      const auto size = fs_entry.file_size(ec);
      if (!ec) {
        entry.size_bytes = static_cast<std::int64_t>(size);
        result.total_bytes += entry.size_bytes;
      }
      entry.name = fs_entry.path().filename().wstring();
      if (entry.name.empty()) {
        entry.name = fs_entry.path().wstring();
      } else {
        entry.directory = fs_entry.path().parent_path().wstring();
      }
    } else {
      result.missing_count += 1;
    }
    result.entries.push_back(std::move(entry));
  }
  return result;
}

std::wstring FormatPreviewFileSize(std::int64_t bytes) {
  if (bytes < 0) {
    return L"—";
  }
  wchar_t buffer[64]{};
  const double value = static_cast<double>(bytes);
  if (bytes < 1024) {
    swprintf_s(buffer, L"%lld B", bytes);
  } else if (bytes < 1024 * 1024) {
    swprintf_s(buffer, L"%.1f KB", value / 1024.0);
  } else if (bytes < 1024LL * 1024 * 1024) {
    swprintf_s(buffer, L"%.1f MB", value / (1024.0 * 1024.0));
  } else {
    swprintf_s(buffer, L"%.2f GB", value / (1024.0 * 1024.0 * 1024.0));
  }
  return buffer;
}

float ClampPreviewZoom(float zoom) {
  if (!std::isfinite(zoom)) {
    return 1.0f;
  }
  // 吸附 0.1 步进，避免浮点累加漂移出 1.7000002 之类的绘制比例。
  const float snapped = std::round(zoom * 10.0f) / 10.0f;
  return std::clamp(snapped, 1.0f, 5.0f);
}

namespace {

constexpr char kStartFragmentMark[] = "<!--StartFragment-->";
constexpr char kEndFragmentMark[] = "<!--EndFragment-->";
// 头部偏移键只在载荷前段查找，避免正文里出现 "StartHTML:" 字样被误当头解析。
constexpr size_t kCfHtmlHeaderScanBytes = 512;

const char* FindAsciiICase(const std::string& text, const char* needle, size_t from) {
  const size_t needle_len = strlen(needle);
  if (needle_len == 0 || text.size() < needle_len || from > text.size() - needle_len) {
    return nullptr;
  }
  const auto lower = [](char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  };
  for (size_t i = from; i <= text.size() - needle_len; ++i) {
    size_t j = 0;
    while (j < needle_len && lower(text[i + j]) == lower(needle[j])) {
      ++j;
    }
    if (j == needle_len) {
      return text.data() + i;
    }
  }
  return nullptr;
}

// 解析 CF_HTML 头里 "StartHTML:0000000105" 样式的十进制字节偏移。
bool ParseHeaderOffset(const std::string& raw, const char* key, size_t* out) {
  const size_t key_len = strlen(key);
  const size_t scan_end = std::min(raw.size(), kCfHtmlHeaderScanBytes);
  const auto lower = [](char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  };
  for (size_t i = 0; i + key_len < scan_end; ++i) {
    size_t j = 0;
    while (j < key_len && lower(raw[i + j]) == lower(key[j])) {
      ++j;
    }
    if (j != key_len) {
      continue;
    }
    size_t value = 0;
    bool any_digit = false;
    size_t k = i + key_len;
    while (k < raw.size() && std::isdigit(static_cast<unsigned char>(raw[k]))) {
      value = value * 10 + static_cast<size_t>(raw[k] - '0');
      ++k;
      any_digit = true;
    }
    if (!any_digit) {
      return false;
    }
    *out = value;
    return true;
  }
  return false;
}

}  // namespace

std::string ExtractHtmlFragment(const std::string& raw_cf_html) {
  if (raw_cf_html.empty()) {
    return {};
  }
  const char* start = FindAsciiICase(raw_cf_html, kStartFragmentMark, 0);
  const char* end = FindAsciiICase(raw_cf_html, kEndFragmentMark, 0);
  if (start != nullptr && end != nullptr && end > start) {
    start += strlen(kStartFragmentMark);
    return std::string(start, static_cast<size_t>(end - start));
  }
  // 无 fragment 标记：优先头内 StartFragment/EndFragment 字节偏移（正文精确
  // 范围）；退化到 StartHTML/EndHTML 会截出整个 <html> 文档，嵌套进包壳后
  // 浏览器解析出多余结构（曾表现为内容被撑高、滚动条外露）。
  size_t start_fragment = 0;
  size_t end_fragment = 0;
  const bool has_start =
      ParseHeaderOffset(raw_cf_html, "StartFragment:", &start_fragment) &&
      start_fragment < raw_cf_html.size();
  const bool has_end = ParseHeaderOffset(raw_cf_html, "EndFragment:", &end_fragment) &&
      end_fragment <= raw_cf_html.size() && end_fragment >= start_fragment;
  if (has_start && has_end && end_fragment > start_fragment) {
    return raw_cf_html.substr(start_fragment, end_fragment - start_fragment);
  }
  size_t start_html = 0;
  size_t end_html = raw_cf_html.size();
  size_t value = 0;
  if (ParseHeaderOffset(raw_cf_html, "StartHTML:", &value) && value < raw_cf_html.size()) {
    start_html = value;
  }
  if (ParseHeaderOffset(raw_cf_html, "EndHTML:", &value) && value <= raw_cf_html.size() && value >= start_html) {
    end_html = value;
  }
  if (start_html < end_html) {
    return raw_cf_html.substr(start_html, end_html - start_html);
  }
  return raw_cf_html;
}

PreviewDocStatus BuildPreviewHtmlDoc(
    const std::string& html_fragment_utf8,
    bool dark_theme,
    std::wstring* out_doc) {
  if (out_doc == nullptr) {
    return PreviewDocStatus::kOk;
  }
  *out_doc = {};
  if (html_fragment_utf8.size() > kMaxHtmlFragmentBytes) {
    return PreviewDocStatus::kTooLarge;
  }
  const std::wstring fragment = common::Utf8ToWide(html_fragment_utf8).wide;
  std::wstring doc;
  doc.reserve(fragment.size() + 512);
  doc += L"<!DOCTYPE html><html><head><meta charset=\"utf-8\"><style>";
  doc += dark_theme ? kDocStyleDark : kDocStyleLight;
  doc += L"</style></head><body>";
  doc += fragment;
  doc += L"</body></html>";
  *out_doc = std::move(doc);
  return PreviewDocStatus::kOk;
}

}  // namespace clipd2d::ui
