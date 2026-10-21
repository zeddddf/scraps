#pragma once

#include <vector>

#include "core/actions/ActionDescriptor.h"
#include "core/clipboard/ContentProfile.h"

namespace clipd2d::core::actions {

class ActionResolver {
 public:
  std::vector<ActionDescriptor> Resolve(
      const clipboard::ContentProfile& profile) const;
};

}  // namespace clipd2d::core::actions
