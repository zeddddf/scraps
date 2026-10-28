#pragma once

// 业务核抽取计划 §4：粘贴队列纪律（纯逻辑）。
// 入队闸门（clip_id>0 / 关闭拒新）/ FIFO / 排空 / 清空 / kind→执行面标签映射
// 都在这里；线程等待唤醒、剪贴板事务与失败回抛留在 AppController。
//
// 无锁：调用方持 mutex_ 访问（与现状 pending_paste_requests_ 的锁语义一致）。

#include <cstddef>
#include <deque>

#include "core/commands/IPasteService.h"

namespace clipd2d::core::commands {

// kind → 执行面分发标签。分发闭包（真正调用哪个 Paste*）仍由 AppController 持有；
// 这里只固化「哪种请求走哪条执行面」的映射，防止改 kind 时静默走错分支。
enum class PasteDispatchTarget {
  ById,       // PasteById
  PlainText,  // PastePlainTextById
  Action,     // TransformPasteById
};

constexpr PasteDispatchTarget DispatchFor(QueuedPasteKind kind) {
  switch (kind) {
    case QueuedPasteKind::Default:
      return PasteDispatchTarget::ById;
    case QueuedPasteKind::PlainText:
      return PasteDispatchTarget::PlainText;
    case QueuedPasteKind::Action:
      return PasteDispatchTarget::Action;
  }
  return PasteDispatchTarget::ById;
}

class PasteQueueModel {
 public:
  // 入队闸门：clip_id<=0 拒绝；Close 之后拒绝。接受时追加到队尾。
  bool Enqueue(QueuedPasteRequest request) {
    if (request.clip_id <= 0) {
      return false;
    }
    if (closed_) {
      return false;
    }
    pending_.push_back(std::move(request));
    return true;
  }

  bool HasPending() const { return !pending_.empty(); }
  std::size_t size() const { return pending_.size(); }
  bool closed() const { return closed_; }

  // FIFO 出队；空队列返回 false。Close 之后仍可排空（吞掉剩余请求）。
  bool Pop(QueuedPasteRequest* out) {
    if (pending_.empty()) {
      return false;
    }
    if (out != nullptr) {
      *out = std::move(pending_.front());
    }
    pending_.pop_front();
    return true;
  }

  // 关闸：之后拒新；已入队的仍可 Pop 排空。
  void Close() { closed_ = true; }

  // 丢弃全部未出队请求（Shutdown 路径的清队列语义）。
  void Clear() { pending_.clear(); }

  // 重新开闸并清空（Initialize 再启动路径）。
  void Reset() {
    pending_.clear();
    closed_ = false;
  }

 private:
  std::deque<QueuedPasteRequest> pending_;
  bool closed_ = false;
};

}  // namespace clipd2d::core::commands
