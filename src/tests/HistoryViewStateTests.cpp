#include "ui/HistoryViewState.h"

#include <string>

#include "tests/TestSupport.h"

namespace clipd2d::tests {

namespace {

using clipd2d::core::store::ClipRecord;
using clipd2d::core::store::DateRangeFilter;
using clipd2d::ui::HistoryViewState;

}  // namespace

CLIPD2D_TEST(HistoryViewStateDerivedGettersTrackBrowserState) {
  HistoryViewState state;

  // 默认 browser_state_：group 无、tag 空、日期 All → 派生 getter 全部回退到中性值。
  CLIPD2D_CHECK_EQ(state.current_group_id(), static_cast<std::int64_t>(0));
  CLIPD2D_CHECK(state.current_group_name().empty());
  CLIPD2D_CHECK_EQ(state.current_tag_id(), static_cast<std::int64_t>(0));
  CLIPD2D_CHECK_EQ(state.current_date_range(), DateRangeFilter::All);

  // 设置 browser_state_ 字段后，getter 必须同步反映派生值。
  state.browser_state_.group_id = 42;
  state.browser_state_.group_name = L"Work";
  state.browser_state_.filters.tag_ids = {7, 8};
  state.browser_state_.filters.date_range = DateRangeFilter::Last7Days;
  CLIPD2D_CHECK_EQ(state.current_group_id(), static_cast<std::int64_t>(42));
  CLIPD2D_CHECK_EQ(state.current_group_name(), std::wstring(L"Work"));
  CLIPD2D_CHECK_EQ(state.current_tag_id(), static_cast<std::int64_t>(7));
  CLIPD2D_CHECK_EQ(state.current_date_range(), DateRangeFilter::Last7Days);

  // 复位后 getter 回到中性值（group_id 取 value_or(0)，tag 取 front()）。
  state.browser_state_.group_id.reset();
  state.browser_state_.filters.tag_ids.clear();
  state.browser_state_.filters.date_range = DateRangeFilter::All;
  CLIPD2D_CHECK_EQ(state.current_group_id(), static_cast<std::int64_t>(0));
  CLIPD2D_CHECK_EQ(state.current_tag_id(), static_cast<std::int64_t>(0));
  CLIPD2D_CHECK_EQ(state.current_date_range(), DateRangeFilter::All);
}

CLIPD2D_TEST(HistoryViewStatePageTouchTickProtocolIsMonotonic) {
  HistoryViewState state;

  // 初始：tick 计数器为 0，无任何页访问记录，页缓存为空。
  CLIPD2D_CHECK_EQ(state.page_access_tick_, 0u);
  CLIPD2D_CHECK(state.page_access_ticks_.empty());
  CLIPD2D_CHECK(state.page_cache_.empty());

  // LRU 触摸协议：先递增 page_access_tick_，再把新值记录到 page_access_ticks_[页号]。
  const auto touch_page = [&state](int page) {
    ++state.page_access_tick_;
    state.page_access_ticks_[page] = state.page_access_tick_;
  };
  touch_page(0);
  touch_page(1);
  touch_page(0);

  CLIPD2D_CHECK_EQ(state.page_access_tick_, 3u);
  CLIPD2D_CHECK_EQ(state.page_access_ticks_.at(1), 2u);
  CLIPD2D_CHECK_EQ(state.page_access_ticks_.at(0), 3u);
  CLIPD2D_CHECK(state.page_access_ticks_.at(0) > state.page_access_ticks_.at(1));
  CLIPD2D_CHECK_EQ(state.page_access_ticks_.size(), static_cast<size_t>(2));
}

CLIPD2D_TEST(HistoryViewStateStagingSnapshotDefaultsAndStateBits) {
  HistoryViewState state;

  // 默认 staging_snapshot_：非激活、计数未就绪、代次与条目数均为 0。
  CLIPD2D_CHECK(!state.staging_snapshot_.active);
  CLIPD2D_CHECK(!state.staging_snapshot_.count_ready);
  CLIPD2D_CHECK_EQ(state.staging_snapshot_.generation, 0u);
  CLIPD2D_CHECK_EQ(state.staging_snapshot_.item_count, 0);
  CLIPD2D_CHECK(state.staging_snapshot_.page_cache.empty());
  CLIPD2D_CHECK(state.staging_snapshot_.required_pages.empty());
  CLIPD2D_CHECK_EQ(state.staging_snapshot_.target_scroll_offset, 0);

  // 状态位可独立置位，且互不影响。
  state.staging_snapshot_.active = true;
  CLIPD2D_CHECK(state.staging_snapshot_.active);
  CLIPD2D_CHECK(!state.staging_snapshot_.count_ready);

  state.staging_snapshot_.count_ready = true;
  state.staging_snapshot_.item_count = 57;
  state.staging_snapshot_.generation = 9;
  CLIPD2D_CHECK(state.staging_snapshot_.count_ready);
  CLIPD2D_CHECK_EQ(state.staging_snapshot_.item_count, 57);
  CLIPD2D_CHECK_EQ(state.staging_snapshot_.generation, 9u);
}

CLIPD2D_TEST(HistoryViewStateSelectionDefaultsAndAssignment) {
  HistoryViewState state;

  // 初始选择：无选中条目，索引为 -1，待定/显式选择均为空。
  CLIPD2D_CHECK_EQ(state.selected_index_, -1);
  CLIPD2D_CHECK(!state.selected_clip_.has_value());
  CLIPD2D_CHECK(!state.pending_selected_clip_id_.has_value());
  CLIPD2D_CHECK(!state.explicit_selected_clip_id_.has_value());

  // 赋值后可读回。
  ClipRecord record;
  record.plain_text = "hello clip";
  state.selected_clip_ = record;
  state.selected_index_ = 3;
  state.explicit_selected_clip_id_ = 91;
  CLIPD2D_CHECK(state.selected_clip_.has_value());
  CLIPD2D_CHECK_EQ(state.selected_clip_->plain_text, std::string("hello clip"));
  CLIPD2D_CHECK_EQ(state.selected_index_, 3);
  CLIPD2D_CHECK_EQ(state.explicit_selected_clip_id_.value(), static_cast<std::int64_t>(91));

  // 清除选择恢复初始形态。
  state.selected_clip_.reset();
  state.selected_index_ = -1;
  CLIPD2D_CHECK(!state.selected_clip_.has_value());
  CLIPD2D_CHECK_EQ(state.selected_index_, -1);
}

}  // namespace clipd2d::tests
