#include "editing/SelectionModel.h"
#include <algorithm>
#include <cmath>

const int HANDLE_SIZE = 8; // Hit-test threshold for handles

SelectionModel::SelectionModel() : state_(State::None), activeHandle_(ResizeHandle::None) {
    roi_ = {0, 0, 0, 0};
    originalRoi_ = {0, 0, 0, 0};
    dragStart_ = {0, 0};
}

RectPx SelectionModel::GetCurrentRoi() const {
    if (roi_.width < 0 || roi_.height < 0) {
        // Return normalized if needed
        int nx = std::min(roi_.x, roi_.right());
        int ny = std::min(roi_.y, roi_.bottom());
        int nw = std::abs(roi_.width);
        int nh = std::abs(roi_.height);
        return {nx, ny, nw, nh};
    }
    return roi_;
}

void SelectionModel::NormalizeRoi() {
    roi_ = GetCurrentRoi();
}

SelectionModel::ResizeHandle SelectionModel::HitTestHandle(const PointPx& pt) const {
    if (!HasSelection()) return ResizeHandle::None;

    // Check 8 points first to prioritize handle resizing over body moving
    auto nearPt = [](const PointPx& p1, int x, int y) {
        return std::abs(p1.x - x) <= HANDLE_SIZE && std::abs(p1.y - y) <= HANDLE_SIZE;
    };

    int l = roi_.x;
    int t = roi_.y;
    int r = roi_.right();
    int b = roi_.bottom();
    int mx = l + roi_.width / 2;
    int my = t + roi_.height / 2;

    if (nearPt(pt, l, t)) return ResizeHandle::TopLeft;
    if (nearPt(pt, mx, t)) return ResizeHandle::TopCenter;
    if (nearPt(pt, r, t)) return ResizeHandle::TopRight;
    if (nearPt(pt, r, my)) return ResizeHandle::RightCenter;
    if (nearPt(pt, r, b)) return ResizeHandle::BottomRight;
    if (nearPt(pt, mx, b)) return ResizeHandle::BottomCenter;
    if (nearPt(pt, l, b)) return ResizeHandle::BottomLeft;
    if (nearPt(pt, l, my)) return ResizeHandle::LeftCenter;

    return ResizeHandle::None;
}

void SelectionModel::HandleMouseDown(const PointPx& pt) {
    dragStart_ = pt;
    if (state_ == State::None) {
        state_ = State::Creating;
        roi_ = {pt.x, pt.y, 0, 0};
    } else if (state_ == State::Selected) {
        activeHandle_ = HitTestHandle(pt);
        if (activeHandle_ != ResizeHandle::None) {
            state_ = State::Resizing;
            originalRoi_ = roi_;
        } else if (roi_.contains(pt)) {
            state_ = State::Moving;
            originalRoi_ = roi_;
        } else {
            // Click outside resets
            state_ = State::Creating;
            roi_ = {pt.x, pt.y, 0, 0};
        }
    }
}

void SelectionModel::UpdateResize(const PointPx& pt) {
    int dx = pt.x - dragStart_.x;
    int dy = pt.y - dragStart_.y;

    int l = originalRoi_.x;
    int t = originalRoi_.y;
    int r = originalRoi_.right();
    int b = originalRoi_.bottom();

    switch (activeHandle_) {
        case ResizeHandle::TopLeft:     l += dx; t += dy; break;
        case ResizeHandle::TopCenter:   t += dy; break;
        case ResizeHandle::TopRight:    r += dx; t += dy; break;
        case ResizeHandle::RightCenter: r += dx; break;
        case ResizeHandle::BottomRight: r += dx; b += dy; break;
        case ResizeHandle::BottomCenter:b += dy; break;
        case ResizeHandle::BottomLeft:  l += dx; b += dy; break;
        case ResizeHandle::LeftCenter:  l += dx; break;
        default: break;
    }

    // Apply virtual screen boundary constraints to resizing edges
    int screenL = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int screenT = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int screenR = screenL + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int screenB = screenT + GetSystemMetrics(SM_CYVIRTUALSCREEN);

    l = std::max(screenL, std::min(l, screenR));
    t = std::max(screenT, std::min(t, screenB));
    r = std::max(screenL, std::min(r, screenR));
    b = std::max(screenT, std::min(b, screenB));

    roi_ = {l, t, r - l, b - t};
}

void SelectionModel::HandleMouseMove(const PointPx& pt) {
    int screenL = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int screenT = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int screenR = screenL + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int screenB = screenT + GetSystemMetrics(SM_CYVIRTUALSCREEN);

    if (state_ == State::Creating) {
        int clampX = std::max(screenL, std::min(pt.x, screenR));
        int clampY = std::max(screenT, std::min(pt.y, screenB));
        roi_.width = clampX - roi_.x;
        roi_.height = clampY - roi_.y;
    } else if (state_ == State::Moving) {
        int dx = pt.x - dragStart_.x;
        int dy = pt.y - dragStart_.y;
        
        int x = originalRoi_.x + dx;
        int y = originalRoi_.y + dy;
        
        // Prevent moving the selection rectangle outside screen bounds
        x = std::max(screenL, std::min(x, screenR - originalRoi_.width));
        y = std::max(screenT, std::min(y, screenB - originalRoi_.height));
        
        roi_ = {x, y, originalRoi_.width, originalRoi_.height};
    } else if (state_ == State::Resizing) {
        UpdateResize(pt);
    }
}

void SelectionModel::HandleMouseUp(const PointPx& pt) {
    if (state_ == State::Creating || state_ == State::Moving || state_ == State::Resizing) {
        HandleMouseMove(pt);
        NormalizeRoi();
        
        if (roi_.width < 10 || roi_.height < 10) {
            // Too small, discard
            state_ = State::None;
            roi_ = {0, 0, 0, 0};
        } else {
            state_ = State::Selected;
        }
    }
}
