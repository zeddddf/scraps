#include "ui/PanelInputDialog.h"

#include <dwmapi.h>
#include <windowsx.h>

#include <array>
#include <memory>

#include <commctrl.h>

#include "common/StringUtil.h"
#include "ui/D2DWindowlessTextBox.h"
#include "ui/D2dShell.h"

namespace clipd2d::ui {

namespace {

constexpr wchar_t kDialogClassName[] = L"Scraps.PanelInputDialog";
constexpr int kIdOk = 1;
constexpr int kIdCancel = 2;
constexpr DWORD DWMWA_WINDOW_CORNER_PREFERENCE_VALUE = 33;
constexpr int kDwmCornerRoundValue = 2;
constexpr UINT_PTR kCaretTimerId = 100;

struct HitRegion {
  RECT rect{};
  int kind = 0;
  int index = -1;
};

enum HitKind {
  HitNone = 0,
  HitConfirm = 1,
  HitCancel = 2,
  HitColor = 3,
};

struct DialogState {
  HINSTANCE instance = nullptr;
  HWND owner = nullptr;
  HWND hwnd = nullptr;
  D2DWindowlessTextBox name_textbox;    // D2D 无窗口文本框，替代 Win32 EDIT
  D2dShell d2d_shell;
  bool d2d_ready = false;
  PanelInputDialogConfig config;
  std::wstring result_text;
  std::wstring result_color;
  bool confirmed = false;
  bool done = false;
  RECT edit_rect{};
  RECT error_rect{};
  RECT confirm_rect{};
  RECT cancel_rect{};
  std::vector<RECT> color_rects;
  std::vector<HitRegion> hit_regions;
  POINT hover_point{-1, -1};
  std::wstring error_message;
};

bool RectContainsPoint(const RECT& rect, POINT point) {
  return point.x >= rect.left && point.x < rect.right &&
      point.y >= rect.top && point.y < rect.bottom;
}

void CenterToOwner(HWND hwnd, HWND owner) {
  RECT rect{};
  RECT owner_rect{};
  GetWindowRect(hwnd, &rect);
  if (owner != nullptr) {
    GetWindowRect(owner, &owner_rect);
  } else {
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &owner_rect, 0);
  }
  const int width = rect.right - rect.left;
  const int height = rect.bottom - rect.top;
  const int x = owner_rect.left + ((owner_rect.right - owner_rect.left) - width) / 2;
  const int y = owner_rect.top + ((owner_rect.bottom - owner_rect.top) - height) / 2;
  SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
}

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

void FinishDialog(DialogState* state, bool confirmed) {
  if (state == nullptr || state->hwnd == nullptr) {
    return;
  }
  state->confirmed = confirmed;
  state->done = true;
  DestroyWindow(state->hwnd);
}

void LayoutDialog(DialogState* state) {
  if (state == nullptr || state->hwnd == nullptr) {
    return;
  }
  RECT client{};
  GetClientRect(state->hwnd, &client);

  constexpr int kMargin = 18;
  constexpr int kEditHeight = 30;
  constexpr int kErrorHeight = 18;
  constexpr int kButtonWidth = 86;
  constexpr int kButtonHeight = 32;
  constexpr int kButtonGap = 10;
  constexpr int kColorChipHeight = 26;
  constexpr int kColorChipWidth = 72;
  constexpr int kColorChipGap = 8;

  const int content_width = client.right - client.left - kMargin * 2;
  state->edit_rect = RECT{kMargin, 78, kMargin + content_width, 78 + kEditHeight};
  state->error_rect = RECT{
      kMargin,
      state->edit_rect.bottom + 6,
      kMargin + content_width,
      state->edit_rect.bottom + 6 + kErrorHeight};

  // 同步更新 D2D 文本框位置（内边距 8px 水平，5px 垂直）
  if (state->d2d_ready) {
    const D2D1_RECT_F textbox_rect = D2D1::RectF(
        static_cast<float>(state->edit_rect.left) + 8.0f,
        static_cast<float>(state->edit_rect.top) + 5.0f,
        static_cast<float>(state->edit_rect.right) - 8.0f,
        static_cast<float>(state->edit_rect.bottom) - 5.0f);
    state->name_textbox.SetRect(textbox_rect);
  }

  state->confirm_rect = RECT{
      client.right - kMargin - kButtonWidth * 2 - kButtonGap,
      client.bottom - kMargin - kButtonHeight,
      client.right - kMargin - kButtonWidth - kButtonGap,
      client.bottom - kMargin};
  state->cancel_rect = RECT{
      client.right - kMargin - kButtonWidth,
      client.bottom - kMargin - kButtonHeight,
      client.right - kMargin,
      client.bottom - kMargin};

  state->color_rects.clear();
  if (!state->config.color_options.empty()) {
    int left = kMargin;
    int top = state->error_rect.bottom + 14;
    for (size_t index = 0; index < state->config.color_options.size(); ++index) {
      state->color_rects.push_back(RECT{
          left,
          top,
          left + kColorChipWidth,
          top + kColorChipHeight});
      left += kColorChipWidth + kColorChipGap;
      if (left + kColorChipWidth > client.right - kMargin) {
        left = kMargin;
        top += kColorChipHeight + kColorChipGap;
      }
    }
  }
}

std::optional<HitRegion> HitTest(DialogState* state, POINT point) {
  if (state == nullptr) {
    return std::nullopt;
  }
  if (RectContainsPoint(state->confirm_rect, point)) {
    return HitRegion{state->confirm_rect, HitConfirm, -1};
  }
  if (RectContainsPoint(state->cancel_rect, point)) {
    return HitRegion{state->cancel_rect, HitCancel, -1};
  }
  for (size_t index = 0; index < state->color_rects.size(); ++index) {
    if (RectContainsPoint(state->color_rects[index], point)) {
      return HitRegion{state->color_rects[index], HitColor, static_cast<int>(index)};
    }
  }
  return std::nullopt;
}

void DrawDialog(DialogState* state) {
  if (state == nullptr || !state->d2d_ready) {
    return;
  }
  RECT client{};
  GetClientRect(state->hwnd, &client);
  if (auto* target = state->d2d_shell.BeginFrame()) {
    target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    const auto rect = D2D1::RectF(
        static_cast<float>(client.left),
        static_cast<float>(client.top),
        static_cast<float>(client.right),
        static_cast<float>(client.bottom));
    target->Clear(state->d2d_shell.ColorSurface());
    target->FillRectangle(rect, state->d2d_shell.surface_brush());
    target->DrawRectangle(
        D2D1::RectF(rect.left + 0.5f, rect.top + 0.5f, rect.right - 0.5f, rect.bottom - 0.5f),
        state->d2d_shell.border_brush(),
        1.0f);

    const D2D1_RECT_F title_rect = D2D1::RectF(18.0f, 16.0f, rect.right - 18.0f, 42.0f);
    const D2D1_RECT_F prompt_rect = D2D1::RectF(18.0f, 52.0f, rect.right - 18.0f, 70.0f);
    target->DrawTextW(
        state->config.title.c_str(),
        static_cast<UINT32>(state->config.title.size()),
        state->d2d_shell.section_format(),
        title_rect,
        state->d2d_shell.text_brush(),
        D2D1_DRAW_TEXT_OPTIONS_CLIP);
    target->DrawTextW(
        state->config.prompt.c_str(),
        static_cast<UINT32>(state->config.prompt.size()),
        state->d2d_shell.small_format(),
        prompt_rect,
        state->d2d_shell.muted_text_brush(),
        D2D1_DRAW_TEXT_OPTIONS_CLIP);

    // 绘制文本输入框圆角背景
    const D2D1_ROUNDED_RECT edit_rounded = D2D1::RoundedRect(
        D2D1::RectF(
            static_cast<float>(state->edit_rect.left),
            static_cast<float>(state->edit_rect.top),
            static_cast<float>(state->edit_rect.right),
            static_cast<float>(state->edit_rect.bottom)),
        10.0f,
        10.0f);
    target->FillRoundedRectangle(edit_rounded, state->d2d_shell.surface_alt_brush());
    target->DrawRoundedRectangle(
        edit_rounded,
        state->name_textbox.IsFocused()
            ? state->d2d_shell.accent_brush()
            : state->d2d_shell.border_brush(),
        1.0f);

    // 绘制 D2D 文本框内容（光标、选区、文字、拼音）
    state->name_textbox.Draw(target);

    // 绘制错误提示
    if (!state->error_message.empty()) {
      target->DrawTextW(
          state->error_message.c_str(),
          static_cast<UINT32>(state->error_message.size()),
          state->d2d_shell.tiny_format(),
          D2D1::RectF(
              static_cast<float>(state->error_rect.left),
              static_cast<float>(state->error_rect.top),
              static_cast<float>(state->error_rect.right),
              static_cast<float>(state->error_rect.bottom)),
          state->d2d_shell.danger_brush(),
          D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }

    // 绘制颜色选择器
    if (!state->config.color_options.empty()) {
      const float color_prompt_top = static_cast<float>(state->color_rects.front().top - 20);
      const D2D1_RECT_F color_prompt = D2D1::RectF(
          18.0f, color_prompt_top, rect.right - 18.0f, color_prompt_top + 16.0f);
      target->DrawTextW(
          L"\u989c\u8272",
          2,
          state->d2d_shell.small_format(),
          color_prompt,
          state->d2d_shell.muted_text_brush(),
          D2D1_DRAW_TEXT_OPTIONS_CLIP);
      for (size_t index = 0; index < state->color_rects.size(); ++index) {
        const bool selected =
            state->result_color == state->config.color_options[index].value ||
            (state->result_color.empty() &&
             state->config.initial_color == state->config.color_options[index].value);
        const bool hovered = RectContainsPoint(state->color_rects[index], state->hover_point);
        const auto chip_rect = D2D1::RoundedRect(
            D2D1::RectF(
                static_cast<float>(state->color_rects[index].left),
                static_cast<float>(state->color_rects[index].top),
                static_cast<float>(state->color_rects[index].right),
                static_cast<float>(state->color_rects[index].bottom)),
            10.0f,
            10.0f);
        target->FillRoundedRectangle(
            chip_rect,
            selected
                ? state->d2d_shell.accent_soft_brush()
                : (hovered ? state->d2d_shell.surface_hover_brush()
                           : state->d2d_shell.surface_alt_brush()));
        target->DrawRoundedRectangle(
            chip_rect,
            selected ? state->d2d_shell.accent_brush() : state->d2d_shell.border_brush(),
            1.0f);
        target->DrawTextW(
            state->config.color_options[index].label.c_str(),
            static_cast<UINT32>(state->config.color_options[index].label.size()),
            state->d2d_shell.tiny_format(),
            D2D1::RectF(
                static_cast<float>(state->color_rects[index].left),
                static_cast<float>(state->color_rects[index].top + 4),
                static_cast<float>(state->color_rects[index].right),
                static_cast<float>(state->color_rects[index].bottom)),
            selected ? state->d2d_shell.accent_brush() : state->d2d_shell.text_brush(),
            D2D1_DRAW_TEXT_OPTIONS_CLIP);
      }
    }

    // 绘制确定/取消按钮
    const auto draw_button = [&](const RECT& button_rect, const wchar_t* text, bool primary) {
      const bool hovered = RectContainsPoint(button_rect, state->hover_point);
      const auto rounded = D2D1::RoundedRect(
          D2D1::RectF(
              static_cast<float>(button_rect.left),
              static_cast<float>(button_rect.top),
              static_cast<float>(button_rect.right),
              static_cast<float>(button_rect.bottom)),
          10.0f,
          10.0f);
      target->FillRoundedRectangle(
          rounded,
          primary
              ? state->d2d_shell.accent_soft_brush()
              : (hovered ? state->d2d_shell.surface_hover_brush()
                         : state->d2d_shell.surface_alt_brush()));
      target->DrawRoundedRectangle(
          rounded,
          primary ? state->d2d_shell.accent_brush() : state->d2d_shell.border_brush(),
          1.0f);
      target->DrawTextW(
          text,
          static_cast<UINT32>(wcslen(text)),
          state->d2d_shell.small_format(),
          D2D1::RectF(
              static_cast<float>(button_rect.left),
              static_cast<float>(button_rect.top + 5),
              static_cast<float>(button_rect.right),
              static_cast<float>(button_rect.bottom)),
          primary ? state->d2d_shell.accent_brush() : state->d2d_shell.text_brush(),
          D2D1_DRAW_TEXT_OPTIONS_CLIP);
    };

    draw_button(state->confirm_rect, L"\u786e\u5b9a", true);
    draw_button(state->cancel_rect, L"\u53d6\u6d88", false);
    state->d2d_shell.EndFrame();
  }
}

LRESULT CALLBACK DialogProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
  auto* state = reinterpret_cast<DialogState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  switch (message) {
    case WM_NCCREATE: {
      const auto* create = reinterpret_cast<CREATESTRUCTW*>(l_param);
      state = reinterpret_cast<DialogState*>(create->lpCreateParams);
      SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
      return TRUE;
    }
    case WM_CREATE: {
      state->hwnd = hwnd;
      state->result_color = state->config.initial_color;
      state->d2d_ready = state->d2d_shell.Initialize(hwnd).ok();
      if (state->d2d_ready) {
        state->d2d_shell.EnsureSecondaryTextFormats();

        // 初始化 D2D 无窗口文本框
        state->name_textbox.Initialize(state->d2d_shell.dwrite_factory());
        state->name_textbox.SetDrawBorder(false);
        state->name_textbox.SetDrawKnob(false);
        state->name_textbox.SetDrawCloseButton(false);
        state->name_textbox.SetFontSize(13.0f);
        state->name_textbox.SetFontFamily(L"Segoe UI");
        state->name_textbox.SetTextColor(state->d2d_shell.ColorText());
        state->name_textbox.SetPlaceholderColor(state->d2d_shell.ColorSubtleText());
        state->name_textbox.SetSelectionColor(D2D1::ColorF(0.23f, 0.48f, 1.0f, 0.25f));
        state->name_textbox.SetBorderColor(state->d2d_shell.ColorAccent());
        state->name_textbox.SetText(state->config.initial_text);
        // 按 Enter 时触发确认
        state->name_textbox.SetOnEnterPressed([state]() {
          PostMessageW(state->hwnd, WM_COMMAND, MAKEWPARAM(kIdOk, 0), 0);
        });
        state->name_textbox.SetFocused(true, hwnd);
        // 启动光标闪烁定时器
        SetTimer(hwnd, kCaretTimerId, 500, nullptr);
      }
      LayoutDialog(state);
      CenterToOwner(hwnd, state->owner);
      ApplyDialogRoundedRegion(hwnd);
      SetForegroundWindow(hwnd);
      SetActiveWindow(hwnd);
      SetFocus(hwnd);
      return 0;
    }
    case WM_TIMER:
      if (w_param == kCaretTimerId) {
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      return 0;
    case WM_SIZE:
      ApplyDialogRoundedRegion(hwnd);
      LayoutDialog(state);
      return 0;
    case WM_MOUSEMOVE: {
      if (state == nullptr) return 0;
      const POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
      state->hover_point = point;
      state->name_textbox.HandleMouseInput(
          hwnd, message,
          D2D1::Point2F(static_cast<float>(point.x), static_cast<float>(point.y)));
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    }
    case WM_LBUTTONDOWN: {
      if (state == nullptr) return 0;
      const POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
      state->name_textbox.HandleMouseInput(
          hwnd, message,
          D2D1::Point2F(static_cast<float>(point.x), static_cast<float>(point.y)));
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    }
    case WM_LBUTTONDBLCLK: {
      if (state == nullptr) return 0;
      const POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
      state->name_textbox.HandleMouseInput(
          hwnd, message,
          D2D1::Point2F(static_cast<float>(point.x), static_cast<float>(point.y)));
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    }
    case WM_LBUTTONUP: {
      if (state == nullptr) return 0;
      const POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
      state->name_textbox.HandleMouseInput(
          hwnd, message,
          D2D1::Point2F(static_cast<float>(point.x), static_cast<float>(point.y)));
      // 检查按钮点击
      const auto hit = HitTest(state, point);
      if (hit.has_value()) {
        switch (hit->kind) {
          case HitConfirm:
            PostMessageW(hwnd, WM_COMMAND, MAKEWPARAM(kIdOk, 0), 0);
            return 0;
          case HitCancel:
            PostMessageW(hwnd, WM_COMMAND, MAKEWPARAM(kIdCancel, 0), 0);
            return 0;
          case HitColor:
            if (hit->index >= 0 &&
                hit->index < static_cast<int>(state->color_rects.size())) {
              state->result_color =
                  state->config.color_options[static_cast<size_t>(hit->index)].value;
              InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
          case HitNone:
            return 0;
        }
      }
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    }
    case WM_KEYDOWN:
      if (state == nullptr) return DefWindowProcW(hwnd, message, w_param, l_param);
      if (w_param == VK_ESCAPE) {
        FinishDialog(state, false);
        return 0;
      }
      if (state->name_textbox.HandleMessage(hwnd, message, w_param, l_param)) {
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      return DefWindowProcW(hwnd, message, w_param, l_param);
    case WM_CHAR:
      if (state == nullptr) return 0;
      if (w_param == L'\x1B') return 0;  // Escape 在 WM_KEYDOWN 已处理
      if (state->name_textbox.HandleMessage(hwnd, message, w_param, l_param)) {
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      return 0;
    case WM_IME_COMPOSITION:
    case WM_IME_STARTCOMPOSITION:
    case WM_IME_ENDCOMPOSITION:
      if (state != nullptr) {
        const bool handled =
            state->name_textbox.HandleMessage(hwnd, message, w_param, l_param);
        InvalidateRect(hwnd, nullptr, FALSE);
        if (handled) return 0;
      }
      return DefWindowProcW(hwnd, message, w_param, l_param);
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps{};
      BeginPaint(hwnd, &ps);
      DrawDialog(state);
      EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_COMMAND: {
      const int id = LOWORD(w_param);
      if (id == kIdOk) {
        if (state == nullptr) return 0;
        const std::wstring trimmed = common::TrimWide(state->name_textbox.GetText());
        if (trimmed.empty()) {
          state->error_message = state->config.empty_text_error.empty()
              ? std::wstring(L"\u540d\u79f0\u4e0d\u80fd\u4e3a\u7a7a\u3002")
              : state->config.empty_text_error;
          InvalidateRect(hwnd, nullptr, FALSE);
          SetFocus(hwnd);
          return 0;
        }
        state->result_text = trimmed;
        if (state->config.color_options.empty()) {
          state->result_color.clear();
        } else if (state->result_color.empty()) {
          state->result_color = state->config.color_options.front().value;
        }
        FinishDialog(state, true);
        return 0;
      }
      if (id == kIdCancel) {
        FinishDialog(state, false);
        return 0;
      }
      break;
    }
    case WM_CLOSE:
      FinishDialog(state, false);
      return 0;
    case WM_DESTROY:
      KillTimer(hwnd, kCaretTimerId);
      if (state != nullptr) {
        state->d2d_shell.Shutdown();
        state->hwnd = nullptr;
      }
      return 0;
  }
  return DefWindowProcW(hwnd, message, w_param, l_param);
}

ATOM EnsureDialogClass(HINSTANCE instance) {
  static ATOM atom = 0;
  if (atom != 0) {
    return atom;
  }

  WNDCLASSW wc{};
  wc.lpfnWndProc = DialogProc;
  wc.hInstance = instance;
  wc.lpszClassName = kDialogClassName;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = nullptr;
  // CS_DBLCLKS 允许双击选词
  wc.style = CS_DBLCLKS;
  atom = RegisterClassW(&wc);
  return atom;
}

}  // namespace

bool ShowPanelInputDialog(
    HINSTANCE instance,
    HWND owner,
    const PanelInputDialogConfig& config,
    std::wstring* out_text,
    std::wstring* out_color) {
  if (instance == nullptr || out_text == nullptr || out_color == nullptr) {
    return false;
  }
  if (EnsureDialogClass(instance) == 0) {
    return false;
  }

  DialogState state{};
  state.instance = instance;
  state.owner = owner;
  state.config = config;

  if (owner != nullptr) {
    EnableWindow(owner, FALSE);
  }
  HWND hwnd = CreateWindowExW(
      WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
      kDialogClassName,
      L"",
      WS_POPUP,
      CW_USEDEFAULT,
      CW_USEDEFAULT,
      config.color_options.empty() ? 360 : 360,
      config.color_options.empty() ? 196 : 292,
      owner,
      nullptr,
      instance,
      &state);
  if (hwnd == nullptr) {
    if (owner != nullptr) {
      EnableWindow(owner, TRUE);
    }
    return false;
  }

  ShowWindow(hwnd, SW_SHOWNORMAL);
  UpdateWindow(hwnd);

  // 纯消息循环，无需 IsDialogMessageW（已无子 EDIT 控件）
  MSG message{};
  while (!state.done && IsWindow(hwnd) && GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessage(&message);
  }

  if (owner != nullptr) {
    EnableWindow(owner, TRUE);
    SetActiveWindow(owner);
    SetForegroundWindow(owner);
  }

  if (!state.confirmed) {
    return false;
  }
  *out_text = state.result_text;
  *out_color = state.result_color;
  return true;
}

}  // namespace clipd2d::ui
