#include "ScreenshotProtocol.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

std::string WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

std::optional<ScreenshotLaunchOptions> ScreenshotLaunchOptions::Parse(int argc, wchar_t** argv) {
    ScreenshotLaunchOptions opts;
    try {
        for (int i = 1; i < argc; ++i) {
            std::wstring arg = argv[i];
            if (arg == L"--session-id" && i + 1 < argc) {
                opts.sessionId = WideToUtf8(argv[++i]);
            } else if (arg == L"--pipe" && i + 1 < argc) {
                opts.pipeName = argv[++i];
            } else if (arg == L"--mode" && i + 1 < argc) {
                opts.mode = WideToUtf8(argv[++i]);
            } else if (arg == L"--owner-pid" && i + 1 < argc) {
                opts.ownerPid = std::stoul(argv[++i]);
            } else if (arg == L"--hide-owner-windows" && i + 1 < argc) {
                std::wstring val = argv[++i];
                opts.hideOwnerWindows = (val == L"true" || val == L"1");
            }
        }
    } catch (...) {
        return std::nullopt;
    }
    
    // If empty, it's just test mode
    return opts;
}

std::string ScreenshotMessage::ToJson() const {
    json j;
    
    switch (type) {
        case ScreenshotMessageType::CaptureCompleted: j["type"] = "capture.completed"; break;
        case ScreenshotMessageType::CaptureCanceled: j["type"] = "capture.canceled"; break;
        case ScreenshotMessageType::CaptureFailed: j["type"] = "capture.failed"; break;
        default: j["type"] = "unknown"; break;
    }
    
    j["success"] = success;

    if (type == ScreenshotMessageType::CaptureCompleted) {
        j["image"] = {
            {"width", imageWidth},
            {"height", imageHeight},
            {"payloadFormat", payloadFormat},
            {"payloadSize", payloadSize}
        };
        j["selection"] = {
            {"x", selection.x},
            {"y", selection.y},
            {"width", selection.width},
            {"height", selection.height}
        };
        j["diagnostics"] = {
            {"backend", backend},
            {"monitorCount", monitorCount}
        };
    } else if (type == ScreenshotMessageType::CaptureFailed) {
        j["error"] = {
            {"code", static_cast<int>(errorCode)},
            {"message", errorMessage}
        };
    }

    return j.dump();
}

std::optional<ScreenshotMessage> ScreenshotMessage::FromJson(const std::string& json_str) {
    try {
        json j = json::parse(json_str);
        ScreenshotMessage msg;
        
        std::string type_str = j.value("type", "");
        if (type_str == "capture.completed") msg.type = ScreenshotMessageType::CaptureCompleted;
        else if (type_str == "capture.canceled") msg.type = ScreenshotMessageType::CaptureCanceled;
        else if (type_str == "capture.failed") msg.type = ScreenshotMessageType::CaptureFailed;
        else msg.type = ScreenshotMessageType::Unknown;

        msg.success = j.value("success", false);

        if (j.contains("image")) {
            auto& img = j["image"];
            msg.imageWidth = img.value("width", 0);
            msg.imageHeight = img.value("height", 0);
            msg.payloadFormat = img.value("payloadFormat", "");
            msg.payloadSize = img.value("payloadSize", 0);
        }

        if (j.contains("selection")) {
            auto& sel = j["selection"];
            msg.selection.x = sel.value("x", 0);
            msg.selection.y = sel.value("y", 0);
            msg.selection.width = sel.value("width", 0);
            msg.selection.height = sel.value("height", 0);
        }

        if (j.contains("diagnostics")) {
            auto& diag = j["diagnostics"];
            msg.backend = diag.value("backend", "");
            msg.monitorCount = diag.value("monitorCount", 0);
        }

        if (j.contains("error")) {
            auto& err = j["error"];
            msg.errorCode = static_cast<ScreenshotErrorCode>(err.value("code", 0));
            msg.errorMessage = err.value("message", "");
        }

        return msg;
    } catch (...) {
        return std::nullopt;
    }
}

PipeTransport::PipeTransport() = default;

PipeTransport::~PipeTransport() {
    Disconnect();
}

bool PipeTransport::Connect(const std::wstring& pipeName, DWORD timeoutMs) {
    if (!WaitNamedPipeW(pipeName.c_str(), timeoutMs)) {
        return false;
    }

    pipe_ = CreateFileW(
        pipeName.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);

    if (pipe_ == INVALID_HANDLE_VALUE) {
        return false;
    }

    DWORD mode = PIPE_READMODE_MESSAGE;
    if (!SetNamedPipeHandleState(pipe_, &mode, nullptr, nullptr)) {
        // Continue even if setting mode fails
    }

    return true;
}

void PipeTransport::Disconnect() {
    if (pipe_ != INVALID_HANDLE_VALUE) {
        CloseHandle(pipe_);
        pipe_ = INVALID_HANDLE_VALUE;
    }
}

bool PipeTransport::SendMessageJson(const ScreenshotMessage& msg) {
    if (pipe_ == INVALID_HANDLE_VALUE) return false;

    std::string payload = msg.ToJson();
    DWORD bytes_written = 0;
    BOOL write_ok = WriteFile(
        pipe_,
        payload.data(),
        static_cast<DWORD>(payload.size()),
        &bytes_written,
        nullptr);
        
    return write_ok == TRUE && bytes_written == payload.size();
}

bool PipeTransport::SendPayload(const void* data, size_t size) {
    if (pipe_ == INVALID_HANDLE_VALUE || !data || size == 0) return false;

    DWORD bytes_written = 0;
    BOOL write_ok = WriteFile(
        pipe_,
        data,
        static_cast<DWORD>(size),
        &bytes_written,
        nullptr);
        
    return write_ok == TRUE && bytes_written == size;
}

bool PipeTransport::ReadMessageJson(std::string& out_json) {
    if (pipe_ == INVALID_HANDLE_VALUE) return false;

    out_json.clear();
    std::vector<char> read_buffer(4096);
    DWORD bytes_read = 0;

    while (true) {
        BOOL read_ok = ReadFile(
            pipe_,
            read_buffer.data(),
            static_cast<DWORD>(read_buffer.size()),
            &bytes_read,
            nullptr);

        if (read_ok) {
            out_json.append(read_buffer.data(), bytes_read);
            return true;
        }

        DWORD error = GetLastError();
        if (error == ERROR_MORE_DATA) {
            out_json.append(read_buffer.data(), bytes_read);
        } else {
            return false;
        }
    }
}

bool PipeTransport::Peek(DWORD* bytes_avail) {
    if (pipe_ == INVALID_HANDLE_VALUE) return false;
    return PeekNamedPipe(pipe_, nullptr, 0, nullptr, bytes_avail, nullptr) == TRUE;
}
