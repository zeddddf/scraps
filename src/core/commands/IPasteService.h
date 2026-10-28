#pragma once

// IPasteService —— 粘贴执行面。
// 覆盖「把某条剪贴写回目标窗口」这一用例：同步/异步粘贴、纯文本/动作变换粘贴、队列推进、
// OCR 文本粘贴、贴纸复制。目标定位上下文由 clipboard::PasteTargetContext 承载。
// 变更本接口 = 改"UI 能发起哪些粘贴"；库结构变更见 ILibraryMutator。

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "common/Status.h"
#include "core/clipboard/ClipboardWriter.h"
#include "core/clipboard/PasteTarget.h"

namespace clipd2d::core::commands {

// 异步粘贴（UI 路径专用）请求体。曾是 AppController 的嵌套类型，提到命名空间作用域
// 以便 UI 只依赖本头而不必拉入整个 AppController。
// 契约见 AppController.h 的 QueuePaste 注释：Queue* 只入队立即返回，事务在
// paste_worker_ 线程串行执行，失败结果经 SetPasteResultDispatcher 回抛。
enum class QueuedPasteKind {
  Default,
  PlainText,
  Action,
};

struct QueuedPasteRequest {
  QueuedPasteKind kind = QueuedPasteKind::Default;
  std::int64_t clip_id = 0;
  std::string action_id;
  clipboard::PasteTargetContext target_context{};
  clipboard::PasteRoute route = clipboard::PasteRoute::Fast;
};

class IPasteService {
 public:
  virtual ~IPasteService() = default;

  virtual common::Status PasteLatest(HWND target_window = nullptr) = 0;
  virtual common::Status PasteLatest(const clipboard::PasteTargetContext& target_context, clipboard::PasteRoute route) = 0;
  virtual common::Status PasteById(std::int64_t id, HWND target_window = nullptr) = 0;
  virtual common::Status PasteById(
      std::int64_t id,
      const clipboard::PasteTargetContext& target_context,
      clipboard::PasteRoute route) = 0;

  virtual bool QueuePaste(QueuedPasteRequest request) = 0;
  virtual bool HasPendingPaste() const = 0;
  virtual void SetPasteResultDispatcher(std::function<void(const common::Status&)> dispatcher) = 0;

  virtual common::Status PastePlainTextById(std::int64_t id, HWND target_window = nullptr) = 0;
  virtual common::Status PastePlainTextById(
      std::int64_t id,
      const clipboard::PasteTargetContext& target_context,
      clipboard::PasteRoute route) = 0;
  virtual common::Status TransformPasteById(std::int64_t id, std::string_view action_id, HWND target_window = nullptr) = 0;
  virtual common::Status TransformPasteById(
      std::int64_t id,
      std::string_view action_id,
      const clipboard::PasteTargetContext& target_context,
      clipboard::PasteRoute route) = 0;

  virtual common::Status PasteNextInQueue(const clipboard::PasteTargetContext& target_context, clipboard::PasteRoute route) = 0;
  virtual common::Status CopyOcrTextById(std::int64_t id) = 0;
  virtual common::Status PasteOcrTextById(
      std::int64_t id,
      const clipboard::PasteTargetContext& target_context,
      clipboard::PasteRoute route) = 0;
  virtual common::Status CopyClipForStickerById(std::int64_t id) = 0;
};

}  // namespace clipd2d::core::commands
