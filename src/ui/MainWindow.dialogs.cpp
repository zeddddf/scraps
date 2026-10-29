// Responsibility: Dialog panel configuration and submit handlers for group/tag workflows.
// Key dependencies: MainWindow state, DialogPanelWindow, AppController mutations, BrowserCommand updates.
// Invariants: All group/tag create/rename/delete dialogs must route through DialogPanelWindow with settings-style interaction guards.

#include "ui/MainWindowInternal.h"
#include "core/commands/ILibraryMutator.h"

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
  const auto it = std::find_if(state_.groups_.begin(), state_.groups_.end(), [group_id](const core::store::GroupRecord& group) {
    return group.id == group_id;
  });
  return it != state_.groups_.end() ? &(*it) : nullptr;
}

void MainWindow::OpenCreateGroupDialog(const std::wstring& retry_text) {
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
  config.initial_text = retry_text;

  auto actions = MakeDialogPanelHostActions(
      this,
      [this](const DialogPanelWindow::Result& result) {
        if (!result.confirmed || library_mutator_ == nullptr) {
          return;
        }
        // P0-1:写库异步（EnsureGroup 的输出 id 经 shared_ptr 带回 UI 线程）。
        auto* group_mutator = library_mutator_;
        const std::wstring group_name = result.text;
        auto group_id = std::make_shared<std::int64_t>(0);
        PostLibraryMutation(
            [group_mutator, group_name, group_id]() -> common::Status {
              const common::Status status = group_mutator->EnsureGroup(group_name, &*group_id);
              if (status.ok() && *group_id <= 0) {
                return common::Status::Error(common::StatusCode::InternalError, "ensure group returned no id");
              }
              return status;
            },
            [this, group_name, group_id](const common::Status& status) {
              if (!status.ok() || *group_id <= 0) {
                // 失败恢复：重开对话框预填用户输入，避免输入丢失（审查 P1-1）。
                RefreshStatus(L"\u65b0\u5efa\u8d44\u6599\u5939\u5931\u8d25");
                OpenCreateGroupDialog(group_name);
                return;
              }
              RefreshGroups();
              RefreshStatus(L"\u5df2\u65b0\u5efa\u8d44\u6599\u5939");
              SelectGroupById(*group_id);
            });
      });
  dialog_panel_window_.Show(config, std::move(actions));
}

void MainWindow::OpenRenameGroupDialog(std::int64_t group_id, const std::wstring& retry_text) {
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
  config.initial_text = retry_text.empty() ? group->name : retry_text;
  config.empty_text_error = L"\u8d44\u6599\u5939\u540d\u79f0\u4e0d\u80fd\u4e3a\u7a7a\u3002";

  auto actions = MakeDialogPanelHostActions(
      this,
      [this, group_id](const DialogPanelWindow::Result& result) {
        if (!result.confirmed || library_mutator_ == nullptr) {
          return;
        }
        auto* group_mutator = library_mutator_;
        const std::wstring group_name = result.text;
        PostLibraryMutation(
            [group_mutator, group_id, group_name] { return group_mutator->RenameGroup(group_id, group_name); },
            [this, group_id, group_name](const common::Status& status) {
              if (!status.ok()) {
                // 失败恢复：重开对话框预填用户输入（审查 P1-1）。
                RefreshStatus(L"\u91cd\u547d\u540d\u8d44\u6599\u5939\u5931\u8d25");
                OpenRenameGroupDialog(group_id, group_name);
                return;
              }
              if (state_.current_group_id() == group_id) {
                state_.browser_state_.group_name = group_name;
              }
              RefreshGroups();
              RefreshStatus(L"\u5df2\u91cd\u547d\u540d\u8d44\u6599\u5939");
              RefreshData(true);
            });
      });
  dialog_panel_window_.Show(config, std::move(actions));
}

void MainWindow::OpenCreateTagDialog(std::int64_t clip_id, bool apply_filter_after_create,
                                     const std::wstring& retry_text, const std::wstring& retry_color) {
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
  config.initial_text = retry_text;
  config.initial_color = retry_color.empty() ? L"#3B82F6" : retry_color;
  config.color_options = BuildTagDialogColorOptions();

  auto actions = MakeDialogPanelHostActions(
      this,
      [this, clip_id, apply_filter_after_create](const DialogPanelWindow::Result& result) {
        if (!result.confirmed || library_mutator_ == nullptr) {
          return;
        }
        // P0-1:EnsureTag → (可选)AddTag 链式异步；tag_id 经 shared_ptr 带回。
        auto* tag_mutator = library_mutator_;
        const std::wstring tag_name = result.text;
        const std::wstring tag_color = result.color;
        auto tag_id = std::make_shared<std::int64_t>(0);
        const auto finish = [this, clip_id, apply_filter_after_create, tag_id]() {
          RefreshTags();
          if (apply_filter_after_create || clip_id == 0) {
            core::browser::BrowserCommand select_tag;
            select_tag.kind = core::browser::BrowserCommandKind::SelectTag;
            select_tag.tag_id = *tag_id;
            ApplyBrowserCommand(select_tag);
          }
          RefreshStatus(L"Created tag");
          RefreshData(true);
        };
        PostLibraryMutation(
            [tag_mutator, tag_name, tag_color, tag_id]() -> common::Status {
              const common::Status status = tag_mutator->EnsureTag(tag_name, tag_color, &*tag_id);
              if (status.ok() && *tag_id <= 0) {
                return common::Status::Error(common::StatusCode::InternalError, "ensure tag returned no id");
              }
              return status;
            },
            [this, clip_id, tag_mutator, tag_name, tag_color, tag_id, apply_filter_after_create, finish](const common::Status& status) {
              if (!status.ok() || *tag_id <= 0) {
                // 失败恢复：重开对话框预填名称与颜色（审查 P1-1）。
                RefreshStatus(L"\u65b0\u5efa\u6807\u7b7e\u5931\u8d25");
                OpenCreateTagDialog(clip_id, apply_filter_after_create, tag_name, tag_color);
                return;
              }
              if (clip_id > 0) {
                const std::int64_t created_tag_id = *tag_id;
                PostLibraryMutation(
                    [tag_mutator, clip_id, created_tag_id] { return tag_mutator->AddTag(clip_id, created_tag_id); },
                    [this, finish](const common::Status& add_status) {
                      if (!add_status.ok()) {
                        RefreshStatus(L"\u6dfb\u52a0\u6807\u7b7e\u5931\u8d25");
                        return;
                      }
                      finish();
                    });
                return;
              }
              finish();
            });
      });
  dialog_panel_window_.Show(config, std::move(actions));
}

void MainWindow::OpenRenameTagDialog(std::int64_t tag_id, const std::wstring& retry_text,
                                     const std::wstring& retry_color) {
  const auto it = std::find_if(state_.tags_.begin(), state_.tags_.end(), [tag_id](const core::store::TagRecord& tag) {
    return tag.id == tag_id;
  });
  if (it == state_.tags_.end()) {
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
  config.initial_text = retry_text.empty() ? it->name : retry_text;
  config.initial_color = retry_color.empty() ? it->color : retry_color;
  config.empty_text_error = L"\u6807\u7b7e\u540d\u79f0\u4e0d\u80fd\u4e3a\u7a7a\u3002";
  config.color_options = BuildTagDialogColorOptions();

  auto actions = MakeDialogPanelHostActions(
      this,
      [this, tag_id](const DialogPanelWindow::Result& result) {
        if (!result.confirmed || library_mutator_ == nullptr) {
          return;
        }
        auto* tag_mutator = library_mutator_;
        const std::wstring tag_name = result.text;
        const std::wstring tag_color = result.color;
        PostLibraryMutation(
            [tag_mutator, tag_id, tag_name, tag_color] { return tag_mutator->RenameTag(tag_id, tag_name, tag_color); },
            [this, tag_id, tag_name, tag_color](const common::Status& status) {
              if (!status.ok()) {
                // 失败恢复：重开对话框预填用户输入（审查 P1-1）。
                RefreshStatus(L"Rename tag failed");
                OpenRenameTagDialog(tag_id, tag_name, tag_color);
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
        if (!result.confirmed || library_mutator_ == nullptr) {
          return;
        }
        auto* group_mutator = library_mutator_;
        PostLibraryMutation(
            [group_mutator, group_id] { return group_mutator->DeleteGroup(group_id); },
            [this, group_id](const common::Status& status) {
              if (!status.ok()) {
                RefreshStatus(L"\u5220\u9664\u8d44\u6599\u5939\u5931\u8d25");
                return;
              }
              if (state_.current_group_id() == group_id) {
                ApplyBrowserCommand(core::browser::BrowserCommand{core::browser::BrowserCommandKind::SetScopeLibrary});
              }
              RefreshGroups();
              RefreshStatus(L"\u5df2\u5220\u9664\u8d44\u6599\u5939");
              RefreshData(true);
            });
      });
  dialog_panel_window_.Show(config, std::move(actions));
}

void MainWindow::OpenDeleteTagDialog(std::int64_t tag_id) {
  const auto it = std::find_if(state_.tags_.begin(), state_.tags_.end(), [tag_id](const core::store::TagRecord& tag) {
    return tag.id == tag_id;
  });
  if (it == state_.tags_.end()) {
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
        if (!result.confirmed || library_mutator_ == nullptr) {
          return;
        }
        auto* tag_mutator = library_mutator_;
        PostLibraryMutation(
            [tag_mutator, tag_id] { return tag_mutator->DeleteTag(tag_id); },
            [this](const common::Status& status) {
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
  if (library_mutator_ == nullptr || clip_id <= 0 || group_id <= 0) {
    return false;
  }
  auto* move_mutator = library_mutator_;
  PostLibraryMutation(
      [move_mutator, clip_id, group_id] { return move_mutator->MoveClipsToGroup({clip_id}, group_id); },
      [this, success_status](const common::Status& status) {
        if (!status.ok()) {
          return;
        }
        RefreshStatus(success_status);
        RefreshData(true);
      });
  return true;
}

bool MainWindow::MoveClipToGroupByIndex(std::int64_t clip_id, size_t group_index) {
  if (group_index >= state_.groups_.size()) {
    return false;
  }
  return MoveClipToGroupById(clip_id, state_.groups_[group_index].id, L"\u5df2\u79fb\u52a8\u5230\u8d44\u6599\u5939");
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
        if (!result.confirmed || library_mutator_ == nullptr) {
          return;
        }
        auto* group_mutator = library_mutator_;
        const std::wstring group_name = result.text;
        auto group_id = std::make_shared<std::int64_t>(0);
        PostLibraryMutation(
            [group_mutator, group_name, group_id]() -> common::Status {
              const common::Status status = group_mutator->EnsureGroup(group_name, &*group_id);
              if (status.ok() && *group_id <= 0) {
                return common::Status::Error(common::StatusCode::InternalError, "ensure group returned no id");
              }
              return status;
            },
            [this, clip_id, group_id](const common::Status& status) {
              if (!status.ok() || *group_id <= 0) {
                RefreshStatus(L"\u65b0\u5efa\u8d44\u6599\u5939\u5931\u8d25");
                return;
              }
              RefreshGroups();
              MoveClipToGroupById(clip_id, *group_id, L"\u5df2\u79fb\u52a8\u5230\u65b0\u8d44\u6599\u5939");
            });
      });
  dialog_panel_window_.Show(config, std::move(actions));
  return true;
}

bool MainWindow::PromptCreateTag(std::int64_t clip_id, bool apply_filter_after_create) {
  OpenCreateTagDialog(clip_id, apply_filter_after_create);
  return true;
}

void MainWindow::RenameCurrentTag() {
  if (library_mutator_ == nullptr || state_.current_tag_id() <= 0) {
    return;
  }
  OpenRenameTagDialog(state_.current_tag_id());
}

void MainWindow::DeleteCurrentTag() {
  if (library_mutator_ == nullptr || state_.current_tag_id() <= 0) {
    return;
  }
  OpenDeleteTagDialog(state_.current_tag_id());
}

}  // namespace clipd2d::ui
