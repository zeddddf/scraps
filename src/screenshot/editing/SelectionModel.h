#pragma once
#include "platform/MonitorTopology.h"
#include <optional>

class SelectionModel {
public:
    enum class State { None, Creating, Selected, Moving, Resizing };
    enum class ResizeHandle { None, TopLeft, TopCenter, TopRight, RightCenter, BottomRight, BottomCenter, BottomLeft, LeftCenter };

    SelectionModel();

    void HandleMouseDown(const PointPx& pt);
    void HandleMouseMove(const PointPx& pt);
    void HandleMouseUp(const PointPx& pt);
    
    bool HasSelection() const { return state_ != State::None && (std::abs(roi_.width) > 0 && std::abs(roi_.height) > 0); }
    RectPx GetCurrentRoi() const;
    State GetState() const { return state_; }
    ResizeHandle GetActiveHandle() const { return activeHandle_; }
    void SetRoi(const RectPx& r) { roi_ = r; state_ = State::Selected; }
    ResizeHandle HitTestHandle(const PointPx& pt) const;
    void Reset() { state_ = State::None; roi_ = {0,0,0,0}; }

private:
    State state_;
    RectPx roi_;
    PointPx dragStart_;
    RectPx originalRoi_;
    ResizeHandle activeHandle_;

    void UpdateResize(const PointPx& pt);
    void NormalizeRoi();
};
