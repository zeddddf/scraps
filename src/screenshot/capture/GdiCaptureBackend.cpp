#include "capture/GdiCaptureBackend.h"
#include "protocol/ScreenshotProtocol.h"
#include <windows.h>

GdiCaptureBackend::GdiCaptureBackend() {}
GdiCaptureBackend::~GdiCaptureBackend() {}

bool GdiCaptureBackend::IsSupported() const {
    return true; // GDI always supported
}

CaptureResult GdiCaptureBackend::CaptureAllMonitors(const std::vector<MonitorSnapshot>& monitors) {
    CaptureResult result;
    result.backend = CaptureBackendType::GdiBitBlt;
    result.success = false;
    result.frame.timestampQpc = 0;
    result.frame.backend = CaptureBackendType::GdiBitBlt;

    HDC hScreenDC = GetDC(nullptr);
    if (!hScreenDC) {
        result.errorCode = ScreenshotErrorCode::GdiFallbackFailed;
        return result;
    }

    HDC hMemoryDC = CreateCompatibleDC(hScreenDC);
    if (!hMemoryDC) {
        ReleaseDC(nullptr, hScreenDC);
        result.errorCode = ScreenshotErrorCode::GdiFallbackFailed;
        return result;
    }

    for (const auto& monitor : monitors) {
        int w = monitor.virtualBounds.width;
        int h = monitor.virtualBounds.height;
        int x = monitor.virtualBounds.x;
        int y = monitor.virtualBounds.y;

        BITMAPINFO bmi = {0};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = -h; // Top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* pixels = nullptr;
        HBITMAP hBitmap = CreateDIBSection(hScreenDC, &bmi, DIB_RGB_COLORS, &pixels, nullptr, 0);
        if (hBitmap) {
            HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemoryDC, hBitmap);
            
            // Capture
            BitBlt(hMemoryDC, 0, 0, w, h, hScreenDC, x, y, SRCCOPY | CAPTUREBLT);
            
            SelectObject(hMemoryDC, hOldBitmap);

            MonitorFrame mframe;
            mframe.monitor = monitor;
            mframe.width = w;
            mframe.height = h;
            mframe.stride = w * 4;
            
            size_t sizeBytes = mframe.height * mframe.stride;
            mframe.bgra8.assign(static_cast<std::byte*>(pixels), static_cast<std::byte*>(pixels) + sizeBytes);
            
            // GDI produces BGRX, force Alpha to 255 (so D2D PREMULTIPLIED acts opaque)
            uint8_t* bgra = reinterpret_cast<uint8_t*>(mframe.bgra8.data());
            for (int i = 0; i < w * h; ++i) {
                bgra[i * 4 + 3] = 255;
            }

            result.frame.monitors.push_back(std::move(mframe));
            DeleteObject(hBitmap);
        }
    }

    DeleteDC(hMemoryDC);
    ReleaseDC(nullptr, hScreenDC);

    if (!result.frame.monitors.empty()) {
        result.success = true;
        result.errorCode = ScreenshotErrorCode::None;
    } else {
        result.success = false;
        result.errorCode = ScreenshotErrorCode::GdiFallbackFailed;
    }

    return result;
}

void GdiCaptureBackend::Reset() {
}
