#include "ui/vlist/RowHeightModel.h"

#include <string>

#include "core/store/ClipTypes.h"
#include "tests/TestSupport.h"

namespace clipd2d::tests {

namespace {

using clipd2d::core::store::ClipContentType;
using clipd2d::core::store::ClipSummary;
using clipd2d::ui::vlist::RowHeightModel;

ClipSummary MakeTextSummary(int text_length) {
  ClipSummary summary{};
  summary.content_type = ClipContentType::Text;
  summary.text_length = text_length;
  return summary;
}

ClipSummary MakePreviewSummary(ClipContentType content_type) {
  ClipSummary summary{};
  summary.content_type = content_type;
  summary.text_length = 0;
  return summary;
}

}  // namespace

CLIPD2D_TEST(EstimateTextLinesHandlesBoundariesAndMinimalWidth) {
  // 0 / 负长度 → 1 行原始值 → 被 clamp 到 kMinTextLines。
  CLIPD2D_CHECK_EQ(RowHeightModel::EstimateTextLines(0, 400), RowHeightModel::kMinTextLines);
  CLIPD2D_CHECK_EQ(RowHeightModel::EstimateTextLines(1, 400), RowHeightModel::kMinTextLines);
  CLIPD2D_CHECK_EQ(RowHeightModel::EstimateTextLines(-5, 400), RowHeightModel::kMinTextLines);

  // 超长文本被 clamp 到 kMaxTextLines。
  CLIPD2D_CHECK_EQ(RowHeightModel::EstimateTextLines(1000000, 400), RowHeightModel::kMaxTextLines);

  // content_width 最小 16 保护：width<=0 或极窄时 chars_per_line 一律落到 16，
  // 与显式给 16*7=112px 宽度的估算结果一致。
  CLIPD2D_CHECK_EQ(RowHeightModel::EstimateTextLines(33, 0), RowHeightModel::EstimateTextLines(33, 112));
  CLIPD2D_CHECK_EQ(RowHeightModel::EstimateTextLines(33, -100), RowHeightModel::EstimateTextLines(33, 112));
  CLIPD2D_CHECK_EQ(RowHeightModel::EstimateTextLines(33, 16), RowHeightModel::EstimateTextLines(33, 0));
  CLIPD2D_CHECK_EQ(RowHeightModel::EstimateTextLines(33, 0), RowHeightModel::kMaxTextLines);
}

CLIPD2D_TEST(MeasurePreviewRowsAreConstantAcrossInputs) {
  // Files / Image 行高只由预览常量决定，与 text_length、content_width 无关。
  const int files_comfy = RowHeightModel::Measure(MakePreviewSummary(ClipContentType::Files), 400, false);
  const int image_comfy = RowHeightModel::Measure(MakePreviewSummary(ClipContentType::Image), 400, false);
  CLIPD2D_CHECK_EQ(files_comfy, image_comfy);
  CLIPD2D_CHECK_EQ(
      files_comfy,
      RowHeightModel::kPadTop + RowHeightModel::kHeaderHeight + RowHeightModel::kPreviewContentComfy +
          RowHeightModel::kPadBottom);

  ClipSummary image_long = MakePreviewSummary(ClipContentType::Image);
  image_long.text_length = 100000;
  CLIPD2D_CHECK_EQ(RowHeightModel::Measure(image_long, 400, false), files_comfy);
  CLIPD2D_CHECK_EQ(RowHeightModel::Measure(image_long, 120, false), files_comfy);

  // compact 与 comfy 的差值恒等于两个预览常量之差。
  const int files_compact = RowHeightModel::Measure(MakePreviewSummary(ClipContentType::Files), 400, true);
  CLIPD2D_CHECK_EQ(
      files_compact,
      RowHeightModel::kPadTop + RowHeightModel::kHeaderHeight + RowHeightModel::kPreviewContentCompact +
          RowHeightModel::kPadBottom);
  CLIPD2D_CHECK_EQ(
      files_comfy - files_compact,
      RowHeightModel::kPreviewContentComfy - RowHeightModel::kPreviewContentCompact);
}

CLIPD2D_TEST(MeasureTextRowsAreClampedByMinAndMaxLines) {
  const int line_comfy = RowHeightModel::kLineHeightComfy;
  const int line_compact = RowHeightModel::kLineHeightCompact;

  // measure 上限由 kMaxTextLines 锁死（渲染侧契约）。
  const int max_comfy =
      RowHeightModel::kPadTop + RowHeightModel::kHeaderHeight + RowHeightModel::kMaxTextLines * line_comfy +
      RowHeightModel::kPadBottom;
  const int min_comfy =
      RowHeightModel::kPadTop + RowHeightModel::kHeaderHeight + RowHeightModel::kMinTextLines * line_comfy +
      RowHeightModel::kPadBottom;

  CLIPD2D_CHECK_EQ(RowHeightModel::Measure(MakeTextSummary(0), 400, false), min_comfy);
  CLIPD2D_CHECK_EQ(RowHeightModel::Measure(MakeTextSummary(1), 400, false), min_comfy);
  CLIPD2D_CHECK_EQ(RowHeightModel::Measure(MakeTextSummary(1000000), 400, false), max_comfy);

  // 任意长度都落在 [min, max] 区间内。
  for (const int length : {2, 16, 17, 33, 48, 49, 200, 5000}) {
    const int height = RowHeightModel::Measure(MakeTextSummary(length), 400, false);
    CLIPD2D_CHECK(height >= min_comfy);
    CLIPD2D_CHECK(height <= max_comfy);
  }

  // compact 与 comfy 的差值 = 行数 × 行高差，行数即 EstimateTextLines 的结果。
  const int length = 33;
  const int lines = RowHeightModel::EstimateTextLines(length, 400);
  const int comfy = RowHeightModel::Measure(MakeTextSummary(length), 400, false);
  const int compact = RowHeightModel::Measure(MakeTextSummary(length), 400, true);
  CLIPD2D_CHECK_EQ(comfy - compact, lines * (line_comfy - line_compact));
  CLIPD2D_CHECK(compact < comfy);
}

CLIPD2D_TEST(MeasureTagsBlockAddsConstantBonus) {
  ClipSummary text_no_tags = MakeTextSummary(200);
  ClipSummary text_with_tags = text_no_tags;
  text_with_tags.tag_names.push_back(L"work");

  ClipSummary image_no_tags = MakePreviewSummary(ClipContentType::Image);
  ClipSummary image_with_tags = image_no_tags;
  image_with_tags.tag_names.push_back(L"work");

  // 文本行与预览行、comfy 与 compact：加 tag 块都只多出固定的 kTagBlockHeight。
  CLIPD2D_CHECK_EQ(
      RowHeightModel::Measure(text_with_tags, 400, false),
      RowHeightModel::Measure(text_no_tags, 400, false) + RowHeightModel::kTagBlockHeight);
  CLIPD2D_CHECK_EQ(
      RowHeightModel::Measure(text_with_tags, 400, true),
      RowHeightModel::Measure(text_no_tags, 400, true) + RowHeightModel::kTagBlockHeight);
  CLIPD2D_CHECK_EQ(
      RowHeightModel::Measure(image_with_tags, 400, false),
      RowHeightModel::Measure(image_no_tags, 400, false) + RowHeightModel::kTagBlockHeight);
  CLIPD2D_CHECK_EQ(
      RowHeightModel::Measure(image_with_tags, 400, true),
      RowHeightModel::Measure(image_no_tags, 400, true) + RowHeightModel::kTagBlockHeight);
}

}  // namespace clipd2d::tests
