#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <d2d1_1.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <dwrite.h>
#include <windows.h>
#include <dcomp.h>
#include <wrl/client.h>
#include "common/Status.h"

namespace clipd2d::ui {

class D2dShell {
 public:
  D2dShell() = default;
  ~D2dShell();

  common::Status Initialize(HWND hwnd);
  common::Status Initialize1_1(HWND hwnd);
  common::Status InitializeDComp(HWND hwnd);
  void Shutdown();
  void Resize(UINT width, UINT height);
  void Resize1_1(UINT width, UINT height);
  void UpdateDpi(UINT dpi);

  ID2D1HwndRenderTarget* BeginFrame();
  ID2D1DeviceContext* BeginFrame1_1();
  bool EndFrame();
  bool EndFrame1_1();

  HWND hwnd() const { return hwnd_; }
  float dpi_scale() const { return dpi_scale_; }
  UINT dpi() const { return dpi_; }
  std::uint64_t target_generation() const { return target_generation_; }
  bool secondary_text_formats_ready() const { return secondary_text_formats_ready_; }
  bool is_dcomp() const { return is_dcomp_; }

  ID2D1HwndRenderTarget* target() const { return render_target_.Get(); }
  ID2D1DeviceContext* device_context() const { return device_context_.Get(); }
  ID2D1RenderTarget* active_target() const { return is_1_1_ ? static_cast<ID2D1RenderTarget*>(device_context_.Get()) : static_cast<ID2D1RenderTarget*>(render_target_.Get()); }
  ID2D1Factory* d2d_factory() const { return d2d_factory_.Get(); }
  IDWriteFactory* dwrite_factory() const { return dwrite_factory_.Get(); }

  ID2D1SolidColorBrush* background_brush() const { return background_brush_.Get(); }
  ID2D1SolidColorBrush* surface_brush() const { return surface_brush_.Get(); }
  ID2D1SolidColorBrush* surface_alt_brush() const { return surface_alt_brush_.Get(); }
  ID2D1SolidColorBrush* surface_hover_brush() const { return surface_hover_brush_.Get(); }
  ID2D1SolidColorBrush* surface_selected_brush() const { return surface_selected_brush_.Get(); }
  ID2D1SolidColorBrush* border_brush() const { return border_brush_.Get(); }
  ID2D1SolidColorBrush* accent_brush() const { return accent_brush_.Get(); }
  ID2D1SolidColorBrush* accent_soft_brush() const { return accent_soft_brush_.Get(); }
  ID2D1SolidColorBrush* text_brush() const { return text_brush_.Get(); }
  ID2D1SolidColorBrush* muted_text_brush() const { return muted_text_brush_.Get(); }
  ID2D1SolidColorBrush* subtle_text_brush() const { return subtle_text_brush_.Get(); }
  ID2D1SolidColorBrush* danger_brush() const { return danger_brush_.Get(); }
  ID2D1SolidColorBrush* success_brush() const { return success_brush_.Get(); }
  ID2D1SolidColorBrush* white_brush() const { return white_brush_.Get(); }

  // Q2:动态颜色实心刷缓存(标签 chip/对话框色板/开关旋钮/搜索清除按钮等每帧
  // 路径),对齐上述具名刷缓存模式——一次创建反复用,随设备资源重建失效。
  // 返回的指针由 shell 持有,调用方不得 Release、不得 SetColor(共享对象)。
  ID2D1SolidColorBrush* solid_brush(ID2D1RenderTarget* target, const D2D1_COLOR_F& color);

  // Q2:搜索清除按钮 × 字形的文本格式(原每帧创建),一次创建反复用。
  IDWriteTextFormat* search_clear_format() const { return search_clear_format_.Get(); }

  IDWriteTextFormat* title_format() const { return title_format_.Get(); }
  IDWriteTextFormat* section_format() const { return section_format_.Get(); }
  IDWriteTextFormat* body_format() const { return body_format_.Get(); }
  IDWriteTextFormat* body_bold_format() const { return body_bold_format_.Get(); }
  IDWriteTextFormat* meta_format() const { return meta_format_.Get(); }
  IDWriteTextFormat* small_format() const { return small_format_.Get(); }
  IDWriteTextFormat* content_format() const { return content_format_.Get(); }
  IDWriteTextFormat* tiny_format() const { return tiny_format_.Get(); }
  IDWriteTextFormat* icon_format() const { return icon_format_.Get(); }
  IDWriteTextFormat* icon_small_format() const { return icon_small_format_.Get(); }
  IDWriteTextFormat* badge_format() const { return badge_format_.Get(); }

  common::Status EnsureSecondaryTextFormats();

  common::Status CreateBitmapFromBgra(
      UINT width,
      UINT height,
      const std::vector<std::uint8_t>& pixels,
      ID2D1Bitmap** bitmap) const;

 private:
  common::Status EnsureDeviceResources();
  common::Status CreateDeviceResources();
  common::Status CreateDeviceResources1_1();
  common::Status CreateDCompDeviceResources();
  common::Status SetupTargetBitmap1_1();
  common::Status CreatePrimaryTextFormats();
  common::Status CreateSecondaryTextFormats();
  common::Status CreateBrushes();
  void ReleaseDeviceResources();

  HWND hwnd_ = nullptr;
  UINT dpi_ = 96;
  float dpi_scale_ = 1.0f;
  std::uint64_t target_generation_ = 1;
  bool secondary_text_formats_ready_ = false;
  bool is_1_1_ = false;
  Microsoft::WRL::ComPtr<ID2D1Factory> d2d_factory_;
  Microsoft::WRL::ComPtr<IDWriteFactory> dwrite_factory_;
  Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> render_target_;

  // Direct2D 1.1 Support
  Microsoft::WRL::ComPtr<ID2D1Device> d2d_device_;
  Microsoft::WRL::ComPtr<ID2D1DeviceContext> device_context_;
  Microsoft::WRL::ComPtr<IDXGISwapChain1> swap_chain_;
  Microsoft::WRL::ComPtr<ID2D1Bitmap1> target_bitmap_;

  // DirectComposition Support
  bool is_dcomp_ = false;
  Microsoft::WRL::ComPtr<IDCompositionDevice> dcomp_device_;
  Microsoft::WRL::ComPtr<IDCompositionTarget> dcomp_target_;
  Microsoft::WRL::ComPtr<IDCompositionVisual> dcomp_visual_;

  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> background_brush_;
  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> surface_brush_;
  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> surface_alt_brush_;
  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> surface_hover_brush_;
  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> surface_selected_brush_;
  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> border_brush_;
  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> accent_brush_;
  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> accent_soft_brush_;
  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> text_brush_;
  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> muted_text_brush_;
  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> subtle_text_brush_;
  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> danger_brush_;
  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> success_brush_;
  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> white_brush_;

  Microsoft::WRL::ComPtr<IDWriteTextFormat> title_format_;
  Microsoft::WRL::ComPtr<IDWriteTextFormat> section_format_;
  Microsoft::WRL::ComPtr<IDWriteTextFormat> body_format_;
  Microsoft::WRL::ComPtr<IDWriteTextFormat> body_bold_format_;
  Microsoft::WRL::ComPtr<IDWriteTextFormat> meta_format_;
  Microsoft::WRL::ComPtr<IDWriteTextFormat> small_format_;
  Microsoft::WRL::ComPtr<IDWriteTextFormat> content_format_;
  Microsoft::WRL::ComPtr<IDWriteTextFormat> tiny_format_;
  Microsoft::WRL::ComPtr<IDWriteTextFormat> icon_format_;
  Microsoft::WRL::ComPtr<IDWriteTextFormat> icon_small_format_;
  Microsoft::WRL::ComPtr<IDWriteTextFormat> badge_format_;
  // Q2:每帧路径复用的缓存(动态色刷 / 搜索清除按钮 × 格式)。
  Microsoft::WRL::ComPtr<IDWriteTextFormat> search_clear_format_;
  std::unordered_map<std::uint32_t, Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>> solid_brush_cache_;
};

}  // namespace clipd2d::ui
