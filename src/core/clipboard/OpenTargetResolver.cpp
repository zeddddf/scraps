#include "core/clipboard/OpenTargetResolver.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <optional>
#include <vector>

#include "common/StringUtil.h"
#include "common/Utf8.h"

namespace clipd2d::core::clipboard {

namespace {

std::wstring NormalizeToSingleCandidateLine(std::wstring_view text) {
  std::wstring normalized;
  normalized.reserve(text.size());
  for (size_t index = 0; index < text.size(); ++index) {
    const wchar_t ch = text[index];
    if (ch == L'\r') {
      normalized.push_back(L'\n');
      if (index + 1 < text.size() && text[index + 1] == L'\n') {
        ++index;
      }
      continue;
    }
    normalized.push_back(ch);
  }

  std::vector<std::wstring> non_empty_lines;
  for (const auto& part : common::SplitWide(normalized, L'\n')) {
    std::wstring trimmed = common::TrimWide(part);
    if (!trimmed.empty()) {
      non_empty_lines.push_back(std::move(trimmed));
    }
  }

  if (non_empty_lines.size() != 1) {
    return {};
  }
  return non_empty_lines.front();
}

std::wstring StripSingleOuterQuotes(std::wstring value) {
  if (value.size() < 2) {
    return value;
  }
  const wchar_t first = value.front();
  const wchar_t last = value.back();
  if ((first == L'"' && last == L'"') || (first == L'\'' && last == L'\'')) {
    return common::TrimWide(std::wstring_view(value).substr(1, value.size() - 2));
  }
  return value;
}

bool StartsWithHttpScheme(std::wstring_view value) {
  const std::wstring lowered = common::ToLowerWide(value);
  return lowered.starts_with(L"http://") || lowered.starts_with(L"https://");
}

bool IsAbsoluteWindowsPath(std::wstring_view value) {
  if (value.size() >= 3 &&
      std::iswalpha(value[0]) != 0 &&
      value[1] == L':' &&
      (value[2] == L'\\' || value[2] == L'/')) {
    return true;
  }
  return value.size() >= 2 && value[0] == L'\\' && value[1] == L'\\';
}

int HexValue(wchar_t ch) {
  if (ch >= L'0' && ch <= L'9') {
    return static_cast<int>(ch - L'0');
  }
  if (ch >= L'a' && ch <= L'f') {
    return 10 + static_cast<int>(ch - L'a');
  }
  if (ch >= L'A' && ch <= L'F') {
    return 10 + static_cast<int>(ch - L'A');
  }
  return -1;
}

std::optional<std::wstring> DecodePercentEscapes(std::wstring_view value) {
  std::wstring decoded;
  decoded.reserve(value.size());
  for (size_t index = 0; index < value.size(); ++index) {
    const wchar_t ch = value[index];
    if (ch != L'%') {
      decoded.push_back(ch);
      continue;
    }
    if (index + 2 >= value.size()) {
      return std::nullopt;
    }
    const int hi = HexValue(value[index + 1]);
    const int lo = HexValue(value[index + 2]);
    if (hi < 0 || lo < 0) {
      return std::nullopt;
    }
    decoded.push_back(static_cast<wchar_t>((hi << 4) | lo));
    index += 2;
  }
  return decoded;
}

std::optional<std::wstring> TryParseFileUri(std::wstring_view value) {
  const std::wstring lowered = common::ToLowerWide(value);
  if (!lowered.starts_with(L"file:///")) {
    return std::nullopt;
  }

  std::wstring encoded_path(value.substr(8));
  if (encoded_path.empty()) {
    return std::nullopt;
  }

  auto decoded = DecodePercentEscapes(encoded_path);
  if (!decoded.has_value()) {
    return std::nullopt;
  }

  std::replace(decoded->begin(), decoded->end(), L'/', L'\\');
  if (decoded->size() >= 3 &&
      (*decoded)[0] == L'\\' &&
      std::iswalpha((*decoded)[1]) != 0 &&
      (*decoded)[2] == L':') {
    decoded->erase(decoded->begin());
  }

  if (!IsAbsoluteWindowsPath(*decoded)) {
    return std::nullopt;
  }
  return decoded;
}

OpenTarget ResolveExistingPath(const std::wstring& candidate) {
  std::filesystem::path path(candidate);
  std::error_code ec;
  if (!std::filesystem::exists(path, ec) || ec) {
    return {};
  }

  const std::filesystem::path normalized = std::filesystem::weakly_canonical(path, ec);
  const std::wstring resolved = ec ? path.lexically_normal().wstring() : normalized.wstring();
  if (std::filesystem::is_directory(path, ec) && !ec) {
    return OpenTarget{OpenTargetKind::Directory, resolved};
  }
  return OpenTarget{OpenTargetKind::File, resolved};
}

}  // namespace

OpenTarget ResolveOpenTargetFromText(std::string_view utf8_text) {
  const common::Utf8Conversion conversion = common::Utf8ToWide(utf8_text);
  if (!conversion.status.ok()) {
    return {};
  }

  std::wstring candidate = NormalizeToSingleCandidateLine(conversion.wide);
  if (candidate.empty()) {
    return {};
  }

  candidate = common::TrimWide(StripSingleOuterQuotes(std::move(candidate)));
  if (candidate.empty()) {
    return {};
  }

  if (StartsWithHttpScheme(candidate)) {
    return OpenTarget{OpenTargetKind::Url, candidate};
  }

  if (const auto file_uri = TryParseFileUri(candidate); file_uri.has_value()) {
    return ResolveExistingPath(*file_uri);
  }

  if (!IsAbsoluteWindowsPath(candidate)) {
    return {};
  }
  return ResolveExistingPath(candidate);
}

}  // namespace clipd2d::core::clipboard
