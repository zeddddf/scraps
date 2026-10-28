#include "tests/TestSupport.h"

#include "platform/CoordinateMapper.h"

namespace clipd2d::tests {
namespace {

MonitorSnapshot MakeSnapshot(int ox, int oy, int w, int h, float dpiX = 96.0f, float dpiY = 96.0f) {
  MonitorSnapshot s{};
  s.virtualBounds = RectPx{ox, oy, w, h};
  s.workArea = s.virtualBounds;
  s.dpiX = dpiX;
  s.dpiY = dpiY;
  s.rotation = 0;
  s.isPrimary = (ox == 0 && oy == 0);
  s.deviceName = L"TEST";
  return s;
}

// NormalizeSelection / VirtualPxToMonitorLocalPx / GetDpiScale* 不读 topology_，
// 这里造真 topology 只为满足构造引用，断言本身是纯算术。
CoordinateMapper MakeMapperAgainstRealTopology(MonitorTopology& topology) {
  return CoordinateMapper(topology);
}

}  // namespace

CLIPD2D_TEST(CoordMapperNormalizeSelectionOrdersDragCorners) {
  MonitorTopology topology;
  CoordinateMapper mapper = MakeMapperAgainstRealTopology(topology);

  const RectPx backward = mapper.NormalizeSelection(PointPx{10, 20}, PointPx{5, 8});
  CLIPD2D_CHECK_EQ(backward.x, 5);
  CLIPD2D_CHECK_EQ(backward.y, 8);
  CLIPD2D_CHECK_EQ(backward.width, 5);
  CLIPD2D_CHECK_EQ(backward.height, 12);

  const RectPx forward = mapper.NormalizeSelection(PointPx{0, 0}, PointPx{3, 4});
  CLIPD2D_CHECK_EQ(forward.x, 0);
  CLIPD2D_CHECK_EQ(forward.y, 0);
  CLIPD2D_CHECK_EQ(forward.width, 3);
  CLIPD2D_CHECK_EQ(forward.height, 4);
}

CLIPD2D_TEST(CoordMapperVirtualToLocalUsesMonitorOrigin) {
  MonitorTopology topology;
  CoordinateMapper mapper = MakeMapperAgainstRealTopology(topology);

  const MonitorSnapshot mon = MakeSnapshot(100, 50, 1920, 1080);
  const PointPx local = mapper.VirtualPxToMonitorLocalPx(PointPx{110, 60}, mon);
  CLIPD2D_CHECK_EQ(local.x, 10);
  CLIPD2D_CHECK_EQ(local.y, 10);

  // 负向原点（主屏左侧的副屏）也要走同一减法。
  const MonitorSnapshot left = MakeSnapshot(-1920, 0, 1920, 1080);
  const PointPx localLeft = mapper.VirtualPxToMonitorLocalPx(PointPx{-10, 5}, left);
  CLIPD2D_CHECK_EQ(localLeft.x, 1910);
  CLIPD2D_CHECK_EQ(localLeft.y, 5);
}

CLIPD2D_TEST(CoordMapperScreenToVirtualIsIdentityInPerMonitorV2) {
  MonitorTopology topology;
  CoordinateMapper mapper = MakeMapperAgainstRealTopology(topology);

  // PMv2 进程下屏幕坐标即虚拟桌面物理像素，ScreenToVirtualPx 是恒等映射。
  const PointPx in{-37, 42};
  const PointPx out = mapper.ScreenToVirtualPx(in);
  CLIPD2D_CHECK_EQ(out.x, in.x);
  CLIPD2D_CHECK_EQ(out.y, in.y);
}

CLIPD2D_TEST(CoordMapperDpiScaleUses96Baseline) {
  MonitorTopology topology;
  CoordinateMapper mapper = MakeMapperAgainstRealTopology(topology);

  const MonitorSnapshot mon100 = MakeSnapshot(0, 0, 100, 100, 96.0f, 96.0f);
  CLIPD2D_CHECK_EQ(mapper.GetDpiScaleX(mon100), 1.0f);
  CLIPD2D_CHECK_EQ(mapper.GetDpiScaleY(mon100), 1.0f);

  const MonitorSnapshot mon150 = MakeSnapshot(0, 0, 100, 100, 144.0f, 120.0f);
  CLIPD2D_CHECK_EQ(mapper.GetDpiScaleX(mon150), 1.5f);
  CLIPD2D_CHECK_EQ(mapper.GetDpiScaleY(mon150), 1.25f);
}

CLIPD2D_TEST(CoordMapperSplitSingleMonitorBoundsIsExact) {
  MonitorTopology topology;
  CoordinateMapper mapper = MakeMapperAgainstRealTopology(topology);

  for (const auto& mon : topology.GetMonitors()) {
    const auto rois = mapper.SplitSelectionByMonitor(mon.virtualBounds);
    CLIPD2D_CHECK_EQ(rois.size(), static_cast<size_t>(1));
    // 整屏选区裁出的就是这块屏本身，局部坐标从 (0,0) 起。
    CLIPD2D_CHECK_EQ(rois[0].virtualRect.x, mon.virtualBounds.x);
    CLIPD2D_CHECK_EQ(rois[0].virtualRect.y, mon.virtualBounds.y);
    CLIPD2D_CHECK_EQ(rois[0].virtualRect.width, mon.virtualBounds.width);
    CLIPD2D_CHECK_EQ(rois[0].virtualRect.height, mon.virtualBounds.height);
    CLIPD2D_CHECK_EQ(rois[0].localRect.x, 0);
    CLIPD2D_CHECK_EQ(rois[0].localRect.y, 0);
    CLIPD2D_CHECK_EQ(rois[0].localRect.width, mon.virtualBounds.width);
    CLIPD2D_CHECK_EQ(rois[0].localRect.height, mon.virtualBounds.height);
    CLIPD2D_CHECK(rois[0].hMonitor == mon.hMonitor);
  }
}

CLIPD2D_TEST(CoordMapperSplitVirtualDesktopCoversEveryMonitor) {
  MonitorTopology topology;
  CoordinateMapper mapper = MakeMapperAgainstRealTopology(topology);

  const RectPx bounds = topology.GetVirtualDesktopBounds();
  const auto rois = mapper.SplitSelectionByMonitor(bounds);
  CLIPD2D_CHECK_EQ(rois.size(), topology.GetMonitors().size());

  // 每块屏的裁剪结果都落在选区内，且局部偏移 = 虁拟坐标 - 屏原点。
  for (const auto& roi : rois) {
    CLIPD2D_CHECK(roi.virtualRect.x >= bounds.x && roi.virtualRect.y >= bounds.y);
    CLIPD2D_CHECK(roi.virtualRect.right() <= bounds.right());
    CLIPD2D_CHECK(roi.virtualRect.bottom() <= bounds.bottom());
  }
}

CLIPD2D_TEST(CoordMapperSplitOutsideVirtualDesktopIsEmpty) {
  MonitorTopology topology;
  CoordinateMapper mapper = MakeMapperAgainstRealTopology(topology);

  const RectPx bounds = topology.GetVirtualDesktopBounds();
  // away：远离虚拟桌面的选区（注意不能叫 far——windows.h 里是空宏）。
  const RectPx away{bounds.right() + 1000, bounds.bottom() + 1000, 50, 50};
  CLIPD2D_CHECK(mapper.SplitSelectionByMonitor(away).empty());

  // 空选区也不该产出任何 ROI。
  CLIPD2D_CHECK(mapper.SplitSelectionByMonitor(RectPx{0, 0, 0, 0}).empty());
}

CLIPD2D_TEST(CoordMapperSplitLocalRectMatchesMonitorOffset) {
  MonitorTopology topology;
  CoordinateMapper mapper = MakeMapperAgainstRealTopology(topology);
  if (topology.GetMonitors().empty()) return;

  // 取主屏内部一小块，验证 localRect = virtualRect - 屏原点。
  const MonitorSnapshot& mon = topology.GetMonitors().front();
  const RectPx probe{mon.virtualBounds.x + 10, mon.virtualBounds.y + 20, 100, 80};
  const auto rois = mapper.SplitSelectionByMonitor(probe);
  CLIPD2D_CHECK_EQ(rois.size(), static_cast<size_t>(1));
  CLIPD2D_CHECK_EQ(rois[0].localRect.x, 10);
  CLIPD2D_CHECK_EQ(rois[0].localRect.y, 20);
  CLIPD2D_CHECK_EQ(rois[0].localRect.width, 100);
  CLIPD2D_CHECK_EQ(rois[0].localRect.height, 80);
}

}  // namespace clipd2d::tests
