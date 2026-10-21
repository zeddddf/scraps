#pragma once
#include <windows.h>
#include <memory>

struct ScreenshotLaunchOptions;
class ScreenshotSession;

class ScreenshotApp {
public:
    ScreenshotApp();
    ~ScreenshotApp();

    int Run(const ScreenshotLaunchOptions& options);

private:
    std::unique_ptr<ScreenshotSession> session_;
};
