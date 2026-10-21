#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace clipd2d::core::commands {

enum class CommandId {
  ShowWindow,
  PasteLatest,
  PasteById,
  TransformPaste,
  SearchHistory,
  ExportBackup,
  ImportBackup,
};

struct CommandRequest {
  CommandId id = CommandId::ShowWindow;
  std::optional<std::int64_t> clip_id;
  std::string query;
  std::string action_id;
  std::string type_filter;
  std::string source_filter;
  std::optional<bool> favorite_only;
  std::optional<bool> pinned_only;
  std::string path;
  std::string secret;
  int days = 0;
  bool wants_json = false;
};

struct CommandResponse {
  bool ok = false;
  std::string code;
  std::string message;
  std::string data;

  static CommandResponse Success(
      std::string code,
      std::string message,
      std::string data = {});

  static CommandResponse Failure(
      std::string code,
      std::string message,
      std::string data = {});
};

const char* ToCommandName(CommandId id);
std::optional<CommandId> ParseCommandName(std::string_view name);

}  // namespace clipd2d::core::commands
