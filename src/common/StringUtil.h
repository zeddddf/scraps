#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace clipd2d::common {

std::string TrimAscii(std::string_view text);
std::wstring TrimWide(std::wstring_view text);
std::string ToLowerAscii(std::string_view text);
std::wstring ToLowerWide(std::wstring_view text);
std::wstring JoinWide(const std::vector<std::wstring>& values, std::wstring_view separator);
std::vector<std::wstring> SplitWide(std::wstring_view text, wchar_t separator);
std::string JsonEscape(std::string_view text);
std::wstring NormalizePathForCompare(std::wstring_view path);
std::optional<std::int64_t> ParseInt64(std::string_view text);

}  // namespace clipd2d::common
