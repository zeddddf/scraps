#include "tests/TestSupport.h"

#include "core/policies/ImageIngestPolicy.h"

CLIPD2D_TEST(ImageIngestPolicyDetectsLargeImages) {
  const auto bytes = clipd2d::core::policies::EstimatedDecodedBytes(3000, 3000);
  CLIPD2D_CHECK(bytes > clipd2d::core::policies::LargeImageThresholdBytes());
  CLIPD2D_CHECK(clipd2d::core::policies::IsLargeImageEstimate(bytes));
}

CLIPD2D_TEST(ImageIngestPolicyPreviewPathDependsOnLargeFlag) {
  const std::wstring storage = L"C:\\temp\\image.png";
  CLIPD2D_CHECK_EQ(clipd2d::core::policies::ResolvePreviewPath(storage, false), storage);
  CLIPD2D_CHECK(clipd2d::core::policies::ResolvePreviewPath(storage, true).empty());
}
