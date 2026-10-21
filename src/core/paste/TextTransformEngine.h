#pragma once

#include <string>
#include <string_view>

#include "common/Status.h"

namespace clipd2d::core::paste {

class TextTransformEngine {
 public:
  common::Status Transform(
      std::string_view action_id,
      std::string_view input,
      std::string* output) const;
};

}  // namespace clipd2d::core::paste
