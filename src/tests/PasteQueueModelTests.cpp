#include "tests/TestSupport.h"

#include "core/commands/PasteQueueModel.h"

// 业务核抽取计划 §4.3：粘贴队列纪律矩阵。
// 入队闸门 / FIFO / Close 排空 / Clear / kind→执行面标签映射。

namespace clipd2d::tests {
namespace {

using clipd2d::core::commands::PasteDispatchTarget;
using clipd2d::core::commands::PasteQueueModel;
using clipd2d::core::commands::DispatchFor;
using clipd2d::core::commands::QueuedPasteKind;
using clipd2d::core::commands::QueuedPasteRequest;

QueuedPasteRequest MakeRequest(std::int64_t clip_id, QueuedPasteKind kind = QueuedPasteKind::Default) {
  QueuedPasteRequest request;
  request.clip_id = clip_id;
  request.kind = kind;
  return request;
}

}  // namespace

// ── 入队闸门 ───────────────────────────────────────────────────────

CLIPD2D_TEST(PasteQueueRejectsNonPositiveClipId) {
  PasteQueueModel queue;
  CLIPD2D_CHECK(!queue.Enqueue(MakeRequest(0)));
  CLIPD2D_CHECK(!queue.Enqueue(MakeRequest(-1)));
  CLIPD2D_CHECK(!queue.HasPending());
  CLIPD2D_CHECK_EQ(queue.size(), 0u);

  CLIPD2D_CHECK(queue.Enqueue(MakeRequest(1)));
  CLIPD2D_CHECK_EQ(queue.size(), 1u);
}

CLIPD2D_TEST(PasteQueueRejectsEnqueueAfterClose) {
  PasteQueueModel queue;
  CLIPD2D_CHECK(queue.Enqueue(MakeRequest(1)));
  queue.Close();
  CLIPD2D_CHECK(!queue.Enqueue(MakeRequest(2)));
  CLIPD2D_CHECK_EQ(queue.size(), 1u);  // 已入队的不受影响
  CLIPD2D_CHECK(queue.closed());
}

// ── FIFO ───────────────────────────────────────────────────────────

CLIPD2D_TEST(PasteQueuePopsInFifoOrder) {
  PasteQueueModel queue;
  CLIPD2D_CHECK(queue.Enqueue(MakeRequest(10)));
  CLIPD2D_CHECK(queue.Enqueue(MakeRequest(20)));
  CLIPD2D_CHECK(queue.Enqueue(MakeRequest(30)));

  QueuedPasteRequest out;
  CLIPD2D_CHECK(queue.Pop(&out));
  CLIPD2D_CHECK_EQ(out.clip_id, 10);
  CLIPD2D_CHECK(queue.Pop(&out));
  CLIPD2D_CHECK_EQ(out.clip_id, 20);
  CLIPD2D_CHECK(queue.Pop(&out));
  CLIPD2D_CHECK_EQ(out.clip_id, 30);
  CLIPD2D_CHECK(!queue.Pop(&out));  // 空队列
  CLIPD2D_CHECK(!queue.HasPending());
}

// ── Close：拒新、已有仍可排空 ─────────────────────────────────────

CLIPD2D_TEST(PasteQueueCloseAllowsDrainingRemaining) {
  PasteQueueModel queue;
  CLIPD2D_CHECK(queue.Enqueue(MakeRequest(1)));
  CLIPD2D_CHECK(queue.Enqueue(MakeRequest(2)));
  queue.Close();

  // Close 后已有请求仍可排空（worker 退出前的吞吐语义）。
  QueuedPasteRequest out;
  CLIPD2D_CHECK(queue.Pop(&out));
  CLIPD2D_CHECK_EQ(out.clip_id, 1);
  CLIPD2D_CHECK(queue.Pop(&out));
  CLIPD2D_CHECK_EQ(out.clip_id, 2);
  CLIPD2D_CHECK(!queue.Pop(&out));  // 排空至空
  CLIPD2D_CHECK(!queue.HasPending());
}

// ── Clear / Reset ──────────────────────────────────────────────────

CLIPD2D_TEST(PasteQueueClearDropsAllPending) {
  PasteQueueModel queue;
  CLIPD2D_CHECK(queue.Enqueue(MakeRequest(1)));
  CLIPD2D_CHECK(queue.Enqueue(MakeRequest(2)));
  queue.Clear();
  CLIPD2D_CHECK(!queue.HasPending());
  CLIPD2D_CHECK(!queue.closed());  // Clear 只清队列，不动闸门
  CLIPD2D_CHECK(queue.Enqueue(MakeRequest(3)));  // 闸门未关仍可入队
  CLIPD2D_CHECK_EQ(queue.size(), 1u);
}

CLIPD2D_TEST(PasteQueueResetReopensGate) {
  PasteQueueModel queue;
  CLIPD2D_CHECK(queue.Enqueue(MakeRequest(1)));
  queue.Close();
  queue.Clear();
  queue.Reset();
  CLIPD2D_CHECK(!queue.closed());
  CLIPD2D_CHECK(!queue.HasPending());
  CLIPD2D_CHECK(queue.Enqueue(MakeRequest(2)));  // 再开闸后可入队
}

// ── kind → 执行面标签映射（三态）──────────────────────────────────

CLIPD2D_TEST(PasteQueueDispatchMapsKindToTarget) {
  CLIPD2D_CHECK_EQ(DispatchFor(QueuedPasteKind::Default), PasteDispatchTarget::ById);
  CLIPD2D_CHECK_EQ(DispatchFor(QueuedPasteKind::PlainText), PasteDispatchTarget::PlainText);
  CLIPD2D_CHECK_EQ(DispatchFor(QueuedPasteKind::Action), PasteDispatchTarget::Action);
}

}  // namespace clipd2d::tests
