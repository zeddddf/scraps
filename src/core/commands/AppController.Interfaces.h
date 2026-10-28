#pragma once

// AppController.Interfaces.h —— AppController 的窄接口汇总。
//
// 设计意图（docs/plans/architecture-agent-readiness.md 第 3 步）：
// AppController 是 ~113 成员的上帝类，UI 不该依赖整颗头。这里按用例切出 5 个纯虚接口，
// AppController 多重继承它们（实现不变，仍是单对象）。消费方（MainWindow 各分部）按需
// include 单个接口头即可，新增功能时可按"改哪个接口"定位，而不是读整颗头：
//
//   IHistoryFeed     读：列表/详情/标签/队列/分组/动作解析
//   IPasteService    写剪贴板回目标窗口：粘贴/变换/队列推进/OCR 文本
//   ILibraryMutator  写库结构：标签/分组/队列/收藏/置顶/移动/清空
//   ISettingsStore   设置读写
//   IMaintenance     维护与杂项：启动维护/导出/二维码/OCR
//
// 进程装配面（Initialize/Shutdown/OnClipboardChanged/SetRefreshDispatcher）
// 不进任何接口，由 app 层直接持有 AppController 使用。

#include "core/commands/IHistoryFeed.h"
#include "core/commands/ILibraryMutator.h"
#include "core/commands/IMaintenance.h"
#include "core/commands/IPasteService.h"
#include "core/commands/ISettingsStore.h"

namespace clipd2d::core::commands {

// 一次注入的窄接口集合。MainWindow 持有的 5 个窄指针由此装配；
// 在 AppController 上可用 AppController::service_refs() 取得。
struct AppServiceRefs {
  IHistoryFeed* history = nullptr;
  IPasteService* paste = nullptr;
  ILibraryMutator* library = nullptr;
  ISettingsStore* settings = nullptr;
  IMaintenance* maintenance = nullptr;
};

}  // namespace clipd2d::core::commands
