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

// ── 全局主题状态（C1 暗色模式）──────────────────────────────────────
// 每个窗口各持一个 D2dShell（各自的渲染目标 + 刷子），主题是进程级属性，
// 用全局纪元号驱动：SetDarkThemeActive 变更时递增纪元，各窗口每帧开头
// SyncTheme() 比对纪元，不一致则重建刷子并刷新 DWM/文本框颜色。
inline bool IsDarkThemeActive() {
  extern bool g_dark_theme_active;
  return g_dark_theme_active;
}

inline std::uint32_t ThemeEpoch() {
  extern std::uint32_t g_theme_epoch;
  return g_theme_epoch;
}

// 设置全局暗色态；变化时递增纪元（各窗口下一帧自动重建调色板资源）。
void SetDarkThemeActive(bool dark);

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

  // C1 暗色模式：按当前主题在明/暗两组散落硬编码色间取值。
  D2D1_COLOR_F themed_color(const D2D1_COLOR_F& light, const D2D1_COLOR_F& dark) const;

  // ── C1 语义化取色：优先用这些而非 themed_color 手写配对，保证明暗两套
  // 色板一致、层级正确（暗色按 iOS 规范：中性灰阶 + 官方系统色）。
  D2D1_COLOR_F ColorBackground() const;       // 窗口底
  D2D1_COLOR_F ColorSurface() const;          // 卡片/行/列表
  D2D1_COLOR_F ColorSurfaceAlt() const;       // 输入框/次要控件
  D2D1_COLOR_F ColorSurfaceHover() const;     // 悬停态
  D2D1_COLOR_F ColorSurfaceSelected() const;  // 列表选中态
  D2D1_COLOR_F ColorBorder() const;           // 描边/分隔线
  D2D1_COLOR_F ColorAccent() const;           // 主色
  D2D1_COLOR_F ColorAccentSoft() const;       // 主色弱化填充（选中底）
  D2D1_COLOR_F ColorText() const;             // 主文本
  D2D1_COLOR_F ColorMutedText() const;        // 次要文本
  D2D1_COLOR_F ColorSubtleText() const;       // 弱文本/占位符

  // 每帧开头调用：主题纪元变化时重建刷子。返回 true 表示本帧刷子已重建，
  // 调用方应同步刷新文本框颜色 / DWM 属性等主题相关资源。
  bool SyncTheme();

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

  std::uint32_t brushes_theme_epoch_ = 0;  // 0 = 尚未按主题构建

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
