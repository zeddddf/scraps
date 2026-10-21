#include "overlay/OverlayWindowManager.h"
#include "app/ScreenshotSession.h"
#include "overlay/OverlayRenderer.h"
#include "platform/UiaSnappingService.h"
#include <d2d1helper.h>
#include <windowsx.h>
#include <algorithm>

namespace {

void RGBToHSL(int r, int g, int b, int& h, int& s, int& l) {
    float rf = r / 255.0f;
    float gf = g / 255.0f;
    float bf = b / 255.0f;

    float maxVal = std::max({rf, gf, bf});
    float minVal = std::min({rf, gf, bf});
    float diff = maxVal - minVal;

    float hf = 0.0f;
    float sf = 0.0f;
    float lf = (maxVal + minVal) / 2.0f;

    if (diff > 0.0f) {
        if (lf < 0.5f) {
            sf = diff / (maxVal + minVal);
        } else {
            sf = diff / (2.0f - maxVal - minVal);
        }

        if (maxVal == rf) {
            hf = (gf - bf) / diff + (gf < bf ? 6.0f : 0.0f);
        } else if (maxVal == gf) {
            hf = (bf - rf) / diff + 2.0f;
        } else {
            hf = (rf - gf) / diff + 4.0f;
        }
        hf /= 6.0f;
    }

    h = static_cast<int>(hf * 360.0f + 0.5f);
    s = static_cast<int>(sf * 100.0f + 0.5f);
    l = static_cast<int>(lf * 100.0f + 0.5f);
}

void RGBToHSV(int r, int g, int b, int& h, int& s, int& v) {
    float rf = r / 255.0f;
    float gf = g / 255.0f;
    float bf = b / 255.0f;

    float maxVal = std::max({rf, gf, bf});
    float minVal = std::min({rf, gf, bf});
    float diff = maxVal - minVal;

    float hf = 0.0f;
    float sf = maxVal == 0.0f ? 0.0f : diff / maxVal;
    float vf = maxVal;

    if (diff > 0.0f) {
        if (maxVal == rf) {
            hf = (gf - bf) / diff + (gf < bf ? 6.0f : 0.0f);
        } else if (maxVal == gf) {
            hf = (bf - rf) / diff + 2.0f;
        } else {
            hf = (rf - gf) / diff + 4.0f;
        }
        hf /= 6.0f;
    }

    h = static_cast<int>(hf * 360.0f + 0.5f);
    s = static_cast<int>(sf * 100.0f + 0.5f);
    v = static_cast<int>(vf * 100.0f + 0.5f);
}

} // namespace

bool OverlayWindowManager::classRegistered_ = false;

OverlayWindowManager::OverlayWindowManager(ScreenshotSession* session) : session_(session) {}

OverlayWindowManager::~OverlayWindowManager() {
    Destroy();
}

void OverlayWindowManager::RegisterWindowClass() {
    if (classRegistered_) return;
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_CROSS);
    wc.lpszClassName = L"ScrapsScreenshotOverlay";
    RegisterClassExW(&wc);
    classRegistered_ = true;
}

bool OverlayWindowManager::CreateWindows(const std::vector<MonitorSnapshot>& monitors) {
    bool topologyChanged = false;
    if (windows_.size() != monitors.size()) {
        topologyChanged = true;
    } else {
        for (size_t i = 0; i < windows_.size(); ++i) {
            if (windows_[i].monitor.hMonitor != monitors[i].hMonitor ||
                windows_[i].monitor.virtualBounds.x != monitors[i].virtualBounds.x ||
                windows_[i].monitor.virtualBounds.y != monitors[i].virtualBounds.y ||
                windows_[i].monitor.virtualBounds.width != monitors[i].virtualBounds.width ||
                windows_[i].monitor.virtualBounds.height != monitors[i].virtualBounds.height) {
                topologyChanged = true;
                break;
            }
        }
    }

    if (topologyChanged) {
        Destroy();
    } else if (!windows_.empty()) {
        return true;
    }

    RegisterWindowClass();
    for (const auto& monitor : monitors) {
        OverlayWindow ow;
        ow.monitor = monitor;
        
        ow.hwnd = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP,
            L"ScrapsScreenshotOverlay",
            L"ScreenshotOverlay",
            WS_POPUP,
            monitor.virtualBounds.x, monitor.virtualBounds.y,
            monitor.virtualBounds.width, monitor.virtualBounds.height,
            nullptr, nullptr, GetModuleHandle(nullptr), this
        );

        if (!ow.hwnd) return false;

        ow.dcomp = std::make_unique<DCompDeviceResources>();
        if (!ow.dcomp->Initialize(ow.hwnd)) {
            // DComp initialization failed — destroy the orphaned HWND and bail out
            DestroyWindow(ow.hwnd);
            return false;
        }

        windows_.push_back(std::move(ow));
    }

    // Phase 2: register the first overlay HWND so dwell UIA results are posted here
    if (!windows_.empty() && session_) {
        session_->GetStateMachine().GetUiaSnapping().SetNotifyHwnd(windows_[0].hwnd);
        SetTimer(windows_[0].hwnd, 2 /* TIMER_CARET */, 500, nullptr);
    }

    return true;
}

void OverlayWindowManager::Show() {
    // Initialize lastCursorPos_ to the current physical cursor position
    POINT pt;
    if (GetCursorPos(&pt)) {
        lastCursorPos_ = { pt.x, pt.y };
    } else {
        lastCursorPos_ = { -9999, -9999 };
    }

    // Set the initial hovered window rect in stateMachine to the window containing this point
    if (session_) {
        session_->GetStateMachine().InitializeInitialHoverRect(lastCursorPos_);
    }

    for (auto& ow : windows_) {
        ShowWindow(ow.hwnd, SW_SHOW);
        UpdateWindow(ow.hwnd);
    }
    
    if (!windows_.empty()) {
        HWND primary = windows_[0].hwnd;
        SetForegroundWindow(primary);
        SetFocus(primary);
    }
}

void OverlayWindowManager::Hide() {
    for (auto& ow : windows_) {
        ShowWindow(ow.hwnd, SW_HIDE);
    }
}

void OverlayWindowManager::Destroy() {
    // Release DComp resources BEFORE destroying the HWND.
    // IDCompositionTarget holds a reference to the HWND; destroying the window
    // first while DComp resources are still alive would leave dangling references.
    for (auto& ow : windows_) {
        ow.dcomp.reset();  // Release DComp target/visual/swapchain first
    }
    for (auto& ow : windows_) {
        if (ow.hwnd) {
            DestroyWindow(ow.hwnd);
            ow.hwnd = nullptr;
        }
    }
    windows_.clear();
}



void OverlayWindowManager::InvalidateAll() {
    UiState dummyUi;
    auto& sm = session_->GetStateMachine();
    dummyUi.selectionModel = &sm.GetSelection();
    dummyUi.hoveredWindowRect = sm.GetHoveredWindowRect();
    dummyUi.document = &sm.GetDocument();
    dummyUi.toolbar = &sm.GetToolbar();
    dummyUi.currentDraft = sm.GetCurrentDraft();
    dummyUi.isAltPressed = sm.IsAltPressed();
    dummyUi.colorFormat = sm.GetColorFormat();
    
    // Windowless Text Editor State
    dummyUi.isEditingText = sm.IsEditingText();
    dummyUi.activeText = sm.GetActiveText();
    dummyUi.imeCompositionString = sm.GetImeCompositionString();
    dummyUi.caretIndex = sm.GetCaretIndex();
    dummyUi.textRect = sm.GetTextRect();
    dummyUi.caretVisible = sm.IsCaretVisible();
    dummyUi.textColor = sm.GetContext().color;
    dummyUi.fontSize = sm.GetContext().fontSize;
    dummyUi.isBold = sm.GetContext().isBold;
    dummyUi.isItalic = sm.GetContext().isItalic;
    dummyUi.selectionAnchor = sm.GetSelectionAnchor();
    dummyUi.selectionActive = sm.GetSelectionActive();
    dummyUi.textLayout = sm.GetActiveTextLayout();
    
    dummyUi.cursorPos = lastCursorPos_;

    RectPx currentRoi = dummyUi.selectionModel->GetCurrentRoi();
    bool hasSelection = dummyUi.selectionModel->HasSelection();

    auto intersects = [](const RectPx& r1, const RectPx& r2) -> bool {
        int x1 = std::max(r1.x, r2.x);
        int y1 = std::max(r1.y, r2.y);
        int x2 = std::min(r1.x + r1.width, r2.x + r2.width);
        int y2 = std::min(r1.y + r1.height, r2.y + r2.height);
        return (x1 < x2 && y1 < y2);
    };

    auto cursorOnMonitor = [](const PointPx& p, const MonitorSnapshot& monitor) -> bool {
        return (p.x >= monitor.virtualBounds.x && p.x < monitor.virtualBounds.x + monitor.virtualBounds.width &&
                p.y >= monitor.virtualBounds.y && p.y < monitor.virtualBounds.y + monitor.virtualBounds.height);
    };

    for (auto& ow : windows_) {
        bool needRepaint = false;

        // 1. If cursor is on this monitor
        if (cursorOnMonitor(dummyUi.cursorPos, ow.monitor)) {
            needRepaint = true;
        }
        
        // 2. If current selection intersects this monitor
        if (hasSelection && intersects(currentRoi, ow.monitor.virtualBounds)) {
            needRepaint = true;
        }

        // 3. If previous selection intersected this monitor
        if (ow.lastHasSelection && intersects(ow.lastSelectionRect, ow.monitor.virtualBounds)) {
            needRepaint = true;
        }

        // 4. If current hover intersects this monitor
        if (dummyUi.hoveredWindowRect && intersects(*dummyUi.hoveredWindowRect, ow.monitor.virtualBounds)) {
            needRepaint = true;
        }

        // 5. If previous hover intersected this monitor
        if (ow.lastHoveredRect && intersects(*ow.lastHoveredRect, ow.monitor.virtualBounds)) {
            needRepaint = true;
        }

        if (needRepaint) {
            RenderWindow(ow);
            ValidateRect(ow.hwnd, nullptr);
        }

        // Update cache
        ow.lastSelectionRect = currentRoi;
        ow.lastHasSelection = hasSelection;
        ow.lastHoveredRect = dummyUi.hoveredWindowRect;
    }
}

void OverlayWindowManager::RenderWindow(OverlayWindow& ow) {
    if (!ow.dcomp) return;

    UiState dummyUi;
    auto& sm = session_->GetStateMachine();
    dummyUi.selectionModel = &sm.GetSelection();
    dummyUi.hoveredWindowRect = sm.GetHoveredWindowRect();
    dummyUi.document = &sm.GetDocument();
    dummyUi.toolbar = &sm.GetToolbar();
    dummyUi.currentDraft = sm.GetCurrentDraft();
    dummyUi.isAltPressed = sm.IsAltPressed();
    dummyUi.colorFormat = sm.GetColorFormat();
    
    // Windowless Text Editor State
    dummyUi.isEditingText = sm.IsEditingText();
    dummyUi.activeText = sm.GetActiveText();
    dummyUi.imeCompositionString = sm.GetImeCompositionString();
    dummyUi.caretIndex = sm.GetCaretIndex();
    dummyUi.textRect = sm.GetTextRect();
    dummyUi.caretVisible = sm.IsCaretVisible();
    dummyUi.textColor = sm.GetContext().color;
    dummyUi.fontSize = sm.GetContext().fontSize;
    dummyUi.isBold = sm.GetContext().isBold;
    dummyUi.isItalic = sm.GetContext().isItalic;
    dummyUi.selectionAnchor = sm.GetSelectionAnchor();
    dummyUi.selectionActive = sm.GetSelectionActive();
    dummyUi.textLayout = sm.GetActiveTextLayout();
    
    dummyUi.cursorPos = lastCursorPos_;

    const MonitorFrame* targetFrame = nullptr;
    for (const auto& mf : session_->GetDesktopFrame().monitors) {
        if (mf.monitor.hMonitor == ow.monitor.hMonitor) {
            targetFrame = &mf;
            break;
        }
    }

    ID2D1Bitmap1* bgBitmap = nullptr;
    if (targetFrame && !targetFrame->bgra8.empty()) {
        bgBitmap = ow.dcomp->GetOrCreateBackgroundBitmap(
            targetFrame->width,
            targetFrame->height,
            targetFrame->stride,
            targetFrame->bgra8
        );
    }

    renderer_.Render(
        ow.dcomp->GetD2DContext(),
        ow.monitor,
        bgBitmap,
        targetFrame,
        dummyUi
    );
    ow.dcomp->Commit();
}

PointPx OverlayWindowManager::GetMsgPosition(HWND /*hwnd*/, LPARAM /*lParam*/) {
    POINT pt;
    if (GetCursorPos(&pt)) {
        return { pt.x, pt.y };
    }
    DWORD pos = GetMessagePos();
    return { GET_X_LPARAM(pos), GET_Y_LPARAM(pos) };
}

HWND OverlayWindowManager::GetOwnerHwnd() const {
    if (windows_.empty()) return nullptr;
    return windows_[0].hwnd;
}

LRESULT CALLBACK OverlayWindowManager::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    OverlayWindowManager* manager = nullptr;
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCT*>(lParam);
        manager = reinterpret_cast<OverlayWindowManager*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(manager));
    } else {
        manager = reinterpret_cast<OverlayWindowManager*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    if (manager) {
        return manager->HandleMessage(hwnd, msg, wParam, lParam);
    } else {
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

LRESULT OverlayWindowManager::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            for (auto& ow : windows_) {
                if (ow.hwnd == hwnd) {
                    RenderWindow(ow);
                    break;
                }
            }
            EndPaint(hwnd, &ps);
            return 0;
        }

        // ── Dwell timer: fires 80 ms after last mouse move ──────────────────
        case WM_TIMER: {
            if (wParam == 1 /* TIMER_DWELL */ && session_) {
                KillTimer(hwnd, 1);
                auto& snap = session_->GetStateMachine().GetUiaSnapping();
                auto state = session_->GetStateMachine().GetSelection().GetState();
                // Only trigger dwell query while hovering (not during drag/selection)
                if (state == SelectionModel::State::None)
                    snap.QueryDwellAsync(lastCursorPos_);
            } else if (wParam == 2 /* TIMER_CARET */ && session_) {
                auto& sm = session_->GetStateMachine();
                if (sm.IsEditingText()) {
                    sm.ToggleCaret();
                    InvalidateAll();
                }
            }
            return 0;
        }

        // ── UIA dwell result: more-precise element found asynchronously ──────
        case WM_SNAP_UIA_RESULT: {
            auto* r = reinterpret_cast<RectPx*>(wParam);
            if (r && session_) {
                auto state = session_->GetStateMachine().GetSelection().GetState();
                if (state == SelectionModel::State::None) {
                    session_->GetStateMachine().SetHoveredFromUia(*r, lastCursorPos_);
                    InvalidateAll();
                }
            }
            delete r;  // main thread owns the heap object posted by DwellWorker
            return 0;
        }

        case WM_SETCURSOR: {
            UpdateCursor(hwnd);
            return TRUE;
        }

        case WM_MOUSEWHEEL: {
            if (session_) {
                short delta = GET_WHEEL_DELTA_WPARAM(wParam);
                PointPx p = GetMsgPosition(hwnd, lParam);
                auto& stateMachine = session_->GetStateMachine();
                if (stateMachine.GetMode() == InteractionStateMachine::Mode::SelectRoi && !stateMachine.IsEditingText()) {
                    stateMachine.ProcessMouseWheelEx(delta, p);
                } else {
                    float floatDelta = delta > 0 ? 1.0f : -1.0f;
                    float currentScale = renderer_.GetLoupeScale();
                    renderer_.SetLoupeScale(currentScale + floatDelta);
                }
                InvalidateAll();
                return 0;
            }
            break;
        }

        case WM_IME_COMPOSITION: {
            if (session_ && session_->GetStateMachine().IsEditingText()) {
                auto& sm = session_->GetStateMachine();
                if (lParam & GCS_COMPSTR) {
                    HIMC hIMC = ImmGetContext(hwnd);
                    if (hIMC) {
                        LONG size = ImmGetCompositionStringW(hIMC, GCS_COMPSTR, nullptr, 0);
                        if (size >= 0) {
                            std::vector<wchar_t> buf(size / sizeof(wchar_t) + 1);
                            ImmGetCompositionStringW(hIMC, GCS_COMPSTR, buf.data(), size);
                            buf[size / sizeof(wchar_t)] = L'\0';
                            sm.SetImeCompositionString(buf.data());
                        }
                        ImmReleaseContext(hwnd, hIMC);
                    }
                }
                if (lParam & GCS_RESULTSTR) {
                    sm.SetImeCompositionString(L"");
                    sm.UpdateTextLayoutMetrics();
                    break; // Let DefWindowProc translate committed characters
                }
                sm.UpdateTextLayoutMetrics();
                sm.UpdateImeWindowPosition(hwnd);
                InvalidateAll();
                return 0;
            }
            break;
        }

        case WM_IME_ENDCOMPOSITION: {
            if (session_ && session_->GetStateMachine().IsEditingText()) {
                auto& sm = session_->GetStateMachine();
                sm.SetImeCompositionString(L"");
                sm.UpdateTextLayoutMetrics();
                InvalidateAll();
            }
            break; // Let DefWindowProc handle IME cleanup
        }

        case WM_CHAR:
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP:
        case WM_KEYUP:
        case WM_KEYDOWN: {
            if (!session_) break;
            auto& stateMachine = session_->GetStateMachine();

            // Prioritize windowless text editor input if editing
            if (stateMachine.IsEditingText()) {
                if (stateMachine.HandleKeyboardMessage(hwnd, msg, wParam, lParam)) {
                    InvalidateAll();
                    return 0;
                }
            }

            // Track Alt (VK_MENU) key state
            if (msg == WM_SYSKEYDOWN || msg == WM_KEYDOWN) {
                if (wParam == VK_MENU) {
                    stateMachine.SetAltPressed(true);
                    InvalidateAll();
                    return 0; // Prevent menu activation
                }
            } else if (msg == WM_SYSKEYUP || msg == WM_KEYUP) {
                if (wParam == VK_MENU) {
                    stateMachine.SetAltPressed(false);
                    InvalidateAll();
                    return 0;
                }
            }

            // Process other keyboard inputs only on KEYDOWN events
            if (msg != WM_KEYDOWN && msg != WM_SYSKEYDOWN) {
                break;
            }

            // Cycle color formats via Shift key
            if (wParam == VK_SHIFT) {
                stateMachine.CycleColorFormat();
                InvalidateAll();
                return 0;
            }

            // Tab key: cycle through snap candidates at current cursor position
            if (wParam == VK_TAB) {
                bool shiftHeld = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                session_->GetStateMachine().TriggerTabCycle(!shiftHeld);
                InvalidateAll();
                return 0;
            }

            // 1. Copy Color (C key)
            if (wParam == 'C') {
                POINT pt;
                if (GetCursorPos(&pt)) {
                    for (const auto& mf : session_->GetDesktopFrame().monitors) {
                        if (pt.x >= mf.monitor.virtualBounds.x && pt.x < mf.monitor.virtualBounds.x + mf.monitor.virtualBounds.width &&
                            pt.y >= mf.monitor.virtualBounds.y && pt.y < mf.monitor.virtualBounds.y + mf.monitor.virtualBounds.height) {
                            
                            int localX = pt.x - mf.monitor.virtualBounds.x;
                            int localY = pt.y - mf.monitor.virtualBounds.y;
                            int clampX = std::max(0, std::min(localX, mf.width - 1));
                            int clampY = std::max(0, std::min(localY, mf.height - 1));
                            int pixelIndex = clampY * mf.stride + clampX * 4;
                            
                            if (pixelIndex + 2 < mf.bgra8.size()) {
                                int b = (int)mf.bgra8[pixelIndex];
                                int g = (int)mf.bgra8[pixelIndex + 1];
                                int r = (int)mf.bgra8[pixelIndex + 2];
                                
                                wchar_t colorStr[128];
                                auto format = stateMachine.GetColorFormat();
                                if (format == InteractionStateMachine::ColorFormat::RGB) {
                                    swprintf_s(colorStr, L"rgb(%d, %d, %d)", r, g, b);
                                } else if (format == InteractionStateMachine::ColorFormat::Hex) {
                                    swprintf_s(colorStr, L"#%02X%02X%02X", r, g, b);
                                } else if (format == InteractionStateMachine::ColorFormat::HSV) {
                                    int h, s, v;
                                    RGBToHSV(r, g, b, h, s, v);
                                    swprintf_s(colorStr, L"hsv(%d, %d%%, %d%%)", h, s, v);
                                } else if (format == InteractionStateMachine::ColorFormat::HSL) {
                                    int h, s, l;
                                    RGBToHSL(r, g, b, h, s, l);
                                    swprintf_s(colorStr, L"hsl(%d, %d%%, %d%%)", h, s, l);
                                }
                                
                                if (OpenClipboard(nullptr)) {
                                    EmptyClipboard();
                                    size_t sz = (wcslen(colorStr) + 1) * sizeof(wchar_t);
                                    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, sz);
                                    if (hMem) {
                                        void* ptr = GlobalLock(hMem);
                                        memcpy(ptr, colorStr, sz);
                                        GlobalUnlock(hMem);
                                        SetClipboardData(CF_UNICODETEXT, hMem);
                                    }
                                    CloseClipboard();
                                }
                            }
                            break;
                        }
                    }
                }
                return 0;
            }

            // 2. Escape Tiered Backout
            if (wParam == VK_ESCAPE) {
                InteractionStateMachine::Action action = stateMachine.ProcessMouseInputEx(msg, { static_cast<int>(wParam), 0 });
                if (action == InteractionStateMachine::Action::Cancel) {
                    session_->Cancel();
                } else {
                    InvalidateAll();
                }
                return 0;
            }
            
            // 3. Return key (Complete)
            if (wParam == VK_RETURN) {
                session_->Complete();
                return 0;
            }

            // 4. Arrow keys (Nudge cursor OR Nudge selection)
            if (wParam == VK_UP || wParam == VK_DOWN || wParam == VK_LEFT || wParam == VK_RIGHT) {
                auto& selection = stateMachine.GetSelection();
                
                if (selection.GetState() == SelectionModel::State::Selected) {
                    // Adjust Selection Rect
                    RectPx roi = selection.GetCurrentRoi();
                    if (GetKeyState(VK_SHIFT) & 0x8000) {
                        // Shift + Arrow: Resize right/bottom bounds
                        if (wParam == VK_UP) roi.height = std::max(1, roi.height - 1);
                        else if (wParam == VK_DOWN) roi.height += 1;
                        else if (wParam == VK_LEFT) roi.width = std::max(1, roi.width - 1);
                        else if (wParam == VK_RIGHT) roi.width += 1;
                    } else {
                        // Standard Arrow: Move selection bounds
                        if (wParam == VK_UP) roi.y -= 1;
                        else if (wParam == VK_DOWN) roi.y += 1;
                        else if (wParam == VK_LEFT) roi.x -= 1;
                        else if (wParam == VK_RIGHT) roi.x += 1;
                    }
                    selection.SetRoi(roi);
                    stateMachine.GetToolbar().SetSelectionRect(D2D1::RectF(
                        static_cast<float>(roi.x), static_cast<float>(roi.y),
                        static_cast<float>(roi.x + roi.width), static_cast<float>(roi.y + roi.height)
                    ));
                    InvalidateAll();
                } else {
                    // Nudge Cursor
                    POINT pt;
                    if (GetCursorPos(&pt)) {
                        if (wParam == VK_UP) pt.y -= 1;
                        else if (wParam == VK_DOWN) pt.y += 1;
                        else if (wParam == VK_LEFT) pt.x -= 1;
                        else if (wParam == VK_RIGHT) pt.x += 1;
                        SetCursorPos(pt.x, pt.y);
                        
                        InteractionStateMachine::Action action = stateMachine.ProcessMouseInputEx(WM_MOUSEMOVE, { pt.x, pt.y });
                        (void)action;
                        lastCursorPos_ = { pt.x, pt.y };
                        InvalidateAll();
                    }
                }
                return 0;
            }
            break;
        }
        case WM_MOUSEMOVE: {
            PointPx p = GetMsgPosition(hwnd, lParam);

            if (p.x == lastCursorPos_.x && p.y == lastCursorPos_.y) {
                return 0;
            }
            lastCursorPos_ = p;

            if (session_) {
                // Cancel any pending dwell UIA query; restart the 200 ms timer
                session_->GetStateMachine().GetUiaSnapping().CancelDwellQuery();
                KillTimer(hwnd, 1);
                auto state = session_->GetStateMachine().GetSelection().GetState();
                if (state == SelectionModel::State::None)
                    SetTimer(hwnd, 1 /* TIMER_DWELL */, 120, nullptr); // Shortened from 200ms to 120ms for faster, snappier response
                InteractionStateMachine::Action action = session_->GetStateMachine().ProcessMouseInputEx(msg, p);
                
                if (session_->GetStateMachine().IsEditingText()) {
                    session_->GetStateMachine().UpdateImeWindowPosition(hwnd);
                }
                
                if (action == InteractionStateMachine::Action::Complete) {
                    ReleaseCapture();
                    session_->Complete();
                    return 0;
                } else if (action == InteractionStateMachine::Action::Cancel) {
                    ReleaseCapture();
                    session_->Cancel();
                    return 0;
                } else if (action == InteractionStateMachine::Action::Pin) {
                    ReleaseCapture();
                    session_->Pin();
                    return 0;
                } else if (action == InteractionStateMachine::Action::Save) {
                    ReleaseCapture();
                    session_->Save();
                    return 0;
                }
                
                UpdateCursor(hwnd);
                InvalidateAll(); // Redraw selection
                return 0;
            }
            break;
        }
        case WM_LBUTTONDBLCLK:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP: {
            PointPx p = GetMsgPosition(hwnd, lParam);
            lastCursorPos_ = p;

            if (session_) {
                bool clickedToolbar = false;
                D2D1_POINT_2F d2dPt = D2D1::Point2F(static_cast<float>(p.x), static_cast<float>(p.y));
                clickedToolbar = session_->GetStateMachine().GetToolbar().HitTestToolbarOnly(d2dPt);

                if (msg == WM_LBUTTONDOWN) {
                    SetCapture(hwnd);
                }
                else if (msg == WM_LBUTTONUP) ReleaseCapture();

                InteractionStateMachine::Action action = session_->GetStateMachine().ProcessMouseInputEx(msg, p);
                
                if (session_->GetStateMachine().IsEditingText()) {
                    session_->GetStateMachine().UpdateTextLayoutMetrics();
                    session_->GetStateMachine().UpdateImeWindowPosition(hwnd);
                }

                if (action == InteractionStateMachine::Action::Complete) {
                    ReleaseCapture();
                    session_->Complete();
                    return 0;
                } else if (action == InteractionStateMachine::Action::Cancel) {
                    ReleaseCapture();
                    session_->Cancel();
                    return 0;
                } else if (action == InteractionStateMachine::Action::Pin) {
                    ReleaseCapture();
                    session_->Pin();
                    return 0;
                } else if (action == InteractionStateMachine::Action::Save) {
                    ReleaseCapture();
                    session_->Save();
                    return 0;
                }
                
                UpdateCursor(hwnd);
                InvalidateAll(); // Redraw selection
                return 0;
            }
            break;
        }
        case WM_RBUTTONDOWN:
            if (session_) {
                PointPx p = GetMsgPosition(hwnd, lParam);
                InteractionStateMachine::Action action = session_->GetStateMachine().ProcessMouseInputEx(msg, p);
                if (action == InteractionStateMachine::Action::Cancel) {
                    session_->Cancel();
                } else {
                    InvalidateAll();
                }
                return 0;
            }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void OverlayWindowManager::UpdateCursor(HWND hwnd) {
    POINT pt;
    if (GetCursorPos(&pt)) {
        POINT clientPt = pt;
        ScreenToClient(hwnd, &clientPt);
        D2D1_POINT_2F d2dPt = D2D1::Point2F(static_cast<float>(clientPt.x), static_cast<float>(clientPt.y));
        
        if (session_) {
            auto& stateMachine = session_->GetStateMachine();
            if (stateMachine.GetToolbar().HitTestToolbarOnly(d2dPt)) {
                SetCursor(LoadCursor(nullptr, IDC_ARROW));
                return;
            }

            if (stateMachine.IsEditingText()) {
                D2D1_RECT_F textRect = stateMachine.GetTextRect();
                
                // 0. Check if hovering on the close button (top-right of frame)
                float closeX = textRect.right + 4.0f;
                float closeY = textRect.top - 4.0f;
                float dxClose = d2dPt.x - closeX;
                float dyClose = d2dPt.y - closeY;
                if (dxClose * dxClose + dyClose * dyClose <= 64.0f) { // 8px radius hit area
                    SetCursor(LoadCursor(nullptr, IDC_HAND));
                    return;
                }
                
                // 1. Check if hovering on the right resize knob
                float knobX = textRect.right + 4.0f;
                float knobY = (textRect.top + textRect.bottom) / 2.0f;
                float dx = d2dPt.x - knobX;
                float dy = d2dPt.y - knobY;
                if (dx * dx + dy * dy <= 64.0f) { // 8px radius hit area
                    SetCursor(LoadCursor(nullptr, IDC_SIZEWE));
                    return;
                }

                // 2. Check if hovering on the dashed border / move frame
                D2D1_RECT_F outerFrame = D2D1::RectF(
                    textRect.left - 10.0f,
                    textRect.top - 10.0f,
                    textRect.right + 10.0f,
                    textRect.bottom + 10.0f
                );
                bool insideFrame = (d2dPt.x >= outerFrame.left && d2dPt.x <= outerFrame.right &&
                                    d2dPt.y >= outerFrame.top && d2dPt.y <= outerFrame.bottom);
                bool insideText = (d2dPt.x >= textRect.left && d2dPt.x <= textRect.right &&
                                   d2dPt.y >= textRect.top && d2dPt.y <= textRect.bottom);

                if (insideFrame && !insideText) {
                    SetCursor(LoadCursor(nullptr, IDC_SIZEALL));
                    return;
                }

                // 3. Inside the textbox: show I-Beam text cursor
                if (insideText) {
                    SetCursor(LoadCursor(nullptr, IDC_IBEAM));
                    return;
                }
            }

            // If we are in Text Tool mode but not actively editing, check if hovering over any committed text block!
            if (stateMachine.GetContext().activeTool == ToolType::Text) {
                auto hitText = stateMachine.GetDocument().HitTestText(d2dPt);
                if (hitText) {
                    // Check if hovering on its close button (top-right of frame)
                    float closeX = hitText->rect.right + 4.0f;
                    float closeY = hitText->rect.top - 4.0f;
                    float dx = d2dPt.x - closeX;
                    float dy = d2dPt.y - closeY;
                    if (dx * dx + dy * dy <= 64.0f) { // 8px radius hit area
                        SetCursor(LoadCursor(nullptr, IDC_HAND));
                        return;
                    }
                    SetCursor(LoadCursor(nullptr, IDC_IBEAM));
                    return;
                }
            }
            
            const auto& selection = stateMachine.GetSelection();
            auto state = selection.GetState();
            
            if (state == SelectionModel::State::None || 
                state == SelectionModel::State::Creating || 
                state == SelectionModel::State::Moving || 
                state == SelectionModel::State::Resizing) 
            {
                // Hide system cursor during active drag/resize to eliminate DWM composition latency
                SetCursor(NULL);
            } 
            else if (state == SelectionModel::State::Selected) 
            {
                PointPx virtualPt = { pt.x, pt.y };
                auto handle = selection.HitTestHandle(virtualPt);
                
                if (handle != SelectionModel::ResizeHandle::None) {
                    switch (handle) {
                        case SelectionModel::ResizeHandle::TopLeft:
                        case SelectionModel::ResizeHandle::BottomRight:
                            SetCursor(LoadCursor(nullptr, IDC_SIZENWSE));
                            break;
                        case SelectionModel::ResizeHandle::TopRight:
                        case SelectionModel::ResizeHandle::BottomLeft:
                            SetCursor(LoadCursor(nullptr, IDC_SIZENESW));
                            break;
                        case SelectionModel::ResizeHandle::TopCenter:
                        case SelectionModel::ResizeHandle::BottomCenter:
                            SetCursor(LoadCursor(nullptr, IDC_SIZENS));
                            break;
                        case SelectionModel::ResizeHandle::LeftCenter:
                        case SelectionModel::ResizeHandle::RightCenter:
                            SetCursor(LoadCursor(nullptr, IDC_SIZEWE));
                            break;
                        default:
                            SetCursor(LoadCursor(nullptr, IDC_CROSS));
                            break;
                    }
                } else {
                    SetCursor(LoadCursor(nullptr, IDC_CROSS));
                }
            }
        }
    }
}

void OverlayWindowManager::ResetBackgroundBitmaps() {
    for (auto& ow : windows_) {
        if (ow.dcomp) {
            ow.dcomp->ResetBackgroundBitmap();
        }
    }
}
