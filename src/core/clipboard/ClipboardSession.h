#pragma once

#include <windows.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "common/Status.h"
#include "core/policies/ClipboardRetryPolicy.h"

// 能力边界：**剪贴板会话与全局内存写原语**。
// - 唯一允许调用 OpenClipboard/CloseClipboard 的地方（RAII 守卫）；
// - 唯一封装 GlobalAlloc/GlobalLock/SetClipboardData 组合的地方。
// 不决定「写什么格式、什么内容」——那是各写回模块的职责。
namespace clipd2d::core::clipboard {

// 测试探测点：每成功打开一次剪贴板会话回调一次。
// 用于断言一次图片写回只含一个剪贴板会话（Bug A 回归锁）。传 nullptr 关闭。
using ClipboardSessionProbe = void (*)();
void SetClipboardSessionProbe(ClipboardSessionProbe probe);

// 剪贴板打开/关闭的 RAII 守卫（读/写共用唯一实现）。
// 打开失败会按 ClipboardOpenRetryDelaysMs 退避重试。
// INVARIANT: 只在这里 OpenClipboard/CloseClipboard；其它模块不得直接调用这两个 API。
class ClipboardScope {
 public:
  explicit ClipboardScope(HWND owner = nullptr) {
    for (const DWORD delay_ms : policies::ClipboardOpenRetryDelaysMs()) {
      if (delay_ms > 0) {
        Sleep(delay_ms);
      }
      if (OpenClipboard(owner) == TRUE) {
        opened_ = true;
        if (session_probe() != nullptr) {
          session_probe()();
        }
        break;
      }
    }
  }
  ~ClipboardScope() {
    if (opened_) {
      CloseClipboard();
    }
  }

  ClipboardScope(const ClipboardScope&) = delete;
  ClipboardScope& operator=(const ClipboardScope&) = delete;

  bool opened() const { return opened_; }

 private:
  static ClipboardSessionProbe& session_probe();
  bool opened_ = false;
};

// —— 写侧全局内存原语（全部要求调用方已持有打开的剪贴板会话）——

common::Status SetGlobalTextData(UINT format, const std::wstring& text);
common::Status SetGlobalAnsiData(UINT format, const std::string& text);
common::Status SetGlobalBinaryData(UINT format, const std::vector<std::uint8_t>& bytes);

// 写 CF_HDROP 文件列表（joined_paths 以 \n 分隔），并附 PreferredDropEffect=Copy。
common::Status SetClipboardFilesData(const std::wstring& joined_paths);

}  // namespace clipd2d::core::clipboard
