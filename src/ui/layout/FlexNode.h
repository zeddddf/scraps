#pragma once

#include <memory>
#include <vector>
#include <windows.h>
#include <yoga/Yoga.h>

namespace clipd2d::ui {

class FlexNode {
 public:
  FlexNode();
  ~FlexNode();

  // Non-copyable
  FlexNode(const FlexNode&) = delete;
  FlexNode& operator=(const FlexNode&) = delete;

  // Moveable
  FlexNode(FlexNode&& other) noexcept;
  FlexNode& operator=(FlexNode&& other) noexcept;

  // Tree building
  FlexNode& AddChild(std::shared_ptr<FlexNode> child);
  FlexNode& InsertChild(std::shared_ptr<FlexNode> child, uint32_t index);

  // Layout calculations
  void CalculateLayout(float available_width, float available_height, YGDirection direction = YGDirectionLTR);
  float GetLeft() const;
  float GetTop() const;
  float GetWidth() const;
  float GetHeight() const;

  const std::vector<std::shared_ptr<FlexNode>>& GetChildren() const { return children_; }

  RECT GetAbsoluteRect(float base_x, float base_y) const {
    float x = base_x + GetLeft();
    float y = base_y + GetTop();
    YGNodeRef p = YGNodeGetParent(node_);
    while (p != nullptr) {
        x += YGNodeLayoutGetLeft(p);
        y += YGNodeLayoutGetTop(p);
        p = YGNodeGetParent(p);
    }
    return {
        static_cast<LONG>(x),
        static_cast<LONG>(y),
        static_cast<LONG>(x + GetWidth()),
        static_cast<LONG>(y + GetHeight())
    };
  }

  // Fluent API for Flex properties
  FlexNode& Width(float width);
  FlexNode& Height(float height);
  FlexNode& WidthPercent(float percent);
  FlexNode& HeightPercent(float percent);
  FlexNode& WidthAuto();
  FlexNode& HeightAuto();

  FlexNode& MinWidth(float min_width);
  FlexNode& MinHeight(float min_height);
  FlexNode& MaxWidth(float max_width);
  FlexNode& MaxHeight(float max_height);

  FlexNode& FlexDirection(YGFlexDirection direction);
  FlexNode& JustifyContent(YGJustify justify);
  FlexNode& AlignItems(YGAlign align);
  FlexNode& AlignSelf(YGAlign align);
  FlexNode& FlexWrap(YGWrap wrap);

  FlexNode& FlexGrow(float grow);
  FlexNode& FlexShrink(float shrink);
  FlexNode& FlexBasis(float basis);
  FlexNode& FlexBasisPercent(float percent);
  FlexNode& FlexBasisAuto();

  FlexNode& Padding(YGEdge edge, float padding);
  FlexNode& Margin(YGEdge edge, float margin);
  FlexNode& Position(YGEdge edge, float position);
  FlexNode& PositionType(YGPositionType type);

  FlexNode& Gap(YGGutter gutter, float gapLength);

  // 显隐（YGDisplayNone 折叠占位；设置搜索按行过滤用）
  FlexNode& Display(YGDisplay display);
  YGDisplay GetDisplay() const;

  FlexNode& MeasureFunc(YGMeasureFunc measure_func);
  FlexNode& Context(void* context);
  void* GetContext() const;

  YGNodeRef GetNode() const { return node_; }

 private:
  YGNodeRef node_;
  std::vector<std::shared_ptr<FlexNode>> children_;
};

}  // namespace clipd2d::ui
