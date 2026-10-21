#pragma once

#include <string>

namespace clipd2d::common {

enum class StatusCode {
  Ok = 0,
  InvalidArgument,
  NotFound,
  NotImplemented,
  AlreadyExists,
  PlatformError,
  InternalError,
  Unavailable,
};

struct Status {
  StatusCode code = StatusCode::Ok;
  std::string message;

  [[nodiscard]] bool ok() const {
    return code == StatusCode::Ok;
  }

  static Status Ok() {
    return {};
  }

  static Status Error(StatusCode code_value, std::string message_value) {
    return Status{code_value, std::move(message_value)};
  }
};

}  // namespace clipd2d::common
