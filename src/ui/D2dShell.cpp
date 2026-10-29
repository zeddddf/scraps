#include "ui/D2dShell.h"

#include <algorithm>
#include <sstream>

#include "common/StartupTrace.h"
#include "common/Log.h"

#pragma comment(lib, "dcomp.lib")

namespace clipd2d::ui {

// 全局主题状态（声明见 D2dShell.h）：进程级明/暗态与纪元号。
bool g_dark_theme_active = false;
std::uint32_t g_theme_epoch = 1;

namespace {

common::Status FromHr(HRESULT hr, const char* message) {
  if (SUCCEEDED(hr)) {
    return common::Status::Ok();
  }
  char buffer[256];
  snprintf(buffer, sizeof(buffer), "%s (HRESULT: 0x%08X)", message, static_cast<unsigned int>(hr));
  return common::Status::Error(common::StatusCode::PlatformError, buffer);
}

HRESULT CreateIconTextFormatWithFallback(IDWriteFactory* factory, float font_size, IDWriteTextFormat** format) {
  if (factory == nullptr || format == nullptr) {
    return E_INVALIDARG;
  }

  HRESULT hr = factory->CreateTextFormat(
      L"Segoe Fluent Icons",
      nullptr,
      DWRITE_FONT_WEIGHT_NORMAL,
      DWRITE_FONT_STYLE_NORMAL,
      DWRITE_FONT_STRETCH_NORMAL,
      font_size,
      L"",
      format);
  if (SUCCEEDED(hr)) {
    return hr;
  }

  return factory->CreateTextFormat(
      L"Segoe MDL2 Assets",
      nullptr,
      DWRITE_FONT_WEIGHT_NORMAL,
      DWRITE_FONT_STYLE_NORMAL,
      DWRITE_FONT_STRETCH_NORMAL,
      font_size,
      L"",
      format);
}

}  // namespace

D2dShell::~D2dShell() {
  Shutdown();
}

common::Status D2dShell::Initialize(HWND hwnd) {
  hwnd_ = hwnd;
  dpi_ = hwnd_ != nullptr ? GetDpiForWindow(hwnd_) : 96;
  dpi_scale_ = static_cast<float>(dpi_) / 96.0f;
  secondary_text_formats_ready_ = false;
  is_1_1_ = false;

  HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d_factory_.ReleaseAndGetAddressOf());
  common::Status status = FromHr(hr, "创建 D2D 工厂失败。");
  if (!status.ok()) {
    return status;
  }
  clipd2d::common::MarkStartupPhase("d2d_factory_ready");

  hr = DWriteCreateFactory(
      DWRITE_FACTORY_TYPE_SHARED,
      __uuidof(IDWriteFactory),
      reinterpret_cast<IUnknown**>(dwrite_factory_.ReleaseAndGetAddressOf()));
  status = FromHr(hr, "创建 DWrite 工厂失败。");
  if (!status.ok()) {
    return status;
  }
  clipd2d::common::MarkStartupPhase("dwrite_factory_ready");

  status = CreatePrimaryTextFormats();
  if (!status.ok()) {
    return status;
  }
  clipd2d::common::MarkStartupPhase("d2d_primary_text_formats_ready");
  return EnsureDeviceResources();
}

common::Status D2dShell::Initialize1_1(HWND hwnd) {
  hwnd_ = hwnd;
  dpi_ = hwnd_ != nullptr ? GetDpiForWindow(hwnd_) : 96;
  dpi_scale_ = static_cast<float>(dpi_) / 96.0f;
  secondary_text_formats_ready_ = false;
  is_1_1_ = true;

  HRESULT hr = D2D1CreateFactory(
      D2D1_FACTORY_TYPE_SINGLE_THREADED,
      __uuidof(ID2D1Factory1),
      reinterpret_cast<void**>(d2d_factory_.ReleaseAndGetAddressOf()));
  common::Status status = FromHr(hr, "创建 D2D1Factory1 失败。");
  if (!status.ok()) {
    return status;
  }
  clipd2d::common::MarkStartupPhase("d2d_factory_ready");

  hr = DWriteCreateFactory(
      DWRITE_FACTORY_TYPE_SHARED,
      __uuidof(IDWriteFactory),
      reinterpret_cast<IUnknown**>(dwrite_factory_.ReleaseAndGetAddressOf()));
  status = FromHr(hr, "创建 DWrite 工厂失败。");
  if (!status.ok()) {
    return status;
  }
  clipd2d::common::MarkStartupPhase("dwrite_factory_ready");

  status = CreatePrimaryTextFormats();
  if (!status.ok()) {
    return status;
  }
  clipd2d::common::MarkStartupPhase("d2d_primary_text_formats_ready");
  return EnsureDeviceResources();
}

common::Status D2dShell::InitializeDComp(HWND hwnd) {
  hwnd_ = hwnd;
  dpi_ = hwnd_ != nullptr ? GetDpiForWindow(hwnd_) : 96;
  dpi_scale_ = static_cast<float>(dpi_) / 96.0f;
  secondary_text_formats_ready_ = false;
  is_1_1_ = true;
  is_dcomp_ = true;

  HRESULT hr = D2D1CreateFactory(
      D2D1_FACTORY_TYPE_SINGLE_THREADED,
      __uuidof(ID2D1Factory1),
      reinterpret_cast<void**>(d2d_factory_.ReleaseAndGetAddressOf()));
  common::Status status = FromHr(hr, "创建 D2D1Factory1 失败。");
  if (!status.ok()) {
    return status;
  }
  clipd2d::common::MarkStartupPhase("d2d_factory_ready");

  hr = DWriteCreateFactory(
      DWRITE_FACTORY_TYPE_SHARED,
      __uuidof(IDWriteFactory),
      reinterpret_cast<IUnknown**>(dwrite_factory_.ReleaseAndGetAddressOf()));
  status = FromHr(hr, "创建 DWrite 工厂失败。");
  if (!status.ok()) {
    return status;
  }
  clipd2d::common::MarkStartupPhase("dwrite_factory_ready");

  status = CreatePrimaryTextFormats();
  if (!status.ok()) {
    return status;
  }
  clipd2d::common::MarkStartupPhase("d2d_primary_text_formats_ready");
  return common::Status::Ok();
}

void D2dShell::Shutdown() {
  ReleaseDeviceResources();
  badge_format_.Reset();
  icon_small_format_.Reset();
  icon_format_.Reset();
  tiny_format_.Reset();
  small_format_.Reset();
  meta_format_.Reset();
  body_bold_format_.Reset();
  body_format_.Reset();
  section_format_.Reset();
  title_format_.Reset();
  secondary_text_formats_ready_ = false;
  dwrite_factory_.Reset();
  d2d_factory_.Reset();
  hwnd_ = nullptr;
  is_dcomp_ = false;
}

void D2dShell::Resize(UINT width, UINT height) {
  if (is_1_1_) {
    Resize1_1(width, height);
  } else if (render_target_ != nullptr) {
    render_target_->Resize(D2D1::SizeU(width, height));
  }
}

void D2dShell::Resize1_1(UINT width, UINT height) {
  if (device_context_ == nullptr || swap_chain_ == nullptr) {
    return;
  }

  device_context_->SetTarget(nullptr);
  target_bitmap_.Reset();

  HRESULT hr = swap_chain_->ResizeBuffers(
      0,
      width,
      height,
      DXGI_FORMAT_UNKNOWN,
      0
  );
  if (FAILED(hr)) {
    common::WriteLog(common::LogLevel::Error, "d2d.resize", "ResizeBuffers 失败 hr=" + std::to_string(hr));
    return;
  }

  SetupTargetBitmap1_1();
}

void D2dShell::UpdateDpi(UINT dpi) {
  if (dpi_ != dpi) {
    dpi_ = dpi;
    dpi_scale_ = static_cast<float>(dpi_) / 96.0f;
    if (is_1_1_) {
      if (device_context_ != nullptr) {
        device_context_->SetDpi(static_cast<FLOAT>(dpi_), static_cast<FLOAT>(dpi_));
      }
    } else if (render_target_ != nullptr) {
      render_target_->SetDpi(static_cast<FLOAT>(dpi_), static_cast<FLOAT>(dpi_));
    }
  }
}

ID2D1HwndRenderTarget* D2dShell::BeginFrame() {
  if (!EnsureDeviceResources().ok()) {
    return nullptr;
  }
  render_target_->BeginDraw();
  render_target_->SetDpi(static_cast<FLOAT>(dpi_), static_cast<FLOAT>(dpi_));
  render_target_->SetTransform(D2D1::Matrix3x2F::Identity());
  return render_target_.Get();
}

ID2D1DeviceContext* D2dShell::BeginFrame1_1() {
  if (!EnsureDeviceResources().ok()) {
    return nullptr;
  }
  device_context_->BeginDraw();
  device_context_->SetDpi(static_cast<FLOAT>(dpi_), static_cast<FLOAT>(dpi_));
  device_context_->SetTransform(D2D1::Matrix3x2F::Identity());
  return device_context_.Get();
}

bool D2dShell::EndFrame() {
  if (render_target_ == nullptr) {
    return false;
  }
  const HRESULT hr = render_target_->EndDraw();
  if (hr == D2DERR_RECREATE_TARGET) {
    common::WriteLog(common::LogLevel::Warning, "d2d.end_frame", "EndDraw returned D2DERR_RECREATE_TARGET");
    ReleaseDeviceResources();
    ++target_generation_;
    return true;
  }
  if (FAILED(hr)) {
    std::ostringstream stream;
    stream << "EndDraw failed hr=0x" << std::hex << std::uppercase << static_cast<std::uint32_t>(hr);
    common::WriteLog(
        common::LogLevel::Warning,
        "d2d.end_frame",
        stream.str());
  }
  return false;
}

bool D2dShell::EndFrame1_1() {
  if (device_context_ == nullptr || swap_chain_ == nullptr) {
    return false;
  }
  const HRESULT hr = device_context_->EndDraw();
  if (hr == D2DERR_RECREATE_TARGET) {
    common::WriteLog(common::LogLevel::Warning, "d2d.end_frame1_1", "EndDraw returned D2DERR_RECREATE_TARGET");
    ReleaseDeviceResources();
    ++target_generation_;
    return true;
  }
  if (FAILED(hr)) {
    std::ostringstream stream;
    stream << "EndDraw failed hr=0x" << std::hex << std::uppercase << static_cast<std::uint32_t>(hr);
    common::WriteLog(common::LogLevel::Warning, "d2d.end_frame1_1", stream.str());
    return false;
  }

  HRESULT present_hr = swap_chain_->Present(1, 0);
  if (present_hr == DXGI_ERROR_DEVICE_RESET || present_hr == DXGI_ERROR_DEVICE_REMOVED) {
    common::WriteLog(common::LogLevel::Warning, "d2d.present", "Present returned device loss");
    ReleaseDeviceResources();
    ++target_generation_;
    return true;
  }


  return false;
}

common::Status D2dShell::CreateBitmapFromBgra(
    UINT width,
    UINT height,
    const std::vector<std::uint8_t>& pixels,
    ID2D1Bitmap** bitmap) const {
  if (bitmap == nullptr) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "位图输出对象不能为空。");
  }
  *bitmap = nullptr;
  ID2D1RenderTarget* rt = active_target();
  if (rt == nullptr) {
    return common::Status::Error(common::StatusCode::Unavailable, "D2D 渲染目标尚未就绪。");
  }
  if (width == 0 || height == 0 || pixels.empty()) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "位图像素数据无效。");
  }

  const UINT stride = width * 4;
  HRESULT hr = rt->CreateBitmap(
      D2D1::SizeU(width, height),
      pixels.data(),
      stride,
      D2D1::BitmapProperties(
          D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)),
      bitmap);
  return FromHr(hr, "创建 D2D 位图失败。");
}

common::Status D2dShell::EnsureDeviceResources() {
  if (is_1_1_) {
    if (device_context_ != nullptr) {
      return common::Status::Ok();
    }
    if (is_dcomp_) {
      common::Status status = CreateDCompDeviceResources();
      if (status.ok()) {
        return status;
      }
      common::WriteLog(common::LogLevel::Warning, "d2d.initialize", "CreateDCompDeviceResources failed, falling back to 1.1 Hwnd. Error: " + status.message);
      is_dcomp_ = false;
      ReleaseDeviceResources();
    }
    return CreateDeviceResources1_1();
  } else {
    if (render_target_ != nullptr) {
      return common::Status::Ok();
    }
    return CreateDeviceResources();
  }
}

common::Status D2dShell::CreateDeviceResources() {
  if (hwnd_ == nullptr || d2d_factory_ == nullptr) {
    return common::Status::Error(common::StatusCode::Unavailable, "D2D 资源尚未初始化。");
  }

  RECT rect{};
  GetClientRect(hwnd_, &rect);
  const D2D1_SIZE_U size = D2D1::SizeU(
      static_cast<UINT>(std::max<LONG>(1, rect.right - rect.left)),
      static_cast<UINT>(std::max<LONG>(1, rect.bottom - rect.top)));

  HRESULT hr = d2d_factory_->CreateHwndRenderTarget(
      D2D1::RenderTargetProperties(
          D2D1_RENDER_TARGET_TYPE_DEFAULT,
          D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_IGNORE),
          0.0f,
          0.0f,
          D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE),
      D2D1::HwndRenderTargetProperties(hwnd_, size),
      render_target_.ReleaseAndGetAddressOf());
  common::Status status = FromHr(hr, "创建 D2D 渲染目标失败。");
  if (!status.ok()) {
    return status;
  }
  clipd2d::common::MarkStartupPhase("d2d_render_target_ready");

  render_target_->SetDpi(static_cast<FLOAT>(dpi_), static_cast<FLOAT>(dpi_));
  return CreateBrushes();
}

common::Status D2dShell::CreateDCompDeviceResources() {
  UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
  D3D_FEATURE_LEVEL featureLevels[] = {
      D3D_FEATURE_LEVEL_11_1,
      D3D_FEATURE_LEVEL_11_0,
      D3D_FEATURE_LEVEL_10_1,
      D3D_FEATURE_LEVEL_10_0,
      D3D_FEATURE_LEVEL_9_3,
      D3D_FEATURE_LEVEL_9_1
  };

  Microsoft::WRL::ComPtr<ID3D11Device> d3d_device;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3d_context;
  D3D_FEATURE_LEVEL featureLevel;
  HRESULT hr = D3D11CreateDevice(
      nullptr,
      D3D_DRIVER_TYPE_HARDWARE,
      nullptr,
      creationFlags,
      featureLevels,
      ARRAYSIZE(featureLevels),
      D3D11_SDK_VERSION,
      &d3d_device,
      &featureLevel,
      &d3d_context
  );
  if (FAILED(hr)) {
    hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_WARP,
        nullptr,
        creationFlags,
        featureLevels,
        ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        &d3d_device,
        &featureLevel,
        &d3d_context
    );
  }
  if (FAILED(hr)) {
    return FromHr(hr, "创建 D3D11 设备失败。");
  }

  Microsoft::WRL::ComPtr<IDXGIDevice> dxgi_device;
  hr = d3d_device.As(&dxgi_device);
  if (FAILED(hr)) {
    return FromHr(hr, "获取 DXGI 设备失败。");
  }

  Microsoft::WRL::ComPtr<ID2D1Factory1> factory1;
  hr = d2d_factory_.As(&factory1);
  if (FAILED(hr)) {
    return FromHr(hr, "获取 D2D1Factory1 失败。");
  }

  hr = factory1->CreateDevice(dxgi_device.Get(), d2d_device_.ReleaseAndGetAddressOf());
  if (FAILED(hr)) {
    return FromHr(hr, "创建 D2D 设备失败。");
  }

  hr = d2d_device_->CreateDeviceContext(
      D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
      device_context_.ReleaseAndGetAddressOf()
  );
  if (FAILED(hr)) {
    return FromHr(hr, "创建 D2D 设备上下文失败。");
  }

  device_context_->SetUnitMode(D2D1_UNIT_MODE_DIPS);

  Microsoft::WRL::ComPtr<IDXGIAdapter> dxgi_adapter;
  hr = dxgi_device->GetAdapter(&dxgi_adapter);
  if (FAILED(hr)) {
    return FromHr(hr, "获取 DXGI 适配器失败。");
  }

  Microsoft::WRL::ComPtr<IDXGIFactory2> dxgi_factory;
  hr = dxgi_adapter->GetParent(IID_PPV_ARGS(&dxgi_factory));
  if (FAILED(hr)) {
    return FromHr(hr, "获取 DXGI 工厂失败。");
  }

  RECT rect{};
  GetClientRect(hwnd_, &rect);
  UINT width = std::max<UINT>(1, rect.right - rect.left);
  UINT height = std::max<UINT>(1, rect.bottom - rect.top);

  DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {0};
  swapChainDesc.Width = width;
  swapChainDesc.Height = height;
  swapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  swapChainDesc.Stereo = FALSE;
  swapChainDesc.SampleDesc.Count = 1;
  swapChainDesc.SampleDesc.Quality = 0;
  swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swapChainDesc.BufferCount = 2;
  swapChainDesc.Scaling = DXGI_SCALING_STRETCH; // Required for composition swapchains
  swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
  swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED; // Premultiplied alpha for transparent blending
  // P2-7 曾在此启用 DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT 并在
  // RenderFrame 前等待。实测（2026-10-29）：DComp FLIP_SEQUENTIAL+PREMULTIPLIED
  // 组合链上 waitable 永不 signal,每帧等满 100ms 超时,动画帧率 60fps→10fps
  // （debug.log 1172 条 ui.frame.slow setup_ms≈100）,已回退。vsync 节流保留在
  // Present(1,0);输入延迟目标仍由下方设备级 SetMaximumFrameLatency(1) 达成
  // （排队帧数钉在 1,不依赖 waitable）。

  // Create swap chain for composition
  hr = dxgi_factory->CreateSwapChainForComposition(
      d3d_device.Get(),
      &swapChainDesc,
      nullptr,
      swap_chain_.ReleaseAndGetAddressOf()
  );
  if (FAILED(hr)) {
    return FromHr(hr, "创建 DXGI 组合交换链失败。");
  }

  Microsoft::WRL::ComPtr<IDXGIDevice1> dxgi_device1;
  if (SUCCEEDED(dxgi_device.As(&dxgi_device1))) {
    dxgi_device1->SetMaximumFrameLatency(1);
  }

  // Set up DirectComposition
  hr = DCompositionCreateDevice(dxgi_device.Get(), IID_PPV_ARGS(&dcomp_device_));
  if (FAILED(hr)) {
    return FromHr(hr, "创建 DComp 设备失败。");
  }

  hr = dcomp_device_->CreateTargetForHwnd(hwnd_, TRUE, &dcomp_target_);
  if (FAILED(hr)) {
    return FromHr(hr, "创建 DComp Target 失败。");
  }

  hr = dcomp_device_->CreateVisual(&dcomp_visual_);
  if (FAILED(hr)) {
    return FromHr(hr, "创建 DComp Visual 失败。");
  }

  hr = dcomp_visual_->SetContent(swap_chain_.Get());
  if (FAILED(hr)) {
    return FromHr(hr, "设置 DComp Visual 内容失败。");
  }

  hr = dcomp_target_->SetRoot(dcomp_visual_.Get());
  if (FAILED(hr)) {
    return FromHr(hr, "设置 DComp Target 根节点失败。");
  }

  hr = dcomp_device_->Commit();
  if (FAILED(hr)) {
    return FromHr(hr, "提交 DComp 事务失败。");
  }

  if (hwnd_ != nullptr) {
    LONG_PTR exStyle = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, exStyle | WS_EX_NOREDIRECTIONBITMAP);
  }

  common::Status status = SetupTargetBitmap1_1();
  if (!status.ok()) {
    return status;
  }
  return CreateBrushes();
}

common::Status D2dShell::CreateDeviceResources1_1() {
  UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
  D3D_FEATURE_LEVEL featureLevels[] = {
      D3D_FEATURE_LEVEL_11_1,
      D3D_FEATURE_LEVEL_11_0,
      D3D_FEATURE_LEVEL_10_1,
      D3D_FEATURE_LEVEL_10_0,
      D3D_FEATURE_LEVEL_9_3,
      D3D_FEATURE_LEVEL_9_1
  };

  Microsoft::WRL::ComPtr<ID3D11Device> d3d_device;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3d_context;
  D3D_FEATURE_LEVEL featureLevel;
  HRESULT hr = D3D11CreateDevice(
      nullptr,
      D3D_DRIVER_TYPE_HARDWARE,
      nullptr,
      creationFlags,
      featureLevels,
      ARRAYSIZE(featureLevels),
      D3D11_SDK_VERSION,
      &d3d_device,
      &featureLevel,
      &d3d_context
  );
  if (FAILED(hr)) {
    hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_WARP,
        nullptr,
        creationFlags,
        featureLevels,
        ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        &d3d_device,
        &featureLevel,
        &d3d_context
    );
  }
  if (FAILED(hr)) {
    return FromHr(hr, "创建 D3D11 设备失败。");
  }

  Microsoft::WRL::ComPtr<IDXGIDevice> dxgi_device;
  hr = d3d_device.As(&dxgi_device);
  if (FAILED(hr)) {
    return FromHr(hr, "获取 DXGI 设备失败。");
  }

  Microsoft::WRL::ComPtr<ID2D1Factory1> factory1;
  hr = d2d_factory_.As(&factory1);
  if (FAILED(hr)) {
    return FromHr(hr, "获取 D2D1Factory1 失败。");
  }

  hr = factory1->CreateDevice(dxgi_device.Get(), d2d_device_.ReleaseAndGetAddressOf());
  if (FAILED(hr)) {
    return FromHr(hr, "创建 D2D 设备失败。");
  }

  hr = d2d_device_->CreateDeviceContext(
      D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
      device_context_.ReleaseAndGetAddressOf()
  );
  if (FAILED(hr)) {
    return FromHr(hr, "创建 D2D 设备上下文失败。");
  }

  device_context_->SetUnitMode(D2D1_UNIT_MODE_DIPS);

  Microsoft::WRL::ComPtr<IDXGIAdapter> dxgi_adapter;
  hr = dxgi_device->GetAdapter(&dxgi_adapter);
  if (FAILED(hr)) {
    return FromHr(hr, "获取 DXGI 适配器失败。");
  }

  Microsoft::WRL::ComPtr<IDXGIFactory2> dxgi_factory;
  hr = dxgi_adapter->GetParent(IID_PPV_ARGS(&dxgi_factory));
  if (FAILED(hr)) {
    return FromHr(hr, "获取 DXGI 工厂失败。");
  }

  RECT rect{};
  GetClientRect(hwnd_, &rect);
  UINT width = std::max<UINT>(1, rect.right - rect.left);
  UINT height = std::max<UINT>(1, rect.bottom - rect.top);

  DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {0};
  swapChainDesc.Width = width;
  swapChainDesc.Height = height;
  swapChainDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  swapChainDesc.Stereo = FALSE;
  swapChainDesc.SampleDesc.Count = 1;
  swapChainDesc.SampleDesc.Quality = 0;
  swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swapChainDesc.BufferCount = 2;
  swapChainDesc.Scaling = DXGI_SCALING_NONE;
  swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
  swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
  // P2-7 曾在同处启用 waitable flag,回退原因见组合链路径注释。hwnd 链未单独
  // 实测,但 waitable 语义相同,统一不走。

  hr = dxgi_factory->CreateSwapChainForHwnd(
      d3d_device.Get(),
      hwnd_,
      &swapChainDesc,
      nullptr,
      nullptr,
      swap_chain_.ReleaseAndGetAddressOf()
  );
  if (FAILED(hr)) {
    return FromHr(hr, "创建 DXGI 交换链失败。");
  }

  Microsoft::WRL::ComPtr<IDXGIDevice1> dxgi_device1;
  if (SUCCEEDED(dxgi_device.As(&dxgi_device1))) {
    dxgi_device1->SetMaximumFrameLatency(1);
  }

  common::Status status = SetupTargetBitmap1_1();
  if (!status.ok()) {
    return status;
  }
  return CreateBrushes();
}

common::Status D2dShell::SetupTargetBitmap1_1() {
  if (swap_chain_ == nullptr || device_context_ == nullptr) {
    return common::Status::Error(common::StatusCode::Unavailable, "交换链或设备上下文未就绪。");
  }

  Microsoft::WRL::ComPtr<IDXGISurface> dxgi_back_buffer;
  HRESULT hr = swap_chain_->GetBuffer(0, IID_PPV_ARGS(&dxgi_back_buffer));
  if (FAILED(hr)) {
    return FromHr(hr, "获取交换链后备缓冲区失败。");
  }

  D2D1_BITMAP_PROPERTIES1 bitmapProperties = D2D1::BitmapProperties1(
      D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)
  );

  hr = device_context_->CreateBitmapFromDxgiSurface(
      dxgi_back_buffer.Get(),
      &bitmapProperties,
      target_bitmap_.ReleaseAndGetAddressOf()
  );
  if (FAILED(hr)) {
    return FromHr(hr, "从 DXGI 表面创建 D2D 位图失败。");
  }

  device_context_->SetTarget(target_bitmap_.Get());
  device_context_->SetDpi(static_cast<float>(dpi_), static_cast<float>(dpi_));

  return common::Status::Ok();
}

common::Status D2dShell::EnsureSecondaryTextFormats() {
  if (secondary_text_formats_ready_) {
    return common::Status::Ok();
  }
  const common::Status status = CreateSecondaryTextFormats();
  if (status.ok()) {
    secondary_text_formats_ready_ = true;
  }
  return status;
}

common::Status D2dShell::CreatePrimaryTextFormats() {
  HRESULT hr = dwrite_factory_->CreateTextFormat(
      L"Segoe UI Semibold",
      nullptr,
      DWRITE_FONT_WEIGHT_SEMI_BOLD,
      DWRITE_FONT_STYLE_NORMAL,
      DWRITE_FONT_STRETCH_NORMAL,
      24.0f,
      L"",
      title_format_.ReleaseAndGetAddressOf());
  if (FAILED(hr)) {
    return common::Status::Error(common::StatusCode::PlatformError, "创建标题字体失败。");
  }

  hr = dwrite_factory_->CreateTextFormat(
      L"Segoe UI Semibold",
      nullptr,
      DWRITE_FONT_WEIGHT_SEMI_BOLD,
      DWRITE_FONT_STYLE_NORMAL,
      DWRITE_FONT_STRETCH_NORMAL,
      20.0f,
      L"",
      section_format_.ReleaseAndGetAddressOf());
  if (FAILED(hr)) {
    return common::Status::Error(common::StatusCode::PlatformError, "创建分组字体失败。");
  }

  hr = dwrite_factory_->CreateTextFormat(
      L"Segoe UI",
      nullptr,
      DWRITE_FONT_WEIGHT_NORMAL,
      DWRITE_FONT_STYLE_NORMAL,
      DWRITE_FONT_STRETCH_NORMAL,
      12.0f,
      L"",
      meta_format_.ReleaseAndGetAddressOf());
  if (FAILED(hr)) {
    return common::Status::Error(common::StatusCode::PlatformError, "创建元信息字体失败。");
  }

  hr = dwrite_factory_->CreateTextFormat(
      L"Segoe UI",
      nullptr,
      DWRITE_FONT_WEIGHT_NORMAL,
      DWRITE_FONT_STYLE_NORMAL,
      DWRITE_FONT_STRETCH_NORMAL,
      14.0f,
      L"",
      body_format_.ReleaseAndGetAddressOf());
  if (FAILED(hr)) {
    return common::Status::Error(common::StatusCode::PlatformError, "创建正文字体失败。");
  }

  hr = dwrite_factory_->CreateTextFormat(
      L"Segoe UI Semibold",
      nullptr,
      DWRITE_FONT_WEIGHT_SEMI_BOLD,
      DWRITE_FONT_STYLE_NORMAL,
      DWRITE_FONT_STRETCH_NORMAL,
      14.0f,
      L"",
      body_bold_format_.ReleaseAndGetAddressOf());
  if (FAILED(hr)) {
    return common::Status::Error(common::StatusCode::PlatformError, "创建加粗正文字体失败。");
  }

  hr = dwrite_factory_->CreateTextFormat(
      L"Segoe UI",
      nullptr,
      DWRITE_FONT_WEIGHT_NORMAL,
      DWRITE_FONT_STYLE_NORMAL,
      DWRITE_FONT_STRETCH_NORMAL,
      12.0f,
      L"",
      small_format_.ReleaseAndGetAddressOf());
  if (FAILED(hr)) {
    return common::Status::Error(common::StatusCode::PlatformError, "Failed to create small text format.");
  }
  small_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

  hr = dwrite_factory_->CreateTextFormat(
      L"Segoe UI",
      nullptr,
      DWRITE_FONT_WEIGHT_NORMAL,
      DWRITE_FONT_STYLE_NORMAL,
      DWRITE_FONT_STRETCH_NORMAL,
      12.0f,
      L"",
      content_format_.ReleaseAndGetAddressOf());
  if (FAILED(hr)) {
    return common::Status::Error(common::StatusCode::PlatformError, "Failed to create content text format.");
  }
  content_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);

  hr = CreateIconTextFormatWithFallback(dwrite_factory_.Get(), 18.0f, icon_format_.ReleaseAndGetAddressOf());
  if (FAILED(hr)) {
    return common::Status::Error(common::StatusCode::PlatformError, "创建图标字体失败。");
  }

  body_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
  body_bold_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
  meta_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
  icon_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
  section_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
  title_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);

  // Q2:搜索清除按钮 × 字形格式(原渲染路径每帧创建一份,现一次创建反复用;
  // 放主格式集而不是 Secondary,保证搜索框一有文本即可用)。
  hr = dwrite_factory_->CreateTextFormat(
      L"Segoe UI",
      nullptr,
      DWRITE_FONT_WEIGHT_SEMI_BOLD,
      DWRITE_FONT_STYLE_NORMAL,
      DWRITE_FONT_STRETCH_NORMAL,
      15.0f,
      L"",
      search_clear_format_.ReleaseAndGetAddressOf());
  if (FAILED(hr)) {
    return common::Status::Error(common::StatusCode::PlatformError, "创建搜索清除按钮字体失败。");
  }
  search_clear_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
  search_clear_format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
  search_clear_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
  return common::Status::Ok();
}

common::Status D2dShell::CreateSecondaryTextFormats() {
  HRESULT hr = dwrite_factory_->CreateTextFormat(
      L"Segoe UI",
      nullptr,
      DWRITE_FONT_WEIGHT_NORMAL,
      DWRITE_FONT_STYLE_NORMAL,
      DWRITE_FONT_STRETCH_NORMAL,
      11.0f,
      L"",
      tiny_format_.ReleaseAndGetAddressOf());
  if (FAILED(hr)) {
    return common::Status::Error(common::StatusCode::PlatformError, "创建微型字体失败。");
  }

  hr = CreateIconTextFormatWithFallback(dwrite_factory_.Get(), 13.0f, icon_small_format_.ReleaseAndGetAddressOf());
  if (FAILED(hr)) {
    return common::Status::Error(common::StatusCode::PlatformError, "创建小图标字体失败。");
  }

  hr = dwrite_factory_->CreateTextFormat(
      L"Segoe UI Semibold",
      nullptr,
      DWRITE_FONT_WEIGHT_SEMI_BOLD,
      DWRITE_FONT_STYLE_NORMAL,
      DWRITE_FONT_STRETCH_NORMAL,
      12.0f,
      L"",
      badge_format_.ReleaseAndGetAddressOf());
  if (FAILED(hr)) {
    return common::Status::Error(common::StatusCode::PlatformError, "创建徽标字体失败。");
  }

  tiny_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
  icon_small_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
  badge_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
  badge_format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
  return common::Status::Ok();
}

namespace {

// C1：调色板唯一权威定义。语义化取色（ColorXxx）与具名刷共用同一份。
struct ThemePalette {
  D2D1_COLOR_F background;
  D2D1_COLOR_F surface;
  D2D1_COLOR_F surface_alt;
  D2D1_COLOR_F surface_hover;
  D2D1_COLOR_F surface_selected;
  D2D1_COLOR_F border;
  D2D1_COLOR_F accent;
  D2D1_COLOR_F accent_soft;
  D2D1_COLOR_F text;
  D2D1_COLOR_F muted_text;
  D2D1_COLOR_F subtle_text;
  D2D1_COLOR_F danger;
  D2D1_COLOR_F success;
  D2D1_COLOR_F white;
};

const ThemePalette kLightPalette = {
    D2D1::ColorF(0xF3F4F6), D2D1::ColorF(0xFFFFFF), D2D1::ColorF(0xF7F9FD),
    D2D1::ColorF(0xEFF4FF), D2D1::ColorF(0xDCE8FF), D2D1::ColorF(0xD7DFED),
    D2D1::ColorF(0x3A7AFE), D2D1::ColorF(0xE7F0FF), D2D1::ColorF(0x23262D),
    D2D1::ColorF(0x5E6573), D2D1::ColorF(0x8D96A6), D2D1::ColorF(0xD24A4A),
    D2D1::ColorF(0x26A65B), D2D1::ColorF(0xFFFFFF)};

const ThemePalette kDarkPalette = {
    D2D1::ColorF(0x1C1C1E), D2D1::ColorF(0x2C2C2E), D2D1::ColorF(0x3A3A3C),
    D2D1::ColorF(0x323234), D2D1::ColorF(0x3E3E43), D2D1::ColorF(0x38383A),
    D2D1::ColorF(0x0A84FF), D2D1::ColorF(0x1F3350), D2D1::ColorF(0xFFFFFF),
    D2D1::ColorF(0x98989F), D2D1::ColorF(0x636368), D2D1::ColorF(0xFF453A),
    D2D1::ColorF(0x30D158), D2D1::ColorF(0xFFFFFF)};

const ThemePalette& PaletteFor(bool dark) {
  return dark ? kDarkPalette : kLightPalette;
}

}  // namespace

common::Status D2dShell::CreateBrushes() {
  ID2D1RenderTarget* rt = active_target();
  if (rt == nullptr) {
    return common::Status::Error(common::StatusCode::Unavailable, "无可用渲染目标。");
  }
  // Q2:动态色刷随设备资源重建失效(刷子绑定创建时的渲染目标)。
  solid_brush_cache_.clear();

  // C1 暗色模式：明/暗两套调色板，按全局主题取值。浅色保持历史值不变；
  // 深色按 iOS 规范：中性灰阶层级（#1C1C1E → #2C2C2E → #3A3A3C）+
  // 系统色官方暗色值（蓝 #0A84FF / 红 #FF453A / 绿 #30D158）。
  const ThemePalette& palette = IsDarkThemeActive() ? kDarkPalette : kLightPalette;

  HRESULT hr = rt->CreateSolidColorBrush(palette.background, background_brush_.ReleaseAndGetAddressOf());
  if (SUCCEEDED(hr)) hr = rt->CreateSolidColorBrush(palette.surface, surface_brush_.ReleaseAndGetAddressOf());
  if (SUCCEEDED(hr)) hr = rt->CreateSolidColorBrush(palette.surface_alt, surface_alt_brush_.ReleaseAndGetAddressOf());
  if (SUCCEEDED(hr)) hr = rt->CreateSolidColorBrush(palette.surface_hover, surface_hover_brush_.ReleaseAndGetAddressOf());
  if (SUCCEEDED(hr)) hr = rt->CreateSolidColorBrush(palette.surface_selected, surface_selected_brush_.ReleaseAndGetAddressOf());
  if (SUCCEEDED(hr)) hr = rt->CreateSolidColorBrush(palette.border, border_brush_.ReleaseAndGetAddressOf());
  if (SUCCEEDED(hr)) hr = rt->CreateSolidColorBrush(palette.accent, accent_brush_.ReleaseAndGetAddressOf());
  if (SUCCEEDED(hr)) hr = rt->CreateSolidColorBrush(palette.accent_soft, accent_soft_brush_.ReleaseAndGetAddressOf());
  if (SUCCEEDED(hr)) hr = rt->CreateSolidColorBrush(palette.text, text_brush_.ReleaseAndGetAddressOf());
  if (SUCCEEDED(hr)) hr = rt->CreateSolidColorBrush(palette.muted_text, muted_text_brush_.ReleaseAndGetAddressOf());
  if (SUCCEEDED(hr)) hr = rt->CreateSolidColorBrush(palette.subtle_text, subtle_text_brush_.ReleaseAndGetAddressOf());
  if (SUCCEEDED(hr)) hr = rt->CreateSolidColorBrush(palette.danger, danger_brush_.ReleaseAndGetAddressOf());
  if (SUCCEEDED(hr)) hr = rt->CreateSolidColorBrush(palette.success, success_brush_.ReleaseAndGetAddressOf());
  if (SUCCEEDED(hr)) hr = rt->CreateSolidColorBrush(palette.white, white_brush_.ReleaseAndGetAddressOf());
  if (SUCCEEDED(hr)) {
    brushes_theme_epoch_ = ThemeEpoch();
    clipd2d::common::MarkStartupPhase("d2d_brushes_ready");
  }
  return FromHr(hr, "创建 D2D 画刷失败。");
}

D2D1_COLOR_F D2dShell::ColorBackground() const { return PaletteFor(IsDarkThemeActive()).background; }
D2D1_COLOR_F D2dShell::ColorSurface() const { return PaletteFor(IsDarkThemeActive()).surface; }
D2D1_COLOR_F D2dShell::ColorSurfaceAlt() const { return PaletteFor(IsDarkThemeActive()).surface_alt; }
D2D1_COLOR_F D2dShell::ColorSurfaceHover() const { return PaletteFor(IsDarkThemeActive()).surface_hover; }
D2D1_COLOR_F D2dShell::ColorSurfaceSelected() const { return PaletteFor(IsDarkThemeActive()).surface_selected; }
D2D1_COLOR_F D2dShell::ColorBorder() const { return PaletteFor(IsDarkThemeActive()).border; }
D2D1_COLOR_F D2dShell::ColorAccent() const { return PaletteFor(IsDarkThemeActive()).accent; }
D2D1_COLOR_F D2dShell::ColorAccentSoft() const { return PaletteFor(IsDarkThemeActive()).accent_soft; }
D2D1_COLOR_F D2dShell::ColorText() const { return PaletteFor(IsDarkThemeActive()).text; }
D2D1_COLOR_F D2dShell::ColorMutedText() const { return PaletteFor(IsDarkThemeActive()).muted_text; }
D2D1_COLOR_F D2dShell::ColorSubtleText() const { return PaletteFor(IsDarkThemeActive()).subtle_text; }

void SetDarkThemeActive(bool dark) {
  if (g_dark_theme_active != dark) {
    g_dark_theme_active = dark;
    ++g_theme_epoch;
  }
}

bool D2dShell::SyncTheme() {
  if (active_target() == nullptr || brushes_theme_epoch_ == ThemeEpoch()) {
    return false;
  }
  return CreateBrushes().ok();
}

D2D1_COLOR_F D2dShell::themed_color(const D2D1_COLOR_F& light, const D2D1_COLOR_F& dark) const {
  return IsDarkThemeActive() ? dark : light;
}

ID2D1SolidColorBrush* D2dShell::solid_brush(ID2D1RenderTarget* target, const D2D1_COLOR_F& color) {
  // Q2:按颜色键缓存实心刷,消掉 settings/overlay/行渲染里每帧
  // CreateSolidColorBrush 的开销(对齐具名刷缓存模式)。
  if (target == nullptr) {
    return nullptr;
  }
  const auto to_byte = [](float value) -> std::uint32_t {
    if (value < 0.0f) {
      value = 0.0f;
    } else if (value > 1.0f) {
      value = 1.0f;
    }
    return static_cast<std::uint32_t>(value * 255.0f + 0.5f);
  };
  const std::uint32_t key = (to_byte(color.r) << 24) | (to_byte(color.g) << 16) |
      (to_byte(color.b) << 8) | to_byte(color.a);
  const auto it = solid_brush_cache_.find(key);
  if (it != solid_brush_cache_.end() && it->second != nullptr) {
    return it->second.Get();
  }
  // 调色板来自用户标签颜色,封顶防无界增长;溢出即整表重建(创建很便宜)。
  if (solid_brush_cache_.size() >= 64) {
    solid_brush_cache_.clear();
  }
  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
  if (FAILED(target->CreateSolidColorBrush(color, brush.ReleaseAndGetAddressOf())) ||
      brush == nullptr) {
    return nullptr;
  }
  ID2D1SolidColorBrush* raw = brush.Get();
  solid_brush_cache_[key] = std::move(brush);
  return raw;
}

void D2dShell::WaitForFrameLatency(DWORD timeout_ms) {
  // P2-7:渲染前等 DXGI 放行(上一帧已 retire/缓冲可用)。超时兜底:设备丢失
  // 或驱动长时间不给信号时按节流降级继续,绝不把消息线程吊死在等待上。
  if (frame_latency_waitable_ == nullptr) {
    return;
  }
  (void)::WaitForSingleObjectEx(frame_latency_waitable_, timeout_ms, FALSE);
}

void D2dShell::ReleaseDeviceResources() {
  solid_brush_cache_.clear();
  white_brush_.Reset();
  success_brush_.Reset();
  danger_brush_.Reset();
  subtle_text_brush_.Reset();
  muted_text_brush_.Reset();
  text_brush_.Reset();
  accent_soft_brush_.Reset();
  accent_brush_.Reset();
  border_brush_.Reset();
  surface_selected_brush_.Reset();
  surface_hover_brush_.Reset();
  surface_alt_brush_.Reset();
  surface_brush_.Reset();
  background_brush_.Reset();
  render_target_.Reset();

  dcomp_visual_.Reset();
  dcomp_target_.Reset();
  dcomp_device_.Reset();

  target_bitmap_.Reset();
  swap_chain_.Reset();
  if (frame_latency_waitable_ != nullptr) {
    ::CloseHandle(frame_latency_waitable_);  // P2-7
    frame_latency_waitable_ = nullptr;
  }
  device_context_.Reset();
  d2d_device_.Reset();

  content_format_.Reset();
}

}  // namespace clipd2d::ui
