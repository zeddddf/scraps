#include "ui/vlist/RowHeightModel.h"

#include <algorithm>

namespace clipd2d::ui::vlist {

int RowHeightModel::EstimateTextLines(int text_length, int content_width) {
  // Average glyph advance for the row's small text is roughly 7px at 100% DPI;
  // width buckets absorb rounding. Never below 1 char per line.
  const int chars_per_line = std::max(16, content_width / 7);
  const int length = std::max(0, text_length);
  const int raw_lines = length <= 0 ? 1 : (length + chars_per_line - 1) / chars_per_line;
  return std::clamp(raw_lines, kMinTextLines, kMaxTextLines);
}

int RowHeightModel::Measure(const core::store::ClipSummary& summary, int content_width, bool compact) {
  const bool has_tags = !summary.tag_names.empty();
  const int tags_height = has_tags ? kTagBlockHeight : 0;

  int content_height = 0;
  if (summary.content_type == core::store::ClipContentType::Files ||
      summary.content_type == core::store::ClipContentType::Image) {
    content_height = compact ? kPreviewContentCompact : kPreviewContentComfy;
  } else {
    const int text_length =
        summary.text_length > 0 ? summary.text_length : static_cast<int>(summary.snippet.size());
    const int lines = EstimateTextLines(text_length, content_width);
    content_height = lines * (compact ? kLineHeightCompact : kLineHeightComfy);
  }

  return kPadTop + kHeaderHeight + tags_height + content_height + kPadBottom;
}

}  // namespace clipd2d::ui::vlist
