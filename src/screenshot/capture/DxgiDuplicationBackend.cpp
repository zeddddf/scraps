#include "capture/DxgiDuplicationBackend.h"
#include "protocol/ScreenshotProtocol.h"
#include "overlay/SharedDeviceResources.h"
#include <iostream>

DxgiDuplicationBackend::DxgiDuplicationBackend() {}

DxgiDuplicationBackend::~DxgiDuplicationBackend() {
    Reset();
}

bool DxgiDuplicationBackend::IsSupported() const {
    return true; // Supported on Win8+
}

HRESULT DxgiDuplicationBackend::InitializeD3D() {
    if (d3dDevice_) return S_OK;

    auto& shared = SharedDeviceResources::Instance();
    if (!shared.Initialize()) {
        return E_FAIL;
    }

    d3dDevice_ = shared.GetD3D11Device();
    d3dContext_ = shared.GetD3D11DeviceContext();
    return S_OK;
}

HRESULT DxgiDuplicationBackend::InitializeDuplication() {
    return S_OK;
}

CaptureResult DxgiDuplicationBackend::CaptureAllMonitors(const std::vector<MonitorSnapshot>& monitors) {
    CaptureResult result;
    result.backend = CaptureBackendType::DxgiDuplication;
    result.success = false;

    HRESULT hr = InitializeD3D();
    if (FAILED(hr)) {
        result.errorCode = ScreenshotErrorCode::UnknownError;
        result.hr = hr;
        return result;
    }

    result.frame.timestampQpc = 0; 
    result.frame.backend = CaptureBackendType::DxgiDuplication;

    Microsoft::WRL::ComPtr<IDXGIFactory1> dxgiFactory;
    hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&dxgiFactory);
    if (FAILED(hr)) {
        result.errorCode = ScreenshotErrorCode::UnknownError;
        result.hr = hr;
        return result;
    }

    for (const auto& monitor : monitors) {
        Microsoft::WRL::ComPtr<IDXGIOutput> targetOutput;
        
        UINT adapterIndex = 0;
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        while (dxgiFactory->EnumAdapters1(adapterIndex, &adapter) != DXGI_ERROR_NOT_FOUND) {
            DXGI_ADAPTER_DESC1 adapterDesc;
            adapter->GetDesc1(&adapterDesc);
            
            if (adapterDesc.AdapterLuid.LowPart == monitor.adapterLuid.LowPart && 
                adapterDesc.AdapterLuid.HighPart == monitor.adapterLuid.HighPart) {
                
                UINT outputIndex = 0;
                Microsoft::WRL::ComPtr<IDXGIOutput> output;
                while (adapter->EnumOutputs(outputIndex, &output) != DXGI_ERROR_NOT_FOUND) {
                    DXGI_OUTPUT_DESC outputDesc;
                    output->GetDesc(&outputDesc);
                    
                    if (monitor.deviceName == outputDesc.DeviceName) {
                        targetOutput = output;
                        break;
                    }
                    outputIndex++;
                }
            }
            if (targetOutput) break;
            adapterIndex++;
        }
        
        if (!targetOutput) {
            continue;
        }
        
        Microsoft::WRL::ComPtr<IDXGIOutput1> output1;
        hr = targetOutput.As(&output1);
        if (FAILED(hr)) continue;

        Microsoft::WRL::ComPtr<IDXGIOutputDuplication> duplication;
        hr = output1->DuplicateOutput(d3dDevice_.Get(), &duplication);
        if (FAILED(hr)) {
            if (hr == DXGI_ERROR_ACCESS_LOST) {
                result.success = false;
                result.errorCode = ScreenshotErrorCode::AccessLost;
                result.hr = hr;
                return result;
            }
            continue;
        }

        DXGI_OUTDUPL_FRAME_INFO frameInfo = {};
        Microsoft::WRL::ComPtr<IDXGIResource> desktopResource;
        
        int retries = 5;
        while (retries-- > 0) {
            hr = duplication->AcquireNextFrame(200, &frameInfo, &desktopResource);
            if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
                continue;
            }
            break;
        }

        if (FAILED(hr)) {
            if (hr == DXGI_ERROR_ACCESS_LOST) {
                result.success = false;
                result.errorCode = ScreenshotErrorCode::AccessLost;
                result.hr = hr;
                return result;
            }
            continue;
        }

        Microsoft::WRL::ComPtr<ID3D11Texture2D> desktopTexture;
        hr = desktopResource.As(&desktopTexture);
        if (SUCCEEDED(hr)) {
            D3D11_TEXTURE2D_DESC desc;
            desktopTexture->GetDesc(&desc);

            desc.Usage = D3D11_USAGE_STAGING;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            desc.BindFlags = 0;
            desc.MiscFlags = 0;

            Microsoft::WRL::ComPtr<ID3D11Texture2D> stagingTexture;
            hr = d3dDevice_->CreateTexture2D(&desc, nullptr, &stagingTexture);
            if (SUCCEEDED(hr)) {
                d3dContext_->CopyResource(stagingTexture.Get(), desktopTexture.Get());

                D3D11_MAPPED_SUBRESOURCE mapped;
                hr = d3dContext_->Map(stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped);
                if (SUCCEEDED(hr)) {
                    MonitorFrame mframe;
                    mframe.monitor = monitor;
                    mframe.width = desc.Width;
                    mframe.height = desc.Height;
                    mframe.stride = desc.Width * 4;

                    mframe.bgra8.resize(mframe.height * mframe.stride);

                    const uint8_t* src = static_cast<const uint8_t*>(mapped.pData);
                    uint8_t* dst = reinterpret_cast<uint8_t*>(mframe.bgra8.data());

                    for (int y = 0; y < mframe.height; ++y) {
                        memcpy(dst + y * mframe.stride, src + y * mapped.RowPitch, mframe.stride);
                    }

                    d3dContext_->Unmap(stagingTexture.Get(), 0);
                    result.frame.monitors.push_back(std::move(mframe));
                }
            }
        }
        
        duplication->ReleaseFrame();
    }
    
    if (!result.frame.monitors.empty()) {
        // Verify it's not a completely black frame (DXGI Hybrid GPU bug)
        bool isAllBlack = true;
        for (const auto& mf : result.frame.monitors) {
            const uint64_t* data64 = reinterpret_cast<const uint64_t*>(mf.bgra8.data());
            size_t count = mf.bgra8.size() / 8;
            for (size_t i = 0; i < count; ++i) {
                if (data64[i] != 0) {
                    isAllBlack = false;
                    break;
                }
            }
            if (!isAllBlack) break;
        }

        if (isAllBlack) {
            result.success = false;
            result.errorCode = ScreenshotErrorCode::UnknownError; // Fallback indicator
            result.frame.monitors.clear();
        } else {
            result.success = true;
            result.errorCode = ScreenshotErrorCode::None;
            result.hr = S_OK;
        }
    } else {
        result.success = false;
        result.errorCode = ScreenshotErrorCode::UnknownError;
    }

    return result;
}

void DxgiDuplicationBackend::Reset() {
    outputs_.clear();
    d3dContext_.Reset();
    d3dDevice_.Reset();
}
