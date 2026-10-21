#pragma once

#include "core/commands/Command.h"

namespace clipd2d::ipc {

class NamedPipeClient {
 public:
  core::commands::CommandResponse Send(
      const core::commands::CommandRequest& request) const;
};

}  // namespace clipd2d::ipc
