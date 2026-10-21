#include "overlay/OverlayRenderer.h"
#include "editing/InteractionStateMachine.h"
#include "editing/SelectionModel.h"
#include "platform/CoordinateMapper.h"
#include <dxgi1_2.h>
#include <d2d1helper.h>
#include <algorithm>
#include <string>

#pragma comment(lib, "dwrite.lib")
#include "editing/InteractionStateMachine.h"
#include "platform/CoordinateMapper.h"
#include <dxgi1_2.h>
#include <d2d1helper.h>

OverlayRenderer::OverlayRenderer() {
    DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        &dwriteFactory_
    );

    if (dwriteFactory_) {
        dwriteFactory_->CreateTextFormat(
            L"Segoe UI",
            nullptr,
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            12.0f,
            L"en-us",
            &textFormat_
        );
        if (textFormat_) {
            textFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            textFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }
}

OverlayRenderer::~OverlayRenderer() {}

void OverlayRenderer::EnsureBrushes(ID2D1DeviceContext* context) {
    if (cachedDimBrush_) return; // Already initialized

    context->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.4f), &cachedDimBrush_);
    context->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.5f, 1.0f, 1.0f), &cachedBorderBrush_);
    context->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.47f, 0.83f, 0.25f), &cachedHoverOuterBrush_);
    context->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.8f, 1.0f, 0.85f), &cachedHoverInnerBrush_);
    context->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.8f), &cachedLoupeBorderBrush_);
    context->CreateSolidColorBrush(D2D1::ColorF(0.0f, 1.0f, 1.0f, 0.5f), &cachedCrosshairBrush_);
    context->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f), &cachedTextBgBrush_);  // Pure black for cursor outline
    context->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &cachedTextBrush_);
}

void OverlayRenderer::ResetCachedBrushes() {
    cachedDimBrush_.Reset();
    cachedBorderBrush_.Reset();
    cachedHoverOuterBrush_.Reset();
    cachedHoverInnerBrush_.Reset();
    cachedLoupeBorderBrush_.Reset();
    cachedCrosshairBrush_.Reset();
    cachedTextBgBrush_.Reset();
    cachedTextBrush_.Reset();
}

void OverlayRenderer::Render(
    ID2D1DeviceContext* d2dContext,
    const MonitorSnapshot& monitor,
    ID2D1Bitmap1* bgBitmap,
    const MonitorFrame* targetFrame,
    const UiState& ui)
{
    d2dContext->BeginDraw();
    d2dContext->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

    EnsureBrushes(d2dContext);

    if (bgBitmap && targetFrame) {
        D2D1_RECT_F destRect = D2D1::RectF(
            0.0f, 0.0f,
            static_cast<float>(targetFrame->width),
            static_cast<float>(targetFrame->height)
        );
        d2dContext->DrawBitmap(
            bgBitmap,
            &destRect,
            1.0f,
            D2D1_INTERPOLATION_MODE_LINEAR,
            nullptr
        );
    }

    // ── Advance hover animation & lock in latest hover rect ────────────────
    if (ui.hoveredWindowRect) {
        lastKnownHoveredRect_ = *ui.hoveredWindowRect;
        hoverAlpha_ = std::min(1.0f, hoverAlpha_ + 0.2f);
    } else {
        hoverAlpha_ = std::max(0.0f, hoverAlpha_ - 0.15f);
    }

    // Monitor-local hover rect (used for both dim cutout and border)
    D2D1_RECT_F hwRect = {};
    bool drawHover = hoverAlpha_ > 0.0f &&
                     lastKnownHoveredRect_.width  > 0 &&
                     lastKnownHoveredRect_.height > 0;
    if (drawHover) {
        int lhx = lastKnownHoveredRect_.x - monitor.virtualBounds.x;
        int lhy = lastKnownHoveredRect_.y - monitor.virtualBounds.y;
        hwRect = D2D1::RectF(
            static_cast<float>(lhx),
            static_cast<float>(lhy),
            static_cast<float>(lhx + lastKnownHoveredRect_.width),
            static_cast<float>(lhy + lastKnownHoveredRect_.height));
    }

    // ── Dim overlay ──────────────────────────────────────────────────────────
    float W = static_cast<float>(monitor.virtualBounds.width);
    float H = static_cast<float>(monitor.virtualBounds.height);

    float mouseX = static_cast<float>(ui.cursorPos.x - monitor.virtualBounds.x);
    float mouseY = static_cast<float>(ui.cursorPos.y - monitor.virtualBounds.y);
    D2D1_POINT_2F mousePt = D2D1::Point2F(static_cast<float>(ui.cursorPos.x), static_cast<float>(ui.cursorPos.y));

    // Helper: fill dim around a cut-out rect (4 surrounding rects)
    // The cut-out rect shows at full original brightness; surroundings are dimmed.
    auto fillDim4 = [&](D2D1_RECT_F cut) {
        if (cut.top > 0.0f)
            d2dContext->FillRectangle({0.0f, 0.0f, W, cut.top}, cachedDimBrush_.Get());
        if (cut.bottom < H)
            d2dContext->FillRectangle({0.0f, cut.bottom, W, H}, cachedDimBrush_.Get());
        if (cut.left > 0.0f)
            d2dContext->FillRectangle({0.0f, cut.top, cut.left, cut.bottom}, cachedDimBrush_.Get());
        if (cut.right < W)
            d2dContext->FillRectangle({cut.right, cut.top, W, cut.bottom}, cachedDimBrush_.Get());
    };

    const auto& selectionModel = ui.selectionModel;
    if (selectionModel && selectionModel->HasSelection()) {
        // ── Active selection: dim outside, full brightness inside ───────────
        RectPx virtualRoi = selectionModel->GetCurrentRoi();
        int localX = virtualRoi.x - monitor.virtualBounds.x;
        int localY = virtualRoi.y - monitor.virtualBounds.y;
        D2D1_RECT_F selRect = D2D1::RectF(
            static_cast<float>(localX),
            static_cast<float>(localY),
            static_cast<float>(localX + virtualRoi.width),
            static_cast<float>(localY + virtualRoi.height));

        fillDim4(selRect);

        // Selection border
        d2dContext->DrawRectangle(selRect, cachedBorderBrush_.Get(), 2.0f);

        // Resize handles
        float hs = 4.0f;
        auto drawHandle = [&](float x, float y) {
            d2dContext->FillRectangle({x-hs, y-hs, x+hs, y+hs}, cachedBorderBrush_.Get());
        };
        float mx = (selRect.left + selRect.right)  * 0.5f;
        float my = (selRect.top  + selRect.bottom) * 0.5f;
        drawHandle(selRect.left,  selRect.top);
        drawHandle(mx,            selRect.top);
        drawHandle(selRect.right, selRect.top);
        drawHandle(selRect.right, my);
        drawHandle(selRect.right, selRect.bottom);
        drawHandle(mx,            selRect.bottom);
        drawHandle(selRect.left,  selRect.bottom);
        drawHandle(selRect.left,  my);

    } else if (drawHover) {
        // ── Hover / snap state ──────────────────────────────────────────────
        // Step 1: full-screen dim (always applied, works for any window size)
        d2dContext->FillRectangle({0.0f, 0.0f, W, H}, cachedDimBrush_.Get());

        // Step 2: "reveal" hover area — redraw the frozen screenshot clipped
        // to hwRect.  Opacity fades in with hoverAlpha_, so the hover area
        // smoothly returns to full original brightness as hoverAlpha_ → 1.
        // This handles maximized / full-screen windows correctly: fillDim4's
        // strict > / < checks break for rects that equal monitor bounds.
        if (bgBitmap && targetFrame) {
            D2D1_RECT_F destRect = D2D1::RectF(
                0.0f, 0.0f,
                static_cast<float>(targetFrame->width),
                static_cast<float>(targetFrame->height));
            d2dContext->PushAxisAlignedClip(hwRect, D2D1_ANTIALIAS_MODE_ALIASED);
            d2dContext->DrawBitmap(bgBitmap, &destRect, hoverAlpha_,
                D2D1_INTERPOLATION_MODE_LINEAR, nullptr);
            d2dContext->PopAxisAlignedClip();
        }

        // Step 3: neon cyan + blue double border (fades in with hoverAlpha_)
        cachedHoverOuterBrush_->SetOpacity(hoverAlpha_);
        cachedHoverInnerBrush_->SetOpacity(hoverAlpha_);
        d2dContext->DrawRectangle(hwRect, cachedHoverOuterBrush_.Get(), 5.0f);
        d2dContext->DrawRectangle(hwRect, cachedHoverInnerBrush_.Get(), 1.5f);

    } else {
        // ── No hover, no selection: full-screen dim ───────────────────────
        d2dContext->FillRectangle({0.0f, 0.0f, W, H}, cachedDimBrush_.Get());
    }

    // Draw Annotations and Toolbar
    D2D1_MATRIX_3X2_F oldTransform;
    d2dContext->GetTransform(&oldTransform);
    d2dContext->SetTransform(
        D2D1::Matrix3x2F::Translation(-static_cast<float>(monitor.virtualBounds.x), -static_cast<float>(monitor.virtualBounds.y))
        * oldTransform
    );

    if (ui.document) {
        // 马赛克需要背景位图做模糊源；其在标注坐标系（虚拟坐标）中的位置即
        // 当前显示器帧的虚拟矩形。
        const D2D1_RECT_F mosaicBgDest = (bgBitmap && targetFrame)
            ? D2D1::RectF(
                  static_cast<float>(monitor.virtualBounds.x),
                  static_cast<float>(monitor.virtualBounds.y),
                  static_cast<float>(monitor.virtualBounds.x + targetFrame->width),
                  static_cast<float>(monitor.virtualBounds.y + targetFrame->height))
            : D2D1_RECT_F{};
        ui.document->DrawAll(d2dContext, dwriteFactory_.Get(), bgBitmap, mosaicBgDest);
        
        // If Text Tool is active, check if hovered over any committed text box to draw hover border & close button!
        if (ui.toolbar && ui.toolbar->GetCurrentContext().activeTool == ToolType::Text) {
            auto hitText = ui.document->HitTestText(mousePt);
            if (hitText) {
                // Draw a beautiful subtle border around the hovered committed text box
                D2D1_RECT_F hoveredFrameRect = D2D1::RectF(
                    hitText->rect.left - 4.0f,
                    hitText->rect.top - 4.0f,
                    hitText->rect.right + 4.0f,
                    hitText->rect.bottom + 4.0f
                );
                
                d2dContext->DrawRectangle(hoveredFrameRect, cachedBorderBrush_.Get(), 1.0f);
                
                // Draw a close button at the top-right corner of the hovered committed text box
                float closeR = 8.0f;
                float closeX = hoveredFrameRect.right;
                float closeY = hoveredFrameRect.top;
                
                float dx = mousePt.x - closeX;
                float dy = mousePt.y - closeY;
                bool isCloseHovered = (dx * dx + dy * dy <= closeR * closeR * 1.5f);
                
                D2D1_ELLIPSE closeCircle = D2D1::Ellipse(D2D1::Point2F(closeX, closeY), closeR, closeR);
                
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> closeBgBrush;
                Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> closeFgBrush;
                
                D2D1_COLOR_F bgColor = isCloseHovered ? D2D1::ColorF(0.9f, 0.1f, 0.1f, 0.9f) : D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.6f);
                D2D1_COLOR_F fgColor = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.00f);
                
                if (SUCCEEDED(d2dContext->CreateSolidColorBrush(bgColor, &closeBgBrush)) &&
                    SUCCEEDED(d2dContext->CreateSolidColorBrush(fgColor, &closeFgBrush))) {
                    
                    d2dContext->FillEllipse(&closeCircle, closeBgBrush.Get());
                    d2dContext->DrawEllipse(&closeCircle, cachedBorderBrush_.Get(), 0.5f);
                    
                    float len = 3.0f;
                    d2dContext->DrawLine(
                        D2D1::Point2F(closeX - len, closeY - len),
                        D2D1::Point2F(closeX + len, closeY + len),
                        closeFgBrush.Get(),
                        1.5f
                    );
                    d2dContext->DrawLine(
                        D2D1::Point2F(closeX + len, closeY - len),
                        D2D1::Point2F(closeX - len, closeY + len),
                        closeFgBrush.Get(),
                        1.5f
                    );
                }
            }
        }
    }
    if (ui.currentDraft) {
        const D2D1_RECT_F draftBgDest = (bgBitmap && targetFrame)
            ? D2D1::RectF(
                  static_cast<float>(monitor.virtualBounds.x),
                  static_cast<float>(monitor.virtualBounds.y),
                  static_cast<float>(monitor.virtualBounds.x + targetFrame->width),
                  static_cast<float>(monitor.virtualBounds.y + targetFrame->height))
            : D2D1_RECT_F{};
        ui.currentDraft->Draw(d2dContext, dwriteFactory_.Get(), bgBitmap, draftBgDest);
    }
    if (ui.toolbar && ui.selectionModel && ui.selectionModel->GetState() == SelectionModel::State::Selected) {
        ui.toolbar->Draw(d2dContext, bgBitmap);
    }

    d2dContext->SetTransform(oldTransform);

    // Draw Custom D2D Cursor to bypass hardware cursor vs DWM compositing lag
    // Show crosshair in None (hover), Creating, Moving, and Resizing states
    if (ui.selectionModel) {
        auto state = ui.selectionModel->GetState();
        if (state == SelectionModel::State::None ||
            state == SelectionModel::State::Creating || 
            state == SelectionModel::State::Moving || 
            state == SelectionModel::State::Resizing) 
        {
            float cx = static_cast<float>(ui.cursorPos.x - monitor.virtualBounds.x);
            float cy = static_cast<float>(ui.cursorPos.y - monitor.virtualBounds.y);

            // Only draw if cursor is within this monitor's bounds
            if (cx >= 0.0f && cx < static_cast<float>(monitor.virtualBounds.width) &&
                cy >= 0.0f && cy < static_cast<float>(monitor.virtualBounds.height))
            {
                float arm   = 13.0f;  // white arm half-length → total span 26px
                float extra = 1.0f;   // black extends 1px past each white tip to cap it

                // Switch to aliased mode: pixel-perfect, no gray anti-alias fringe
                d2dContext->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);

                // --- Pure black outline (3px) ---
                // Extend by `extra` on every end so the black fully caps the white tips
                d2dContext->DrawLine(D2D1::Point2F(cx - arm - extra, cy), D2D1::Point2F(cx + arm + extra, cy), cachedTextBgBrush_.Get(), 3.0f);
                d2dContext->DrawLine(D2D1::Point2F(cx, cy - arm - extra), D2D1::Point2F(cx, cy + arm + extra), cachedTextBgBrush_.Get(), 3.0f);

                // --- Pure white center (1px) on top ---
                d2dContext->DrawLine(D2D1::Point2F(cx - arm, cy), D2D1::Point2F(cx + arm, cy), cachedTextBrush_.Get(), 1.0f);
                d2dContext->DrawLine(D2D1::Point2F(cx, cy - arm), D2D1::Point2F(cx, cy + arm), cachedTextBrush_.Get(), 1.0f);

                // Restore default antialias mode for subsequent rendering
                d2dContext->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            }
        }
    }

    // Draw Alt Guidelines
    if (ui.isAltPressed) {
        float cx = static_cast<float>(ui.cursorPos.x - monitor.virtualBounds.x);
        float cy = static_cast<float>(ui.cursorPos.y - monitor.virtualBounds.y);
        float monitorWidth = static_cast<float>(monitor.virtualBounds.width);
        float monitorHeight = static_cast<float>(monitor.virtualBounds.height);

        // 1. Mouse Crosshair Guidelines
        d2dContext->DrawLine(D2D1::Point2F(0.0f, cy + 0.5f), D2D1::Point2F(monitorWidth, cy + 0.5f), cachedCrosshairBrush_.Get(), 1.0f);
        d2dContext->DrawLine(D2D1::Point2F(cx + 0.5f, 0.0f), D2D1::Point2F(cx + 0.5f, monitorHeight), cachedCrosshairBrush_.Get(), 1.0f);

        // 2. Selection Alignment & Center Guidelines
        if (ui.selectionModel && ui.selectionModel->HasSelection()) {
            RectPx virtualRoi = ui.selectionModel->GetCurrentRoi();
            int localX = virtualRoi.x - monitor.virtualBounds.x;
            int localY = virtualRoi.y - monitor.virtualBounds.y;

            D2D1_RECT_F selRect = D2D1::RectF(
                static_cast<float>(localX),
                static_cast<float>(localY),
                static_cast<float>(localX + virtualRoi.width),
                static_cast<float>(localY + virtualRoi.height)
            );

            // Left, Top, Right, Bottom Borders extending across screen
            d2dContext->DrawLine(D2D1::Point2F(0.0f, selRect.top + 0.5f), D2D1::Point2F(monitorWidth, selRect.top + 0.5f), cachedCrosshairBrush_.Get(), 1.0f);
            d2dContext->DrawLine(D2D1::Point2F(0.0f, selRect.bottom + 0.5f), D2D1::Point2F(monitorWidth, selRect.bottom + 0.5f), cachedCrosshairBrush_.Get(), 1.0f);
            d2dContext->DrawLine(D2D1::Point2F(selRect.left + 0.5f, 0.0f), D2D1::Point2F(selRect.left + 0.5f, monitorHeight), cachedCrosshairBrush_.Get(), 1.0f);
            d2dContext->DrawLine(D2D1::Point2F(selRect.right + 0.5f, 0.0f), D2D1::Point2F(selRect.right + 0.5f, monitorHeight), cachedCrosshairBrush_.Get(), 1.0f);

            // Selection Center Crosshairs
            float midX = selRect.left + (selRect.right - selRect.left) / 2.0f;
            float midY = selRect.top + (selRect.bottom - selRect.top) / 2.0f;
            
            d2dContext->DrawLine(D2D1::Point2F(0.0f, midY + 0.5f), D2D1::Point2F(monitorWidth, midY + 0.5f), cachedCrosshairBrush_.Get(), 1.0f);
            d2dContext->DrawLine(D2D1::Point2F(midX + 0.5f, 0.0f), D2D1::Point2F(midX + 0.5f, monitorHeight), cachedCrosshairBrush_.Get(), 1.0f);
        }
    }

    if (ui.selectionModel && targetFrame && bgBitmap) {
        auto state = ui.selectionModel->GetState();
        // Smart visibility: Hide magnifier during drag-selection (Creating state)
        bool showLoupe = (state == SelectionModel::State::None || 
                          state == SelectionModel::State::Resizing);
        
        if (showLoupe) {
            // Check if cursor is roughly within this monitor
            if (ui.cursorPos.x >= monitor.virtualBounds.x - 20 && ui.cursorPos.x < monitor.virtualBounds.x + monitor.virtualBounds.width + 20 &&
                ui.cursorPos.y >= monitor.virtualBounds.y - 20 && ui.cursorPos.y < monitor.virtualBounds.y + monitor.virtualBounds.height + 20) {
                RenderLoupe(d2dContext, bgBitmap, monitor, *targetFrame, ui);
            }
        }
    }

    if (ui.isEditingText && ui.textLayout) {
        // Map textRect to monitor-local coordinates to support robust multi-monitor layouts
        D2D1_RECT_F localTextRect = D2D1::RectF(
            ui.textRect.left - monitor.virtualBounds.x,
            ui.textRect.top - monitor.virtualBounds.y,
            ui.textRect.right - monitor.virtualBounds.x,
            ui.textRect.bottom - monitor.virtualBounds.y
        );

        // Draw a beautiful subtle dashed Fluent frame border around localTextRect
        // Pad by 4 pixels to make it look premium
        D2D1_RECT_F frameRect = D2D1::RectF(
            localTextRect.left - 4.0f,
            localTextRect.top - 4.0f,
            localTextRect.right + 4.0f,
            localTextRect.bottom + 4.0f
        );
        
        d2dContext->DrawRectangle(frameRect, cachedBorderBrush_.Get(), 1.0f);
        
        // Draw a small resize knob (knurled circle) on the right edge center
        float knobX = localTextRect.right + 4.0f;
        float knobY = (localTextRect.top + localTextRect.bottom) / 2.0f;
        D2D1_ELLIPSE knob = D2D1::Ellipse(D2D1::Point2F(knobX, knobY), 4.0f, 4.0f);
        
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> whiteBrush;
        d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &whiteBrush);
        if (whiteBrush) {
            d2dContext->FillEllipse(&knob, whiteBrush.Get());
        }
        d2dContext->DrawEllipse(&knob, cachedBorderBrush_.Get(), 1.0f);
        
        // Draw active text layout
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> textBrush;
        if (SUCCEEDED(d2dContext->CreateSolidColorBrush(ui.textColor, &textBrush))) {
            IDWriteTextLayout* activeLayout = ui.textLayout;
            
            // Draw selection highlight behind text
            if (ui.selectionAnchor != ui.selectionActive && !ui.activeText.empty()) {
                size_t start = std::min(ui.selectionAnchor, ui.selectionActive);
                size_t end = std::max(ui.selectionAnchor, ui.selectionActive);
                if (end > ui.activeText.length()) end = ui.activeText.length();
                if (start < end) {
                    UINT32 selStart = static_cast<UINT32>(start);
                    UINT32 selLen = static_cast<UINT32>(end - start);
                    
                    UINT32 maxRects = 32;
                    std::vector<DWRITE_HIT_TEST_METRICS> hitMetrics(maxRects);
                    UINT32 actualRectCount = 0;
                    
                    if (SUCCEEDED(activeLayout->HitTestTextRange(
                            selStart,
                            selLen,
                            0.0f,
                            0.0f,
                            hitMetrics.data(),
                            maxRects,
                            &actualRectCount))) {
                        
                        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> highlightBrush;
                        if (SUCCEEDED(d2dContext->CreateSolidColorBrush(
                                D2D1::ColorF(0.0f, 0.47f, 0.83f, 0.28f),
                                &highlightBrush))) {
                            
                            for (UINT32 i = 0; i < actualRectCount; ++i) {
                                D2D1_RECT_F highlightRect = D2D1::RectF(
                                    localTextRect.left + hitMetrics[i].left,
                                    localTextRect.top + hitMetrics[i].top,
                                    localTextRect.left + hitMetrics[i].left + hitMetrics[i].width,
                                    localTextRect.top + hitMetrics[i].top + hitMetrics[i].height
                                );
                                d2dContext->FillRectangle(highlightRect, highlightBrush.Get());
                            }
                        }
                    }
                }
            }

            // Draw a subtle dotted underline under the pre-edit composition pinyin string
            if (!ui.imeCompositionString.empty()) {
                UINT32 compStart = static_cast<UINT32>(ui.caretIndex);
                UINT32 compLen = static_cast<UINT32>(ui.imeCompositionString.length());
                
                UINT32 maxRects = 16;
                std::vector<DWRITE_HIT_TEST_METRICS> hitMetrics(maxRects);
                UINT32 actualRectCount = 0;
                
                if (SUCCEEDED(activeLayout->HitTestTextRange(
                        compStart,
                        compLen,
                        0.0f,
                        0.0f,
                        hitMetrics.data(),
                        maxRects,
                        &actualRectCount))) {
                    
                    for (UINT32 i = 0; i < actualRectCount; ++i) {
                        float lineY = localTextRect.top + hitMetrics[i].top + hitMetrics[i].height - 1.0f;
                        float lineX1 = localTextRect.left + hitMetrics[i].left;
                        float lineX2 = lineX1 + hitMetrics[i].width;
                        
                        d2dContext->DrawLine(
                            D2D1::Point2F(lineX1, lineY),
                            D2D1::Point2F(lineX2, lineY),
                            textBrush.Get(),
                            1.0f
                        );
                    }
                }
            }

            d2dContext->DrawTextLayout(
                D2D1::Point2F(localTextRect.left, localTextRect.top),
                activeLayout,
                textBrush.Get()
            );
            
            // Draw blinking caret at the visual caret index (end of pinyin)
            if (ui.caretVisible) {
                float caretX = localTextRect.left;
                float caretY = localTextRect.top;
                float caretH = ui.fontSize;
                
                std::wstring displayText = ui.activeText.substr(0, ui.caretIndex) + ui.imeCompositionString + ui.activeText.substr(ui.caretIndex);
                if (!displayText.empty()) {
                    DWRITE_HIT_TEST_METRICS metrics = {};
                    float localX = 0, localY = 0;
                    size_t visualCaret = ui.caretIndex + ui.imeCompositionString.length();
                    UINT32 pos = (visualCaret > 0) ? static_cast<UINT32>(visualCaret - 1) : 0;
                    BOOL trail = (visualCaret > 0) ? TRUE : FALSE;
                    
                    activeLayout->HitTestTextPosition(pos, trail, &localX, &localY, &metrics);
                    caretX = localTextRect.left + localX;
                    caretY = localTextRect.top + localY;
                    caretH = metrics.height;
                }
                
                d2dContext->DrawLine(
                    D2D1::Point2F(caretX, caretY),
                    D2D1::Point2F(caretX, caretY + caretH),
                    textBrush.Get(),
                    1.5f
                );
            }

            // Draw a close button at the top-right corner of the active textbox frame
            float closeR = 8.0f;
            float closeX = frameRect.right;
            float closeY = frameRect.top;
            
            float dx = mouseX - closeX;
            float dy = mouseY - closeY;
            bool isCloseHovered = (dx * dx + dy * dy <= closeR * closeR * 1.5f);
            
            D2D1_ELLIPSE closeCircle = D2D1::Ellipse(D2D1::Point2F(closeX, closeY), closeR, closeR);
            
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> closeBgBrush;
            Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> closeFgBrush;
            
            D2D1_COLOR_F bgColor = isCloseHovered ? D2D1::ColorF(0.9f, 0.1f, 0.1f, 0.9f) : D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.6f);
            D2D1_COLOR_F fgColor = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.00f);
            
            if (SUCCEEDED(d2dContext->CreateSolidColorBrush(bgColor, &closeBgBrush)) &&
                SUCCEEDED(d2dContext->CreateSolidColorBrush(fgColor, &closeFgBrush))) {
                
                d2dContext->FillEllipse(&closeCircle, closeBgBrush.Get());
                d2dContext->DrawEllipse(&closeCircle, cachedBorderBrush_.Get(), 0.5f);
                
                float len = 3.0f;
                d2dContext->DrawLine(
                    D2D1::Point2F(closeX - len, closeY - len),
                    D2D1::Point2F(closeX + len, closeY + len),
                    closeFgBrush.Get(),
                    1.5f
                );
                d2dContext->DrawLine(
                    D2D1::Point2F(closeX + len, closeY - len),
                    D2D1::Point2F(closeX - len, closeY + len),
                    closeFgBrush.Get(),
                    1.5f
                );
            }
        }
    }

    d2dContext->EndDraw();
}

void OverlayRenderer::RenderLoupe(
    ID2D1DeviceContext* d2dContext,
    ID2D1Bitmap* backgroundBitmap,
    const MonitorSnapshot& monitor,
    const MonitorFrame& frame,
    const UiState& ui)
{
    PointPx cursorPos = ui.cursorPos;
    int localX = cursorPos.x - monitor.virtualBounds.x;
    int localY = cursorPos.y - monitor.virtualBounds.y;

    // DPI Scaling
    float dpiScale = monitor.dpiX / 96.0f;
    float loupeSize = 120.0f * dpiScale;
    float halfSize = loupeSize / 2.0f;
    float cropRadius = halfSize / loupeScale_;

    // Center of loupe offset to the bottom-right of the cursor
    float cx = static_cast<float>(localX) + halfSize + 15.0f * dpiScale;
    float cy = static_cast<float>(localY) + halfSize + 15.0f * dpiScale;

    // Check boundary constraints and flip/adjust if it exceeds monitor bounds
    if (cx + halfSize > frame.width) {
        cx = static_cast<float>(localX) - halfSize - 15.0f * dpiScale;
    }
    if (cy + halfSize + 35.0f * dpiScale > frame.height) {
        cy = static_cast<float>(localY) - halfSize - 15.0f * dpiScale;
    }

    D2D1_RECT_F destRect = D2D1::RectF(cx - halfSize, cy - halfSize, cx + halfSize, cy + halfSize);
    D2D1_RECT_F srcRect = D2D1::RectF(
        static_cast<float>(localX) - cropRadius,
        static_cast<float>(localY) - cropRadius,
        static_cast<float>(localX) + cropRadius,
        static_cast<float>(localY) + cropRadius
    );

    // Draw Loupe Background (Image)
    d2dContext->DrawBitmap(
        backgroundBitmap,
        &destRect,
        1.0f,
        D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR,
        &srcRect
    );

    // Draw Loupe Border
    d2dContext->DrawRectangle(destRect, cachedLoupeBorderBrush_.Get(), 2.0f * dpiScale);

    // Draw Crosshair
    float lx = cx + 0.5f;
    float ly = cy + 0.5f;
    d2dContext->DrawLine(D2D1::Point2F(lx, cy - halfSize), D2D1::Point2F(lx, cy + halfSize), cachedCrosshairBrush_.Get(), 1.0f * dpiScale);
    d2dContext->DrawLine(D2D1::Point2F(cx - halfSize, ly), D2D1::Point2F(cx + halfSize, ly), cachedCrosshairBrush_.Get(), 1.0f * dpiScale);

    // Get color at cursor
    int clampX = std::max(0, std::min(localX, frame.width - 1));
    int clampY = std::max(0, std::min(localY, frame.height - 1));
    int pixelIndex = clampY * frame.stride + clampX * 4;

    std::byte b = frame.bgra8[pixelIndex];
    std::byte g = frame.bgra8[pixelIndex + 1];
    std::byte r = frame.bgra8[pixelIndex + 2];

    int rVal = static_cast<int>(r);
    int gVal = static_cast<int>(g);
    int bVal = static_cast<int>(b);

    wchar_t colorText[128];
    auto format = ui.colorFormat;

    if (format == InteractionStateMachine::ColorFormat::RGB) {
        swprintf_s(colorText, L"RGB: %d, %d, %d", rVal, gVal, bVal);
    } else if (format == InteractionStateMachine::ColorFormat::Hex) {
        swprintf_s(colorText, L"#%02X%02X%02X", rVal, gVal, bVal);
    } else if (format == InteractionStateMachine::ColorFormat::HSV) {
        int h, s, v;
        float rf = rVal / 255.0f;
        float gf = gVal / 255.0f;
        float bf = bVal / 255.0f;
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
        swprintf_s(colorText, L"HSV: %d, %d%%, %d%%", h, s, v);
    } else if (format == InteractionStateMachine::ColorFormat::HSL) {
        int h, s, l;
        float rf = rVal / 255.0f;
        float gf = gVal / 255.0f;
        float bf = bVal / 255.0f;
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
        swprintf_s(colorText, L"HSL: %d, %d%%, %d%%", h, s, l);
    }

    if (textFormat_ && dwriteFactory_) {
        // Draw Text Background
        float textTop = cy + halfSize + 5.0f;
        float textBottom = textTop + 30.0f * dpiScale;
        
        // If text goes off the bottom of the screen, move it above the loupe
        if (textBottom > frame.height) {
            textBottom = cy - halfSize - 5.0f;
            textTop = textBottom - 30.0f * dpiScale;
        }

        D2D1_RECT_F textBgRect = D2D1::RectF(cx - halfSize, textTop, cx + halfSize, textBottom);
        d2dContext->FillRectangle(textBgRect, cachedTextBgBrush_.Get());

        // Recreate the textFormat_ dynamically using scaled sizes for crisp drawing on high-DPI screens
        Microsoft::WRL::ComPtr<IDWriteTextFormat> localFormat;
        HRESULT hr = dwriteFactory_->CreateTextFormat(
            L"Segoe UI",
            nullptr,
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            11.0f * dpiScale,
            L"en-us",
            &localFormat
        );
        if (SUCCEEDED(hr) && localFormat) {
            localFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            localFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

            // Draw Text
            d2dContext->DrawTextW(
                colorText,
                (UINT32)wcslen(colorText),
                localFormat.Get(),
                &textBgRect,
                cachedTextBrush_.Get()
            );
        }
    }
}
