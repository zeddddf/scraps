#include "AnnotationDocument.h"
#include <cmath>
#include <dwrite.h>
#include <wrl/client.h>
#include <algorithm>

void RectAnnotation::Draw(ID2D1DeviceContext* context, IDWriteFactory* /*dwriteFactory*/) {
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    if (SUCCEEDED(context->CreateSolidColorBrush(color, &brush))) {
        context->DrawRectangle(rect, brush.Get(), strokeWidth);
    }
}

void EllipseAnnotation::Draw(ID2D1DeviceContext* context, IDWriteFactory* /*dwriteFactory*/) {
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    if (SUCCEEDED(context->CreateSolidColorBrush(color, &brush))) {
        context->DrawEllipse(ellipse, brush.Get(), strokeWidth);
    }
}

void LineAnnotation::Draw(ID2D1DeviceContext* context, IDWriteFactory* /*dwriteFactory*/) {
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    if (SUCCEEDED(context->CreateSolidColorBrush(color, &brush))) {
        context->DrawLine(startPoint, endPoint, brush.Get(), strokeWidth);
    }
}

void ArrowAnnotation::Draw(ID2D1DeviceContext* context, IDWriteFactory* /*dwriteFactory*/) {
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    if (SUCCEEDED(context->CreateSolidColorBrush(color, &brush))) {
        context->DrawLine(startPoint, endPoint, brush.Get(), strokeWidth);
        
        float angle = std::atan2(endPoint.y - startPoint.y, endPoint.x - startPoint.x);
        float arrowLen = 10.0f + strokeWidth;
        float angle1 = angle + 3.14159f / 6.0f;
        float angle2 = angle - 3.14159f / 6.0f;
        
        D2D1_POINT_2F p1 = D2D1::Point2F(endPoint.x - arrowLen * std::cos(angle1), endPoint.y - arrowLen * std::sin(angle1));
        D2D1_POINT_2F p2 = D2D1::Point2F(endPoint.x - arrowLen * std::cos(angle2), endPoint.y - arrowLen * std::sin(angle2));
        
        context->DrawLine(endPoint, p1, brush.Get(), strokeWidth);
        context->DrawLine(endPoint, p2, brush.Get(), strokeWidth);
    }
}

void FreehandAnnotation::Draw(ID2D1DeviceContext* context, IDWriteFactory* /*dwriteFactory*/) {
    if (points.size() < 2) return;
    
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    if (SUCCEEDED(context->CreateSolidColorBrush(color, &brush))) {
        for (size_t i = 1; i < points.size(); ++i) {
            context->DrawLine(points[i - 1], points[i], brush.Get(), strokeWidth);
        }
    }
}

void TextAnnotation::Draw(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory) {
    if (!dwriteFactory || text.empty()) return;

    DWRITE_FONT_WEIGHT weight = isBold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL;
    DWRITE_FONT_STYLE style = isItalic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL;

    Microsoft::WRL::ComPtr<IDWriteTextFormat> textFormat;
    if (SUCCEEDED(dwriteFactory->CreateTextFormat(
            L"Microsoft YaHei",
            nullptr,
            weight,
            style,
            DWRITE_FONT_STRETCH_NORMAL,
            fontSize,
            L"zh-cn",
            &textFormat))) {
        
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
        if (SUCCEEDED(context->CreateSolidColorBrush(color, &brush))) {
            float w = rect.right - rect.left;
            float h = rect.bottom - rect.top;
            if (w < 50.0f) w = 50.0f;
            if (h < fontSize) h = fontSize;

            Microsoft::WRL::ComPtr<IDWriteTextLayout> textLayout;
            if (SUCCEEDED(dwriteFactory->CreateTextLayout(
                    text.c_str(),
                    static_cast<UINT32>(text.length()),
                    textFormat.Get(),
                    w,
                    h,
                    &textLayout))) {
                
                context->DrawTextLayout(
                    D2D1::Point2F(rect.left, rect.top),
                    textLayout.Get(),
                    brush.Get()
                );
            }
        }
    }
}

void MosaicAnnotation::Draw(ID2D1DeviceContext* context, IDWriteFactory* /*dwriteFactory*/) {
    if (points.size() < 2) return;
    
    // Mosaic is a bit special. For now, we draw thick semi-transparent black lines as a placeholder 
    // to build the "mask". In the actual render pipeline, OverlayRenderer will apply the D2D Blur Effect 
    // using this mask layer. 
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    if (SUCCEEDED(context->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f), &brush))) {
        for (size_t i = 1; i < points.size(); ++i) {
            context->DrawLine(points[i - 1], points[i], brush.Get(), strokeWidth);
        }
    }
}

void AnnotationDocument::AddAnnotation(std::shared_ptr<Annotation> annotation) {
    if (annotation) {
        annotations_.push_back(annotation);
        redoStack_.clear();
    }
}

void AnnotationDocument::Undo() {
    if (!annotations_.empty()) {
        redoStack_.push_back(annotations_.back());
        annotations_.pop_back();
    }
}

void AnnotationDocument::Redo() {
    if (!redoStack_.empty()) {
        annotations_.push_back(redoStack_.back());
        redoStack_.pop_back();
    }
}

void AnnotationDocument::DrawAll(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory) {
    for (const auto& ann : annotations_) {
        ann->Draw(context, dwriteFactory);
    }
}

void AnnotationDocument::Clear() {
    annotations_.clear();
    redoStack_.clear();
}

std::shared_ptr<TextAnnotation> AnnotationDocument::HitTestText(D2D1_POINT_2F pt) {
    // Search in reverse order so we hit test the topmost (newest) text first
    for (auto it = annotations_.rbegin(); it != annotations_.rend(); ++it) {
        if ((*it)->GetType() == AnnotationType::Text) {
            auto textAnn = std::static_pointer_cast<TextAnnotation>(*it);
            if (pt.x >= textAnn->rect.left && pt.x <= textAnn->rect.right &&
                pt.y >= textAnn->rect.top && pt.y <= textAnn->rect.bottom) {
                return textAnn;
            }
        }
    }
    return nullptr;
}

void AnnotationDocument::RemoveAnnotation(std::shared_ptr<Annotation> annotation) {
    auto it = std::find(annotations_.begin(), annotations_.end(), annotation);
    if (it != annotations_.end()) {
        annotations_.erase(it);
    }
}
