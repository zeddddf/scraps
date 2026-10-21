#include "overlay/DCompDeviceResources.h"
#include "overlay/SharedDeviceResources.h"
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dcomp.lib")

DCompDeviceResources::DCompDeviceResources() {}
DCompDeviceResources::~DCompDeviceResources() {}

bool DCompDeviceResources::Initialize(HWND hwnd) {
    hwnd_ = hwnd;
    auto& shared = SharedDeviceResources::Instance();
    if (!shared.Initialize()) return false;

    dcompDevice_ = shared.GetDCompDevice();

    if (!CreateSwapChain()) return false;

    HRESULT hr = dcompDevice_->CreateTargetForHwnd(hwnd_, TRUE, &dcompTarget_);
    if (FAILED(hr)) return false;

    hr = dcompDevice_->CreateVisual(&dcompVisual_);
    if (FAILED(hr)) return false;

    hr = dcompVisual_->SetContent(swapChain_.Get());
    if (FAILED(hr)) return false;

    hr = dcompTarget_->SetRoot(dcompVisual_.Get());
    if (FAILED(hr)) return false;

    return SUCCEEDED(dcompDevice_->Commit());
}

bool DCompDeviceResources::CreateSwapChain() {
    auto& shared = SharedDeviceResources::Instance();

    RECT clientRect;
    GetClientRect(hwnd_, &clientRect);

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.Width = clientRect.right - clientRect.left;
    swapChainDesc.Height = clientRect.bottom - clientRect.top;
    swapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    swapChainDesc.Stereo = FALSE;
    swapChainDesc.SampleDesc.Count = 1;
    swapChainDesc.SampleDesc.Quality = 0;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = 2;
    swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED; 

    HRESULT hr = shared.GetDxgiFactory()->CreateSwapChainForComposition(
        shared.GetD3D11Device(),
        &swapChainDesc,
        nullptr,
        &swapChain_
    );
    if (FAILED(hr)) return false;

    hr = shared.GetD2DDevice()->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &d2dContext_);
    if (FAILED(hr)) return false;

    Microsoft::WRL::ComPtr<IDXGISurface> dxgiBackBuffer;
    hr = swapChain_->GetBuffer(0, IID_PPV_ARGS(&dxgiBackBuffer));
    if (FAILED(hr)) return false;

    D2D1_BITMAP_PROPERTIES1 bitmapProperties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
    );

    hr = d2dContext_->CreateBitmapFromDxgiSurface(dxgiBackBuffer.Get(), &bitmapProperties, &d2dTargetBitmap_);
    if (FAILED(hr)) return false;

    d2dContext_->SetTarget(d2dTargetBitmap_.Get());
    return true;
}

void DCompDeviceResources::BeginDraw() {
    if (d2dContext_) {
        d2dContext_->BeginDraw();
    }
}

void DCompDeviceResources::EndDraw() {
    if (d2dContext_) {
        d2dContext_->EndDraw();
    }
}

void DCompDeviceResources::Commit() {
    if (swapChain_) {
        swapChain_->Present(0, 0); // VSync disabled during selection drag to prevent lag
    }
}

bool DCompDeviceResources::Resize(int width, int height) {
    if (!swapChain_) return false;
    if (width <= 0 || height <= 0) return false;

    d2dContext_->SetTarget(nullptr);
    d2dTargetBitmap_.Reset();
    bgBitmap_.Reset(); // Reset background bitmap cache on resize

    HRESULT hr = swapChain_->ResizeBuffers(
        2,
        width,
        height,
        DXGI_FORMAT_B8G8R8A8_UNORM,
        0
    );
    if (FAILED(hr)) return false;

    Microsoft::WRL::ComPtr<IDXGISurface> dxgiBackBuffer;
    hr = swapChain_->GetBuffer(0, IID_PPV_ARGS(&dxgiBackBuffer));
    if (FAILED(hr)) return false;

    D2D1_BITMAP_PROPERTIES1 bitmapProperties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
    );

    hr = d2dContext_->CreateBitmapFromDxgiSurface(dxgiBackBuffer.Get(), &bitmapProperties, &d2dTargetBitmap_);
    if (FAILED(hr)) return false;

    d2dContext_->SetTarget(d2dTargetBitmap_.Get());
    return true;
}

void DCompDeviceResources::RecreateOnDeviceLost() {
    // Shared Device Resources manages device state.
    // If device is lost, we reset the shared singleton.
    SharedDeviceResources::Instance().Reset();
    bgBitmap_.Reset();
}

ID2D1Bitmap1* DCompDeviceResources::GetOrCreateBackgroundBitmap(int width, int height, int stride, const std::vector<std::byte>& bgra8) {
    if (bgBitmap_) {
        return bgBitmap_.Get();
    }
    if (bgra8.empty() || !d2dContext_) return nullptr;

    D2D1_BITMAP_PROPERTIES1 bitmapProperties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_NONE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE)
    );

    D2D1_SIZE_U size = D2D1::SizeU(width, height);
    HRESULT hr = d2dContext_->CreateBitmap(
        size,
        bgra8.data(),
        stride,
        &bitmapProperties,
        &bgBitmap_
    );
    if (FAILED(hr)) return nullptr;
    return bgBitmap_.Get();
}
