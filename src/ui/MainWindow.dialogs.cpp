// Responsibility: Dialog panel configuration and submit handlers for group/tag workflows.
// Key dependencies: MainWindow state, DialogPanelWindow, AppController mutations, BrowserCommand updates.
// Invariants: All group/tag create/rename/delete dialogs must route through DialogPanelWindow with settings-style interaction guards.

#include "ui/MainWindowInternal.h"

namespace clipd2d::ui {

namespace {

std::vector<DialogPanelWindow::ColorOption> BuildTagDialogColorOptions() {
  return {
      {L"\u84dd\u8272", L"#3B82F6"},
      {L"\u7eff\u8272", L"#10B981"},
      {L"\u6a59\u8272", L"#F59E0B"},
      {L"\u7ea2\u8272", L"#EF4444"},
      {L"\u7d2b\u8272", L"#8B5CF6"},
      {L"\u9752\u8272", L"#06B6D4"},
      {L"\u7c89\u8272", L"#EC4899"},
      {L"\u7070\u8272", L"#6B7280"},
  };
}

DialogPanelWindow::HostActions MakeDialogPanelHostActions(
    MainWindow* owner,
    std::function<void(const DialogPanelWindow::Result&)> on_result) {
  DialogPanelWindow::HostActions actions;
  actions.on_show = [owner]() { owner->NotifySettingsWindowShown(); };
  actions.on_hide = [owner]() { owner->NotifySettingsWindowHidden(); };
  actions.on_begin_input_focus = [owner]() { owner->CaptureExternalFocusRestoreContext(); };
  actions.on_end_input_focus = [owner]() { owner->RestoreExternalFocusRestoreContext(); };
  actions.on_result = std::move(on_result);
  return actions;
}
}  // namespace

std::optional<std::int64_t> MainWindow::PromptCreateGroup() {
  OpenCreateGroupDialog();
  return std::nullopt;
}

const core::store::GroupRecord* MainWindow::FindGroupById(std::int64_t group_id) const {
  if (group_id <= 0) {
    return nullptr;
  }
  const auto it = std::find_if(groups_.begin(), groups_.end(), [group_id](const core::store::GroupRecord& group) {
    return group.id == group_id;
  });
  return it != groups_.end() ? &(*it) : nullptr;
}

void MainWindow::OpenCreateGroupDialog() {
  if (!EnsureDialogPanelWindowCreated().ok()) {
    RefreshStatus(L"对话框初始化失败");
    return;
  }
  DialogPanelWindow::Config config;
  config.has_text_input = true;
  config.auto_focus_input = true;
  config.requires_non_empty_text = true;
  config.title = L"\u65b0\u5efa\u8d44\u6599\u5939";
  config.prompt = L"\u8d44\u6599\u5939\u540d\u79f0";
  config.confirm_text = L"\u65b0\u5efa";
  config.cancel_text = L"\u53d6\u6d88";
  config.empty_text_error = L"\u8d44\u6599\u5939\u540d\u79f0\u4e0d\u80fd\u4e3a\u7a7a\u3002";

  auto actions = MakeDialogPanelHostActions(
      this,
      [this](const DialogPanelWindow::Result& result) {
        if (!result.confirmed || controller_ == nullptr) {
          return;
        }
        std::int64_t group_id = 0;
        const common::Status status = controller_->EnsureGroup(result.text, &group_id);
        if (!status.ok() || group_id <= 0) {
          RefreshStatus(L"\u65b0\u5efa\u8d44\u6599\u5939\u5931\u8d25");
          return;
        }
        RefreshGroups();
        RefreshStatus(L"\u5df2\u65b0\u5efa\u8d44\u6599\u5939");
        SelectGroupById(group_id);
      });
  dialog_panel_window_.Show(config, std::move(actions));
}

void MainWindow::OpenRenameGroupDialog(std::int64_t group_id) {
  const core::store::GroupRecord* group = FindGroupById(group_id);
  if (group == nullptr) {
    return;
  }
  if (!EnsureDialogPanelWindowCreated().ok()) {
    RefreshStatus(L"对话框初始化失败");
    return;
  }
  DialogPanelWindow::Config config;
  config.has_text_input = true;
  config.auto_focus_input = true;
  config.requires_non_empty_text = true;
  config.title = L"\u91cd\u547d\u540d\u8d44\u6599\u5939";
  config.prompt = L"\u8d44\u6599\u5939\u540d\u79f0";
  config.confirm_text = L"\u4fdd\u5b58";
  config.cancel_text = L"\u53d6\u6d88";
  config.initial_text = group->name;
  config.empty_text_error = L"\u8d44\u6599\u5939\u540d\u79f0\u4e0d\u80fd\u4e3a\u7a7a\u3002";

  auto actions = MakeDialogPanelHostActions(
      this,
      [this, group_id](const DialogPanelWindow::Result& result) {
        if (!result.confirmed || controller_ == nullptr) {
          return;
        }
        const common::Status status = controller_->RenameGroup(group_id, result.text);
        if (!status.ok()) {
          RefreshStatus(L"\u91cd\u547d\u540d\u8d44\u6599\u5939\u5931\u8d25");
          return;
        }
        if (current_group_id_ == group_id) {
          browser_state_.group_name = result.text;
          current_group_name_ = result.text;
        }
        RefreshGroups();
        RefreshStatus(L"\u5df2\u91cd\u547d\u540d\u8d44\u6599\u5939");
        RefreshData(true);
      });
  dialog_panel_window_.Show(config, std::move(actions));
}

void MainWindow::OpenCreateTagDialog(std::int64_t clip_id, bool apply_filter_after_create) {
  if (!EnsureDialogPanelWindowCreated().ok()) {
    RefreshStatus(L"对话框初始化失败");
    return;
  }
  DialogPanelWindow::Config config;
  config.has_text_input = true;
  config.auto_focus_input = true;
  config.requires_non_empty_text = true;
  config.has_color_options = true;
  config.title = L"\u65b0\u5efa\u6807\u7b7e";
  config.prompt = L"\u6807\u7b7e\u540d\u79f0";
  config.confirm_text = L"\u65b0\u5efa";
  config.cancel_text = L"\u53d6\u6d88";
  config.empty_text_error = L"\u6807\u7b7e\u540d\u79f0\u4e0d\u80fd\u4e3a\u7a7a\u3002";
  config.initial_color = L"#3B82F6";
  config.color_options = BuildTagDialogColorOptions();

  auto actions = MakeDialogPanelHostActions(
      this,
      [this, clip_id, apply_filter_after_create](const DialogPanelWindow::Result& result) {
        if (!result.confirmed || controller_ == nullptr) {
          return;
        }
        std::int64_t tag_id = 0;
        common::Status status = controller_->EnsureTag(result.text, result.color, &tag_id);
        if (!status.ok() || tag_id <= 0) {
          RefreshStatus(L"\u65b0\u5efa\u6807\u7b7e\u5931\u8d25");
          return;
        }
        if (clip_id > 0) {
          status = controller_->AddTag(clip_id, tag_id);
          if (!status.ok()) {
            RefreshStatus(L"\u6dfb\u52a0\u6807\u7b7e\u5931\u8d25");
            return;
          }
        }
        RefreshTags();
        if (apply_filter_after_create || clip_id == 0) {
          core::browser::BrowserCommand select_tag;
          select_tag.kind = core::browser::BrowserCommandKind::SelectTag;
          select_tag.tag_id = tag_id;
          ApplyBrowserCommand(select_tag);
        }
        RefreshStatus(L"Created tag");
        RefreshData(true);
      });
  dialog_panel_window_.Show(config, std::move(actions));
}

void MainWindow::OpenRenameTagDialog(std::int64_t tag_id) {
  const auto it = std::find_if(tags_.begin(), tags_.end(), [tag_id](const core::store::TagRecord& tag) {
    return tag.id == tag_id;
  });
  if (it == tags_.end()) {
    return;
  }
  if (!EnsureDialogPanelWindowCreated().ok()) {
    RefreshStatus(L"对话框初始化失败");
    return;
  }
  DialogPanelWindow::Config config;
  config.has_text_input = true;
  config.auto_focus_input = true;
  config.requires_non_empty_text = true;
  config.has_color_options = true;
  config.title = L"\u91cd\u547d\u540d\u6807\u7b7e";
  config.prompt = L"\u6807\u7b7e\u540d\u79f0";
  config.confirm_text = L"\u4fdd\u5b58";
  config.cancel_text = L"\u53d6\u6d88";
  config.initial_text = it->name;
  config.initial_color = it->color;
  config.empty_text_error = L"\u6807\u7b7e\u540d\u79f0\u4e0d\u80fd\u4e3a\u7a7a\u3002";
  config.color_options = BuildTagDialogColorOptions();

  auto actions = MakeDialogPanelHostActions(
      this,
      [this, tag_id](const DialogPanelWindow::Result& result) {
        if (!result.confirmed || controller_ == nullptr) {
          return;
        }
        const common::Status status = controller_->RenameTag(tag_id, result.text, result.color);
        if (!status.ok()) {
          RefreshStatus(L"Rename tag failed");
          return;
        }
        core::browser::BrowserCommand select_tag;
        select_tag.kind = core::browser::BrowserCommandKind::SelectTag;
        select_tag.tag_id = tag_id;
        ApplyBrowserCommand(select_tag);
        RefreshTags();
        RefreshStatus(L"Renamed tag");
        RefreshData(true);
      });
  dialog_panel_window_.Show(config, std::move(actions));
}

void MainWindow::OpenDeleteGroupDialog(std::int64_t group_id) {
  const core::store::GroupRecord* group = FindGroupById(group_id);
  if (group == nullptr) {
    return;
  }
  if (!EnsureDialogPanelWindowCreated().ok()) {
    RefreshStatus(L"对话框初始化失败");
    return;
  }
  DialogPanelWindow::Config config;
  config.dangerous_confirm = true;
  config.title = L"\u5220\u9664\u8d44\u6599\u5939";
  config.message = L"\u5220\u9664\u8d44\u6599\u5939\u201c" + group->name + L"\u201d\uff1f\n\u5176\u4e2d\u6761\u76ee\u4f1a\u4fdd\u7559\u5728\u8d44\u6599\u5e93\u4e2d\u3002";
  config.confirm_text = L"\u5220\u9664";
  config.cancel_text = L"\u53d6\u6d88";

  auto actions = MakeDialogPanelHostActions(
      this,
      [this, group_id](const DialogPanelWindow::Result& result) {
        if (!result.confirmed || controller_ == nullptr) {
          return;
        }
        const common::Status status = controller_->DeleteGroup(group_id);
        if (!status.ok()) {
          RefreshStatus(L"\u5220\u9664\u8d44\u6599\u5939\u5931\u8d25");
          return;
        }
        if (current_group_id_ == group_id) {
          ApplyBrowserCommand(core::browser::BrowserCommand{core::browser::BrowserCommandKind::SetScopeLibrary});
        }
        RefreshGroups();
        RefreshStatus(L"\u5df2\u5220\u9664\u8d44\u6599\u5939");
        RefreshData(true);
      });
  dialog_panel_window_.Show(config, std::move(actions));
}

void MainWindow::OpenDeleteTagDialog(std::int64_t tag_id) {
  const auto it = std::find_if(tags_.begin(), tags_.end(), [tag_id](const core::store::TagRecord& tag) {
    return tag.id == tag_id;
  });
  if (it == tags_.end()) {
    return;
  }
  if (!EnsureDialogPanelWindowCreated().ok()) {
    RefreshStatus(L"对话框初始化失败");
    return;
  }
  DialogPanelWindow::Config config;
  config.dangerous_confirm = true;
  config.title = L"\u5220\u9664\u6807\u7b7e";
  config.message = L"\u5220\u9664\u6807\u7b7e\u201c" + it->name + L"\u201d\uff1f\n\u5df2\u6253\u4e0a\u8be5\u6807\u7b7e\u7684\u6761\u76ee\u4e0d\u4f1a\u88ab\u5220\u9664\u3002";
  config.confirm_text = L"\u5220\u9664";
  config.cancel_text = L"\u53d6\u6d88";

  auto actions = MakeDialogPanelHostActions(
      this,
      [this, tag_id](const DialogPanelWindow::Result& result) {
        if (!result.confirmed || controller_ == nullptr) {
          return;
        }
        const common::Status status = controller_->DeleteTag(tag_id);
        if (!status.ok()) {
          RefreshStatus(L"\u5220\u9664\u6807\u7b7e\u5931\u8d25");
          return;
        }
        ApplyBrowserCommand(core::browser::BrowserCommand{
            core::browser::BrowserCommandKind::ClearTags});
        RefreshTags();
        RefreshStatus(L"Deleted tag");
        RefreshData(true);
      });
  dialog_panel_window_.Show(config, std::move(actions));
}

bool MainWindow::RenameGroupById(std::int64_t group_id) {
  OpenRenameGroupDialog(group_id);
  return true;
}

bool MainWindow::DeleteGroupById(std::int64_t group_id) {
  OpenDeleteGroupDialog(group_id);
  return true;
}

bool MainWindow::CreateGroupAndSelect() {
  OpenCreateGroupDialog();
  return true;
}

bool MainWindow::MoveClipToGroupById(
    std::int64_t clip_id,
    std::int64_t group_id,
    const std::wstring& success_status) {
  if (controller_ == nullptr || clip_id <= 0 || group_id <= 0) {
    return false;
  }
  if (!controller_->MoveClipsToGroup({clip_id}, group_id).ok()) {
    return false;
  }
  RefreshStatus(success_status);
  RefreshData(true);
  return true;
}

bool MainWindow::MoveClipToGroupByIndex(std::int64_t clip_id, size_t group_index) {
  if (group_index >= groups_.size()) {
    return false;
  }
  return MoveClipToGroupById(clip_id, groups_[group_index].id, L"\u5df2\u79fb\u52a8\u5230\u8d44\u6599\u5939");
}

bool MainWindow::CreateGroupAndMoveClip(std::int64_t clip_id) {
  if (!EnsureDialogPanelWindowCreated().ok()) {
    RefreshStatus(L"对话框初始化失败");
    return false;
  }
  DialogPanelWindow::Config config;
  config.has_text_input = true;
  config.auto_focus_input = true;
  config.requires_non_empty_text = true;
  config.title = L"\u65b0\u5efa\u8d44\u6599\u5939";
  config.prompt = L"\u8d44\u6599\u5939\u540d\u79f0";
  config.confirm_text = L"\u65b0\u5efa";
  config.cancel_text = L"\u53d6\u6d88";
  config.empty_text_error = L"\u8d44\u6599\u5939\u540d\u79f0\u4e0d\u80fd\u4e3a\u7a7a\u3002";

  auto actions = MakeDialogPanelHostActions(
      this,
      [this, clip_id](const DialogPanelWindow::Result& result) {
        if (!result.confirmed || controller_ == nullptr) {
          return;
        }
        std::int64_t group_id = 0;
        const common::Status status = controller_->EnsureGroup(result.text, &group_id);
        if (!status.ok() || group_id <= 0) {
          RefreshStatus(L"\u65b0\u5efa\u8d44\u6599\u5939\u5931\u8d25");
          return;
        }
        RefreshGroups();
        MoveClipToGroupById(clip_id, group_id, L"\u5df2\u79fb\u52a8\u5230\u65b0\u8d44\u6599\u5939");
      });
  dialog_panel_window_.Show(config, std::move(actions));
  return true;
}

bool MainWindow::PromptCreateTag(std::int64_t clip_id, bool apply_filter_after_create) {
  OpenCreateTagDialog(clip_id, apply_filter_after_create);
  return true;
}

void MainWindow::RenameCurrentTag() {
  if (controller_ == nullptr || current_tag_id_ <= 0) {
    return;
  }
  OpenRenameTagDialog(current_tag_id_);
}

void MainWindow::DeleteCurrentTag() {
  if (controller_ == nullptr || current_tag_id_ <= 0) {
    return;
  }
  OpenDeleteTagDialog(current_tag_id_);
}

}  // namespace clipd2d::ui
