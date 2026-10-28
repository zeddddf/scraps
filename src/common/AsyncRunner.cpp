#include "common/AsyncRunner.h"

namespace clipd2d::common {

AsyncRunner::AsyncRunner(std::size_t thread_count) : pool_(thread_count) {}

AsyncRunner::~AsyncRunner() {
  Shutdown();
}

void AsyncRunner::Post(std::function<void()> task) {
  pool_.Post(std::move(task));
}

void AsyncRunner::Shutdown() {
  pool_.Shutdown();
}

}  // namespace clipd2d::common
