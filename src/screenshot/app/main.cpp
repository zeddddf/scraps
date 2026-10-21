#include "app/ScreenshotApp.h"
#include "protocol/ScreenshotProtocol.h"
#include <shellapi.h>
#include <objbase.h>
#include <windows.h>

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    HRESULT hrCom = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hrCom) && hrCom != RPC_E_CHANGED_MODE) {
        return -1;
    }

    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return -1;

    auto options_opt = ScreenshotLaunchOptions::Parse(argc, argv);
    LocalFree(argv);

    if (!options_opt) {
        return -1;
    }

    ScreenshotApp app;
    int result = app.Run(*options_opt);

    if (SUCCEEDED(hrCom)) {
        CoUninitialize();
    }
    return result;
}
