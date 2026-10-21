#include "core/paste/TextTransformEngine.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cwctype>
#include <sstream>
#include <string>

#include "common/Utf8.h"

namespace clipd2d::core::paste {

namespace {

std::wstring TrimWide(std::wstring value) {
  const auto not_space = [](wchar_t ch) { return !std::iswspace(ch); };
  const auto begin = std::find_if(value.begin(), value.end(), not_space);
  const auto end = std::find_if(value.rbegin(), value.rend(), not_space).base();
  if (begin >= end) {
    return {};
  }

  return std::wstring(begin, end);
}

common::Status MapCase(DWORD flags, std::string_view input, std::string* output) {
  const auto wide = common::Utf8ToWide(input);
  if (!wide.status.ok()) {
    return wide.status;
  }

  std::wstring mapped;
  mapped.resize(wide.wide.size());

  const int result = LCMapStringEx(
      LOCALE_NAME_USER_DEFAULT,
      flags,
      wide.wide.data(),
      static_cast<int>(wide.wide.size()),
      mapped.data(),
      static_cast<int>(mapped.size()),
      nullptr,
      nullptr,
      0);
  if (result <= 0) {
    return common::Status::Error(
        common::StatusCode::PlatformError,
        "Windows 大小写映射失败。");
  }

  const auto utf8 = common::WideToUtf8(mapped);
  if (!utf8.status.ok()) {
    return utf8.status;
  }

  *output = utf8.utf8;
  return common::Status::Ok();
}

std::string CamelCase(std::wstring_view input) {
  std::wstring result;
  result.reserve(input.size());

  bool capitalize_next = false;
  bool first_word = true;

  for (wchar_t ch : input) {
    if (!std::iswalnum(ch)) {
      capitalize_next = true;
      continue;
    }

    if (result.empty()) {
      result.push_back(static_cast<wchar_t>(std::towlower(ch)));
      first_word = false;
      capitalize_next = false;
      continue;
    }

    if (capitalize_next) {
      result.push_back(static_cast<wchar_t>(std::towupper(ch)));
      capitalize_next = false;
      continue;
    }

    if (first_word) {
      result.push_back(static_cast<wchar_t>(std::towlower(ch)));
    } else {
      result.push_back(static_cast<wchar_t>(std::towlower(ch)));
    }
  }

  return common::WideToUtf8(result).utf8;
}

common::Status NormalizeNewlines(std::string_view input, std::string* output) {
  const auto wide = common::Utf8ToWide(input);
  if (!wide.status.ok()) {
    return wide.status;
  }

  std::wstring normalized;
  normalized.reserve(wide.wide.size());
  for (size_t index = 0; index < wide.wide.size(); ++index) {
    const wchar_t ch = wide.wide[index];
    if (ch == L'\r') {
      if (index + 1 < wide.wide.size() && wide.wide[index + 1] == L'\n') {
        ++index;
      }
      normalized.push_back(L'\n');
      continue;
    }
    if (ch == 0x2028 || ch == 0x2029) {
      normalized.push_back(L'\n');
      continue;
    }
    normalized.push_back(ch);
  }

  const auto utf8 = common::WideToUtf8(normalized);
  if (!utf8.status.ok()) {
    return utf8.status;
  }
  *output = utf8.utf8;
  return common::Status::Ok();
}

common::Status PowerShellHereString(std::string_view input, std::string* output) {
  std::string normalized;
  const common::Status status = NormalizeNewlines(input, &normalized);
  if (!status.ok()) {
    return status;
  }

  std::string wrapped = "@'\n";
  wrapped += normalized;
  if (wrapped.empty() || wrapped.back() != '\n') {
    wrapped.push_back('\n');
  }
  wrapped += "'@";
  *output = std::move(wrapped);
  return common::Status::Ok();
}

std::string UrlEncode(std::string_view input) {
  static const char kHex[] = "0123456789ABCDEF";
  std::string output;
  output.reserve(input.size() * 3);

  for (unsigned char ch : input) {
    const bool is_unreserved =
        std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~';
    if (is_unreserved) {
      output.push_back(static_cast<char>(ch));
      continue;
    }

    output.push_back('%');
    output.push_back(kHex[(ch >> 4) & 0x0F]);
    output.push_back(kHex[ch & 0x0F]);
  }

  return output;
}

int HexValue(char ch) {
  if (ch >= '0' && ch <= '9') {
    return ch - '0';
  }
  if (ch >= 'a' && ch <= 'f') {
    return 10 + (ch - 'a');
  }
  if (ch >= 'A' && ch <= 'F') {
    return 10 + (ch - 'A');
  }
  return -1;
}

common::Status UrlDecode(std::string_view input, std::string* output) {
  std::string decoded;
  decoded.reserve(input.size());

  for (size_t index = 0; index < input.size(); ++index) {
    const char current = input[index];
    if (current == '+') {
      decoded.push_back(' ');
      continue;
    }

    if (current == '%' && index + 2 < input.size()) {
      const int high = HexValue(input[index + 1]);
      const int low = HexValue(input[index + 2]);
      if (high < 0 || low < 0) {
        return common::Status::Error(
            common::StatusCode::InvalidArgument,
            "URL Decode 输入包含非法转义。");
      }

      decoded.push_back(static_cast<char>((high << 4) | low));
      index += 2;
      continue;
    }

    decoded.push_back(current);
  }

  *output = std::move(decoded);
  return common::Status::Ok();
}

common::Status JsonPretty(std::string_view input, std::string* output) {
  std::string formatted;
  formatted.reserve(input.size() + 16);

  int depth = 0;
  bool in_string = false;
  bool escaped = false;

  auto append_indent = [&formatted, &depth]() {
    formatted.append(static_cast<size_t>(depth) * 2, ' ');
  };

  for (char ch : input) {
    if (escaped) {
      formatted.push_back(ch);
      escaped = false;
      continue;
    }

    if (ch == '\\') {
      formatted.push_back(ch);
      escaped = true;
      continue;
    }

    if (ch == '"') {
      formatted.push_back(ch);
      in_string = !in_string;
      continue;
    }

    if (in_string) {
      formatted.push_back(ch);
      continue;
    }

    switch (ch) {
      case '{':
      case '[':
        formatted.push_back(ch);
        formatted.push_back('\n');
        ++depth;
        append_indent();
        break;

      case '}':
      case ']':
        formatted.push_back('\n');
        depth = std::max(0, depth - 1);
        append_indent();
        formatted.push_back(ch);
        break;

      case ',':
        formatted.push_back(ch);
        formatted.push_back('\n');
        append_indent();
        break;

      case ':':
        formatted.append(": ");
        break;

      case ' ':
      case '\t':
      case '\r':
      case '\n':
        break;

      default:
        formatted.push_back(ch);
        break;
    }
  }

  *output = std::move(formatted);
  return common::Status::Ok();
}

}  // namespace

common::Status TextTransformEngine::Transform(
    std::string_view action_id,
    std::string_view input,
    std::string* output) const {
  if (output == nullptr) {
    return common::Status::Error(
        common::StatusCode::InvalidArgument,
        "输出参数不能为空。");
  }

  if (action_id == "upper") {
    return MapCase(LCMAP_UPPERCASE, input, output);
  }

  if (action_id == "lower") {
    return MapCase(LCMAP_LOWERCASE, input, output);
  }

  if (action_id == "trim") {
    const auto wide = common::Utf8ToWide(input);
    if (!wide.status.ok()) {
      return wide.status;
    }

    const auto utf8 = common::WideToUtf8(TrimWide(wide.wide));
    if (!utf8.status.ok()) {
      return utf8.status;
    }

    *output = utf8.utf8;
    return common::Status::Ok();
  }

  if (action_id == "camel") {
    const auto wide = common::Utf8ToWide(input);
    if (!wide.status.ok()) {
      return wide.status;
    }

    *output = CamelCase(wide.wide);
    return common::Status::Ok();
  }

  if (action_id == "url-encode") {
    *output = UrlEncode(input);
    return common::Status::Ok();
  }

  if (action_id == "url-decode") {
    return UrlDecode(input, output);
  }

  if (action_id == "json-pretty") {
    return JsonPretty(input, output);
  }

  if (action_id == "powershell-here-string") {
    return PowerShellHereString(input, output);
  }

  return common::Status::Error(
      common::StatusCode::NotImplemented,
      "暂不支持的文本变换动作。");
}

}  // namespace clipd2d::core::paste
