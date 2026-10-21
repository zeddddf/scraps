#pragma once

#include <functional>

#include "common/Status.h"
#include "core/commands/AppController.h"
#include "core/commands/Command.h"

namespace clipd2d::core::commands {

class CommandService {
 public:
  using ShowWindowDispatcher = std::function<void()>;

  void SetController(AppController* controller);
  void SetShowWindowDispatcher(ShowWindowDispatcher dispatcher);

  CommandResponse Handle(const CommandRequest& request) const;

 private:
  static CommandResponse FromStatus(common::Status status, std::string success_message, std::string data = {});

  AppController* controller_ = nullptr;
  ShowWindowDispatcher show_window_dispatcher_;
};

}  // namespace clipd2d::core::commands
