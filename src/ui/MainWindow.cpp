#include "ui/MainWindowInternal.h"
#include "app/StartupTrace.h"
#include <psapi.h>
#include "ui/ClipVisualAssets.h"
#include "ui/PanelWindowChrome.h"

namespace clipd2d::ui {

// L7:ShowFilterChipBarFlag 的唯一定义(单一 TU 具名实体)。声明见 MainWindowInternal.h;
// 匿名命名空间副本已移除——跨 TU 的可变状态必须只有一份,否则各 .cpp 各持一份副本。
bool& ShowFilterChipBarFlag() {
  static bool value = false;
  return value;
}

MainWindow::MainWindow() : history_data_controller_(*this), history_asset_loader_(*this), scroll_controller_(std::make_unique<ListScrollController>(this)) {}

namespace {
constexpr wchar_t kSearchClearTooltipText[] = L"\u6E05\u7A7A\u641C\u7D22";
constexpr wchar_t kSearchClearGlyph[] = L"\u00D7";
}

const wchar_t* MainWindow::WindowClassName() {
  return kMainWindowClassName;
}

UINT MainWindow::ShowWindowMessageId() {
  return kShowWindowMessage;
}

MainWindow::~MainWindow() {
  Destroy();
}

common::Status MainWindow::Create(
    HINSTANCE instance,
    int n_cmd_show,
    bool show_initial_panel_after_layout,
    core::commands::AppController* controller,
    OpenSettingsCallback open_settings_callback,
    OwnedWindowPredicate owned_window_predicate) {
  instance_ = instance;
  controller_ = controller;
  open_settings_callback_ = std::move(open_settings_callback);
  owned_window_predicate_ = std::move(owned_window_predicate);
  QueryPerformanceFrequency(&performance_counter_frequency_);

  WNDCLASSEXW window_class{};
  window_class.cbSize = sizeof(window_class);
  window_class.style = kPopupPanelClassStyle | CS_DBLCLKS;
  window_class.lpfnWndProc = &MainWindow::StaticWindowProc;
  window_class.hInstance = instance_;
  window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  window_class.hbrBackground = nullptr;
  window_class.lpszClassName = kMainWindowClassName;
  RegisterClassExW(&window_class);

  dummy_owner_ = CreateWindowExW(
      0, L"STATIC", L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance_, nullptr);

  hwnd_ = CreateWindowExW(
      kPopupPanelWindowExStyle,
      kMainWindowClassName,
      L"Scraps",
      kPopupPanelWindowStyle,
      CW_USEDEFAULT,
      CW_USEDEFAULT,
      window_width_,
      window_height_,
      dummy_owner_,
      nullptr,
      instance_,
      this);
  if (hwnd_ == nullptr) {
    return common::Status::Error(common::StatusCode::PlatformError, "Create main window failed.");
  }

  clipd2d::app::MarkStartupPhase("main_window_hwnd_ready");

  common::Status status = d2d_shell_.InitializeDComp(hwnd_);
  if (!status.ok()) {
    return status;
  }
  clipd2d::app::MarkStartupPhase("main_window_d2d_ready");
  status = CreateControls();
  if (!status.ok()) {
    return status;
  }
  clipd2d::app::MarkStartupPhase("main_window_controls_ready");
  LayoutControls();
  clipd2d::app::MarkStartupPhase("main_window_layout_ready");
  clipd2d::app::MarkStartupPhase("main_window_fast_refresh_started");
  pending_initial_show_ = n_cmd_show != SW_HIDE && show_initial_panel_after_layout;
  RefreshSettingsCache();
  RefreshData(false, RefreshDataMode::FastStartup);
  UpdateRoundedRegion();
  ShowScrollBar(hwnd_, SB_VERT, FALSE);

  if (!IsWindowVisible(hwnd_)) {
    ShowWindow(hwnd_, SW_HIDE);
  }
  clipd2d::app::MarkStartupPhase("main_window_create_complete");
  MaybeArmScrollBench();
  return common::Status::Ok();
}

common::Status MainWindow::EnsureHoverHintWindowCreated() {
  if (hover_hint_window_.hwnd() != nullptr) {
    return common::Status::Ok();
  }
  const common::Status status = hover_hint_window_.Create(instance_, hwnd_);
  if (!status.ok()) {
    common::WriteLog(common::LogLevel::Warning, "ui.hover_hint.create", status.message);
  }
  return status;
}

common::Status MainWindow::EnsureDialogPanelWindowCreated() {
  if (dialog_panel_window_.hwnd() != nullptr) {
    return common::Status::Ok();
  }
  const common::Status status = dialog_panel_window_.Create(instance_, hwnd_);
  if (!status.ok()) {
    common::WriteLog(common::LogLevel::Warning, "ui.dialog_panel.create", status.message);
  }
  return status;
}

void MainWindow::Destroy() {
  StopAutoHideHooks();
  if (hwnd_ != nullptr) {
    KillTimer(hwnd_, kHiddenCacheTrimTimerId);
    KillTimer(hwnd_, kHiddenWorkingSetTrimTimerId);
    KillTimer(hwnd_, kHiddenWarmupTimerId);
  }
  thread_pool_.Shutdown();
  DrainPendingOwnedMessages();
  hover_hint_window_.Destroy();
  dialog_panel_window_.Destroy();
  preview_window_.Destroy();
  if (tooltip_ != nullptr) {
    DestroyWindow(tooltip_);
    tooltip_ = nullptr;
  }
  preview_bitmap_.bitmap.Reset();
  thumbnail_bitmaps_.clear();
  text_layout_cache_.Clear();
  d2d_shell_.Shutdown();
  if (hwnd_ != nullptr) {
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
  if (dummy_owner_ != nullptr) {
    DestroyWindow(dummy_owner_);
    dummy_owner_ = nullptr;
  }
}

void MainWindow::DrainPendingOwnedMessages() {
  if (hwnd_ == nullptr) {
    return;
  }
  // Q1:窗口销毁前回收队列中尚未派发的 owned-message payload,封死"接收方漏处理
  // 即泄漏"的路径。thread_pool_.Shutdown() 之后工作线程不再投递;任意线程的
  // RequestRefreshFromAnyThread 仍有极窄的竞态窗口(见实施报告遗留风险)。
  MSG msg{};
  while (PeekMessageW(&msg, hwnd_, kOwnedMessageFirst, kOwnedMessageLast, PM_REMOVE)) {
    switch (msg.message) {
      case kRefreshWindowMessage:
        delete reinterpret_cast<core::commands::RefreshRequest*>(msg.lParam);
        break;
      case kCountReadyMessage:
        delete reinterpret_cast<CountReadyPayload*>(msg.lParam);
        break;
      case kPageReadyMessage:
        delete reinterpret_cast<PageReadyPayload*>(msg.lParam);
        break;
      case kClipReadyMessage:
        delete reinterpret_cast<ClipReadyPayload*>(msg.lParam);
        break;
      case kStartupMetadataReadyMessage:
        delete reinterpret_cast<StartupMetadataReadyPayload*>(msg.lParam);
        break;
      case kThumbnailReadyMessage:
      case kPreviewReadyMessage:
        delete reinterpret_cast<BitmapReadyPayload*>(msg.lParam);
        break;
      case kHandleExternalPointerMessage:
        delete reinterpret_cast<ExternalPointerMessagePayload*>(msg.lParam);
        break;
      default:
        // 其余窗口消息不携带堆 payload;窗口即将销毁,直接丢弃。
        break;
    }
  }
}

void MainWindow::RequestShowFromAnyThread() const {
  if (hwnd_ != nullptr) {
    PostMessageW(hwnd_, kShowWindowMessage, 0, 0);
  }
}

void MainWindow::RequestToggleFromAnyThread() const {
  if (hwnd_ != nullptr) {
    PostMessageW(hwnd_, kToggleWindowMessage, 0, 0);
  }
}

void MainWindow::RequestRefreshFromAnyThread(core::commands::RefreshRequest request) const {
  if (hwnd_ == nullptr) {
    return;
  }
  // Q1:payload 所有权随消息交接(接收方 TakeOwnedMessagePayload 析构),
  // 投递失败由 PostOwnedMessage 回收,不再裸 new 泄漏。
  (void)PostOwnedMessage(
      hwnd_,
      kRefreshWindowMessage,
      0,
      std::make_unique<core::commands::RefreshRequest>(std::move(request)));
}

void MainWindow::RequestRefreshPreserveViewport() const {
  RequestRefreshFromAnyThread(core::commands::MakePreserveViewportRefreshRequest());
}

void MainWindow::RequestRefreshForNewestClip(std::int64_t clip_id) const {
  RequestRefreshFromAnyThread(core::commands::MakeJumpToTopRefreshRequest(clip_id));
}

LRESULT CALLBACK MainWindow::StaticWindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
  MainWindow* self = nullptr;
  if (message == WM_NCCREATE) {
    auto* create_struct = reinterpret_cast<CREATESTRUCTW*>(l_param);
    self = static_cast<MainWindow*>(create_struct->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  } else {
    self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  }
  if (self != nullptr) {
    self->hwnd_ = hwnd;
    return self->WindowProc(message, w_param, l_param);
  }
  return DefWindowProcW(hwnd, message, w_param, l_param);
}

common::Status MainWindow::CreateControls() {

  if (!search_textbox_.Initialize(d2d_shell_.dwrite_factory())) {
    return common::Status::Error(common::StatusCode::PlatformError, "Initialize search_textbox failed.");
  }
  search_textbox_.SetDrawBorder(false);
  search_textbox_.SetDrawKnob(false);
  search_textbox_.SetDrawCloseButton(false);
  search_textbox_.SetFontSize(12.0f);
  search_textbox_.SetTextColor(D2D1::ColorF(0x23262D));
  search_textbox_.SetBorderColor(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
  search_textbox_.SetPlaceholder(L"搜索记录或操作 (Ctrl + F)...");
  search_textbox_.SetPlaceholderColor(D2D1::ColorF(0x9CA3AF));
  search_textbox_.SetOnTextChanged([this](const std::wstring& text) {
    core::browser::BrowserCommand command;
    command.kind = core::browser::BrowserCommandKind::SetSearchText;
    command.search_text = text;
    ApplyBrowserCommand(command);
    LayoutControls();
    ShowFilterChipBarFlag() = !browser_service_.BuildPresentation(BuildBrowserState()).chips.empty();
    RefreshData(false);
  });
  search_textbox_.SetOnEnterPressed([this]() {
    PerformDefaultPaste();
  });

  if (!overlay_textbox_.Initialize(d2d_shell_.dwrite_factory())) {
    return common::Status::Error(common::StatusCode::PlatformError, "Initialize overlay_textbox failed.");
  }
  overlay_textbox_.SetDrawBorder(false);
  overlay_textbox_.SetDrawKnob(false);
  overlay_textbox_.SetDrawCloseButton(false);
  overlay_textbox_.SetFontSize(12.0f);
  overlay_textbox_.SetTextColor(D2D1::ColorF(0x23262D));
  overlay_textbox_.SetBorderColor(D2D1::ColorF(0.3f, 0.3f, 0.3f, 0.0f));
  overlay_textbox_.SetPlaceholderColor(D2D1::ColorF(0x9CA3AF));
  overlay_textbox_.SetOnTextChanged([this](const std::wstring& text) {
    overlay_dialog_.input_text = text;
    ClearOverlayDialogError();
  });
  overlay_textbox_.SetOnEnterPressed([this]() {
    CommitOverlayDialog();
  });

  return common::Status::Ok();
}

common::Status MainWindow::EnsurePreviewWindowCreated() {
  if (preview_window_.hwnd() != nullptr) {
    return common::Status::Ok();
  }
  if (instance_ == nullptr || hwnd_ == nullptr) {
    return common::Status::Error(common::StatusCode::Unavailable, "Preview window is not available yet.");
  }
  return preview_window_.Create(instance_, hwnd_);
}

PreviewWindow::HostActions MainWindow::BuildPreviewHostActions() {
  PreviewWindow::HostActions actions;
  actions.on_request_ocr = [this](std::int64_t clip_id, bool force_rerun) {
    if (controller_ == nullptr) {
      return common::Status::Error(common::StatusCode::Unavailable, "OCR controller is unavailable.");
    }
    return controller_->RequestImageOcr(clip_id, force_rerun);
  };
  actions.on_copy_ocr_text = [this](std::int64_t clip_id) {
    if (controller_ == nullptr) {
      return common::Status::Error(common::StatusCode::Unavailable, "OCR controller is unavailable.");
    }
    return controller_->CopyOcrTextById(clip_id);
  };
  actions.on_paste_ocr_text = [this](std::int64_t clip_id) {
    if (controller_ == nullptr) {
      return common::Status::Error(common::StatusCode::Unavailable, "OCR controller is unavailable.");
    }
    const core::clipboard::PasteTargetContext target_context = CapturePasteTargetContext();
    const core::clipboard::PasteRoute route = paste_route_requires_recovery_
        ? core::clipboard::PasteRoute::Recovery
        : core::clipboard::PasteRoute::Fast;
    return controller_->PasteOcrTextById(clip_id, target_context, route);
  };
  actions.query_ocr_provider_status = [this](std::wstring* message) {
    if (controller_ == nullptr) {
      if (message != nullptr) {
        *message = L"OCR controller is unavailable.";
      }
      return common::Status::Error(common::StatusCode::Unavailable, "OCR controller is unavailable.");
    }
    return controller_->GetImageOcrProviderStatus(message);
  };
  actions.on_show = [this]() {
    NotifyPreviewWindowShown();
  };
  actions.on_hide = [this]() {
    NotifyPreviewWindowHidden();
  };
  actions.enable_image_ocr = true;
  return actions;
}

int MainWindow::GetFileThumbnailTargetExtent() const {
  return visual_assets::ResolveFileThumbnailDecodeExtent(std::clamp(GetItemHeight() - 46, 28, 42));
}

void MainWindow::LayoutControls() {
  if (hwnd_ == nullptr) {
    return;
  }

  RECT client{};
  GetClientRect(hwnd_, &client);
  const PanelLayout layout = ComputeLayout(client.right, client.bottom);
  const bool has_search_text = !search_textbox_.GetText().empty();
  const int search_edit_left = layout.search_box.left + 28;
  const int search_edit_right = SearchTextRightEdge(layout, has_search_text);

  search_textbox_.SetRect(D2D1::RectF(
      static_cast<float>(search_edit_left),
      static_cast<float>(layout.search_box.top + 4),
      static_cast<float>(search_edit_right),
      static_cast<float>(layout.search_box.bottom - 4)
  ));

  const RECT search_clear_rect = SearchClearButtonRect(layout);
  if (tooltip_ != nullptr) {
    for (int i = 0; i < static_cast<int>(std::size(layout.toolbar)); ++i) {
      TOOLINFOW tool{};
      tool.cbSize = sizeof(tool);
      tool.hwnd = hwnd_;
      tool.uId = kToolbarTooltipBase + i;
      tool.rect = layout.toolbar[i];
      SendMessageW(tooltip_, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&tool));
    }
    for (int i = 0; i < static_cast<int>(std::size(kVisibleTabs)); ++i) {
      TOOLINFOW tool{};
      tool.cbSize = sizeof(tool);
      tool.hwnd = hwnd_;
      tool.uId = kTabTooltipBase + i;
      tool.rect = layout.tabs[i];
      SendMessageW(tooltip_, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&tool));
    }
    for (int i = 0; i < static_cast<int>(std::size(layout.nav_actions)); ++i) {
      TOOLINFOW tool{};
      tool.cbSize = sizeof(tool);
      tool.hwnd = hwnd_;
      tool.uId = kTabTooltipBase + static_cast<UINT_PTR>(std::size(kVisibleTabs)) + i;
      tool.rect = layout.nav_actions[i];
      SendMessageW(tooltip_, TTM_NEWTOOLRECTW, 0, reinterpret_cast<LPARAM>(&tool));
    }
  }
  UpdateOverlayDialogLayout();
  UpdateScrollBar();
}

void MainWindow::UpdateRoundedRegion() {
  ApplyPopupPanelWindowChrome(hwnd_, kPopupPanelLightBorderColor);
}

void MainWindow::SaveWindowPlacementIfNeeded(bool include_position) {
  if (controller_ == nullptr || hwnd_ == nullptr) {
    return;
  }
  RECT window_rect{};
  if (!GetWindowRect(hwnd_, &window_rect)) {
    return;
  }

  core::store::AppSettings settings = controller_->current_settings();
  settings.saved_window_width = std::max(kMinWindowWidth, static_cast<int>(window_rect.right - window_rect.left));
  settings.saved_window_height = std::max(kMinWindowHeight, static_cast<int>(window_rect.bottom - window_rect.top));
  settings.has_saved_window_size = true;
  if (include_position && settings.window_position_mode == core::store::WindowPositionMode::Remember) {
    settings.saved_window_x = window_rect.left;
    settings.saved_window_y = window_rect.top;
    settings.has_saved_window_position = true;
  }
  controller_->SaveSettings(settings);
}

POINT MainWindow::ComputeFollowMouseOrigin(int width, int height) const {
  POINT cursor{};
  GetCursorPos(&cursor);
  MONITORINFO monitor_info{};
  monitor_info.cbSize = sizeof(monitor_info);
  const HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
  GetMonitorInfoW(monitor, &monitor_info);

  const RECT work = monitor_info.rcWork;
  constexpr int kCursorGap = 16;
  int x = cursor.x + kCursorGap;
  int y = cursor.y + kCursorGap;
  if (x + width > work.right) {
    x = cursor.x - width - kCursorGap;
  }
  if (y + height > work.bottom) {
    y = cursor.y - height - kCursorGap;
  }
  x = std::clamp(x, static_cast<int>(work.left), static_cast<int>(work.right - width));
  y = std::clamp(y, static_cast<int>(work.top), static_cast<int>(work.bottom - height));
  return POINT{x, y};
}

RECT MainWindow::ScrollbarHitRect(const RECT& track_rect, bool expanded) const {
  RECT hit_rect = track_rect;
  hit_rect.left -= 6;
  if (expanded) {
    hit_rect.right += 1;
  }
  return hit_rect;
}

void MainWindow::ShowAndActivate(bool refresh_data) {
  CancelDeferredHiddenCacheTrim();
  CancelDeferredHiddenWorkingSetTrim();
  CancelDeferredHiddenWarmup();
  common::WriteLog(
      common::LogLevel::Info,
      "ui.visibility",
      "ShowAndActivate begin refresh_data=" + std::string(refresh_data ? "true" : "false") +
          " visible=" + std::string(IsWindowVisible(hwnd_) ? "true" : "false") +
          " panel_locked=" + std::string(panel_locked_ ? "true" : "false"));
  const core::store::AppSettings settings = controller_ != nullptr ? controller_->current_settings() : core::store::AppSettings{};
  RefreshSettingsCache();
  platform::win32::HotkeySpec local_hotkey{};
  if (platform::win32::ParseLocalHotkeyText(settings.search_focus_hotkey, &local_hotkey).ok()) {
    search_focus_hotkey_ = local_hotkey;
  } else {
    platform::win32::ParseLocalHotkeyText(L"Tab", &search_focus_hotkey_);
  }
  search_focus_requested_ = false;
  pressed_static_hit_.reset();
  scroll_controller_->GetManualScrollDirection() = 0;
  hover_point_ = POINT{-1, -1};
  mouse_tracking_ = false;
  detail::ClearTransientInteractionState(&interaction_guard_state_);
  paste_route_requires_recovery_ = false;
  focus_coordinator_.set_paste_target_context({});
  SetPanelAllowsActivation(false);
  HWND foreground = GetForegroundWindow();
  if (foreground != nullptr && !IsOwnedWindow(foreground)) {
    UpdateLastExternalFocusContextFromWindow(foreground);
  }
  PrepareWindowPlacementForShow();
  if (refresh_data) {
    pending_show_after_refresh_ = true;
    RefreshData(true);
    common::WriteLog(
        common::LogLevel::Info,
        "ui.visibility",
        "ShowAndActivate deferred until refreshed frame is ready");
    return;
  }
  PresentPreparedWindow(settings.focus_search_on_open);
}

void MainWindow::PrepareWindowPlacementForShow() {
  const core::store::AppSettings settings = controller_ != nullptr ? controller_->current_settings() : core::store::AppSettings{};
  window_width_ = settings.has_saved_window_size ? std::max(kMinWindowWidth, settings.saved_window_width) : kDefaultWindowWidth;
  window_height_ = settings.has_saved_window_size ? std::max(kMinWindowHeight, settings.saved_window_height) : kDefaultWindowHeight;
  if (settings.window_position_mode == core::store::WindowPositionMode::Remember && settings.has_saved_window_position) {
    window_x_ = settings.saved_window_x;
    window_y_ = settings.saved_window_y;
    has_window_position_ = true;
  } else if (settings.window_position_mode == core::store::WindowPositionMode::FollowMouse) {
    const POINT origin = ComputeFollowMouseOrigin(window_width_, window_height_);
    window_x_ = origin.x;
    window_y_ = origin.y;
    has_window_position_ = true;
  } else if (!has_window_position_) {
    RECT work_area{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0);
    window_x_ = work_area.left + ((work_area.right - work_area.left) - window_width_) / 2;
      window_y_ = work_area.top + 36;
      has_window_position_ = true;
  }
  frame_interval_ms_ = std::max<UINT>(4, static_cast<UINT>(std::lround(1000.0 / DetectRefreshRateHz(hwnd_))));
  SetWindowPos(
      hwnd_,
      HWND_TOPMOST,
      window_x_,
      window_y_,
      window_width_,
      window_height_,
      SWP_NOACTIVATE);
}

void MainWindow::PresentPreparedWindow(bool focus_search_on_open, bool suppress_list_cache) {
  if (hwnd_ == nullptr) {
    return;
  }

  suppress_list_cache_during_present_ = suppress_list_cache;
  (void)d2d_shell_.EnsureSecondaryTextFormats();
  RenderFrame();
  suppress_list_cache_during_present_ = false;
  ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
  UpdateWindow(hwnd_);
  PostMessageW(hwnd_, kInstallAutoHideHooksMessage, 0, 0);
  if (!startup_metadata_warmup_requested_) {
    startup_metadata_warmup_requested_ = true;
    RequestStartupMetadataAsync();
  }
  if (focus_search_on_open) {
    PostMessageW(hwnd_, kFocusSearchMessage, 1, 0);
  }
  common::WriteLog(
      common::LogLevel::Info,
      "ui.visibility",
      "ShowAndActivate end visible=" + std::string(IsWindowVisible(hwnd_) ? "true" : "false") +
          " panel_allows_activation=" + std::string(panel_allows_activation_ ? "true" : "false"));
}

void MainWindow::RefreshSettingsCache() {
  const core::store::AppSettings settings = controller_ != nullptr ? controller_->current_settings() : core::store::AppSettings{};
  third_party_sticker_enabled_ = settings.third_party_sticker_enabled;
  third_party_sticker_hotkey_valid_ = false;
  third_party_sticker_hotkey_ = {};
  if (third_party_sticker_enabled_) {
    platform::win32::HotkeySpec hotkey;
    if (platform::win32::ParseLocalHotkeyText(common::TrimWide(settings.third_party_sticker_hotkey), &hotkey).ok()) {
      third_party_sticker_hotkey_ = hotkey;
      third_party_sticker_hotkey_valid_ = true;
    }
  }
  if (hwnd_ != nullptr) {
    PostMessageW(hwnd_, kApplyWindowBlurMessage, static_cast<WPARAM>(settings.window_blur_type), 0);
  }
}

bool MainWindow::ShouldShowThirdPartyStickerButton(const core::store::ClipSummary& summary) const {
  if (!third_party_sticker_enabled_ || !third_party_sticker_hotkey_valid_) {
    return false;
  }
  if (summary.content_type == core::store::ClipContentType::Image) {
    return true;
  }
  if (summary.content_type != core::store::ClipContentType::Files || summary.file_count != 1) {
    return false;
  }
  return IsImageFilePath(FirstFilePathFromSummary(summary));
}

void MainWindow::HideWindow() {
  common::WriteLog(
      common::LogLevel::Info,
      "ui.visibility",
      "HideWindow begin visible=" + std::string(IsWindowVisible(hwnd_) ? "true" : "false") +
          " panel_locked=" + std::string(panel_locked_ ? "true" : "false"));
  StopAutoHideHooks();
  SetPanelAllowsActivation(false);
  HideHoverTooltip();
  SaveWindowPlacementIfNeeded(true);
  pressed_static_hit_.reset();
  hover_point_ = POINT{-1, -1};
  mouse_tracking_ = false;
  scroll_controller_->StopInertialScroll(false);
  scroll_controller_->SetScrollAnimationActive(false);
  last_animation_tick_ms_ = 0;
  last_render_tick_qpc_ = 0;
  KillTimer(hwnd_, kAnimationTimerId);
  animation_timer_active_ = false;
  detail::ClearTransientInteractionState(&interaction_guard_state_);
  focus_coordinator_.ResetForHide();
  EndMenu();
  ShowWindow(hwnd_, SW_HIDE);
  ScheduleDeferredHiddenCacheTrim();
  ScheduleDeferredHiddenWorkingSetTrim();
  common::WriteLog(common::LogLevel::Info, "ui.visibility", "HideWindow end");
}

void MainWindow::ToggleWindow() {
  if (hwnd_ == nullptr) {
    return;
  }
  if (IsWindowVisible(hwnd_)) {
    HideWindow();
    return;
  }
  ShowAndActivate();
}

void MainWindow::FocusSearchBox(bool select_all) {
  if (hwnd_ == nullptr) {
    return;
  }
  CaptureExternalFocusRestoreContext();
  search_focus_requested_ = true;
  paste_route_requires_recovery_ = true;
  SetPanelAllowsActivation(true);
  ShowWindow(hwnd_, SW_SHOWNORMAL);
  SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
  SetForegroundWindow(hwnd_);
  SetActiveWindow(hwnd_);
  SetFocus(hwnd_);
  search_textbox_.SetFocused(true, hwnd_);
  if (select_all) {
    search_textbox_.SelectAll();
  }
  UpdateAutoHideHooks();
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::ClearSearch() {
  search_textbox_.SetText(L"");
  LayoutControls();
  ApplyBrowserCommand(core::browser::BrowserCommand{
      core::browser::BrowserCommandKind::ClearSearchText});
  ShowFilterChipBarFlag() = !browser_service_.BuildPresentation(BuildBrowserState()).chips.empty();
  RefreshData(false);
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::CaptureExternalFocusRestoreContext() {
  focus_coordinator_.CaptureRestoreContext(
      GetForegroundWindow(),
      [this](HWND hwnd) { return IsOwnedWindow(hwnd); },
      [this](HWND hwnd) { return IsMenuPopupWindow(hwnd); });
}

void MainWindow::RestoreExternalFocusRestoreContext() {
  (void)focus_coordinator_.RestoreCapturedContext(
      [this](HWND hwnd) { return IsOwnedWindow(hwnd); },
      [this](HWND hwnd) { return IsMenuPopupWindow(hwnd); });
}

std::optional<core::clipboard::PasteTargetContext> MainWindow::BuildExternalFocusRestoreContext() const {
  return focus_coordinator_.BuildRestoreContext();
}

bool MainWindow::RestoreExternalFocusContext(const core::clipboard::PasteTargetContext& context) {
  return focus_coordinator_.RestoreExplicitContext(
      context,
      [this](HWND hwnd) { return IsOwnedWindow(hwnd); },
      [this](HWND hwnd) { return IsMenuPopupWindow(hwnd); });
}

void MainWindow::UpdateLastExternalFocusContextFromWindow(HWND window) {
  focus_coordinator_.ObserveExternalWindow(
      window,
      [this](HWND hwnd) { return IsOwnedWindow(hwnd); },
      [this](HWND hwnd) { return IsMenuPopupWindow(hwnd); });
}

bool MainWindow::IsOverlayDialogVisible() const {
  return overlay_dialog_.visible;
}

void MainWindow::UpdateOverlayDialogLayout() {
  if (hwnd_ == nullptr || !overlay_dialog_.visible) {
    return;
  }

  RECT client{};
  GetClientRect(hwnd_, &client);
  const int client_width = std::max(1L, client.right - client.left);
  const int client_height = std::max(1L, client.bottom - client.top);
  const int card_width = 380;
  const int card_height = overlay_dialog_.has_color_options ? 320 : 220;
  const int card_left = (client_width - card_width) / 2;
  const int card_top = (client_height - card_height) / 2;
  overlay_dialog_.card_rect = RECT{card_left, card_top, card_left + card_width, card_top + card_height};
  overlay_dialog_.message_rect = RECT{
      card_left + 18,
      card_top + 52,
      card_left + card_width - 18,
      overlay_dialog_.has_text_input ? card_top + 74 : card_top + card_height - 72};
  overlay_dialog_.input_rect = RECT{
      card_left + 18,
      card_top + 86,
      card_left + card_width - 18,
      card_top + 116};
  overlay_dialog_.error_rect = RECT{
      overlay_dialog_.input_rect.left,
      overlay_dialog_.input_rect.bottom + 6,
      overlay_dialog_.input_rect.right,
      overlay_dialog_.input_rect.bottom + 24};
  overlay_dialog_.confirm_rect = RECT{
      card_left + card_width - 18 - 86 * 2 - 10,
      card_top + card_height - 18 - 32,
      card_left + card_width - 18 - 86 - 10,
      card_top + card_height - 18};
  overlay_dialog_.cancel_rect = RECT{
      card_left + card_width - 18 - 86,
      card_top + card_height - 18 - 32,
      card_left + card_width - 18,
      card_top + card_height - 18};
  overlay_dialog_.color_rects.clear();
  if (overlay_dialog_.has_color_options) {
    int left = overlay_dialog_.input_rect.left;
    int top = overlay_dialog_.error_rect.bottom + 14;
    constexpr int chip_width = 72;
    constexpr int chip_height = 26;
    constexpr int chip_gap = 8;
    for (size_t index = 0; index < overlay_dialog_.color_options.size(); ++index) {
      overlay_dialog_.color_rects.push_back(RECT{left, top, left + chip_width, top + chip_height});
      left += chip_width + chip_gap;
      if (left + chip_width > overlay_dialog_.card_rect.right - 18) {
        left = overlay_dialog_.input_rect.left;
        top += chip_height + chip_gap;
      }
    }
  }

  if (overlay_dialog_.has_text_input) {
    overlay_textbox_.SetRect(D2D1::RectF(
        static_cast<float>(overlay_dialog_.input_rect.left + 8),
        static_cast<float>(overlay_dialog_.input_rect.top + 4),
        static_cast<float>(overlay_dialog_.input_rect.right - 8),
        static_cast<float>(overlay_dialog_.input_rect.bottom - 4)
    ));
  }
}

void MainWindow::OpenOverlayDialog(OverlayDialogState state) {
  if (!state.visible) {
    state.visible = true;
  }
  if (!IsOverlayDialogVisible()) {
    CaptureExternalFocusRestoreContext();
    state.restore_external_focus = true;
  } else {
    state.restore_external_focus = overlay_dialog_.restore_external_focus;
  }
  overlay_dialog_ = std::move(state);
  SetPanelAllowsActivation(true);
  ShowWindow(hwnd_, SW_SHOWNORMAL);
  SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
  SetForegroundWindow(hwnd_);
  SetActiveWindow(hwnd_);
  overlay_textbox_.SetText(overlay_dialog_.input_text);
  UpdateOverlayDialogLayout();
  if (overlay_dialog_.has_text_input) {
    FocusOverlayDialogInput(overlay_dialog_.select_all_on_open);
  }
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::CloseOverlayDialog(bool restore_focus) {
  const bool should_restore = overlay_dialog_.restore_external_focus && restore_focus;
  overlay_dialog_ = OverlayDialogState{};
  overlay_textbox_.SetFocused(false, hwnd_);
  overlay_textbox_.Reset();
  SetPanelAllowsActivation(false);
  if (should_restore) {
    RestoreExternalFocusRestoreContext();
  }
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::FocusOverlayDialogInput(bool select_all) {
  if (!overlay_dialog_.visible || !overlay_dialog_.has_text_input) {
    return;
  }
  SetPanelAllowsActivation(true);
  SetForegroundWindow(hwnd_);
  SetActiveWindow(hwnd_);
  SetFocus(hwnd_);
  overlay_textbox_.SetFocused(true, hwnd_);
  if (select_all) {
    overlay_textbox_.SelectAll();
  }
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::SyncOverlayDialogInputText() {
  if (!overlay_dialog_.visible || !overlay_dialog_.has_text_input) {
    return;
  }
  overlay_dialog_.input_text = overlay_textbox_.GetText();
}

void MainWindow::ClearOverlayDialogError() {
  if (!overlay_dialog_.error_text.empty()) {
    overlay_dialog_.error_text.clear();
    InvalidateRect(hwnd_, nullptr, FALSE);
  }
}

bool MainWindow::IsSearchFocusHotkey(UINT virtual_key, UINT modifiers) const {
  return platform::win32::HotkeyMatches(virtual_key, modifiers, search_focus_hotkey_);
}

void MainWindow::HideHoverTooltip() {
  if (hover_hint_window_.hwnd() != nullptr) {
    hover_hint_window_.Hide();
  }
}

void MainWindow::UpdateHoverTooltip(POINT point) {
  std::wstring text;
  if (const auto static_hit = ResolveStaticHit(point); static_hit.has_value()) {
    switch (static_hit->kind) {
      case HitKind::SearchClear: text = strings::kClearSearch; break;
      case HitKind::ToolbarButton:
        switch (static_cast<ToolbarCommand>(static_hit->index)) {
          case ToolbarCommand::PanelLock: text = strings::kToolbarPanelLock; break;
          case ToolbarCommand::Settings: text = strings::kToolbarSettings; break;
          case ToolbarCommand::More: text = strings::kToolbarMore; break;
        }
        break;
      case HitKind::Tab:
        switch (static_hit->index) {
          case 0: text = strings::kTabHistory; break;
          case 1: text = strings::kTabFavorites; break;
          case 2: text = L"\u8d44\u6599\u5e93"; break;
        }
        break;
      case HitKind::NavButton:
        text = static_hit->index == 0 ? strings::kToolbarQueue : strings::kToolbarFilter;
        break;
      case HitKind::ClearFilters:
        text = L"\u6e05\u9664\u5168\u90e8";
        break;
      case HitKind::BottomButton: text = L"\u6458\u8981"; break;
      case HitKind::BackToTop: text = strings::kBackToTop; break;
      default: break;
    }
  } else {
    for (auto it = hit_regions_.rbegin(); it != hit_regions_.rend() && text.empty(); ++it) {
      if (!RectContains(it->rect, point)) {
        continue;
      }
      switch (it->kind) {
        case HitKind::ItemSticker: text = L"\u8d34\u56fe"; break;
        case HitKind::ItemFavorite: text = L"\u6536\u85cf"; break;
        case HitKind::ItemPinned: text = L"\u7f6e\u9876"; break;
        case HitKind::ItemDelete: text = L"\u5220\u9664"; break;
        case HitKind::FilterChip: text = L"\u6e05\u9664\u8be5\u6761\u4ef6"; break;
        default: break;
      }
    }
  }
  if (text.empty()) {
    HideHoverTooltip();
    return;
  }
  POINT screen_point = point;
  ClientToScreen(hwnd_, &screen_point);
  if (EnsureHoverHintWindowCreated().ok()) {
    hover_hint_window_.ShowText(std::move(text), screen_point);
  }
}

void MainWindow::SetPanelAllowsActivation(bool allow_activation) {
  if (hwnd_ == nullptr || panel_allows_activation_ == allow_activation) {
    return;
  }

  LONG_PTR ex_style = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
  if (allow_activation) {
    ex_style &= ~static_cast<LONG_PTR>(WS_EX_NOACTIVATE);
  } else {
    ex_style |= static_cast<LONG_PTR>(WS_EX_NOACTIVATE);
  }
  SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, ex_style);
  SetWindowPos(
      hwnd_,
      nullptr,
      0,
      0,
      0,
      0,
      SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED |
          (allow_activation ? 0 : SWP_NOACTIVATE));
  panel_allows_activation_ = allow_activation;
}

bool MainWindow::IsOwnedWindow(HWND hwnd) const {
  if (hwnd == nullptr) {
    return false;
  }
  const HWND root = GetAncestor(hwnd, GA_ROOT);
  if (root == hwnd_ || root == preview_window_.hwnd() || root == hover_hint_window_.hwnd() || root == dialog_panel_window_.hwnd()) {
    return true;
  }
  if (GetWindow(root, GW_OWNER) == hwnd_) {
    return true;
  }
  return owned_window_predicate_ ? owned_window_predicate_(root) : false;
}

void MainWindow::StartDrawerAnimation(bool open) {
  (void)open;
}

bool MainWindow::PointInInteractiveRegion(POINT point) const {
  if (hwnd_ == nullptr) {
    return false;
  }
  RECT client{};
  GetClientRect(hwnd_, &client);
  const PanelLayout layout = ComputeLayout(client.right, client.bottom);
  if (IsOverlayDialogVisible()) {
    if (RectContains(overlay_dialog_.card_rect, point)) {
      return true;
    }
  }
  if (RectContains(layout.list, point) || RectContains(layout.bottom_bar, point) || RectContains(layout.chips_bar, point) || RectContains(layout.search_box, point)) {
    return true;
  }
  for (const auto& rect : layout.toolbar) {
    if (RectContains(rect, point)) {
      return true;
    }
  }
  for (const auto& rect : layout.tabs) {
    if (RectContains(rect, point)) {
      return true;
    }
  }
  for (const auto& rect : layout.nav_actions) {
    if (RectContains(rect, point)) {
      return true;
    }
  }
  return false;
}

LRESULT MainWindow::HitTestNonClient(POINT screen_point) const {
  if (hwnd_ == nullptr) {
    return HTCLIENT;
  }

  RECT window_rect{};
  GetWindowRect(hwnd_, &window_rect);
  const LONG width = window_rect.right - window_rect.left;
  const LONG height = window_rect.bottom - window_rect.top;
  const LONG x = screen_point.x - window_rect.left;
  const LONG y = screen_point.y - window_rect.top;

  POINT client_point = screen_point;
  ScreenToClient(hwnd_, &client_point);
  RECT client{};
  GetClientRect(hwnd_, &client);
  const PanelLayout layout = ComputeLayout(client.right, client.bottom);
  if (RectContains(layout.list, client_point) &&
      RectContains(ScrollbarHitRect(layout.scrollbar_track, true), client_point)) {
    return HTCLIENT;
  }

  const bool left = x < kResizeBorder;
  const bool right = x >= width - kResizeBorder;
  const bool top = y < kResizeBorder;
  const bool bottom = y >= height - kResizeBorder;
  if (top && left) return HTTOPLEFT;
  if (top && right) return HTTOPRIGHT;
  if (bottom && left) return HTBOTTOMLEFT;
  if (bottom && right) return HTBOTTOMRIGHT;
  if (left) return HTLEFT;
  if (right) return HTRIGHT;
  if (top) return HTTOP;
  if (bottom) return HTBOTTOM;

  if (PointInInteractiveRegion(client_point)) {
    return HTCLIENT;
  }

  RECT draggable_top{0, layout.header.top, client.right, layout.list.top};
  if (RectContains(draggable_top, client_point)) {
    return HTCAPTION;
  }
  return HTCLIENT;
}


LRESULT MainWindow::WindowProc(UINT message, WPARAM w_param, LPARAM l_param) {
  switch (message) {
    case WM_NCCALCSIZE:
      return 0;

    case WM_SIZE:
      window_width_ = std::max(kMinWindowWidth, static_cast<int>(LOWORD(l_param)));
      window_height_ = std::max(kMinWindowHeight, static_cast<int>(HIWORD(l_param)));
      if (d2d_shell_.hwnd() != nullptr) {
        d2d_shell_.Resize(LOWORD(l_param), HIWORD(l_param));
      }
      dirty_rows_.clear();
      LayoutControls();
      {
        // Width changes re-wrap row text — re-measure row heights.
        RECT client{};
        GetClientRect(hwnd_, &client);
        const PanelLayout layout = ComputeLayout(client.right, client.bottom);
        const int content_width = std::max(
            80, static_cast<int>(layout.list.right - layout.list.left) - 2 * kRowPaddingX);
        if (content_width != viewport_content_width_) {
          RebuildViewportHeights();
        }
      }
      InvalidateRect(hwnd_, nullptr, FALSE);
      return 0;

    case WM_MOVE:
      if (IsWindowVisible(hwnd_) && !IsIconic(hwnd_) && !IsZoomed(hwnd_)) {
        window_x_ = static_cast<int>(static_cast<short>(LOWORD(l_param)));
        window_y_ = static_cast<int>(static_cast<short>(HIWORD(l_param)));
        has_window_position_ = true;
      }
      frame_interval_ms_ = std::max<UINT>(4, static_cast<UINT>(std::lround(1000.0 / DetectRefreshRateHz(hwnd_))));
      return 0;

    case WM_ENTERSIZEMOVE:
      in_live_resize_ = true;
      search_edit_had_focus_ = search_textbox_.IsFocused();
      resize_search_text_ = search_textbox_.GetText();
      dialog_edit_had_focus_ = overlay_textbox_.IsFocused();
      return 0;

    case WM_EXITSIZEMOVE:
      in_live_resize_ = false;
      dirty_rows_.clear();
      LayoutControls();
      if (search_edit_had_focus_) {
        search_textbox_.SetFocused(true, hwnd_);
      }
      UpdateOverlayDialogLayout();
      if (overlay_dialog_.visible && overlay_dialog_.has_text_input && dialog_edit_had_focus_) {
        overlay_textbox_.SetFocused(true, hwnd_);
      }
      SaveWindowPlacementIfNeeded(true);
      InvalidateRect(hwnd_, nullptr, FALSE);
      return 0;

    case WM_GETMINMAXINFO: {
      auto* min_max = reinterpret_cast<MINMAXINFO*>(l_param);
      min_max->ptMinTrackSize.x = kMinWindowWidth;
      min_max->ptMinTrackSize.y = kMinWindowHeight;
      return 0;
    }

    case WM_DPICHANGED: {
      // C1:DPI 变化必须同步 D2D shell(对照 SettingsWindow::WindowProc 的正确
      // 写法),否则 DIP 与物理像素混用,行高/命中整体错位。
      d2d_shell_.UpdateDpi(HIWORD(w_param));
      const RECT* suggested_rect = reinterpret_cast<RECT*>(l_param);
      SetWindowPos(hwnd_, nullptr, suggested_rect->left, suggested_rect->top, suggested_rect->right - suggested_rect->left, suggested_rect->bottom - suggested_rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
      frame_interval_ms_ = std::max<UINT>(4, static_cast<UINT>(std::lround(1000.0 / DetectRefreshRateHz(hwnd_))));
      // 行高/布局缓存重建:文本布局按像素宽分桶,换屏后桶宽已失效。
      dirty_rows_.clear();
      text_layout_cache_.Clear();
      RebuildViewportHeights();
      LayoutControls();
      InvalidateRect(hwnd_, nullptr, FALSE);
      return 0;
    }

    case WM_TIMER:
      if (w_param == kDeferredPageRequestTimerId) {
        ::KillTimer(hwnd_, kDeferredPageRequestTimerId);
        deferred_page_request_pending_ = false;
        RequestVisiblePages();
        return 0;
      }
      if (w_param == kAnimationTimerId) {
        StepAnimation();
        return 0;
      }
      if (w_param == kHiddenCacheTrimTimerId) {
        RunDeferredHiddenCacheTrim();
        return 0;
      }
      if (w_param == kHiddenWorkingSetTrimTimerId) {
        RunDeferredHiddenWorkingSetTrim();
        return 0;
      }
      if (w_param == kDragDecayTimerId) {
        ::KillTimer(hwnd_, kDragDecayTimerId);
        if (drag_scrollbar_) {
          InvalidateListRegion();
        }
        return 0;
      }
      if (w_param == kScrollBenchTimerId) {
        RunScrollBenchmark();
        return 0;
      }
      break;

    case WM_ACTIVATE:
      common::WriteLog(
          common::LogLevel::Info,
          "ui.visibility",
          "WM_ACTIVATE state=" + std::to_string(LOWORD(w_param)) +
              " visible=" + std::string(IsWindowVisible(hwnd_) ? "true" : "false") +
              " panel_locked=" + std::string(panel_locked_ ? "true" : "false"));
      if (LOWORD(w_param) != WA_INACTIVE) {
        paste_route_requires_recovery_ = true;
      } else {
        SetPanelAllowsActivation(false);
      }
      return 0;

    case WM_ERASEBKGND:
      return 1;

    case WM_PAINT: {
      // 硬规则 2:WM_PAINT 只做 BeginPaint/EndPaint + 置脏,绘制交给帧泵
      // (kFrameRenderMessage 在主循环里渲染),避免 WM_PAINT 内做 D2D 破坏帧序。
      PAINTSTRUCT ps{};
      BeginPaint(hwnd_, &ps);
      EndPaint(hwnd_, &ps);
      MarkFrameDirty();
      return 0;
    }

    case WM_MOUSEMOVE: {
      POINT pt{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
      D2D1_POINT_2F ptF{static_cast<float>(pt.x), static_cast<float>(pt.y)};
      search_textbox_.HandleMouseInput(hwnd_, message, ptF);
      overlay_textbox_.HandleMouseInput(hwnd_, message, ptF);
      OnMouseMove(pt);
      return 0;
    }
    case WM_MOUSEACTIVATE: {
      POINT pt;
      GetCursorPos(&pt);
      ScreenToClient(hwnd_, &pt);
      RECT client{};
      GetClientRect(hwnd_, &client);
      const PanelLayout layout = ComputeLayout(client.right, client.bottom);
      if (RectContains(layout.search_box, pt) || 
          (IsOverlayDialogVisible() && RectContains(overlay_dialog_.input_rect, pt))) {
        SetPanelAllowsActivation(true);
        return MA_ACTIVATE;
      }
      return MA_NOACTIVATE;
    }
    case WM_MOUSELEAVE:
      mouse_tracking_ = false;
      HideHoverTooltip();
      if (row_drag_active_) {
        return 0;
      }
      if (hover_point_.x >= 0 || hover_point_.y >= 0) {
        const int previous_row = RowIndexAtPoint(hover_point_);
        if (previous_row >= 0) {
          MarkRowDirty(previous_row);
          InvalidateListRegion();
        } else {
          InvalidateRect(hwnd_, nullptr, FALSE);
        }
        hover_point_ = POINT{-1, -1};
      }
      return 0;
    case WM_LBUTTONDOWN: {
      POINT pt{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
      D2D1_POINT_2F ptF{static_cast<float>(pt.x), static_cast<float>(pt.y)};
      RECT client{};
      GetClientRect(hwnd_, &client);
      const PanelLayout layout = ComputeLayout(client.right, client.bottom);
      const bool has_search_text = !search_textbox_.GetText().empty();
      const bool on_clear_btn = has_search_text && RectContains(SearchClearButtonRect(layout), pt);
      if (!on_clear_btn) {
        search_textbox_.HandleMouseInput(hwnd_, message, ptF);
      }
      overlay_textbox_.HandleMouseInput(hwnd_, message, ptF);
      OnLeftButtonDown(pt, false);
      return 0;
    }
    case WM_LBUTTONDBLCLK: {
      POINT pt{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
      D2D1_POINT_2F ptF{static_cast<float>(pt.x), static_cast<float>(pt.y)};
      RECT client{};
      GetClientRect(hwnd_, &client);
      const PanelLayout layout = ComputeLayout(client.right, client.bottom);
      const bool has_search_text = !search_textbox_.GetText().empty();
      const bool on_clear_btn = has_search_text && RectContains(SearchClearButtonRect(layout), pt);
      if (!on_clear_btn) {
        search_textbox_.HandleMouseInput(hwnd_, message, ptF);
      }
      overlay_textbox_.HandleMouseInput(hwnd_, message, ptF);
      OnLeftButtonDown(pt, true);
      return 0;
    }
    case WM_LBUTTONUP: {
      POINT pt{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
      D2D1_POINT_2F ptF{static_cast<float>(pt.x), static_cast<float>(pt.y)};
      RECT client{};
      GetClientRect(hwnd_, &client);
      const PanelLayout layout = ComputeLayout(client.right, client.bottom);
      const bool has_search_text = !search_textbox_.GetText().empty();
      const bool on_clear_btn = has_search_text && RectContains(SearchClearButtonRect(layout), pt);
      if (!on_clear_btn) {
        search_textbox_.HandleMouseInput(hwnd_, message, ptF);
      }
      overlay_textbox_.HandleMouseInput(hwnd_, message, ptF);

      if (row_drag_active_) {
        FinishRowDrag(pt, false);
        armed_click_item_index_ = -1;
        return 0;
      }
      if (drag_scrollbar_) {
        drag_scrollbar_ = false;
        ::KillTimer(hwnd_, kDragDecayTimerId);
        ReleaseCapture();
        InvalidateRect(hwnd_, nullptr, FALSE);
        armed_click_item_index_ = -1;
        return 0;
      }
      if (pressed_static_hit_.has_value()) {
        const HitRegion pressed_hit = pressed_static_hit_->hit;
        const bool pressed_inside = pressed_static_hit_->inside;
        pressed_static_hit_.reset();
        if (GetCapture() == hwnd_) {
          ReleaseCapture();
        }
        if (pressed_inside) {
          if (const auto released_hit = ResolveStaticHit(pt);
              released_hit.has_value() &&
              released_hit->kind == pressed_hit.kind &&
              released_hit->index == pressed_hit.index) {
            PerformHitAction(pressed_hit);
          } else {
            InvalidateRect(hwnd_, nullptr, FALSE);
          }
        } else {
          InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return 0;
      }
      if (armed_click_item_index_ >= 0) {
        const int released_index = RowIndexAtPoint(pt);
        const int armed_index = armed_click_item_index_;
        armed_click_item_index_ = -1;
        if (released_index == armed_index && selected_index_ == armed_index) {
          PostMessageW(hwnd_, kInvokeDefaultPasteMessage, 0, 0);
        }
      } else {
        row_drag_candidate_active_ = false;
        row_drag_candidate_index_ = -1;
      }
      return 0;
    }
    case WM_RBUTTONUP:
      OnRightButtonUp(POINT{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)});
      return 0;
    case WM_CAPTURECHANGED:
      drag_scrollbar_ = false;
      ::KillTimer(hwnd_, kDragDecayTimerId);
      if (row_drag_active_) {
        FinishRowDrag(hover_point_, true);
      }
      armed_click_item_index_ = -1;
      if (pressed_static_hit_.has_value()) {
        pressed_static_hit_.reset();
        InvalidateRect(hwnd_, nullptr, FALSE);
      }
      row_drag_candidate_active_ = false;
      row_drag_candidate_index_ = -1;
      return 0;
    case WM_MOUSEWHEEL:
      OnMouseWheel(GET_WHEEL_DELTA_WPARAM(w_param));
      return 0;

    case WM_VSCROLL: {
      SCROLLINFO info{};
      info.cbSize = sizeof(info);
      info.fMask = SIF_ALL;
      GetScrollInfo(hwnd_, SB_VERT, &info);
      int new_pos = info.nPos;
      switch (LOWORD(w_param)) {
        case SB_LINEUP: new_pos -= GetItemHeight() / 2; break;
        case SB_LINEDOWN: new_pos += GetItemHeight() / 2; break;
        case SB_PAGEUP: new_pos -= static_cast<int>(info.nPage); break;
        case SB_PAGEDOWN: new_pos += static_cast<int>(info.nPage); break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION:
          new_pos = info.nTrackPos;
          break;
      }
      const int clamped_pos = std::clamp(
          new_pos,
          static_cast<int>(info.nMin),
          std::max(static_cast<int>(info.nMin), static_cast<int>(info.nMax - static_cast<int>(info.nPage) + 1)));
      if (clamped_pos != info.nPos) {
        scroll_controller_->GetManualScrollDirection() = clamped_pos < info.nPos ? -1 : 1;
      }
      scroll_controller_->GetShowScrollbarUntilMs() = NowEpochMs() + 1200;
      scroll_controller_->SetScrollOffsetTarget(
          clamped_pos,
          LOWORD(w_param) == SB_THUMBTRACK || LOWORD(w_param) == SB_THUMBPOSITION);
      InvalidateRect(hwnd_, nullptr, FALSE);
      return 0;
    }

    case WM_KEYDOWN: {
      if (overlay_textbox_.IsFocused()) {
        if (w_param == VK_ESCAPE) {
          CloseOverlayDialog(true);
          return 0;
        }
        if (w_param == VK_RETURN) {
          CommitOverlayDialog();
          return 0;
        }
        if (overlay_textbox_.HandleMessage(hwnd_, message, w_param, l_param)) {
          InvalidateRect(hwnd_, nullptr, FALSE);
          return 0;
        }
      }
      if (search_textbox_.IsFocused()) {
        if (w_param == VK_UP || w_param == VK_DOWN || w_param == VK_PRIOR || w_param == VK_NEXT || w_param == VK_RETURN || w_param == VK_ESCAPE) {
          OnKeyboardNavigate(static_cast<UINT>(w_param), (GetKeyState(VK_SHIFT) & 0x8000) != 0);
          return 0;
        }
        if (search_textbox_.HandleMessage(hwnd_, message, w_param, l_param)) {
          InvalidateRect(hwnd_, nullptr, FALSE);
          return 0;
        }
      }
      if ((GetKeyState(VK_CONTROL) & 0x8000) != 0 && w_param == 'F') {
        FocusSearchBox(true);
        return 0;
      }
      if (IsSearchFocusHotkey(static_cast<UINT>(w_param), platform::win32::GetPressedHotkeyModifiers())) {
        FocusSearchBox(true);
        return 0;
      }
      OnKeyboardNavigate(static_cast<UINT>(w_param), (GetKeyState(VK_SHIFT) & 0x8000) != 0);
      return 0;
    }

    case WM_CHAR: {
      if (overlay_textbox_.IsFocused()) {
        if (w_param == VK_ESCAPE) {
          return 0;
        }
        if (overlay_textbox_.HandleMessage(hwnd_, message, w_param, l_param)) {
          InvalidateRect(hwnd_, nullptr, FALSE);
          return 0;
        }
      }
      if (search_textbox_.IsFocused()) {
        if (w_param == VK_ESCAPE) {
          return 0;
        }
        if (search_textbox_.HandleMessage(hwnd_, message, w_param, l_param)) {
          InvalidateRect(hwnd_, nullptr, FALSE);
          return 0;
        }
      }
      break;
    }

    case WM_IME_COMPOSITION:
    case WM_IME_STARTCOMPOSITION:
    case WM_IME_ENDCOMPOSITION: {
      if (overlay_textbox_.IsFocused()) {
        const bool handled = overlay_textbox_.HandleMessage(hwnd_, message, w_param, l_param);
        InvalidateRect(hwnd_, nullptr, FALSE);
        if (handled) return 0;
      }
      if (search_textbox_.IsFocused()) {
        const bool handled = search_textbox_.HandleMessage(hwnd_, message, w_param, l_param);
        InvalidateRect(hwnd_, nullptr, FALSE);
        if (handled) return 0;
      }
      return DefWindowProcW(hwnd_, message, w_param, l_param);
    }

    case WM_COMMAND:
      if (LOWORD(w_param) == kIdSearchClearButton && HIWORD(w_param) == BN_CLICKED) {
        ClearSearch();
        return 0;
      }
      break;

    case WM_NCHITTEST:
      return HitTestNonClient(POINT{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)});
    case kApplyWindowBlurMessage:
      ApplyWindowBlur(hwnd_, static_cast<core::store::WindowBlurType>(w_param));
      return 0;
    case kShowWindowMessage:
      ShowAndActivate();
      return 0;
    case kToggleWindowMessage:
      ToggleWindow();
      return 0;
    case kRefreshWindowMessage: {
      auto request = TakeOwnedMessagePayload<core::commands::RefreshRequest>(l_param);
      core::commands::RefreshRequest effective_request = request != nullptr
          ? *request
          : core::commands::MakePreserveViewportRefreshRequest();
      if (pending_refresh_request_.has_value()) {
        effective_request = core::commands::MergeRefreshRequest(*pending_refresh_request_, effective_request);
      }
      pending_refresh_request_ = effective_request;
      RefreshData(effective_request);
      return 0;
    }
    case kFocusSearchMessage:
      FocusSearchBox(w_param != 0);
      return 0;
    case kHandleExternalPointerMessage: {
      auto payload = TakeOwnedMessagePayload<ExternalPointerMessagePayload>(l_param);
      if (payload != nullptr) {
        HandleExternalPointerDown(*payload);
      }
      return 0;
    }
    case kUpdatePasteTargetMessage: {
      const HWND target = reinterpret_cast<HWND>(w_param);
      if (target != nullptr && !IsOwnedWindow(target) && !IsMenuPopupWindow(target)) {
        UpdateLastExternalFocusContextFromWindow(target);
      }
      return 0;
    }
    case kInvokeDefaultPasteMessage:
      PerformDefaultPaste();
      return 0;
    case kCommitPasteMessage:
      CommitPendingPasteRequest();
      return 0;
    case kPresentListMessage:
      list_present_scheduled_ = false;
      InvalidateListRegion();
      return 0;
    case kFrameRenderMessage: {
      // L5 帧泵:置脏后由主循环渲染(不在 WM_PAINT 里画)。frame_render_scheduled_
      // 合并重复调度,一次置脏只出一帧:不漏帧、不重复帧。
      frame_render_scheduled_ = false;
      if (frame_dirty_) {
        frame_dirty_ = false;
        RenderFrame();
      }
      // L6:仍有持续工作(滚动物理/拖拽衰减/骨架 shimmer)→ 交给动画定时器
      // 按 frame_interval_ms_ 节拍接续泵帧;无工作则空转自停。
      if (has_pending_work()) {
        EnsureAnimationTimer();
      }
      return 0;
    }
    case kCountReadyMessage:
      HandleCountReady(w_param, l_param);
      return 0;
    case kPageReadyMessage:
      HandlePageReady(w_param, l_param);
      return 0;
    case kClipReadyMessage:
      HandleClipReady(w_param, l_param);
      return 0;
    case kStartupMetadataReadyMessage:
      HandleStartupMetadataReady(w_param, l_param);
      return 0;
    case kWarmSecondaryD2dMessage:
      if (d2d_shell_.EnsureSecondaryTextFormats().ok()) {
        dirty_rows_.clear();
        InvalidateRect(hwnd_, nullptr, FALSE);
      }
      return 0;
    case kInstallAutoHideHooksMessage:
      UpdateAutoHideHooks();
      return 0;
    case kIconReadyMessage:
      HandleIconReady(w_param, l_param);
      return 0;
    case kPreviewReadyMessage:
      HandlePreviewReady(w_param, l_param);
      return 0;
    case kThumbnailReadyMessage:
      HandleThumbnailReady(w_param, l_param);
      return 0;
    case kSearchKeyMessage:
      if (IsOverlayDialogVisible() && overlay_textbox_.IsFocused()) {
        if (w_param == VK_ESCAPE) {
          CloseOverlayDialog(true);
          return 0;
        }
        if (w_param == VK_RETURN) {
          CommitOverlayDialog();
          return 0;
        }
      }
      OnKeyboardNavigate(static_cast<UINT>(w_param), l_param != 0);
      return 0;
    case WM_NCACTIVATE:
      // Force DWM to think the window is active.
      // This is crucial for keeping Windows 11 Acrylic enabled when the window is shown with SW_SHOWNOACTIVATE.
      return DefWindowProcW(hwnd_, message, TRUE, l_param);
    case WM_CLOSE:
      HideWindow();
      return 0;
  }
  return DefWindowProcW(hwnd_, message, w_param, l_param);
}


int MainWindow::GetItemCount() const { return item_count_; }

int MainWindow::GetViewportHeight() const {
  RECT client{};
  GetClientRect(hwnd_, &client);
  const PanelLayout layout = ComputeLayout(client.right, client.bottom);
  return std::max(1L, layout.list.bottom - layout.list.top);
}
void MainWindow::RequestRender() {
  InvalidateListRegion();
}

}  // namespace clipd2d::ui
