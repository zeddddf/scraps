#include "ui/ClipRowText.h"

#include <algorithm>
#include <filesystem>
#include <cstdio>

#include "common/Utf8.h"
#include "core/browser/ClipTextDisplay.h"

namespace clipd2d::ui {

std::wstring ExtractFirstLine(std::string_view text) {
  return common::clip_text_display::ExtractFirstLine(text);
}


std::wstring ToWide(std::string_view text) {
  return common::Utf8ToWide(text).wide;
}


std::wstring FormatByteSize(std::uintmax_t bytes) {
  static constexpr const wchar_t* kUnits[] = {L"B", L"KB", L"MB", L"GB", L"TB"};
  constexpr size_t kUnitCount = sizeof(kUnits) / sizeof(kUnits[0]);
  double value = static_cast<double>(bytes);
  size_t unit_index = 0;
  while (value >= 1024.0 && unit_index + 1 < kUnitCount) {
    value /= 1024.0;
    ++unit_index;
  }

  wchar_t buffer[32]{};
  if (unit_index == 0) {
    swprintf_s(buffer, L"%llu %ls", static_cast<unsigned long long>(bytes), kUnits[unit_index]);
  } else if (value >= 100.0) {
    swprintf_s(buffer, L"%.0f %ls", value, kUnits[unit_index]);
  } else {
    swprintf_s(buffer, L"%.1f %ls", value, kUnits[unit_index]);
  }
  return buffer;
}


std::wstring FileDisplayName(const std::wstring& path) {
  if (path.empty()) {
    return L"";
  }
  std::filesystem::path fs_path(path);
  std::wstring name = fs_path.filename().wstring();
  if (name.empty()) {
    name = fs_path.wstring();
  }
  return name;
}


std::wstring FileTrailingText(const core::store::ClipSummary& summary, const std::wstring& first_path) {
  if (summary.file_count > 1) {
    return L"\x5171 " + std::to_wstring(summary.file_count) + L" \x9879";
  }
  if (first_path.empty()) {
    return L"";
  }

  std::error_code ec;
  std::filesystem::path fs_path(first_path);
  if (std::filesystem::is_directory(fs_path, ec)) {
    return L"\x6587\x4EF6\x5939";
  }

  const auto file_size = std::filesystem::file_size(fs_path, ec);
  if (!ec) {
    return FormatByteSize(file_size);
  }
  if (_wcsicmp(fs_path.extension().c_str(), L".exe") == 0) {
    return L"\x7A0B\x5E8F";
  }
  return L"\x6587\x4EF6";
}


std::wstring FilePathLine(const core::store::ClipSummary& summary, const std::wstring& first_path) {
  if (summary.file_count > 1) {
    std::wstring line;
    if (!summary.snippet.empty()) {
      line = ToWide(summary.snippet);
    } else if (!first_path.empty()) {
      std::filesystem::path fs_path(first_path);
      line = fs_path.has_parent_path() ? fs_path.parent_path().wstring() : first_path;
    }
    if (!line.empty()) {
      line += L"  \x00B7  ";
    }
    line += L"\x53E6 " + std::to_wstring(std::max(1, summary.file_count - 1)) + L" \x9879";
    return line;
  }
  if (!first_path.empty()) {
    return first_path;
  }
  return ExtractFirstLine(summary.snippet);
}


std::wstring SummaryTitle(const core::store::ClipSummary& summary) {
  return common::clip_text_display::SummaryTitle(summary);
}


std::wstring RowContentText(const core::store::ClipSummary& summary) {
  return common::clip_text_display::RowContentText(summary);
}


std::wstring FirstFilePathFromSummary(const core::store::ClipSummary& summary) {
  if (summary.content_type == core::store::ClipContentType::Files && !summary.storage_path.empty()) {
    return common::TrimWide(summary.storage_path);
  }
  if (summary.content_type == core::store::ClipContentType::Files && !summary.secondary_path.empty()) {
    return common::TrimWide(summary.secondary_path);
  }
  return common::TrimWide(ExtractFirstLine(summary.snippet));
}


bool SupportsSystemThumbnailPreviewPath(const std::wstring& path) {
  return IsImageFilePath(path) || IsVideoFilePath(path);
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


bool IsVideoFilePath(const std::wstring& path) {
  if (path.empty()) {
    return false;
  }

  const std::wstring extension = LowercaseWide(std::filesystem::path(path).extension().wstring());
  return extension == L".mp4" || extension == L".mov" || extension == L".m4v" ||
         extension == L".avi" || extension == L".mkv" || extension == L".webm" ||
         extension == L".wmv";
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

}  // namespace clipd2d::ui
