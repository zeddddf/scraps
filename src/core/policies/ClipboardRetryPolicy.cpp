#include "core/policies/ClipboardRetryPolicy.h"

namespace clipd2d::core::policies {

const std::array<DWORD, 8>& ClipboardOpenRetryDelaysMs() {
  static constexpr std::array<DWORD, 8> kRetryDelaysMs{0, 10, 20, 50, 100, 200, 400, 800};
  return kRetryDelaysMs;
}

}  // namespace clipd2d::core::policies
