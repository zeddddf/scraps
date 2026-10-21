#include "RoiComposer.h"
#include <cstring>
#include <algorithm>

void RoiComposer::Compose(const DesktopFrame& frame, const RectPx& virtualRoi, std::vector<std::byte>& outBgra8, int& outWidth, int& outHeight) {
    outWidth = virtualRoi.width;
    outHeight = virtualRoi.height;

    if (outWidth <= 0 || outHeight <= 0) {
        outBgra8.clear();
        return;
    }

    outBgra8.assign(outWidth * outHeight * 4, std::byte{0});

    for (const auto& mframe : frame.monitors) {
        if (mframe.bgra8.empty() || mframe.width <= 0 || mframe.height <= 0) continue;

        RectPx intersectRect = virtualRoi.intersect(mframe.monitor.virtualBounds);
        if (intersectRect.width <= 0 || intersectRect.height <= 0) continue;

        int localX = intersectRect.x - mframe.monitor.virtualBounds.x;
        int localY = intersectRect.y - mframe.monitor.virtualBounds.y;

        int outX = intersectRect.x - virtualRoi.x;
        int outY = intersectRect.y - virtualRoi.y;

        int srcStride = mframe.stride;
        if (srcStride <= 0) srcStride = mframe.width * 4;

        int destStride = outWidth * 4;
        int copyBytes = intersectRect.width * 4;

        for (int y = 0; y < intersectRect.height; ++y) {
            int srcOffset = (localY + y) * srcStride + localX * 4;
            int dstOffset = (outY + y) * destStride + outX * 4;

            if (srcOffset >= 0 && dstOffset >= 0 && 
                srcOffset + copyBytes <= static_cast<int>(mframe.bgra8.size()) && 
                dstOffset + copyBytes <= static_cast<int>(outBgra8.size())) {
                std::memcpy(outBgra8.data() + dstOffset, mframe.bgra8.data() + srcOffset, copyBytes);
            }
        }
    }
}
