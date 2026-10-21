#pragma once

#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include "capture/DesktopFrame.h"
#include "platform/MonitorTopology.h"
#include <optional>
#include <memory>

#include "editing/InteractionStateMachine.h"

class SelectionModel;
class AnnotationDocument;
class ToolbarUI;
class Annotation;

struct UiState {
    const SelectionModel* selectionModel = nullptr;
    AnnotationDocument* document = nullptr;
    ToolbarUI* toolbar = nullptr;
    std::shared_ptr<Annotation> currentDraft;
    PointPx cursorPos = {0, 0};
    std::optional<RectPx> hoveredWindowRect;
    bool isAltPressed = false;
    InteractionStateMachine::ColorFormat colorFormat = InteractionStateMachine::ColorFormat::Hex;
    
    // Windowless Text Editor State
    bool isEditingText = false;
    std::wstring activeText;
    std::wstring imeCompositionString;
    size_t caretIndex = 0;
    D2D1_RECT_F textRect{};
    bool caretVisible = true;
    D2D1_COLOR_F textColor{};
    float fontSize = 14.0f;
    bool isBold = false;
    bool isItalic = false;
    size_t selectionAnchor = 0;
    size_t selectionActive = 0;
    IDWriteTextLayout* textLayout = nullptr;
};

class OverlayRenderer {
public:
    OverlayRenderer();
    ~OverlayRenderer();

    void Render(
        ID2D1DeviceContext* d2dContext,
        const MonitorSnapshot& monitor,
        ID2D1Bitmap1* bgBitmap,
        const MonitorFrame* targetFrame,
        const UiState& ui
    );

    float GetLoupeScale() const { return loupeScale_; }
    void SetLoupeScale(float scale) { loupeScale_ = scale < 2.0f ? 2.0f : (scale > 20.0f ? 20.0f : scale); }

    float GetHoverAlpha() const { return hoverAlpha_; }
    void SetHoverAlpha(float alpha) { hoverAlpha_ = alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha); }

private:
    Microsoft::WRL::ComPtr<IDWriteFactory> dwriteFactory_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> textFormat_;
    float loupeScale_ = 6.0f;
    float hoverAlpha_ = 0.0f;

    // Replaces the former 'static' local variable in Render() — must be per-instance
    // to avoid cross-monitor state pollution in multi-monitor scenarios.
    RectPx lastKnownHoveredRect_{};

    // Cached D2D brushes — created once, reused every frame.
    // Reset via ResetCachedBrushes() on device lost.
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> cachedDimBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> cachedBorderBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> cachedHoverOuterBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> cachedHoverInnerBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> cachedLoupeBorderBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> cachedCrosshairBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> cachedTextBgBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> cachedTextBrush_;

    void EnsureBrushes(ID2D1DeviceContext* context);
    void ResetCachedBrushes();

    void RenderLoupe(
        ID2D1DeviceContext* d2dContext,
        ID2D1Bitmap* backgroundBitmap,
        const MonitorSnapshot& monitor,
        const MonitorFrame& frame,
        const UiState& ui
    );
};
