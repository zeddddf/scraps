#include "PinWindow.h"
#include "PinWindowManager.h"
#include "export/PngExporter.h"
#include <commdlg.h>
#include <shlwapi.h>
#include <cmath>
#include <algorithm>
#include <string>
#include <d2d1effects.h>

#pragma comment(lib, "shlwapi.lib")

constexpr const wchar_t* kClassName = L"Scraps_PinWindow";

// Constants for aesthetics
constexpr float kPadding = 16.0f;
constexpr float kCornerRadius = 6.0f;

// Context Menu IDs
constexpr UINT_PTR IDM_COPY = 2001;
constexpr UINT_PTR IDM_SAVE = 2002;
constexpr UINT_PTR IDM_CLOSE = 2003;

static void ShowError(const wchar_t* title, const wchar_t* format, ...) {
    va_list args;
    va_start(args, format);
    wchar_t buf[1024];
    vswprintf_s(buf, format, args);
    va_end(args);
    MessageBoxW(nullptr, buf, title, MB_OK | MB_ICONERROR | MB_TOPMOST);
}

bool PinWindow::RegisterClass(HINSTANCE hInstance) {
    WNDCLASSEXW wcx{};
    wcx.cbSize = sizeof(wcx);
    wcx.style = CS_DBLCLKS | CS_HREDRAW | CS_VREDRAW;
    wcx.lpfnWndProc = WndProc;
    wcx.hInstance = hInstance;
    wcx.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wcx.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
    wcx.lpszClassName = kClassName;

    ATOM atom = ::RegisterClassExW(&wcx);
    return atom != 0;
}

PinWindow::PinWindow(HWND parent, const std::vector<std::byte>& bgraData, int width, int height, int x, int y)
    : rawBgra_(bgraData), rawWidth_(width), rawHeight_(height) {
    // Force alpha channel to fully opaque once at construction time.
    // DXGI BGRA captures may have undefined alpha; D2D premultiplied mode requires alpha=255.
    for (size_t i = 3; i < rawBgra_.size(); i += 4) {
        rawBgra_[i] = std::byte{255};
    }
    
    HINSTANCE hInstance = GetModuleHandle(nullptr);
    
    // Add padding to window dimensions for shadow space
    int w = width + static_cast<int>(kPadding * 2);
    int h = height + static_cast<int>(kPadding * 2);
    int wx = x - static_cast<int>(kPadding);
    int wy = y - static_cast<int>(kPadding);

    hwnd_ = CreateWindowExW(
        WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
        kClassName,
        L"Pin",
        WS_POPUP,
        wx, wy, w, h,
        parent,
        nullptr,
        hInstance,
        this
    );

    if (!hwnd_) {
        DWORD err = GetLastError();
        ShowError(L"CreateWindowExW Error", L"Failed to create window. Error: %lu", err);
        return;
    }

    dcomp_ = std::make_unique<DCompDeviceResources>();
    if (!dcomp_->Initialize(hwnd_)) {
        ShowError(L"DComp Error", L"Failed to initialize DComp resources for PinWindow.");
    }
}

PinWindow::~PinWindow() {
    // Resources are automatically cleaned up via Smart Pointers / std::unique_ptr
}

bool PinWindow::Show() {
    if (!hwnd_ || !dcomp_) return false;
    
    // Start shadow fade-in animation
    animStartTime_ = GetTickCount64();
    isAnimatingShadow_ = true;
    SetTimer(hwnd_, 3001, 16, nullptr);
    
    Paint();
    
    ::ShowWindow(hwnd_, SW_SHOWNA);
    ::UpdateWindow(hwnd_);
    return true;
}

LRESULT CALLBACK PinWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    PinWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        CREATESTRUCT* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        self = reinterpret_cast<PinWindow*>(cs->lpCreateParams);
        if (self) {
            self->hwnd_ = hwnd;
        }
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<PinWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self) {
        return self->HandleMessage(hwnd, msg, wParam, lParam);
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT PinWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE:
            return 0;
            
        case WM_NCHITTEST: {
            POINT pt;
            GetCursorPos(&pt);
            RECT rect;
            GetWindowRect(hwnd, &rect);
            
            // Allow dragging only when clicking inside the actual image region (excluding transparent shadow area)
            float imgLeft = rect.left + kPadding;
            float imgTop = rect.top + kPadding;
            float imgRight = rect.right - kPadding;
            float imgBottom = rect.bottom - kPadding;

            if (pt.x >= imgLeft && pt.x <= imgRight && pt.y >= imgTop && pt.y <= imgBottom) {
                return HTCAPTION;
            }
            return HTTRANSPARENT; // Click passes through the shadow area
        }

        case WM_ACTIVATE:
            isFocused_ = (LOWORD(wParam) != WA_INACTIVE);
            Paint();
            return 0;

        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE && isFocused_) {
                DestroyWindow(hwnd);
                return 0;
            }
            break;

        case WM_PAINT:
            Paint();
            ::ValidateRect(hwnd, nullptr);
            return 0;

        case WM_TIMER:
            if (wParam == 3001) {
                Paint();
                return 0;
            }
            break;

        case WM_SIZE:
            // Handled dynamically during window resizing via Resize()
            return 0;

        case WM_NCLBUTTONDBLCLK:
            if (wParam == HTCAPTION && isFocused_) {
                DestroyWindow(hwnd);
            }
            return 0;

        case WM_LBUTTONDBLCLK:
            if (isFocused_) {
                DestroyWindow(hwnd);
            }
            return 0;

        case WM_MOUSEWHEEL: {
            short delta = GET_WHEEL_DELTA_WPARAM(wParam);
            bool isCtrl = (GetKeyState(VK_CONTROL) < 0);
            
            POINT screenPt;
            GetCursorPos(&screenPt);

            if (isCtrl) {
                HandleOpacity(delta);
            } else {
                HandleZoom(delta, screenPt);
            }
            return 0;
        }

        case WM_RBUTTONUP: {
            POINT pt;
            GetCursorPos(&pt);
            
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, IDM_COPY, L"复制 (Copy)");
            AppendMenuW(hMenu, MF_STRING, IDM_SAVE, L"保存 (Save)");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hMenu, MF_STRING, IDM_CLOSE, L"销毁 (Close)");

            TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(hMenu);
            return 0;
        }

        case WM_COMMAND: {
            UINT_PTR id = LOWORD(wParam);
            if (id == IDM_COPY) {
                CopyToClipboard();
            } else if (id == IDM_SAVE) {
                SaveToFile();
            } else if (id == IDM_CLOSE) {
                DestroyWindow(hwnd);
            }
            return 0;
        }

        case WM_DESTROY:
            PinWindowManager::Instance().RemovePinWindow(hwnd);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void PinWindow::Paint() {
    if (!dcomp_) return;

    dcomp_->BeginDraw();
    ID2D1DeviceContext* d2dContext = dcomp_->GetD2DContext();
    if (!d2dContext) {
        dcomp_->EndDraw();
        return;
    }

    // Calculate animation progress
    float animProgress = 1.0f;
    if (isAnimatingShadow_) {
        ULONGLONG elapsed = GetTickCount64() - animStartTime_;
        animProgress = static_cast<float>(elapsed) / 450.0f;
        if (animProgress >= 1.0f) {
            animProgress = 1.0f;
            isAnimatingShadow_ = false;
            KillTimer(hwnd_, 3001);
        }
        // Smooth cubic ease-out curve
        float t = 1.0f - animProgress;
        animProgress = 1.0f - (t * t * t);
    }

    // Clear background with fully transparent pixels (Alpha = 0)
    d2dContext->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

    RECT rect;
    GetClientRect(hwnd_, &rect);
    float w = static_cast<float>(rect.right - rect.left);
    float h = static_cast<float>(rect.bottom - rect.top);

    float imgW = w - kPadding * 2.0f;
    float imgH = h - kPadding * 2.0f;

    if (imgW > 0 && imgH > 0) {
        D2D1_RECT_F imgRect = D2D1::RectF(kPadding, kPadding, kPadding + imgW, kPadding + imgH);
        D2D1_ROUNDED_RECT roundedImg = D2D1::RoundedRect(imgRect, kCornerRadius, kCornerRadius);

        // Push Layer with global opacity multiplier
        D2D1_LAYER_PARAMETERS parameters = D2D1::LayerParameters(
            D2D1::InfiniteRect(),
            nullptr,
            D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
            D2D1::IdentityMatrix(),
            alpha_ / 255.0f
        );
        d2dContext->PushLayer(&parameters, nullptr);

        // 1. Draw premium blue shadow using CLSID_D2D1Shadow
        Microsoft::WRL::ComPtr<ID2D1Effect> shadowEffect;
        HRESULT hr = d2dContext->CreateEffect(CLSID_D2D1Shadow, &shadowEffect);
        if (SUCCEEDED(hr) && shadowEffect) {
            // Save original target
            Microsoft::WRL::ComPtr<ID2D1Image> originalTarget;
            d2dContext->GetTarget(&originalTarget);

            // Record geometry into command list
            Microsoft::WRL::ComPtr<ID2D1CommandList> shadowCmdList;
            hr = d2dContext->CreateCommandList(&shadowCmdList);
            if (SUCCEEDED(hr) && shadowCmdList) {
                d2dContext->SetTarget(shadowCmdList.Get());
                
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> fillBrush;
                d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &fillBrush);
                if (fillBrush) {
                    d2dContext->FillRoundedRectangle(roundedImg, fillBrush.Get());
                }
                
                shadowCmdList->Close();

                // Set target back to swapchain target
                d2dContext->SetTarget(originalTarget.Get());

                // Configure shadow effect
                shadowEffect->SetInput(0, shadowCmdList.Get());
                shadowEffect->SetValue(D2D1_SHADOW_PROP_BLUR_STANDARD_DEVIATION, std::max(0.1f, 8.0f * animProgress));
                
                D2D1_VECTOR_4F shadowColor = isFocused_ ? 
                    D2D1::Vector4F(0.0f, 0.47f, 0.83f, 0.75f * animProgress) : // Fluent Blue (deeper)
                    D2D1::Vector4F(0.3f, 0.3f, 0.3f, 0.55f * animProgress);  // Darker Neutral Gray
                shadowEffect->SetValue(D2D1_SHADOW_PROP_COLOR, shadowColor);

                // Draw shadow with a slightly larger vertical offset drifting into position
                d2dContext->DrawImage(shadowEffect.Get(), D2D1::Point2F(0.0f, 3.0f * animProgress));
            } else {
                d2dContext->SetTarget(originalTarget.Get());
            }
        }

        // 2. Draw the clipped image using Bitmap Brush
        if (!d2dBitmap_ && rawWidth_ > 0 && rawHeight_ > 0 && !rawBgra_.empty()) {
            // Alpha already forced to 255 in constructor; just create the D2D bitmap.
            D2D1_SIZE_U bmpSize = D2D1::SizeU(rawWidth_, rawHeight_);
            D2D1_BITMAP_PROPERTIES1 bmpProps = D2D1::BitmapProperties1(
                D2D1_BITMAP_OPTIONS_NONE,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
            );

            hr = d2dContext->CreateBitmap(bmpSize, rawBgra_.data(), rawWidth_ * 4, &bmpProps, &d2dBitmap_);
            // CreateBitmap failure is handled gracefully: d2dBitmap_ remains null, image simply won't render
        }

        if (d2dBitmap_) {
            Microsoft::WRL::ComPtr<ID2D1BitmapBrush> bmpBrush;
            d2dContext->CreateBitmapBrush(d2dBitmap_.Get(), &bmpBrush);
            if (bmpBrush) {
                float scaleW = imgW / static_cast<float>(rawWidth_);
                float scaleH = imgH / static_cast<float>(rawHeight_);
                D2D1_MATRIX_3X2_F mat = D2D1::Matrix3x2F::Scale(scaleW, scaleH) * D2D1::Matrix3x2F::Translation(kPadding, kPadding);
                bmpBrush->SetTransform(mat);
                bmpBrush->SetInterpolationMode(D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);

                d2dContext->FillRoundedRectangle(roundedImg, bmpBrush.Get());

                // Draw a thin subtle border around the image
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> borderBrush;
                D2D1_COLOR_F borderColor = isFocused_ ?
                    D2D1::ColorF(0.0f, 0.47f, 0.83f, 0.4f) :
                    D2D1::ColorF(0.5f, 0.5f, 0.5f, 0.3f);
                d2dContext->CreateSolidColorBrush(borderColor, &borderBrush);
                if (borderBrush) {
                    d2dContext->DrawRoundedRectangle(roundedImg, borderBrush.Get(), 1.0f);
                }
            }
        }

        d2dContext->PopLayer();
    }

    dcomp_->EndDraw();
    dcomp_->Commit();
}

void PinWindow::HandleZoom(short delta, POINT mouseScreenPt) {
    float multiplier = (delta > 0) ? 1.1f : 0.9f;
    float newScale = scale_ * multiplier;
    
    newScale = std::clamp(newScale, 0.15f, 10.0f);
    if (std::abs(newScale - scale_) < 0.001f) return;

    RECT rect;
    GetWindowRect(hwnd_, &rect);

    // Calculate position of cursor relative to window's IMAGE top-left
    float dx = static_cast<float>(mouseScreenPt.x - rect.left) - kPadding;
    float dy = static_cast<float>(mouseScreenPt.y - rect.top) - kPadding;

    float imgX = dx / scale_;
    float imgY = dy / scale_;

    scale_ = newScale;

    // Calculate new total window size including shadow padding
    int newImgW = static_cast<int>(std::round(rawWidth_ * scale_));
    int newImgH = static_cast<int>(std::round(rawHeight_ * scale_));

    int newW = newImgW + static_cast<int>(kPadding * 2.0f);
    int newH = newImgH + static_cast<int>(kPadding * 2.0f);

    // Adjust left/top to keep cursor at the exact same image pixel location
    int newLeft = static_cast<int>(std::round(mouseScreenPt.x - imgX * scale_ - kPadding));
    int newTop = static_cast<int>(std::round(mouseScreenPt.y - imgY * scale_ - kPadding));

    SetWindowPos(hwnd_, nullptr, newLeft, newTop, newW, newH, SWP_NOZORDER | SWP_NOACTIVATE);
    
    if (dcomp_) {
        dcomp_->Resize(newW, newH);
    }
    
    Paint();
}

void PinWindow::HandleOpacity(short delta) {
    int newAlpha = static_cast<int>(alpha_) + ((delta > 0) ? 15 : -15);
    newAlpha = std::clamp(newAlpha, 26, 255);
    
    if (newAlpha == alpha_) return;
    
    alpha_ = static_cast<BYTE>(newAlpha);
    Paint();
}

void PinWindow::SaveToFile() {
    wchar_t szFile[MAX_PATH] = L"screenshot.png";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
    ofn.lpstrFilter = L"PNG Image\0*.png\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrDefExt = L"png";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;

    if (GetSaveFileNameW(&ofn)) {
        std::vector<std::byte> pngData;
        if (PngExporter::Encode(rawBgra_, rawWidth_, rawHeight_, pngData)) {
            HANDLE hFile = CreateFileW(szFile, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hFile != INVALID_HANDLE_VALUE) {
                std::byte* dataPtr = pngData.data();
                DWORD bytesToWrite = static_cast<DWORD>(pngData.size());
                DWORD bytesWritten = 0;
                WriteFile(hFile, dataPtr, bytesToWrite, &bytesWritten, nullptr);
                CloseHandle(hFile);
            }
        }
    }
}

void PinWindow::CopyToClipboard() {
    std::vector<std::byte> pngData;
    if (!PngExporter::Encode(rawBgra_, rawWidth_, rawHeight_, pngData)) {
        return;
    }

    if (OpenClipboard(hwnd_)) {
        EmptyClipboard();

        UINT pngFormat = RegisterClipboardFormatW(L"PNG");
        HGLOBAL hPng = GlobalAlloc(GMEM_MOVEABLE, pngData.size());
        if (hPng) {
            void* ptr = GlobalLock(hPng);
            memcpy(ptr, pngData.data(), pngData.size());
            GlobalUnlock(hPng);
            SetClipboardData(pngFormat, hPng);
        }

        size_t headerSize = sizeof(BITMAPV5HEADER);
        size_t pixelSize = rawWidth_ * rawHeight_ * 4;
        HGLOBAL hDib = GlobalAlloc(GMEM_MOVEABLE, headerSize + pixelSize);
        if (hDib) {
            void* ptr = GlobalLock(hDib);
            BITMAPV5HEADER* bV5 = reinterpret_cast<BITMAPV5HEADER*>(ptr);
            memset(bV5, 0, headerSize);
            bV5->bV5Size = sizeof(BITMAPV5HEADER);
            bV5->bV5Width = rawWidth_;
            bV5->bV5Height = -rawHeight_; // top-down
            bV5->bV5Planes = 1;
            bV5->bV5BitCount = 32;
            bV5->bV5Compression = BI_BITFIELDS;
            bV5->bV5RedMask   = 0x00FF0000;
            bV5->bV5GreenMask = 0x0000FF00;
            bV5->bV5BlueMask  = 0x000000FF;
            bV5->bV5AlphaMask = 0xFF000000;
            
            uint8_t* pixels = reinterpret_cast<uint8_t*>(bV5) + headerSize;
            memcpy(pixels, rawBgra_.data(), pixelSize);
            
            GlobalUnlock(hDib);
            SetClipboardData(CF_DIBV5, hDib);
        }

        CloseClipboard();
    }
}
