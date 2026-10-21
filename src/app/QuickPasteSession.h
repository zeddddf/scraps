#pragma once

#include <vector>

#include <windows.h>

#include "common/Status.h"
#include "core/browser/QuickPasteQuery.h"
#include "core/clipboard/ClipboardWriter.h"
#include "core/commands/AppController.h"
#include "ui/QuickPasteWindow.h"

namespace clipd2d::app {

class QuickPasteSession {
 public:
  QuickPasteSession() = default;
  ~QuickPasteSession();

  common::Status Create(HINSTANCE instance, HWND owner, core::commands::AppController* controller);
  void Destroy();

  void Toggle();
  void Close();
  bool visible() const { return window_.visible(); }

 private:
  static LRESULT CALLBACK KeyboardHookProc(int code, WPARAM w_param, LPARAM l_param);
  static LRESULT CALLBACK MouseHookProc(int code, WPARAM w_param, LPARAM l_param);

  common::Status Open();
  common::Status EnsureWindowCreated();
  std::vector<core::browser::QuickPasteCandidate> LoadCandidates();
  POINT ResolveAnchorScreenPoint() const;
  void InstallHooks();
  void UninstallHooks();
  bool HandleKeyboardHook(WPARAM w_param, const KBDLLHOOKSTRUCT& hook);
  void HandleMouseHook(WPARAM w_param, const MSLLHOOKSTRUCT& hook);
  void AcceptCandidate(int index, bool plain_text);
  void CloseInternal();

  HINSTANCE instance_ = nullptr;
  HWND owner_ = nullptr;
  core::commands::AppController* controller_ = nullptr;
  ui::QuickPasteWindow window_;
  core::clipboard::PasteTargetContext target_context_{};
  std::vector<core::browser::QuickPasteCandidate> candidates_;
  HHOOK keyboard_hook_ = nullptr;
  HHOOK mouse_hook_ = nullptr;

  inline static QuickPasteSession* active_session_ = nullptr;
};

}  // namespace clipd2d::app
