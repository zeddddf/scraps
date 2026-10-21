#pragma once
#include <d3d11.h>
#include <d2d1_1.h>
#include <dcomp.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

class SharedDeviceResources {
public:
    static SharedDeviceResources& Instance() {
        static SharedDeviceResources instance;
        return instance;
    }

    bool Initialize() {
        if (initialized_) return true;

        UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        D3D_FEATURE_LEVEL featureLevels[] = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0
        };

        HRESULT hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, creationFlags,
            featureLevels, ARRAYSIZE(featureLevels), D3D11_SDK_VERSION,
            &d3dDevice_, nullptr, &d3dContext_
        );

        if (hr == DXGI_ERROR_UNSUPPORTED) {
            hr = D3D11CreateDevice(
                nullptr, D3D_DRIVER_TYPE_WARP, nullptr, creationFlags,
                featureLevels, ARRAYSIZE(featureLevels), D3D11_SDK_VERSION,
                &d3dDevice_, nullptr, &d3dContext_
            );
        }
        if (FAILED(hr)) return false;

        if (FAILED(d3dDevice_.As(&dxgiDevice_))) return false;

        Microsoft::WRL::ComPtr<IDXGIDevice1> dxgiDevice1;
        if (SUCCEEDED(dxgiDevice_.As(&dxgiDevice1))) {
            dxgiDevice1->SetMaximumFrameLatency(1);
        }

        Microsoft::WRL::ComPtr<IDXGIAdapter> adapter;
        dxgiDevice_->GetAdapter(&adapter);
        adapter->GetParent(IID_PPV_ARGS(&dxgiFactory_));

        hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, IID_PPV_ARGS(&d2dFactory_));
        if (FAILED(hr)) return false;

        hr = d2dFactory_->CreateDevice(dxgiDevice_.Get(), &d2dDevice_);
        if (FAILED(hr)) return false;

        hr = DCompositionCreateDevice(dxgiDevice_.Get(), IID_PPV_ARGS(&dcompDevice_));
        if (FAILED(hr)) return false;

        initialized_ = true;
        return true;
    }

    void Reset() {
        dcompDevice_.Reset();
        d2dDevice_.Reset();
        d2dFactory_.Reset();
        dxgiFactory_.Reset();
        dxgiDevice_.Reset();
        d3dContext_.Reset();
        d3dDevice_.Reset();
        initialized_ = false;
    }

    ID3D11Device* GetD3D11Device() const { return d3dDevice_.Get(); }
    ID3D11DeviceContext* GetD3D11DeviceContext() const { return d3dContext_.Get(); }
    IDXGIFactory2* GetDxgiFactory() const { return dxgiFactory_.Get(); }
    ID2D1Factory1* GetD2DFactory() const { return d2dFactory_.Get(); }
    ID2D1Device* GetD2DDevice() const { return d2dDevice_.Get(); }
    IDCompositionDevice* GetDCompDevice() const { return dcompDevice_.Get(); }

private:
    SharedDeviceResources() = default;
    ~SharedDeviceResources() = default;

    Microsoft::WRL::ComPtr<ID3D11Device> d3dDevice_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3dContext_;
    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice_;
    Microsoft::WRL::ComPtr<IDXGIFactory2> dxgiFactory_;

    Microsoft::WRL::ComPtr<ID2D1Factory1> d2dFactory_;
    Microsoft::WRL::ComPtr<ID2D1Device> d2dDevice_;

    Microsoft::WRL::ComPtr<IDCompositionDevice> dcompDevice_;

    bool initialized_ = false;
};
