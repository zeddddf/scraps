#pragma once

#include <string>
#include <string_view>

namespace clipd2d::common {

std::string HashBytes(std::string_view bytes);
std::string Sha256Hex(std::string_view bytes);

}  // namespace clipd2d::common
