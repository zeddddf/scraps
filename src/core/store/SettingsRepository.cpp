#include "core/store/SettingsRepository.h"

#include "core/store/ClipRepository.h"

namespace clipd2d::core::store {

common::Status SettingsRepository::Load(AppSettings* settings) const {
  if (repository_ == nullptr) {
    return common::Status::Error(common::StatusCode::Unavailable, "设置仓储未绑定。");
  }
  return repository_->LoadSettings(settings);
}

common::Status SettingsRepository::Save(const AppSettings& settings) const {
  if (repository_ == nullptr) {
    return common::Status::Error(common::StatusCode::Unavailable, "设置仓储未绑定。");
  }
  return repository_->SaveSettings(settings);
}

}  // namespace clipd2d::core::store
