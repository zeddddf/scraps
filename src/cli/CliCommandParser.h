#pragma once

#include <string>

#include "core/commands/Command.h"

namespace clipd2d::cli {

struct CliParseResult {
  bool should_exit = false;
  int exit_code = 0;
  std::string output;
  bool has_request = false;
  core::commands::CommandRequest request;
};

CliParseResult ParseCommandLine(int argc, wchar_t* argv[]);

}  // namespace clipd2d::cli
