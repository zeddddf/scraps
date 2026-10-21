#include "app/QuickPasteSession.h"

#include <winuser.h>

#include <algorithm>
#include <optional>

#include "common/Log.h"
#include "common/StringUtil.h"

namespace clipd2d::app {

namespace {

constexpr UINT kQuickPasteCandidateLimit = 9;

bool IsModifierVirtualKey(DWORD virtual_key) {
  return virtual_key == VK_SHIFT || virtual_key == VK_LSHIFT || virtual_key == VK_RSHIFT ||
      virtual_key == VK_CONTROL || virtual_key == VK_LCONTROL || virtual_key == VK_RCONTROL ||
      virtual_key == VK_MENU || virtual_key == VK_LMENU || virtual_key == VK_RMENU ||
      virtual_key == VK_LWIN || virtual_key == VK_RWIN;
}

bool IsDigitSelectionKey(DWORD virtual_key, int* candidate_index) {
  if (candidate_index == nullptr) {
    return false;
  }
  if (virtual_key >= '1' && virtual_key <= '9') {
    *candidate_index = static_cast<int>(virtual_key - '1');
    return true;
  }
  if (virtual_key >= VK_NUMPAD1 && virtual_key <= VK_NUMPAD9) {
    *candidate_index = static_cast<int>(virtual_key - VK_NUMPAD1);
    return true;
  }
  return false;
}

bool IsSystemKeyMessage(WPARAM message) {
  return message == WM_SYSKEYDOWN || message == WM_SYSKEYUP;
}

}  // namespace

QuickPasteSession::~QuickPasteSession() {
  Destroy();
}

common::Status QuickPasteSession::Create(HINSTANCE instance, HWND owner, core::commands::AppController* controller) {
  instance_ = instance;
  owner_ = owner;
  controller_ = controller;
  return common::Status::Ok();
}

void QuickPasteSession::Destroy() {
  CloseInternal();
  window_.Destroy();
  controller_ = nullptr;
}

void QuickPasteSession::Toggle() {
  if (visible()) {
    Close();
    return;
  }
  const common::Status status = Open();
  if (!status.ok()) {
    common::WriteLog(common::LogLevel::Warning, "quick_paste.open", status.message);
  }
}

void QuickPasteSession::Close() {
  CloseInternal();
}

common::Status QuickPasteSession::Open() {
  if (controller_ == nullptr) {
    return common::Status::Error(common::StatusCode::Unavailable, "Quick paste controller is not ready.");
  }
  common::Status status = EnsureWindowCreated();
  if (!status.ok()) {
    return status;
  }
  candidates_ = LoadCandidates();
  if (candidates_.empty()) {
    return common::Status::Error(common::StatusCode::NotFound, "Quick paste has no history candidates.");
  }

  target_context_ = core::clipboard::CapturePasteTargetContext();
  const POINT anchor = ResolveAnchorScreenPoint();

  ui::QuickPasteWindow::HostActions actions;
  actions.on_accept = [this](int index, bool plain_text) {
    AcceptCandidate(index, plain_text);
  };
  actions.on_close = [this]() {
    CloseInternal();
  };
  auto blur_type = controller_->current_settings().window_blur_type;
  int blur_opacity = controller_->current_settings().window_blur_opacity;
  window_.Show(anchor, candidates_, 0, blur_type, blur_opacity, std::move(actions));
  InstallHooks();
  return common::Status::Ok();
}

common::Status QuickPasteSession::EnsureWindowCreated() {
  if (window_.hwnd() != nullptr) {
    return common::Status::Ok();
  }
  return window_.Create(instance_, owner_);
}

std::vector<core::browser::QuickPasteCandidate> QuickPasteSession::LoadCandidates() {
  std::vector<core::store::ClipSummary> summaries;
  core::store::ClipQuery query;
  query.view = core::store::ClipView::All;
  if (controller_->ListClips(query, 0, kQuickPasteCandidateLimit, &summaries).ok()) {
    return core::browser::BuildQuickPasteCandidates(summaries);
  }
  return {};
}

POINT QuickPasteSession::ResolveAnchorScreenPoint() const {
  POINT anchor{};
  DWORD foreground_thread = 0;
  if (HWND foreground = GetForegroundWindow(); foreground != nullptr) {
    foreground_thread = GetWindowThreadProcessId(foreground, nullptr);
  }
  if (foreground_thread != 0) {
    GUITHREADINFO gui_info{};
    gui_info.cbSize = sizeof(gui_info);
    if (GetGUIThreadInfo(foreground_thread, &gui_info)) {
      const bool has_caret_rect =
          gui_info.rcCaret.left != gui_info.rcCaret.right ||
          gui_info.rcCaret.top != gui_info.rcCaret.bottom;
      if (gui_info.hwndCaret != nullptr && has_caret_rect) {
        RECT caret_rect = gui_info.rcCaret;
        MapWindowPoints(gui_info.hwndCaret, HWND_DESKTOP, reinterpret_cast<POINT*>(&caret_rect), 2);
        anchor.x = caret_rect.left;
        anchor.y = caret_rect.bottom;
        return anchor;
      }
    }

    const DWORD current_thread = GetCurrentThreadId();
    if (AttachThreadInput(current_thread, foreground_thread, TRUE)) {
      POINT caret_point{};
      if (GetCaretPos(&caret_point)) {
        GUITHREADINFO attached_info{};
        attached_info.cbSize = sizeof(attached_info);
        if (GetGUIThreadInfo(foreground_thread, &attached_info)) {
          if (attached_info.hwndCaret != nullptr) {
            HWND reference_window = attached_info.hwndCaret;
            MapWindowPoints(reference_window, HWND_DESKTOP, &caret_point, 1);
            anchor = caret_point;
            AttachThreadInput(current_thread, foreground_thread, FALSE);
            return anchor;
          }
        }
      }
      AttachThreadInput(current_thread, foreground_thread, FALSE);
    }
  }

  if (!GetCursorPos(&anchor)) {
    anchor.x = 0;
    anchor.y = 0;
  }
  return anchor;
}

void QuickPasteSession::InstallHooks() {
  if (active_session_ != nullptr && active_session_ != this) {
    active_session_->CloseInternal();
  }
  active_session_ = this;

  if (keyboard_hook_ == nullptr) {
    keyboard_hook_ = SetWindowsHookExW(WH_KEYBOARD_LL, &QuickPasteSession::KeyboardHookProc, instance_, 0);
  }
  if (mouse_hook_ == nullptr) {
    mouse_hook_ = SetWindowsHookExW(WH_MOUSE_LL, &QuickPasteSession::MouseHookProc, instance_, 0);
  }
}

void QuickPasteSession::UninstallHooks() {
  if (keyboard_hook_ != nullptr) {
    UnhookWindowsHookEx(keyboard_hook_);
    keyboard_hook_ = nullptr;
  }
  if (mouse_hook_ != nullptr) {
    UnhookWindowsHookEx(mouse_hook_);
    mouse_hook_ = nullptr;
  }
  if (active_session_ == this) {
    active_session_ = nullptr;
  }
}

bool QuickPasteSession::HandleKeyboardHook(WPARAM w_param, const KBDLLHOOKSTRUCT& hook) {
  if (!window_.visible()) {
    return false;
  }
  if (w_param != WM_KEYDOWN && w_param != WM_SYSKEYDOWN) {
    return false;
  }
  if ((hook.flags & LLKHF_INJECTED) != 0) {
    return false;
  }

  const bool shift_pressed = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
  const bool control_pressed = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
  const bool alt_pressed = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
  const bool win_pressed =
      (GetAsyncKeyState(VK_LWIN) & 0x8000) != 0 ||
      (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0;

  if (IsModifierVirtualKey(hook.vkCode)) {
    return false;
  }

  if (hook.vkCode == VK_ESCAPE) {
    PostMessageW(window_.hwnd(), ui::QuickPasteWindow::kCloseWindowMessage, 0, 0);
    return true;
  }
  if (hook.vkCode == VK_UP) {
    PostMessageW(window_.hwnd(), ui::QuickPasteWindow::kMoveSelectionMessage, static_cast<WPARAM>(-1), 0);
    return true;
  }
  if (hook.vkCode == VK_DOWN) {
    PostMessageW(window_.hwnd(), ui::QuickPasteWindow::kMoveSelectionMessage, static_cast<WPARAM>(1), 0);
    return true;
  }
  if (hook.vkCode == VK_RETURN) {
    PostMessageW(window_.hwnd(), ui::QuickPasteWindow::kAcceptCurrentMessage, 0, shift_pressed ? 1 : 0);
    return true;
  }

  int candidate_index = -1;
  if (!control_pressed && !alt_pressed && !win_pressed && !shift_pressed &&
      IsDigitSelectionKey(hook.vkCode, &candidate_index)) {
    PostMessageW(window_.hwnd(), ui::QuickPasteWindow::kAcceptIndexMessage, static_cast<WPARAM>(candidate_index), 0);
    return true;
  }

  PostMessageW(window_.hwnd(), ui::QuickPasteWindow::kCloseWindowMessage, 0, 0);
  return false;
}

void QuickPasteSession::HandleMouseHook(WPARAM w_param, const MSLLHOOKSTRUCT& hook) {
  if (!window_.visible()) {
    return;
  }

  switch (w_param) {
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
      if (!window_.ContainsScreenPoint(hook.pt)) {
        PostMessageW(window_.hwnd(), ui::QuickPasteWindow::kCloseWindowMessage, 0, 0);
      }
      break;
    default:
      break;
  }
}

void QuickPasteSession::AcceptCandidate(int index, bool plain_text) {
  if (controller_ == nullptr || index < 0 || index >= static_cast<int>(candidates_.size())) {
    CloseInternal();
    return;
  }

  const auto target_context = target_context_;
  const std::int64_t clip_id = candidates_[index].clip_id;
  const bool supports_plain_text = candidates_[index].supports_plain_text_paste;
  CloseInternal();

  common::Status status;
  if (plain_text && supports_plain_text) {
    status = controller_->PastePlainTextById(clip_id, target_context, core::clipboard::PasteRoute::Recovery);
  } else {
    status = controller_->PasteById(clip_id, target_context, core::clipboard::PasteRoute::Recovery);
  }
  if (!status.ok()) {
    common::WriteLog(common::LogLevel::Warning, "quick_paste.accept", status.message);
  }
}

void QuickPasteSession::CloseInternal() {
  UninstallHooks();
  window_.Hide();
  candidates_.clear();
  target_context_ = {};
}

LRESULT CALLBACK QuickPasteSession::KeyboardHookProc(int code, WPARAM w_param, LPARAM l_param) {
  if (code < 0 || active_session_ == nullptr) {
    return CallNextHookEx(nullptr, code, w_param, l_param);
  }

  const auto* hook = reinterpret_cast<const KBDLLHOOKSTRUCT*>(l_param);
  if (hook != nullptr && active_session_->HandleKeyboardHook(w_param, *hook)) {
    return 1;
  }
  return CallNextHookEx(nullptr, code, w_param, l_param);
}

LRESULT CALLBACK QuickPasteSession::MouseHookProc(int code, WPARAM w_param, LPARAM l_param) {
  if (code < 0 || active_session_ == nullptr) {
    return CallNextHookEx(nullptr, code, w_param, l_param);
  }

  const auto* hook = reinterpret_cast<const MSLLHOOKSTRUCT*>(l_param);
  if (hook != nullptr) {
    active_session_->HandleMouseHook(w_param, *hook);
  }
  return CallNextHookEx(nullptr, code, w_param, l_param);
}

}  // namespace clipd2d::app
