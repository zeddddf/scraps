#include "app/ScreenshotSession.h"
#include "protocol/ScreenshotProtocol.h"
#include <nlohmann/json.hpp>
#include "capture/CaptureSupervisor.h"
#include "overlay/OverlayWindowManager.h"
#include "platform/MonitorTopology.h"
#include "export/RoiComposer.h"
#include "export/PngExporter.h"
#include "pin/PinWindowManager.h"
#include <commdlg.h>
#include "overlay/SharedDeviceResources.h"

namespace {
bool RenderAnnotationsToBuffer(
    const RectPx& exportRoi,
    int width,
    int height,
    std::vector<std::byte>& composedBgra,
    AnnotationDocument& document
) {
    if (composedBgra.empty() || width <= 0 || height <= 0) return false;

    auto& shared = SharedDeviceResources::Instance();
    if (!shared.Initialize()) return false;

    ID2D1Device* d2dDevice = shared.GetD2DDevice();
    if (!d2dDevice) return false;

    Microsoft::WRL::ComPtr<ID2D1DeviceContext> d2dContext;
    HRESULT hr = d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &d2dContext);
    if (FAILED(hr)) return false;

    Microsoft::WRL::ComPtr<IDWriteFactory> dwriteFactory;
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(dwriteFactory.GetAddressOf()));
    if (FAILED(hr)) return false;

    D2D1_SIZE_U size = D2D1::SizeU(width, height);
    D2D1_BITMAP_PROPERTIES1 srcProperties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_NONE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
    );

    // 源位图（含背景像素，可作马赛克模糊效果的输入）。
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> sourceBitmap;
    hr = d2dContext->CreateBitmap(size, composedBgra.data(), width * 4, &srcProperties, &sourceBitmap);
    if (FAILED(hr)) return false;

    D2D1_BITMAP_PROPERTIES1 tgtProperties = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
    );
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> targetBitmap;
    hr = d2dContext->CreateBitmap(size, nullptr, 0, &tgtProperties, &targetBitmap);
    if (FAILED(hr)) return false;

    d2dContext->SetTarget(targetBitmap.Get());
    d2dContext->BeginDraw();

    // 先按 1:1 落位背景，再切换到虚拟坐标系绘制标注。
    d2dContext->SetTransform(D2D1::Matrix3x2F::Identity());
    d2dContext->DrawBitmap(sourceBitmap.Get());

    D2D1_MATRIX_3X2_F translation = D2D1::Matrix3x2F::Translation(
        -static_cast<float>(exportRoi.x),
        -static_cast<float>(exportRoi.y)
    );
    d2dContext->SetTransform(translation);

    // 源背景在标注坐标系（虚拟坐标）中的矩形。
    const D2D1_RECT_F backgroundDest = D2D1::RectF(
        static_cast<float>(exportRoi.x),
        static_cast<float>(exportRoi.y),
        static_cast<float>(exportRoi.x + width),
        static_cast<float>(exportRoi.y + height));
    document.DrawAll(d2dContext.Get(), dwriteFactory.Get(), sourceBitmap.Get(), backgroundDest);

    hr = d2dContext->EndDraw();
    if (FAILED(hr)) return false;

    D2D1_BITMAP_PROPERTIES1 stagingProps = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
    );
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> stagingBitmap;
    hr = d2dContext->CreateBitmap(size, nullptr, 0, &stagingProps, &stagingBitmap);
    if (SUCCEEDED(hr)) {
        D2D1_POINT_2U destPoint = D2D1::Point2U(0, 0);
        D2D1_RECT_U srcRect = D2D1::RectU(0, 0, width, height);
        hr = stagingBitmap->CopyFromBitmap(&destPoint, targetBitmap.Get(), &srcRect);
        if (SUCCEEDED(hr)) {
            D2D1_MAPPED_RECT mappedRect;
            hr = stagingBitmap->Map(D2D1_MAP_OPTIONS_READ, &mappedRect);
            if (SUCCEEDED(hr)) {
                for (int y = 0; y < height; ++y) {
                    std::memcpy(
                        composedBgra.data() + y * width * 4,
                        mappedRect.bits + y * mappedRect.pitch,
                        width * 4
                    );
                }
                stagingBitmap->Unmap();
                return true;
            }
        }
    }

    D2D1_MAPPED_RECT mappedRect;
    hr = targetBitmap->Map(D2D1_MAP_OPTIONS_READ, &mappedRect);
    if (SUCCEEDED(hr)) {
        for (int y = 0; y < height; ++y) {
            std::memcpy(
                composedBgra.data() + y * width * 4,
                mappedRect.bits + y * mappedRect.pitch,
                width * 4
            );
        }
        targetBitmap->Unmap();
        return true;
    }

    return false;
}
} // namespace

ScreenshotSession::ScreenshotSession() {}
ScreenshotSession::~ScreenshotSession() {}

bool ScreenshotSession::Initialize(const ScreenshotLaunchOptions& options) {
    mainThreadId_ = GetCurrentThreadId();
    isStandbyMode_ = (options.mode == "standby");

    pipe_ = std::make_unique<PipeTransport>();
    if (!options.pipeName.empty()) {
        pipe_->Connect(options.pipeName);
    }
    capture_ = std::make_unique<CaptureSupervisor>();
    overlayManager_ = std::make_unique<OverlayWindowManager>(this);

    if (isStandbyMode_) {
        MonitorTopology topology;
        overlayManager_->CreateWindows(topology.GetMonitors());

        ipcListenerThread_ = std::make_unique<std::thread>([this]() {
            while (true) {
                DWORD bytes_avail = 0;
                if (pipe_->Peek(&bytes_avail)) {
                    if (bytes_avail > 0) {
                        std::string cmd;
                        if (pipe_->ReadMessageJson(cmd)) {
                            ProcessIpcCommand(cmd);
                        } else {
                            break;
                        }
                    } else {
                        Sleep(50);
                    }
                } else {
                    break;
                }
            }
            ::PostThreadMessageW(mainThreadId_, WM_QUIT, 0, 0);
        });
    }

    return true;
}

void ScreenshotSession::StartInteractiveCapture() {
    stateMachine_.GetUiaSnapping().StartCrawl();
    CaptureInitialFrame();
    ShowOverlay();
}

void ScreenshotSession::Cancel() {
    ScreenshotMessage msg;
    msg.type = ScreenshotMessageType::CaptureCanceled;
    msg.success = true;
    pipe_->SendMessageJson(msg);
    if (isStandbyMode_) {
        EnterStandbyState();
    } else {
        ::PostQuitMessage(0);
    }
}

void ScreenshotSession::Complete() {
    RectPx exportRoi;
    if (stateMachine_.GetSelection().HasSelection()) {
        exportRoi = stateMachine_.GetSelection().GetCurrentRoi();
    } else {
        MonitorTopology topology;
        exportRoi = topology.GetVirtualDesktopBounds();
    }

    std::vector<std::byte> composedBgra;
    int outW = 0, outH = 0;
    
    RoiComposer::Compose(desktopFrame_, exportRoi, composedBgra, outW, outH);

    // Burn annotations into the composed BGRA8 buffer
    RenderAnnotationsToBuffer(exportRoi, outW, outH, composedBgra, stateMachine_.GetDocument());

    std::vector<std::byte> pngData;
    PngExporter::Encode(composedBgra, outW, outH, pngData);

    // Write to system clipboard
    if (OpenClipboard(nullptr)) {
        EmptyClipboard();

        // 1. Write PNG
        UINT pngFormat = RegisterClipboardFormatW(L"PNG");
        HGLOBAL hPng = GlobalAlloc(GMEM_MOVEABLE, pngData.size());
        if (hPng) {
            void* ptr = GlobalLock(hPng);
            memcpy(ptr, pngData.data(), pngData.size());
            GlobalUnlock(hPng);
            SetClipboardData(pngFormat, hPng);
        }

        // 2. Write CF_DIBV5
        size_t headerSize = sizeof(BITMAPV5HEADER);
        size_t pixelSize = outW * outH * 4;
        HGLOBAL hDib = GlobalAlloc(GMEM_MOVEABLE, headerSize + pixelSize);
        if (hDib) {
            void* ptr = GlobalLock(hDib);
            BITMAPV5HEADER* bV5 = reinterpret_cast<BITMAPV5HEADER*>(ptr);
            memset(bV5, 0, headerSize);
            bV5->bV5Size = sizeof(BITMAPV5HEADER);
            bV5->bV5Width = outW;
            bV5->bV5Height = -outH; // top-down
            bV5->bV5Planes = 1;
            bV5->bV5BitCount = 32;
            bV5->bV5Compression = BI_BITFIELDS;
            bV5->bV5RedMask   = 0x00FF0000;
            bV5->bV5GreenMask = 0x0000FF00;
            bV5->bV5BlueMask  = 0x000000FF;
            bV5->bV5AlphaMask = 0xFF000000;
            
            uint8_t* pixels = reinterpret_cast<uint8_t*>(bV5) + headerSize;
            memcpy(pixels, composedBgra.data(), pixelSize);
            
            GlobalUnlock(hDib);
            SetClipboardData(CF_DIBV5, hDib);
        }

        CloseClipboard();
    }

    ScreenshotMessage msg;
    msg.type = ScreenshotMessageType::CaptureCompleted;
    msg.success = true;
    msg.imageWidth = outW;
    msg.imageHeight = outH;
    msg.payloadFormat = "";
    msg.selection = exportRoi;
    msg.payloadSize = 0;
    
    pipe_->SendMessageJson(msg);

    if (isStandbyMode_) {
        EnterStandbyState();
    } else {
        ::PostQuitMessage(0);
    }
}

void ScreenshotSession::Pin() {
    RectPx exportRoi;
    if (stateMachine_.GetSelection().HasSelection()) {
        exportRoi = stateMachine_.GetSelection().GetCurrentRoi();
    } else {
        MonitorTopology topology;
        exportRoi = topology.GetVirtualDesktopBounds();
    }

    std::vector<std::byte> composedBgra;
    int outW = 0, outH = 0;
    
    RoiComposer::Compose(desktopFrame_, exportRoi, composedBgra, outW, outH);

    // Burn annotations into the composed BGRA8 buffer
    RenderAnnotationsToBuffer(exportRoi, outW, outH, composedBgra, stateMachine_.GetDocument());

    PinWindowManager::Instance().CreatePinWindow(nullptr, composedBgra, outW, outH, exportRoi.x, exportRoi.y);

    if (isStandbyMode_) {
        EnterStandbyState();
    } else {
        if (overlayManager_) {
            overlayManager_->Destroy();
        }
    }
}

void ScreenshotSession::Save() {
    RectPx exportRoi;
    if (stateMachine_.GetSelection().HasSelection()) {
        exportRoi = stateMachine_.GetSelection().GetCurrentRoi();
    } else {
        MonitorTopology topology;
        exportRoi = topology.GetVirtualDesktopBounds();
    }

    std::vector<std::byte> composedBgra;
    int outW = 0, outH = 0;
    
    RoiComposer::Compose(desktopFrame_, exportRoi, composedBgra, outW, outH);

    // Burn annotations into the composed BGRA8 buffer
    RenderAnnotationsToBuffer(exportRoi, outW, outH, composedBgra, stateMachine_.GetDocument());

    HWND hwndOwner = overlayManager_ ? overlayManager_->GetOwnerHwnd() : nullptr;

    wchar_t szFile[MAX_PATH] = L"screenshot.png";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwndOwner;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile) / sizeof(wchar_t);
    ofn.lpstrFilter = L"PNG Image\0*.png\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrDefExt = L"png";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;

    if (GetSaveFileNameW(&ofn)) {
        std::vector<std::byte> pngData;
        if (PngExporter::Encode(composedBgra, outW, outH, pngData)) {
            HANDLE hFile = CreateFileW(szFile, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hFile != INVALID_HANDLE_VALUE) {
                std::byte* dataPtr = pngData.data();
                DWORD bytesToWrite = static_cast<DWORD>(pngData.size());
                DWORD bytesWritten = 0;
                WriteFile(hFile, dataPtr, bytesToWrite, &bytesWritten, nullptr);
                CloseHandle(hFile);

                if (isStandbyMode_) {
                    EnterStandbyState();
                } else {
                    ::PostQuitMessage(0);
                }
            }
        }
    }
}

void ScreenshotSession::CaptureInitialFrame() {
    overlayManager_->ResetBackgroundBitmaps();
    auto result = capture_->CaptureOnce();
    if (result.success) {
        desktopFrame_ = std::move(result.frame);
    } else {
        wchar_t errMsg[256];
        wsprintf(errMsg, L"Capture failed. Error Code: %d, HR: 0x%08X", (int)result.errorCode, result.hr);
        MessageBoxW(nullptr, errMsg, L"Capture Error", MB_ICONERROR);
        SendFailed(result.errorCode, result.hr);
        if (isStandbyMode_) {
            EnterStandbyState();
        } else {
            ::PostQuitMessage(0);
        }
    }
}

void ScreenshotSession::ShowOverlay() {
    MonitorTopology topology;
    // We already acquired the desktop frame in CaptureInitialFrame
    // Now create windows for each monitor
    if (overlayManager_->CreateWindows(topology.GetMonitors())) {
        overlayManager_->InvalidateAll(); // Draw dim mask
        overlayManager_->Show();
    }
}

void ScreenshotSession::ExportResult() {
    std::vector<std::byte> dummy;
    SendCompleted(dummy);
}

void ScreenshotSession::SendCompleted(const std::vector<std::byte>& pngBytes) {
    ScreenshotMessage msg;
    msg.type = ScreenshotMessageType::CaptureCompleted;
    msg.success = true;
    pipe_->SendMessageJson(msg);
    if (!pngBytes.empty()) {
        pipe_->SendPayload(pngBytes.data(), pngBytes.size());
    }
}

void ScreenshotSession::SendCancelled() {
    ScreenshotMessage msg;
    msg.type = ScreenshotMessageType::CaptureCanceled;
    pipe_->SendMessageJson(msg);
}

void ScreenshotSession::SendFailed(ScreenshotErrorCode code, HRESULT /*hr*/) {
    ScreenshotMessage msg;
    msg.type = ScreenshotMessageType::CaptureFailed;
    msg.success = false;
    msg.errorCode = code;
    pipe_->SendMessageJson(msg);
}

void ScreenshotSession::ProcessIpcCommand(const std::string& cmdJson) {
    try {
        auto j = nlohmann::json::parse(cmdJson);
        if (j.value("command", "") == "wake_up") {
            ::PostThreadMessageW(mainThreadId_, WM_USER_WAKEUP, 0, 0);
        }
    } catch (...) {
    }
}

void ScreenshotSession::EnterStandbyState() {
    if (overlayManager_) {
        overlayManager_->Hide();
    }
    stateMachine_.Reset();
    desktopFrame_.monitors.clear();
}
