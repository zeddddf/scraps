#pragma once

#include <d2d1.h>
#include <windows.h>

namespace clipd2d::ui {

// Q9:RECT → D2D1_RECT_F 的单一定义(原 ClipImagePreview.cpp / ClipVisualFileContent.cpp /
// MainWindowInternal.h / QuickPasteWindow.cpp / SettingsWindow.cpp / SettingsDrawUtil.cpp
// 六处各自复制一份,已收敛至此)。
inline D2D1_RECT_F ToRectF(const RECT& rect) {
  return D2D1::RectF(
      static_cast<float>(rect.left),
      static_cast<float>(rect.top),
      static_cast<float>(rect.right),
      static_cast<float>(rect.bottom));
}

}  // namespace clipd2d::ui
