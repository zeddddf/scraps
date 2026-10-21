#pragma once
#include "capture/ICaptureBackend.h"

class GdiCaptureBackend : public ICaptureBackend {
public:
    GdiCaptureBackend();
    ~GdiCaptureBackend() override;

    bool IsSupported() const override;
    CaptureBackendType Type() const override { return CaptureBackendType::GdiBitBlt; }
    CaptureResult CaptureAllMonitors(const std::vector<MonitorSnapshot>& monitors) override;
    void Reset() override;
};
