#include "app/ScreenshotApp.h"
#include "app/ScreenshotSession.h"
#include <windows.h>

ScreenshotApp::ScreenshotApp() {}
ScreenshotApp::~ScreenshotApp() {}

int ScreenshotApp::Run(const ScreenshotLaunchOptions& options) {
    session_ = std::make_unique<ScreenshotSession>();
    if (!session_->Initialize(options)) {
        return -1;
    }

    if (!session_->IsStandbyMode()) {
        session_->StartInteractiveCapture();
    }

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        if (msg.message == WM_USER_WAKEUP) {
            session_->StartInteractiveCapture();
        } else {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return static_cast<int>(msg.wParam);
}
