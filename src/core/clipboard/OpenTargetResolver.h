#pragma once

#include <string>
#include <string_view>

namespace clipd2d::core::clipboard {

enum class OpenTargetKind {
  None = 0,
  Url,
  File,
  Directory,
};

struct OpenTarget {
  OpenTargetKind kind = OpenTargetKind::None;
  std::wstring value;
};

OpenTarget ResolveOpenTargetFromText(std::string_view utf8_text);

}  // namespace clipd2d::core::clipboard
