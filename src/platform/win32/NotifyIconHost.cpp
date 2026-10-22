#include "platform/win32/NotifyIconHost.h"

#include <shellapi.h>

#include "common/AppIcon.h"

namespace clipd2d::platform::win32 {

NotifyIconHost::~NotifyIconHost() {
  Shutdown();
}

common::Status NotifyIconHost::Initialize(HWND owner, const TrayLabels& labels) {
  owner_ = owner;
  labels_ = labels;
  data_.cbSize = sizeof(data_);
  data_.hWnd = owner_;
  data_.uID = 1;
  data_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  data_.uCallbackMessage = kTrayMessage;
  data_.hIcon = clipd2d::common::LoadAppIcon();
  wcscpy_s(data_.szTip, ARRAYSIZE(data_.szTip), L"Scraps");

  if (!Shell_NotifyIconW(NIM_ADD, &data_)) {
    return common::Status::Error(common::StatusCode::PlatformError, "创建托盘图标失败。");
  }
  return common::Status::Ok();
}

void NotifyIconHost::Shutdown() {
  if (owner_ != nullptr) {
    Shell_NotifyIconW(NIM_DELETE, &data_);
    if (data_.hIcon != nullptr) {
      DestroyIcon(data_.hIcon);
      data_.hIcon = nullptr;
    }
    owner_ = nullptr;
  }
}

void NotifyIconHost::UpdateMonitoringState(bool enabled) {
  if (owner_ == nullptr) {
    return;
  }
  wcscpy_s(
      data_.szTip,
      ARRAYSIZE(data_.szTip),
      enabled ? labels_.tip_monitoring : labels_.tip_paused);
  Shell_NotifyIconW(NIM_MODIFY, &data_);
}

void NotifyIconHost::ShowBalloon(const std::wstring& title, const std::wstring& text) {
  if (owner_ == nullptr) {
    return;
  }
  NOTIFYICONDATAW balloon = data_;
  balloon.uFlags = NIF_INFO;
  balloon.dwInfoFlags = NIIF_NONE;
  wcsncpy_s(balloon.szInfoTitle, ARRAYSIZE(balloon.szInfoTitle), title.c_str(), _TRUNCATE);
  wcsncpy_s(balloon.szInfo, ARRAYSIZE(balloon.szInfo), text.c_str(), _TRUNCATE);
  Shell_NotifyIconW(NIM_MODIFY, &balloon);
}

int NotifyIconHost::ShowContextMenu(bool monitoring_enabled) {
  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING, kCommandShow, labels_.show);
  AppendMenuW(menu, MF_STRING, kCommandToggleMonitoring, monitoring_enabled ? labels_.pause_monitoring : labels_.resume_monitoring);
  AppendMenuW(menu, MF_STRING, kCommandSettings, labels_.settings);
  AppendMenuW(menu, MF_STRING, kCommandClearHistory, labels_.clear_history);
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kCommandExit, labels_.exit);

  POINT cursor{};
  GetCursorPos(&cursor);
  SetForegroundWindow(owner_);
  const int command = TrackPopupMenu(
      menu,
      TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_BOTTOMALIGN,
      cursor.x,
      cursor.y,
      0,
      owner_,
      nullptr);
  DestroyMenu(menu);
  return command;
}

}  // namespace clipd2d::platform::win32
