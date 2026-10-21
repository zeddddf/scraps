#include "WindowEnumerationService.h"
#include <windows.h>
#include <dwmapi.h>
#include <string>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "user32.lib")

#ifndef DWMWA_CLOAKED
#define DWMWA_CLOAKED 14
#endif

namespace {

bool IsCapturableWindow(HWND hwnd, RECT& outRect) {
    // Must be visible
    if (!IsWindowVisible(hwnd)) {
        return false;
    }

    // Minimized windows shouldn't be snapped
    if (IsIconic(hwnd)) {
        return false;
    }

    LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);

    // Filter out 100% transparent layered windows
    if (exStyle & WS_EX_LAYERED) {
        COLORREF crKey;
        BYTE bAlpha;
        DWORD dwFlags;
        if (GetLayeredWindowAttributes(hwnd, &crKey, &bAlpha, &dwFlags)) {
            if ((dwFlags & LWA_ALPHA) && bAlpha == 0) {
                return false;
            }
        }
    }

    // Exclude Windows 8+ cloaked windows (like inactive UWP apps or suspended windows)
    int cloakedVal = 0;
    HRESULT hr = DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloakedVal, sizeof(cloakedVal));
    if (SUCCEEDED(hr) && cloakedVal != 0) {
        return false;
    }

    // Filter out typical system background and helper classes
    WCHAR className[256];
    if (GetClassNameW(hwnd, className, 256)) {
        std::wstring cls = className;
        if (cls == L"Progman" || cls == L"WorkerW") {
            return false;
        }
        // Exclude some common system overlay helpers
        if (cls == L"EdgeUiInputTopWnd" || cls == L"DummyDWMWindow") {
            return false;
        }
    }

    // Exclude untitled tool windows, which are usually shadows or helper overlays
    int titleLen = GetWindowTextLength(hwnd);
    if (titleLen == 0 && (exStyle & WS_EX_TOOLWINDOW)) {
        return false;
    }

    // Attempt to get the true visual bounds (excluding drop shadows, including extended frames)
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &outRect, sizeof(outRect)))) {
        if (outRect.right <= outRect.left || outRect.bottom <= outRect.top) {
            return false;
        }
    } else {
        // Fallback to standard GetWindowRect
        if (!GetWindowRect(hwnd, &outRect) || outRect.right <= outRect.left || outRect.bottom <= outRect.top) {
            return false;
        }
    }

    return true;
}

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    auto* windowRects = reinterpret_cast<std::vector<RectPx>*>(lParam);

    RECT rect;
    if (IsCapturableWindow(hwnd, rect)) {
        // RECT uses left/top/right/bottom, convert to x, y, width, height (RectPx)
        windowRects->push_back({rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top});
    }
    return TRUE;
}

} // namespace

std::vector<RectPx> WindowEnumerationService::EnumerateCapturableWindows() {
    std::vector<RectPx> windowRects;
    // EnumWindows implicitly orders from topmost to bottommost in Z-order
    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&windowRects));
    return windowRects;
}
