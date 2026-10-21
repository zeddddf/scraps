#include "capture/WgcCaptureBackend.h"
#include "protocol/ScreenshotProtocol.h"

#include <windows.h>
#include <dxgi1_2.h>
#include <d3d11.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <iostream>

#pragma comment(lib, "windowsapp.lib")

using namespace winrt;
using namespace winrt;

WgcCaptureBackend::WgcCaptureBackend() {}
WgcCaptureBackend::~WgcCaptureBackend() { Reset(); }

bool WgcCaptureBackend::IsSupported() const {
    return winrt::Windows::Graphics::Capture::GraphicsCaptureSession::IsSupported();
}

bool WgcCaptureBackend::InitializeD3D() {
    if (d3dDevice_) return true;

    UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };

    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, creationFlags,
        featureLevels, ARRAYSIZE(featureLevels), D3D11_SDK_VERSION,
        &d3dDevice_, nullptr, &d3dContext_
    );

    if (FAILED(hr)) {
        hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, creationFlags,
            featureLevels, ARRAYSIZE(featureLevels), D3D11_SDK_VERSION,
            &d3dDevice_, nullptr, &d3dContext_
        );
    }
    return SUCCEEDED(hr);
}

CaptureResult WgcCaptureBackend::CaptureAllMonitors(const std::vector<MonitorSnapshot>& monitors) {
    CaptureResult result;
    result.backend = CaptureBackendType::WindowsGraphicsCapture;
    result.success = false;
    result.frame.timestampQpc = 0;
    result.frame.backend = CaptureBackendType::WindowsGraphicsCapture;

    if (!IsSupported() || !InitializeD3D()) {
        result.errorCode = ScreenshotErrorCode::WgcUnavailable;
        return result;
    }

    try {
        winrt::init_apartment(apartment_type::multi_threaded);
    } catch (...) {
        // Apartment might already be initialized, which is fine
    }

    Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
    d3dDevice_.As(&dxgiDevice);

    com_ptr<::IInspectable> inspectable;
    CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.Get(), reinterpret_cast<IInspectable**>(winrt::put_abi(inspectable)));
    auto device = inspectable.as<winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice>();

    auto interopFactory = winrt::get_activation_factory<winrt::Windows::Graphics::Capture::GraphicsCaptureItem, IGraphicsCaptureItemInterop>();

    for (const auto& monitor : monitors) {
        winrt::Windows::Graphics::Capture::GraphicsCaptureItem item{nullptr};
        HRESULT hr = interopFactory->CreateForMonitor(monitor.hMonitor, winrt::guid_of<ABI::Windows::Graphics::Capture::IGraphicsCaptureItem>(), winrt::put_abi(item));
        if (FAILED(hr) || !item) continue;

        auto size = item.Size();
        auto framePool = winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool::CreateFreeThreaded(device, winrt::Windows::Graphics::DirectX::DirectXPixelFormat::B8G8R8A8UIntNormalized, 1, size);
        auto session = framePool.CreateCaptureSession(item);
        
        // Disable mouse cursor capture to avoid double cursor
        session.IsCursorCaptureEnabled(false);

        HANDLE frameArrivedEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame frameData{nullptr};
        
        auto token = framePool.FrameArrived([&](auto& pool, auto&) {
            frameData = pool.TryGetNextFrame();
            SetEvent(frameArrivedEvent);
        });

        session.StartCapture();

        // Wait up to 1000ms for a frame
        if (WaitForSingleObject(frameArrivedEvent, 1000) == WAIT_OBJECT_0 && frameData) {
            auto surface = frameData.Surface();
            auto access = surface.as<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
            
            Microsoft::WRL::ComPtr<ID3D11Texture2D> desktopTexture;
            access->GetInterface(IID_PPV_ARGS(&desktopTexture));

            if (desktopTexture) {
                D3D11_TEXTURE2D_DESC desc;
                desktopTexture->GetDesc(&desc);
                desc.Usage = D3D11_USAGE_STAGING;
                desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                desc.BindFlags = 0;
                desc.MiscFlags = 0;

                Microsoft::WRL::ComPtr<ID3D11Texture2D> stagingTexture;
                if (SUCCEEDED(d3dDevice_->CreateTexture2D(&desc, nullptr, &stagingTexture))) {
                    d3dContext_->CopyResource(stagingTexture.Get(), desktopTexture.Get());

                    D3D11_MAPPED_SUBRESOURCE mapped;
                    if (SUCCEEDED(d3dContext_->Map(stagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
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

                        // WGC sets Alpha to 255 automatically for opaque regions, but sometimes 0 for black borders.
                        // We will force Alpha to 255 just in case.
                        for (int i = 0; i < mframe.width * mframe.height; ++i) {
                            dst[i * 4 + 3] = 255;
                        }

                        d3dContext_->Unmap(stagingTexture.Get(), 0);
                        result.frame.monitors.push_back(std::move(mframe));
                    }
                }
            }
        }

        session.Close();
        framePool.FrameArrived(token);
        framePool.Close();
        CloseHandle(frameArrivedEvent);
    }

    if (!result.frame.monitors.empty()) {
        result.success = true;
        result.errorCode = ScreenshotErrorCode::None;
    } else {
        result.success = false;
        result.errorCode = ScreenshotErrorCode::UnknownError;
    }

    return result;
}

void WgcCaptureBackend::Reset() {
    d3dContext_.Reset();
    d3dDevice_.Reset();
}
