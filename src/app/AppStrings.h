#pragma once

// Central user-facing strings for surfaces that live outside src/ui
// (tray menu, notifications, CLI-adjacent messages).
//
// src/ui keeps its own literals for now; the migration plan and full inventory
// live in docs/plans/i18n.md. New UI surfaces should add their labels here so
// that a future locale switch only touches this file.

#include <string>

#include "platform/win32/NotifyIconHost.h"

namespace clipd2d::app::l10n {

struct TrayLabels {
  std::wstring show;
  std::wstring pause_monitoring;
  std::wstring resume_monitoring;
  std::wstring settings;
  std::wstring clear_history;
  std::wstring exit;
  std::wstring tip_monitoring;
  std::wstring tip_paused;
};

inline TrayLabels TrayLabelsZhCn() {
  return TrayLabels{
      .show = L"显示主窗口",
      .pause_monitoring = L"暂停监听",
      .resume_monitoring = L"恢复监听",
      .settings = L"设置",
      .clear_history = L"清空历史",
      .exit = L"退出",
      .tip_monitoring = L"Scraps - 正在监听",
      .tip_paused = L"Scraps - 已暂停",
  };
}

inline TrayLabels TrayLabelsEnUs() {
  return TrayLabels{
      .show = L"Show main window",
      .pause_monitoring = L"Pause monitoring",
      .resume_monitoring = L"Resume monitoring",
      .settings = L"Settings",
      .clear_history = L"Clear history",
      .exit = L"Exit",
      .tip_monitoring = L"Scraps - monitoring",
      .tip_paused = L"Scraps - paused",
  };
}

// Single selection point; swap here (or wire to settings) to change locale.
inline TrayLabels TrayLabels() {
  return TrayLabelsZhCn();
}

// Raw-struct bridge for the platform layer (no std::wstring members).
inline platform::win32::TrayLabels TrayLabelsRaw() {
  return platform::win32::TrayLabels{
      L"显示主窗口", L"暂停监听", L"恢复监听", L"设置",
      L"清空历史", L"退出", L"Scraps - 正在监听", L"Scraps - 已暂停",
  };
}

struct UpdateMessages {
  std::wstring balloon_title;
  std::wstring balloon_text_suffix;  // appended after "vX.Y.Z "
};

inline UpdateMessages UpdateMessagesZhCn() {
  return UpdateMessages{
      .balloon_title = L"Scraps 有新版本",
      .balloon_text_suffix = L" 已发布，点击此通知打开下载页。",
  };
}

inline UpdateMessages UpdateMessages() {
  return UpdateMessagesZhCn();
}

}  // namespace clipd2d::app::l10n
