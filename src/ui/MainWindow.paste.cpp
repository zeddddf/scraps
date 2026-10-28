#include "ui/MainWindowInternal.h"
#include "core/commands/IPasteService.h"
#include "core/commands/IMaintenance.h"
#include "core/clipboard/OpenTargetResolver.h"

// 粘贴链路：排队/提交粘贴请求、默认/纯文本/动作变换粘贴、队列推进。
namespace clipd2d::ui {

void MainWindow::PerformThirdPartySticker() {
  const auto* summary = GetSelectedSummary();
  if (summary == nullptr || paste_service_ == nullptr) {
    return;
  }

  if (!third_party_sticker_enabled_ || !third_party_sticker_hotkey_valid_) {
    RefreshStatus(L"\u8bf7\u5148\u5728\u8bbe\u7f6e\u4e2d\u542f\u7528\u5e76\u914d\u7f6e\u7b2c\u4e09\u65b9\u8d34\u56fe\u5feb\u6377\u952e");
    return;
  }
  if (!ShouldShowThirdPartyStickerButton(*summary)) {
    RefreshStatus(L"\u5f53\u524d\u6761\u76ee\u4e0d\u652f\u6301\u8d34\u56fe");
    return;
  }

  const common::Status copy_status = paste_service_->CopyClipForStickerById(summary->id);
  if (!copy_status.ok()) {
    RefreshStatus(L"\u51c6\u5907\u8d34\u56fe\u526a\u8d34\u677f\u5931\u8d25");
    return;
  }

  const common::Status hotkey_status = platform::win32::SendHotkeyInput(third_party_sticker_hotkey_);
  if (!hotkey_status.ok()) {
    RefreshStatus(L"\u89e6\u53d1\u7b2c\u4e09\u65b9\u8d34\u56fe\u5feb\u6377\u952e\u5931\u8d25");
    return;
  }
}

void MainWindow::QueuePasteRequest(PendingPasteKind kind, std::int64_t clip_id, std::string action_id) {
  if (paste_service_ == nullptr || clip_id <= 0) {
    return;
  }
  common::Stopwatch queue_stopwatch;
  pending_paste_request_.active = true;
  pending_paste_request_.kind = kind;
  pending_paste_request_.clip_id = clip_id;
  pending_paste_request_.action_id = std::move(action_id);
  pending_paste_request_.target_context =
      core::clipboard::IsValidPasteTargetContext(focus_coordinator_.paste_target_context())
          ? focus_coordinator_.paste_target_context()
          : CapturePasteTargetContext();
  pending_paste_request_.route = paste_route_requires_recovery_ ? core::clipboard::PasteRoute::Recovery
                                                                : core::clipboard::PasteRoute::Fast;
  const std::int64_t target_us = queue_stopwatch.ElapsedUs();
  common::WriteLog(
      common::LogLevel::Info,
      "ui.paste_target",
      "queue kind=" + std::to_string(static_cast<int>(kind)) +
          " clip_id=" + std::to_string(clip_id) +
          " route=" + std::to_string(static_cast<int>(pending_paste_request_.route)) +
          " stored_root=" + std::to_string(reinterpret_cast<std::uintptr_t>(pending_paste_request_.target_context.root_window)) +
          " stored_focus=" + std::to_string(reinterpret_cast<std::uintptr_t>(pending_paste_request_.target_context.focus_window)) +
          " target_capture_ms=" + common::FormatMs(target_us));

  if (!panel_locked_) {
    HideWindow();
  }
  const std::int64_t hide_us = queue_stopwatch.ElapsedUs() - target_us;
  PostMessageW(hwnd_, kCommitPasteMessage, 0, 0);
  if (hide_us >= 4000) {
    common::WriteLog(
        common::LogLevel::Info,
        "ui.paste_target",
        "hide elapsed_ms=" + common::FormatMs(hide_us));
  }
}

void MainWindow::CommitPendingPasteRequest() {
  if (!pending_paste_request_.active || paste_service_ == nullptr) {
    return;
  }
  common::Stopwatch commit_stopwatch;
  PendingPasteRequest request = std::move(pending_paste_request_);
  pending_paste_request_ = {};

  const HWND foreground = GetForegroundWindow();
  core::clipboard::PasteTargetContext current_external_context{};
  if (foreground != nullptr && !IsOwnedWindow(foreground)) {
    current_external_context = core::clipboard::CapturePasteTargetContext(foreground);
    if (core::clipboard::IsValidPasteTargetContext(current_external_context)) {
      focus_coordinator_.ObserveExternalWindow(
          foreground,
          [this](HWND hwnd) { return IsOwnedWindow(hwnd); },
          [this](HWND hwnd) { return IsMenuPopupWindow(hwnd); });
    }
  }

  core::clipboard::PasteTargetContext target_context{};
  core::clipboard::PasteRoute route = core::clipboard::PasteRoute::Fast;
  if (core::clipboard::IsValidPasteTargetContext(current_external_context)) {
    target_context = current_external_context;
  } else if (core::clipboard::IsValidPasteTargetContext(request.target_context)) {
    target_context = request.target_context;
    route = core::clipboard::PasteRoute::Recovery;
  } else {
    target_context = CapturePasteTargetContext();
    route = core::clipboard::PasteRoute::Recovery;
  }
  common::WriteLog(
      common::LogLevel::Info,
      "ui.paste_target",
      "commit kind=" + std::to_string(static_cast<int>(request.kind)) +
          " clip_id=" + std::to_string(request.clip_id) +
          " foreground=" + std::to_string(reinterpret_cast<std::uintptr_t>(foreground)) +
          " current_root=" + std::to_string(reinterpret_cast<std::uintptr_t>(current_external_context.root_window)) +
          " request_root=" + std::to_string(reinterpret_cast<std::uintptr_t>(request.target_context.root_window)) +
          " final_root=" + std::to_string(reinterpret_cast<std::uintptr_t>(target_context.root_window)) +
          " final_focus=" + std::to_string(reinterpret_cast<std::uintptr_t>(target_context.focus_window)) +
          " route=" + std::to_string(static_cast<int>(route)));

  // 异步提交：事务（WIC 解码/像素拷贝/前台恢复/修饰键等待，可达数百毫秒）在
  // AppController 的 paste worker 线程串行执行，UI 线程立即返回，面板隐藏后
  // 窗口不再无响应。失败经 kPasteFailedMessage 回抛。
  core::commands::QueuedPasteRequest queued;
  queued.clip_id = request.clip_id;
  queued.target_context = target_context;
  queued.route = route;
  switch (request.kind) {
    case PendingPasteKind::Default:
      queued.kind = core::commands::QueuedPasteKind::Default;
      break;
    case PendingPasteKind::PlainText:
      queued.kind = core::commands::QueuedPasteKind::PlainText;
      break;
    case PendingPasteKind::Action:
      queued.kind = core::commands::QueuedPasteKind::Action;
      queued.action_id = request.action_id;
      break;
  }

  if (!paste_service_->QueuePaste(std::move(queued))) {
    if (!panel_locked_ && !IsWindowVisible(hwnd_)) {
      ShowAndActivate();
    }
    RefreshStatus(L"\u7c98\u8d34\u5931\u8d25");
    return;
  }
  const std::int64_t commit_elapsed_us = commit_stopwatch.ElapsedUs();
  if (commit_elapsed_us >= 4000) {
    common::WriteLog(
        common::LogLevel::Info,
        "ui.paste_target",
        "commit enqueue elapsed_ms=" + common::FormatMs(commit_elapsed_us));
  }
}

core::clipboard::PasteTargetContext MainWindow::CapturePasteTargetContext() const {
  return focus_coordinator_.CapturePreferredPasteTarget(
      GetForegroundWindow(),
      [this](HWND hwnd) { return IsOwnedWindow(hwnd); },
      [this](HWND hwnd) { return IsMenuPopupWindow(hwnd); });
}

void MainWindow::PerformDefaultPaste() {
  const auto* summary = GetSelectedSummary();
  if (summary == nullptr || paste_service_ == nullptr) {
    return;
  }
  QueuePasteRequest(PendingPasteKind::Default, summary->id);
}

void MainWindow::PerformPlainTextPaste() {
  const auto* summary = GetSelectedSummary();
  if (summary == nullptr || paste_service_ == nullptr) {
    return;
  }
  QueuePasteRequest(PendingPasteKind::PlainText, summary->id);
}

void MainWindow::PerformActionById(const std::string& action_id) {
  const auto* summary = GetSelectedSummary();
  if (summary == nullptr || paste_service_ == nullptr) {
    return;
  }

  if (action_id == "paste-original") {
    QueuePasteRequest(PendingPasteKind::Default, summary->id);
  } else if (action_id == "paste-plain-text" || action_id == "paste-path") {
    QueuePasteRequest(PendingPasteKind::PlainText, summary->id);
  } else if (action_id == "open-target") {
    OpenSelectedResolvedTarget();
    return;
  } else if (action_id == "show-qr") {
    const core::clipboard::OpenTarget target =
        core::clipboard::ResolveOpenTargetFromText(state_.selected_clip_->plain_text);
    if (target.kind != core::clipboard::OpenTargetKind::Url || target.value.empty() || maintenance_ == nullptr) {
      RefreshStatus(L"Current item is not a QR-capable link");
      return;
    }
    std::wstring qr_path;
    const common::Status qr_status = maintenance_->GenerateQrCodeForUrl(target.value, &qr_path);
    if (!qr_status.ok()) {
      RefreshStatus(L"Generate QR code failed");
      return;
    }
    const common::Status preview_status = EnsurePreviewWindowCreated();
    if (!preview_status.ok()) {
      RefreshStatus(L"Open QR preview failed");
      return;
    }
    core::store::ClipRecord qr_clip;
    qr_clip.summary.content_type = core::store::ClipContentType::Image;
    qr_clip.summary.title = "Link QR";
    qr_clip.storage_path = qr_path;
    qr_clip.summary.storage_path = qr_path;
    qr_clip.plain_text = state_.selected_clip_->plain_text;
    PreviewWindow::HostActions host_actions;
    host_actions.on_show = [this]() {
      NotifyPreviewWindowShown();
    };
    host_actions.on_hide = [this]() {
      NotifyPreviewWindowHidden();
    };
    host_actions.enable_image_ocr = false;
    host_actions.image_copy_label = L"Copy QR";
    host_actions.on_copy_image = [qr_path]() {
      SuppressClipboardCapture();
      return core::clipboard::SetClipboardImageFromPng(qr_path);
    };
    preview_window_.ShowClip(qr_clip, std::move(host_actions));
    return;
  } else if (action_id == "open-default") {
    OpenSelectedWithDefaultApp();
    return;
  } else if (action_id == "copy-path") {
    CopySelectedPath();
    return;
  } else if (action_id == "open-folder") {
    OpenSelectedFolder();
    return;
  } else {
    QueuePasteRequest(PendingPasteKind::Action, summary->id, action_id);
  }
}

void MainWindow::PasteNextQueueItem() {
  if (paste_service_ == nullptr || state_.queue_count_ <= 0) {
    RefreshStatus(L"\u961f\u5217\u4e3a\u7a7a");
    return;
  }
  const core::clipboard::PasteTargetContext target_context =
      core::clipboard::IsValidPasteTargetContext(focus_coordinator_.paste_target_context())
          ? focus_coordinator_.paste_target_context()
          : CapturePasteTargetContext();
  const core::clipboard::PasteRoute route =
      paste_route_requires_recovery_ ? core::clipboard::PasteRoute::Recovery : core::clipboard::PasteRoute::Fast;
  if (!panel_locked_) {
    HideWindow();
  }
  const common::Status status = paste_service_->PasteNextInQueue(target_context, route);
  if (!status.ok()) {
    if (!panel_locked_ && !IsWindowVisible(hwnd_)) {
      ShowAndActivate();
    }
    RefreshStatus(L"\u961f\u5217\u7c98\u8d34\u5931\u8d25");
    return;
  }
  RefreshStatus(L"Pasted next queue item");
  RefreshData(true);
}

}  // namespace clipd2d::ui
