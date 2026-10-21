#pragma once
#include <d2d1_1.h>
#include <dwrite.h>
#include <vector>
#include <memory>
#include <string>

enum class AnnotationType {
    Rect,
    Ellipse,
    Line,
    Arrow,
    Freehand,
    Text,
    Mosaic
};

class Annotation {
public:
    D2D1_COLOR_F color;
    float strokeWidth;

    Annotation(D2D1_COLOR_F c, float w) : color(c), strokeWidth(w) {}
    virtual ~Annotation() = default;
    
    // Some annotations (like Text) need IDWriteFactory.
    // background/backgroundDest: 源背景位图与其在当前坐标系中的目标矩形（马赛克
    // 模糊涂抹需要读取笔画下的背景；为 nullptr 时马赛克退化为半透明遮挡）。
    virtual void Draw(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory, ID2D1Bitmap* background, const D2D1_RECT_F& backgroundDest) = 0;
    virtual AnnotationType GetType() const = 0;
};

class RectAnnotation : public Annotation {
public:
    D2D1_RECT_F rect;
    RectAnnotation(D2D1_RECT_F r, D2D1_COLOR_F c, float w) : Annotation(c, w), rect(r) {}
    void Draw(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory, ID2D1Bitmap* background, const D2D1_RECT_F& backgroundDest) override;
    AnnotationType GetType() const override { return AnnotationType::Rect; }
};

class EllipseAnnotation : public Annotation {
public:
    D2D1_ELLIPSE ellipse;
    EllipseAnnotation(D2D1_ELLIPSE e, D2D1_COLOR_F c, float w) : Annotation(c, w), ellipse(e) {}
    void Draw(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory, ID2D1Bitmap* background, const D2D1_RECT_F& backgroundDest) override;
    AnnotationType GetType() const override { return AnnotationType::Ellipse; }
};

class LineAnnotation : public Annotation {
public:
    D2D1_POINT_2F startPoint;
    D2D1_POINT_2F endPoint;
    LineAnnotation(D2D1_POINT_2F start, D2D1_POINT_2F end, D2D1_COLOR_F c, float w) 
        : Annotation(c, w), startPoint(start), endPoint(end) {}
    void Draw(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory, ID2D1Bitmap* background, const D2D1_RECT_F& backgroundDest) override;
    AnnotationType GetType() const override { return AnnotationType::Line; }
};

class ArrowAnnotation : public Annotation {
public:
    D2D1_POINT_2F startPoint;
    D2D1_POINT_2F endPoint;
    ArrowAnnotation(D2D1_POINT_2F start, D2D1_POINT_2F end, D2D1_COLOR_F c, float w) 
        : Annotation(c, w), startPoint(start), endPoint(end) {}
    void Draw(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory, ID2D1Bitmap* background, const D2D1_RECT_F& backgroundDest) override;
    AnnotationType GetType() const override { return AnnotationType::Arrow; }
};

class FreehandAnnotation : public Annotation {
public:
    std::vector<D2D1_POINT_2F> points;
    FreehandAnnotation(const std::vector<D2D1_POINT_2F>& pts, D2D1_COLOR_F c, float w) 
        : Annotation(c, w), points(pts) {}
    void Draw(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory, ID2D1Bitmap* background, const D2D1_RECT_F& backgroundDest) override;
    AnnotationType GetType() const override { return AnnotationType::Freehand; }
};

class TextAnnotation : public Annotation {
public:
    std::wstring text;
    D2D1_RECT_F rect;
    float fontSize;
    bool isBold;
    bool isItalic;

    TextAnnotation(const std::wstring& t, D2D1_RECT_F r, D2D1_COLOR_F c, float size, bool bold = false, bool italic = false) 
        : Annotation(c, 0.0f), text(t), rect(r), fontSize(size), isBold(bold), isItalic(italic) {}
    void Draw(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory, ID2D1Bitmap* background, const D2D1_RECT_F& backgroundDest) override;
    AnnotationType GetType() const override { return AnnotationType::Text; }
};

class MosaicAnnotation : public Annotation {
public:
    std::vector<D2D1_POINT_2F> points;
    MosaicAnnotation(const std::vector<D2D1_POINT_2F>& pts, float w) 
        : Annotation(D2D1::ColorF(D2D1::ColorF::Black), w), points(pts) {}
    void Draw(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory, ID2D1Bitmap* background, const D2D1_RECT_F& backgroundDest) override;
    AnnotationType GetType() const override { return AnnotationType::Mosaic; }
};

class AnnotationDocument {
public:
    void AddAnnotation(std::shared_ptr<Annotation> annotation);
    void Undo();
    void Redo();
    void DrawAll(ID2D1DeviceContext* context, IDWriteFactory* dwriteFactory = nullptr, ID2D1Bitmap* background = nullptr, D2D1_RECT_F backgroundDest = {});
    void Clear();
    std::shared_ptr<TextAnnotation> HitTestText(D2D1_POINT_2F pt);
    void RemoveAnnotation(std::shared_ptr<Annotation> annotation);

private:
    std::vector<std::shared_ptr<Annotation>> annotations_;
    std::vector<std::shared_ptr<Annotation>> redoStack_;
};
