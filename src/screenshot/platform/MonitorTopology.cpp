#include "MonitorTopology.h"
#include <shellscalingapi.h>
#include <dxgi.h>
#include <wrl/client.h>
#pragma comment(lib, "Shcore.lib")
#pragma comment(lib, "dxgi.lib")

MonitorTopology::MonitorTopology() {
    Refresh();
}

MonitorTopology::~MonitorTopology() = default;

static BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData) {
    auto* monitors = reinterpret_cast<std::vector<MonitorSnapshot>*>(dwData);
    
    MONITORINFOEXW mi = {};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(hMonitor, &mi)) return TRUE;

    MonitorSnapshot snapshot = {};
    snapshot.hMonitor = hMonitor;
    snapshot.deviceName = mi.szDevice;
    snapshot.isPrimary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;

    // In Per-Monitor V2, mi.rcMonitor is in physical pixels (Virtual Desktop Physical Pixel Space)
    snapshot.virtualBounds = { mi.rcMonitor.left, mi.rcMonitor.top, 
                               mi.rcMonitor.right - mi.rcMonitor.left, 
                               mi.rcMonitor.bottom - mi.rcMonitor.top };
    
    snapshot.workArea = { mi.rcWork.left, mi.rcWork.top, 
                          mi.rcWork.right - mi.rcWork.left, 
                          mi.rcWork.bottom - mi.rcWork.top };

    // Get DPI
    UINT dpiX = 96, dpiY = 96;
    GetDpiForMonitor(hMonitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    snapshot.dpiX = static_cast<float>(dpiX);
    snapshot.dpiY = static_cast<float>(dpiY);

    // Get Rotation
    DEVMODEW devMode = {};
    devMode.dmSize = sizeof(devMode);
    if (EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &devMode)) {
        snapshot.rotation = devMode.dmDisplayOrientation; // 0, 1, 2, 3 (0, 90, 180, 270)
    } else {
        snapshot.rotation = 0;
    }

    // Attempt to get adapter LUID via DXGI
    snapshot.adapterLuid = {0, 0};
    Microsoft::WRL::ComPtr<IDXGIFactory1> dxgiFactory;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&dxgiFactory)))) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        for (UINT i = 0; dxgiFactory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
            Microsoft::WRL::ComPtr<IDXGIOutput> output;
            bool found = false;
            for (UINT j = 0; adapter->EnumOutputs(j, &output) != DXGI_ERROR_NOT_FOUND; ++j) {
                DXGI_OUTPUT_DESC desc;
                if (SUCCEEDED(output->GetDesc(&desc)) && wcscmp(desc.DeviceName, mi.szDevice) == 0) {
                    DXGI_ADAPTER_DESC1 adapterDesc;
                    if (SUCCEEDED(adapter->GetDesc1(&adapterDesc))) {
                        snapshot.adapterLuid = adapterDesc.AdapterLuid;
                        found = true;
                    }
                    break;
                }
            }
            if (found) break;
        }
    }

    monitors->push_back(snapshot);
    return TRUE;
}

void MonitorTopology::Refresh() {
    monitors_.clear();
    EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc, reinterpret_cast<LPARAM>(&monitors_));
}

const MonitorSnapshot* MonitorTopology::GetMonitorFromPoint(const PointPx& pt) const {
    for (const auto& m : monitors_) {
        if (m.virtualBounds.contains(pt)) {
            return &m;
        }
    }
    return nullptr;
}

const MonitorSnapshot* MonitorTopology::GetMonitorFromWindow(HWND hwnd) const {
    HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    for (const auto& m : monitors_) {
        if (m.hMonitor == hMon) {
            return &m;
        }
    }
    return nullptr;
}

RectPx MonitorTopology::GetVirtualDesktopBounds() const {
    if (monitors_.empty()) return {0, 0, 0, 0};
    
    int minX = monitors_[0].virtualBounds.x;
    int minY = monitors_[0].virtualBounds.y;
    int maxX = monitors_[0].virtualBounds.right();
    int maxY = monitors_[0].virtualBounds.bottom();

    for (size_t i = 1; i < monitors_.size(); ++i) {
        if (monitors_[i].virtualBounds.x < minX) minX = monitors_[i].virtualBounds.x;
        if (monitors_[i].virtualBounds.y < minY) minY = monitors_[i].virtualBounds.y;
        if (monitors_[i].virtualBounds.right() > maxX) maxX = monitors_[i].virtualBounds.right();
        if (monitors_[i].virtualBounds.bottom() > maxY) maxY = monitors_[i].virtualBounds.bottom();
    }

    return {minX, minY, maxX - minX, maxY - minY};
}
