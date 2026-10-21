#include "tests/TestSupport.h"

#include <cstdint>
#include <string>
#include <vector>

#include "ipc/NamedPipeProtocol.h"
#include "ipc/SessionToken.h"

namespace {

using clipd2d::core::commands::CommandId;
using clipd2d::core::commands::CommandRequest;
using clipd2d::core::commands::CommandResponse;
using clipd2d::ipc::TryDeserializeRequest;
using clipd2d::ipc::TryDeserializeResponse;
using clipd2d::ipc::SerializeRequest;
using clipd2d::ipc::SerializeResponse;

}  // namespace

CLIPD2D_TEST(NamedPipeProtocolParseNormalRequest) {
  std::string payload = R"({"command":"search","clipId":1234,"query":"hello world","wantsJson":true})";
  CommandRequest request;
  std::string error;
  
  bool ok = TryDeserializeRequest(payload, &request, &error);
  if (!ok) {
    printf("PARSE ERROR: %s\n", error.c_str());
  }
  CLIPD2D_CHECK(ok);
  CLIPD2D_CHECK_EQ(request.id, CommandId::SearchHistory);
  CLIPD2D_CHECK(request.clip_id.has_value());
  CLIPD2D_CHECK_EQ(*request.clip_id, static_cast<std::int64_t>(1234));
  CLIPD2D_CHECK_EQ(request.query, std::string("hello world"));
  CLIPD2D_CHECK_EQ(request.wants_json, true);
}

CLIPD2D_TEST(NamedPipeProtocolParseReorderedAndSpacedRequest) {
  std::string payload = R"(
    {
      "query" : "spaced query",
      "wantsJson"  : false,
      "command" : "paste-latest",
      "clipId": 999
    }
  )";
  CommandRequest request;
  std::string error;
  
  CLIPD2D_CHECK(TryDeserializeRequest(payload, &request, &error));
  CLIPD2D_CHECK_EQ(request.id, CommandId::PasteLatest);
  CLIPD2D_CHECK(request.clip_id.has_value());
  CLIPD2D_CHECK_EQ(*request.clip_id, static_cast<std::int64_t>(999));
  CLIPD2D_CHECK_EQ(request.query, std::string("spaced query"));
  CLIPD2D_CHECK_EQ(request.wants_json, false);
}

CLIPD2D_TEST(NamedPipeProtocolParseUnicodeEscapesAndSubstrings) {
  // Test case with \uXXXX and matching keys inside query
  std::string payload = R"({"command":"search","query":"Test with \u4e2d\u6587 and key \"command\": \"fake_command\" and emoji \ud83d\ude00"})";
  CommandRequest request;
  std::string error;
  
  CLIPD2D_CHECK(TryDeserializeRequest(payload, &request, &error));
  CLIPD2D_CHECK_EQ(request.id, CommandId::SearchHistory);
  // \u4e2d\u6587 is "中文" in UTF-8
  CLIPD2D_CHECK(request.query.find("中文") != std::string::npos);
  // "command" key inside query string must not throw off the command parser
  CLIPD2D_CHECK(request.query.find("\"command\": \"fake_command\"") != std::string::npos);
}

CLIPD2D_TEST(NamedPipeProtocolIgnoreUnusedFieldsAndArrays) {
  std::string payload = R"({
    "command": "show",
    "unknownField": {
      "nestedKey": [1, 2, 3]
    },
    "arrayField": [
      "one", "two", {"three": 3}
    ],
    "wantsJson": true
  })";
  CommandRequest request;
  std::string error;
  
  CLIPD2D_CHECK(TryDeserializeRequest(payload, &request, &error));
  CLIPD2D_CHECK_EQ(request.id, CommandId::ShowWindow);
  CLIPD2D_CHECK_EQ(request.wants_json, true);
}

CLIPD2D_TEST(NamedPipeProtocolHandleMalformedJson) {
  std::vector<std::string> malformed_payloads = {
    R"({"command": "show")", // unclosed object
    R"({"command": show})", // unquoted string
    R"({"command": "show", "clipId": })", // missing value
    R"({"command": "show", "clipId": 123,)", // trailing comma without field
    R"({"command": "unknown_command"})", // invalid command
  };
  
  for (const auto& payload : malformed_payloads) {
    CommandRequest request;
    std::string error;
    CLIPD2D_CHECK(!TryDeserializeRequest(payload, &request, &error));
  }
}

CLIPD2D_TEST(NamedPipeProtocolRoundtripResponse) {
  CommandResponse response;
  response.ok = true;
  response.code = "success";
  response.message = "Message with \"quotes\" and \n newlines.";
  response.data = "Data block";
  
  std::string serialized = SerializeResponse(response);
  CommandResponse deserialized;
  std::string error;
  
  CLIPD2D_CHECK(TryDeserializeResponse(serialized, &deserialized, &error));
  CLIPD2D_CHECK_EQ(deserialized.ok, true);
  CLIPD2D_CHECK_EQ(deserialized.code, response.code);
  CLIPD2D_CHECK_EQ(deserialized.message, response.message);
  CLIPD2D_CHECK_EQ(deserialized.data, response.data);
}

CLIPD2D_TEST(NamedPipeProtocolRequestEscapingRoundtrip) {
  // 引号、反斜杠、控制符、Unicode（BMP + 代理对）的序列化/解析 roundtrip。
  CommandRequest request;
  request.id = CommandId::SearchHistory;
  request.query =
      "quote \" backslash C:\\dir\\file newline\n tab\t end \xE4\xB8\xAD\xE6\x96\x87 "
      "\xF0\x9F\x98\x80";
  request.action_id = "act\\\"id";
  request.type_filter = "text/*";

  const std::string serialized = SerializeRequest(request);
  CommandRequest deserialized;
  std::string error;
  CLIPD2D_CHECK(TryDeserializeRequest(serialized, &deserialized, &error));
  CLIPD2D_CHECK_EQ(deserialized.id, CommandId::SearchHistory);
  CLIPD2D_CHECK_EQ(deserialized.query, request.query);
  CLIPD2D_CHECK_EQ(deserialized.action_id, request.action_id);
  CLIPD2D_CHECK_EQ(deserialized.type_filter, request.type_filter);
}

CLIPD2D_TEST(NamedPipeProtocolRequestTokenRoundtrip) {
  CommandRequest request;
  request.id = CommandId::PasteLatest;
  request.clip_id = 42;

  // 带 token 的请求：序列化后可提取出同一令牌。
  const std::string serialized = SerializeRequest(request, "feedface01234567");
  std::string token;
  CLIPD2D_CHECK(clipd2d::ipc::TryExtractSessionToken(serialized, &token));
  CLIPD2D_CHECK_EQ(token, std::string("feedface01234567"));

  // 不带 token 的请求（向后兼容）：提取成功且令牌为空。
  std::string legacy_payload = R"({"command":"paste-latest","clipId":42})";
  std::string legacy_token = "should-be-cleared";
  CLIPD2D_CHECK(clipd2d::ipc::TryExtractSessionToken(legacy_payload, &legacy_token));
  CLIPD2D_CHECK(legacy_token.empty());

  // token 字段类型错误：提取失败。
  CLIPD2D_CHECK(!clipd2d::ipc::TryExtractSessionToken(R"({"command":"show","token":123})", &token));
}

CLIPD2D_TEST(NamedPipeProtocolHelloRequestDetection) {
  CLIPD2D_CHECK(clipd2d::ipc::IsHelloRequest(R"({"command":"hello","token":"abc"})"));
  CLIPD2D_CHECK(!clipd2d::ipc::IsHelloRequest(R"({"command":"search"})"));
  CLIPD2D_CHECK(!clipd2d::ipc::IsHelloRequest(R"({"command": show})"));  // 畸形 JSON
  CLIPD2D_CHECK(!clipd2d::ipc::IsHelloRequest(""));
}

CLIPD2D_TEST(NamedPipeProtocolSessionTokenAcceptReject) {
  // 未配置期望令牌（降级路径）：放行。
  CLIPD2D_CHECK(clipd2d::ipc::IsSessionTokenAccepted("", "anything"));
  CLIPD2D_CHECK(clipd2d::ipc::IsSessionTokenAccepted("", ""));

  // 令牌匹配：接受。
  CLIPD2D_CHECK(clipd2d::ipc::IsSessionTokenAccepted("tok-123", "tok-123"));

  // 令牌不匹配 / 缺失 / 过长 / 过短：拒绝。
  CLIPD2D_CHECK(!clipd2d::ipc::IsSessionTokenAccepted("tok-123", "tok-124"));
  CLIPD2D_CHECK(!clipd2d::ipc::IsSessionTokenAccepted("tok-123", ""));
  CLIPD2D_CHECK(!clipd2d::ipc::IsSessionTokenAccepted("tok-123", "tok-123-extra"));
  CLIPD2D_CHECK(!clipd2d::ipc::IsSessionTokenAccepted("tok-123", "tok-12"));
}

CLIPD2D_TEST(NamedPipeProtocolActionCommandsRequireHandshake) {
  // 动作命令（触发粘贴/输入合成）必须通过握手。
  CLIPD2D_CHECK(clipd2d::ipc::RequiresHandshake(CommandId::PasteLatest));
  CLIPD2D_CHECK(clipd2d::ipc::RequiresHandshake(CommandId::PasteById));
  CLIPD2D_CHECK(clipd2d::ipc::RequiresHandshake(CommandId::TransformPaste));

  // 非动作命令（显示窗口/查询历史）保持向后兼容，无需握手。
  CLIPD2D_CHECK(!clipd2d::ipc::RequiresHandshake(CommandId::ShowWindow));
  CLIPD2D_CHECK(!clipd2d::ipc::RequiresHandshake(CommandId::SearchHistory));
}

CLIPD2D_TEST(NamedPipeSessionTokenGeneration) {
  const std::string first = clipd2d::ipc::GenerateSessionToken();
  const std::string second = clipd2d::ipc::GenerateSessionToken();

  // 64 位十六进制（32 字节随机数）。
  CLIPD2D_CHECK_EQ(first.size(), static_cast<std::size_t>(64));
  CLIPD2D_CHECK_EQ(second.size(), static_cast<std::size_t>(64));
  for (const char ch : first) {
    CLIPD2D_CHECK((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'));
  }
  // 两次生成不应相同。
  CLIPD2D_CHECK(first != second);
}
