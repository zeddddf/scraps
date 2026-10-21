#include "PinWindowManager.h"
#include <algorithm>

PinWindowManager& PinWindowManager::Instance() {
    static PinWindowManager instance;
    return instance;
}

void PinWindowManager::CreatePinWindow(HWND parent, const std::vector<std::byte>& bgraData, int width, int height, int x, int y) {
    HINSTANCE hInstance = GetModuleHandle(nullptr);
    PinWindow::RegisterClass(hInstance);

    auto pin = std::make_unique<PinWindow>(parent, bgraData, width, height, x, y);
    if (pin->Show()) {
        pinWindows_.push_back(std::move(pin));
    }
}

void PinWindowManager::RemovePinWindow(HWND hwnd) {
    auto it = std::remove_if(pinWindows_.begin(), pinWindows_.end(), [hwnd](const auto& pin) {
        return pin->GetHwnd() == hwnd;
    });
    if (it != pinWindows_.end()) {
        pinWindows_.erase(it, pinWindows_.end());
    }

    if (pinWindows_.empty()) {
        PostQuitMessage(0);
    }
}

void PinWindowManager::CloseAll() {
    std::vector<HWND> hwnds;
    for (const auto& pin : pinWindows_) {
        hwnds.push_back(pin->GetHwnd());
    }
    for (HWND hwnd : hwnds) {
        DestroyWindow(hwnd);
    }
    pinWindows_.clear();
}

void PinWindowManager::RunMessageLoop() {
    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}
