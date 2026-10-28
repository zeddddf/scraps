#include "core/clipboard/PasteDispatch.h"

#include <windows.h>

#include <string>
#include <vector>

#include "common/Log.h"
#include "common/Status.h"
#include "common/Stopwatch.h"
#include "common/Utf8.h"
#include "core/clipboard/PasteTarget.h"

namespace clipd2d::core::clipboard {

namespace {

// 等用户松开 Ctrl/Shift/Alt/Win 再派发粘贴键，避免修饰键粘连污染 SendInput 序列。
// 预算 80ms：正常松手 20~50ms 足够；原 150ms 会让每次粘贴固定多等一截（实测
// dispatch 段因此接近跑满）。仍未松开也照常派发——宁可修饰键粘连，也不牺牲响应。
// 用 QPC 而非 GetTickCount：后者的 15.625ms 粒度会让 80ms 预算实际等到 94ms
// （边界判定天然向上取整），而这正是"每次粘贴固定多等一截"的来源之一。
void WaitForModifierRelease(std::int64_t timeout_us) {
  common::Stopwatch stopwatch;
  while (stopwatch.ElapsedUs() < timeout_us) {
    if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) == 0 &&
        (GetAsyncKeyState(VK_SHIFT) & 0x8000) == 0 &&
        (GetAsyncKeyState(VK_MENU) & 0x8000) == 0 &&
        (GetAsyncKeyState(VK_LWIN) & 0x8000) == 0 &&
        (GetAsyncKeyState(VK_RWIN) & 0x8000) == 0) {
      return;
    }
    Sleep(5);
  }
  common::WriteLog(
      common::LogLevel::Info,
      "clipboard.paste",
      "modifier release wait timed out after " + common::FormatMs(stopwatch.ElapsedUs()) + "ms");
}

bool TrySendWmPaste(HWND target_window) {
  if (!IsStandardPasteControl(target_window)) {
    return false;
  }
  DWORD_PTR result = 0;
  return SendMessageTimeoutW(
             target_window,
             WM_PASTE,
             0,
             0,
             SMTO_ABORTIFHUNG | SMTO_NORMAL,
             250,
             &result) != 0;
}

common::Status SendUnicodeText(const std::wstring& text) {
  if (text.empty()) {
    return common::Status::Ok();
  }

  std::vector<INPUT> inputs;
  inputs.reserve(text.size() * 2);
  for (wchar_t ch : text) {
    INPUT down{};
    down.type = INPUT_KEYBOARD;
    down.ki.dwFlags = KEYEVENTF_UNICODE;
    down.ki.wScan = ch;
    inputs.push_back(down);

    INPUT up = down;
    up.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
    inputs.push_back(up);
  }

  if (SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT)) != inputs.size()) {
    return common::Status::Error(common::StatusCode::PlatformError, "Failed to send unicode text.");
  }
  return common::Status::Ok();
}

common::Status SendPasteInputSequence(bool use_shift_insert, bool use_terminal_paste, std::string_view text_fallback) {
  if (use_terminal_paste && !text_fallback.empty()) {
    return SendUnicodeText(common::Utf8ToWide(text_fallback).wide);
  }

  INPUT inputs[6]{};
  UINT input_count = 0;
  if (use_terminal_paste) {
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_CONTROL;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = VK_SHIFT;
    inputs[2].type = INPUT_KEYBOARD;
    inputs[2].ki.wVk = 'V';
    inputs[3].type = INPUT_KEYBOARD;
    inputs[3].ki.wVk = 'V';
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[4].type = INPUT_KEYBOARD;
    inputs[4].ki.wVk = VK_SHIFT;
    inputs[4].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[5].type = INPUT_KEYBOARD;
    inputs[5].ki.wVk = VK_CONTROL;
    inputs[5].ki.dwFlags = KEYEVENTF_KEYUP;
    input_count = 6;
  } else if (use_shift_insert) {
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_SHIFT;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = VK_INSERT;
    inputs[2].type = INPUT_KEYBOARD;
    inputs[2].ki.wVk = VK_INSERT;
    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[3].type = INPUT_KEYBOARD;
    inputs[3].ki.wVk = VK_SHIFT;
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
    input_count = 4;
  } else {
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_CONTROL;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = 'V';
    inputs[2].type = INPUT_KEYBOARD;
    inputs[2].ki.wVk = 'V';
    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[3].type = INPUT_KEYBOARD;
    inputs[3].ki.wVk = VK_CONTROL;
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
    input_count = 4;
  }

  if (SendInput(input_count, inputs, sizeof(INPUT)) != input_count) {
    return common::Status::Error(common::StatusCode::PlatformError, "Send paste shortcut failed.");
  }
  return common::Status::Ok();
}

common::Status PerformLegacyPasteShortcut(HWND target_window, std::string_view text_fallback) {
  HWND foreground_window = GetForegroundWindow();
  const std::wstring target_process_name =
      (target_window != nullptr && IsWindow(target_window)) ? GetWindowProcessNameLower(target_window) : std::wstring{};
  const bool use_terminal_paste = IsTerminalProcessName(target_process_name);
  const bool use_shift_insert =
      !use_terminal_paste && target_window != nullptr && IsStandardPasteControl(target_window);

  if (target_window != nullptr && IsWindow(target_window)) {
    if (IsIconic(target_window)) {
      ShowWindow(target_window, SW_RESTORE);
    }

    if (foreground_window != target_window) {
      const DWORD current_thread_id = GetCurrentThreadId();
      const DWORD target_thread_id = GetWindowThreadProcessId(target_window, nullptr);
      const DWORD foreground_thread_id =
          foreground_window == nullptr ? 0 : GetWindowThreadProcessId(foreground_window, nullptr);

      bool attached_foreground = false;
      bool attached_target = false;
      if (foreground_thread_id != 0 && foreground_thread_id != current_thread_id) {
        attached_foreground = AttachThreadInput(current_thread_id, foreground_thread_id, TRUE) == TRUE;
      }
      if (target_thread_id != 0 &&
          target_thread_id != current_thread_id &&
          target_thread_id != foreground_thread_id) {
        attached_target = AttachThreadInput(current_thread_id, target_thread_id, TRUE) == TRUE;
      }

      BringWindowToTop(target_window);
      SetForegroundWindow(target_window);

      if (attached_target) {
        AttachThreadInput(current_thread_id, target_thread_id, FALSE);
      }
      if (attached_foreground) {
        AttachThreadInput(current_thread_id, foreground_thread_id, FALSE);
      }
    }

    for (int attempt = 0; attempt < 12; ++attempt) {
      foreground_window = GetForegroundWindow();
      if (foreground_window == target_window) {
        break;
      }
      Sleep(10);
    }
  } else {
    target_window = foreground_window;
  }

  WaitForModifierRelease(80000);
  Sleep(use_terminal_paste ? 25 : 10);
  return SendPasteInputSequence(use_shift_insert, use_terminal_paste, text_fallback);
}

common::Status DispatchPasteToContext(const PasteTargetContext& context, std::string_view text_fallback) {
  // Always use PerformLegacyPasteShortcut (Ctrl+V): 100% reliable across processes,
  // unlike WM_PASTE which is flaky for child/foreign controls.
  return PerformLegacyPasteShortcut(context.root_window, text_fallback);
}

}  // namespace

common::Status PerformPasteToTarget(
    const PasteTargetContext& requested_context,
    PasteRoute route,
    std::string_view text_fallback) {
  PasteTargetContext context = requested_context;
  if (!IsValidPasteTargetContext(context)) {
    context = CapturePasteTargetContext();
  }
  if (!IsValidPasteTargetContext(context)) {
    return common::Status::Error(common::StatusCode::Unavailable, "Paste target unavailable.");
  }
  context = RefinePasteTargetContext(context, "pre-dispatch");
  common::WriteLog(
      common::LogLevel::Info,
      "clipboard.paste_target",
      "perform begin requested_root=" + std::to_string(reinterpret_cast<std::uintptr_t>(requested_context.root_window)) +
          " actual_root=" + std::to_string(reinterpret_cast<std::uintptr_t>(context.root_window)) +
          " actual_focus=" + std::to_string(reinterpret_cast<std::uintptr_t>(context.focus_window)) +
          " route=" + std::to_string(static_cast<int>(route)));

  const HWND foreground_root = NormalizeRootWindow(GetForegroundWindow());

  auto try_fast_dispatch = [&]() -> common::Status {
      if (foreground_root != context.root_window) {
        return common::Status::Error(common::StatusCode::Unavailable, "Fast paste target changed.");
      }
      return DispatchPasteToContext(context, text_fallback);
    };

  if (route == PasteRoute::Fast) {
    const common::Status fast_status = try_fast_dispatch();
    if (fast_status.ok()) {
      return fast_status;
    }
    route = PasteRoute::Recovery;
  }

  if (!TryRestorePasteTarget(context)) {
    context = CapturePasteTargetContext(context.root_window);
    if (!TryRestorePasteTarget(context)) {
      return common::Status::Error(common::StatusCode::Unavailable, "Restore paste target failed.");
    }
  }

  context = RefinePasteTargetContext(context, "post-restore");

  return DispatchPasteToContext(context, text_fallback);
}

}  // namespace clipd2d::core::clipboard
