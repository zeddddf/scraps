#include "ui/HoverHintWindow.h"

#include <algorithm>
#include <cmath>

namespace clipd2d::ui {

namespace {

constexpr wchar_t kHoverHintWindowClassName[] = L"Scraps.HoverHintWindow";

}  // namespace

HoverHintWindow::~HoverHintWindow() {
  Destroy();
}

common::Status HoverHintWindow::Create(HINSTANCE instance, HWND owner) {
  instance_ = instance;
  owner_ = owner;
  WNDCLASSEXW window_class{};
  window_class.cbSize = sizeof(window_class);
  window_class.lpfnWndProc = &HoverHintWindow::StaticWindowProc;
  window_class.hInstance = instance_;
  window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  window_class.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  window_class.lpszClassName = kHoverHintWindowClassName;
  RegisterClassExW(&window_class);

  hwnd_ = CreateWindowExW(
      WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_LAYERED,
      kHoverHintWindowClassName,
      L"",
      WS_POPUP,
      CW_USEDEFAULT,
      CW_USEDEFAULT,
      10,
      10,
      owner_,
      nullptr,
      instance_,
      this);
  if (hwnd_ == nullptr) {
    return common::Status::Error(common::StatusCode::PlatformError, "Create hover hint window failed.");
  }
  const common::Status status = EnsureDeviceIndependentResources();
  if (!status.ok()) {
    return status;
  }
  ShowWindow(hwnd_, SW_HIDE);
  return common::Status::Ok();
}

void HoverHintWindow::Destroy() {
  ReleaseDeviceResources();
  text_brush_.Reset();
  border_brush_.Reset();
  background_brush_.Reset();
  text_format_.Reset();
  dwrite_factory_.Reset();
  d2d_factory_.Reset();
  if (hwnd_ != nullptr) {
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
}

common::Status HoverHintWindow::EnsureDeviceIndependentResources() {
  if (!d2d_factory_) {
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d_factory_.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
      return common::Status::Error(common::StatusCode::PlatformError, "Create hover hint D2D factory failed.");
    }
  }
  if (!dwrite_factory_) {
    HRESULT hr = DWriteCreateFactory(
        DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(dwrite_factory_.ReleaseAndGetAddressOf()));
    if (FAILED(hr)) {
      return common::Status::Error(common::StatusCode::PlatformError, "Create hover hint DWrite factory failed.");
    }
  }
  if (!text_format_) {
    HRESULT hr = dwrite_factory_->CreateTextFormat(
        L"Segoe UI",
        nullptr,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        12.0f,
        L"",
        text_format_.ReleaseAndGetAddressOf());
    if (FAILED(hr)) {
      return common::Status::Error(common::StatusCode::PlatformError, "Create hover hint text format failed.");
    }
    text_format_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    text_format_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    text_format_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
  }
  return common::Status::Ok();
}

void HoverHintWindow::ReleaseDeviceResources() {
  text_brush_.Reset();
  border_brush_.Reset();
  background_brush_.Reset();
  dc_render_target_.Reset();
  if (memory_dc_ != nullptr) {
    if (previous_bitmap_ != nullptr) {
      SelectObject(memory_dc_, previous_bitmap_);
      previous_bitmap_ = nullptr;
    }
    DeleteDC(memory_dc_);
    memory_dc_ = nullptr;
  }
  if (dib_bitmap_ != nullptr) {
    DeleteObject(dib_bitmap_);
    dib_bitmap_ = nullptr;
    dib_bits_ = nullptr;
  }
  width_ = 0;
  height_ = 0;
}

common::Status HoverHintWindow::EnsureDeviceResources(int width, int height) {
  if (dc_render_target_ != nullptr && width_ == width && height_ == height) {
    return common::Status::Ok();
  }

  ReleaseDeviceResources();

  BITMAPINFO bitmap_info{};
  bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bitmap_info.bmiHeader.biWidth = width;
  bitmap_info.bmiHeader.biHeight = -height;
  bitmap_info.bmiHeader.biPlanes = 1;
  bitmap_info.bmiHeader.biBitCount = 32;
  bitmap_info.bmiHeader.biCompression = BI_RGB;

  HDC screen_dc = GetDC(nullptr);
  if (screen_dc == nullptr) {
    return common::Status::Error(common::StatusCode::PlatformError, "GetDC failed for hover hint.");
  }
  memory_dc_ = CreateCompatibleDC(screen_dc);
  if (memory_dc_ == nullptr) {
    ReleaseDC(nullptr, screen_dc);
    return common::Status::Error(common::StatusCode::PlatformError, "CreateCompatibleDC failed for hover hint.");
  }
  dib_bitmap_ = CreateDIBSection(screen_dc, &bitmap_info, DIB_RGB_COLORS, &dib_bits_, nullptr, 0);
  ReleaseDC(nullptr, screen_dc);
  if (dib_bitmap_ == nullptr || dib_bits_ == nullptr) {
    ReleaseDeviceResources();
    return common::Status::Error(common::StatusCode::PlatformError, "CreateDIBSection failed for hover hint.");
  }
  previous_bitmap_ = static_cast<HBITMAP>(SelectObject(memory_dc_, dib_bitmap_));

  const D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
      D2D1_RENDER_TARGET_TYPE_DEFAULT,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
  HRESULT hr = d2d_factory_->CreateDCRenderTarget(
      &properties,
      dc_render_target_.ReleaseAndGetAddressOf());
  if (FAILED(hr)) {
    ReleaseDeviceResources();
    return common::Status::Error(common::StatusCode::PlatformError, "CreateDCRenderTarget failed for hover hint.");
  }

  RECT rect{0, 0, width, height};
  hr = dc_render_target_->BindDC(memory_dc_, &rect);
  if (FAILED(hr)) {
    ReleaseDeviceResources();
    return common::Status::Error(common::StatusCode::PlatformError, "BindDC failed for hover hint.");
  }

  hr = dc_render_target_->CreateSolidColorBrush(
      D2D1::ColorF(0.137f, 0.149f, 0.176f, 0.60f),
      background_brush_.ReleaseAndGetAddressOf());
  if (SUCCEEDED(hr)) {
    hr = dc_render_target_->CreateSolidColorBrush(
        D2D1::ColorF(0.294f, 0.333f, 0.388f, 0.74f),
        border_brush_.ReleaseAndGetAddressOf());
  }
  if (SUCCEEDED(hr)) {
    hr = dc_render_target_->CreateSolidColorBrush(
        D2D1::ColorF(0.961f, 0.969f, 0.980f, 1.0f),
        text_brush_.ReleaseAndGetAddressOf());
  }
  if (FAILED(hr)) {
    ReleaseDeviceResources();
    return common::Status::Error(common::StatusCode::PlatformError, "Create brushes failed for hover hint.");
  }

  width_ = width;
  height_ = height;
  return common::Status::Ok();
}

common::Status HoverHintWindow::MeasureText(const std::wstring& text, int max_width, int* width, int* height) {
  if (width == nullptr || height == nullptr) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "MeasureText outputs cannot be null.");
  }
  Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
  HRESULT hr = dwrite_factory_->CreateTextLayout(
      text.c_str(),
      static_cast<UINT32>(text.size()),
      text_format_.Get(),
      static_cast<FLOAT>(max_width),
      64.0f,
      layout.ReleaseAndGetAddressOf());
  if (FAILED(hr)) {
    return common::Status::Error(common::StatusCode::PlatformError, "CreateTextLayout failed for hover hint.");
  }
  DWRITE_TEXT_METRICS metrics{};
  hr = layout->GetMetrics(&metrics);
  if (FAILED(hr)) {
    return common::Status::Error(common::StatusCode::PlatformError, "GetMetrics failed for hover hint.");
  }
  *width = static_cast<int>(std::ceil(metrics.widthIncludingTrailingWhitespace));
  *height = static_cast<int>(std::ceil(metrics.height));
  return common::Status::Ok();
}

common::Status HoverHintWindow::PresentLayered(int width, int height) {
  POINT source_point{0, 0};
  SIZE size{width, height};
  BLENDFUNCTION blend{};
  blend.BlendOp = AC_SRC_OVER;
  blend.SourceConstantAlpha = 255;
  blend.AlphaFormat = AC_SRC_ALPHA;
  HDC screen_dc = GetDC(nullptr);
  if (screen_dc == nullptr) {
    return common::Status::Error(common::StatusCode::PlatformError, "GetDC failed for layered hover hint.");
  }
  RECT window_rect{};
  GetWindowRect(hwnd_, &window_rect);
  POINT window_point{window_rect.left, window_rect.top};
  const BOOL updated = UpdateLayeredWindow(
      hwnd_,
      screen_dc,
      &window_point,
      &size,
      memory_dc_,
      &source_point,
      0,
      &blend,
      ULW_ALPHA);
  ReleaseDC(nullptr, screen_dc);
  if (!updated) {
    return common::Status::Error(common::StatusCode::PlatformError, "UpdateLayeredWindow failed for hover hint.");
  }
  return common::Status::Ok();
}

void HoverHintWindow::ShowText(std::wstring text, POINT screen_anchor) {
  if (hwnd_ == nullptr || text.empty()) {
    return;
  }
  text_ = std::move(text);
  const common::Status setup_status = EnsureDeviceIndependentResources();
  if (!setup_status.ok()) {
    return;
  }
  int text_width = 0;
  int text_height = 0;
  if (!MeasureText(text_, 260, &text_width, &text_height).ok()) {
    return;
  }
  const int width = std::clamp(std::max(48, text_width + 20), 48, 280);
  const int height = std::max(26, text_height + 12);
  const common::Status resource_status = EnsureDeviceResources(width, height);
  if (!resource_status.ok()) {
    return;
  }
  SetWindowPos(
      hwnd_,
      HWND_TOPMOST,
      screen_anchor.x + 16,
      screen_anchor.y + 20,
      width,
      height,
      SWP_NOACTIVATE);

  dc_render_target_->BeginDraw();
  dc_render_target_->SetTransform(D2D1::Matrix3x2F::Identity());
  dc_render_target_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
  dc_render_target_->Clear(D2D1::ColorF(0, 0.0f));
  const D2D1_ROUNDED_RECT background =
      D2D1::RoundedRect(D2D1::RectF(0.5f, 0.5f, static_cast<FLOAT>(width) - 0.5f, static_cast<FLOAT>(height) - 0.5f), 10.0f, 10.0f);
  dc_render_target_->FillRoundedRectangle(background, background_brush_.Get());
  dc_render_target_->DrawRoundedRectangle(background, border_brush_.Get(), 1.0f);

  Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
  if (SUCCEEDED(dwrite_factory_->CreateTextLayout(
          text_.c_str(),
          static_cast<UINT32>(text_.size()),
          text_format_.Get(),
          static_cast<FLOAT>(width - 20),
          static_cast<FLOAT>(height - 12),
          layout.ReleaseAndGetAddressOf()))) {
    DWRITE_TEXT_METRICS metrics{};
    if (SUCCEEDED(layout->GetMetrics(&metrics))) {
      const float draw_x = std::max(10.0f, (static_cast<float>(width) - metrics.widthIncludingTrailingWhitespace) * 0.5f);
      const float draw_y = std::max(6.0f, (static_cast<float>(height) - metrics.height) * 0.5f);
      dc_render_target_->DrawTextLayout(
          D2D1::Point2F(draw_x, draw_y),
          layout.Get(),
          text_brush_.Get(),
          D2D1_DRAW_TEXT_OPTIONS_CLIP);
    } else {
      dc_render_target_->DrawTextLayout(
          D2D1::Point2F(10.0f, 6.0f),
          layout.Get(),
          text_brush_.Get(),
          D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
  }
  if (FAILED(dc_render_target_->EndDraw())) {
    return;
  }
  (void)PresentLayered(width, height);
  ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
}

void HoverHintWindow::Hide() {
  if (hwnd_ != nullptr) {
    ShowWindow(hwnd_, SW_HIDE);
  }
}

LRESULT CALLBACK HoverHintWindow::StaticWindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
  HoverHintWindow* self = nullptr;
  if (message == WM_NCCREATE) {
    auto* create_struct = reinterpret_cast<CREATESTRUCTW*>(l_param);
    self = static_cast<HoverHintWindow*>(create_struct->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  } else {
    self = reinterpret_cast<HoverHintWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  }
  if (self != nullptr) {
    self->hwnd_ = hwnd;
    return self->WindowProc(message, w_param, l_param);
  }
  return DefWindowProcW(hwnd, message, w_param, l_param);
}

LRESULT HoverHintWindow::WindowProc(UINT message, WPARAM w_param, LPARAM l_param) {
  switch (message) {
    case WM_PAINT:
      return 0;
    case WM_ERASEBKGND:
      return 1;
  }
  return DefWindowProcW(hwnd_, message, w_param, l_param);
}

}  // namespace clipd2d::ui
