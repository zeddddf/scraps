#pragma once

#include <vector>
#include <cstddef>
#include <cstdint>
#include <d3d11.h>
#include <wrl/client.h>
#include "platform/MonitorTopology.h"

enum class ColorSpace {
    SRgb,
    ScRgb,
    Hdr10,
    HdrTonemappedSRgb
};

enum class CaptureBackendType {
    DxgiDuplication,
    WindowsGraphicsCapture,
    GdiBitBlt
};

struct MonitorFrame {
    MonitorSnapshot monitor;

    int width = 0;
    int height = 0;
    int stride = 0;

    DXGI_FORMAT sourceFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
    ColorSpace colorSpace = ColorSpace::SRgb;

    std::vector<std::byte> bgra8;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
};

struct DesktopFrame {
    std::vector<MonitorFrame> monitors;
    uint64_t timestampQpc = 0;
    CaptureBackendType backend = CaptureBackendType::DxgiDuplication;
};
