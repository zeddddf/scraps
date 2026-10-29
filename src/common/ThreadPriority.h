#pragma once

// P1-3 高负载设防（docs/plans/ui-fluidity-roadmap.md）：
// 所有后台工作线程统一降为 BELOW_NORMAL。核心动机——系统高负载（别的程序把
// CPU 打满）时，调度器按优先级给 UI 线程留出出帧时间，worker 队列慢一点只是
// 页面/图标晚到几帧，绝不能反过来挤掉消息循环。

#include <windows.h>

namespace clipd2d::common {

// 在目标线程内部调用（作用于当前线程）。幂等，失败静默——降级失败不致命。
inline void SetCurrentThreadBackgroundPriority() {
  ::SetThreadPriority(::GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
}

}  // namespace clipd2d::common
