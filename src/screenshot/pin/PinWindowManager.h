#pragma once

#include "PinWindow.h"
#include <vector>
#include <memory>

class PinWindowManager {
public:
    static PinWindowManager& Instance();

    void CreatePinWindow(HWND parent, const std::vector<std::byte>& bgraData, int width, int height, int x, int y);
    void RemovePinWindow(HWND hwnd);
    void CloseAll();
    size_t GetCount() const { return pinWindows_.size(); }

    // Keeps the message loop running until all pin windows are closed
    void RunMessageLoop();

private:
    PinWindowManager() = default;
    ~PinWindowManager() = default;

    // Prevent copying
    PinWindowManager(const PinWindowManager&) = delete;
    PinWindowManager& operator=(const PinWindowManager&) = delete;

    std::vector<std::unique_ptr<PinWindow>> pinWindows_;
};
