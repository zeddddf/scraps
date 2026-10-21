#pragma once
#include <windows.h>
#include <vector>
#include <memory>
#include "platform/MonitorTopology.h"
#include "overlay/OverlayRenderer.h"
#include "overlay/DCompDeviceResources.h"

class ScreenshotSession;

class OverlayWindowManager {
public:
    OverlayWindowManager(ScreenshotSession* session);
    ~OverlayWindowManager();

    bool CreateWindows(const std::vector<MonitorSnapshot>& monitors);
    void Show();
    void Hide();
    void Destroy();
    void InvalidateAll();
    void ResetBackgroundBitmaps();

    HWND GetOwnerHwnd() const;
    OverlayRenderer& GetRenderer() { return renderer_; }
    
private:
    struct OverlayWindow {
        HWND hwnd;
        MonitorSnapshot monitor;
        std::unique_ptr<DCompDeviceResources> dcomp;
        RectPx lastSelectionRect{};
        bool lastHasSelection = false;
        std::optional<RectPx> lastHoveredRect = std::nullopt;
    };

    std::vector<OverlayWindow> windows_;
    ScreenshotSession* session_ = nullptr;
    OverlayRenderer renderer_;
    PointPx lastCursorPos_{ -9999, -9999 };

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void RegisterWindowClass();
    void RenderWindow(OverlayWindow& ow);
    PointPx GetMsgPosition(HWND hwnd, LPARAM lParam);
    void UpdateCursor(HWND hwnd);
    static bool classRegistered_;
};
