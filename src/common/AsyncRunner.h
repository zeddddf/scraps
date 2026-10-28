#pragma once

#include <cstddef>
#include <functional>

#include "common/ThreadPool.h"

namespace clipd2d::common {

// 统一异步入口：强制「按值捕获 + PostMessage 回投」约定（INV-6，软约束，
// 本类无法在语言层强制——见 refactoring-plan §二）。
// Shutdown() 在析构时排空队列并 join，保证窗口析构早于线程池死亡。
class AsyncRunner {
 public:
  explicit AsyncRunner(std::size_t thread_count);
  ~AsyncRunner();

  AsyncRunner(const AsyncRunner&) = delete;
  AsyncRunner& operator=(const AsyncRunner&) = delete;

  void Post(std::function<void()> task);  // 内部转持 common::ThreadPool
  void Shutdown();                        // 幂等

 private:
  ThreadPool pool_;  // 复用既有实现，不删 ThreadPool
};

}  // namespace clipd2d::common
