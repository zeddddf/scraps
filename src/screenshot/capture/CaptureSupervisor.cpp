#include "capture/CaptureSupervisor.h"
#include "capture/DxgiDuplicationBackend.h"
#include "capture/WgcCaptureBackend.h"
#include "capture/GdiCaptureBackend.h"
#include "protocol/ScreenshotProtocol.h"

CaptureSupervisor::CaptureSupervisor() {
    dxgi_ = std::make_unique<DxgiDuplicationBackend>();
}

CaptureSupervisor::~CaptureSupervisor() {}

CaptureResult CaptureSupervisor::CaptureOnce() {
    MonitorTopology topology;
    auto monitors = topology.GetMonitors();

    // 1. Try DXGI
    auto result = dxgi_->CaptureAllMonitors(monitors);
    if (result.success) {
        return result;
    }

    // 2. Try WGC
    WgcCaptureBackend wgc;
    result = wgc.CaptureAllMonitors(monitors);
    if (result.success) {
        return result;
    }

    // 3. Fallback to GDI
    GdiCaptureBackend gdiBackend;
    result = gdiBackend.CaptureAllMonitors(monitors);
    
    return result;
}

CaptureResult CaptureSupervisor::TryDxgi() {
    MonitorTopology topology;
    if (dxgi_->IsSupported()) {
        return dxgi_->CaptureAllMonitors(topology.GetMonitors());
    }
    return TryWgc();
}

CaptureResult CaptureSupervisor::TryWgc() {
    CaptureResult result;
    result.success = false;
    result.errorCode = ScreenshotErrorCode::WgcUnavailable;
    return result;
}

CaptureResult CaptureSupervisor::TryGdi() {
    CaptureResult result;
    result.success = false;
    result.errorCode = ScreenshotErrorCode::GdiFallbackFailed;
    return result;
}

void CaptureSupervisor::RebuildDeviceResources() {
    dxgi_->Reset();
}

void CaptureSupervisor::RebuildMonitorTopology() {
}
