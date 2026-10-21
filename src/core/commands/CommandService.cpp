#include "core/commands/CommandService.h"

#include "common/Utf8.h"

namespace clipd2d::core::commands {

namespace {

std::optional<store::ClipContentType> ParseTypeFilter(std::string_view value) {
  if (value.empty()) {
    return std::nullopt;
  }
  if (value == "text") return store::ClipContentType::Text;
  if (value == "html") return store::ClipContentType::Html;
  if (value == "rtf") return store::ClipContentType::Rtf;
  if (value == "image") return store::ClipContentType::Image;
  if (value == "files") return store::ClipContentType::Files;
  return std::nullopt;
}

}  // namespace

void CommandService::SetController(AppController* controller) {
  controller_ = controller;
}

void CommandService::SetShowWindowDispatcher(ShowWindowDispatcher dispatcher) {
  show_window_dispatcher_ = std::move(dispatcher);
}

CommandResponse CommandService::FromStatus(
    common::Status status,
    std::string success_message,
    std::string data) {
  if (!status.ok()) {
    return CommandResponse::Failure("operation_failed", std::move(status.message));
  }
  return CommandResponse::Success("ok", std::move(success_message), std::move(data));
}

CommandResponse CommandService::Handle(const CommandRequest& request) const {
  if (controller_ == nullptr) {
    return CommandResponse::Failure("service_unavailable", "应用控制器尚未初始化。");
  }

  switch (request.id) {
    case CommandId::ShowWindow:
      if (!show_window_dispatcher_) {
        return CommandResponse::Failure("service_unavailable", "主窗口调度器尚未准备好。");
      }
      show_window_dispatcher_();
      return CommandResponse::Success("ok", "主窗口唤起请求已发送。");

    case CommandId::PasteLatest:
      return FromStatus(controller_->PasteLatest(), "最近一条内容已粘贴。");

    case CommandId::PasteById:
      if (!request.clip_id.has_value()) {
        return CommandResponse::Failure("invalid_argument", "paste 缺少 --id。");
      }
      return FromStatus(controller_->PasteById(*request.clip_id), "条目已粘贴。");

    case CommandId::TransformPaste:
      if (!request.clip_id.has_value() || request.action_id.empty()) {
        return CommandResponse::Failure("invalid_argument", "transform-paste 需要同时提供 --id 和 --action。");
      }
      return FromStatus(
          controller_->TransformPasteById(*request.clip_id, request.action_id),
          "变形粘贴已完成。");

    case CommandId::SearchHistory: {
      store::ClipQuery query;
      query.search_text = request.query;
      query.type_filter = ParseTypeFilter(request.type_filter);
      if (!request.type_filter.empty() && !query.type_filter.has_value()) {
        return CommandResponse::Failure("invalid_argument", "无效的 --type 参数。");
      }
      query.source_app_name = common::Utf8ToWide(request.source_filter).wide;
      query.favorite_only = request.favorite_only.value_or(false);
      query.pinned_only = request.pinned_only.value_or(false);
      query.days = request.days;
      std::vector<store::ClipSummary> clips;
      const common::Status status = controller_->ListClips(query, 0, 50, &clips);
      if (!status.ok()) {
        return CommandResponse::Failure("operation_failed", status.message);
      }
      return CommandResponse::Success("ok", "搜索完成。", controller_->BuildSearchJson(clips));
    }
  }

  return CommandResponse::Failure("internal_error", "未知命令。");
}

}  // namespace clipd2d::core::commands
