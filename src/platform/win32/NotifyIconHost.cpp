#include "platform/win32/NotifyIconHost.h"

#include <shellapi.h>

namespace clipd2d::platform::win32 {

NotifyIconHost::~NotifyIconHost() {
  Shutdown();
}

common::Status NotifyIconHost::Initialize(HWND owner) {
  owner_ = owner;
  data_.cbSize = sizeof(data_);
  data_.hWnd = owner_;
  data_.uID = 1;
  data_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  data_.uCallbackMessage = kTrayMessage;
  data_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
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
      enabled ? L"Scraps - 正在监听" : L"Scraps - 已暂停");
  Shell_NotifyIconW(NIM_MODIFY, &data_);
}

int NotifyIconHost::ShowContextMenu(bool monitoring_enabled) {
  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING, kCommandShow, L"显示主窗口");
  AppendMenuW(menu, MF_STRING, kCommandToggleMonitoring, monitoring_enabled ? L"暂停监听" : L"恢复监听");
  AppendMenuW(menu, MF_STRING, kCommandSettings, L"设置");
  AppendMenuW(menu, MF_STRING, kCommandClearHistory, L"清空历史");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, kCommandExit, L"退出");

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
