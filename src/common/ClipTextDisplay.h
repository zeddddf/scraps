#pragma once

#include <cwctype>
#include <string>
#include <string_view>

#include "common/StringUtil.h"
#include "common/Utf8.h"
#include "core/store/ClipTypes.h"

namespace clipd2d::common::clip_text_display {

inline std::wstring RecoverWideText(std::string_view text) {
  if (text.empty()) {
    return {};
  }

  auto converted = Utf8ToWide(text);
  if (converted.status.ok() && !converted.wide.empty()) {
    return converted.wide;
  }

  for (size_t length = text.size(); length > 0; --length) {
    converted = Utf8ToWide(text.substr(0, length));
    if (converted.status.ok() && !converted.wide.empty()) {
      return converted.wide;
    }
  }

  return {};
}

inline std::wstring NormalizeWideText(std::wstring_view text, bool preserve_line_breaks) {
  std::wstring normalized;
  normalized.reserve(text.size());

  bool pending_space = false;
  bool line_has_content = false;

  for (size_t index = 0; index < text.size(); ++index) {
    wchar_t ch = text[index];
    if (ch == L'\r') {
      ch = L'\n';
    }

    if (ch == L'\n') {
      if (!preserve_line_breaks) {
        pending_space = line_has_content;
        continue;
      }

      while (!normalized.empty() && normalized.back() == L' ') {
        normalized.pop_back();
      }
      if (!normalized.empty() && normalized.back() != L'\n') {
        normalized.push_back(L'\n');
      }
      pending_space = false;
      line_has_content = false;
      continue;
    }

    if (std::iswspace(ch) != 0) {
      pending_space = line_has_content;
      continue;
    }

    if (pending_space && !normalized.empty() && normalized.back() != L'\n') {
      normalized.push_back(L' ');
    }
    pending_space = false;
    normalized.push_back(ch);
    line_has_content = true;
  }

  while (!normalized.empty() &&
         (normalized.back() == L' ' || normalized.back() == L'\n')) {
    normalized.pop_back();
  }

  return TrimWide(normalized);
}

inline std::wstring NormalizeSingleLineText(std::string_view text) {
  return NormalizeWideText(RecoverWideText(text), false);
}

inline std::wstring NormalizeMultilineText(std::string_view text) {
  return NormalizeWideText(RecoverWideText(text), true);
}

inline std::wstring ExtractFirstLine(std::string_view text) {
  const std::wstring wide = RecoverWideText(text);
  const size_t pos = wide.find_first_of(L"\r\n");
  return pos == std::wstring::npos ? wide : wide.substr(0, pos);
}

inline std::wstring TrimForDisplay(std::wstring text, size_t limit) {
  if (text.size() <= limit) {
    return text;
  }
  text.resize(limit);
  text += L"...";
  return text;
}

inline const wchar_t* ContentFallbackLabel(core::store::ClipContentType type) {
  switch (type) {
    case core::store::ClipContentType::Html:
      return L"HTML Content";
    case core::store::ClipContentType::Rtf:
      return L"RTF Content";
    case core::store::ClipContentType::Text:
      return L"Text Content";
    case core::store::ClipContentType::Image:
      return L"Image";
    case core::store::ClipContentType::Files:
      return L"Files";
  }
  return L"Clip";
}

inline std::wstring SummaryTitle(const core::store::ClipSummary& summary) {
  if (!summary.title.empty()) {
    const std::wstring title = NormalizeSingleLineText(summary.title);
    if (!title.empty()) {
      return TrimForDisplay(title, 36);
    }
  }
  if (!summary.snippet.empty()) {
    const std::wstring snippet = TrimWide(ExtractFirstLine(summary.snippet));
    if (!snippet.empty()) {
      return TrimForDisplay(snippet, 36);
    }
  }
  return L"Empty Clip";
}

inline std::wstring SummarySnippet(const core::store::ClipSummary& summary) {
  if (summary.content_type == core::store::ClipContentType::Image) {
    return summary.image_width > 0 && summary.image_height > 0 ? L"Image Preview" : L"Image";
  }
  if (summary.content_type == core::store::ClipContentType::Files) {
    return TrimForDisplay(TrimWide(ExtractFirstLine(summary.snippet)), 48);
  }
  return TrimForDisplay(NormalizeMultilineText(summary.snippet), 72);
}

inline std::wstring RowContentText(const core::store::ClipSummary& summary) {
  if (!summary.snippet.empty()) {
    const std::wstring snippet = NormalizeMultilineText(summary.snippet);
    if (!snippet.empty()) {
      return snippet;
    }
  }
  if (!summary.title.empty()) {
    const std::wstring title = NormalizeMultilineText(summary.title);
    if (!title.empty()) {
      return title;
    }
  }
  return ContentFallbackLabel(summary.content_type);
}

inline std::wstring ClampToLineCount(std::wstring_view text, size_t max_lines) {
  if (max_lines == 0 || text.empty()) {
    return {};
  }

  size_t line_count = 1;
  size_t cut_position = text.size();
  for (size_t index = 0; index < text.size(); ++index) {
    if (text[index] != L'\n') {
      continue;
    }
    if (line_count >= max_lines) {
      cut_position = index;
      break;
    }
    ++line_count;
  }

  return TrimWide(std::wstring(text.substr(0, cut_position)));
}

inline std::wstring QuickPastePrimaryText(const core::store::ClipSummary& summary, size_t max_lines = 2) {
  return ClampToLineCount(RowContentText(summary), max_lines);
}

}  // namespace clipd2d::common::clip_text_display
