#pragma once
#include <d2d1_1.h>
#include <vector>

enum class ToolType {
    Rect,
    Ellipse,
    Line,
    Arrow,
    Freehand,
    Mosaic,
    Text,
    Undo,
    Redo,
    Pin,
    Ocr,
    Save,
    Cancel,
    Done,
    None
};

struct ToolContext {
    ToolType activeTool = ToolType::None;
    float strokeWidth = 2.0f; // 2.0, 4.0, 8.0
    D2D1_COLOR_F color = D2D1::ColorF(D2D1::ColorF::Red);
    float fontSize = 28.0f;   // Default set to 28.0px
    bool isBold = false;
    bool isItalic = false;
};
