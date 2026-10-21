#include "common/StringUtil.h"

#include <algorithm>
#include <charconv>
#include <cwctype>

namespace clipd2d::common {

std::string TrimAscii(std::string_view text) {
  size_t begin = 0;
  size_t end = text.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) {
    ++begin;
  }
  while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
    --end;
  }
  return std::string(text.substr(begin, end - begin));
}

std::wstring TrimWide(std::wstring_view text) {
  size_t begin = 0;
  size_t end = text.size();
  while (begin < end && std::iswspace(text[begin])) {
    ++begin;
  }
  while (end > begin && std::iswspace(text[end - 1])) {
    --end;
  }
  return std::wstring(text.substr(begin, end - begin));
}

std::string ToLowerAscii(std::string_view text) {
  std::string lowered(text);
  std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return lowered;
}

std::wstring ToLowerWide(std::wstring_view text) {
  std::wstring lowered(text);
  std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](wchar_t ch) {
    return static_cast<wchar_t>(std::towlower(ch));
  });
  return lowered;
}

std::wstring JoinWide(const std::vector<std::wstring>& values, std::wstring_view separator) {
  std::wstring result;
  for (size_t index = 0; index < values.size(); ++index) {
    if (index > 0) {
      result.append(separator);
    }
    result.append(values[index]);
  }
  return result;
}

std::vector<std::wstring> SplitWide(std::wstring_view text, wchar_t separator) {
  std::vector<std::wstring> parts;
  size_t begin = 0;
  while (begin <= text.size()) {
    const size_t position = text.find(separator, begin);
    if (position == std::wstring_view::npos) {
      parts.push_back(std::wstring(text.substr(begin)));
      break;
    }
    parts.push_back(std::wstring(text.substr(begin, position - begin)));
    begin = position + 1;
  }
  return parts;
}

std::string JsonEscape(std::string_view text) {
  std::string output;
  output.reserve(text.size() + 8);
  for (char ch : text) {
    switch (ch) {
      case '\\':
        output.append("\\\\");
        break;
      case '"':
        output.append("\\\"");
        break;
      case '\n':
        output.append("\\n");
        break;
      case '\r':
        output.append("\\r");
        break;
      case '\t':
        output.append("\\t");
        break;
      default:
        output.push_back(ch);
        break;
    }
  }
  return output;
}

std::wstring NormalizePathForCompare(std::wstring_view path) {
  std::wstring value = ToLowerWide(TrimWide(path));
  std::replace(value.begin(), value.end(), L'/', L'\\');
  return value;
}

std::optional<std::int64_t> ParseInt64(std::string_view text) {
  std::int64_t value = 0;
  const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (ec != std::errc() || ptr != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

}  // namespace clipd2d::common
