#pragma once

// 主面板布局常量。原先混在 MainWindowInternal.h 的匿名命名空间里，
// 被 13 个 TU 各持一份副本；这里改为 inline constexpr 单一定义。
// 行级常量在 ui/vlist/RowMetrics.h，此处 re-export 以保持调用点名字不变。

#include "ui/vlist/RowMetrics.h"

#include <cstddef>

namespace clipd2d::ui {

using vlist::kRowHeaderHeight;
using vlist::kRowPaddingX;
using vlist::kRowHeaderIconSize;
using vlist::kRowActionButtonSize;
using vlist::kRowActionGap;
using vlist::kTagChipHeight;
using vlist::kTagChipGap;

inline constexpr int kDefaultWindowWidth = 460;
inline constexpr int kDefaultWindowHeight = 720;
inline constexpr int kMinWindowWidth = 340;
inline constexpr int kMinWindowHeight = 420;
inline constexpr int kResizeBorder = 2;
inline constexpr int kHeaderHeight = 36;
inline constexpr int kToolbarHeight = 36;
inline constexpr int kBottomBarHeight = 28;
inline constexpr int kBottomBarButtonGap = 10;
inline constexpr int kToolbarButtonSize = 28;
inline constexpr int kToolbarGap = 6;
inline constexpr int kTabButtonWidth = 28;
inline constexpr int kTopPadding = 0;
inline constexpr int kHorizontalPadding = 10;
inline constexpr int kScrollBarWidth = 6;
inline constexpr int kQueueDragHandleWidth = 14;
inline constexpr int kTimeGroupHeaderHeight = 20;
inline constexpr int kBackToTopButtonWidth = 108;
inline constexpr int kBackToTopButtonHeight = 24;
inline constexpr int kBackToTopRevealHeight = 36;
inline constexpr float kSeparatorStrokeWidth = 0.6f;
inline constexpr float kControlStrokeWidth = 0.6f;
inline constexpr float kSearchIconInset = 6.5f;
inline constexpr float kToolbarIconInset = 6.5f;
inline constexpr float kTabIconInset = 6.5f;
inline constexpr float kTopIconWeightBoost = 0.78f;
inline constexpr int kPageCacheLimit = 12;
inline constexpr std::size_t kTextLayoutCacheLimit = 512;
inline constexpr std::size_t kSourceIconCacheLimit = 64;
inline constexpr std::size_t kFileIconCacheLimit = 96;
inline constexpr std::size_t kThumbnailCacheLimit = 128;

}  // namespace clipd2d::ui
