#pragma once
#include <windows.h>
#include <vector>
#include <string>
#include <algorithm>

#include <algorithm>

struct PointPx {
    int x;
    int y;
};

struct RectPx {
    int x;
    int y;
    int width;
    int height;
    
    int right() const { return x + width; }
    int bottom() const { return y + height; }
    
    bool contains(const PointPx& pt) const {
        return pt.x >= x && pt.x < right() && pt.y >= y && pt.y < bottom();
    }
    
    bool intersects(const RectPx& other) const {
        return !(other.right() <= x || other.x >= right() || 
                 other.bottom() <= y || other.y >= bottom());
    }
    
    RectPx intersect(const RectPx& other) const {
        int nx = std::max(x, other.x);
        int ny = std::max(y, other.y);
        int nr = std::min(right(), other.right());
        int nb = std::min(bottom(), other.bottom());
        if (nx < nr && ny < nb) return {nx, ny, nr - nx, nb - ny};
        return {0, 0, 0, 0};
    }
};

struct MonitorSnapshot {
    HMONITOR hMonitor;
    LUID adapterLuid;
    RectPx virtualBounds; // Virtual Desktop Physical Pixel Space
    RectPx workArea;
    float dpiX;
    float dpiY;
    int rotation; // 0, 90, 180, 270 (matches DMDO_*)
    bool isPrimary;
    std::wstring deviceName;
};

struct MonitorRoi {
    HMONITOR hMonitor;
    RectPx localRect;  // Monitor Local Pixel Space
    RectPx virtualRect; // Virtual Desktop Physical Pixel Space
};

class MonitorTopology {
public:
    MonitorTopology();
    ~MonitorTopology();

    // Re-enumerate all monitors
    void Refresh();

    const std::vector<MonitorSnapshot>& GetMonitors() const { return monitors_; }
    const MonitorSnapshot* GetMonitorFromPoint(const PointPx& pt) const;
    const MonitorSnapshot* GetMonitorFromWindow(HWND hwnd) const;
    
    // Gets the bounding rectangle of all monitors in Virtual Desktop space
    RectPx GetVirtualDesktopBounds() const;

private:
    std::vector<MonitorSnapshot> monitors_;
};
