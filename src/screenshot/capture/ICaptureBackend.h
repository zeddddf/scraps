#pragma once

#include <vector>
#include <windows.h>
#include "platform/MonitorTopology.h"
#include "capture/DesktopFrame.h"

enum class ScreenshotErrorCode : int;

struct CaptureResult {
    bool success = false;
    ScreenshotErrorCode errorCode;
    HRESULT hr = S_OK;

    CaptureBackendType backend = CaptureBackendType::DxgiDuplication;
    DesktopFrame frame;
};

class ICaptureBackend {
public:
    virtual ~ICaptureBackend() = default;

    virtual CaptureBackendType Type() const = 0;
    virtual bool IsSupported() const = 0;

    virtual CaptureResult CaptureAllMonitors(
        const std::vector<MonitorSnapshot>& monitors
    ) = 0;

    virtual void Reset() = 0;
};
