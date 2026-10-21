#pragma once

#include <string>
#include <string_view>

#include "core/commands/Command.h"

namespace clipd2d::ipc {

inline constexpr wchar_t kCommandPipeName[] = LR"(\\.\pipe\Scraps.CommandService)";

std::string SerializeRequest(const core::commands::CommandRequest& request);
std::string SerializeRequest(
    const core::commands::CommandRequest& request,
    std::string_view session_token);
bool TryDeserializeRequest(
    std::string_view payload,
    core::commands::CommandRequest* request,
    std::string* error_message);

std::string SerializeResponse(const core::commands::CommandResponse& response);
bool TryDeserializeResponse(
    std::string_view payload,
    core::commands::CommandResponse* response,
    std::string* error_message);

// ---- 会话令牌握手（S3）----
// 请求可携带可选字段 "token"（向后兼容：不携带则视为未握手）。
// 探测消息使用 command:"hello"，服务器回 auth_ok / unauthorized，便于协商。
// 提取请求中的会话令牌；JSON 不可解析返回 false，无 token 字段返回 true 且清空。
bool TryExtractSessionToken(std::string_view payload, std::string* token);
// 是否为握手探测消息（command == "hello"）。
bool IsHelloRequest(std::string_view payload);
// 动作命令（触发粘贴/输入合成）必须先通过握手。
bool RequiresHandshake(core::commands::CommandId id);
// 校验令牌：expected 为空表示服务器未配置令牌（降级放行）；
// 否则要求 provided 恒时比较相等。
bool IsSessionTokenAccepted(std::string_view expected, std::string_view provided);

}  // namespace clipd2d::ipc
