#pragma once

// 列表行布局常量 —— measure 与 draw 的共同依据。
// 唯一来源：RowHeightModel.h 与 MainWindowMetrics.h 都引用这里，不得复制数值。

namespace clipd2d::ui::vlist {

inline constexpr int kRowHeaderHeight = 30;
inline constexpr int kRowPaddingX = 10;
inline constexpr int kRowHeaderIconSize = 18;
inline constexpr int kRowActionButtonSize = 18;
inline constexpr int kRowActionGap = 4;
inline constexpr int kTagChipHeight = 18;
inline constexpr int kTagChipGap = 6;

}  // namespace clipd2d::ui::vlist
