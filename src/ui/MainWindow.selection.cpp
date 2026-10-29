#include "ui/MainWindowInternal.h"
#include "core/commands/IHistoryFeed.h"
#include "core/commands/ILibraryMutator.h"
#include "core/clipboard/OpenTargetResolver.h"

// 选中面：选中索引/加载、收藏/置顶切换、选中项操作（删除/打开/复制/预览）、资料夹切换。
namespace clipd2d::ui {

const core::store::ClipSummary* MainWindow::GetSummaryAt(int index) {
  if (index < 0 || index >= state_.item_count_) {
    return nullptr;
  }
  const int page = index / state_.page_size_;
  const int offset = index % state_.page_size_;
  auto it = state_.page_cache_.find(page);
  if (it == state_.page_cache_.end()) {
    RequestPageAsync(page);
    return nullptr;
  }
  TouchPage(page);
  if (offset < 0 || offset >= static_cast<int>(it->second.size())) {
    return nullptr;
  }
  return &it->second[static_cast<size_t>(offset)];
}

const core::store::ClipSummary* MainWindow::GetSelectedSummary() const {
  if (state_.selected_index_ < 0 || state_.selected_index_ >= state_.item_count_) {
    return nullptr;
  }
  const int page = state_.selected_index_ / state_.page_size_;
  const int offset = state_.selected_index_ % state_.page_size_;
  auto it = state_.page_cache_.find(page);
  if (it == state_.page_cache_.end()) {
    return nullptr;
  }
  if (offset < 0 || offset >= static_cast<int>(it->second.size())) {
    return nullptr;
  }
  return &it->second[static_cast<size_t>(offset)];
}

void MainWindow::SelectIndex(int index, bool open_drawer) {
  if (index < 0 || index >= state_.item_count_) {
    return;
  }
  const int previous_index = state_.selected_index_;
  state_.selected_index_ = index;
  EnsureSelectionVisible();
  LoadSelection();
  if (open_drawer) {
    StartDrawerAnimation(true);
  }
  MarkRowDirty(previous_index);
  MarkRowDirty(state_.selected_index_);
  InvalidateListRegion();
}

void MainWindow::LoadSelection() {
  const auto* summary = GetSummaryAt(state_.selected_index_);
  if (summary != nullptr) {
    RequestSelectedClipAsync(summary->id);
  }
}

void MainWindow::ToggleFavoriteSelected() {
  const auto* summary = GetSelectedSummary();
  if (summary == nullptr || library_mutator_ == nullptr) {
    return;
  }
  const std::int64_t clip_id = summary->id;
  const bool favorite = !summary->is_favorite;
  const int selected_index = state_.selected_index_;
  // P0-1 乐观翻转：立即改本地缓存并重绘，写库走异步串行通道；失败回滚 + 提示。
  // 刷新（含 Favorites 视图的条目移除）推迟到写库完成后，避免异步重查读到旧值。
  const auto apply_flag = [this, clip_id](bool value) {
    for (auto& [page_index, clips] : state_.page_cache_) {
      for (auto& clip : clips) {
        if (clip.id == clip_id) {
          clip.is_favorite = value;
        }
      }
    }
    if (state_.selected_clip_.has_value() && state_.selected_clip_->summary.id == clip_id) {
      state_.selected_clip_->summary.is_favorite = value;
    }
  };
  apply_flag(favorite);
  MarkRowDirty(selected_index);
  InvalidateListRegion();
  auto* mutator = library_mutator_;
  PostLibraryMutation(
      [mutator, clip_id, favorite] { return mutator->ToggleFavorite(clip_id, favorite); },
      [this, favorite, apply_flag](const common::Status& status) {
        if (!status.ok()) {
          apply_flag(!favorite);
          MarkRowDirty(state_.selected_index_);
          InvalidateListRegion();
          RefreshStatus(L"Update favorite failed");
          return;
        }
        if (state_.current_view_ == core::store::ClipView::Favorites && !favorite) {
          RefreshData(false);
        } else {
          RefreshData(true);
        }
      });
}

void MainWindow::TogglePinnedSelected() {
  const auto* summary = GetSelectedSummary();
  if (summary == nullptr || library_mutator_ == nullptr) {
    return;
  }
  const std::int64_t clip_id = summary->id;
  const bool pinned = !summary->is_pinned;
  const int selected_index = state_.selected_index_;
  // P0-1 乐观翻转，同 ToggleFavoriteSelected。
  const auto apply_flag = [this, clip_id](bool value) {
    for (auto& [page_index, clips] : state_.page_cache_) {
      for (auto& clip : clips) {
        if (clip.id == clip_id) {
          clip.is_pinned = value;
        }
      }
    }
    if (state_.selected_clip_.has_value() && state_.selected_clip_->summary.id == clip_id) {
      state_.selected_clip_->summary.is_pinned = value;
    }
  };
  apply_flag(pinned);
  MarkRowDirty(selected_index);
  InvalidateListRegion();
  auto* mutator = library_mutator_;
  PostLibraryMutation(
      [mutator, clip_id, pinned] { return mutator->TogglePinned(clip_id, pinned); },
      [this, pinned, apply_flag](const common::Status& status) {
        if (!status.ok()) {
          apply_flag(!pinned);
          MarkRowDirty(state_.selected_index_);
          InvalidateListRegion();
          RefreshStatus(L"Update pinned failed");
          return;
        }
        if (state_.current_view_ == core::store::ClipView::Pinned && !pinned) {
          RefreshData(false);
        } else {
          RefreshData(true);
        }
      });
}

void MainWindow::DeleteSelected() {
  const auto* summary = GetSelectedSummary();
  if (summary == nullptr || library_mutator_ == nullptr) {
    return;
  }

  std::optional<std::int64_t> replacement_clip_id;
  const int deleted_index = state_.selected_index_;
  if (deleted_index >= 0) {
    if (const auto* next_summary = GetSummaryAt(deleted_index + 1); next_summary != nullptr && next_summary->id != summary->id) {
      replacement_clip_id = next_summary->id;
    } else if (deleted_index > 0) {
      if (const auto* previous_summary = GetSummaryAt(deleted_index - 1); previous_summary != nullptr && previous_summary->id != summary->id) {
        replacement_clip_id = previous_summary->id;
      }
    }
  }

  // P0-1 删除改异步：删除会移动后续所有行的索引，不适合乐观移除；写库在
  // worker 执行（UI 不阻塞），完成后回投做选中态收尾与刷新。
  const std::int64_t clip_id = summary->id;
  auto* mutator = library_mutator_;
  PostLibraryMutation(
      [mutator, clip_id] { return mutator->DeleteClip(clip_id); },
      [this, replacement_clip_id](const common::Status& status) {
        if (!status.ok()) {
          RefreshStatus(L"\u5220\u9664\u5931\u8d25");
          return;
        }
        state_.selected_clip_.reset();
        state_.actions_.clear();
        state_.explicit_selected_clip_id_ = replacement_clip_id;
        RefreshData(true);
      });
}

void MainWindow::OpenSelectedFolder() {
  if (!state_.selected_clip_.has_value()) {
    return;
  }
  std::wstring path;
  if (state_.selected_clip_->summary.content_type == core::store::ClipContentType::Image) {
    path = DirectoryFromPath(state_.selected_clip_->storage_path);
  } else if (state_.selected_clip_->summary.content_type == core::store::ClipContentType::Files) {
    path = DirectoryFromPath(FirstFilePathFromClip(*state_.selected_clip_));
  } else {
    path = DirectoryFromPath(state_.selected_clip_->summary.source_app_path);
  }
  OpenDirectoryPath(path);
}

void MainWindow::CopySelectedPath() {
  if (!state_.selected_clip_.has_value()) {
    return;
  }
  std::wstring path;
  if (state_.selected_clip_->summary.content_type == core::store::ClipContentType::Image) {
    path = state_.selected_clip_->storage_path;
  } else if (state_.selected_clip_->summary.content_type == core::store::ClipContentType::Files) {
    path = FirstFilePathFromClip(*state_.selected_clip_);
  }
  if (path.empty()) {
    return;
  }
  SuppressClipboardCapture();
  if (CopyTextToClipboard(hwnd_, path)) {
    RefreshStatus(L"Copied path");
  } else {
    RefreshStatus(L"\u590d\u5236\u8def\u5f84\u5931\u8d25");
  }
}

void MainWindow::OpenSelectedPreview() {
  EnsureSelectedClipLoaded();
  if (!state_.selected_clip_.has_value()) {
    return;
  }
  const common::Status status = EnsurePreviewWindowCreated();
  if (!status.ok()) {
    RefreshStatus(L"\u6253\u5f00\u9884\u89c8\u5931\u8d25");
    return;
  }
  preview_window_.ShowClip(*state_.selected_clip_, BuildPreviewHostActions());
}

void MainWindow::OpenSelectedWithDefaultApp() {
  EnsureSelectedClipLoaded();
  if (!state_.selected_clip_.has_value()) {
    return;
  }

  std::wstring path;
  if (state_.selected_clip_->summary.content_type == core::store::ClipContentType::Files) {
    path = FirstFilePathFromClip(*state_.selected_clip_);
  } else if (state_.selected_clip_->summary.content_type == core::store::ClipContentType::Image) {
    if (!state_.selected_clip_->secondary_path.empty()) {
      path = state_.selected_clip_->secondary_path;
    } else {
      path = state_.selected_clip_->storage_path;
    }
  }

  if (path.empty()) {
    RefreshStatus(L"\u6ca1\u6709\u53ef\u6253\u5f00\u7684\u8def\u5f84");
    return;
  }

  const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
  if (result <= 32) {
    RefreshStatus(L"\u6253\u5f00\u5931\u8d25");
  }
}

void MainWindow::OpenSelectedResolvedTarget() {
  EnsureSelectedClipLoaded();
  if (!state_.selected_clip_.has_value()) {
    return;
  }

  const core::clipboard::OpenTarget target =
      core::clipboard::ResolveOpenTargetFromText(state_.selected_clip_->plain_text);
  if (target.kind == core::clipboard::OpenTargetKind::None || target.value.empty()) {
    RefreshStatus(L"No resolvable target");
    return;
  }

  const auto result =
      reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", target.value.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
  if (result <= 32) {
    RefreshStatus(L"\u6253\u5f00\u5931\u8d25");
  }
}

bool MainWindow::SelectGroupById(std::int64_t group_id) {
  const core::store::GroupRecord* group = FindGroupById(group_id);
  if (group == nullptr) {
    return false;
  }

  core::browser::BrowserCommand select_group;
  select_group.kind = core::browser::BrowserCommandKind::SelectGroup;
  select_group.group_id = group->id;
  select_group.group_name = group->name;
  ApplyBrowserCommand(select_group);
  RefreshData(false);
  return true;
}

bool MainWindow::SelectGroupByIndex(size_t index) {
  if (index >= state_.groups_.size()) {
    return false;
  }
  return SelectGroupById(state_.groups_[index].id);
}

void MainWindow::EnsureSelectedClipLoaded() {
  const auto* summary = GetSelectedSummary();
  if (summary == nullptr || history_feed_ == nullptr) {
    return;
  }
  if (state_.selected_clip_ && state_.selected_clip_->summary.id == summary->id) {
    return;
  }
  core::store::ClipRecord clip;
  const common::Status status = history_feed_->GetClipById(summary->id, &clip);
  if (!status.ok()) {
    return;
  }
  state_.selected_clip_ = std::move(clip);
  state_.actions_ = history_feed_->ResolveActions(*state_.selected_clip_);
  SortActions(&state_.actions_);
}

}  // namespace clipd2d::ui
