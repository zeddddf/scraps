#pragma once

#include <windows.h>
#include <shellapi.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "common/Status.h"
#include "core/clipboard/ClipboardSession.h"
#include "core/policies/ClipboardRetryPolicy.h"

// 能力边界：**只负责「从已打开的剪贴板把字节读出来」**。
// 不解析内容语义、不判断格式是否支持、不写盘。判断"能不能用"属于采集编排。
// ClipboardScope（打开/关闭守卫）统一由 ClipboardSession.h 提供。
namespace clipd2d::core::clipboard {

// CF_UNICODETEXT / CF_TEXT 的大小受限读取结果（暴露给测试，便于验证对
// 非 NUL 结尾的恶意负载的截断行为）。
struct ReadGlobalTextResult {
  common::Status status = common::Status::Ok();
  std::string text;
};

// 两个读取器都不会越过 GlobalSize(handle)，也不要求源内存块以 NUL 结尾。
ReadGlobalTextResult ReadGlobalText(HGLOBAL handle);
ReadGlobalTextResult ReadAnsiGlobal(HGLOBAL handle);

// 读 HGLOBAL 的原始字节；handle 为空或长度为 0 时返回空 vector。
std::vector<std::uint8_t> ReadGlobalBytes(HGLOBAL handle);

// 读 CF_HDROP 的文件列表。
std::vector<std::wstring> ReadFiles(HDROP drop);

}  // namespace clipd2d::core::clipboard
