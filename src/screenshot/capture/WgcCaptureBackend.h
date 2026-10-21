#pragma once
#include "capture/ICaptureBackend.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <vector>

class WgcCaptureBackend : public ICaptureBackend {
public:
    WgcCaptureBackend();
    ~WgcCaptureBackend() override;

    bool IsSupported() const override;
    CaptureBackendType Type() const override { return CaptureBackendType::WindowsGraphicsCapture; }
    CaptureResult CaptureAllMonitors(const std::vector<MonitorSnapshot>& monitors) override;
    void Reset() override;

private:
    Microsoft::WRL::ComPtr<ID3D11Device> d3dDevice_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3dContext_;
    bool InitializeD3D();
};
