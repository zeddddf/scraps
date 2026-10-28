#pragma once

// ISettingsStore —— 设置持久化面。
// 覆盖「读写应用设置」这一用例：加载、保存、读取当前生效快照。
// 变更本接口 = 改"UI 能碰哪些设置"。

#include "common/Status.h"
#include "core/store/ClipTypes.h"

namespace clipd2d::core::commands {

class ISettingsStore {
 public:
  virtual ~ISettingsStore() = default;

  virtual common::Status LoadSettings(store::AppSettings* settings) = 0;
  virtual common::Status SaveSettings(const store::AppSettings& settings) = 0;
  virtual store::AppSettings current_settings() const = 0;
};

}  // namespace clipd2d::core::commands
