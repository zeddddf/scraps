#pragma once

// Row height measurement — the "measure == draw" contract shared with
// MainWindow::RenderListRow.
//
// RenderListRow places content relative to the row rect:
//   [row.top + 2]                    header (30px)
//   [header.bottom + 2]              tag chips (18px) when tags exist
//   [header.bottom or chips end]     content area -> row.bottom - 4
// A taller row rect automatically gives the wrapped text more visible lines,
// so measurement only has to decide the total height from O(1) metadata.

#include "core/store/ClipTypes.h"
#include "ui/vlist/RowMetrics.h"

namespace clipd2d::ui::vlist {

struct RowHeightModel {
  // Layout constants — 唯一来源是 ui/vlist/RowMetrics.h，这里只做别名。
  static constexpr int kPadTop = 2;
  static constexpr int kHeaderHeight = kRowHeaderHeight;   // == kRowHeaderHeight
  static constexpr int kTagBlockHeight = kTagChipHeight + 4;  // 18 + 4
  static constexpr int kPadBottom = 4;

  static constexpr int kLineHeightComfy = 20;
  static constexpr int kLineHeightCompact = 16;
  static constexpr int kPreviewContentComfy = 56;
  static constexpr int kPreviewContentCompact = 44;

  static constexpr int kMinTextLines = 2;
  static constexpr int kMaxTextLines = 3;

  // Deterministic, metadata-only. content_width is the row content width in px.
  static int Measure(const core::store::ClipSummary& summary, int content_width, bool compact);

  // Estimate wrapped line count from text length (O(1), no DWrite).
  static int EstimateTextLines(int text_length, int content_width);
};

}  // namespace clipd2d::ui::vlist
