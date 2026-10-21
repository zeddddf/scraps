#include "ui/PanelConfirmDialog.h"

#include <dwmapi.h>
#include <windowsx.h>

#include "ui/D2dShell.h"

namespace clipd2d::ui {

namespace {

constexpr wchar_t kDialogClassName[] = L"Scraps.PanelConfirmDialog";
constexpr int kIdConfirm = 1;
constexpr int kIdCancel = 2;
constexpr DWORD DWMWA_WINDOW_CORNER_PREFERENCE_VALUE = 33;
constexpr int kDwmCornerRoundValue = 2;

enum HitKind {
  HitNone = 0,
  HitConfirm = 1,
  HitCancel = 2,
};

struct DialogState {
  HINSTANCE instance = nullptr;
  HWND owner = nullptr;
  HWND hwnd = nullptr;
  D2dShell d2d_shell;
  bool d2d_ready = false;
  PanelConfirmDialogConfig config;
  bool confirmed = false;
  bool done = false;
  RECT message_rect{};
  RECT confirm_rect{};
  RECT cancel_rect{};
  POINT hover_point{-1, -1};
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
  constexpr int kButtonWidth = 96;
  constexpr int kButtonHeight = 32;
  constexpr int kButtonGap = 10;

  state->message_rect = RECT{kMargin, 52, client.right - kMargin, client.bottom - 66};
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
}

int HitTest(DialogState* state, POINT point) {
  if (state == nullptr) {
    return HitNone;
  }
  if (RectContainsPoint(state->confirm_rect, point)) {
    return HitConfirm;
  }
  if (RectContainsPoint(state->cancel_rect, point)) {
    return HitCancel;
  }
  return HitNone;
}

void DrawDialog(DialogState* state) {
  if (state == nullptr || !state->d2d_ready) {
    return;
  }
  RECT client{};
  GetClientRect(state->hwnd, &client);
  if (auto* target = state->d2d_shell.BeginFrame()) {
    const D2D1_RECT_F rect = D2D1::RectF(
        static_cast<float>(client.left),
        static_cast<float>(client.top),
        static_cast<float>(client.right),
        static_cast<float>(client.bottom));
    target->Clear(D2D1::ColorF(0xFFFFFF));
    target->FillRectangle(rect, state->d2d_shell.surface_brush());
    target->DrawRectangle(
        D2D1::RectF(rect.left + 0.5f, rect.top + 0.5f, rect.right - 0.5f, rect.bottom - 0.5f),
        state->d2d_shell.border_brush(),
        1.0f);

    const D2D1_RECT_F title_rect = D2D1::RectF(18.0f, 16.0f, rect.right - 18.0f, 40.0f);
    target->DrawTextW(
        state->config.title.c_str(),
        static_cast<UINT32>(state->config.title.size()),
        state->d2d_shell.section_format(),
        title_rect,
        state->d2d_shell.text_brush(),
        D2D1_DRAW_TEXT_OPTIONS_CLIP);

    target->DrawTextW(
        state->config.message.c_str(),
        static_cast<UINT32>(state->config.message.size()),
        state->d2d_shell.small_format(),
        D2D1::RectF(
            static_cast<float>(state->message_rect.left),
            static_cast<float>(state->message_rect.top),
            static_cast<float>(state->message_rect.right),
            static_cast<float>(state->message_rect.bottom)),
        state->d2d_shell.muted_text_brush(),
        D2D1_DRAW_TEXT_OPTIONS_NONE);

    const auto draw_button = [&](const RECT& button_rect, const std::wstring& text, bool primary, bool dangerous) {
      const bool hovered = RectContainsPoint(button_rect, state->hover_point);
      const auto rounded = D2D1::RoundedRect(
          D2D1::RectF(
              static_cast<float>(button_rect.left),
              static_cast<float>(button_rect.top),
              static_cast<float>(button_rect.right),
              static_cast<float>(button_rect.bottom)),
          10.0f,
          10.0f);
      ID2D1SolidColorBrush* fill_brush = nullptr;
      ID2D1SolidColorBrush* border_brush = nullptr;
      ID2D1SolidColorBrush* text_brush = nullptr;
      if (primary) {
        fill_brush = dangerous ? state->d2d_shell.danger_brush() : state->d2d_shell.accent_soft_brush();
        border_brush = dangerous ? state->d2d_shell.danger_brush() : state->d2d_shell.accent_brush();
        text_brush = dangerous ? state->d2d_shell.white_brush() : state->d2d_shell.accent_brush();
      } else {
        fill_brush = hovered ? state->d2d_shell.surface_hover_brush() : state->d2d_shell.surface_alt_brush();
        border_brush = state->d2d_shell.border_brush();
        text_brush = state->d2d_shell.text_brush();
      }
      target->FillRoundedRectangle(rounded, fill_brush);
      target->DrawRoundedRectangle(rounded, border_brush, 1.0f);
      target->DrawTextW(
          text.c_str(),
          static_cast<UINT32>(text.size()),
          state->d2d_shell.small_format(),
          D2D1::RectF(
              static_cast<float>(button_rect.left),
              static_cast<float>(button_rect.top + 5),
              static_cast<float>(button_rect.right),
              static_cast<float>(button_rect.bottom)),
          text_brush,
          D2D1_DRAW_TEXT_OPTIONS_CLIP);
    };

    draw_button(
        state->confirm_rect,
        state->config.confirm_text.empty() ? std::wstring(L"\u786e\u5b9a") : state->config.confirm_text,
        true,
        state->config.confirm_is_dangerous);
    draw_button(
        state->cancel_rect,
        state->config.cancel_text.empty() ? std::wstring(L"\u53d6\u6d88") : state->config.cancel_text,
        false,
        false);
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
    case WM_CREATE:
      if (state == nullptr) {
        return -1;
      }
      state->hwnd = hwnd;
      state->d2d_ready = state->d2d_shell.Initialize(hwnd).ok();
      if (state->d2d_ready) {
        state->d2d_shell.EnsureSecondaryTextFormats();
      }
      LayoutDialog(state);
      CenterToOwner(hwnd, state->owner);
      ApplyDialogRoundedRegion(hwnd);
      SetForegroundWindow(hwnd);
      SetActiveWindow(hwnd);
      return 0;
    case WM_SIZE:
      ApplyDialogRoundedRegion(hwnd);
      LayoutDialog(state);
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    case WM_MOUSEMOVE:
      if (state != nullptr) {
        state->hover_point = POINT{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      return 0;
    case WM_LBUTTONUP:
      switch (HitTest(state, POINT{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)})) {
        case HitConfirm:
          PostMessageW(hwnd, WM_COMMAND, MAKEWPARAM(kIdConfirm, 0), 0);
          return 0;
        case HitCancel:
          PostMessageW(hwnd, WM_COMMAND, MAKEWPARAM(kIdCancel, 0), 0);
          return 0;
        default:
          return 0;
      }
    case WM_KEYDOWN:
      if (w_param == VK_RETURN) {
        PostMessageW(hwnd, WM_COMMAND, MAKEWPARAM(kIdConfirm, 0), 0);
        return 0;
      }
      if (w_param == VK_ESCAPE) {
        PostMessageW(hwnd, WM_COMMAND, MAKEWPARAM(kIdCancel, 0), 0);
        return 0;
      }
      break;
    case WM_ERASEBKGND:
      return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps{};
      BeginPaint(hwnd, &ps);
      DrawDialog(state);
      EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_COMMAND:
      if (LOWORD(w_param) == kIdConfirm) {
        FinishDialog(state, true);
        return 0;
      }
      if (LOWORD(w_param) == kIdCancel) {
        FinishDialog(state, false);
        return 0;
      }
      break;
    case WM_CLOSE:
      FinishDialog(state, false);
      return 0;
    case WM_DESTROY:
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
  atom = RegisterClassW(&wc);
  return atom;
}

}  // namespace

bool ShowPanelConfirmDialog(
    HINSTANCE instance,
    HWND owner,
    const PanelConfirmDialogConfig& config) {
  if (instance == nullptr) {
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
      380,
      220,
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

  MSG message{};
  while (!state.done && IsWindow(hwnd) && GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }

  if (owner != nullptr) {
    EnableWindow(owner, TRUE);
    SetActiveWindow(owner);
    SetForegroundWindow(owner);
  }

  return state.confirmed;
}

}  // namespace clipd2d::ui
