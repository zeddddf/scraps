#include "editing/InteractionStateMachine.h"

#include <cmath>
#include <chrono>
#include <imm.h>
#include <algorithm>

#pragma comment(lib, "imm32.lib")

namespace {
Microsoft::WRL::ComPtr<IDWriteTextLayout> CreateLayoutHelper(IDWriteFactory* factory, const std::wstring& text, float width, float fontSize, bool bold, bool italic) {
    DWRITE_FONT_WEIGHT weight = bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL;
    DWRITE_FONT_STYLE style = italic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
    if (FAILED(factory->CreateTextFormat(
            L"Microsoft YaHei",
            nullptr,
            weight,
            style,
            DWRITE_FONT_STRETCH_NORMAL,
            fontSize,
            L"zh-cn",
            &fmt))) {
        return nullptr;
    }
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    if (FAILED(factory->CreateTextLayout(
            text.c_str(),
            static_cast<UINT32>(text.length()),
            fmt.Get(),
            width,
            10000.0f, // large height
            &layout))) {
        return nullptr;
    }
    return layout;
}
}

InteractionStateMachine::InteractionStateMachine() : currentMode_(Mode::SelectRoi), uiaSnapping_() {
    windowRects_ = WindowEnumerationService::EnumerateCapturableWindows();
    lastWindowRefresh_ = std::chrono::steady_clock::now();
    // Fix G: exclude our own overlay from UIA crawl
    uiaSnapping_.SetOverlayClass(L"ScrapsScreenshotOverlay");
    uiaSnapping_.StartCrawl();

    MonitorTopology topology;
    cachedMonitors_ = topology.GetMonitors();
}

void InteractionStateMachine::Reset() {
    CommitActiveText();
    selection_.Reset();
    document_.Clear();
    currentMode_ = Mode::SelectRoi;
    toolContext_.activeTool = ToolType::None;
    toolbar_.SetCurrentContext(toolContext_);
    currentDraft_ = nullptr;
    isDrawing_ = false;
    isEditingText_ = false;
    activeText_.clear();
    imeCompositionString_.clear();
    caretIndex_ = 0;
    textRect_ = D2D1_RECT_F{};
    
    // Clear UIA Snapping/Tab states
    tabCandidates_.clear();
    tabIndex_ = 0;
    uiaOverrideRect_ = std::nullopt;
    uiaOverridePt_ = { -9999, -9999 };
    hoveredWindowRect_ = std::nullopt;
    
    // Refresh windows
    windowRects_ = WindowEnumerationService::EnumerateCapturableWindows();
    lastWindowRefresh_ = std::chrono::steady_clock::now();

    MonitorTopology topology;
    cachedMonitors_ = topology.GetMonitors();
}

void InteractionStateMachine::RefreshMonitorTopology() {
    // U7: 显示器热插拔后重枚举拓扑与可捕获窗口，并把选区钳制进新的虚拟屏边界。
    MonitorTopology topology;
    cachedMonitors_ = topology.GetMonitors();
    windowRects_ = WindowEnumerationService::EnumerateCapturableWindows();
    lastWindowRefresh_ = std::chrono::steady_clock::now();
    hoveredWindowRect_ = std::nullopt;
    tabCandidates_.clear();
    tabIndex_ = 0;

    if (cachedMonitors_.empty()) {
        return;
    }
    RectPx screen = cachedMonitors_.front().virtualBounds;
    for (const auto& m : cachedMonitors_) {
        const RectPx& b = m.virtualBounds;
        if (b.x < screen.x) screen.x = b.x;
        if (b.y < screen.y) screen.y = b.y;
        const int right = b.x + b.width;
        const int bottom = b.y + b.height;
        if (right > screen.x + screen.width) screen.width = right - screen.x;
        if (bottom > screen.y + screen.height) screen.height = bottom - screen.y;
    }
    if (selection_.GetState() == SelectionModel::State::Selected) {
        RectPx r = selection_.GetCurrentRoi();
        if (r.width > 0 && r.height > 0) {
            if (r.x < screen.x) r.x = screen.x;
            if (r.y < screen.y) r.y = screen.y;
            if (r.x + r.width > screen.x + screen.width) {
                r.width = screen.x + screen.width - r.x;
            }
            if (r.y + r.height > screen.y + screen.height) {
                r.height = screen.y + screen.height - r.y;
            }
            if (r.width > 0 && r.height > 0) {
                selection_.SetRoi(r);
                toolbar_.SetSelectionRect(D2D1::RectF(
                    static_cast<float>(r.x),
                    static_cast<float>(r.y),
                    static_cast<float>(r.x + r.width),
                    static_cast<float>(r.y + r.height)));
            }
        }
    }
}

void InteractionStateMachine::InitializeInitialHoverRect(const PointPx& pt) {
    // Ensure we have the latest window rectangles when starting the session
    windowRects_ = WindowEnumerationService::EnumerateCapturableWindows();
    lastWindowRefresh_ = std::chrono::steady_clock::now();

    hoveredWindowRect_ = std::nullopt;
    tabCandidates_.clear();
    tabIndex_ = 0;

    for (const auto& r : windowRects_) {
        if (r.contains(pt)) {
            hoveredWindowRect_ = r;
            tabCandidates_.push_back(r);
            tabIndex_ = 0;
            break;
        }
    }
}

void InteractionStateMachine::SetMode(Mode m) {
    currentMode_ = m;
    if (selection_.GetState() == SelectionModel::State::Selected) {
        RectPx r = selection_.GetCurrentRoi();
        toolbar_.SetSelectionRect(D2D1::RectF(
            static_cast<float>(r.x), 
            static_cast<float>(r.y), 
            static_cast<float>(r.x + r.width), 
            static_cast<float>(r.y + r.height)
        ));
    }
}

// ── P3-17 模式级事件转移表 ─────────────────────────────────────────────
// 行序即优先级（自上而下首个 guard 命中生效）。U4/U5 语义只看这张表：
//   ESC① 编辑态 → 提交非空草稿退编辑；ESC② 工具态 → 退回 SelectRoi；
//   ESC③ SelectRoi → 请求退出截图。右键/双击同理逐级退一层。
namespace {
constexpr InteractionStateMachine::TransitionRow kTransitionTable[] = {
    // ESC 梯级
    {InteractionStateMachine::LadderEvent::Escape,
     &InteractionStateMachine::GuardIsEditingText,
     InteractionStateMachine::EvEffect::CommitTextAndStay},
    {InteractionStateMachine::LadderEvent::Escape,
     &InteractionStateMachine::GuardInToolMode,
     InteractionStateMachine::EvEffect::BackoutTool},
    {InteractionStateMachine::LadderEvent::Escape,
     nullptr,
     InteractionStateMachine::EvEffect::RequestCancel},
    // 右键梯级
    {InteractionStateMachine::LadderEvent::RightButtonDown,
     &InteractionStateMachine::GuardIsDrawingDraft,
     InteractionStateMachine::EvEffect::DiscardDraftBackout},
    {InteractionStateMachine::LadderEvent::RightButtonDown,
     &InteractionStateMachine::GuardHasSelection,
     InteractionStateMachine::EvEffect::ClearSelection},
    {InteractionStateMachine::LadderEvent::RightButtonDown,
     nullptr,
     InteractionStateMachine::EvEffect::RequestCancel},
    // 双击：仅本手势提交过草稿才撤销（U5）
    {InteractionStateMachine::LadderEvent::DoubleClick,
     &InteractionStateMachine::GuardShouldUndoGestureDraft,
     InteractionStateMachine::EvEffect::UndoGestureDraft},
};
}  // namespace

InteractionStateMachine::Action InteractionStateMachine::DispatchLadder(LadderEvent event) {
    for (const auto& row : kTransitionTable) {
        if (row.event != event) {
            continue;
        }
        if (row.guard != nullptr && !(this->*row.guard)()) {
            continue;
        }
        switch (row.effect) {
            case EvEffect::CommitTextAndStay: {
                // C2/U4：非空草稿保留为标注（CommitActiveText），绝不静默丢弃。
                CommitActiveText();
                return Action::None;
            }
            case EvEffect::DiscardDraftBackout: {
                currentDraft_ = nullptr;
                isDrawing_ = false;
                return Action::None;
            }
            case EvEffect::BackoutTool: {
                currentMode_ = Mode::SelectRoi;
                toolContext_.activeTool = ToolType::None;
                toolbar_.SetCurrentContext(toolContext_);
                currentDraft_ = nullptr;
                isDrawing_ = false;
                return Action::None;
            }
            case EvEffect::ClearSelection: {
                selection_.Reset();
                currentMode_ = Mode::SelectRoi;
                toolContext_.activeTool = ToolType::None;
                toolbar_.SetCurrentContext(toolContext_);
                return Action::None;
            }
            case EvEffect::RequestCancel: {
                return Action::Cancel;
            }
            case EvEffect::UndoGestureDraft: {
                document_.Undo();
                gestureDraftCommitted_ = false;
                return Action::None;
            }
        }
        return Action::None;
    }
    return Action::None;
}

void InteractionStateMachine::ProcessMouseInput(UINT msg, const PointPx& pt) {
    ProcessMouseInputEx(msg, pt);
}

InteractionStateMachine::Action InteractionStateMachine::ProcessMouseInputEx(UINT msg, const PointPx& pt) {
    D2D1_POINT_2F d2dPt = D2D1::Point2F(static_cast<float>(pt.x), static_cast<float>(pt.y));

    // 1./2. ESC 与右键的梯级语义由转移表驱动（P3-17，见 kTransitionTable）。
    if (msg == WM_KEYDOWN && pt.x == VK_ESCAPE) {
        return DispatchLadder(LadderEvent::Escape);
    }
    if (msg == WM_RBUTTONDOWN) {
        return DispatchLadder(LadderEvent::RightButtonDown);
    }

    if (selection_.GetState() == SelectionModel::State::Selected) {
        if (msg == WM_LBUTTONDBLCLK) {
            bool insideTextEditor = false;
            if (isEditingText_) {
                insideTextEditor = (d2dPt.x >= textRect_.left && d2dPt.x <= textRect_.right &&
                                    d2dPt.y >= textRect_.top && d2dPt.y <= textRect_.bottom);
            }
            if (!insideTextEditor && !toolbar_.HitTestToolbarOnly(d2dPt)) {
                RectPx roi = selection_.GetCurrentRoi();
                if (pt.x >= roi.x && pt.x <= roi.x + roi.width &&
                    pt.y >= roi.y && pt.y <= roi.y + roi.height) {
                    
                    // U5: 仅当本次手势确实提交过草稿时才撤销它（表 guard 行
                    //     GuardShouldUndoGestureDraft；空双击的无条件 Undo 会
                    //     误删上一条真实标注）。
                    if (GuardShouldUndoGestureDraft()) {
                        document_.Undo();
                        gestureDraftCommitted_ = false;
                    }
                    
                    CommitActiveText();
                    return Action::Complete;
                }
            }
        }

        if (msg == WM_LBUTTONDOWN) {
            ToolType action;
            if (toolbar_.HitTest(d2dPt, toolContext_, action)) {
                if (action != ToolType::None) {
                    CommitActiveText();
                    if (action == ToolType::Rect) {
                        SetMode(Mode::DrawRectangle);
                        return Action::None;
                    } else if (action == ToolType::Arrow) {
                        SetMode(Mode::DrawArrow);
                        return Action::None;
                    } else if (action == ToolType::Ellipse) {
                        SetMode(Mode::DrawEllipse);
                        return Action::None;
                    } else if (action == ToolType::Line) {
                        SetMode(Mode::DrawLine);
                        return Action::None;
                    } else if (action == ToolType::Freehand) {
                        SetMode(Mode::DrawFreehand);
                        return Action::None;
                    } else if (action == ToolType::Mosaic) {
                        SetMode(Mode::DrawMosaic);
                        return Action::None;
                    } else if (action == ToolType::Text) {
                        SetMode(Mode::Text);
                        return Action::None;
                    } else if (action == ToolType::Undo) {
                        document_.Undo();
                        return Action::None;
                    } else if (action == ToolType::Redo) {
                        document_.Redo();
                        return Action::None;
                    } else if (action == ToolType::Pin) {
                        return Action::Pin;
                    } else if (action == ToolType::Save) {
                        return Action::Save;
                    } else if (action == ToolType::Done) {
                        return Action::Complete;
                    } else if (action == ToolType::Cancel) {
                        return Action::Cancel;
                    }
                } else {
                    // Clicked styling button (e.g. font size, color, bold, italic)
                    // If actively editing text, update layout metrics so it takes effect instantly
                    if (isEditingText_) {
                        UpdateTextLayoutMetrics();
                    }
                }
                return Action::None; // Handled by toolbar
            }
        } else if (msg == WM_MOUSEMOVE) {
            toolbar_.UpdateHover(d2dPt);
        }
    }

    if (currentMode_ == Mode::SelectRoi) {
        switch (msg) {
            case WM_LBUTTONDOWN:
                mouseDownPt_ = pt;
                selection_.HandleMouseDown(pt);
                break;
            case WM_MOUSEMOVE:
                if (selection_.GetState() == SelectionModel::State::None) {
                    // Fix D: refresh top-level window list every 3 s
                    auto now = std::chrono::steady_clock::now();
                    if (now - lastWindowRefresh_ > std::chrono::seconds(3)) {
                        windowRects_ = WindowEnumerationService::EnumerateCapturableWindows();
                        lastWindowRefresh_ = now;
                    }

                    // UIA override persistence: keep dwell result until cursor moves >8px
                    if (uiaOverrideRect_.has_value()) {
                        int dx = pt.x - uiaOverridePt_.x;
                        int dy = pt.y - uiaOverridePt_.y;
                        if (dx * dx + dy * dy <= 64) {
                            // Still within 8px of dwell point — keep UIA result, skip HWND re-scan
                            hoveredWindowRect_ = uiaOverrideRect_;
                            selection_.HandleMouseMove(pt);
                            break;
                        }
                        // Moved >8px — discard override, fall through to HWND snapshot
                        uiaOverrideRect_ = std::nullopt;
                    }

                    hoveredWindowRect_ = std::nullopt;

                    if (autoDetectEnabled_) {
                        std::vector<RectPx> candidates = uiaSnapping_.HitTestAll(pt);
                        
                        // Check if candidates match tabCandidates_
                        bool candidatesMatch = (candidates.size() == tabCandidates_.size());
                        if (candidatesMatch) {
                            for (size_t i = 0; i < candidates.size(); ++i) {
                                if (candidates[i].x != tabCandidates_[i].x ||
                                    candidates[i].y != tabCandidates_[i].y ||
                                    candidates[i].width != tabCandidates_[i].width ||
                                    candidates[i].height != tabCandidates_[i].height) {
                                    candidatesMatch = false;
                                    break;
                                }
                            }
                        }

                        if (!candidatesMatch) {
                            tabCandidates_ = candidates;
                            tabIndex_ = 0;
                            if (!tabCandidates_.empty()) {
                                hoveredWindowRect_ = tabCandidates_[0];
                            }
                        } else {
                            // Candidates are identical, keep current tabIndex_ and hoveredWindowRect_
                            if (!tabCandidates_.empty() && tabIndex_ >= 0 && tabIndex_ < static_cast<int>(tabCandidates_.size())) {
                                hoveredWindowRect_ = tabCandidates_[tabIndex_];
                            }
                        }
                    }

                    // Fallback to top-level window if snapshot has no match yet
                    if (!hoveredWindowRect_) {
                        tabCandidates_.clear();
                        tabIndex_ = 0;
                        for (const auto& r : windowRects_) {
                            if (r.contains(pt)) {
                                hoveredWindowRect_ = r;
                                tabCandidates_.push_back(r);
                                break;
                            }
                        }
                    }
                } else {
                    hoveredWindowRect_ = std::nullopt;
                    tabCandidates_.clear();
                    tabIndex_ = 0;
                    uiaOverrideRect_ = std::nullopt;
                }
                selection_.HandleMouseMove(pt);
                break;
            case WM_LBUTTONUP:
                if (selection_.GetState() == SelectionModel::State::Creating) {
                    if (std::abs(pt.x - mouseDownPt_.x) <= 3 && std::abs(pt.y - mouseDownPt_.y) <= 3) {
                        if (hoveredWindowRect_) {
                            selection_.SetRoi(*hoveredWindowRect_);
                            hoveredWindowRect_ = std::nullopt;
                            
                            // Update toolbar position
                            RectPx r = selection_.GetCurrentRoi();
                            toolbar_.SetSelectionRect(D2D1::RectF(static_cast<float>(r.x), static_cast<float>(r.y), static_cast<float>(r.x + r.width), static_cast<float>(r.y + r.height)));
                            return Action::None;
                        }
                    }
                }
                selection_.HandleMouseUp(pt);
                if (selection_.GetState() == SelectionModel::State::Selected) {
                    RectPx r = selection_.GetCurrentRoi();
                    toolbar_.SetSelectionRect(D2D1::RectF(static_cast<float>(r.x), static_cast<float>(r.y), static_cast<float>(r.x + r.width), static_cast<float>(r.y + r.height)));
                }
                break;
        }
    } else if (currentMode_ == Mode::DrawRectangle || currentMode_ == Mode::DrawArrow || 
               currentMode_ == Mode::DrawEllipse || currentMode_ == Mode::DrawLine || 
               currentMode_ == Mode::DrawFreehand || currentMode_ == Mode::DrawMosaic) {
        switch (msg) {
            case WM_LBUTTONDOWN:
                isDrawing_ = true;
                gestureDraftCommitted_ = false;  // U5: 新手势开始，清草稿标记
                drawStartPt_ = d2dPt;
                if (currentMode_ == Mode::DrawFreehand) {
                    currentDraft_ = std::make_shared<FreehandAnnotation>(std::vector<D2D1_POINT_2F>{d2dPt}, toolContext_.color, toolContext_.strokeWidth);
                } else if (currentMode_ == Mode::DrawMosaic) {
                    currentDraft_ = std::make_shared<MosaicAnnotation>(std::vector<D2D1_POINT_2F>{d2dPt}, toolContext_.strokeWidth * 4.0f);
                }
                break;
            case WM_MOUSEMOVE:
                if (isDrawing_) {
                    if (currentMode_ == Mode::DrawRectangle) {
                        currentDraft_ = std::make_shared<RectAnnotation>(
                            D2D1::RectF(std::min(drawStartPt_.x, d2dPt.x), std::min(drawStartPt_.y, d2dPt.y),
                                        std::max(drawStartPt_.x, d2dPt.x), std::max(drawStartPt_.y, d2dPt.y)),
                            toolContext_.color, toolContext_.strokeWidth
                        );
                    } else if (currentMode_ == Mode::DrawArrow) {
                        currentDraft_ = std::make_shared<ArrowAnnotation>(drawStartPt_, d2dPt, toolContext_.color, toolContext_.strokeWidth);
                    } else if (currentMode_ == Mode::DrawLine) {
                        currentDraft_ = std::make_shared<LineAnnotation>(drawStartPt_, d2dPt, toolContext_.color, toolContext_.strokeWidth);
                    } else if (currentMode_ == Mode::DrawEllipse) {
                        float cx = (drawStartPt_.x + d2dPt.x) / 2.0f;
                        float cy = (drawStartPt_.y + d2dPt.y) / 2.0f;
                        float rx = std::abs(d2dPt.x - drawStartPt_.x) / 2.0f;
                        float ry = std::abs(d2dPt.y - drawStartPt_.y) / 2.0f;
                        currentDraft_ = std::make_shared<EllipseAnnotation>(D2D1::Ellipse(D2D1::Point2F(cx, cy), rx, ry), toolContext_.color, toolContext_.strokeWidth);
                    } else if (currentMode_ == Mode::DrawFreehand) {
                        if (currentDraft_) {
                            auto freehand = std::static_pointer_cast<FreehandAnnotation>(currentDraft_);
                            freehand->points.push_back(d2dPt);
                        }
                    } else if (currentMode_ == Mode::DrawMosaic) {
                        if (currentDraft_) {
                            auto mosaic = std::static_pointer_cast<MosaicAnnotation>(currentDraft_);
                            mosaic->points.push_back(d2dPt);
                        }
                    }
                }
                break;
            case WM_LBUTTONUP:
                if (isDrawing_) {
                    if (currentDraft_) {
                        document_.AddAnnotation(currentDraft_);
                        currentDraft_ = nullptr;
                        gestureDraftCommitted_ = true;  // U5: 本手势提交过草稿
                    }
                    isDrawing_ = false;
                }
                break;
        }
    } else if (currentMode_ == Mode::Text) {
        switch (msg) {
            case WM_LBUTTONDOWN: {
                lastMousePt_ = pt;
                if (isEditingText_) {
                    // Check if clicked the close button (top-right of frame)
                    float closeX = textRect_.right + 4.0f;
                    float closeY = textRect_.top - 4.0f;
                    float dxClose = d2dPt.x - closeX;
                    float dyClose = d2dPt.y - closeY;
                    if (dxClose * dxClose + dyClose * dyClose <= 64.0f) { // 8px radius hit area
                        // C2: 显式丢弃——新草稿丢弃；重编辑已有标注则恢复原对象。
                        isEditingText_ = false;
                        activeText_.clear();
                        caretIndex_ = 0;
                        selectionAnchor_ = 0;
                        selectionActive_ = 0;
                        isDragSelecting_ = false;
                        textRect_ = D2D1_RECT_F{};
                        localUndoStack_.clear();
                        localRedoStack_.clear();
                        if (editingSource_) {
                            document_.AddAnnotation(editingSource_);
                            editingSource_.reset();
                        }
                        return Action::None;
                    }

                    // Check if clicked the right resize knob
                    float knobX = textRect_.right + 4.0f;
                    float knobY = (textRect_.top + textRect_.bottom) / 2.0f;
                    float dx = d2dPt.x - knobX;
                    float dy = d2dPt.y - knobY;
                    if (dx * dx + dy * dy <= 64.0f) { // 8px radius hit area
                        textDragState_ = TextDragState::ResizingWidth;
                        return Action::None;
                    }

                    // Check if clicked the border/frame for moving
                    D2D1_RECT_F outerFrame = D2D1::RectF(
                        textRect_.left - 10.0f,
                        textRect_.top - 10.0f,
                        textRect_.right + 10.0f,
                        textRect_.bottom + 10.0f
                    );
                    
                    bool clickInsideFrame = (d2dPt.x >= outerFrame.left && d2dPt.x <= outerFrame.right &&
                                             d2dPt.y >= outerFrame.top && d2dPt.y <= outerFrame.bottom);
                    
                    bool clickInsideText = (d2dPt.x >= textRect_.left && d2dPt.x <= textRect_.right &&
                                            d2dPt.y >= textRect_.top && d2dPt.y <= textRect_.bottom);

                    if (clickInsideFrame && !clickInsideText) {
                        textDragState_ = TextDragState::Moving;
                        return Action::None;
                    }

                    if (clickInsideText) {
                        // Position the caret via HitTestPoint
                        EnsurePrivateDWriteFactory();
                        float w = textRect_.right - textRect_.left;
                        auto layout = CreateLayoutHelper(dwriteFactoryPrivate_.Get(), activeText_, w, toolContext_.fontSize, toolContext_.isBold, toolContext_.isItalic);
                        if (layout) {
                            BOOL isTrailing = FALSE;
                            BOOL isInside = FALSE;
                            DWRITE_HIT_TEST_METRICS hitMetrics = {};
                            float localX = d2dPt.x - textRect_.left;
                            float localY = d2dPt.y - textRect_.top;
                            if (localX < 0.0f) localX = 0.0f;
                            if (localY < 0.0f) localY = 0.0f;
                            layout->HitTestPoint(localX, localY, &isTrailing, &isInside, &hitMetrics);
                            size_t newCaret = hitMetrics.textPosition + (isTrailing ? 1 : 0);

                            if (!(GetKeyState(VK_SHIFT) & 0x8000)) {
                                selectionAnchor_ = newCaret;
                            }
                            selectionActive_ = newCaret;
                            caretIndex_ = newCaret;
                            isDragSelecting_ = true;
                            caretVisible_ = true;
                        }
                        return Action::None;
                    }
                    
                    // Clicked completely outside: Commit
                    CommitActiveText();
                }

                // Check if clicked on an existing committed text annotation to edit or delete it!
                auto hitText = document_.HitTestText(d2dPt);
                if (hitText) {
                    // Check if clicked its close button (top-right of frame)
                    float closeX = hitText->rect.right + 4.0f;
                    float closeY = hitText->rect.top - 4.0f;
                    float dx = d2dPt.x - closeX;
                    float dy = d2dPt.y - closeY;
                    if (dx * dx + dy * dy <= 64.0f) { // 8px radius hit area
                        document_.RemoveAnnotation(hitText);
                        break;
                    }

                    document_.RemoveAnnotation(hitText);
                    // C2: 暂存原对象，取消编辑时恢复（编辑提交则以新内容替换）。
                    editingSource_ = hitText;

                    isEditingText_ = true;
                    activeText_ = hitText->text;
                    textRect_ = hitText->rect;
                    toolContext_.fontSize = hitText->fontSize;
                    toolContext_.isBold = hitText->isBold;
                    toolContext_.isItalic = hitText->isItalic;
                    toolContext_.color = hitText->color;
                    toolbar_.SetCurrentContext(toolContext_);
                    
                    EnsurePrivateDWriteFactory();
                    float w = textRect_.right - textRect_.left;
                    auto layout = CreateLayoutHelper(dwriteFactoryPrivate_.Get(), activeText_, w, toolContext_.fontSize, toolContext_.isBold, toolContext_.isItalic);
                    if (layout) {
                        BOOL isTrailing = FALSE;
                        BOOL isInside = FALSE;
                        DWRITE_HIT_TEST_METRICS hitMetrics = {};
                        float localX = d2dPt.x - textRect_.left;
                        float localY = d2dPt.y - textRect_.top;
                        if (localX < 0.0f) localX = 0.0f;
                        if (localY < 0.0f) localY = 0.0f;
                        layout->HitTestPoint(localX, localY, &isTrailing, &isInside, &hitMetrics);
                        size_t newCaret = hitMetrics.textPosition + (isTrailing ? 1 : 0);
                        
                        selectionAnchor_ = newCaret;
                        selectionActive_ = newCaret;
                        caretIndex_ = newCaret;
                    } else {
                        caretIndex_ = activeText_.length();
                        selectionAnchor_ = caretIndex_;
                        selectionActive_ = caretIndex_;
                    }
                    isDragSelecting_ = true;
                    caretVisible_ = true;
                    UpdateTextLayoutMetrics();
                    break;
                }

                // Start a brand-new text box at clicked location (if clicked inside ROI)
                if (selection_.HasSelection() && selection_.GetCurrentRoi().contains(pt)) {
                    isEditingText_ = true;
                    activeText_ = L"";
                    caretIndex_ = 0;
                    selectionAnchor_ = 0;
                    selectionActive_ = 0;
                    isDragSelecting_ = false;
                    caretVisible_ = true;
                    float initialWidth = 200.0f;
                    float initialHeight = toolContext_.fontSize + 8.0f;
                    textRect_ = D2D1::RectF(
                        d2dPt.x,
                        d2dPt.y,
                        d2dPt.x + initialWidth,
                        d2dPt.y + initialHeight
                    );
                    UpdateTextLayoutMetrics();
                }
                break;
            }
            case WM_LBUTTONDBLCLK: {
                if (isEditingText_) {
                    bool clickInsideText = (d2dPt.x >= textRect_.left && d2dPt.x <= textRect_.right &&
                                            d2dPt.y >= textRect_.top && d2dPt.y <= textRect_.bottom);
                    if (clickInsideText) {
                        EnsurePrivateDWriteFactory();
                        float w = textRect_.right - textRect_.left;
                        auto layout = CreateLayoutHelper(dwriteFactoryPrivate_.Get(), activeText_, w, toolContext_.fontSize, toolContext_.isBold, toolContext_.isItalic);
                        if (layout) {
                            BOOL isTrailing = FALSE;
                            BOOL isInside = FALSE;
                            DWRITE_HIT_TEST_METRICS hitMetrics = {};
                            float localX = d2dPt.x - textRect_.left;
                            float localY = d2dPt.y - textRect_.top;
                            if (localX < 0.0f) localX = 0.0f;
                            if (localY < 0.0f) localY = 0.0f;
                            layout->HitTestPoint(localX, localY, &isTrailing, &isInside, &hitMetrics);
                            size_t hitIndex = hitMetrics.textPosition + (isTrailing ? 1 : 0);
                            SelectWord(hitIndex);
                            caretVisible_ = true;
                        }
                        return Action::None;
                    }
                }
                break;
            }
            case WM_MOUSEMOVE: {
                if (isEditingText_) {
                    float dx = static_cast<float>(pt.x - lastMousePt_.x);
                    float dy = static_cast<float>(pt.y - lastMousePt_.y);
                    lastMousePt_ = pt;

                    if (textDragState_ == TextDragState::Moving) {
                        textRect_.left += dx;
                        textRect_.right += dx;
                        textRect_.top += dy;
                        textRect_.bottom += dy;
                    } else if (textDragState_ == TextDragState::ResizingWidth) {
                        textRect_.right += dx;
                        if (textRect_.right - textRect_.left < 50.0f) {
                            textRect_.right = textRect_.left + 50.0f;
                        }
                        UpdateTextLayoutMetrics();
                    } else if (isDragSelecting_) {
                        EnsurePrivateDWriteFactory();
                        float w = textRect_.right - textRect_.left;
                        auto layout = CreateLayoutHelper(dwriteFactoryPrivate_.Get(), activeText_, w, toolContext_.fontSize, toolContext_.isBold, toolContext_.isItalic);
                        if (layout) {
                            BOOL isTrailing = FALSE;
                            BOOL isInside = FALSE;
                            DWRITE_HIT_TEST_METRICS hitMetrics = {};
                            float localX = d2dPt.x - textRect_.left;
                            float localY = d2dPt.y - textRect_.top;
                            if (localX < 0.0f) localX = 0.0f;
                            if (localY < 0.0f) localY = 0.0f;
                            layout->HitTestPoint(localX, localY, &isTrailing, &isInside, &hitMetrics);
                            size_t newCaret = hitMetrics.textPosition + (isTrailing ? 1 : 0);
                            selectionActive_ = newCaret;
                            caretIndex_ = newCaret;
                            caretVisible_ = true;
                        }
                    }
                }
                break;
            }
            case WM_LBUTTONUP: {
                textDragState_ = TextDragState::None;
                isDragSelecting_ = false;
                break;
            }
        }
    }
    return Action::None;
}

// CreateLayoutHelper moved to top of file

void InteractionStateMachine::EnsurePrivateDWriteFactory() {
    if (!dwriteFactoryPrivate_) {
        DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            &dwriteFactoryPrivate_
        );
    }
}

void InteractionStateMachine::CommitActiveText() {
    if (!isEditingText_) return;
    if (!activeText_.empty()) {
        auto textAnn = std::make_shared<TextAnnotation>(
            activeText_,
            textRect_,
            toolContext_.color,
            toolContext_.fontSize,
            toolContext_.isBold,
            toolContext_.isItalic
        );
        document_.AddAnnotation(textAnn);
    }
    // C2: 提交路径以新标注替换重编辑来源；无论是否提交都释放暂存。
    editingSource_.reset();
    isEditingText_ = false;
    activeText_.clear();
    caretIndex_ = 0;
    selectionAnchor_ = 0;
    selectionActive_ = 0;
    isDragSelecting_ = false;
    localUndoStack_.clear();
    localRedoStack_.clear();
    textRect_ = D2D1_RECT_F{};
    cachedTextLayout_.Reset();
    cachedTextFormat_.Reset();
}

void InteractionStateMachine::UpdateTextLayoutMetrics() {
    if (!isEditingText_) return;
    EnsurePrivateDWriteFactory();
    if (!dwriteFactoryPrivate_) return;

    float w = textRect_.right - textRect_.left;
    if (w < 50.0f) w = 50.0f;

    std::wstring displayText = activeText_.substr(0, caretIndex_) + imeCompositionString_ + activeText_.substr(caretIndex_);

    cachedTextLayout_ = CreateLayoutHelper(
        dwriteFactoryPrivate_.Get(),
        displayText,
        w,
        toolContext_.fontSize,
        toolContext_.isBold,
        toolContext_.isItalic
    );
    if (cachedTextLayout_) {
        DWRITE_TEXT_METRICS metrics = {};
        if (SUCCEEDED(cachedTextLayout_->GetMetrics(&metrics))) {
            float fontHeight = metrics.height;
            if (fontHeight < toolContext_.fontSize) {
                fontHeight = toolContext_.fontSize;
            }
            textRect_.bottom = textRect_.top + fontHeight;
        }
    }
}

bool InteractionStateMachine::HandleKeyboardMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (!isEditingText_) return false;
    (void)lParam;

    if (msg == WM_CHAR) {
        wchar_t ch = static_cast<wchar_t>(wParam);
        
        bool isSeparator = (ch == L' ' || ch == L'\t' || ch == L'\r' || ch == L'\n');
        if (isSeparator || HasTextSelection() || localUndoStack_.empty()) {
            PushLocalUndoState();
        }

        if (ch == L'\b') { // Backspace
            if (HasTextSelection()) {
                DeleteSelection();
                UpdateTextLayoutMetrics();
                UpdateImeWindowPosition(hwnd);
                caretVisible_ = true;
            } else if (caretIndex_ > 0 && !activeText_.empty()) {
                activeText_.erase(caretIndex_ - 1, 1);
                caretIndex_--;
                selectionAnchor_ = caretIndex_;
                selectionActive_ = caretIndex_;
                UpdateTextLayoutMetrics();
                UpdateImeWindowPosition(hwnd);
                caretVisible_ = true;
            }
        } else if (ch == L'\r' || ch == L'\n') { // Enter -> Newline
            if (HasTextSelection()) {
                DeleteSelection();
            }
            activeText_.insert(caretIndex_, L"\n");
            caretIndex_++;
            selectionAnchor_ = caretIndex_;
            selectionActive_ = caretIndex_;
            UpdateTextLayoutMetrics();
            UpdateImeWindowPosition(hwnd);
            caretVisible_ = true;
        } else if (ch == L'\x1B') { // Escape -> Commit
            CommitActiveText();
        } else if (ch == L'\t') { // Tab
            if (HasTextSelection()) {
                DeleteSelection();
            }
            activeText_.insert(caretIndex_, L"    ");
            caretIndex_ += 4;
            selectionAnchor_ = caretIndex_;
            selectionActive_ = caretIndex_;
            UpdateTextLayoutMetrics();
            UpdateImeWindowPosition(hwnd);
            caretVisible_ = true;
        } else if (ch >= 32) { // Printable chars
            if (HasTextSelection()) {
                DeleteSelection();
            }
            activeText_.insert(caretIndex_, 1, ch);
            caretIndex_++;
            selectionAnchor_ = caretIndex_;
            selectionActive_ = caretIndex_;
            UpdateTextLayoutMetrics();
            UpdateImeWindowPosition(hwnd);
            caretVisible_ = true;
        }
        return true;
    }

    if (msg == WM_KEYDOWN) {
        switch (wParam) {
            case VK_LEFT: {
                bool shiftHeld = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                if (caretIndex_ > 0) {
                    caretIndex_--;
                }
                if (shiftHeld) {
                    selectionActive_ = caretIndex_;
                } else {
                    selectionAnchor_ = caretIndex_;
                    selectionActive_ = caretIndex_;
                }
                caretVisible_ = true;
                UpdateImeWindowPosition(hwnd);
                return true;
            }
            case VK_RIGHT: {
                bool shiftHeld = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                if (caretIndex_ < activeText_.length()) {
                    caretIndex_++;
                }
                if (shiftHeld) {
                    selectionActive_ = caretIndex_;
                } else {
                    selectionAnchor_ = caretIndex_;
                    selectionActive_ = caretIndex_;
                }
                caretVisible_ = true;
                UpdateImeWindowPosition(hwnd);
                return true;
            }
            case VK_UP: {
                bool shiftHeld = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                EnsurePrivateDWriteFactory();
                if (dwriteFactoryPrivate_) {
                    float w = textRect_.right - textRect_.left;
                    DWRITE_FONT_WEIGHT weight = toolContext_.isBold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL;
                    DWRITE_FONT_STYLE style = toolContext_.isItalic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL;
                    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
                    if (SUCCEEDED(dwriteFactoryPrivate_->CreateTextFormat(L"Microsoft YaHei", nullptr, weight, style, DWRITE_FONT_STRETCH_NORMAL, toolContext_.fontSize, L"zh-cn", &fmt))) {
                        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
                        if (SUCCEEDED(dwriteFactoryPrivate_->CreateTextLayout(activeText_.c_str(), static_cast<UINT32>(activeText_.length()), fmt.Get(), w, 10000.0f, &layout))) {
                            float localX = 0, localY = 0;
                            DWRITE_HIT_TEST_METRICS hitMetrics = {};
                            UINT32 pos = (caretIndex_ > 0) ? static_cast<UINT32>(caretIndex_ - 1) : 0;
                            BOOL trail = (caretIndex_ > 0) ? TRUE : FALSE;
                            if (SUCCEEDED(layout->HitTestTextPosition(pos, trail, &localX, &localY, &hitMetrics))) {
                                float targetY = localY - hitMetrics.height / 2.0f - 2.0f;
                                size_t newCaret = 0;
                                if (targetY >= 0.0f) {
                                    BOOL isTrailingNew = FALSE;
                                    BOOL isInsideNew = FALSE;
                                    DWRITE_HIT_TEST_METRICS hitMetricsNew = {};
                                    if (SUCCEEDED(layout->HitTestPoint(localX, targetY, &isTrailingNew, &isInsideNew, &hitMetricsNew))) {
                                        newCaret = hitMetricsNew.textPosition + (isTrailingNew ? 1 : 0);
                                    }
                                } else {
                                    newCaret = 0;
                                }
                                caretIndex_ = newCaret;
                                if (shiftHeld) {
                                    selectionActive_ = caretIndex_;
                                } else {
                                    selectionAnchor_ = caretIndex_;
                                    selectionActive_ = caretIndex_;
                                }
                            }
                        }
                    }
                }
                caretVisible_ = true;
                UpdateImeWindowPosition(hwnd);
                return true;
            }
            case VK_DOWN: {
                bool shiftHeld = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                EnsurePrivateDWriteFactory();
                if (dwriteFactoryPrivate_) {
                    float w = textRect_.right - textRect_.left;
                    DWRITE_FONT_WEIGHT weight = toolContext_.isBold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL;
                    DWRITE_FONT_STYLE style = toolContext_.isItalic ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL;
                    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
                    if (SUCCEEDED(dwriteFactoryPrivate_->CreateTextFormat(L"Microsoft YaHei", nullptr, weight, style, DWRITE_FONT_STRETCH_NORMAL, toolContext_.fontSize, L"zh-cn", &fmt))) {
                        Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
                        if (SUCCEEDED(dwriteFactoryPrivate_->CreateTextLayout(activeText_.c_str(), static_cast<UINT32>(activeText_.length()), fmt.Get(), w, 10000.0f, &layout))) {
                            float localX = 0, localY = 0;
                            DWRITE_HIT_TEST_METRICS hitMetrics = {};
                            UINT32 pos = (caretIndex_ > 0) ? static_cast<UINT32>(caretIndex_ - 1) : 0;
                            BOOL trail = (caretIndex_ > 0) ? TRUE : FALSE;
                            if (SUCCEEDED(layout->HitTestTextPosition(pos, trail, &localX, &localY, &hitMetrics))) {
                                float targetY = localY + hitMetrics.height + 2.0f;
                                size_t newCaret = activeText_.length();
                                BOOL isTrailingNew = FALSE;
                                BOOL isInsideNew = FALSE;
                                DWRITE_HIT_TEST_METRICS hitMetricsNew = {};
                                if (SUCCEEDED(layout->HitTestPoint(localX, targetY, &isTrailingNew, &isInsideNew, &hitMetricsNew))) {
                                    newCaret = hitMetricsNew.textPosition + (isTrailingNew ? 1 : 0);
                                }
                                caretIndex_ = newCaret;
                                if (shiftHeld) {
                                    selectionActive_ = caretIndex_;
                                } else {
                                    selectionAnchor_ = caretIndex_;
                                    selectionActive_ = caretIndex_;
                                }
                            }
                        }
                    }
                }
                caretVisible_ = true;
                UpdateImeWindowPosition(hwnd);
                return true;
            }
            case VK_DELETE:
                if (HasTextSelection()) {
                    PushLocalUndoState();
                    DeleteSelection();
                    UpdateTextLayoutMetrics();
                    UpdateImeWindowPosition(hwnd);
                    caretVisible_ = true;
                } else if (caretIndex_ < activeText_.length()) {
                    PushLocalUndoState();
                    activeText_.erase(caretIndex_, 1);
                    UpdateTextLayoutMetrics();
                    UpdateImeWindowPosition(hwnd);
                    caretVisible_ = true;
                }
                return true;
            case VK_HOME: {
                bool shiftHeld = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                caretIndex_ = 0;
                if (shiftHeld) {
                    selectionActive_ = caretIndex_;
                } else {
                    selectionAnchor_ = caretIndex_;
                    selectionActive_ = caretIndex_;
                }
                caretVisible_ = true;
                UpdateImeWindowPosition(hwnd);
                return true;
            }
            case VK_END: {
                bool shiftHeld = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                caretIndex_ = activeText_.length();
                if (shiftHeld) {
                    selectionActive_ = caretIndex_;
                } else {
                    selectionAnchor_ = caretIndex_;
                    selectionActive_ = caretIndex_;
                }
                caretVisible_ = true;
                UpdateImeWindowPosition(hwnd);
                return true;
            }
            case 'C': {
                if (GetKeyState(VK_CONTROL) & 0x8000) {
                    if (HasTextSelection()) {
                        size_t start = std::min(selectionAnchor_, selectionActive_);
                        size_t end = std::max(selectionAnchor_, selectionActive_);
                        std::wstring selText = activeText_.substr(start, end - start);
                        if (OpenClipboard(hwnd)) {
                            EmptyClipboard();
                            size_t sz = (selText.length() + 1) * sizeof(wchar_t);
                            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, sz);
                            if (hMem) {
                                void* ptr = GlobalLock(hMem);
                                memcpy(ptr, selText.c_str(), sz);
                                GlobalUnlock(hMem);
                                SetClipboardData(CF_UNICODETEXT, hMem);
                            }
                            CloseClipboard();
                        }
                    }
                    return true;
                }
                break;
            }
            case 'X': {
                if (GetKeyState(VK_CONTROL) & 0x8000) {
                    if (HasTextSelection()) {
                        PushLocalUndoState();
                        size_t start = std::min(selectionAnchor_, selectionActive_);
                        size_t end = std::max(selectionAnchor_, selectionActive_);
                        std::wstring selText = activeText_.substr(start, end - start);
                        if (OpenClipboard(hwnd)) {
                            EmptyClipboard();
                            size_t sz = (selText.length() + 1) * sizeof(wchar_t);
                            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, sz);
                            if (hMem) {
                                void* ptr = GlobalLock(hMem);
                                memcpy(ptr, selText.c_str(), sz);
                                GlobalUnlock(hMem);
                                SetClipboardData(CF_UNICODETEXT, hMem);
                            }
                            CloseClipboard();
                        }
                        DeleteSelection();
                        UpdateTextLayoutMetrics();
                        UpdateImeWindowPosition(hwnd);
                        caretVisible_ = true;
                    }
                    return true;
                }
                break;
            }
            case 'V': {
                if (GetKeyState(VK_CONTROL) & 0x8000) {
                    PushLocalUndoState();
                    if (HasTextSelection()) {
                        DeleteSelection();
                    }
                    if (OpenClipboard(hwnd)) {
                        HANDLE hData = GetClipboardData(CF_UNICODETEXT);
                        if (hData) {
                            wchar_t* pText = static_cast<wchar_t*>(GlobalLock(hData));
                            if (pText) {
                                std::wstring pasted(pText);
                                pasted.erase(std::remove(pasted.begin(), pasted.end(), L'\r'), pasted.end());
                                activeText_.insert(caretIndex_, pasted);
                                caretIndex_ += pasted.length();
                                selectionAnchor_ = caretIndex_;
                                selectionActive_ = caretIndex_;
                                UpdateTextLayoutMetrics();
                                UpdateImeWindowPosition(hwnd);
                                caretVisible_ = true;
                            }
                            GlobalUnlock(hData);
                        }
                        CloseClipboard();
                    }
                    return true;
                }
                break;
            }
            case 'Z': {
                if (GetKeyState(VK_CONTROL) & 0x8000) {
                    if (!localUndoStack_.empty()) {
                        localRedoStack_.push_back({ activeText_, caretIndex_, selectionAnchor_, selectionActive_, textRect_ });
                        auto last = localUndoStack_.back();
                        localUndoStack_.pop_back();
                        activeText_ = last.text;
                        caretIndex_ = last.caretIndex;
                        selectionAnchor_ = last.selectionAnchor;
                        selectionActive_ = last.selectionActive;
                        textRect_ = last.textRect;
                        UpdateTextLayoutMetrics();
                        UpdateImeWindowPosition(hwnd);
                        caretVisible_ = true;
                    }
                    return true;
                }
                break;
            }
            case 'Y': {
                if (GetKeyState(VK_CONTROL) & 0x8000) {
                    if (!localRedoStack_.empty()) {
                        localUndoStack_.push_back({ activeText_, caretIndex_, selectionAnchor_, selectionActive_, textRect_ });
                        auto next = localRedoStack_.back();
                        localRedoStack_.pop_back();
                        activeText_ = next.text;
                        caretIndex_ = next.caretIndex;
                        selectionAnchor_ = next.selectionAnchor;
                        selectionActive_ = next.selectionActive;
                        textRect_ = next.textRect;
                        UpdateTextLayoutMetrics();
                        UpdateImeWindowPosition(hwnd);
                        caretVisible_ = true;
                    }
                    return true;
                }
                break;
            }
        }
    }
    return false;
}

void InteractionStateMachine::UpdateImeWindowPosition(HWND hwnd) {
    if (!isEditingText_) return;
    EnsurePrivateDWriteFactory();
    if (!dwriteFactoryPrivate_) return;

    float w = textRect_.right - textRect_.left;
    std::wstring displayText = activeText_.substr(0, caretIndex_) + imeCompositionString_ + activeText_.substr(caretIndex_);
    auto layout = CreateLayoutHelper(dwriteFactoryPrivate_.Get(), displayText, w, toolContext_.fontSize, toolContext_.isBold, toolContext_.isItalic);
    if (!layout) return;

    float caretX = textRect_.left;
    float caretY = textRect_.top;
    float caretH = toolContext_.fontSize;

    // Position the IME window exactly at the start of the composition string (pinyin start)
    size_t visualPos = caretIndex_;

    if (!displayText.empty()) {
        float localX = 0, localY = 0;
        DWRITE_HIT_TEST_METRICS metrics = {};
        UINT32 pos = (visualPos > 0) ? static_cast<UINT32>(visualPos - 1) : 0;
        BOOL trail = (visualPos > 0) ? TRUE : FALSE;
        if (SUCCEEDED(layout->HitTestTextPosition(pos, trail, &localX, &localY, &metrics))) {
            caretX = textRect_.left + localX;
            caretY = textRect_.top + localY;
            caretH = metrics.height;
        }
    }

    int clientX = static_cast<int>(caretX);
    int clientY = static_cast<int>(caretY);

    HIMC hIMC = ImmGetContext(hwnd);
    if (hIMC) {
        // 1. Sync composition font size, style & face name
        LOGFONTW lf = {};
        lf.lfHeight = -static_cast<LONG>(toolContext_.fontSize);
        lf.lfWeight = toolContext_.isBold ? FW_BOLD : FW_NORMAL;
        lf.lfItalic = toolContext_.isItalic ? TRUE : FALSE;
        lf.lfCharSet = DEFAULT_CHARSET;
        wcscpy_s(lf.lfFaceName, L"Microsoft YaHei");
        ImmSetCompositionFontW(hIMC, &lf);

        // 2. Position the inline pinyin composition window
        COMPOSITIONFORM compForm = {};
        compForm.dwStyle = CFS_POINT;
        compForm.ptCurrentPos.x = clientX;
        compForm.ptCurrentPos.y = clientY;
        ImmSetCompositionWindow(hIMC, &compForm);

        // 3. Exclude composition/caret area from candidate list overlap
        CANDIDATEFORM candForm = {};
        candForm.dwIndex = 0;
        candForm.dwStyle = CFS_EXCLUDE;
        candForm.rcArea.left = clientX;
        candForm.rcArea.top = clientY;
        candForm.rcArea.right = clientX + static_cast<int>(toolContext_.fontSize);
        candForm.rcArea.bottom = clientY + static_cast<int>(caretH);
        candForm.ptCurrentPos.x = clientX;
        candForm.ptCurrentPos.y = clientY + static_cast<int>(caretH);
        ImmSetCandidateWindow(hIMC, &candForm);

        ImmReleaseContext(hwnd, hIMC);
    }
}

void InteractionStateMachine::PushLocalUndoState() {
    TextState state = { activeText_, caretIndex_, selectionAnchor_, selectionActive_, textRect_ };
    if (!localUndoStack_.empty()) {
        const auto& last = localUndoStack_.back();
        if (last.text == state.text && last.caretIndex == state.caretIndex &&
            last.selectionAnchor == state.selectionAnchor && last.selectionActive == state.selectionActive) {
            return;
        }
    }
    localUndoStack_.push_back(state);
    if (localUndoStack_.size() > 50) {
        localUndoStack_.erase(localUndoStack_.begin());
    }
    localRedoStack_.clear();
}

void InteractionStateMachine::DeleteSelection() {
    if (!HasTextSelection()) return;
    size_t start = std::min(selectionAnchor_, selectionActive_);
    size_t end = std::max(selectionAnchor_, selectionActive_);
    if (end > activeText_.length()) end = activeText_.length();
    if (start < end) {
        activeText_.erase(start, end - start);
    }
    caretIndex_ = start;
    selectionAnchor_ = caretIndex_;
    selectionActive_ = caretIndex_;
}

void InteractionStateMachine::SelectWord(size_t clickPos) {
    EnsurePrivateDWriteFactory();
    if (!dwriteFactoryPrivate_ || activeText_.empty()) return;

    float w = textRect_.right - textRect_.left;
    if (w < 50.0f) w = 50.0f;

    auto layout = CreateLayoutHelper(
        dwriteFactoryPrivate_.Get(),
        activeText_,
        w,
        toolContext_.fontSize,
        toolContext_.isBold,
        toolContext_.isItalic
    );
    if (!layout) return;

    UINT32 maxCount = static_cast<UINT32>(activeText_.length()) + 2;
    std::vector<DWRITE_CLUSTER_METRICS> clusterMetrics(maxCount);
    UINT32 actualCount = 0;
    if (SUCCEEDED(layout->GetClusterMetrics(clusterMetrics.data(), maxCount, &actualCount))) {
        size_t currentPos = 0;
        size_t targetClusterIndex = 0;
        for (size_t i = 0; i < actualCount; ++i) {
            size_t nextPos = currentPos + clusterMetrics[i].length;
            if (clickPos >= currentPos && clickPos < nextPos) {
                targetClusterIndex = i;
                break;
            }
            currentPos = nextPos;
        }

        size_t wordStart = currentPos;
        size_t tempPos = currentPos;
        for (int i = static_cast<int>(targetClusterIndex); i >= 0; --i) {
            wchar_t ch = activeText_[tempPos];
            if (iswspace(ch) || iswpunct(ch)) {
                if (i != static_cast<int>(targetClusterIndex)) {
                    wordStart = tempPos + clusterMetrics[i].length;
                } else {
                    wordStart = tempPos;
                }
                break;
            }
            if (i == 0) {
                wordStart = 0;
            }
            if (i > 0) {
                tempPos -= clusterMetrics[i - 1].length;
            }
        }

        size_t wordEnd = wordStart;
        tempPos = wordStart;
        size_t startCluster = 0;
        size_t acc = 0;
        for (size_t i = 0; i < actualCount; ++i) {
            if (tempPos == acc) {
                startCluster = i;
                break;
            }
            acc += clusterMetrics[i].length;
        }

        for (size_t i = startCluster; i < actualCount; ++i) {
            wchar_t ch = activeText_[tempPos];
            if (iswspace(ch) || iswpunct(ch)) {
                if (i != startCluster) {
                    wordEnd = tempPos;
                } else {
                    wordEnd = tempPos + clusterMetrics[i].length;
                }
                break;
            }
            tempPos += clusterMetrics[i].length;
            if (i == actualCount - 1) {
                wordEnd = activeText_.length();
            }
        }

        if (wordStart < wordEnd) {
            selectionAnchor_ = wordStart;
            selectionActive_ = wordEnd;
            caretIndex_ = wordEnd;
        }
    }
}

InteractionStateMachine::Action InteractionStateMachine::ProcessMouseWheelEx(short delta, const PointPx& pt) {
    // 1. Only process in SelectRoi mode, and when not actively editing text
    if (currentMode_ != Mode::SelectRoi || isEditingText_) {
        return Action::None;
    }

    // 2. Determine query point: use selection center in Selected state, otherwise mouse cursor position
    PointPx queryPt = pt;
    if (selection_.GetState() == SelectionModel::State::Selected) {
        RectPx currentRoi = selection_.GetCurrentRoi();
        queryPt = PointPx{ currentRoi.x + currentRoi.width / 2, currentRoi.y + currentRoi.height / 2 };
    }

    // Fetch candidates at the determined query point
    std::vector<RectPx> candidates = uiaSnapping_.HitTestAll(queryPt);
    // Fallback to top-level window list if no UIA elements detected under query point
    if (candidates.empty()) {
        for (const auto& r : windowRects_) {
            if (r.contains(queryPt)) {
                candidates.push_back(r);
                break;
            }
        }
    }

    int n = static_cast<int>(candidates.size());

    // 3. Hover / Unlocked State (SelectionModel::State::None)
    if (selection_.GetState() == SelectionModel::State::None) {
        if (n == 0) return Action::None;

        // If candidates changed or we don't have matching tabCandidates_, re-cache
        bool candidatesMatch = (candidates.size() == tabCandidates_.size());
        if (candidatesMatch) {
            for (size_t i = 0; i < candidates.size(); ++i) {
                if (candidates[i].x != tabCandidates_[i].x ||
                    candidates[i].y != tabCandidates_[i].y ||
                    candidates[i].width != tabCandidates_[i].width ||
                    candidates[i].height != tabCandidates_[i].height) {
                    candidatesMatch = false;
                    break;
                }
            }
        }
        if (!candidatesMatch) {
            tabCandidates_ = candidates;
            tabIndex_ = 0;
        }

        if (delta > 0) {
            // Scroll Up: Enlarge selection (move towards parent/shallower element)
            if (tabIndex_ < n - 1) {
                tabIndex_++;
            }
        } else if (delta < 0) {
            // Scroll Down: Shrink selection (move towards child/deeper element)
            if (tabIndex_ > 0) {
                tabIndex_--;
            }
        }

        hoveredWindowRect_ = tabCandidates_[tabIndex_];
        return Action::None;
    }

    // 4. Locked/Selected State (SelectionModel::State::Selected)
    if (selection_.GetState() == SelectionModel::State::Selected) {
        RectPx currentRoi = selection_.GetCurrentRoi();

        bool roiMatched = false;
        int matchIdx = -1;
        for (int i = 0; i < n; ++i) {
            if (candidates[i].x == currentRoi.x &&
                candidates[i].y == currentRoi.y &&
                candidates[i].width == currentRoi.width &&
                candidates[i].height == currentRoi.height) {
                roiMatched = true;
                matchIdx = i;
                break;
            }
        }

        RectPx targetRect;
        bool foundTarget = false;

        if (roiMatched) {
            if (delta > 0) {
                // Scroll Up: Enlarge selection to parent
                if (matchIdx + 1 < n) {
                    targetRect = candidates[matchIdx + 1];
                    foundTarget = true;
                }
            } else if (delta < 0) {
                // Scroll Down: Shrink selection to child
                if (matchIdx - 1 >= 0) {
                    targetRect = candidates[matchIdx - 1];
                    foundTarget = true;
                }
            }
        } else {
            // If ROI is custom (does not match any candidates), find containing / contained candidates
            if (delta > 0) {
                // Scroll Up: Find the smallest candidate that completely contains the current ROI
                for (int i = 0; i < n; ++i) {
                    const auto& cand = candidates[i];
                    if (cand.width * cand.height > currentRoi.width * currentRoi.height &&
                        cand.x <= currentRoi.x && cand.y <= currentRoi.y &&
                        cand.x + cand.width >= currentRoi.x + currentRoi.width &&
                        cand.y + cand.height >= currentRoi.y + currentRoi.height) {
                        targetRect = cand;
                        foundTarget = true;
                        break;
                    }
                }
            } else if (delta < 0) {
                // Scroll Down: Find the largest candidate that is completely contained inside the current ROI
                // Since candidates is deepest-first, we loop backwards to find the largest contained child
                for (int i = n - 1; i >= 0; --i) {
                    const auto& cand = candidates[i];
                    if (cand.width * cand.height < currentRoi.width * currentRoi.height &&
                        cand.x >= currentRoi.x && cand.y >= currentRoi.y &&
                        cand.x + cand.width <= currentRoi.x + currentRoi.width &&
                        cand.y + cand.height <= currentRoi.y + currentRoi.height) {
                        targetRect = cand;
                        foundTarget = true;
                        break;
                    }
                }
            }
        }

        // 5. Proportional scale fallback if no suitable window snaps found
        if (!foundTarget) {
            float scaleFactor = (delta > 0) ? 1.05f : 0.95f;
            int targetW = static_cast<int>(currentRoi.width * scaleFactor);
            int targetH = static_cast<int>(currentRoi.height * scaleFactor);

            // Minimum boundary: 10px
            if (targetW < 10) targetW = 10;
            if (targetH < 10) targetH = 10;

            // Maximum boundary: current monitor bounds
            MonitorSnapshot currentMonitor{};
            bool foundMonitor = false;
            // Center of selection determines which monitor bounds to query
            int cx = currentRoi.x + currentRoi.width / 2;
            int cy = currentRoi.y + currentRoi.height / 2;
            for (const auto& monitor : cachedMonitors_) {
                if (cx >= monitor.virtualBounds.x && cx < monitor.virtualBounds.x + monitor.virtualBounds.width &&
                    cy >= monitor.virtualBounds.y && cy < monitor.virtualBounds.y + monitor.virtualBounds.height) {
                    currentMonitor = monitor;
                    foundMonitor = true;
                    break;
                }
            }
            if (!foundMonitor && !cachedMonitors_.empty()) {
                currentMonitor = cachedMonitors_[0];
                foundMonitor = true;
            }

            if (foundMonitor) {
                if (targetW > currentMonitor.virtualBounds.width) targetW = currentMonitor.virtualBounds.width;
                if (targetH > currentMonitor.virtualBounds.height) targetH = currentMonitor.virtualBounds.height;

                int targetX = cx - targetW / 2;
                int targetY = cy - targetH / 2;

                // Clamp to monitor virtual boundaries
                if (targetX < currentMonitor.virtualBounds.x) targetX = currentMonitor.virtualBounds.x;
                if (targetY < currentMonitor.virtualBounds.y) targetY = currentMonitor.virtualBounds.y;
                if (targetX + targetW > currentMonitor.virtualBounds.x + currentMonitor.virtualBounds.width) {
                    targetX = currentMonitor.virtualBounds.x + currentMonitor.virtualBounds.width - targetW;
                }
                if (targetY + targetH > currentMonitor.virtualBounds.y + currentMonitor.virtualBounds.height) {
                    targetY = currentMonitor.virtualBounds.y + currentMonitor.virtualBounds.height - targetH;
                }

                targetRect = RectPx{ targetX, targetY, targetW, targetH };
                foundTarget = true;
            }
        }

        if (foundTarget) {
            selection_.SetRoi(targetRect);
            // Update toolbar alignment
            toolbar_.SetSelectionRect(D2D1::RectF(
                static_cast<float>(targetRect.x),
                static_cast<float>(targetRect.y),
                static_cast<float>(targetRect.x + targetRect.width),
                static_cast<float>(targetRect.y + targetRect.height)
            ));
        }
    }

    return Action::None;
}

