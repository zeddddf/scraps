#include "AnnotationDocument.h"
#include <cmath>
#include <d2d1effects.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <algorithm>

void RectAnnotation::Draw(ID2D1DeviceContext* context, IDWriteFactory* /*dwriteFactory*/, ID2D1Bitmap* /*background*/, const D2D1_RECT_F& /*backgroundDest*/) {
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    if (SUCCEEDED(context->CreateSolidColorBrush(color, &brush))) {
        context->DrawRectangle(rect, brush.Get(), strokeWidth);
    }
}

void EllipseAnnotation::Draw(ID2D1DeviceContext* context, IDWriteFactory* /*dwriteFactory*/, ID2D1Bitmap* /*background*/, const D2D1_RECT_F& /*backgroundDest*/) {
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    if (SUCCEEDED(context->CreateSolidColorBrush(color, &brush))) {
        context->DrawEllipse(ellipse, brush.Get(), strokeWidth);
    }
}

void LineAnnotation::Draw(ID2D1DeviceContext* context, IDWriteFactory* /*dwriteFactory*/, ID2D1Bitmap* /*background*/, const D2D1_RECT_F& /*backgroundDest*/) {
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    if (SUCCEEDED(context->CreateSolidColorBrush(color, &brush))) {
        context->DrawLine(startPoint, endPoint, brush.Get(), strokeWidth);
    }
}

void ArrowAnnotation::Draw(ID2D1DeviceContext* context, IDWriteFactory* /*dwriteFactory*/, ID2D1Bitmap* /*background*/, const D2D1_RECT_F& /*backgroundDest*/) {
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

void FreehandAnnotation::Draw(ID2D1DeviceContext* context, IDWriteFactory* /*dwriteFactory*/, ID2D1Bitmap* /*background*/, const D2D1_RECT_F& /*backgroundDest*/) {
    if (points.size() < 2) return;
    
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    if (SUCCEEDED(context->CreateSolidColorBrush(color, &brush))) {
        for (size_t i = 1; i < points.size(); ++i) {
            context->DrawLine(points[i - 1], points[i], brush.Get(), strokeWidth);
        }
    }
}

void TextAnnotation::Draw(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory, ID2D1Bitmap* /*background*/, const D2D1_RECT_F& /*backgroundDest*/) {
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

void MosaicAnnotation::Draw(ID2D1DeviceContext* context, IDWriteFactory* /*dwriteFactory*/, ID2D1Bitmap* background, const D2D1_RECT_F& backgroundDest) {
    if (points.size() < 2) return;

    // P3-18a 真马赛克：ID2D1Effect（CLSID_D2D1GaussianBlur）模糊背景，经
    // 笔画几何遮罩合成到笔画区域（系统自带效果，不引第三方库）。
    if (background == nullptr) {
        // 无背景可模糊时的退化：半透明深色遮挡（不再是纯黑实线占位）。
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
        if (SUCCEEDED(context->CreateSolidColorBrush(D2D1::ColorF(0.1f, 0.1f, 0.1f, 0.6f), &brush))) {
            for (size_t i = 1; i < points.size(); ++i) {
                context->DrawLine(points[i - 1], points[i], brush.Get(), strokeWidth);
            }
        }
        return;
    }

    ID2D1Factory* factory = nullptr;
    context->GetFactory(&factory);
    if (factory == nullptr) return;

    Microsoft::WRL::ComPtr<ID2D1PathGeometry> strokePath;
    if (FAILED(factory->CreatePathGeometry(&strokePath))) return;
    {
        Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
        if (FAILED(strokePath->Open(&sink))) return;
        sink->BeginFigure(points[0], D2D1_FIGURE_BEGIN_FILLED);
        for (size_t i = 1; i < points.size(); ++i) {
            sink->AddLine(points[i]);
        }
        sink->EndFigure(D2D1_FIGURE_END_OPEN);
        sink->Close();
    }

    Microsoft::WRL::ComPtr<ID2D1StrokeStyle> roundStyle;
    factory->CreateStrokeStyle(
        D2D1::StrokeStyleProperties(
            D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND),
        nullptr, 0, &roundStyle);

    Microsoft::WRL::ComPtr<ID2D1PathGeometry> mask;
    if (FAILED(factory->CreatePathGeometry(&mask))) return;
    {
        Microsoft::WRL::ComPtr<ID2D1GeometrySink> maskSink;
        if (FAILED(mask->Open(&maskSink))) return;
        // 加宽几何写入 mask 的 sink（笔画足迹作为填充遮罩）。
        if (FAILED(strokePath->Widen(strokeWidth, roundStyle.Get(), nullptr, maskSink.Get()))) return;
        maskSink->Close();
    }

    Microsoft::WRL::ComPtr<ID2D1Effect> blur;
    if (FAILED(context->CreateEffect(CLSID_D2D1GaussianBlur, &blur))) return;
    blur->SetInput(0, background);
    blur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, std::max(6.0f, strokeWidth * 0.35f));

    D2D1_LAYER_PARAMETERS layerParams = D2D1::LayerParameters(
        D2D1::InfiniteRect(),
        mask.Get(),
        D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
        D2D1::IdentityMatrix(),
        1.0f,
        nullptr,
        D2D1_LAYER_OPTIONS_NONE);
    context->PushLayer(layerParams, nullptr);
    context->DrawImage(blur.Get(), D2D1::Point2F(backgroundDest.left, backgroundDest.top));
    context->PopLayer();
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

void AnnotationDocument::DrawAll(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory, ID2D1Bitmap* background, D2D1_RECT_F backgroundDest) {
    for (const auto& ann : annotations_) {
        ann->Draw(context, dwriteFactory, background, backgroundDest);
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
