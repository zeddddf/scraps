#include "core/clipboard/PasteTarget.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <string>

#include "common/Log.h"
#include "common/StringUtil.h"

namespace clipd2d::core::clipboard {

namespace {

std::wstring LowercaseWide(std::wstring text) {
  std::transform(
      text.begin(),
      text.end(),
      text.begin(),
      [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
      });
  return text;
}

std::wstring GetWindowClassLower(HWND window) {
  wchar_t class_name[128]{};
  GetClassNameW(window, class_name, static_cast<int>(std::size(class_name)));
  return LowercaseWide(class_name);
}

bool HasSpecificFocusedChild(const PasteTargetContext& context) {
  return IsValidPasteTargetContext(context) &&
      context.focus_window != nullptr &&
      context.focus_window != context.root_window &&
      BelongsToRootWindow(context.focus_window, context.root_window);
}

}  // namespace

HWND NormalizeRootWindow(HWND window) {
  if (window == nullptr || !IsWindow(window)) {
    return nullptr;
  }
  HWND root = GetAncestor(window, GA_ROOT);
  return (root != nullptr && IsWindow(root)) ? root : window;
}

bool BelongsToRootWindow(HWND window, HWND root_window) {
  return window != nullptr &&
      root_window != nullptr &&
      IsWindow(window) &&
      NormalizeRootWindow(window) == root_window;
}

bool IsStandardPasteControl(HWND window) {
  if (window == nullptr || !IsWindow(window)) {
    return false;
  }
  const std::wstring class_name = GetWindowClassLower(window);
  return class_name == L"edit" ||
      class_name.find(L"richedit") != std::wstring::npos ||
      class_name.find(L"msftedit") != std::wstring::npos;
}

std::wstring GetWindowProcessNameLower(HWND window) {
  DWORD process_id = 0;
  GetWindowThreadProcessId(window, &process_id);
  if (process_id == 0) {
    return {};
  }

  HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id);
  if (process == nullptr) {
    return {};
  }

  std::wstring path(4096, L'\0');
  DWORD size = static_cast<DWORD>(path.size());
  if (!QueryFullProcessImageNameW(process, 0, path.data(), &size)) {
    CloseHandle(process);
    return {};
  }
  CloseHandle(process);
  path.resize(size);

  const size_t slash = path.find_last_of(L"\\/");
  const std::wstring name = slash == std::wstring::npos ? path : path.substr(slash + 1);
  return LowercaseWide(name);
}

bool IsTerminalProcessName(const std::wstring& process_name) {
  return process_name == L"windowsterminal.exe" || process_name == L"wt.exe";
}

bool IsValidPasteTargetContext(const PasteTargetContext& context) {
  return context.root_window != nullptr && IsWindow(context.root_window);
}

PasteTargetContext CapturePasteTargetContext(HWND preferred_window) {
  PasteTargetContext context{};
  HWND root_window = NormalizeRootWindow(preferred_window);
  if (root_window == nullptr) {
    root_window = NormalizeRootWindow(GetForegroundWindow());
  }
  if (root_window == nullptr) {
    return context;
  }

  context.root_window = root_window;
  context.thread_id = GetWindowThreadProcessId(root_window, nullptr);

  GUITHREADINFO info{};
  info.cbSize = sizeof(info);
  if (context.thread_id != 0 &&
      GetGUIThreadInfo(context.thread_id, &info) &&
      BelongsToRootWindow(info.hwndFocus, root_window)) {
    context.focus_window = info.hwndFocus;
  } else {
    context.focus_window = root_window;
  }
  return context;
}

PasteTargetContext RefinePasteTargetContext(const PasteTargetContext& context, const char* stage) {
  if (!IsValidPasteTargetContext(context) || HasSpecificFocusedChild(context)) {
    return context;
  }

  // 预算刻意很小（4×8ms）：这里等的是「面板隐藏后目标应用把焦点落回子控件」这一瞬间。
  // 对 XAML/UWP 宿主（记事本等）GetGUIThreadInfo 永远拿不到 hwndFocus，盲等只会白吞
  // 80ms（实测 pre-dispatch 一段吃掉 128ms）。真正的兜底是 PerformLegacyPasteShortcut
  // 里的前台确认，因此超时短也不会把内容发错窗口。
  for (int attempt = 0; attempt < 4; ++attempt) {
    const PasteTargetContext refined = CapturePasteTargetContext(context.root_window);
    if (!IsValidPasteTargetContext(refined)) {
      break;
    }
    if (HasSpecificFocusedChild(refined)) {
      common::WriteLog(
          common::LogLevel::Info,
          "clipboard.paste_target",
          std::string(stage) +
              " refined focus attempt=" + std::to_string(attempt + 1) +
              " root=" + std::to_string(reinterpret_cast<std::uintptr_t>(refined.root_window)) +
              " focus=" + std::to_string(reinterpret_cast<std::uintptr_t>(refined.focus_window)));
      return refined;
    }
    if (attempt + 1 < 4) {
      Sleep(8);
    }
  }

  common::WriteLog(
      common::LogLevel::Info,
      "clipboard.paste_target",
      std::string(stage) +
          " stayed root-only root=" + std::to_string(reinterpret_cast<std::uintptr_t>(context.root_window)) +
          " focus=" + std::to_string(reinterpret_cast<std::uintptr_t>(context.focus_window)));
  return context;
}

bool TryRestorePasteTarget(const PasteTargetContext& context) {
  if (!IsValidPasteTargetContext(context)) {
    return false;
  }
  common::WriteLog(
      common::LogLevel::Info,
      "clipboard.paste_target",
      "restore begin root=" + std::to_string(reinterpret_cast<std::uintptr_t>(context.root_window)) +
          " focus=" + std::to_string(reinterpret_cast<std::uintptr_t>(context.focus_window)) +
          " thread=" + std::to_string(context.thread_id));

  HWND target_root = context.root_window;
  if (IsIconic(target_root)) {
    ShowWindow(target_root, SW_RESTORE);
  }

  HWND foreground_window = GetForegroundWindow();
  const DWORD current_thread_id = GetCurrentThreadId();
  const DWORD target_thread_id =
      context.thread_id != 0 ? context.thread_id : GetWindowThreadProcessId(target_root, nullptr);
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

  BringWindowToTop(target_root);
  SetForegroundWindow(target_root);
  SetActiveWindow(target_root);
  if (BelongsToRootWindow(context.focus_window, target_root)) {
    SetFocus(context.focus_window);
  }

  if (attached_target) {
    AttachThreadInput(current_thread_id, target_thread_id, FALSE);
  }
  if (attached_foreground) {
    AttachThreadInput(current_thread_id, foreground_thread_id, FALSE);
  }

  for (int attempt = 0; attempt < 12; ++attempt) {
    HWND foreground_root = NormalizeRootWindow(GetForegroundWindow());
    if (foreground_root == target_root) {
      common::WriteLog(
          common::LogLevel::Info,
          "clipboard.paste_target",
          "restore success attempt=" + std::to_string(attempt + 1) +
              " foreground_root=" + std::to_string(reinterpret_cast<std::uintptr_t>(foreground_root)));
      return true;
    }
    Sleep(10);
  }
  const HWND final_foreground_root = NormalizeRootWindow(GetForegroundWindow());
  const bool matched = final_foreground_root == target_root;
  common::WriteLog(
      common::LogLevel::Info,
      "clipboard.paste_target",
      "restore end matched=" + std::string(matched ? "true" : "false") +
          " foreground_root=" + std::to_string(reinterpret_cast<std::uintptr_t>(final_foreground_root)));
  return matched;
}

}  // namespace clipd2d::core::clipboard
