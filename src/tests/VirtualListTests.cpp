#include "ui/vlist/HeightIndex.h"
#include "ui/vlist/RowHeightModel.h"
#include "ui/vlist/StableSlotPool.h"
#include "ui/vlist/VirtualListLayout.h"
#include "ui/vlist/VirtualListViewport.h"

#include <algorithm>
#include <numeric>
#include <string>
#include <vector>

#include "tests/TestSupport.h"

namespace {

using clipd2d::ui::vlist::HeightIndex;
using clipd2d::ui::vlist::RowHeightModel;
using clipd2d::ui::vlist::StableSlotPool;
using clipd2d::ui::vlist::VirtualListViewport;
using clipd2d::ui::vlist::VisibleRangeFor;

int NaiveOffsetOf(const std::vector<int>& heights, int index) {
  int sum = 0;
  for (int i = 0; i < index && i < static_cast<int>(heights.size()); ++i) {
    sum += heights[static_cast<size_t>(i)];
  }
  return sum;
}

int NaiveIndexAtOffset(const std::vector<int>& heights, int offset) {
  int acc = 0;
  for (int i = 0; i < static_cast<int>(heights.size()); ++i) {
    if (acc + heights[static_cast<size_t>(i)] > offset) {
      return i;
    }
    acc += heights[static_cast<size_t>(i)];
  }
  return std::max(0, static_cast<int>(heights.size()) - 1);
}

class FakeSource : public clipd2d::ui::vlist::VirtualListSource {
 public:
  std::vector<int> heights;
  std::vector<clipd2d::ui::vlist::ItemId> ids;

  int ItemCount() const override { return static_cast<int>(heights.size()); }
  std::int64_t StableIdAt(int index) const override {
    return ids.empty() ? index : ids[static_cast<size_t>(index)];
  }
  int MeasureHeightAt(int index) const override { return heights[static_cast<size_t>(index)]; }
};

}  // namespace

namespace clipd2d::tests {

CLIPD2D_TEST(HeightIndexUniformFastPathMatchesNaive) {
  HeightIndex heights;
  heights.Reset(1000, 40);
  CLIPD2D_CHECK(heights.IsUniform());
  CLIPD2D_CHECK_EQ(heights.TotalHeight(), 40000);
  CLIPD2D_CHECK_EQ(heights.OffsetOf(0), 0);
  CLIPD2D_CHECK_EQ(heights.OffsetOf(250), 10000);
  CLIPD2D_CHECK_EQ(heights.IndexAtOffset(0), 0);
  CLIPD2D_CHECK_EQ(heights.IndexAtOffset(39), 0);
  CLIPD2D_CHECK_EQ(heights.IndexAtOffset(40), 1);
  CLIPD2D_CHECK_EQ(heights.IndexAtOffset(40000), 999);
}

CLIPD2D_TEST(HeightIndexVariableHeightsMatchNaive) {
  std::vector<int> expected(257);
  for (int i = 0; i < 257; ++i) {
    expected[static_cast<size_t>(i)] = 30 + (i % 7) * 10;
  }
  HeightIndex heights;
  heights.Reset(257, 30);
  for (int i = 0; i < 257; ++i) {
    heights.SetHeight(i, expected[static_cast<size_t>(i)]);
  }
  CLIPD2D_CHECK(!heights.IsUniform());
  int expected_total = 0;
  for (int h : expected) {
    expected_total += h;
  }
  CLIPD2D_CHECK_EQ(heights.TotalHeight(), expected_total);
  for (int i = 0; i <= 257; ++i) {
    CLIPD2D_CHECK_EQ(heights.OffsetOf(i), NaiveOffsetOf(expected, i));
  }
  for (int offset = 0; offset < expected_total + 5; offset += 13) {
    CLIPD2D_CHECK_EQ(heights.IndexAtOffset(offset), NaiveIndexAtOffset(expected, offset));
  }
}

CLIPD2D_TEST(HeightIndexSingleSetUpdatesTotals) {
  HeightIndex heights;
  heights.Reset(10, 50);
  CLIPD2D_CHECK(heights.SetHeight(4, 120));
  CLIPD2D_CHECK(!heights.SetHeight(4, 120));  // no-op
  CLIPD2D_CHECK_EQ(heights.TotalHeight(), 570);
  CLIPD2D_CHECK_EQ(heights.OffsetOf(5), 320);  // 50*4 + 70 extra at 4
  CLIPD2D_CHECK_EQ(heights.HeightAt(4), 120);
}

CLIPD2D_TEST(HeightIndexResetRestoresUniform) {
  HeightIndex heights;
  heights.Reset(8, 20);
  heights.SetHeight(3, 99);
  heights.Reset(8, 20);
  CLIPD2D_CHECK(heights.IsUniform());
  CLIPD2D_CHECK_EQ(heights.TotalHeight(), 160);
}

CLIPD2D_TEST(VisibleRangeCoversOverscanAndClamps) {
  HeightIndex heights;
  heights.Reset(100, 40);  // 4000px total
  const auto range = VisibleRangeFor(heights, 400.0f, 200, 80, 120);
  CLIPD2D_CHECK_EQ(range.first, 8);   // top=320 -> index 8
  CLIPD2D_CHECK_EQ(range.last, 18);   // bottom=720 -> index 18
  const auto empty = VisibleRangeFor(heights, 0.0f, 0, 0, 0);
  CLIPD2D_CHECK(empty.empty());
}

CLIPD2D_TEST(StableSlotPoolReportsEntryAndExit) {
  StableSlotPool pool;
  pool.BeginFrame();
  pool.Push(StableSlotPool::Row{101, 0, 0.0f, 40.0f});
  pool.Push(StableSlotPool::Row{102, 1, 40.0f, 80.0f});
  pool.EndFrame();
  CLIPD2D_CHECK_EQ(pool.added_ids().size(), 2u);
  CLIPD2D_CHECK_EQ(pool.removed_ids().size(), 0u);

  pool.BeginFrame();
  pool.Push(StableSlotPool::Row{102, 1, 0.0f, 40.0f});
  pool.Push(StableSlotPool::Row{103, 2, 40.0f, 80.0f});
  pool.EndFrame();
  CLIPD2D_CHECK_EQ(pool.added_ids().size(), 1u);
  CLIPD2D_CHECK_EQ(pool.removed_ids().size(), 1u);
  CLIPD2D_CHECK_EQ(pool.added_ids()[0], 103);
  CLIPD2D_CHECK_EQ(pool.removed_ids()[0], 101);
  CLIPD2D_CHECK_EQ(pool.rows().size(), 2u);
  CLIPD2D_CHECK_EQ(pool.rows()[0].stable_id, 102);  // sorted by top
}

CLIPD2D_TEST(RowHeightModelTextGrowsWithLengthAndTags) {
  core::store::ClipSummary summary{};
  summary.content_type = core::store::ClipContentType::Text;
  summary.text_length = 10;
  const int short_no_tags = RowHeightModel::Measure(summary, 400, false);

  summary.text_length = 4000;
  const int long_no_tags = RowHeightModel::Measure(summary, 400, false);
  CLIPD2D_CHECK(long_no_tags > short_no_tags);

  summary.tag_names.push_back(L"t");
  const int long_with_tags = RowHeightModel::Measure(summary, 400, false);
  CLIPD2D_CHECK_EQ(long_with_tags, long_no_tags + RowHeightModel::kTagBlockHeight);

  const int compact = RowHeightModel::Measure(summary, 400, true);
  CLIPD2D_CHECK(compact < long_with_tags);
}

CLIPD2D_TEST(RowHeightModelPreviewRowsAreFixed) {
  core::store::ClipSummary image{};
  image.content_type = core::store::ClipContentType::Image;
  image.text_length = 100000;
  core::store::ClipSummary files{};
  files.content_type = core::store::ClipContentType::Files;
  CLIPD2D_CHECK_EQ(
      RowHeightModel::Measure(image, 400, false), RowHeightModel::Measure(files, 400, false));
}

CLIPD2D_TEST(RowHeightModelEstimatesLinesWithinBounds) {
  CLIPD2D_CHECK_EQ(RowHeightModel::EstimateTextLines(0, 400), RowHeightModel::kMinTextLines);
  CLIPD2D_CHECK_EQ(RowHeightModel::EstimateTextLines(1, 400), RowHeightModel::kMinTextLines);
  CLIPD2D_CHECK_EQ(RowHeightModel::EstimateTextLines(1000000, 400), RowHeightModel::kMaxTextLines);
  CLIPD2D_CHECK(RowHeightModel::EstimateTextLines(200, 100) >= RowHeightModel::kMinTextLines);
}

CLIPD2D_TEST(ViewportAnchorCompensationKeepsScrollStable) {
  FakeSource source;
  source.heights = std::vector<int>(20, 50);
  source.ids.resize(20);
  for (int i = 0; i < 20; ++i) {
    source.ids[static_cast<size_t>(i)] = 1000 + i;
  }
  VirtualListViewport viewport;
  viewport.set_default_height(50);
  viewport.SetSource(&source);
  viewport.Rebuild();
  CLIPD2D_CHECK_EQ(viewport.TotalHeight(), 1000);

  // Rows 0..4 grow by 30 each (150 total above anchor 10).
  for (int i = 0; i < 5; ++i) {
    source.heights[static_cast<size_t>(i)] = 80;
  }
  const int delta = viewport.UpdateRange(0, 19, 10);
  CLIPD2D_CHECK_EQ(delta, 150);
  CLIPD2D_CHECK_EQ(viewport.TotalHeight(), 1150);
  // Rows at/after the anchor do not contribute.
  source.heights[15] = 200;
  CLIPD2D_CHECK_EQ(viewport.UpdateRange(15, 15, 10), 0);
  CLIPD2D_CHECK_EQ(viewport.OffsetOf(10), 650);  // 5*80 + 5*50, unchanged by post-anchor growth
}

CLIPD2D_TEST(ViewportRangeAtUsesVariableHeights) {
  FakeSource source;
  source.heights = {10, 200, 10, 10, 300, 10};
  VirtualListViewport viewport;
  viewport.set_default_height(10);
  viewport.SetSource(&source);
  viewport.Rebuild();
  CLIPD2D_CHECK_EQ(viewport.TotalHeight(), 540);
  // Scroll 0, viewport 100 -> rows 0 (10px) and 1 (200px) intersect.
  const auto range = viewport.RangeAt(0.0f, 100, 0, 0);
  CLIPD2D_CHECK_EQ(range.first, 0);
  CLIPD2D_CHECK_EQ(range.last, 1);
  // Scroll 0, viewport 220 -> rows 0..3 intersect (10+200+10+10).
  const auto wide = viewport.RangeAt(0.0f, 220, 0, 0);
  CLIPD2D_CHECK_EQ(wide.first, 0);
  CLIPD2D_CHECK_EQ(wide.last, 3);
}

}  // namespace clipd2d::tests
