#pragma once

#include <windows.h>
#include <d2d1_1.h>
#include <wrl/client.h>
#include <vector>
#include <memory>
#include "overlay/DCompDeviceResources.h"

class PinWindow {
public:
    PinWindow(HWND parent, const std::vector<std::byte>& bgraData, int width, int height, int x, int y);
    ~PinWindow();

    bool Show();
    HWND GetHwnd() const { return hwnd_; }

    static bool RegisterClass(HINSTANCE hInstance);

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void CreateD2DResources();
    void Paint();
    void HandleZoom(short delta, POINT mouseScreenPt);
    void HandleOpacity(short delta);
    void SaveToFile();
    void CopyToClipboard();

    HWND hwnd_ = nullptr;
    std::vector<std::byte> rawBgra_;
    int rawWidth_ = 0;
    int rawHeight_ = 0;

    float scale_ = 1.0f;
    BYTE alpha_ = 255; // 0-255

    // DComp Resources
    std::unique_ptr<DCompDeviceResources> dcomp_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> d2dBitmap_;

    // Animation
    ULONGLONG animStartTime_ = 0;
    bool isAnimatingShadow_ = false;

    // Focus state
    bool isFocused_ = true;
};
