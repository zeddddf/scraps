#include "common/ThreadPool.h"

#include <algorithm>
#include <exception>
#include <string>

#include "common/Log.h"

namespace clipd2d::common {

ThreadPool::ThreadPool(std::size_t thread_count) {
  if (thread_count == 0) {
    thread_count = std::max<std::size_t>(1, std::thread::hardware_concurrency());
  }

  workers_.reserve(thread_count);
  for (std::size_t i = 0; i < thread_count; ++i) {
    workers_.emplace_back([this]() { WorkerLoop(); });
  }
}

ThreadPool::~ThreadPool() {
  Shutdown();
}

void ThreadPool::Post(std::function<void()> task) {
  if (!task) return;
  {
    std::scoped_lock lock(mutex_);
    if (stopping_) return;
    queue_.push_back(std::move(task));
  }
  cv_.notify_one();
}

void ThreadPool::Shutdown() {
  {
    std::scoped_lock lock(mutex_);
    if (stopping_) return;
    stopping_ = true;
  }
  cv_.notify_all();

  for (auto& worker : workers_) {
    if (worker.joinable()) {
      worker.join();
    }
  }
  workers_.clear();
}

void ThreadPool::WorkerLoop() {
  for (;;) {
    std::function<void()> task;
    {
      std::unique_lock lock(mutex_);
      cv_.wait(lock, [this]() { return stopping_ || !queue_.empty(); });
      if (stopping_ && queue_.empty()) {
        break;
      }
      if (!queue_.empty()) {
        task = std::move(queue_.front());
        queue_.pop_front();
      }
    }
    if (task) {
      // U3：任务异常不得穿透线程入口（std::terminate 会杀掉整个进程）。
      try {
        task();
      } catch (const std::exception& e) {
        common::WriteLog(
            common::LogLevel::Error,
            "common.threadpool",
            std::string("工作线程任务异常: ") + e.what());
      } catch (...) {
        common::WriteLog(
            common::LogLevel::Error,
            "common.threadpool",
            "工作线程任务未知异常。");
      }
    }
  }
}

}  // namespace clipd2d::common
