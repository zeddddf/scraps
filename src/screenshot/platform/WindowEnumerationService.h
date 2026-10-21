#pragma once

#include <vector>
#include "MonitorTopology.h" // Includes definition for RectPx

class WindowEnumerationService {
public:
    // Enumerates all visible, capturable windows on the desktop.
    // Returns their true bounds in virtual desktop coordinates, ordered from topmost to bottommost.
    static std::vector<RectPx> EnumerateCapturableWindows();
};
