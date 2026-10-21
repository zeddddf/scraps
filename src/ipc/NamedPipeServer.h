#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "common/Status.h"
#include "core/commands/Command.h"

namespace clipd2d::ipc {

class NamedPipeServer {
 public:
  using RequestHandler =
      std::function<core::commands::CommandResponse(const core::commands::CommandRequest&)>;

  ~NamedPipeServer();

  common::Status Start(RequestHandler handler);
  void Stop();

 private:
  void NotifyStartup(common::Status status);
  void RunWorker();

  std::atomic<bool> running_{false};
  RequestHandler handler_;
  std::thread worker_;
  std::mutex startup_mutex_;
  std::condition_variable startup_cv_;
  bool startup_signaled_ = false;
  common::Status startup_status_ = common::Status::Ok();
  // 会话令牌（S3）：Start 时生成并写入仅当前用户可读的令牌文件，
  // RunWorker 校验动作命令请求携带的 token 字段。空 = 降级（仅管道 ACL）。
  std::string expected_token_;
};

}  // namespace clipd2d::ipc
