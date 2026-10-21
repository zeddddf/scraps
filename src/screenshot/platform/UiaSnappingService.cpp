#include "platform/UiaSnappingService.h"
#include <dwmapi.h>
#include <algorithm>
#include <chrono>
#include <thread>
#include <cmath>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "dwmapi.lib")

// ─────────────────────────────────────────────────────────────────────────────
//  File-scope helpers & enum callbacks
// ─────────────────────────────────────────────────────────────────────────────
namespace {

double GetMonitorDpiScale(HMONITOR hMonitor) {
    UINT dpiX = 96, dpiY = 96;
    HMODULE hShcore = GetModuleHandleW(L"shcore.dll");
    if (hShcore) {
        typedef HRESULT(WINAPI* LPFNGETDPIFORMONITOR)(HMONITOR, int, UINT*, UINT*);
        auto pfnGetDpiForMonitor = reinterpret_cast<LPFNGETDPIFORMONITOR>(GetProcAddress(hShcore, "GetDpiForMonitor"));
        if (pfnGetDpiForMonitor) {
            pfnGetDpiForMonitor(hMonitor, 0, &dpiX, &dpiY);
        }
    }
    return dpiX / 96.0;
}

double GetMonitorDpiScaleByHwnd(HWND hwnd) {
    HMONITOR hMonitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    return GetMonitorDpiScale(hMonitor);
}

double GetMonitorDpiScaleByPoint(POINT pt) {
    HMONITOR hMonitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    return GetMonitorDpiScale(hMonitor);
}

#ifndef LVM_FIRST
#define LVM_FIRST 0x1000
#endif
#ifndef LVM_GETITEMCOUNT
#define LVM_GETITEMCOUNT (LVM_FIRST + 4)
#endif
#ifndef LVM_GETITEMRECT
#define LVM_GETITEMRECT (LVM_FIRST + 14)
#endif
#ifndef LVIR_BOUNDS
#define LVIR_BOUNDS 0
#endif

bool IsDesktopShell(HWND hwnd) {
    WCHAR cls[64] = {};
    GetClassNameW(hwnd, cls, 64);
    return wcscmp(cls, L"Progman") == 0 || wcscmp(cls, L"WorkerW") == 0;
}

HWND FindDesktopListView() {
    HWND hwndParent = FindWindowW(L"Progman", L"Program Manager");
    HWND hwndShell = FindWindowExW(hwndParent, NULL, L"SHELLDLL_DefView", NULL);
    HWND hwndLV = FindWindowExW(hwndShell, NULL, L"SysListView32", NULL);
    if (!hwndLV) {
        HWND hwndWorker = NULL;
        while ((hwndWorker = FindWindowExW(NULL, hwndWorker, L"WorkerW", NULL)) != NULL) {
            HWND hwndDefView = FindWindowExW(hwndWorker, NULL, L"SHELLDLL_DefView", NULL);
            if (hwndDefView) {
                hwndLV = FindWindowExW(hwndDefView, NULL, L"SysListView32", NULL);
                break;
            }
        }
    }
    return hwndLV;
}

bool IsToplevelCapturable(HWND hwnd, const std::wstring& excludeClass) {
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) return false;

    WCHAR cls[256] = {};
    GetClassNameW(hwnd, cls, 256);
    std::wstring c = cls;

    if (!excludeClass.empty() && c == excludeClass) return false;
    if (c == L"EdgeUiInputTopWnd" || c == L"DummyDWMWindow") return false;
    // Progman/WorkerW handled separately — excluded from normal window list
    if (c == L"Progman" || c == L"WorkerW") return false;

    LONG exStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
    if (exStyle & WS_EX_LAYERED) {
        COLORREF k; BYTE a; DWORD f;
        if (GetLayeredWindowAttributes(hwnd, &k, &a, &f))
            if ((f & LWA_ALPHA) && a == 0) return false;
    }

    int cloaked = 0;
    DwmGetWindowAttribute(hwnd, 14 /* DWMWA_CLOAKED */, &cloaked, sizeof(cloaked));
    if (cloaked) return false;

    // Skip untitled pure tool-windows (IME, tooltip, etc.)
    if (GetWindowTextLength(hwnd) == 0 && (exStyle & WS_EX_TOOLWINDOW)) return false;
    return true;
}

// ── Top-level window enum ────────────────────────────────────────────────────
struct TlEnumCtx {
    std::vector<HWND>   hwnds;         // normal capturable windows
    std::vector<HWND>   desktopHwnds;  // Progman / WorkerW shells
    const std::wstring* excludeClass;
};
BOOL CALLBACK TlEnumProc(HWND hwnd, LPARAM lp) {
    auto* ctx = reinterpret_cast<TlEnumCtx*>(lp);
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) return TRUE;
    if (IsDesktopShell(hwnd))
        ctx->desktopHwnds.push_back(hwnd);
    else if (IsToplevelCapturable(hwnd, *ctx->excludeClass))
        ctx->hwnds.push_back(hwnd);
    return TRUE;
}

// ── Child window enum ────────────────────────────────────────────────────────
struct ChildEnumCtx {
    HWND                                topLevel;
    UiaSnappingService*                 svc;
    int*                                totalCount;
    std::vector<UiaSnappingService::SnapRect>* out;
    HRGN                                processedRgn; // GDI mask from SnapshotWorker
    int                                 zOrder;
    int                                 localZOrderCounter;
};
BOOL CALLBACK ChildEnumProc(HWND child, LPARAM lp) {
    auto* ctx = reinterpret_cast<ChildEnumCtx*>(lp);
    if (!ctx->svc->IsSnapshotRunning() ||
        *ctx->totalCount >= UiaSnappingService::kMaxTotal)
        return FALSE;

    if (!IsWindowVisible(child)) return TRUE;

    RECT r;
    if (!GetWindowRect(child, &r)) return TRUE;
    int w = r.right - r.left;
    int h = r.bottom - r.top;
    if (w < UiaSnappingService::kMinSizePx ||
        h < UiaSnappingService::kMinSizePx) return TRUE;

    // Occlusion check
    if (ctx->processedRgn) {
        HRGN childRgn = CreateRectRgn(r.left, r.top, r.right, r.bottom);
        HRGN childVisible = CreateRectRgn(0, 0, 0, 0);
        int rgnType = CombineRgn(childVisible, childRgn, ctx->processedRgn, RGN_DIFF);
        DeleteObject(childRgn);
        DeleteObject(childVisible);
        if (rgnType == NULLREGION) {
            // Child control is 100% occluded by windows above its parent — skip it!
            return TRUE;
        }
    }

    // Compute depth by walking GetParent() chain up to topLevel
    int depth = 0;
    HWND cur = child;
    while (cur && cur != ctx->topLevel && depth < UiaSnappingService::kMaxDepth) {
        cur = GetParent(cur);
        ++depth;
    }

    int localZOrder = ctx->localZOrderCounter++;
    ctx->out->push_back({ {r.left, r.top, w, h}, depth, ctx->zOrder, localZOrder, child });
    ++(*ctx->totalCount);
    return TRUE;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
//  Lifecycle
// ─────────────────────────────────────────────────────────────────────────────
UiaSnappingService::UiaSnappingService() {}

UiaSnappingService::~UiaSnappingService() {
    StopCrawl();
    CancelDwellQuery();
    if (dwellThread_.joinable()) dwellThread_.detach();
}

void UiaSnappingService::StartCrawl() {
    StopCrawl();
    snapshotReady_   = false;
    snapshotRunning_ = true;
    snapshotThread_  = std::thread(&UiaSnappingService::SnapshotWorker, this);
}

void UiaSnappingService::StopCrawl() {
    snapshotRunning_ = false;
    if (snapshotThread_.joinable()) snapshotThread_.join();
    snapshotReady_ = false;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Phase 1 — EnumChildWindows snapshot (background thread A)
// ─────────────────────────────────────────────────────────────────────────────
void UiaSnappingService::SnapshotWorker() {
    // Initialize COM for UIA warmup
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // ── enumerate top-level windows ──────────────────────────────────────────
    TlEnumCtx tlCtx;
    tlCtx.excludeClass = &overlayClass_;
    EnumWindows(TlEnumProc, reinterpret_cast<LPARAM>(&tlCtx));

    std::vector<SnapRect> snap;
    snap.reserve(512);
    int totalCount = 0;

    // GDI Z-order occlusion region mask
    HRGN processedRgn = CreateRectRgn(0, 0, 0, 0);

    std::vector<TopLevelWindow> tlWindows;

    for (size_t z = 0; z < tlCtx.hwnds.size(); ++z) {
        HWND hwnd = tlCtx.hwnds[z];
        int zOrder = static_cast<int>(z);
        if (!snapshotRunning_ || totalCount >= kMaxTotal) break;

        // Warm up Chromium/Firefox accessibility tree and bulk-cache elements instantly
        WCHAR clsName[128] = {};
        if (GetClassNameW(hwnd, clsName, 128)) {
            if (wcscmp(clsName, L"Chrome_WidgetWin_1") == 0 || wcscmp(clsName, L"MozillaWindowClass") == 0) {
                Microsoft::WRL::ComPtr<IUIAutomation> uia;
                if (SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&uia)))) {
                    Microsoft::WRL::ComPtr<IUIAutomationElement> rootEl;
                    if (SUCCEEDED(uia->ElementFromHandle(hwnd, &rootEl)) && rootEl) {
                        
                        Microsoft::WRL::ComPtr<IUIAutomationCacheRequest> cacheReq;
                        if (SUCCEEDED(uia->CreateCacheRequest(&cacheReq))) {
                            cacheReq->AddProperty(UIA_BoundingRectanglePropertyId);
                            cacheReq->AddProperty(UIA_IsOffscreenPropertyId);
                            // Use UIA_IsControlElementPropertyId to capture structure layout containers
                            // (e.g. divs, panels, card sections) along with content components (buttons, links, text)
                            cacheReq->AddProperty(UIA_IsControlElementPropertyId);

                            VARIANT varTrue; VariantInit(&varTrue); varTrue.vt = VT_BOOL; varTrue.boolVal = VARIANT_TRUE;
                            VARIANT varFalse; VariantInit(&varFalse); varFalse.vt = VT_BOOL; varFalse.boolVal = VARIANT_FALSE;

                            Microsoft::WRL::ComPtr<IUIAutomationCondition> isControlCond;
                            uia->CreatePropertyCondition(UIA_IsControlElementPropertyId, varTrue, &isControlCond);
                            
                            Microsoft::WRL::ComPtr<IUIAutomationCondition> isVisibleCond;
                            uia->CreatePropertyCondition(UIA_IsOffscreenPropertyId, varFalse, &isVisibleCond);
                            
                            Microsoft::WRL::ComPtr<IUIAutomationCondition> combinedCond;
                            uia->CreateAndCondition(isControlCond.Get(), isVisibleCond.Get(), &combinedCond);

                            Microsoft::WRL::ComPtr<IUIAutomationElementArray> elements;
                            if (SUCCEEDED(rootEl->FindAllBuildCache(TreeScope_Descendants, combinedCond.Get(), cacheReq.Get(), &elements)) && elements) {
                                int length = 0;
                                elements->get_Length(&length);
                                double dpiScale = GetMonitorDpiScaleByHwnd(hwnd);

                                for (int i = 0; i < length; ++i) {
                                    if (!snapshotRunning_ || totalCount >= kMaxTotal) break;

                                    Microsoft::WRL::ComPtr<IUIAutomationElement> el;
                                    if (SUCCEEDED(elements->GetElement(i, &el)) && el) {
                                        RECT rect = {0};
                                        el->get_CachedBoundingRectangle(&rect);
                                        
                                        int w = rect.right - rect.left;
                                        int h = rect.bottom - rect.top;
                                        
                                        if (w >= 10 && h >= 10) {
                                             int pxLeft = static_cast<int>(std::round(rect.left * dpiScale));
                                             int pxTop = static_cast<int>(std::round(rect.top * dpiScale));
                                             int pxW = static_cast<int>(std::round(w * dpiScale));
                                             int pxH = static_cast<int>(std::round(h * dpiScale));
                                             
                                             // Depth 99 to ensure it's deeper than the browser window, Z-order same as browser
                                             snap.push_back({ {pxLeft, pxTop, pxW, pxH}, 99, zOrder, i, hwnd });
                                            ++totalCount;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // Top-level window entry at depth 0.
        // Primary: DWM extended frame (excludes invisible drop-shadows).
        // Fallback: plain GetWindowRect (works for WPF / off-DWM / elevated windows).
        RECT frame;
        bool gotRect = SUCCEEDED(DwmGetWindowAttribute(
            hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame)));
        if (!gotRect)
            gotRect = !!GetWindowRect(hwnd, &frame);
        if (gotRect) {
            int w = frame.right - frame.left;
            int h = frame.bottom - frame.top;
            if (w >= 10 && h >= 10) {
                // GDI Z-culling check
                HRGN winRgn = CreateRectRgn(frame.left, frame.top, frame.right, frame.bottom);
                HRGN visiblePart = CreateRectRgn(0, 0, 0, 0);
                int rgnType = CombineRgn(visiblePart, winRgn, processedRgn, RGN_DIFF);
                DeleteObject(visiblePart);

                if (rgnType == NULLREGION) {
                    // Window is 100% occluded by windows above it in Z-order — skip it and all child controls!
                    DeleteObject(winRgn);
                    continue;
                }

                // Add to top-level Z-ordered windows list
                tlWindows.push_back({ {frame.left, frame.top, w, h}, zOrder, hwnd });

                // Add to snapshot (top-level Z-order, depth 0, localZOrder 0)
                snap.push_back({ {frame.left, frame.top, w, h}, 0, zOrder, 0, hwnd });
                ++totalCount;

                // ── Enumerate child controls FIRST, using active mask of windows ABOVE us ──
                ChildEnumCtx cCtx;
                cCtx.topLevel   = hwnd;
                cCtx.svc        = this;
                cCtx.totalCount = &totalCount;
                cCtx.out        = &snap;
                cCtx.processedRgn = processedRgn; // Checked against windows ABOVE us, not containing parent itself!
                cCtx.zOrder     = zOrder;
                cCtx.localZOrderCounter = 1; // child local Z-orders start at 1
                EnumChildWindows(hwnd, ChildEnumProc, reinterpret_cast<LPARAM>(&cCtx));

                // ── NOW merge the parent's region into processedRgn for windows below us ──
                CombineRgn(processedRgn, processedRgn, winRgn, RGN_OR);
                DeleteObject(winRgn);
            }
        }
    }

    // Direct Explorer desktop ListView virtual memory enumeration for icons
    if (snapshotRunning_ && totalCount < kMaxTotal) {
        HWND hwndLV = FindDesktopListView();
        if (hwndLV) {
            int count = static_cast<int>(SendMessageW(hwndLV, LVM_GETITEMCOUNT, 0, 0));
            if (count > 0) {
                DWORD procId = 0;
                GetWindowThreadProcessId(hwndLV, &procId);
                if (procId != 0) {
                    HANDLE hProc = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE, FALSE, procId);
                    if (hProc) {
                        RECT* pRemoteRect = reinterpret_cast<RECT*>(VirtualAllocEx(hProc, NULL, sizeof(RECT), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
                        if (pRemoteRect) {
                            POINT screenOffset = {0, 0};
                            ClientToScreen(hwndLV, &screenOffset);

                            for (int i = 0; i < count; ++i) {
                                if (!snapshotRunning_ || totalCount >= kMaxTotal) break;

                                RECT rLocal = { LVIR_BOUNDS, 0, 0, 0 };
                                if (!WriteProcessMemory(hProc, pRemoteRect, &rLocal, sizeof(RECT), NULL)) continue;

                                if (SendMessageW(hwndLV, LVM_GETITEMRECT, i, reinterpret_cast<LPARAM>(pRemoteRect))) {
                                    if (ReadProcessMemory(hProc, pRemoteRect, &rLocal, sizeof(RECT), NULL)) {
                                        int x = rLocal.left + screenOffset.x;
                                        int y = rLocal.top + screenOffset.y;
                                        int w = rLocal.right - rLocal.left;
                                        int h = rLocal.bottom - rLocal.top;

                                        if (w >= 10 && h >= 10) {
                                            // Desktop icons are child items, Z-order 9999 (lowest), localZOrder i
                                            snap.push_back({ {x, y, w, h}, 1, 9999, i, hwndLV });
                                            ++totalCount;
                                        }
                                    }
                                }
                            }
                            VirtualFreeEx(hProc, pRemoteRect, 0, MEM_RELEASE);
                        }
                        CloseHandle(hProc);
                    }
                }
            }
        }
    }

    // Clean up GDI region
    if (processedRgn) {
        DeleteObject(processedRgn);
    }

    CoUninitialize();

    if (!snapshotRunning_) return;

    // 1. First sort by spatial coordinates to group duplicate rectangles together for physical deduplication
    std::sort(snap.begin(), snap.end(), [](const SnapRect& a, const SnapRect& b) {
        if (a.rect.x != b.rect.x) return a.rect.x < b.rect.x;
        if (a.rect.y != b.rect.y) return a.rect.y < b.rect.y;
        if (a.rect.width != b.rect.width) return a.rect.width < b.rect.width;
        return a.rect.height < b.rect.height;
    });

    // Deduplicate identical rects (same bounding rectangle appearing via different enumeration paths)
    snap.erase(
        std::unique(snap.begin(), snap.end(), [](const SnapRect& a, const SnapRect& b) {
            return a.rect.x == b.rect.x && a.rect.y == b.rect.y &&
                   a.rect.width == b.rect.width && a.rect.height == b.rect.height;
        }),
        snap.end());

    // 2. Now perform the final ranking sort:
    // - zOrder ascending (topmost window first to ensure Z-order correctness)
    // - area ascending (smaller area first to prioritize fine-grained leaf child controls).
    //   Note: Under UIA (especially inside web browsers like Chrome/Firefox), all DOM elements
    //   often get a flat depth (e.g. 99). Area-based sorting naturally restores the visual parent-child
    //   hierarchy, since any nested child element must geometrically have a smaller or equal area than its parent.
    // - depth descending (child control first, acting as a tie-breaker for identical-area nesting)
    // - localZOrder ascending (topmost child first as a final tie-breaker)
    std::sort(snap.begin(), snap.end(), [](const SnapRect& a, const SnapRect& b) {
        if (a.zOrder != b.zOrder) return a.zOrder < b.zOrder;
        
        double areaA = static_cast<double>(a.rect.width) * a.rect.height;
        double areaB = static_cast<double>(b.rect.width) * b.rect.height;
        if (areaA != areaB) {
            return areaA < areaB;
        }

        if (a.depth != b.depth) return a.depth > b.depth;
        return a.localZOrder < b.localZOrder;
    });

    {
        std::lock_guard<std::mutex> lk(snapshotMutex_);
        snapshot_ = std::move(snap);
        topLevelWindows_ = std::move(tlWindows);
    }
    snapshotReady_ = true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Fast hit-test (called on every WM_MOUSEMOVE — pure memory)
// ─────────────────────────────────────────────────────────────────────────────
std::optional<RectPx> UiaSnappingService::HitTestControl(const PointPx& pt) const {
    if (!snapshotReady_) return std::nullopt;
    std::lock_guard<std::mutex> lk(snapshotMutex_);
    
    for (const auto& s : snapshot_) {
        if (s.rect.width >= 8 && s.rect.height >= 8 && s.rect.contains(pt)) {
            // Check if this candidate is occluded by any topmost window above its Z-order
            bool isOccluded = false;
            for (const auto& tlW : topLevelWindows_) {
                if (tlW.zOrder < s.zOrder) {
                    if (tlW.rect.contains(pt)) {
                        isOccluded = true;
                        break;
                    }
                }
            }
            if (!isOccluded) {
                return s.rect;
            }
        }
    }
    return std::nullopt;
}

// Returns ALL candidates for Tab cycling (same sort order as snapshot)
std::vector<RectPx> UiaSnappingService::HitTestAll(const PointPx& pt) const {
    std::vector<RectPx> result;
    if (!snapshotReady_) return result;
    std::lock_guard<std::mutex> lk(snapshotMutex_);
    
    for (const auto& s : snapshot_) {
        if (s.rect.width >= 8 && s.rect.height >= 8 && s.rect.contains(pt)) {
            // Check if this candidate is occluded by any topmost window above its Z-order
            bool isOccluded = false;
            for (const auto& tlW : topLevelWindows_) {
                if (tlW.zOrder < s.zOrder) {
                    if (tlW.rect.contains(pt)) {
                        isOccluded = true;
                        break;
                    }
                }
            }
            if (!isOccluded) {
                result.push_back(s.rect);
            }
        }
    }
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Phase 2 — UIA ElementFromPoint dwell query (background thread B)
// ─────────────────────────────────────────────────────────────────────────────
void UiaSnappingService::QueryDwellAsync(const PointPx& pt) {
    int myId = ++dwellQueryId_;
    if (dwellThread_.joinable()) dwellThread_.detach(); // let previous finish, it'll self-discard
    dwellThread_ = std::thread(&UiaSnappingService::DwellWorker, this, pt, myId);
}

void UiaSnappingService::CancelDwellQuery() {
    ++dwellQueryId_; // any in-flight worker will see ID mismatch and discard its result
}

void UiaSnappingService::DwellWorker(PointPx pt, int queryId) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    Microsoft::WRL::ComPtr<IUIAutomation> uia;
    HRESULT hr = CoCreateInstance(CLSID_CUIAutomation, nullptr,
                                  CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&uia));
    if (FAILED(hr) || dwellQueryId_.load() != queryId) { CoUninitialize(); return; }

    POINT sp = { pt.x, pt.y };
    Microsoft::WRL::ComPtr<IUIAutomationElement> el;
    hr = uia->ElementFromPoint(sp, &el);   // cross-process, may take 10–200 ms

    if (FAILED(hr) || !el || dwellQueryId_.load() != queryId) { CoUninitialize(); return; }

    RECT rect;
    if (SUCCEEDED(el->get_CurrentBoundingRectangle(&rect))) {
        int w = rect.right - rect.left;
        int h = rect.bottom - rect.top;

        if (w > 4 && h > 4 && dwellQueryId_.load() == queryId) {
            double dpiScale = GetMonitorDpiScaleByPoint(sp);

            int cx = rect.left + w / 2;
            int cy = rect.top  + h / 2;
            int hw = w * 3 / 4;   // half-width  expanded by 50 %
            int hh = h * 3 / 4;   // half-height expanded by 50 %

            // Containment check in physical space
            bool nearRectPhysical = (pt.x >= cx - hw && pt.x < cx + hw &&
                                     pt.y >= cy - hh && pt.y < cy + hh);

            // Containment check in logical space (scaling logical UIA bounds to physical space)
            int cxPhys = static_cast<int>(std::round(cx * dpiScale));
            int cyPhys = static_cast<int>(std::round(cy * dpiScale));
            int hwPhys = static_cast<int>(std::round(hw * dpiScale));
            int hhPhys = static_cast<int>(std::round(hh * dpiScale));
            bool nearRectLogical = (pt.x >= cxPhys - hwPhys && pt.x < cxPhys + hwPhys &&
                                    pt.y >= cyPhys - hhPhys && pt.y < cyPhys + hhPhys);

            bool nearRect = nearRectPhysical || nearRectLogical;
            if (!nearRect) {
                // Cursor is far from UIA rect in both coordinate systems — discard.
                CoUninitialize();
                return;
            }

            // If coordinates were logical, scale the rect to physical screen space for precise highlighting
            RECT finalRect = rect;
            if (nearRectLogical && !nearRectPhysical) {
                finalRect.left   = static_cast<long>(std::round(rect.left   * dpiScale));
                finalRect.top    = static_cast<long>(std::round(rect.top    * dpiScale));
                finalRect.right  = static_cast<long>(std::round(std::max(0.0, rect.right  * dpiScale)));
                finalRect.bottom = static_cast<long>(std::round(std::max(0.0, rect.bottom * dpiScale)));
                w = finalRect.right - finalRect.left;
                h = finalRect.bottom - finalRect.top;
            }

            // ── Precision check ────────────────────────────────────────────
            auto current = HitTestControl(pt);
            bool morePrecise = !current.has_value() ||
                               (w * h) < (current->width * current->height);

            if (morePrecise && notifyHwnd_ && dwellQueryId_.load() == queryId) {
                auto* res = new RectPx{ finalRect.left, finalRect.top, w, h };
                PostMessage(notifyHwnd_, WM_SNAP_UIA_RESULT,
                            reinterpret_cast<WPARAM>(res), 0);
            }
        }
    }

    CoUninitialize();
}
