#pragma once
#include "editing/SelectionModel.h"
#include "platform/WindowEnumerationService.h"
#include "platform/UiaSnappingService.h"
#include <windows.h>
#include <vector>
#include <optional>
#include <memory>
#include <chrono>
#include <functional>
#include "editing/AnnotationDocument.h"
#include "editing/ToolbarUI.h"

class InteractionStateMachine {
public:
    enum class Mode { SelectRoi, DrawRectangle, DrawArrow, DrawEllipse, DrawLine, DrawFreehand, DrawMosaic, Text, Blur };

    enum class ColorFormat { RGB, Hex, HSV, HSL };

    InteractionStateMachine();

    void Reset();
    void InitializeInitialHoverRect(const PointPx& pt);
    void SetMode(Mode m);
    Mode GetMode() const { return currentMode_; }
    void ProcessMouseInput(UINT msg, const PointPx& pt);

    const SelectionModel& GetSelection() const { return selection_; }
    SelectionModel& GetSelection() { return selection_; }

    const std::optional<RectPx>& GetHoveredWindowRect() const { return hoveredWindowRect_; }

    AnnotationDocument& GetDocument() { return document_; }
    ToolbarUI& GetToolbar() { return toolbar_; }
    std::shared_ptr<Annotation> GetCurrentDraft() const { return currentDraft_; }
    const ToolContext& GetContext() const { return toolContext_; }
    ToolContext& GetContext() { return toolContext_; }

    enum class Action { None, Complete, Cancel, Pin, Save };
    Action ProcessMouseInputEx(UINT msg, const PointPx& pt);
    Action ProcessMouseWheelEx(short delta, const PointPx& pt);

    // Keyboard states and configuration methods
    void SetAltPressed(bool pressed) { altPressed_ = pressed; }
    bool IsAltPressed() const { return altPressed_; }

    void SetShiftPressed(bool pressed) { shiftPressed_ = pressed; }
    bool IsShiftPressed() const { return shiftPressed_; }

    ColorFormat GetColorFormat() const { return currentColorFormat_; }
    void CycleColorFormat() {
        currentColorFormat_ = static_cast<ColorFormat>((static_cast<int>(currentColorFormat_) + 1) % 4);
    }

    bool IsAutoDetectEnabled() const { return autoDetectEnabled_; }
    void SetAutoDetectEnabled(bool enabled) { autoDetectEnabled_ = enabled; }
    void ToggleAutoDetect() { autoDetectEnabled_ = !autoDetectEnabled_; }

    // Windowless Text Editor Public Interface
    bool IsEditingText() const { return isEditingText_; }
    const std::wstring& GetActiveText() const { return activeText_; }
    size_t GetCaretIndex() const { return caretIndex_; }
    D2D1_RECT_F GetTextRect() const { return textRect_; }
    bool IsCaretVisible() const { return caretVisible_; }
    void ToggleCaret() { caretVisible_ = !caretVisible_; }
    void SetCaretVisible(bool visible) { caretVisible_ = visible; }
    const std::wstring& GetImeCompositionString() const { return imeCompositionString_; }
    void SetImeCompositionString(const std::wstring& str) { imeCompositionString_ = str; }
    
    bool HandleKeyboardMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void CommitActiveText();
    void UpdateTextLayoutMetrics();
    void UpdateImeWindowPosition(HWND hwnd);
    IDWriteTextLayout* GetActiveTextLayout() const { return cachedTextLayout_.Get(); }

    size_t GetSelectionAnchor() const { return selectionAnchor_; }
    size_t GetSelectionActive() const { return selectionActive_; }
    bool HasTextSelection() const { return selectionAnchor_ != selectionActive_; }
    void DeleteSelection();

    // Tab cycling: Tab = back to parent window (shallower); Shift+Tab = drill into child controls (deeper).
    // tabCandidates_ is sorted area-ASC (index 0 = smallest/deepest control, back() = window/largest container).
    // tabIndex_ starts at the best/deepest eligible control, so Tab goes up towards window level.
    void TriggerTabCycle(bool forward) {
        if (tabCandidates_.empty()) return;
        int n = static_cast<int>(tabCandidates_.size());
        if (forward) {
            // Tab: go to shallower (less precise) parent element
            tabIndex_ = (tabIndex_ + 1) % n;
        } else {
            // Shift+Tab: go to deeper (more precise) child element
            tabIndex_ = (tabIndex_ - 1 + n) % n;
        }
        hoveredWindowRect_ = tabCandidates_[tabIndex_];
    }

    // Called by OverlayWindowManager when WM_SNAP_UIA_RESULT arrives.
    // queryPt = physical cursor position at the time of the dwell query.
    void SetHoveredFromUia(const RectPx& r, const PointPx& queryPt) {
        uiaOverrideRect_ = r;
        uiaOverridePt_   = queryPt;
        hoveredWindowRect_ = r;

        // Synchronize to tabCandidates_ to support wheel cycling from the dwell-highlighted element
        if (autoDetectEnabled_) {
            tabCandidates_ = uiaSnapping_.HitTestAll(queryPt);
            if (tabCandidates_.empty()) {
                for (const auto& w : windowRects_) {
                    if (w.contains(queryPt)) {
                        tabCandidates_.push_back(w);
                        break;
                    }
                }
            }
            tabIndex_ = 0;
            for (size_t i = 0; i < tabCandidates_.size(); ++i) {
                if (tabCandidates_[i].x == r.x &&
                    tabCandidates_[i].y == r.y &&
                    tabCandidates_[i].width == r.width &&
                    tabCandidates_[i].height == r.height) {
                    tabIndex_ = static_cast<int>(i);
                    break;
                }
            }
        }
    }

    UiaSnappingService& GetUiaSnapping() { return uiaSnapping_; }
    const UiaSnappingService& GetUiaSnapping() const { return uiaSnapping_; }

private:
    Mode currentMode_;
    SelectionModel selection_;
    
    std::vector<RectPx>   windowRects_;
    std::vector<MonitorSnapshot> cachedMonitors_;
    std::optional<RectPx>  hoveredWindowRect_;
    PointPx                mouseDownPt_{0, 0};
    bool                   isDrawing_ = false;
    D2D1_POINT_2F          drawStartPt_{};
    std::shared_ptr<Annotation> currentDraft_;

    // Windowless Text Editor Private State
    bool isEditingText_ = false;
    std::wstring activeText_;
    std::wstring imeCompositionString_;
    size_t caretIndex_ = 0;
    D2D1_RECT_F textRect_{};
    bool caretVisible_ = true;
    
    enum class TextDragState { None, Moving, ResizingWidth };
    TextDragState textDragState_ = TextDragState::None;
    PointPx lastMousePt_{0, 0};
    
    size_t selectionAnchor_ = 0;
    size_t selectionActive_ = 0;
    bool isDragSelecting_ = false;

    struct TextState {
        std::wstring text;
        size_t caretIndex;
        size_t selectionAnchor;
        size_t selectionActive;
        D2D1_RECT_F textRect;
    };
    std::vector<TextState> localUndoStack_;
    std::vector<TextState> localRedoStack_;

    void PushLocalUndoState();
    void SelectWord(size_t clickPos);

    Microsoft::WRL::ComPtr<IDWriteFactory> dwriteFactoryPrivate_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> cachedTextFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextLayout> cachedTextLayout_;
    void EnsurePrivateDWriteFactory();

    // Tab cycling candidates (refreshed on every hover move)
    std::vector<RectPx>    tabCandidates_;
    int                    tabIndex_ = 0;

    // UIA dwell override: persists until mouse moves >8px from query point
    std::optional<RectPx>  uiaOverrideRect_;
    PointPx                uiaOverridePt_{-9999, -9999};

    AnnotationDocument document_;
    ToolbarUI toolbar_;
    ToolContext toolContext_;

    // Keyboard and UIA snap configurations
    bool altPressed_ = false;
    bool shiftPressed_ = false;
    ColorFormat currentColorFormat_ = ColorFormat::Hex;
    bool autoDetectEnabled_ = true;
    UiaSnappingService uiaSnapping_;

    // Fix D: timestamp for periodic window-list refresh (every 3 s)
    std::chrono::steady_clock::time_point lastWindowRefresh_{};
};
