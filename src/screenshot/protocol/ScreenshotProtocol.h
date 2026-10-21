#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <optional>
#include <windows.h>

struct ScreenshotLaunchOptions {
    std::string sessionId;
    std::wstring pipeName;
    std::string mode;
    uint32_t ownerPid = 0;
    bool hideOwnerWindows = true;

    static std::optional<ScreenshotLaunchOptions> Parse(int argc, wchar_t** argv);
};

enum class ScreenshotMessageType {
    Unknown,
    CaptureCompleted,
    CaptureCanceled,
    CaptureFailed
};

enum class ScreenshotErrorCode {
    None = 0,
    AccessDenied = 1,
    AccessLost = 2,
    Timeout = 3,
    WgcUnavailable = 4,
    GdiFallbackFailed = 5,
    UnknownError = 999
};

#include "platform/MonitorTopology.h"

struct ScreenshotMessage {
    ScreenshotMessageType type = ScreenshotMessageType::Unknown;
    bool success = true;
    
    // For CaptureCompleted
    int imageWidth = 0;
    int imageHeight = 0;
    std::string payloadFormat;
    RectPx selection;
    int payloadSize = 0;
    
    // Diagnostics
    std::string backend;
    int monitorCount = 0;

    // For CaptureFailed
    ScreenshotErrorCode errorCode = ScreenshotErrorCode::None;
    std::string errorMessage;

    std::string ToJson() const;
    static std::optional<ScreenshotMessage> FromJson(const std::string& json_str);
};

class PipeTransport {
public:
    PipeTransport();
    ~PipeTransport();

    // Disable copy
    PipeTransport(const PipeTransport&) = delete;
    PipeTransport& operator=(const PipeTransport&) = delete;

    bool Connect(const std::wstring& pipeName, DWORD timeoutMs = 5000);
    void Disconnect();

    bool SendMessageJson(const ScreenshotMessage& msg);
    
    // Send raw bytes (e.g. for PNG payload after the JSON message)
    bool SendPayload(const void* data, size_t size);

    // Read response/message
    bool ReadMessageJson(std::string& out_json);
    bool Peek(DWORD* bytes_avail);

private:
    HANDLE pipe_ = INVALID_HANDLE_VALUE;
};
