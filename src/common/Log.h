#pragma once

#include <string_view>

namespace clipd2d::common {

enum class LogLevel {
  Info,
  Warning,
  Error,
};

void WriteLog(LogLevel level, std::string_view category, std::string_view message);

}  // namespace clipd2d::common
