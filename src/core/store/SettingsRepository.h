#pragma once

#include "common/Status.h"
#include "core/store/ClipTypes.h"

namespace clipd2d::core::store {

class ClipRepository;

class SettingsRepository {
 public:
  explicit SettingsRepository(ClipRepository* repository) : repository_(repository) {}

  common::Status Load(AppSettings* settings) const;
  common::Status Save(const AppSettings& settings) const;

 private:
  ClipRepository* repository_ = nullptr;
};

}  // namespace clipd2d::core::store
