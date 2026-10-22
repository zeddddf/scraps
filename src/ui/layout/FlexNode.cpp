#include "ui/layout/FlexNode.h"

namespace clipd2d::ui {

FlexNode::FlexNode() : node_(YGNodeNew()) {
  YGNodeSetContext(node_, this);
}

FlexNode::~FlexNode() {
  if (node_) {
    YGNodeFree(node_);
    node_ = nullptr;
  }
}

FlexNode::FlexNode(FlexNode&& other) noexcept : node_(other.node_), children_(std::move(other.children_)) {
  other.node_ = nullptr;
  if (node_) {
    YGNodeSetContext(node_, this);
  }
}

FlexNode& FlexNode::operator=(FlexNode&& other) noexcept {
  if (this != &other) {
    if (node_) {
      YGNodeFree(node_);
    }
    node_ = other.node_;
    children_ = std::move(other.children_);
    other.node_ = nullptr;
    if (node_) {
      YGNodeSetContext(node_, this);
    }
  }
  return *this;
}

FlexNode& FlexNode::AddChild(std::shared_ptr<FlexNode> child) {
  uint32_t index = static_cast<uint32_t>(children_.size());
  children_.push_back(child);
  YGNodeInsertChild(node_, child->GetNode(), index);
  return *this;
}

FlexNode& FlexNode::InsertChild(std::shared_ptr<FlexNode> child, uint32_t index) {
  children_.insert(children_.begin() + index, child);
  YGNodeInsertChild(node_, child->GetNode(), index);
  return *this;
}

void FlexNode::CalculateLayout(float available_width, float available_height, YGDirection direction) {
  YGNodeCalculateLayout(node_, available_width, available_height, direction);
}

float FlexNode::GetLeft() const { return YGNodeLayoutGetLeft(node_); }
float FlexNode::GetTop() const { return YGNodeLayoutGetTop(node_); }
float FlexNode::GetWidth() const { return YGNodeLayoutGetWidth(node_); }
float FlexNode::GetHeight() const { return YGNodeLayoutGetHeight(node_); }

FlexNode& FlexNode::Width(float width) { YGNodeStyleSetWidth(node_, width); return *this; }
FlexNode& FlexNode::Height(float height) { YGNodeStyleSetHeight(node_, height); return *this; }
FlexNode& FlexNode::WidthPercent(float percent) { YGNodeStyleSetWidthPercent(node_, percent); return *this; }
FlexNode& FlexNode::HeightPercent(float percent) { YGNodeStyleSetHeightPercent(node_, percent); return *this; }
FlexNode& FlexNode::WidthAuto() { YGNodeStyleSetWidthAuto(node_); return *this; }
FlexNode& FlexNode::HeightAuto() { YGNodeStyleSetHeightAuto(node_); return *this; }

FlexNode& FlexNode::MinWidth(float min_width) { YGNodeStyleSetMinWidth(node_, min_width); return *this; }
FlexNode& FlexNode::MinHeight(float min_height) { YGNodeStyleSetMinHeight(node_, min_height); return *this; }
FlexNode& FlexNode::MaxWidth(float max_width) { YGNodeStyleSetMaxWidth(node_, max_width); return *this; }
FlexNode& FlexNode::MaxHeight(float max_height) { YGNodeStyleSetMaxHeight(node_, max_height); return *this; }

FlexNode& FlexNode::FlexDirection(YGFlexDirection direction) { YGNodeStyleSetFlexDirection(node_, direction); return *this; }
FlexNode& FlexNode::JustifyContent(YGJustify justify) { YGNodeStyleSetJustifyContent(node_, justify); return *this; }
FlexNode& FlexNode::AlignItems(YGAlign align) { YGNodeStyleSetAlignItems(node_, align); return *this; }
FlexNode& FlexNode::AlignSelf(YGAlign align) { YGNodeStyleSetAlignSelf(node_, align); return *this; }
FlexNode& FlexNode::FlexWrap(YGWrap wrap) { YGNodeStyleSetFlexWrap(node_, wrap); return *this; }

FlexNode& FlexNode::FlexGrow(float grow) { YGNodeStyleSetFlexGrow(node_, grow); return *this; }
FlexNode& FlexNode::FlexShrink(float shrink) { YGNodeStyleSetFlexShrink(node_, shrink); return *this; }
FlexNode& FlexNode::FlexBasis(float basis) { YGNodeStyleSetFlexBasis(node_, basis); return *this; }
FlexNode& FlexNode::FlexBasisPercent(float percent) { YGNodeStyleSetFlexBasisPercent(node_, percent); return *this; }
FlexNode& FlexNode::FlexBasisAuto() { YGNodeStyleSetFlexBasisAuto(node_); return *this; }

FlexNode& FlexNode::Padding(YGEdge edge, float padding) { YGNodeStyleSetPadding(node_, edge, padding); return *this; }
FlexNode& FlexNode::Margin(YGEdge edge, float margin) { YGNodeStyleSetMargin(node_, edge, margin); return *this; }
FlexNode& FlexNode::Position(YGEdge edge, float position) { YGNodeStyleSetPosition(node_, edge, position); return *this; }
FlexNode& FlexNode::PositionType(YGPositionType type) { YGNodeStyleSetPositionType(node_, type); return *this; }

FlexNode& FlexNode::Gap(YGGutter gutter, float gapLength) { YGNodeStyleSetGap(node_, gutter, gapLength); return *this; }

FlexNode& FlexNode::Display(YGDisplay display) { YGNodeStyleSetDisplay(node_, display); return *this; }

YGDisplay FlexNode::GetDisplay() const { return YGNodeStyleGetDisplay(node_); }

FlexNode& FlexNode::MeasureFunc(YGMeasureFunc measure_func) { YGNodeSetMeasureFunc(node_, measure_func); return *this; }
FlexNode& FlexNode::Context(void* context) { YGNodeSetContext(node_, context); return *this; }
void* FlexNode::GetContext() const { return YGNodeGetContext(node_); }

}  // namespace clipd2d::ui
