#pragma once
#include <windows.h>
#include <memory>
#include <vector>
#include <cstddef>
#include <thread>
#include "capture/DesktopFrame.h"
#include "editing/InteractionStateMachine.h"

struct ScreenshotLaunchOptions;
class PipeTransport;
class CaptureSupervisor;
class OverlayWindowManager;
enum class ScreenshotErrorCode : int;

#define WM_USER_WAKEUP (WM_USER + 123)

class ScreenshotSession {
public:
    ScreenshotSession();
    ~ScreenshotSession();

    bool Initialize(const ScreenshotLaunchOptions& options);

    void StartInteractiveCapture();
    void Cancel();
    void Complete();
    void Pin();
    void Save();

    bool IsStandbyMode() const { return isStandbyMode_; }

    const DesktopFrame& GetDesktopFrame() const { return desktopFrame_; }
    InteractionStateMachine& GetStateMachine() { return stateMachine_; }

private:
    void CaptureInitialFrame();
    void ShowOverlay();
    void ExportResult();

    void SendCompleted(const std::vector<std::byte>& pngBytes);
    void SendCancelled();
    void SendFailed(ScreenshotErrorCode code, HRESULT hr);

    void ProcessIpcCommand(const std::string& cmdJson);
    void EnterStandbyState();

    std::unique_ptr<PipeTransport> pipe_;
    std::unique_ptr<CaptureSupervisor> capture_;
    std::unique_ptr<OverlayWindowManager> overlayManager_;
    DesktopFrame desktopFrame_;
    InteractionStateMachine stateMachine_;

    bool isStandbyMode_ = false;
    DWORD mainThreadId_ = 0;
    std::unique_ptr<std::thread> ipcListenerThread_;
};
