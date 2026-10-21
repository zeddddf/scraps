#pragma once

#include <memory>
#include <windows.h>

namespace clipd2d::ui {

// ---------------------------------------------------------------------------
// Q1:PostMessage 堆 payload 的所有权交接小助手。
// 约定:payload 的所有权随消息整体移交给接收方(由 TakeOwnedMessagePayload 取走后
// 自动析构);投递失败时所有权留在本助手内立即回收,不再出现"裸 new 交出去、
// 接收方漏处理即泄漏"的路径。窗口销毁前应调用 DrainOwnedMessagePayload 按消息号
// 回收队列里尚未派发的 payload(见 MainWindow::DrainPendingOwnedMessages)。
// ---------------------------------------------------------------------------

template <typename T>
bool PostOwnedMessage(HWND hwnd, UINT message, WPARAM w_param, std::unique_ptr<T> payload) {
  if (hwnd == nullptr || payload == nullptr) {
    return false;
  }
  T* raw = payload.release();
  if (!::PostMessageW(hwnd, message, w_param, reinterpret_cast<LPARAM>(raw))) {
    // 投递失败:接收方永远不会看到该消息,由本函数回收所有权。
    std::unique_ptr<T> reclaimed(raw);
    return false;
  }
  return true;
}

template <typename T>
std::unique_ptr<T> TakeOwnedMessagePayload(LPARAM l_param) {
  return std::unique_ptr<T>(reinterpret_cast<T*>(l_param));
}

}  // namespace clipd2d::ui
