#pragma once

#include <d3d11.h>
#include <d2d1_1.h>
#include <dcomp.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <windows.h>
#include <vector>

class DCompDeviceResources {
public:
    DCompDeviceResources();
    ~DCompDeviceResources();

    bool Initialize(HWND hwnd);
    ID2D1DeviceContext* GetD2DContext() const { return d2dContext_.Get(); }
    bool Resize(int width, int height);

    void BeginDraw();
    void EndDraw();
    void Commit();

    void RecreateOnDeviceLost();

    ID2D1Bitmap1* GetOrCreateBackgroundBitmap(int width, int height, int stride, const std::vector<std::byte>& bgra8);
    void ResetBackgroundBitmap() { bgBitmap_.Reset(); }

private:
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain_;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> d2dContext_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> d2dTargetBitmap_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> bgBitmap_;

    Microsoft::WRL::ComPtr<IDCompositionDevice> dcompDevice_;
    Microsoft::WRL::ComPtr<IDCompositionTarget> dcompTarget_;
    Microsoft::WRL::ComPtr<IDCompositionVisual> dcompVisual_;

    HWND hwnd_ = nullptr;
    bool CreateSwapChain();
};
