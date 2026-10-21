#pragma once

#include <memory>
#include "capture/ICaptureBackend.h"

class CaptureSupervisor {
public:
    CaptureSupervisor();
    ~CaptureSupervisor();

    CaptureResult CaptureOnce();

private:
    CaptureResult TryDxgi();
    CaptureResult TryWgc();
    CaptureResult TryGdi();

    void RebuildDeviceResources();
    void RebuildMonitorTopology();

    std::unique_ptr<ICaptureBackend> dxgi_;
    std::unique_ptr<ICaptureBackend> wgc_;
    std::unique_ptr<ICaptureBackend> gdi_;
};
