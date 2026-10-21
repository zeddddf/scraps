#include "ipc/NamedPipeProtocol.h"

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace clipd2d::ipc {

namespace {

using json = nlohmann::json;

// 序列化统一交给 nlohmann；剪贴板文本可能不是合法 UTF-8，
// 用 U+FFFD 替换而不是抛异常（error_handler_t::replace）。
std::string DumpJson(const json& value) {
  return value.dump(-1, ' ', false, json::error_handler_t::replace);
}

// 令牌比较用恒时实现，避免逐字节短路。
bool ConstantTimeEquals(std::string_view lhs, std::string_view rhs) {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  unsigned char diff = 0;
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    diff |= static_cast<unsigned char>(lhs[i]) ^ static_cast<unsigned char>(rhs[i]);
  }
  return diff == 0;
}

}  // namespace

std::string SerializeRequest(const core::commands::CommandRequest& request) {
  return SerializeRequest(request, std::string_view{});
}

std::string SerializeRequest(
    const core::commands::CommandRequest& request,
    std::string_view session_token) {
  json root;
  root["command"] = core::commands::ToCommandName(request.id);

  if (request.clip_id.has_value()) {
    root["clipId"] = *request.clip_id;
  }
  if (!request.query.empty()) {
    root["query"] = request.query;
  }
  if (!request.action_id.empty()) {
    root["actionId"] = request.action_id;
  }
  if (!request.type_filter.empty()) {
    root["typeFilter"] = request.type_filter;
  }
  if (!request.source_filter.empty()) {
    root["sourceFilter"] = request.source_filter;
  }
  if (request.favorite_only.has_value()) {
    root["favoriteOnly"] = *request.favorite_only;
  }
  if (request.pinned_only.has_value()) {
    root["pinnedOnly"] = *request.pinned_only;
  }
  if (request.days > 0) {
    root["days"] = request.days;
  }
  root["wantsJson"] = request.wants_json;
  if (!session_token.empty()) {
    root["token"] = std::string(session_token);
  }
  return DumpJson(root);
}

bool TryDeserializeRequest(
    std::string_view payload,
    core::commands::CommandRequest* request,
    std::string* error_message) {
  if (request == nullptr) {
    if (error_message != nullptr) {
      *error_message = "输出请求对象不能为空。";
    }
    return false;
  }

  json root;
  try {
    root = json::parse(payload);
  } catch (const json::parse_error& e) {
    if (error_message != nullptr) {
      *error_message = std::string("JSON 解析出错：") + e.what();
    }
    return false;
  }

  if (!root.is_object()) {
    if (error_message != nullptr) {
      *error_message = "请求不是合法的 JSON 对象。";
    }
    return false;
  }

  const auto command_it = root.find("command");
  if (command_it == root.end() || !command_it->is_string()) {
    if (error_message != nullptr) {
      *error_message = "请求缺少 command 字段或类型不正确。";
    }
    return false;
  }

  const auto command_id = core::commands::ParseCommandName(command_it->get<std::string>());
  if (!command_id.has_value()) {
    if (error_message != nullptr) {
      *error_message = "请求包含未知命令。";
    }
    return false;
  }

  request->id = *command_id;
  request->clip_id.reset();
  request->query.clear();
  request->action_id.clear();
  request->type_filter.clear();
  request->source_filter.clear();
  request->favorite_only.reset();
  request->pinned_only.reset();
  request->days = 0;
  request->wants_json = false;

  if (auto it = root.find("clipId"); it != root.end() && it->is_number_integer()) {
    request->clip_id = it->get<std::int64_t>();
  }
  if (auto it = root.find("query"); it != root.end() && it->is_string()) {
    request->query = it->get<std::string>();
  }
  if (auto it = root.find("actionId"); it != root.end() && it->is_string()) {
    request->action_id = it->get<std::string>();
  }
  if (auto it = root.find("typeFilter"); it != root.end() && it->is_string()) {
    request->type_filter = it->get<std::string>();
  }
  if (auto it = root.find("sourceFilter"); it != root.end() && it->is_string()) {
    request->source_filter = it->get<std::string>();
  }
  if (auto it = root.find("favoriteOnly"); it != root.end() && it->is_boolean()) {
    request->favorite_only = it->get<bool>();
  }
  if (auto it = root.find("pinnedOnly"); it != root.end() && it->is_boolean()) {
    request->pinned_only = it->get<bool>();
  }
  if (auto it = root.find("days"); it != root.end() && it->is_number_integer()) {
    const std::int64_t days = it->get<std::int64_t>();
    if (days > 0) {
      request->days = static_cast<int>(days);
    }
  }
  if (auto it = root.find("wantsJson"); it != root.end() && it->is_boolean()) {
    request->wants_json = it->get<bool>();
  }

  return true;
}

std::string SerializeResponse(const core::commands::CommandResponse& response) {
  json root;
  root["ok"] = response.ok;
  root["code"] = response.code;
  root["message"] = response.message;
  root["data"] = response.data;
  return DumpJson(root);
}

bool TryDeserializeResponse(
    std::string_view payload,
    core::commands::CommandResponse* response,
    std::string* error_message) {
  if (response == nullptr) {
    if (error_message != nullptr) {
      *error_message = "输出响应对象不能为空。";
    }
    return false;
  }

  json root;
  try {
    root = json::parse(payload);
  } catch (const json::parse_error& e) {
    if (error_message != nullptr) {
      *error_message = std::string("JSON 解析出错：") + e.what();
    }
    return false;
  }

  if (!root.is_object()) {
    if (error_message != nullptr) {
      *error_message = "响应不是合法的 JSON 对象。";
    }
    return false;
  }

  const auto ok_it = root.find("ok");
  if (ok_it == root.end() || !ok_it->is_boolean()) {
    if (error_message != nullptr) {
      *error_message = "响应缺少 ok 字段或类型不正确。";
    }
    return false;
  }

  const auto code_it = root.find("code");
  const auto msg_it = root.find("message");
  const auto data_it = root.find("data");

  if (code_it == root.end() || !code_it->is_string() ||
      msg_it == root.end() || !msg_it->is_string() ||
      data_it == root.end() || !data_it->is_string()) {
    if (error_message != nullptr) {
      *error_message = "响应缺少必要字符串字段或类型不正确。";
    }
    return false;
  }

  response->ok = ok_it->get<bool>();
  response->code = code_it->get<std::string>();
  response->message = msg_it->get<std::string>();
  response->data = data_it->get<std::string>();
  return true;
}

bool TryExtractSessionToken(std::string_view payload, std::string* token) {
  if (token != nullptr) {
    token->clear();
  }

  json root;
  try {
    root = json::parse(payload);
  } catch (const json::exception&) {
    return false;
  }
  if (!root.is_object()) {
    return false;
  }

  const auto it = root.find("token");
  if (it == root.end()) {
    return true;  // 无 token 字段：合法（可协商，视为未握手）
  }
  if (!it->is_string()) {
    return false;
  }
  if (token != nullptr) {
    *token = it->get<std::string>();
  }
  return true;
}

bool IsHelloRequest(std::string_view payload) {
  json root;
  try {
    root = json::parse(payload);
  } catch (const json::exception&) {
    return false;
  }
  if (!root.is_object()) {
    return false;
  }
  const auto it = root.find("command");
  return it != root.end() && it->is_string() && it->get<std::string>() == "hello";
}

bool RequiresHandshake(core::commands::CommandId id) {
  switch (id) {
    case core::commands::CommandId::ShowWindow:
    case core::commands::CommandId::SearchHistory:
      return false;
    case core::commands::CommandId::PasteLatest:
    case core::commands::CommandId::PasteById:
    case core::commands::CommandId::TransformPaste:
      return true;
  }
  // 未知命令按需要握手处理（fail-closed）。
  return true;
}

bool IsSessionTokenAccepted(std::string_view expected, std::string_view provided) {
  if (expected.empty()) {
    // 服务器未配置期望令牌（令牌生成失败的降级路径），退回仅依赖管道 ACL。
    return true;
  }
  return ConstantTimeEquals(expected, provided);
}

}  // namespace clipd2d::ipc
