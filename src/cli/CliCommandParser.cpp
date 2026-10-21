#include "cli/CliCommandParser.h"

#include <charconv>
#include <vector>

#include "common/Utf8.h"

namespace clipd2d::cli {

namespace {

CliParseResult MakeError(std::string message) {
  CliParseResult result;
  result.should_exit = true;
  result.exit_code = 1;
  result.output = std::move(message);
  return result;
}

CliParseResult MakeHelp() {
  CliParseResult result;
  result.should_exit = true;
  result.exit_code = 0;
  result.output =
      "scraps-cli 用法:\n"
      "  scraps-cli show\n"
      "  scraps-cli paste-latest\n"
      "  scraps-cli paste --id <id>\n"
      "  scraps-cli transform-paste --id <id> --action <action>\n"
      "  scraps-cli search <keyword> [--type <text|html|rtf|image|files>] [--source <app>] [--favorite] [--pinned] [--days <n>] [--json]\n";
  return result;
}

std::vector<std::string> ConvertArgs(int argc, wchar_t* argv[]) {
  std::vector<std::string> args;
  args.reserve(static_cast<size_t>(argc));
  for (int index = 0; index < argc; ++index) {
    args.push_back(common::WideToUtf8(argv[index]).utf8);
  }
  return args;
}

bool IsFlag(const std::string& arg, const char* name) {
  return arg == name;
}

bool TryParseInt64(const std::string& text, std::int64_t* value) {
  const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), *value);
  return ec == std::errc() && ptr == text.data() + text.size();
}

}  // namespace

CliParseResult ParseCommandLine(int argc, wchar_t* argv[]) {
  if (argc <= 1) {
    return MakeHelp();
  }

  const std::vector<std::string> args = ConvertArgs(argc, argv);
  const std::string& command_name = args[1];
  if (command_name == "--help" || command_name == "-h") {
    return MakeHelp();
  }

  const auto command_id = core::commands::ParseCommandName(command_name);
  if (!command_id.has_value()) {
    return MakeError("未知命令。使用 --help 查看可用命令。");
  }

  CliParseResult result;
  result.has_request = true;
  result.request.id = *command_id;

  for (size_t index = 2; index < args.size(); ++index) {
    const std::string& current = args[index];

    if (IsFlag(current, "--json")) {
      result.request.wants_json = true;
      continue;
    }
    if (IsFlag(current, "--favorite")) {
      result.request.favorite_only = true;
      continue;
    }
    if (IsFlag(current, "--pinned")) {
      result.request.pinned_only = true;
      continue;
    }
    if (IsFlag(current, "--id")) {
      if (index + 1 >= args.size()) {
        return MakeError("--id 缺少参数。");
      }
      std::int64_t clip_id = 0;
      if (!TryParseInt64(args[++index], &clip_id)) {
        return MakeError("--id 必须是整数。");
      }
      result.request.clip_id = clip_id;
      continue;
    }
    if (IsFlag(current, "--action")) {
      if (index + 1 >= args.size()) {
        return MakeError("--action 缺少参数。");
      }
      result.request.action_id = args[++index];
      continue;
    }
    if (IsFlag(current, "--type")) {
      if (index + 1 >= args.size()) {
        return MakeError("--type 缺少参数。");
      }
      result.request.type_filter = args[++index];
      continue;
    }
    if (IsFlag(current, "--source")) {
      if (index + 1 >= args.size()) {
        return MakeError("--source 缺少参数。");
      }
      result.request.source_filter = args[++index];
      continue;
    }
    if (IsFlag(current, "--days")) {
      if (index + 1 >= args.size()) {
        return MakeError("--days 缺少参数。");
      }
      std::int64_t days = 0;
      if (!TryParseInt64(args[++index], &days) || days < 0 || days > 3650) {
        return MakeError("--days 必须是 0 到 3650 之间的整数。");
      }
      result.request.days = static_cast<int>(days);
      continue;
    }

    if (result.request.id == core::commands::CommandId::SearchHistory &&
        result.request.query.empty()) {
      result.request.query = current;
      continue;
    }

    return MakeError("命令参数无效。使用 --help 查看格式。");
  }

  if (result.request.id == core::commands::CommandId::PasteById &&
      !result.request.clip_id.has_value()) {
    return MakeError("paste 命令必须提供 --id。");
  }

  if (result.request.id == core::commands::CommandId::TransformPaste) {
    if (!result.request.clip_id.has_value()) {
      return MakeError("transform-paste 命令必须提供 --id。");
    }
    if (result.request.action_id.empty()) {
      return MakeError("transform-paste 命令必须提供 --action。");
    }
  }

  if (result.request.id == core::commands::CommandId::SearchHistory &&
      result.request.query.empty()) {
    return MakeError("search 命令必须提供关键字。");
  }

  return result;
}

}  // namespace clipd2d::cli
