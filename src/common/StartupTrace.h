#pragma once

#include <string_view>

namespace clipd2d::common {

void ResetStartupTrace();
void MarkStartupPhase(std::string_view phase);

}  // namespace clipd2d::common
