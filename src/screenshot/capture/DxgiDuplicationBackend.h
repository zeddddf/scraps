#pragma once

#include "capture/ICaptureBackend.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <vector>

class DxgiDuplicationBackend : public ICaptureBackend {
public:
    DxgiDuplicationBackend();
    ~DxgiDuplicationBackend() override;

    CaptureBackendType Type() const override { return CaptureBackendType::DxgiDuplication; }
    bool IsSupported() const override;

    CaptureResult CaptureAllMonitors(
        const std::vector<MonitorSnapshot>& monitors
    ) override;

    void Reset() override;

private:
    Microsoft::WRL::ComPtr<ID3D11Device> d3dDevice_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3dContext_;
    
    struct OutputState {
        Microsoft::WRL::ComPtr<IDXGIOutputDuplication> duplication;
        bool hasInitialFrame = false;
    };
    std::vector<OutputState> outputs_;

    HRESULT InitializeD3D();
    HRESULT InitializeDuplication();
};
