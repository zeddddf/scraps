#pragma once

#include <string>

#include <d2d1.h>
#include <dwrite.h>
#include <windows.h>
#include <wrl/client.h>

#include "common/Status.h"

namespace clipd2d::ui {

class HoverHintWindow {
 public:
  HoverHintWindow() = default;
  ~HoverHintWindow();

  common::Status Create(HINSTANCE instance, HWND owner);
  void Destroy();
  void ShowText(std::wstring text, POINT screen_anchor);
  void Hide();
  HWND hwnd() const { return hwnd_; }

 private:
  static LRESULT CALLBACK StaticWindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param);
  LRESULT WindowProc(UINT message, WPARAM w_param, LPARAM l_param);
  common::Status EnsureDeviceIndependentResources();
  common::Status EnsureDeviceResources(int width, int height);
  void ReleaseDeviceResources();
  common::Status MeasureText(const std::wstring& text, int max_width, int* width, int* height);
  common::Status PresentLayered(int width, int height);

  HINSTANCE instance_ = nullptr;
  HWND owner_ = nullptr;
  HWND hwnd_ = nullptr;
  // 幂等状态：鼠标在列表上移动时 UpdateHoverTooltip 每个 WM_MOUSEMOVE 都会调 Hide()，
  // 无条件 ShowWindow(SW_HIDE) 等于每次移动都跟窗口管理器同步一次（实测 15~47ms/次）。
  bool visible_ = false;
  std::wstring text_;
  int width_ = 0;
  int height_ = 0;
  Microsoft::WRL::ComPtr<ID2D1Factory> d2d_factory_;
  Microsoft::WRL::ComPtr<IDWriteFactory> dwrite_factory_;
  Microsoft::WRL::ComPtr<ID2D1DCRenderTarget> dc_render_target_;
  Microsoft::WRL::ComPtr<IDWriteTextFormat> text_format_;
  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> background_brush_;
  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> border_brush_;
  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> text_brush_;
  HDC memory_dc_ = nullptr;
  HBITMAP dib_bitmap_ = nullptr;
  void* dib_bits_ = nullptr;
  HBITMAP previous_bitmap_ = nullptr;
};

}  // namespace clipd2d::ui
