#pragma once

#include <vector>
#include <cstddef>
#include "../capture/DesktopFrame.h"
#include "../platform/MonitorTopology.h"

class RoiComposer {
public:
    static void Compose(const DesktopFrame& frame, const RectPx& virtualRoi, std::vector<std::byte>& outBgra8, int& outWidth, int& outHeight);
};
