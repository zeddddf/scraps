#pragma once

#include <windows.h>
#include <objbase.h>
#include <uiautomationclient.h>
#include <wrl/client.h>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <optional>
#include <string>
#include "platform/MonitorTopology.h" // RectPx / PointPx

// Posted to notifyHwnd when a UIA dwell query finds a more-precise element.
// WPARAM = heap-allocated RectPx* (main thread must delete).
// LPARAM = 0.
#define WM_SNAP_UIA_RESULT  (WM_APP + 42)

class UiaSnappingService {
public:
    struct SnapRect {
        RectPx rect;
        int    depth;       // 0 = top-level window, 1+ = child controls
        int    zOrder;      // Global Z-order Z-index of top-level window
        int    localZOrder; // Local Z-order index of child control within window
        HWND   hwnd;
    };

    struct TopLevelWindow {
        RectPx rect;
        int    zOrder;
        HWND   hwnd;
    };

    // ── public limits (used by enum callbacks) ──────────────────────────────
    static constexpr int kMaxDepth     = 16;
    static constexpr int kMaxTotal     = 3000;
    static constexpr int kMaxPerWindow = 500;
    static constexpr int kMinSizePx    = 4;

    UiaSnappingService();
    ~UiaSnappingService();

    // Configure before StartCrawl
    void SetOverlayClass(const wchar_t* cls) { overlayClass_ = cls; }
    void SetNotifyHwnd(HWND hwnd)            { notifyHwnd_   = hwnd; }

    // Phase 1: build snapshot on background thread A
    void StartCrawl();
    void StopCrawl();

    // Fast O(N) hit: returns the deepest/smallest-area element at pt
    std::optional<RectPx> HitTestControl(const PointPx& pt) const;

    // Returns ALL candidates at pt (depth desc, area asc) for Tab cycling
    std::vector<RectPx> HitTestAll(const PointPx& pt) const;

    // Phase 2: async UIA ElementFromPoint dwell query
    void QueryDwellAsync(const PointPx& pt);
    void CancelDwellQuery();

    // Used by enum callbacks to check cancellation
    bool IsSnapshotRunning() const { return snapshotRunning_.load(); }

private:
    void SnapshotWorker();
    void DwellWorker(PointPx pt, int queryId);

    std::wstring overlayClass_;
    HWND         notifyHwnd_ = nullptr;

    // Snapshot state
    std::vector<SnapRect>       snapshot_;
    std::vector<TopLevelWindow> topLevelWindows_;
    mutable std::mutex          snapshotMutex_;
    std::atomic<bool>      snapshotReady_{false};
    std::thread            snapshotThread_;
    std::atomic<bool>      snapshotRunning_{false};

    // Dwell UIA query state
    std::thread      dwellThread_;
    std::atomic<int> dwellQueryId_{0};

    // UI-thread UIA instance kept alive for dwell worker (COM STA)
    Microsoft::WRL::ComPtr<IUIAutomation> staUia_;
};
