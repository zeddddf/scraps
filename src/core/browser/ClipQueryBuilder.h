#pragma once

#include "core/browser/BrowserState.h"

namespace clipd2d::core::browser {

class ClipQueryBuilder {
 public:
  static store::ClipQuery Build(const BrowserState& state);
};

}  // namespace clipd2d::core::browser
