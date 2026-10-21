#include "CoordinateMapper.h"
#include <algorithm>
#include <cmath>

#include <cmath>

CoordinateMapper::CoordinateMapper(const MonitorTopology& topology)
    : topology_(topology) {
}

PointPx CoordinateMapper::ScreenToVirtualPx(const PointPx& screenPt) const {
    // In a Per-Monitor V2 DPI aware process, GetCursorPos and screen coordinates 
    // are already in Virtual Desktop Physical Pixel Space.
    return screenPt;
}

PointPx CoordinateMapper::VirtualPxToMonitorLocalPx(const PointPx& virtualPt, const MonitorSnapshot& monitor) const {
    return {
        virtualPt.x - monitor.virtualBounds.x,
        virtualPt.y - monitor.virtualBounds.y
    };
}

RectPx CoordinateMapper::NormalizeSelection(const PointPx& p1, const PointPx& p2) const {
    int x = std::min(p1.x, p2.x);
    int y = std::min(p1.y, p2.y);
    int width = std::abs(p2.x - p1.x);
    int height = std::abs(p2.y - p1.y);
    return { x, y, width, height };
}

std::vector<MonitorRoi> CoordinateMapper::SplitSelectionByMonitor(const RectPx& virtualSelection) const {
    std::vector<MonitorRoi> result;
    for (const auto& monitor : topology_.GetMonitors()) {
        if (monitor.virtualBounds.intersects(virtualSelection)) {
            RectPx intersect = monitor.virtualBounds.intersect(virtualSelection);
            
            MonitorRoi roi;
            roi.hMonitor = monitor.hMonitor;
            roi.virtualRect = intersect;
            
            roi.localRect = {
                intersect.x - monitor.virtualBounds.x,
                intersect.y - monitor.virtualBounds.y,
                intersect.width,
                intersect.height
            };
            
            result.push_back(roi);
        }
    }
    return result;
}

float CoordinateMapper::GetDpiScaleX(const MonitorSnapshot& monitor) const {
    return monitor.dpiX / 96.0f;
}

float CoordinateMapper::GetDpiScaleY(const MonitorSnapshot& monitor) const {
    return monitor.dpiY / 96.0f;
}
