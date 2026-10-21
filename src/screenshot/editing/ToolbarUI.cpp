#include "ToolbarUI.h"
#include <wrl/client.h>
#include <d2d1effects.h>
#include <dwrite.h>
#include <windows.h>

ToolbarUI::ToolbarUI() {
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(dwriteFactory_.GetAddressOf()));
    
    if (dwriteFactory_) {
        dwriteFactory_->CreateTextFormat(
            L"Segoe MDL2 Assets", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            14.0f, L"en-us", &iconTextFormat_);
        if (iconTextFormat_) {
            iconTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            iconTextFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }

        dwriteFactory_->CreateTextFormat(
            L"Microsoft YaHei", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            12.0f, L"zh-cn", &tooltipTextFormat_);
        if (tooltipTextFormat_) {
            tooltipTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            tooltipTextFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }

        dwriteFactory_->CreateTextFormat(
            L"Microsoft YaHei", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            12.0f, L"zh-cn", &fontOptionTextFormat_);
        if (fontOptionTextFormat_) {
            fontOptionTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            fontOptionTextFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
    }
}

void ToolbarUI::EnsureBrushes(ID2D1DeviceContext* context) {
    if (cachedTextBrush_) return; // Already initialized

    context->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &cachedTextBrush_);
    context->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.47f, 0.83f, 0.75f), &cachedHighlightBrush_);
    context->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.12f), &cachedHoverBrush_);
    context->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.15f), &cachedBorderBrush_);
    context->CreateSolidColorBrush(D2D1::ColorF(0.08f, 0.08f, 0.08f, 0.65f), &cachedGlassTintBrush_);
    context->CreateSolidColorBrush(D2D1::ColorF(0,0,0), &cachedColorButtonBrush_);
    context->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.95f), &cachedTooltipBgBrush_);
}

void ToolbarUI::SetSelectionRect(D2D1_RECT_F selRect) {
    selectionRect_ = selRect;
    UpdateLayout();
}

void ToolbarUI::SetCurrentContext(const ToolContext& ctx) {
    bool needLayout = (currentContext_.activeTool != ctx.activeTool);
    currentContext_ = ctx;
    if (needLayout) {
        UpdateLayout();
    }
}

void ToolbarUI::UpdateHover(D2D1_POINT_2F pt) {
    hoverPt_ = pt;
}

bool ToolbarUI::IsDrawingTool(ToolType type) const {
    return type == ToolType::Rect || type == ToolType::Ellipse || 
           type == ToolType::Line || type == ToolType::Arrow || 
           type == ToolType::Freehand || type == ToolType::Mosaic || 
           type == ToolType::Text;
}

void ToolbarUI::UpdateLayout() {
    primaryButtons_.clear();
    colorButtons_.clear();
    strokeButtons_.clear();
    
    float btnSize = 32.0f;
    float padding = 8.0f;
    float iconGap = 4.0f;
    
    struct ToolDef { ToolType type; std::wstring icon; bool isSep = false; };
    std::vector<ToolDef> tools = {
        {ToolType::Rect, L"\xE739"},      // Hollow rectangle
        {ToolType::Ellipse, L"\xE81B"},   // Hollow circle
        {ToolType::Line, L"\xE76A"},      // Diagonal line
        {ToolType::Arrow, L"\xE7C2"},     // Arrow
        {ToolType::Freehand, L"\xE70F"},  // Pen/draw
        {ToolType::Mosaic, L"\xE80A"},    // Grid/mosaic
        {ToolType::Text, L"\xE8D2"},      // Text (A)
        {ToolType::None, L"", true}, // Separator
        {ToolType::Undo, L"\xE7A7"},
        {ToolType::Redo, L"\xE7A6"},
        {ToolType::Pin, L"\xE718"},
        {ToolType::Ocr, L"\xE8C3"},       // OCR / document text
        {ToolType::None, L"", true}, // Separator
        {ToolType::Save, L"\xE74E"},
        {ToolType::Cancel, L"\xE711"},
        {ToolType::Done, L"\xE73E"}
    };
    
    float tbWidth = padding * 2;
    for (const auto& t : tools) {
        if (t.isSep) tbWidth += padding * 2;
        else tbWidth += btnSize + iconGap;
    }
    tbWidth -= iconGap; // remove last gap
    
    float tbHeight = btnSize + 2 * padding;
    
    float screenHeight = static_cast<float>(GetSystemMetrics(SM_CYVIRTUALSCREEN));
    if (screenHeight == 0) screenHeight = static_cast<float>(GetSystemMetrics(SM_CYSCREEN));
    
    float startX = selectionRect_.right - tbWidth;
    
    // Clamp to left screen edge
    float screenLeft = static_cast<float>(GetSystemMetrics(SM_XVIRTUALSCREEN));
    if (startX < screenLeft) startX = screenLeft;

    float startY = selectionRect_.bottom + 10.0f;
    
    float subTbHeight = 40.0f;
    float totalHeightNeeded = tbHeight + (IsDrawingTool(currentContext_.activeTool) ? subTbHeight + 5.0f : 0);
    
    if (startY + totalHeightNeeded > screenHeight) {
        startY = selectionRect_.bottom - totalHeightNeeded - 10.0f;
        if (startY < selectionRect_.top) {
            startY = selectionRect_.bottom + 10.0f; 
        }
    }
    
    primaryBounds_ = D2D1::RectF(startX, startY, startX + tbWidth, startY + tbHeight);
    
    float x = startX + padding;
    float y = startY + padding;
    
    for (const auto& t : tools) {
        if (t.isSep) {
            primaryButtons_.push_back({ D2D1::RectF(x, y, x + 1.0f, y + btnSize), L"", ToolType::None, true });
            x += padding * 2;
        } else {
            primaryButtons_.push_back({ D2D1::RectF(x, y, x + btnSize, y + btnSize), t.icon, t.type, false });
            x += btnSize + iconGap;
        }
    }
    
    fontOptionButtons_.clear();
    if (IsDrawingTool(currentContext_.activeTool)) {
        float subY = startY + tbHeight + 5.0f;
        float subX = startX;
        
        std::vector<D2D1_COLOR_F> colors = {
            D2D1::ColorF(D2D1::ColorF::Red),
            D2D1::ColorF(D2D1::ColorF::Orange),
            D2D1::ColorF(D2D1::ColorF::Yellow),
            D2D1::ColorF(D2D1::ColorF::Green),
            D2D1::ColorF(D2D1::ColorF::Blue),
            D2D1::ColorF(D2D1::ColorF::Purple),
            D2D1::ColorF(D2D1::ColorF::Black),
            D2D1::ColorF(D2D1::ColorF::White),
            D2D1::ColorF(D2D1::ColorF::Gray),
            D2D1::ColorF(D2D1::ColorF::Pink)
        };

        if (currentContext_.activeTool == ToolType::Text) {
            float sizeBtnW = 85.0f;
            float styleBtnW = 32.0f;
            float fontColSpace = static_cast<float>(colors.size()) * (24.0f + 4.0f);
            
            float subTbWidth = padding * 2 + sizeBtnW + padding + (2 * styleBtnW + 4.0f) + padding + fontColSpace + padding * 2;
            subBounds_ = D2D1::RectF(subX, subY, subX + subTbWidth, subY + subTbHeight);
            
            float cx = subX + padding;
            float cy = subY + (subTbHeight - btnSize) / 2.0f;
            
            fontSizeBtnBounds_ = D2D1::RectF(cx, cy, cx + sizeBtnW, cy + btnSize);
            cx += sizeBtnW + padding;
            
            // Calculate sizeDropdownBounds_
            float dropLeft = fontSizeBtnBounds_.left;
            float dropTop = fontSizeBtnBounds_.bottom + 2.0f;
            float dropWidth = sizeBtnW;
            float dropHeight = 10 * 20.0f; // 10 sizes, 20px each
            if (dropTop + dropHeight > screenHeight) {
                dropTop = fontSizeBtnBounds_.top - dropHeight - 2.0f;
            }
            sizeDropdownBounds_ = D2D1::RectF(dropLeft, dropTop, dropLeft + dropWidth, dropTop + dropHeight);
            
            boldBtnBounds_ = D2D1::RectF(cx, cy, cx + styleBtnW, cy + btnSize);
            cx += styleBtnW + 4.0f;
            
            italicBtnBounds_ = D2D1::RectF(cx, cy, cx + styleBtnW, cy + btnSize);
            cx += styleBtnW + padding;
            
            cy = subY + (subTbHeight - 24.0f) / 2.0f;
            for (const auto& c : colors) {
                colorButtons_.push_back({ D2D1::RectF(cx, cy, cx + 24.0f, cy + 24.0f), c });
                cx += 28.0f;
            }
        } else {
            float subTbWidth = 3 * 32.0f + padding * 2 + 10.0f + static_cast<float>(colors.size()) * (24.0f + 4.0f) + padding * 2;
            subBounds_ = D2D1::RectF(subX, subY, subX + subTbWidth, subY + subTbHeight);
            
            float cx = subX + padding;
            float cy = subY + (subTbHeight - 32.0f) / 2.0f;
            
            strokeButtons_.push_back({ D2D1::RectF(cx, cy, cx + 32.0f, cy + 32.0f), 2.0f, 2.0f }); cx += 36.0f;
            strokeButtons_.push_back({ D2D1::RectF(cx, cy, cx + 32.0f, cy + 32.0f), 4.0f, 4.0f }); cx += 36.0f;
            strokeButtons_.push_back({ D2D1::RectF(cx, cy, cx + 32.0f, cy + 32.0f), 8.0f, 6.0f }); cx += 36.0f + padding;
            
            cy = subY + (subTbHeight - 24.0f) / 2.0f;
            for (const auto& c : colors) {
                colorButtons_.push_back({ D2D1::RectF(cx, cy, cx + 24.0f, cy + 24.0f), c });
                cx += 28.0f;
            }
        }
    } else {
        subBounds_ = D2D1::RectF();
    }
}

void ToolbarUI::DrawRoundedRect(ID2D1DeviceContext* context, D2D1_RECT_F rect, ID2D1Brush* brush, ID2D1Brush* borderBrush, float strokeWidth, float radius) {
    D2D1_ROUNDED_RECT rrect = D2D1::RoundedRect(rect, radius, radius);
    if (brush) context->FillRoundedRectangle(&rrect, brush);
    if (borderBrush && strokeWidth > 0) context->DrawRoundedRectangle(&rrect, borderBrush, strokeWidth);
}

void ToolbarUI::Draw(ID2D1DeviceContext* context, ID2D1Bitmap1* bgBitmap) {
    if (primaryButtons_.empty()) return;
    
    EnsureBrushes(context);
    
    auto drawGlassBackground = [&](D2D1_RECT_F bounds, float radius) {
        if (bgBitmap) {
            Microsoft::WRL::ComPtr<ID2D1Effect> blurEffect;
            if (SUCCEEDED(context->CreateEffect(CLSID_D2D1GaussianBlur, &blurEffect))) {
                blurEffect->SetInput(0, bgBitmap);
                blurEffect->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, 20.0f);
                
                Microsoft::WRL::ComPtr<ID2D1Factory> factory;
                context->GetFactory(&factory);
                Microsoft::WRL::ComPtr<ID2D1RoundedRectangleGeometry> roundedGeo;
                D2D1_ROUNDED_RECT rrect = D2D1::RoundedRect(bounds, radius, radius);
                factory->CreateRoundedRectangleGeometry(&rrect, &roundedGeo);
                
                D2D1_LAYER_PARAMETERS params = D2D1::LayerParameters(
                    D2D1::InfiniteRect(),
                    roundedGeo.Get(),
                    D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                    D2D1::IdentityMatrix(),
                    1.0f,
                    nullptr,
                    D2D1_LAYER_OPTIONS_NONE
                );
                Microsoft::WRL::ComPtr<ID2D1Layer> layer;
                if (SUCCEEDED(context->CreateLayer(&layer))) {
                    context->PushLayer(&params, layer.Get());
                    context->DrawImage(blurEffect.Get());
                    context->PopLayer();
                }
            }
        }
        
        // Draw the glass overlay dark tint
        DrawRoundedRect(context, bounds, cachedGlassTintBrush_.Get(), cachedBorderBrush_.Get(), 1.0f, radius);
    };
    
    drawGlassBackground(primaryBounds_, 10.0f);
    
    auto ptInRect = [](D2D1_POINT_2F p, D2D1_RECT_F r) {
        return p.x >= r.left && p.x <= r.right && p.y >= r.top && p.y <= r.bottom;
    };
    
    for (const auto& btn : primaryButtons_) {
        if (btn.isSeparator) {
            D2D1_RECT_F sepRect = btn.bounds;
            sepRect.top += 8.0f; sepRect.bottom -= 8.0f;
            context->FillRectangle(sepRect, cachedBorderBrush_.Get());
            continue;
        }
        
        bool isHover = ptInRect(hoverPt_, btn.bounds);
        bool isActive = (btn.action == currentContext_.activeTool);
        
        if (isActive) {
            DrawRoundedRect(context, btn.bounds, cachedHighlightBrush_.Get(), nullptr, 0, 6.0f);
        } else if (isHover) {
            DrawRoundedRect(context, btn.bounds, cachedHoverBrush_.Get(), nullptr, 0, 6.0f);
        }
        
        if (iconTextFormat_) {
            context->DrawText(btn.icon.c_str(), static_cast<UINT32>(btn.icon.length()), iconTextFormat_.Get(), btn.bounds, cachedTextBrush_.Get());
        }
    }
    
    if (IsDrawingTool(currentContext_.activeTool)) {
        drawGlassBackground(subBounds_, 10.0f);
        
        if (currentContext_.activeTool == ToolType::Text) {
            // Draw Size Dropdown Button
            bool sizeHover = ptInRect(hoverPt_, fontSizeBtnBounds_);
            if (sizeHover) {
                DrawRoundedRect(context, fontSizeBtnBounds_, cachedHoverBrush_.Get(), nullptr, 0, 6.0f);
            }
            // Draw a subtle border around the dropdown button to make it look premium
            DrawRoundedRect(context, fontSizeBtnBounds_, nullptr, cachedBorderBrush_.Get(), 0.5f, 6.0f);

            if (fontOptionTextFormat_) {
                std::wstring label = std::to_wstring(static_cast<int>(currentContext_.fontSize)) + L" px \u25BC";
                context->DrawTextW(label.c_str(), static_cast<UINT32>(label.length()), fontOptionTextFormat_.Get(), fontSizeBtnBounds_, cachedTextBrush_.Get());
            }
            
            // Draw Bold (B) Button
            bool boldHover = ptInRect(hoverPt_, boldBtnBounds_);
            bool boldActive = currentContext_.isBold;
            if (boldActive) {
                DrawRoundedRect(context, boldBtnBounds_, cachedHighlightBrush_.Get(), nullptr, 0, 6.0f);
            } else if (boldHover) {
                DrawRoundedRect(context, boldBtnBounds_, cachedHoverBrush_.Get(), nullptr, 0, 6.0f);
            }
            Microsoft::WRL::ComPtr<IDWriteTextFormat> boldFormat = fontOptionTextFormat_;
            if (fontOptionTextFormat_) {
                dwriteFactory_->CreateTextFormat(L"Microsoft YaHei", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"zh-cn", &boldFormat);
                if (boldFormat) {
                    boldFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    boldFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                    context->DrawTextW(L"B", 1, boldFormat.Get(), boldBtnBounds_, cachedTextBrush_.Get());
                }
            }
            
            // Draw Italic (I) Button
            bool italicHover = ptInRect(hoverPt_, italicBtnBounds_);
            bool italicActive = currentContext_.isItalic;
            if (italicActive) {
                DrawRoundedRect(context, italicBtnBounds_, cachedHighlightBrush_.Get(), nullptr, 0, 6.0f);
            } else if (italicHover) {
                DrawRoundedRect(context, italicBtnBounds_, cachedHoverBrush_.Get(), nullptr, 0, 6.0f);
            }
            Microsoft::WRL::ComPtr<IDWriteTextFormat> italicFormat = fontOptionTextFormat_;
            if (fontOptionTextFormat_) {
                dwriteFactory_->CreateTextFormat(L"Microsoft YaHei", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_ITALIC, DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"zh-cn", &italicFormat);
                if (italicFormat) {
                    italicFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                    italicFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                    context->DrawTextW(L"I", 1, italicFormat.Get(), italicBtnBounds_, cachedTextBrush_.Get());
                }
            }
        } else {
            for (const auto& sb : strokeButtons_) {
                bool isHover = ptInRect(hoverPt_, sb.bounds);
                bool isActive = (sb.strokeWidth == currentContext_.strokeWidth);
                
                if (isActive) DrawRoundedRect(context, sb.bounds, cachedHighlightBrush_.Get(), nullptr, 0, 6.0f);
                else if (isHover) DrawRoundedRect(context, sb.bounds, cachedHoverBrush_.Get(), nullptr, 0, 6.0f);
                
                D2D1_POINT_2F center = { (sb.bounds.left + sb.bounds.right)/2, (sb.bounds.top + sb.bounds.bottom)/2 };
                Microsoft::WRL::ComPtr<ID2D1EllipseGeometry> ell;
                Microsoft::WRL::ComPtr<ID2D1Factory> factory;
                context->GetFactory(&factory);
                D2D1_ELLIPSE d2dEll = D2D1::Ellipse(center, sb.displayRadius, sb.displayRadius);
                factory->CreateEllipseGeometry(&d2dEll, &ell);
                context->FillGeometry(ell.Get(), cachedTextBrush_.Get());
            }
        }
        
        for (const auto& cb : colorButtons_) {
            cachedColorButtonBrush_->SetColor(cb.color);
            bool isHover = ptInRect(hoverPt_, cb.bounds);
            
            auto colorEquals = [](const D2D1_COLOR_F& a, const D2D1_COLOR_F& b) {
                float epsilon = 0.001f;
                return std::abs(a.r - b.r) < epsilon && std::abs(a.g - b.g) < epsilon && std::abs(a.b - b.b) < epsilon;
            };
            bool isActive = colorEquals(cb.color, currentContext_.color);
            
            if (isActive) DrawRoundedRect(context, cb.bounds, cachedColorButtonBrush_.Get(), cachedTextBrush_.Get(), 2.0f, 6.0f);
            else DrawRoundedRect(context, cb.bounds, cachedColorButtonBrush_.Get(), cachedBorderBrush_.Get(), 1.0f, 6.0f);
            
            if (isHover && !isActive) {
                DrawRoundedRect(context, cb.bounds, nullptr, cachedTextBrush_.Get(), 1.0f, 6.0f);
            }
        }
    }

    if (currentContext_.activeTool == ToolType::Text && sizeDropdownActive_) {
        // Draw elegant glass/Fluent drop background
        drawGlassBackground(sizeDropdownBounds_, 6.0f);
        // Draw fine border
        DrawRoundedRect(context, sizeDropdownBounds_, nullptr, cachedBorderBrush_.Get(), 0.5f, 6.0f);
        
        std::vector<int> sizes = { 12, 14, 16, 18, 20, 24, 28, 36, 48, 72 };
        float itemH = 20.0f;
        
        for (size_t i = 0; i < sizes.size(); ++i) {
            D2D1_RECT_F itemRect = D2D1::RectF(
                sizeDropdownBounds_.left + 2.0f,
                sizeDropdownBounds_.top + i * itemH + 2.0f,
                sizeDropdownBounds_.right - 2.0f,
                sizeDropdownBounds_.top + (i + 1) * itemH - 2.0f
            );
            
            bool isHover = ptInRect(hoverPt_, itemRect);
            bool isActive = (static_cast<float>(sizes[i]) == currentContext_.fontSize);
            
            if (isActive) {
                DrawRoundedRect(context, itemRect, cachedHighlightBrush_.Get(), nullptr, 0, 4.0f);
                // Draw Fluent vertical active indicator pill on the left edge of the active item
                D2D1_RECT_F activePill = D2D1::RectF(
                    itemRect.left + 3.0f,
                    itemRect.top + 3.0f,
                    itemRect.left + 5.0f,
                    itemRect.bottom - 3.0f
                );
                DrawRoundedRect(context, activePill, cachedTextBrush_.Get(), nullptr, 0, 1.0f);
            } else if (isHover) {
                DrawRoundedRect(context, itemRect, cachedHoverBrush_.Get(), nullptr, 0, 4.0f);
            }
            
            if (fontOptionTextFormat_) {
                std::wstring textLabel = std::to_wstring(sizes[i]) + L" px";
                context->DrawTextW(textLabel.c_str(), static_cast<UINT32>(textLabel.length()), fontOptionTextFormat_.Get(), itemRect, cachedTextBrush_.Get());
            }
        }
    }

    // 3. Draw tooltip for hovered primary button
    auto getToolName = [](ToolType type) -> std::wstring {
        switch (type) {
            case ToolType::Rect: return L"矩形";
            case ToolType::Ellipse: return L"椭圆";
            case ToolType::Line: return L"直线";
            case ToolType::Arrow: return L"箭头";
            case ToolType::Freehand: return L"画笔";
            case ToolType::Mosaic: return L"马赛克";
            case ToolType::Text: return L"文字";
            case ToolType::Undo: return L"撤销";
            case ToolType::Redo: return L"重做";
            case ToolType::Pin: return L"贴图";
            case ToolType::Ocr: return L"提取文字";
            case ToolType::Save: return L"保存";
            case ToolType::Cancel: return L"退出";
            case ToolType::Done: return L"完成";
            default: return L"";
        }
    };

    for (const auto& btn : primaryButtons_) {
        if (!btn.isSeparator && ptInRect(hoverPt_, btn.bounds)) {
            std::wstring name = getToolName(btn.action);
            if (name.empty()) break;

            if (dwriteFactory_ && tooltipTextFormat_) {
                Microsoft::WRL::ComPtr<IDWriteTextLayout> textLayout;
                dwriteFactory_->CreateTextLayout(
                    name.c_str(), static_cast<UINT32>(name.length()),
                    tooltipTextFormat_.Get(), 200.0f, 30.0f, &textLayout
                );
                if (textLayout) {
                    DWRITE_TEXT_METRICS metrics;
                    textLayout->GetMetrics(&metrics);
                    float ttW = metrics.width + 12.0f;
                    float ttH = metrics.height + 8.0f;

                    float btnCenterX = (btn.bounds.left + btn.bounds.right) / 2.0f;
                    float ttLeft = btnCenterX - ttW / 2.0f;
                    float ttTop = btn.bounds.top - ttH - 6.0f;

                    D2D1_RECT_F ttRect = D2D1::RectF(ttLeft, ttTop, ttLeft + ttW, ttTop + ttH);
                    
                    // We only need the text color black here, let's create a temporary brush or reuse one
                    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> ttTextBrush;
                    context->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f), &ttTextBrush);

                    DrawRoundedRect(context, ttRect, cachedTooltipBgBrush_.Get(), cachedBorderBrush_.Get(), 1.0f, 4.0f);
                    context->DrawText(
                        name.c_str(), static_cast<UINT32>(name.length()),
                        tooltipTextFormat_.Get(),
                        ttRect,
                        ttTextBrush.Get()
                    );
                }
            }
            break;
        }
    }
}

bool ToolbarUI::HitTest(D2D1_POINT_2F pt, ToolContext& context, ToolType& outAction) {
    outAction = ToolType::None;
    
    auto ptInRect = [](D2D1_POINT_2F p, D2D1_RECT_F r) {
        return p.x >= r.left && p.x <= r.right && p.y >= r.top && p.y <= r.bottom;
    };
    
    for (const auto& btn : primaryButtons_) {
        if (!btn.isSeparator && ptInRect(pt, btn.bounds)) {
            outAction = btn.action;
            if (IsDrawingTool(btn.action)) {
                context.activeTool = btn.action;
                SetCurrentContext(context);
            }
            return true;
        }
    }
    
    if (IsDrawingTool(currentContext_.activeTool)) {
        for (const auto& cb : colorButtons_) {
            if (ptInRect(pt, cb.bounds)) {
                context.color = cb.color;
                SetCurrentContext(context);
                return true;
            }
        }
        if (currentContext_.activeTool == ToolType::Text) {
            if (sizeDropdownActive_ && ptInRect(pt, sizeDropdownBounds_)) {
                std::vector<int> sizes = { 12, 14, 16, 18, 20, 24, 28, 36, 48, 72 };
                float itemH = 20.0f;
                int index = static_cast<int>((pt.y - sizeDropdownBounds_.top) / itemH);
                if (index >= 0 && index < static_cast<int>(sizes.size())) {
                    context.fontSize = static_cast<float>(sizes[index]);
                    SetCurrentContext(context);
                }
                sizeDropdownActive_ = false;
                outAction = ToolType::None;
                return true;
            }
            
            // If they clicked outside the active dropdown, close it
            if (sizeDropdownActive_ && !ptInRect(pt, fontSizeBtnBounds_)) {
                sizeDropdownActive_ = false;
            }
            
            if (ptInRect(pt, fontSizeBtnBounds_)) {
                sizeDropdownActive_ = !sizeDropdownActive_;
                outAction = ToolType::None;
                return true;
            }
            
            if (ptInRect(pt, boldBtnBounds_)) {
                context.isBold = !context.isBold;
                SetCurrentContext(context);
                outAction = ToolType::None;
                return true;
            }
            
            if (ptInRect(pt, italicBtnBounds_)) {
                context.isItalic = !context.isItalic;
                SetCurrentContext(context);
                outAction = ToolType::None;
                return true;
            }
        } else {
            for (const auto& sb : strokeButtons_) {
                if (ptInRect(pt, sb.bounds)) {
                    context.strokeWidth = sb.strokeWidth;
                    SetCurrentContext(context);
                    return true;
                }
            }
        }
    }
    
    if (ptInRect(pt, primaryBounds_) || 
        (IsDrawingTool(currentContext_.activeTool) && ptInRect(pt, subBounds_)) ||
        (currentContext_.activeTool == ToolType::Text && sizeDropdownActive_ && ptInRect(pt, sizeDropdownBounds_))) {
        return true;
    }
    
    return false;
}

bool ToolbarUI::HitTestToolbarOnly(D2D1_POINT_2F pt) const {
    auto ptInRect = [](D2D1_POINT_2F p, D2D1_RECT_F r) {
        return p.x >= r.left && p.x <= r.right && p.y >= r.top && p.y <= r.bottom;
    };
    if (primaryBounds_.left != primaryBounds_.right && ptInRect(pt, primaryBounds_)) {
        return true;
    }
    if (subBounds_.left != subBounds_.right && ptInRect(pt, subBounds_)) {
        return true;
    }
    if (currentContext_.activeTool == ToolType::Text && sizeDropdownActive_ && ptInRect(pt, sizeDropdownBounds_)) {
        return true;
    }
    return false;
}
