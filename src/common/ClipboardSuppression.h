#pragma once

#include <cstdint>

namespace clipd2d::common {

std::int64_t GetGlobalClipboardSuppressUntil();
void SetGlobalClipboardSuppressUntil(std::int64_t epoch_ms);

}  // namespace clipd2d::common
