#include "ui/MainWindowInternal.h"
#include "core/commands/IHistoryFeed.h"
#include "core/commands/ILibraryMutator.h"
#include "core/commands/IMaintenance.h"
#include "core/commands/ISettingsStore.h"

namespace clipd2d::ui {


int MainWindow::GetItemHeight() const {
  return settings_store_ != nullptr && settings_store_->current_settings().list_density == core::store::ListDensity::Compact ? 84 : 96;
}

int MainWindow::GetTotalContentHeight() const {
  return viewport_.TotalHeight();
}

int MainWindow::GetCollapsedWidth() const {
  return window_width_;
}

int MainWindow::GetExpandedWidth() const {
  return window_width_;
}

int MainWindow::GetDrawerWidth() const {
  return 0;
}




















void MainWindow::RefreshStatus(const std::wstring& text) {
  state_.status_text_ = text;
  InvalidateRect(hwnd_, nullptr, FALSE);
}



bool MainWindow::ShouldShowTimeGroupsInCurrentState() const {
  return browser_service_.BuildPresentation(BuildBrowserState()).show_time_groups;
}






























MainWindow::ReorderScope MainWindow::CurrentReorderScope() const {
  if (!state_.display_query_.search_text.empty() || state_.current_date_range() != core::store::DateRangeFilter::All) {
    return ReorderScope::None;
  }
  if (state_.current_view_ == core::store::ClipView::Queue && state_.current_tag_id() == 0) {
    return ReorderScope::Queue;
  }
  if (state_.current_view_ == core::store::ClipView::Favorites && state_.current_tag_id() == 0) {
    return ReorderScope::Favorites;
  }
  if (state_.current_tag_id() > 0) {
    return ReorderScope::Tag;
  }
  if (state_.current_view_ == core::store::ClipView::All && state_.current_group_id() == 0) {
    return ReorderScope::Pinned;
  }
  return ReorderScope::None;
}

bool MainWindow::CanReorderRow(const core::store::ClipSummary& summary) const {
  switch (CurrentReorderScope()) {
    case ReorderScope::Queue:
      return true;
    case ReorderScope::Favorites:
      return summary.is_favorite;
    case ReorderScope::Tag:
      return state_.current_tag_id() > 0;
    case ReorderScope::Pinned:
      return summary.is_pinned;
    case ReorderScope::None:
    default:
      return false;
  }
}

void MainWindow::ArmRowDrag(POINT point, int index) {
  if (index < 0) {
    row_drag_candidate_active_ = false;
    row_drag_candidate_index_ = -1;
    row_drag_scope_ = ReorderScope::None;
    return;
  }
  const auto* summary = GetSummaryAt(index);
  if (summary == nullptr || (!CanReorderRow(*summary) &&
      (summary->content_type != core::store::ClipContentType::Files &&
       summary->content_type != core::store::ClipContentType::Image))) {
    row_drag_candidate_active_ = false;
    row_drag_candidate_index_ = -1;
    row_drag_scope_ = ReorderScope::None;
    return;
  }
  row_drag_candidate_active_ = true;
  row_drag_candidate_index_ = index;
  row_drag_start_point_ = point;
  row_drag_scope_ = CurrentReorderScope();
}

bool MainWindow::MaybeStartRowDrag(POINT point) {
  if (!row_drag_candidate_active_ || row_drag_active_ || row_drag_candidate_index_ < 0) {
    return false;
  }
  const int drag_dx = GetSystemMetrics(SM_CXDRAG);
  const int drag_dy = GetSystemMetrics(SM_CYDRAG);
  if (std::abs(point.x - row_drag_start_point_.x) < drag_dx && std::abs(point.y - row_drag_start_point_.y) < drag_dy) {
    return false;
  }

  const auto* summary = GetSummaryAt(row_drag_candidate_index_);
  if (summary == nullptr) {
    row_drag_candidate_active_ = false;
    row_drag_candidate_index_ = -1;
    row_drag_scope_ = ReorderScope::None;
    return false;
  }

  if (CanReorderRow(*summary)) {
    row_drag_active_ = true;
    row_drag_item_index_ = row_drag_candidate_index_;
    row_drop_index_ = row_drag_candidate_index_;
    row_drop_after_ = false;
    row_drag_point_ = point;
    RECT client{};
    GetClientRect(hwnd_, &client);
    const PanelLayout layout = ComputeLayout(client.right, client.bottom);
    const float visual_scroll_offset = scroll_controller_->VisualScrollOffset();
    const vlist::RowRect row_pos = vlist::RowRectFor(
        viewport_.heights(), row_drag_item_index_, static_cast<float>(layout.list.top), visual_scroll_offset);
    const int row_top = static_cast<int>(std::lround(row_pos.top));
    const int row_height_px = std::max(1, static_cast<int>(std::lround(row_pos.bottom - row_pos.top)));
    row_drag_pointer_offset_y_ = std::clamp(static_cast<int>(point.y) - row_top, 0, row_height_px - 1);
    row_drag_candidate_active_ = false;
    row_drag_candidate_index_ = -1;
    SelectIndex(row_drag_item_index_, false);
    SetCapture(hwnd_);
    BeginRowDragSession();
    UpdateRowReorderAnimationTargets();
    EnsureAnimationTimer();
    InvalidateListRegion();
    return true;
  }

  row_drag_candidate_active_ = false;
  const int candidate_index = row_drag_candidate_index_;
  row_drag_candidate_index_ = -1;
  row_drag_scope_ = ReorderScope::None;
  return TryStartExternalDrag(candidate_index);
}

void MainWindow::UpdateRowDrag(POINT point) {
  if (!row_drag_active_ || state_.item_count_ <= 0 || !row_drag_session_.has_value()) {
    return;
  }
  const POINT previous_point = row_drag_point_;
  row_drag_point_ = point;

  const std::uint64_t now = GetTickCount64();
  float delta_ms = 0.0f;
  LARGE_INTEGER counter{};
  if (performance_counter_frequency_.QuadPart > 0 && QueryPerformanceCounter(&counter)) {
    if (row_drag_last_update_qpc_ != 0) {
      delta_ms = static_cast<float>(
          (static_cast<double>(counter.QuadPart - row_drag_last_update_qpc_) * 1000.0) /
          static_cast<double>(performance_counter_frequency_.QuadPart));
      delta_ms = std::clamp(delta_ms, 0.5f, 40.0f);
    }
    row_drag_last_update_qpc_ = counter.QuadPart;
  }
  if (delta_ms <= 0.0f && row_drag_last_update_tick_ms_ != 0) {
    delta_ms = static_cast<float>(std::max<std::uint64_t>(1, now - row_drag_last_update_tick_ms_));
  }
  if (delta_ms > 0.0f) {
    const float delta_seconds = std::clamp(delta_ms / 1000.0f, 0.001f, 0.04f);
    const float rows_per_second =
        std::fabs(static_cast<float>(point.y - previous_point.y)) /
        static_cast<float>(std::max(1, GetItemHeight())) /
        delta_seconds;
    row_drag_velocity_rows_per_s_ += (rows_per_second - row_drag_velocity_rows_per_s_) * 0.35f;
  }
  row_drag_last_update_tick_ms_ = now;

  RECT client{};
  GetClientRect(hwnd_, &client);
  const PanelLayout layout = ComputeLayout(client.right, client.bottom);
  constexpr float kAutoScrollEdgeBandPx = 32.0f;
  constexpr float kAutoScrollEntrySpeed = 150.0f;
  constexpr float kAutoScrollBoundarySpeed = 300.0f;
  constexpr float kAutoScrollMaxSpeed = 2600.0f;
  row_drag_auto_scroll_velocity_ = (0.0f);
  const float top_band_end = static_cast<float>(layout.list.top) + kAutoScrollEdgeBandPx;
  const float bottom_band_start = static_cast<float>(layout.list.bottom) - kAutoScrollEdgeBandPx;
  if (static_cast<float>(point.y) <= top_band_end) {
    const float contact_strength = std::clamp((top_band_end - static_cast<float>(point.y)) / kAutoScrollEdgeBandPx, 0.0f, 1.0f);
    const float contact_eased = contact_strength * contact_strength * (3.0f - 2.0f * contact_strength);
    const float overshoot = std::max(0.0f, static_cast<float>(layout.list.top) - static_cast<float>(point.y));
    const float overshoot_strength = std::clamp(overshoot / kAutoScrollEdgeBandPx, 0.0f, 1.0f);
    if (static_cast<float>(point.y) >= static_cast<float>(layout.list.top)) {
      row_drag_auto_scroll_velocity_ = (-std::lerp(kAutoScrollEntrySpeed, kAutoScrollBoundarySpeed, contact_eased));
    } else {
      row_drag_auto_scroll_velocity_ = (-std::lerp(kAutoScrollBoundarySpeed, kAutoScrollMaxSpeed, overshoot_strength * overshoot_strength));
    }
  } else if (static_cast<float>(point.y) >= bottom_band_start) {
    const float contact_strength = std::clamp((static_cast<float>(point.y) - bottom_band_start) / kAutoScrollEdgeBandPx, 0.0f, 1.0f);
    const float contact_eased = contact_strength * contact_strength * (3.0f - 2.0f * contact_strength);
    const float overshoot = std::max(0.0f, static_cast<float>(point.y) - static_cast<float>(layout.list.bottom));
    const float overshoot_strength = std::clamp(overshoot / kAutoScrollEdgeBandPx, 0.0f, 1.0f);
    if (static_cast<float>(point.y) <= static_cast<float>(layout.list.bottom)) {
      row_drag_auto_scroll_velocity_ = (std::lerp(kAutoScrollEntrySpeed, kAutoScrollBoundarySpeed, contact_eased));
    } else {
      row_drag_auto_scroll_velocity_ = (std::lerp(kAutoScrollBoundarySpeed, kAutoScrollMaxSpeed, overshoot_strength * overshoot_strength));
    }
  }
  const float visual_scroll_offset = scroll_controller_->VisualScrollOffset();
  row_drag_session_->desired_slot = ComputeDesiredRowDragSlot(layout.list, visual_scroll_offset);
  EnsureAnimationTimer();
  InvalidateListRegion();
}

common::Status MainWindow::BuildDragFileList(const core::store::ClipSummary& summary, std::vector<std::wstring>* paths) {
  if (history_feed_ == nullptr || paths == nullptr) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "鎷栨嫿瀵煎嚭鍙傛暟鏃犳晥");
  }
  paths->clear();

  core::store::ClipRecord clip;
  common::Status status = history_feed_->GetClipById(summary.id, &clip);
  if (!status.ok()) {
    return status;
  }

  if (summary.content_type == core::store::ClipContentType::Files) {
    for (const auto& path : ExtractFilePathsFromClip(clip)) {
      if (std::filesystem::exists(path)) {
        paths->push_back(path);
      }
    }
    if (!paths->empty()) {
      return common::Status::Ok();
    }
    return common::Status::Error(common::StatusCode::NotFound, "No exportable file found.");
  }

  if (summary.content_type != core::store::ClipContentType::Image) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "浠呮枃浠跺拰鍥剧墖鏉＄洰鏀寔鎷栨嫿瀵煎嚭");
  }

  for (const auto& path : ExtractFilePathsFromClip(clip)) {
    if (!path.empty() && std::filesystem::exists(path)) {
      paths->push_back(path);
      return common::Status::Ok();
    }
  }
  if (!clip.secondary_path.empty() && std::filesystem::exists(clip.secondary_path)) {
    paths->push_back(clip.secondary_path);
    return common::Status::Ok();
  }
  if (!clip.storage_path.empty() && std::filesystem::exists(clip.storage_path) && IsImageFilePath(clip.storage_path)) {
    paths->push_back(clip.storage_path);
    return common::Status::Ok();
  }

  common::AppPaths app_paths;
  status = common::ResolveAppPaths(&app_paths);
  if (!status.ok()) {
    return status;
  }
  const std::filesystem::path export_dir = std::filesystem::path(app_paths.cache_dir) / L"drag-export";
  std::error_code ec;
  std::filesystem::create_directories(export_dir, ec);
  const std::wstring base_name = SanitizeFileName(
      !summary.title.empty() ? common::Utf8ToWide(summary.title).wide : L"image");
  std::wstring exported_path;
  status = maintenance_->ExportClipToFile(
      summary.id,
      (export_dir / (base_name + L"-" + std::to_wstring(summary.id) + L"-" + std::to_wstring(NowEpochMs()))).wstring(),
      &exported_path);
  if (!status.ok()) {
    return status;
  }
  if (!exported_path.empty() && std::filesystem::exists(exported_path)) {
    paths->push_back(exported_path);
    return common::Status::Ok();
  }
  return common::Status::Error(common::StatusCode::NotFound, "No exported image file was produced.");
}

bool MainWindow::TryStartExternalDrag(int index) {
  if (index < 0) {
    index = row_drag_candidate_index_;
  }
  const auto* summary = GetSummaryAt(index);
  if (summary == nullptr) {
    return false;
  }
  if (summary->content_type != core::store::ClipContentType::Files && summary->content_type != core::store::ClipContentType::Image) {
    return false;
  }

  std::vector<std::wstring> paths;
  const common::Status status = BuildDragFileList(*summary, &paths);
  if (!status.ok() || paths.empty()) {
    RefreshStatus(L"Cannot drag out this item");
    return false;
  }

  const bool was_row_drag_active = row_drag_active_;
  const int dragged_row_index = row_drag_item_index_;
  const POINT dragged_row_point = row_drag_point_;
  const int dragged_row_pointer_offset_y = row_drag_pointer_offset_y_;

  row_drag_active_ = false;
  row_drag_candidate_active_ = false;
  row_drag_candidate_index_ = -1;
  row_drag_item_index_ = -1;
  row_drop_index_ = -1;
  row_drop_after_ = false;
  row_drag_point_ = POINT{-1, -1};
  row_drag_pointer_offset_y_ = 0;
  row_drag_scope_ = ReorderScope::None;
  armed_click_item_index_ = -1;
  if (GetCapture() == hwnd_) {
    ReleaseCapture();
  }
  ClearRowDragSession();

  HGLOBAL drop_handle = CreateFileDropHandle(paths);
  if (drop_handle == nullptr) {
    RefreshStatus(L"\u51c6\u5907\u62d6\u62fd\u6570\u636e\u5931\u8d25");
    return false;
  }

  auto* data_object = new (std::nothrow) FileDropDataObject(drop_handle);
  if (data_object == nullptr) {
    GlobalFree(drop_handle);
    RefreshStatus(L"\u51c6\u5907\u62d6\u62fd\u6570\u636e\u5931\u8d25");
    return false;
  }
  (void)was_row_drag_active;
  (void)dragged_row_index;
  (void)dragged_row_point;
  (void)dragged_row_pointer_offset_y;
  HBITMAP preview_bitmap = CreateDragPreviewCardBitmap(*summary, paths);
  SIZE preview_size{0, 0};
  if (preview_bitmap != nullptr) {
    BITMAP preview_object{};
    GetObject(preview_bitmap, sizeof(preview_object), &preview_object);
    preview_size.cx = std::max(1, static_cast<int>(preview_object.bmWidth));
    preview_size.cy = std::max(1, static_cast<int>(preview_object.bmHeight));
  }
  HWND preview_hwnd = preview_bitmap != nullptr
      ? CreateDragPreviewWindow(hwnd_, preview_bitmap, preview_size, POINT{14, 14})
      : nullptr;
  auto* drop_source = new (std::nothrow) SimpleDropSource(preview_hwnd, preview_bitmap, POINT{14, 14});
  if (drop_source == nullptr) {
    if (preview_hwnd != nullptr) {
      DestroyWindow(preview_hwnd);
    }
    if (preview_bitmap != nullptr) {
      DeleteObject(preview_bitmap);
    }
    data_object->Release();
    RefreshStatus(L"Prepare drag source failed");
    return false;
  }

  DWORD effect = DROPEFFECT_NONE;
  const HRESULT hr = DoDragDrop(data_object, drop_source, DROPEFFECT_COPY, &effect);
  drop_source->Release();
  data_object->Release();

  if (SUCCEEDED(hr) && effect != DROPEFFECT_NONE) {
    RefreshStatus(L"Drag export completed");
    return true;
  }
  if (hr == DRAGDROP_S_CANCEL || effect == DROPEFFECT_NONE) {
    RefreshStatus(L"Drag export cancelled");
  } else {
    RefreshStatus(L"\u62d6\u62fd\u5bfc\u51fa\u5931\u8d25");
  }
  return false;
}

void MainWindow::FinishRowDrag(POINT, bool cancel) {
  const bool was_active = row_drag_active_;
  const int drag_index = row_drag_item_index_;
  const int drop_index = row_drop_index_;
  const ReorderScope scope = row_drag_scope_;

  row_drag_active_ = false;
  row_drag_candidate_active_ = false;
  row_drag_candidate_index_ = -1;
  row_drag_item_index_ = -1;
  row_drop_index_ = -1;
  row_drop_after_ = false;
  row_drag_point_ = POINT{-1, -1};
  row_drag_pointer_offset_y_ = 0;
  row_drag_scope_ = ReorderScope::None;
  auto drag_session = row_drag_session_;
  ClearRowDragSession();
  if (GetCapture() == hwnd_) {
    ReleaseCapture();
  }

  if (!was_active) {
    return;
  }

  if (cancel || library_mutator_ == nullptr || drag_index < 0 || drop_index < 0 || state_.item_count_ <= 1 || scope == ReorderScope::None) {
    InvalidateListRegion();
    return;
  }

  if (!drag_session.has_value()) {
    InvalidateListRegion();
    return;
  }
  std::vector<std::int64_t> ordered_ids;
  ordered_ids.reserve(drag_session->ordered_indexes.size() + 1);
  for (int slot = 0; slot <= static_cast<int>(drag_session->ordered_indexes.size()); ++slot) {
    if (slot == drag_session->desired_slot) {
      const auto* dragged_summary = GetSummaryAt(drag_index);
      if (dragged_summary != nullptr) {
        ordered_ids.push_back(dragged_summary->id);
      }
    }
    if (slot < static_cast<int>(drag_session->ordered_indexes.size())) {
      const auto* summary = GetSummaryAt(drag_session->ordered_indexes[slot]);
      if (summary != nullptr) {
        ordered_ids.push_back(summary->id);
      }
    }
  }
  std::vector<std::int64_t> original_ids;
  original_ids.reserve(drag_session->original_ordered_indexes.size() + 1);
  for (int slot = 0; slot <= static_cast<int>(drag_session->original_ordered_indexes.size()); ++slot) {
    if (slot == drag_session->original_gap_slot) {
      const auto* dragged_summary = GetSummaryAt(drag_index);
      if (dragged_summary != nullptr) {
        original_ids.push_back(dragged_summary->id);
      }
    }
    if (slot < static_cast<int>(drag_session->original_ordered_indexes.size())) {
      const auto* summary = GetSummaryAt(drag_session->original_ordered_indexes[slot]);
      if (summary != nullptr) {
        original_ids.push_back(summary->id);
      }
    }
  }
  if (ordered_ids.empty() || ordered_ids.size() != original_ids.size()) {
    InvalidateListRegion();
    return;
  }
  const bool changed = ordered_ids != original_ids;
  if (!changed) {
    InvalidateListRegion();
    return;
  }

  // P0-1:重排单事务批量提交,worker 执行,完成后回投 UI 线程刷新。旧实现是
  // UI 线程上 N 次独立加锁 UPDATE 循环(50 项队列 = 50 次串行往返,全部在
  // drop 消息处理里)。
  std::vector<core::store::ReorderEntry> entries;
  entries.reserve(ordered_ids.size());
  for (size_t i = 0; i < ordered_ids.size(); ++i) {
    entries.push_back(core::store::ReorderEntry{ordered_ids[i], static_cast<int>((i + 1) * 10)});
  }
  const std::int64_t tag_id = state_.current_tag_id();
  auto* mutator = library_mutator_;
  PostLibraryMutation(
      [mutator, scope, tag_id, entries = std::move(entries)]() -> common::Status {
        switch (scope) {
          case ReorderScope::Queue:
            return mutator->ReorderQueueBatch(entries);
          case ReorderScope::Favorites:
            return mutator->ReorderFavoritesBatch(entries);
          case ReorderScope::Tag:
            return mutator->ReorderTagItemsBatch(tag_id, entries);
          case ReorderScope::Pinned:
            return mutator->ReorderPinnedBatch(entries);
          case ReorderScope::None:
          default:
            return common::Status::Ok();
        }
      },
      [this, scope](const common::Status& status) {
        if (status.ok()) {
          switch (scope) {
            case ReorderScope::Queue:
              RefreshStatus(L"Queue order updated");
              break;
            case ReorderScope::Favorites:
              RefreshStatus(L"Favorite order updated");
              break;
            case ReorderScope::Tag:
              RefreshStatus(L"Tag order updated");
              break;
            case ReorderScope::Pinned:
              RefreshStatus(L"Pinned order updated");
              break;
            case ReorderScope::None:
            default:
              break;
          }
        } else {
          RefreshStatus(L"\u8c03\u6574\u987a\u5e8f\u5931\u8d25\uff0c\u5df2\u8fd8\u539f");
        }
        // 成功与失败都重查:成功呈现新顺序,失败回到库里的真实顺序。
        RefreshData(true);
      });
}


}  // namespace clipd2d::ui
