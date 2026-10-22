#include "ui/DialogPanelWindow.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <windowsx.h>

#include "ui/PanelWindowChrome.h"
#include "common/StringUtil.h"

namespace clipd2d::ui {

namespace {

constexpr wchar_t kWindowClassName[] = L"Scraps.DialogPanelWindow";
constexpr DWORD DWMWA_WINDOW_CORNER_PREFERENCE_VALUE = 33;
constexpr int kDwmCornerRoundValue = 2;

void ApplyDialogRoundedRegion(HWND hwnd) {
  if (hwnd == nullptr) {
    return;
  }
  DwmSetWindowAttribute(
      hwnd,
      DWMWA_WINDOW_CORNER_PREFERENCE_VALUE,
      &kDwmCornerRoundValue,
      sizeof(kDwmCornerRoundValue));
}

bool RectContainsPoint(const RECT& rect, POINT point) {
  return point.x >= rect.left && point.x < rect.right &&
      point.y >= rect.top && point.y < rect.bottom;
}

D2D1_COLOR_F ParseDialogColor(std::wstring_view color) {
  if (color.size() != 7 || color[0] != L'#') {
    return D2D1::ColorF(0xD7DFED);
  }
  auto hex_value = [](wchar_t ch) -> int {
    if (ch >= L'0' && ch <= L'9') return ch - L'0';
    if (ch >= L'a' && ch <= L'f') return 10 + (ch - L'a');
    if (ch >= L'A' && ch <= L'F') return 10 + (ch - L'A');
    return -1;
  };
  const int r_high = hex_value(color[1]);
  const int r_low  = hex_value(color[2]);
  const int g_high = hex_value(color[3]);
  const int g_low  = hex_value(color[4]);
  const int b_high = hex_value(color[5]);
  const int b_low  = hex_value(color[6]);
  if (r_high < 0 || r_low < 0 || g_high < 0 || g_low < 0 || b_high < 0 || b_low < 0) {
    return D2D1::ColorF(0xD7DFED);
  }
  const UINT32 rgb = static_cast<UINT32>(
      ((r_high << 4) | r_low) << 16 |
      ((g_high << 4) | g_low) << 8 |
      ((b_high << 4) | b_low));
  return D2D1::ColorF(rgb);
}

RECT ClampDialogRectToWorkArea(const RECT& rect, HWND owner) {
  RECT work_area{};
  HMONITOR monitor = MonitorFromWindow(
      owner != nullptr ? owner : GetDesktopWindow(), MONITOR_DEFAULTTONEAREST);
  MONITORINFO monitor_info{};
  monitor_info.cbSize = sizeof(monitor_info);
  if (GetMonitorInfoW(monitor, &monitor_info)) {
    work_area = monitor_info.rcWork;
  } else {
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work_area, 0);
  }

  RECT clamped = rect;
  const int width  = rect.right - rect.left;
  const int height = rect.bottom - rect.top;
  if (clamped.left < work_area.left) {
    clamped.left  = work_area.left;
    clamped.right = clamped.left + width;
  }
  if (clamped.top < work_area.top) {
    clamped.top    = work_area.top;
    clamped.bottom = clamped.top + height;
  }
  if (clamped.right > work_area.right) {
    clamped.right = work_area.right;
    clamped.left  = clamped.right - width;
  }
  if (clamped.bottom > work_area.bottom) {
    clamped.bottom = work_area.bottom;
    clamped.top    = clamped.bottom - height;
  }
  return clamped;
}

RECT ComputeInitialRect(HWND owner, int width, int height) {
  RECT owner_rect{};
  if (owner != nullptr) {
    GetWindowRect(owner, &owner_rect);
  } else {
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &owner_rect, 0);
  }
  RECT rect{
      owner_rect.left + ((owner_rect.right - owner_rect.left) - width) / 2,
      owner_rect.top + ((owner_rect.bottom - owner_rect.top) - height) / 2,
      0,
      0};
  rect.right  = rect.left + width;
  rect.bottom = rect.top + height;
  return ClampDialogRectToWorkArea(rect, owner);
}

}  // namespace

DialogPanelWindow::~DialogPanelWindow() {
  Destroy();
}

common::Status DialogPanelWindow::Create(HINSTANCE instance, HWND owner) {
  instance_ = instance;
  owner_    = owner;

  WNDCLASSEXW window_class{};
  window_class.cbSize        = sizeof(window_class);
  window_class.hInstance     = instance_;
  window_class.lpfnWndProc   = &DialogPanelWindow::StaticWindowProc;
  window_class.lpszClassName = kWindowClassName;
  window_class.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
  window_class.hbrBackground = nullptr;
  window_class.style         = CS_DBLCLKS;
  RegisterClassExW(&window_class);

  hwnd_ = CreateWindowExW(
      WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
      kWindowClassName,
      L"",
      WS_POPUP | WS_CLIPCHILDREN,
      CW_USEDEFAULT,
      CW_USEDEFAULT,
      380,
      220,
      owner_,
      nullptr,
      instance_,
      this);
  if (hwnd_ == nullptr) {
    return common::Status::Error(
        common::StatusCode::PlatformError, "Failed to create dialog panel window.");
  }

  common::Status status = d2d_shell_.Initialize(hwnd_);
  if (!status.ok()) {
    return status;
  }
  status = d2d_shell_.EnsureSecondaryTextFormats();
  if (!status.ok()) {
    return status;
  }
  ApplyDialogRoundedRegion(hwnd_);
  return InitTextBox();
}

// 初始化 D2D 文本框（替代原 CreateInputProxy）
common::Status DialogPanelWindow::InitTextBox() {
  if (!text_box_.Initialize(d2d_shell_.dwrite_factory())) {
    return common::Status::Error(
        common::StatusCode::PlatformError, "Failed to initialize D2D text box.");
  }
  text_box_.SetDrawBorder(false);
  text_box_.SetDrawKnob(false);
  text_box_.SetDrawCloseButton(false);
  text_box_.SetFontSize(13.0f);
  text_box_.SetFontFamily(L"Segoe UI");
  text_box_.SetTextColor(D2D1::ColorF(0x23262D));
  text_box_.SetPlaceholderColor(D2D1::ColorF(0x8D96A6));
  text_box_.SetSelectionColor(D2D1::ColorF(0.23f, 0.48f, 1.0f, 0.25f));
  text_box_.SetBorderColor(D2D1::ColorF(0x3A7AFE));
  ApplyThemeToTextboxes();
  return common::Status::Ok();
}

void DialogPanelWindow::ApplyThemeToTextboxes() {
  text_box_.SetTextColor(d2d_shell_.ColorText());
  text_box_.SetPlaceholderColor(d2d_shell_.ColorSubtleText());
  text_box_.SetBorderColor(d2d_shell_.ColorAccent());
}

void DialogPanelWindow::Show(const Config& config, HostActions actions) {
  config_        = config;
  host_actions_  = std::move(actions);
  input_text_    = config.initial_text;
  error_text_.clear();
  selected_color_ = config.initial_color;
  if (selected_color_.empty() && !config.color_options.empty()) {
    selected_color_ = config.color_options.front().value;
  }
  hover_point_ = POINT{-1, -1};
  visible_     = true;
  SetWindowAllowsActivation(false);
  LayoutWindow();
  ApplyDialogRoundedRegion(hwnd_);

  // 更新文本框内容
  text_box_.Reset();
  text_box_.SetText(config.initial_text);

  SetWindowPos(
      hwnd_, HWND_TOP, 0, 0, 0, 0,
      SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE);
  ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
  SetInteractionGuardActive(true);
  InvalidateRect(hwnd_, nullptr, FALSE);
  if (config_.has_text_input && config_.auto_focus_input) {
    BeginInputFocus();
  }
}

void DialogPanelWindow::Destroy() {
  SetInteractionGuardActive(false);
  KillTimer(hwnd_, kCaretTimerId);
  d2d_shell_.Shutdown();
  if (hwnd_ != nullptr) {
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
}

// ──────────────────────────────────────────────────────────────────────────────
// Window proc
// ──────────────────────────────────────────────────────────────────────────────

LRESULT CALLBACK DialogPanelWindow::StaticWindowProc(
    HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
  DialogPanelWindow* self = nullptr;
  if (message == WM_NCCREATE) {
    auto* create = reinterpret_cast<CREATESTRUCTW*>(l_param);
    self = static_cast<DialogPanelWindow*>(create->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  } else {
    self = reinterpret_cast<DialogPanelWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  }
  if (self != nullptr) {
    self->hwnd_ = hwnd;
    return self->WindowProc(message, w_param, l_param);
  }
  return DefWindowProcW(hwnd, message, w_param, l_param);
}

// ──────────────────────────────────────────────────────────────────────────────
// Focus management
// ──────────────────────────────────────────────────────────────────────────────

void DialogPanelWindow::SetWindowAllowsActivation(bool allow_activation) {
  if (hwnd_ == nullptr || window_allows_activation_ == allow_activation) {
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
      hwnd_, nullptr, 0, 0, 0, 0,
      SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER |
          SWP_FRAMECHANGED | (allow_activation ? 0 : SWP_NOACTIVATE));
  window_allows_activation_ = allow_activation;
}

void DialogPanelWindow::SetInteractionGuardActive(bool active) {
  if (interaction_guard_active_ == active) {
    return;
  }
  interaction_guard_active_ = active;
  if (active) {
    if (host_actions_.on_show) {
      host_actions_.on_show();
    }
  } else if (host_actions_.on_hide) {
    host_actions_.on_hide();
  }
}

void DialogPanelWindow::BeginInputFocus() {
  if (!config_.has_text_input) {
    return;
  }
  if (!input_focus_active_) {
    input_focus_active_ = true;
    if (host_actions_.on_begin_input_focus) {
      host_actions_.on_begin_input_focus();
    }
  }
  SetWindowAllowsActivation(true);
  ShowWindow(hwnd_, SW_SHOWNORMAL);
  BringWindowToTop(hwnd_);
  SetForegroundWindow(hwnd_);
  SetActiveWindow(hwnd_);
  SetFocus(hwnd_);                            // 焦点给主窗口，WM_CHAR 路由到 text_box_
  text_box_.SetFocused(true, hwnd_);
  // 全选文本（相当于 EM_SETSEL 0,-1）
  text_box_.SelectAll();
  // 启动光标闪烁定时器
  SetTimer(hwnd_, kCaretTimerId, 500, nullptr);
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void DialogPanelWindow::EndInputFocus(bool restore_external_focus) {
  text_box_.SetFocused(false, hwnd_);
  KillTimer(hwnd_, kCaretTimerId);
  SetWindowAllowsActivation(false);
  if (input_focus_active_) {
    input_focus_active_ = false;
    if (host_actions_.on_end_input_focus) {
      host_actions_.on_end_input_focus();
    }
  } else if (restore_external_focus && host_actions_.on_end_input_focus) {
    host_actions_.on_end_input_focus();
  }
}

void DialogPanelWindow::SyncTextBoxToState() {
  input_text_ = common::TrimWide(text_box_.GetText());
}

void DialogPanelWindow::ClearError() {
  if (!error_text_.empty()) {
    error_text_.clear();
    InvalidateRect(hwnd_, nullptr, FALSE);
  }
}

const DialogPanelWindow::HitRegion* DialogPanelWindow::HitTest(POINT point) const {
  for (const auto& hit : hit_regions_) {
    if (RectContainsPoint(hit.rect, point)) {
      return &hit;
    }
  }
  return nullptr;
}

void DialogPanelWindow::CloseDialog(bool confirmed) {
  if (config_.has_text_input) {
    SyncTextBoxToState();
    if (confirmed && config_.requires_non_empty_text && input_text_.empty()) {
      error_text_ = config_.empty_text_error.empty()
          ? std::wstring(L"\u540d\u79f0\u4e0d\u80fd\u4e3a\u7a7a\u3002")
          : config_.empty_text_error;
      InvalidateRect(hwnd_, nullptr, FALSE);
      BeginInputFocus();
      return;
    }
  }

  Result result;
  result.confirmed = confirmed;
  result.text      = input_text_;
  result.color     = selected_color_;

  EndInputFocus(false);
  visible_ = false;
  ShowWindow(hwnd_, SW_HIDE);
  SetInteractionGuardActive(false);
  if (host_actions_.on_result) {
    host_actions_.on_result(result);
  }
  host_actions_ = HostActions{};
}

// ──────────────────────────────────────────────────────────────────────────────
// Layout & Render
// ──────────────────────────────────────────────────────────────────────────────

void DialogPanelWindow::LayoutWindow() {
  if (hwnd_ == nullptr) {
    return;
  }

  const int width  = 380;
  const int height = config_.has_color_options ? 320 : 220;
  const RECT rect  = ComputeInitialRect(owner_, width, height);
  SetWindowPos(
      hwnd_, nullptr,
      rect.left, rect.top, width, height,
      SWP_NOZORDER | SWP_NOOWNERZORDER |
          (window_allows_activation_ ? 0 : SWP_NOACTIVATE));

  RECT client{};
  GetClientRect(hwnd_, &client);
  constexpr int kMargin = 18;
  message_rect_ = RECT{
      kMargin,
      52,
      client.right - kMargin,
      config_.has_text_input ? 74 : client.bottom - 72};
  input_rect_   = RECT{kMargin, 86, client.right - kMargin, 116};
  error_rect_   = RECT{kMargin, 122, client.right - kMargin, 140};
  confirm_rect_ = RECT{
      client.right - kMargin - 96 * 2 - 10,
      client.bottom - kMargin - 32,
      client.right - kMargin - 96 - 10,
      client.bottom - kMargin};
  cancel_rect_ = RECT{
      client.right - kMargin - 96,
      client.bottom - kMargin - 32,
      client.right - kMargin,
      client.bottom - kMargin};

  color_rects_.clear();
  if (config_.has_color_options) {
    int left = kMargin;
    int top  = 156;
    constexpr int chip_width  = 72;
    constexpr int chip_height = 26;
    constexpr int chip_gap    = 8;
    for (size_t index = 0; index < config_.color_options.size(); ++index) {
      color_rects_.push_back(RECT{left, top, left + chip_width, top + chip_height});
      left += chip_width + chip_gap;
      if (left + chip_width > client.right - kMargin) {
        left  = kMargin;
        top  += chip_height + chip_gap;
      }
    }
  }

  // 同步文本框位置（与 input_rect_ 内边距对齐）
  if (config_.has_text_input) {
    const D2D1_RECT_F textbox_rect = D2D1::RectF(
        static_cast<float>(input_rect_.left) + 8.0f,
        static_cast<float>(input_rect_.top) + 5.0f,
        static_cast<float>(input_rect_.right) - 8.0f,
        static_cast<float>(input_rect_.bottom) - 5.0f);
    text_box_.SetRect(textbox_rect);
  }
}

void DialogPanelWindow::RenderFrame() {
  // C1 暗色模式：主题纪元变化时重建刷子并同步文本框颜色与 DWM 暗色属性。
  if (d2d_shell_.SyncTheme()) {
    ApplyThemeToTextboxes();
    ApplyDarkModeAttribute(hwnd_, clipd2d::ui::IsDarkThemeActive());
  }
  ID2D1HwndRenderTarget* target = d2d_shell_.BeginFrame();
  if (target == nullptr) {
    return;
  }

  RECT client{};
  GetClientRect(hwnd_, &client);
  hit_regions_.clear();
  target->Clear(d2d_shell_.ColorSurface());
  target->FillRectangle(
      D2D1::RectF(0.0f, 0.0f,
          static_cast<float>(client.right), static_cast<float>(client.bottom)),
      d2d_shell_.surface_brush());
  target->DrawRectangle(
      D2D1::RectF(0.5f, 0.5f,
          static_cast<float>(client.right) - 0.5f,
          static_cast<float>(client.bottom) - 0.5f),
      d2d_shell_.border_brush(),
      1.0f);

  // 标题
  target->DrawTextW(
      config_.title.c_str(),
      static_cast<UINT32>(config_.title.size()),
      d2d_shell_.section_format(),
      D2D1::RectF(18.0f, 16.0f, static_cast<float>(client.right - 18), 40.0f),
      d2d_shell_.text_brush(),
      D2D1_DRAW_TEXT_OPTIONS_CLIP);

  // 正文/提示
  const std::wstring body_text =
      config_.has_text_input ? config_.prompt : config_.message;
  target->DrawTextW(
      body_text.c_str(),
      static_cast<UINT32>(body_text.size()),
      d2d_shell_.small_format(),
      D2D1::RectF(
          static_cast<float>(message_rect_.left),
          static_cast<float>(message_rect_.top),
          static_cast<float>(message_rect_.right),
          static_cast<float>(message_rect_.bottom)),
      d2d_shell_.muted_text_brush(),
      D2D1_DRAW_TEXT_OPTIONS_NONE);

  // 文本输入区域
  if (config_.has_text_input) {
    const auto input_rounded = D2D1::RoundedRect(
        D2D1::RectF(
            static_cast<float>(input_rect_.left),
            static_cast<float>(input_rect_.top),
            static_cast<float>(input_rect_.right),
            static_cast<float>(input_rect_.bottom)),
        10.0f, 10.0f);
    target->FillRoundedRectangle(input_rounded, d2d_shell_.surface_alt_brush());
    target->DrawRoundedRectangle(
        input_rounded,
        text_box_.IsFocused() ? d2d_shell_.accent_brush() : d2d_shell_.border_brush(),
        1.0f);

    // 绘制 D2D 文本框
    text_box_.Draw(target);

    // 错误提示
    if (!error_text_.empty()) {
      target->DrawTextW(
          error_text_.c_str(),
          static_cast<UINT32>(error_text_.size()),
          d2d_shell_.tiny_format(),
          D2D1::RectF(
              static_cast<float>(error_rect_.left),
              static_cast<float>(error_rect_.top),
              static_cast<float>(error_rect_.right),
              static_cast<float>(error_rect_.bottom)),
          d2d_shell_.danger_brush(),
          D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
  }

  // 颜色选择器
  if (config_.has_color_options && !color_rects_.empty()) {
    const float prompt_top = static_cast<float>(color_rects_.front().top - 20);
    target->DrawTextW(
        L"\u989c\u8272",
        2,
        d2d_shell_.small_format(),
        D2D1::RectF(18.0f, prompt_top, static_cast<float>(client.right - 18), prompt_top + 16.0f),
        d2d_shell_.muted_text_brush(),
        D2D1_DRAW_TEXT_OPTIONS_CLIP);
    for (size_t index = 0; index < color_rects_.size(); ++index) {
      const bool selected = selected_color_ == config_.color_options[index].value;
      const bool hovered  = RectContainsPoint(color_rects_[index], hover_point_);
      // Q2:色板动态色走 D2dShell 缓存,不再每帧 CreateSolidColorBrush。
      ID2D1SolidColorBrush* color_brush =
          d2d_shell_.solid_brush(target, ParseDialogColor(config_.color_options[index].value));
      const auto rounded = D2D1::RoundedRect(
          D2D1::RectF(
              static_cast<float>(color_rects_[index].left),
              static_cast<float>(color_rects_[index].top),
              static_cast<float>(color_rects_[index].right),
              static_cast<float>(color_rects_[index].bottom)),
          10.0f, 10.0f);
      target->FillRoundedRectangle(
          rounded,
          color_brush ? color_brush
              : (selected    ? d2d_shell_.accent_soft_brush()
                : (hovered   ? d2d_shell_.surface_hover_brush()
                             : d2d_shell_.surface_alt_brush())));
      target->DrawRoundedRectangle(
          rounded,
          selected ? d2d_shell_.accent_brush() : d2d_shell_.border_brush(),
          1.0f);
      hit_regions_.push_back(
          HitRegion{color_rects_[index], HitKind::ColorChip, static_cast<int>(index)});
    }
  }

  // 按钮
  const auto draw_button = [&](const RECT& rect, const std::wstring& text,
                                bool primary, bool dangerous) {
    const bool hovered  = RectContainsPoint(rect, hover_point_);
    const auto rounded = D2D1::RoundedRect(
        D2D1::RectF(
            static_cast<float>(rect.left),
            static_cast<float>(rect.top),
            static_cast<float>(rect.right),
            static_cast<float>(rect.bottom)),
        10.0f, 10.0f);
    ID2D1SolidColorBrush* fill       = nullptr;
    ID2D1SolidColorBrush* border     = nullptr;
    ID2D1SolidColorBrush* text_brush = nullptr;
    if (primary) {
      fill       = dangerous ? d2d_shell_.danger_brush() : d2d_shell_.accent_soft_brush();
      border     = dangerous ? d2d_shell_.danger_brush() : d2d_shell_.accent_brush();
      text_brush = dangerous ? d2d_shell_.white_brush()  : d2d_shell_.accent_brush();
    } else {
      fill       = hovered ? d2d_shell_.surface_hover_brush() : d2d_shell_.surface_alt_brush();
      border     = d2d_shell_.border_brush();
      text_brush = d2d_shell_.text_brush();
    }
    target->FillRoundedRectangle(rounded, fill);
    target->DrawRoundedRectangle(rounded, border, 1.0f);
    d2d_shell_.small_format()->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
    d2d_shell_.small_format()->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    target->DrawTextW(
        text.c_str(),
        static_cast<UINT32>(text.size()),
        d2d_shell_.small_format(),
        D2D1::RectF(
            static_cast<float>(rect.left),
            static_cast<float>(rect.top),
            static_cast<float>(rect.right),
            static_cast<float>(rect.bottom)),
        text_brush,
        D2D1_DRAW_TEXT_OPTIONS_CLIP);
    d2d_shell_.small_format()->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    d2d_shell_.small_format()->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
  };

  draw_button(
      confirm_rect_,
      config_.confirm_text.empty() ? std::wstring(L"\u786e\u5b9a") : config_.confirm_text,
      true, config_.dangerous_confirm);
  draw_button(
      cancel_rect_,
      config_.cancel_text.empty() ? std::wstring(L"\u53d6\u6d88") : config_.cancel_text,
      false, false);
  hit_regions_.push_back(HitRegion{confirm_rect_, HitKind::Confirm, -1});
  hit_regions_.push_back(HitRegion{cancel_rect_,  HitKind::Cancel,  -1});

  if (d2d_shell_.EndFrame()) {
    LayoutWindow();
  }
}

// ──────────────────────────────────────────────────────────────────────────────
// WindowProc
// ──────────────────────────────────────────────────────────────────────────────

LRESULT DialogPanelWindow::WindowProc(UINT message, WPARAM w_param, LPARAM l_param) {
  switch (message) {
    case WM_ERASEBKGND:
      return 1;
    case WM_SIZE:
      d2d_shell_.Resize(LOWORD(l_param), HIWORD(l_param));
      LayoutWindow();
      ApplyDialogRoundedRegion(hwnd_);
      InvalidateRect(hwnd_, nullptr, FALSE);
      return 0;
    case WM_DPICHANGED: {
      d2d_shell_.UpdateDpi(HIWORD(w_param));
      const RECT* suggested = reinterpret_cast<const RECT*>(l_param);
      SetWindowPos(
          hwnd_, nullptr,
          suggested->left, suggested->top,
          suggested->right - suggested->left,
          suggested->bottom - suggested->top,
          SWP_NOZORDER | SWP_NOACTIVATE);
      LayoutWindow();
      InvalidateRect(hwnd_, nullptr, FALSE);
      return 0;
    }
    case WM_PAINT: {
      PAINTSTRUCT ps{};
      BeginPaint(hwnd_, &ps);
      RenderFrame();
      EndPaint(hwnd_, &ps);
      return 0;
    }
    case WM_TIMER:
      if (w_param == kCaretTimerId) {
        InvalidateRect(hwnd_, nullptr, FALSE);
      }
      return 0;
    // ── 鼠标输入 ────────────────────────────────────────────────────────────
    case WM_MOUSEMOVE: {
      hover_point_ = POINT{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
      if (config_.has_text_input) {
        text_box_.HandleMouseInput(
            hwnd_, message,
            D2D1::Point2F(static_cast<float>(hover_point_.x),
                          static_cast<float>(hover_point_.y)));
      }
      InvalidateRect(hwnd_, nullptr, FALSE);
      return 0;
    }
    case WM_LBUTTONDOWN: {
      const POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
      if (config_.has_text_input && RectContainsPoint(input_rect_, point)) {
        BeginInputFocus();
      }
      if (config_.has_text_input) {
        text_box_.HandleMouseInput(
            hwnd_, message,
            D2D1::Point2F(static_cast<float>(point.x), static_cast<float>(point.y)));
      }
      InvalidateRect(hwnd_, nullptr, FALSE);
      return 0;
    }
    case WM_LBUTTONDBLCLK: {
      const POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
      if (config_.has_text_input) {
        text_box_.HandleMouseInput(
            hwnd_, message,
            D2D1::Point2F(static_cast<float>(point.x), static_cast<float>(point.y)));
        InvalidateRect(hwnd_, nullptr, FALSE);
      }
      return 0;
    }
    case WM_LBUTTONUP: {
      const POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
      if (config_.has_text_input) {
        text_box_.HandleMouseInput(
            hwnd_, message,
            D2D1::Point2F(static_cast<float>(point.x), static_cast<float>(point.y)));
      }
      const HitRegion* hit = HitTest(point);
      if (hit == nullptr) {
        return 0;
      }
      switch (hit->kind) {
        case HitKind::Confirm:
          CloseDialog(true);
          return 0;
        case HitKind::Cancel:
          CloseDialog(false);
          return 0;
        case HitKind::ColorChip:
          if (hit->index >= 0 &&
              hit->index < static_cast<int>(config_.color_options.size())) {
            selected_color_ = config_.color_options[static_cast<size_t>(hit->index)].value;
            InvalidateRect(hwnd_, nullptr, FALSE);
          }
          return 0;
        case HitKind::None:
          return 0;
      }
      return 0;
    }
    // ── 键盘 / IME 输入 → 路由到 D2D 文本框 ─────────────────────────────────
    case WM_KEYDOWN:
      if (w_param == VK_ESCAPE) {
        CloseDialog(false);
        return 0;
      }
      if (!config_.has_text_input && w_param == VK_RETURN) {
        CloseDialog(true);
        return 0;
      }
      if (config_.has_text_input &&
          text_box_.HandleMessage(hwnd_, message, w_param, l_param)) {
        ClearError();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
      }
      break;
    case WM_CHAR:
      if (w_param == L'\x1B') return 0;  // Escape 在 WM_KEYDOWN 已处理
      if (config_.has_text_input &&
          text_box_.HandleMessage(hwnd_, message, w_param, l_param)) {
        ClearError();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return 0;
      }
      break;
    case WM_IME_COMPOSITION:
    case WM_IME_STARTCOMPOSITION:
    case WM_IME_ENDCOMPOSITION:
      if (config_.has_text_input) {
        const bool handled =
            text_box_.HandleMessage(hwnd_, message, w_param, l_param);
        InvalidateRect(hwnd_, nullptr, FALSE);
        if (handled) return 0;
      }
      return DefWindowProcW(hwnd_, message, w_param, l_param);
    case WM_MOUSEACTIVATE:
      return MA_NOACTIVATE;
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
      break;
    case WM_CLOSE:
      CloseDialog(false);
      return 0;
  }
  return DefWindowProcW(hwnd_, message, w_param, l_param);
}

}  // namespace clipd2d::ui
