#include "tests/TestSupport.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include "export/RoiComposer.h"

namespace clipd2d::tests {
namespace {

// 像素值 = 0xFF000000 | (x&0xFF)<<16 | (y&0xFF)<<8 | 0x7F，可用坐标反查来源。
uint32_t PackBgra(int x, int y) {
  return 0xFF000000u | (static_cast<uint32_t>(x & 0xFF) << 16) |
         (static_cast<uint32_t>(y & 0xFF) << 8) | 0x7Fu;
}

void WritePixel(std::vector<std::byte>& buf, int stride, int x, int y, uint32_t bgra) {
  std::byte* p = buf.data() + static_cast<size_t>(y) * stride + static_cast<size_t>(x) * 4;
  p[0] = static_cast<std::byte>(bgra & 0xFF);
  p[1] = static_cast<std::byte>((bgra >> 8) & 0xFF);
  p[2] = static_cast<std::byte>((bgra >> 16) & 0xFF);
  p[3] = static_cast<std::byte>((bgra >> 24) & 0xFF);
}

uint32_t ReadPixel(const std::vector<std::byte>& buf, int stride, int x, int y) {
  const std::byte* p = buf.data() + static_cast<size_t>(y) * stride + static_cast<size_t>(x) * 4;
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

MonitorFrame MakeMonitorFrame(int ox, int oy, int w, int h, int stride = -1) {
  if (stride < 0) stride = w * 4;
  MonitorFrame mf{};
  mf.monitor.virtualBounds = RectPx{ox, oy, w, h};
  mf.width = w;
  mf.height = h;
  mf.stride = stride;
  mf.bgra8.assign(static_cast<size_t>(stride) * h, std::byte{0});
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      WritePixel(mf.bgra8, stride, x, y, PackBgra(x, y));
    }
  }
  return mf;
}

}  // namespace

CLIPD2D_TEST(RoiComposerFullCoverCopiesEveryPixel) {
  DesktopFrame frame;
  frame.monitors.push_back(MakeMonitorFrame(0, 0, 4, 4));

  std::vector<std::byte> out;
  int outW = -1;
  int outH = -1;
  RoiComposer::Compose(frame, RectPx{0, 0, 4, 4}, out, outW, outH);

  CLIPD2D_CHECK_EQ(outW, 4);
  CLIPD2D_CHECK_EQ(outH, 4);
  CLIPD2D_CHECK_EQ(out.size(), static_cast<size_t>(4 * 4 * 4));
  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 4; ++x) {
      CLIPD2D_CHECK_EQ(ReadPixel(out, outW * 4, x, y), PackBgra(x, y));
    }
  }
}

CLIPD2D_TEST(RoiComposerOffsetRoiCropsCorrectWindow) {
  DesktopFrame frame;
  // 显示器原点在 (10,20)，ROI 抠出其局部 (1,1)-(3,3)。
  frame.monitors.push_back(MakeMonitorFrame(10, 20, 4, 4));

  std::vector<std::byte> out;
  int outW = 0;
  int outH = 0;
  RoiComposer::Compose(frame, RectPx{11, 21, 2, 2}, out, outW, outH);

  CLIPD2D_CHECK_EQ(outW, 2);
  CLIPD2D_CHECK_EQ(outH, 2);
  CLIPD2D_CHECK_EQ(ReadPixel(out, outW * 4, 0, 0), PackBgra(1, 1));
  CLIPD2D_CHECK_EQ(ReadPixel(out, outW * 4, 1, 0), PackBgra(2, 1));
  CLIPD2D_CHECK_EQ(ReadPixel(out, outW * 4, 0, 1), PackBgra(1, 2));
  CLIPD2D_CHECK_EQ(ReadPixel(out, outW * 4, 1, 1), PackBgra(2, 2));
}

CLIPD2D_TEST(RoiComposerPartialOverlapZeroFillsUncovered) {
  DesktopFrame frame;
  frame.monitors.push_back(MakeMonitorFrame(0, 0, 2, 2));

  std::vector<std::byte> out;
  int outW = 0;
  int outH = 0;
  // ROI (1,1)-(3,3)：只有左上角 1 像素落在显示器内。
  RoiComposer::Compose(frame, RectPx{1, 1, 2, 2}, out, outW, outH);

  CLIPD2D_CHECK_EQ(outW, 2);
  CLIPD2D_CHECK_EQ(outH, 2);
  CLIPD2D_CHECK_EQ(ReadPixel(out, outW * 4, 0, 0), PackBgra(1, 1));
  CLIPD2D_CHECK_EQ(ReadPixel(out, outW * 4, 1, 0), 0u);
  CLIPD2D_CHECK_EQ(ReadPixel(out, outW * 4, 0, 1), 0u);
  CLIPD2D_CHECK_EQ(ReadPixel(out, outW * 4, 1, 1), 0u);
}

CLIPD2D_TEST(RoiComposerSkipsEmptyMonitorBuffer) {
  DesktopFrame frame;
  MonitorFrame mf = MakeMonitorFrame(0, 0, 4, 4);
  mf.bgra8.clear();  // 有几何信息但没有像素数据
  frame.monitors.push_back(mf);

  std::vector<std::byte> out;
  int outW = 0;
  int outH = 0;
  RoiComposer::Compose(frame, RectPx{0, 0, 4, 4}, out, outW, outH);

  CLIPD2D_CHECK_EQ(outW, 4);
  CLIPD2D_CHECK_EQ(outH, 4);
  for (size_t i = 0; i < out.size(); ++i) {
    CLIPD2D_CHECK(out[i] == std::byte{0});
  }
}

CLIPD2D_TEST(RoiComposerNonPositiveRoiClearsOutput) {
  DesktopFrame frame;
  frame.monitors.push_back(MakeMonitorFrame(0, 0, 4, 4));

  std::vector<std::byte> out{std::byte{0xAA}};
  int outW = -1;
  int outH = -1;
  RoiComposer::Compose(frame, RectPx{0, 0, 0, 4}, out, outW, outH);

  CLIPD2D_CHECK_EQ(outW, 0);
  CLIPD2D_CHECK_EQ(outH, 4);
  CLIPD2D_CHECK(out.empty());
}

CLIPD2D_TEST(RoiComposerStitchesAdjacentMonitors) {
  DesktopFrame frame;
  // 两块 2x2 显示器横向拼接：(0,0) 与 (2,0)。
  frame.monitors.push_back(MakeMonitorFrame(0, 0, 2, 2));
  frame.monitors.push_back(MakeMonitorFrame(2, 0, 2, 2));

  std::vector<std::byte> out;
  int outW = 0;
  int outH = 0;
  RoiComposer::Compose(frame, RectPx{0, 0, 4, 2}, out, outW, outH);

  CLIPD2D_CHECK_EQ(outW, 4);
  CLIPD2D_CHECK_EQ(outH, 2);
  CLIPD2D_CHECK_EQ(ReadPixel(out, outW * 4, 0, 0), PackBgra(0, 0));
  CLIPD2D_CHECK_EQ(ReadPixel(out, outW * 4, 1, 1), PackBgra(1, 1));
  // 第二块显示器的像素在输出里带自身局部坐标。
  CLIPD2D_CHECK_EQ(ReadPixel(out, outW * 4, 2, 0), PackBgra(0, 0));
  CLIPD2D_CHECK_EQ(ReadPixel(out, outW * 4, 3, 1), PackBgra(1, 1));
}

CLIPD2D_TEST(RoiComposerHonorsSourceStridePadding) {
  DesktopFrame frame;
  // stride 比 width*4 多 8 字节填充，验证拷贝不会把填充算进像素。
  frame.monitors.push_back(MakeMonitorFrame(0, 0, 2, 2, 2 * 4 + 8));

  std::vector<std::byte> out;
  int outW = 0;
  int outH = 0;
  RoiComposer::Compose(frame, RectPx{0, 0, 2, 2}, out, outW, outH);

  CLIPD2D_CHECK_EQ(outW, 2);
  CLIPD2D_CHECK_EQ(outH, 2);
  CLIPD2D_CHECK_EQ(ReadPixel(out, outW * 4, 0, 0), PackBgra(0, 0));
  CLIPD2D_CHECK_EQ(ReadPixel(out, outW * 4, 1, 0), PackBgra(1, 0));
  CLIPD2D_CHECK_EQ(ReadPixel(out, outW * 4, 0, 1), PackBgra(0, 1));
  CLIPD2D_CHECK_EQ(ReadPixel(out, outW * 4, 1, 1), PackBgra(1, 1));
}

}  // namespace clipd2d::tests
