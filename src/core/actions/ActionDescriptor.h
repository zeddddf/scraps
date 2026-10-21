#pragma once

#include <string>

namespace clipd2d::core::actions {

struct ActionDescriptor {
  std::string id;
  std::string label;
  std::wstring icon_token;
  std::string placement = "drawer";
  bool is_primary = false;
  int priority = 0;
};

}  // namespace clipd2d::core::actions
