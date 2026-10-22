#pragma once

// 应用图标统一加载入口：图标资源嵌在各 exe（src/app/app.rc），
// 这里从本模块实例按资源 ID 取。失败回退系统默认应用图标。
#include <windows.h>

#include "common/resource.h"

namespace clipd2d::common {

inline HICON LoadAppIcon() {
  HICON icon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_SCRAPSAPP));
  if (icon == nullptr) {
    icon = LoadIconW(nullptr, IDI_APPLICATION);
  }
  return icon;
}

}  // namespace clipd2d::common
