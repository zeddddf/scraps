#pragma once
#include "MonitorTopology.h"

class CoordinateMapper {
public:
    explicit CoordinateMapper(const MonitorTopology& topology);

    // Coordinate space transformations
    PointPx ScreenToVirtualPx(const PointPx& screenPt) const;
    PointPx VirtualPxToMonitorLocalPx(const PointPx& virtualPt, const MonitorSnapshot& monitor) const;
    
    // Normalization
    RectPx NormalizeSelection(const PointPx& p1, const PointPx& p2) const;
    
    // Split selection across monitors (e.g. for multi-monitor overlay/capture)
    std::vector<MonitorRoi> SplitSelectionByMonitor(const RectPx& virtualSelection) const;

    // DPI Utilities
    float GetDpiScaleX(const MonitorSnapshot& monitor) const;
    float GetDpiScaleY(const MonitorSnapshot& monitor) const;

private:
    const MonitorTopology& topology_;
};
