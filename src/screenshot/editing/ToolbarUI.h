#pragma once
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include "EditingTypes.h"

struct ToolbarButton {
    D2D1_RECT_F bounds;
    std::wstring icon;
    ToolType action;
    bool isSeparator;
};

struct ColorButton {
    D2D1_RECT_F bounds;
    D2D1_COLOR_F color;
};

struct StrokeButton {
    D2D1_RECT_F bounds;
    float strokeWidth;
    float displayRadius;
};

struct FontOptionButton {
    D2D1_RECT_F bounds;
    std::wstring text; // "小", "中", "大", "特", "B", "I"
    float fontSizeValue; // 14, 18, 24, 32 or 0 for toggles
    bool isBoldToggle;
    bool isItalicToggle;
    bool isToggled;
};

class ToolbarUI {
public:
    ToolbarUI();
    void SetSelectionRect(D2D1_RECT_F selRect);
    void Draw(ID2D1DeviceContext* context, ID2D1Bitmap1* bgBitmap = nullptr);
    bool HitTest(D2D1_POINT_2F pt, ToolContext& context, ToolType& outAction);
    
    void UpdateHover(D2D1_POINT_2F pt);
    void SetCurrentContext(const ToolContext& ctx);
    const ToolContext& GetCurrentContext() const { return currentContext_; }
    bool HitTestToolbarOnly(D2D1_POINT_2F pt) const;
    
    D2D1_RECT_F GetPrimaryBounds() const { return primaryBounds_; }
    D2D1_RECT_F GetSubBounds() const { return subBounds_; }

private:
    std::vector<ToolbarButton> primaryButtons_;
    std::vector<ColorButton> colorButtons_;
    std::vector<StrokeButton> strokeButtons_;
    std::vector<FontOptionButton> fontOptionButtons_;
    
    D2D1_RECT_F selectionRect_{};
    D2D1_RECT_F primaryBounds_{};
    D2D1_RECT_F subBounds_{};
    D2D1_POINT_2F hoverPt_{-1, -1};
    
    D2D1_RECT_F fontSizeBtnBounds_{};
    D2D1_RECT_F boldBtnBounds_{};
    D2D1_RECT_F italicBtnBounds_{};
    bool sizeDropdownActive_ = false;
    D2D1_RECT_F sizeDropdownBounds_{};
    
    ToolContext currentContext_;
    
    // Cached DWrite resources (independent of D2D device)
    Microsoft::WRL::ComPtr<IDWriteFactory> dwriteFactory_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> iconTextFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> tooltipTextFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fontOptionTextFormat_;

    // Cached D2D brushes (device dependent)
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> cachedTextBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> cachedHighlightBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> cachedHoverBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> cachedBorderBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> cachedGlassTintBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> cachedColorButtonBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> cachedTooltipBgBrush_;

    void EnsureBrushes(ID2D1DeviceContext* context);
    void UpdateLayout();
    bool IsDrawingTool(ToolType type) const;
    void DrawRoundedRect(ID2D1DeviceContext* context, D2D1_RECT_F rect, ID2D1Brush* brush, ID2D1Brush* borderBrush, float strokeWidth = 1.0f, float radius = 4.0f);
};
