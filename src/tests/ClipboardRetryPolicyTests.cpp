#include "tests/TestSupport.h"

#include "core/policies/ClipboardRetryPolicy.h"

CLIPD2D_TEST(ClipboardRetryPolicyUsesCanonicalSchedule) {
  const auto& delays = clipd2d::core::policies::ClipboardOpenRetryDelaysMs();
  CLIPD2D_CHECK_EQ(delays.size(), static_cast<std::size_t>(8));
  CLIPD2D_CHECK_EQ(delays[0], static_cast<DWORD>(0));
  CLIPD2D_CHECK_EQ(delays[1], static_cast<DWORD>(10));
  CLIPD2D_CHECK_EQ(delays[2], static_cast<DWORD>(20));
  CLIPD2D_CHECK_EQ(delays[3], static_cast<DWORD>(50));
  CLIPD2D_CHECK_EQ(delays[4], static_cast<DWORD>(100));
  CLIPD2D_CHECK_EQ(delays[5], static_cast<DWORD>(200));
  CLIPD2D_CHECK_EQ(delays[6], static_cast<DWORD>(400));
  CLIPD2D_CHECK_EQ(delays[7], static_cast<DWORD>(800));
}
