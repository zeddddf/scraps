#pragma once

#include <string>
#include <string_view>

#include "common/Status.h"

namespace clipd2d::common {

struct Utf8Conversion {
  Status status;
  std::string utf8;
  std::wstring wide;
};

Utf8Conversion Utf8ToWide(std::string_view utf8_text);
Utf8Conversion WideToUtf8(std::wstring_view wide_text);

}  // namespace clipd2d::common
