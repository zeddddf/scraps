#include "core/commands/Command.h"

namespace clipd2d::core::commands {

CommandResponse CommandResponse::Success(
    std::string code,
    std::string message,
    std::string data) {
  return CommandResponse{true, std::move(code), std::move(message), std::move(data)};
}

CommandResponse CommandResponse::Failure(
    std::string code,
    std::string message,
    std::string data) {
  return CommandResponse{false, std::move(code), std::move(message), std::move(data)};
}

const char* ToCommandName(CommandId id) {
  switch (id) {
    case CommandId::ShowWindow:
      return "show";
    case CommandId::PasteLatest:
      return "paste-latest";
    case CommandId::PasteById:
      return "paste";
    case CommandId::TransformPaste:
      return "transform-paste";
    case CommandId::SearchHistory:
      return "search";
    case CommandId::ExportBackup:
      return "export-data";
    case CommandId::ImportBackup:
      return "import-data";
  }

  return "unknown";
}

std::optional<CommandId> ParseCommandName(std::string_view name) {
  if (name == "show") {
    return CommandId::ShowWindow;
  }
  if (name == "paste-latest") {
    return CommandId::PasteLatest;
  }
  if (name == "paste") {
    return CommandId::PasteById;
  }
  if (name == "transform-paste") {
    return CommandId::TransformPaste;
  }
  if (name == "search") {
    return CommandId::SearchHistory;
  }
  if (name == "export-data") {
    return CommandId::ExportBackup;
  }
  if (name == "import-data") {
    return CommandId::ImportBackup;
  }

  return std::nullopt;
}

}  // namespace clipd2d::core::commands
