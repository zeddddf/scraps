#include "ui/QuickPasteWindow.h"

#include <shellapi.h>
#include <shobjidl.h>
#include <wincodec.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "common/Log.h"
#include "ui/ClipImagePreview.h"
#include "ui/ClipVisualAssets.h"
#include "ui/ClipVisualFileContent.h"
#include "ui/D2dUtil.h"
#include "ui/PanelWindowChrome.h"
#include "ui/FluentIcons.h"
#include "ui/PanelWindowChrome.h"

namespace clipd2d::ui {

namespace {

constexpr wchar_t kQuickPasteWindowClassName[] = L"Scraps.QuickPasteWindow";
constexpr int kWindowWidth = 350;
constexpr int kOuterPadding = 8;
constexpr int kRowHeight = 50;
constexpr int kPreviewInset = 1;
constexpr int kNumberColumnWidth = 22;
constexpr int kRowGap = 2;
constexpr int kAnchorOffsetY = 8;
constexpr int kAnchorGapY = 14;
constexpr int kAnchorGapAboveY = 28;
constexpr int kSourceIconSize = 14;
constexpr int kPreviewMaxWidth = 92;
constexpr int kTextMinWidth = 150;
constexpr int kTextTopInset = 3;
constexpr int kTextBottomInset = 3;
RECT MakeRect(int left, int top, int right, int bottom) {
  return RECT{left, top, right, bottom};
}

RECT InsetRectCopy(const RECT& rect, int dx, int dy) {
  return RECT{rect.left + dx, rect.top + dy, rect.right - dx, rect.bottom - dy};
}

void FillRoundedPanel(
    ID2D1RenderTarget* target,
    const RECT& rect,
    float radius,
    ID2D1Brush* fill_brush,
    ID2D1Brush* stroke_brush,
    float stroke_width) {
  const D2D1_ROUNDED_RECT rounded = D2D1::RoundedRect(ToRectF(rect), radius, radius);
  if (fill_brush != nullptr) {
    target->FillRoundedRectangle(rounded, fill_brush);
  }
  if (stroke_brush != nullptr && stroke_width > 0.0f) {
    target->DrawRoundedRectangle(rounded, stroke_brush, stroke_width);
  }
}

void DrawTextLine(
    ID2D1RenderTarget* target,
    IDWriteTextFormat* format,
    ID2D1Brush* brush,
    const RECT& rect,
    std::wstring_view text) {
  if (target == nullptr || format == nullptr || brush == nullptr || text.empty()) {
    return;
  }
  target->DrawTextW(
      text.data(),
      static_cast<UINT32>(text.size()),
      format,
      ToRectF(rect),
      brush,
      D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void DrawWrappedTextBlock(
    IDWriteFactory* factory,
    ID2D1RenderTarget* target,
    IDWriteTextFormat* format,
    ID2D1Brush* brush,
    const RECT& rect,
    std::wstring_view text) {
  if (factory == nullptr || target == nullptr || format == nullptr || brush == nullptr || text.empty()) {
    return;
  }

  const float width = static_cast<float>(std::max<LONG>(1, rect.right - rect.left));
  const float height = static_cast<float>(std::max<LONG>(1, rect.bottom - rect.top));

  Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
  if (FAILED(factory->CreateTextLayout(
          text.data(),
          static_cast<UINT32>(text.size()),
          format,
          width,
          height,
          layout.ReleaseAndGetAddressOf())) ||
      layout == nullptr) {
    target->DrawTextW(
        text.data(),
        static_cast<UINT32>(text.size()),
        format,
        ToRectF(rect),
        brush,
        D2D1_DRAW_TEXT_OPTIONS_CLIP);
    return;
  }

  layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
  layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
  layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

  Microsoft::WRL::ComPtr<IDWriteInlineObject> trimming_sign;
  if (SUCCEEDED(factory->CreateEllipsisTrimmingSign(format, trimming_sign.ReleaseAndGetAddressOf())) &&
      trimming_sign != nullptr) {
    const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
    layout->SetTrimming(&trimming, trimming_sign.Get());
  }

  DWRITE_TEXT_METRICS metrics{};
  float draw_top = static_cast<float>(rect.top);
  if (SUCCEEDED(layout->GetMetrics(&metrics))) {
    const float draw_height = std::min(height, metrics.height);
    draw_top += std::max(0.0f, (height - draw_height) * 0.5f);
  }

  target->PushAxisAlignedClip(ToRectF(rect), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
  target->DrawTextLayout(
      D2D1::Point2F(static_cast<float>(rect.left), draw_top),
      layout.Get(),
      brush,
      D2D1_DRAW_TEXT_OPTIONS_CLIP);
  target->PopAxisAlignedClip();
}

RECT AlignRectVerticalCenter(const RECT& outer, int content_height) {
  const int available_height = static_cast<int>(outer.bottom - outer.top);
  const int offset = std::max(0, (available_height - content_height) / 2);
  const int top = static_cast<int>(outer.top) + offset;
  return MakeRect(outer.left, top, outer.right, top + content_height);
}

RECT ComputeBitmapFitRect(const RECT& slot_rect, ID2D1Bitmap* bitmap, int max_width) {
  if (bitmap == nullptr) {
    return slot_rect;
  }

  const D2D1_SIZE_F bitmap_size = bitmap->GetSize();
  if (bitmap_size.width <= 0.0f || bitmap_size.height <= 0.0f) {
    return slot_rect;
  }

  const int slot_height = std::max(1, static_cast<int>(slot_rect.bottom - slot_rect.top));
  const float aspect = bitmap_size.width / bitmap_size.height;
  const int fit_width = std::max(1, static_cast<int>(std::round(static_cast<float>(slot_height) * aspect)));
  const int capped_width = std::max(1, std::min(fit_width, max_width));
  return MakeRect(slot_rect.left, slot_rect.top, slot_rect.left + capped_width, slot_rect.bottom);
}

bool LowercaseEndsWith(const std::wstring& value, const wchar_t* suffix) {
  const std::wstring lowered_value = std::filesystem::path(value).extension().wstring();
  const std::wstring lowered_suffix = suffix;
  std::wstring value_lower = lowered_value;
  std::wstring suffix_lower = lowered_suffix;
  std::transform(value_lower.begin(), value_lower.end(), value_lower.begin(), towlower);
  std::transform(suffix_lower.begin(), suffix_lower.end(), suffix_lower.begin(), towlower);
  return value_lower == suffix_lower;
}

bool SupportsSystemThumbnailPreviewPath(const std::wstring& path) {
  return LowercaseEndsWith(path, L".png") || LowercaseEndsWith(path, L".jpg") ||
      LowercaseEndsWith(path, L".jpeg") || LowercaseEndsWith(path, L".bmp") ||
      LowercaseEndsWith(path, L".gif") || LowercaseEndsWith(path, L".webp") ||
      LowercaseEndsWith(path, L".tif") || LowercaseEndsWith(path, L".tiff") ||
      LowercaseEndsWith(path, L".ico") || LowercaseEndsWith(path, L".heic") ||
      LowercaseEndsWith(path, L".mp4") || LowercaseEndsWith(path, L".mov") ||
      LowercaseEndsWith(path, L".m4v") || LowercaseEndsWith(path, L".avi") ||
      LowercaseEndsWith(path, L".mkv") || LowercaseEndsWith(path, L".webm") ||
      LowercaseEndsWith(path, L".wmv");
}

bool LoadSystemThumbnailPixels(
    const std::wstring& path,
    int target_width,
    int target_height,
    UINT* width,
    UINT* height,
    std::vector<std::uint8_t>* pixels) {
  if (path.empty() || width == nullptr || height == nullptr || pixels == nullptr) {
    return false;
  }

  *width = 0;
  *height = 0;
  pixels->clear();

  const HRESULT init_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  const bool need_uninit = SUCCEEDED(init_result);

  Microsoft::WRL::ComPtr<IShellItemImageFactory> image_factory;
  const HRESULT create_result = SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&image_factory));
  if (FAILED(create_result) || image_factory == nullptr) {
    if (need_uninit) {
      CoUninitialize();
    }
    return false;
  }

  SIZE requested_size{target_width, target_height};
  HBITMAP bitmap = nullptr;
  const HRESULT image_result = image_factory->GetImage(
      requested_size,
      SIIGBF_BIGGERSIZEOK | SIIGBF_RESIZETOFIT | SIIGBF_THUMBNAILONLY,
      &bitmap);
  if (FAILED(image_result) || bitmap == nullptr) {
    if (bitmap != nullptr) {
      DeleteObject(bitmap);
    }
    if (need_uninit) {
      CoUninitialize();
    }
    return false;
  }

  BITMAP info{};
  if (GetObjectW(bitmap, sizeof(info), &info) == 0 || info.bmWidth <= 0 || info.bmHeight <= 0) {
    DeleteObject(bitmap);
    if (need_uninit) {
      CoUninitialize();
    }
    return false;
  }

  *width = static_cast<UINT>(info.bmWidth);
  *height = static_cast<UINT>(info.bmHeight);

  BITMAPINFO bitmap_info{};
  bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bitmap_info.bmiHeader.biWidth = info.bmWidth;
  bitmap_info.bmiHeader.biHeight = -info.bmHeight;
  bitmap_info.bmiHeader.biPlanes = 1;
  bitmap_info.bmiHeader.biBitCount = 32;
  bitmap_info.bmiHeader.biCompression = BI_RGB;

  const size_t pixel_bytes = static_cast<size_t>(info.bmWidth) * static_cast<size_t>(info.bmHeight) * 4u;
  pixels->resize(pixel_bytes);

  HDC screen_dc = GetDC(nullptr);
  const int copied = GetDIBits(
      screen_dc,
      bitmap,
      0,
      static_cast<UINT>(info.bmHeight),
      pixels->data(),
      &bitmap_info,
      DIB_RGB_COLORS);
  ReleaseDC(nullptr, screen_dc);
  DeleteObject(bitmap);

  if (need_uninit) {
    CoUninitialize();
  }
  if (copied == 0) {
    pixels->clear();
    *width = 0;
    *height = 0;
    return false;
  }
  return true;
}

common::Status LoadScaledBitmapPixels(
    const std::wstring& path,
    int max_width,
    int max_height,
    UINT* width,
    UINT* height,
    std::vector<std::uint8_t>* pixels) {
  if (width == nullptr || height == nullptr || pixels == nullptr || path.empty()) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "Invalid quick paste image decode request.");
  }

  *width = 0;
  *height = 0;
  pixels->clear();

  const HRESULT init_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  const bool need_uninit = SUCCEEDED(init_result);

  IWICImagingFactory* factory = nullptr;
  HRESULT hr = CoCreateInstance(
      CLSID_WICImagingFactory,
      nullptr,
      CLSCTX_INPROC_SERVER,
      IID_PPV_ARGS(&factory));
  if (FAILED(hr)) {
    if (need_uninit) {
      CoUninitialize();
    }
    return common::Status::Error(common::StatusCode::PlatformError, "Create quick paste WIC factory failed.");
  }

  IWICBitmapDecoder* decoder = nullptr;
  hr = factory->CreateDecoderFromFilename(
      path.c_str(),
      nullptr,
      GENERIC_READ,
      WICDecodeMetadataCacheOnLoad,
      &decoder);
  if (FAILED(hr)) {
    factory->Release();
    if (need_uninit) {
      CoUninitialize();
    }
    return common::Status::Error(common::StatusCode::NotFound, "Open quick paste image failed.");
  }

  IWICBitmapFrameDecode* frame = nullptr;
  hr = decoder->GetFrame(0, &frame);
  UINT source_width = 0;
  UINT source_height = 0;
  if (SUCCEEDED(hr)) {
    hr = frame->GetSize(&source_width, &source_height);
  }
  if (FAILED(hr) || source_width == 0 || source_height == 0) {
    if (frame != nullptr) {
      frame->Release();
    }
    decoder->Release();
    factory->Release();
    if (need_uninit) {
      CoUninitialize();
    }
    return common::Status::Error(common::StatusCode::PlatformError, "Read quick paste image size failed.");
  }

  const double scale_x = static_cast<double>(max_width) / static_cast<double>(source_width);
  const double scale_y = static_cast<double>(max_height) / static_cast<double>(source_height);
  const double scale = std::min(1.0, std::min(scale_x, scale_y));
  *width = std::max<UINT>(1, static_cast<UINT>(source_width * scale));
  *height = std::max<UINT>(1, static_cast<UINT>(source_height * scale));

  IWICBitmapScaler* scaler = nullptr;
  hr = factory->CreateBitmapScaler(&scaler);
  if (SUCCEEDED(hr)) {
    hr = scaler->Initialize(frame, *width, *height, WICBitmapInterpolationModeFant);
  }

  IWICFormatConverter* converter = nullptr;
  if (SUCCEEDED(hr)) {
    hr = factory->CreateFormatConverter(&converter);
  }
  if (SUCCEEDED(hr)) {
    hr = converter->Initialize(
        scaler,
        GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0,
        WICBitmapPaletteTypeCustom);
  }
  if (FAILED(hr)) {
    if (converter != nullptr) {
      converter->Release();
    }
    if (scaler != nullptr) {
      scaler->Release();
    }
    frame->Release();
    decoder->Release();
    factory->Release();
    if (need_uninit) {
      CoUninitialize();
    }
    return common::Status::Error(common::StatusCode::PlatformError, "Prepare quick paste image conversion failed.");
  }

  const UINT stride = *width * 4;
  pixels->resize(static_cast<size_t>(stride) * static_cast<size_t>(*height));
  hr = converter->CopyPixels(nullptr, stride, static_cast<UINT>(pixels->size()), pixels->data());

  converter->Release();
  scaler->Release();
  frame->Release();
  decoder->Release();
  factory->Release();
  if (need_uninit) {
    CoUninitialize();
  }

  if (FAILED(hr)) {
    pixels->clear();
    *width = 0;
    *height = 0;
    return common::Status::Error(common::StatusCode::PlatformError, "Copy quick paste image pixels failed.");
  }
  return common::Status::Ok();
}



FluentIcon ResolveGlyphIcon(core::browser::QuickPasteVisualKind kind, core::store::ClipContentType type) {
  if (kind == core::browser::QuickPasteVisualKind::ImageGlyph ||
      type == core::store::ClipContentType::Image) {
    return FluentIcon::Image;
  }
  if (kind == core::browser::QuickPasteVisualKind::FolderGlyph ||
      type == core::store::ClipContentType::Files) {
    return FluentIcon::Folder;
  }
  return FluentIcon::Clipboard;
}

}  // namespace

QuickPasteWindow::~QuickPasteWindow() {
  Destroy();
}

common::Status QuickPasteWindow::Create(HINSTANCE instance, HWND owner) {
  instance_ = instance;
  owner_ = owner;

  WNDCLASSEXW window_class{};
  window_class.cbSize = sizeof(window_class);
  window_class.style = kPopupPanelClassStyle;
  window_class.lpfnWndProc = &QuickPasteWindow::StaticWindowProc;
  window_class.hInstance = instance_;
  window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  window_class.hbrBackground = nullptr;
  window_class.lpszClassName = kQuickPasteWindowClassName;
  RegisterClassExW(&window_class);

  auto try_create = [this](HWND owner_window) -> HWND {
    return CreateWindowExW(
        kPopupPanelWindowExStyle,
        kQuickPasteWindowClassName,
        L"Scraps Quick Paste",
        kPopupPanelWindowStyle,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        kWindowWidth,
        100,
        owner_window,
        nullptr,
        instance_,
        this);
  };

  hwnd_ = try_create(owner_);
  if (hwnd_ == nullptr) {
    const DWORD owner_error = GetLastError();
    common::WriteLog(
        common::LogLevel::Warning,
        "quick_paste.create",
        "CreateWindowEx with owner failed error=" + std::to_string(owner_error));
    hwnd_ = try_create(nullptr);
    if (hwnd_ == nullptr) {
      const DWORD fallback_error = GetLastError();
      common::WriteLog(
          common::LogLevel::Warning,
          "quick_paste.create",
          "CreateWindowEx without owner failed error=" + std::to_string(fallback_error));
      return common::Status::Error(common::StatusCode::PlatformError, "Create quick paste window failed.");
    }
  }

  common::Status status = d2d_shell_.InitializeDComp(hwnd_);
  if (!status.ok()) {
    return status;
  }
  status = d2d_shell_.EnsureSecondaryTextFormats();
  if (!status.ok()) {
    return status;
  }

  ApplyPopupPanelWindowChrome(hwnd_, PopupPanelBorderColor());
  ShowWindow(hwnd_, SW_HIDE);
  return common::Status::Ok();
}

void QuickPasteWindow::Show(
    const POINT& anchor_screen,
    const std::vector<core::browser::QuickPasteCandidate>& candidates,
    int selected_index,
    core::store::WindowBlurType window_blur_type,
    int window_blur_opacity,
    HostActions actions) {
  if (hwnd_ == nullptr) return;

  host_actions_ = std::move(actions);
  window_blur_type_ = window_blur_type;
  window_blur_opacity_ = window_blur_opacity;
  needs_layout_ = true;

  ApplyWindowBlur(hwnd_, window_blur_type);
  
  candidates_.clear();
  candidates_.reserve(candidates.size());
  for (const auto& candidate : candidates) {
    CandidateState state;
    state.candidate = candidate;
    candidates_.push_back(std::move(state));
  }
  selected_index_ = candidates_.empty() ? -1 : std::clamp(selected_index, 0, static_cast<int>(candidates_.size()) - 1);
  hover_index_ = -1;
  anchor_screen_ = anchor_screen;
  bitmap_generation_ = 0;
  ReleaseBitmaps();
  LayoutWindow();
  visible_ = true;
  ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
  SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void QuickPasteWindow::Hide() {
  if (hwnd_ == nullptr) {
    return;
  }
  visible_ = false;
  ShowWindow(hwnd_, SW_HIDE);
  ReleaseBitmaps();
  host_actions_ = {};
  candidates_.clear();
  row_rects_.clear();
  selected_index_ = -1;
  hover_index_ = -1;
}

void QuickPasteWindow::Destroy() {
  ReleaseBitmaps();
  visible_ = false;
  candidates_.clear();
  row_rects_.clear();
  if (hwnd_ != nullptr) {
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
  d2d_shell_.Shutdown();
}

bool QuickPasteWindow::ContainsScreenPoint(POINT point) const {
  if (hwnd_ == nullptr || !visible_) {
    return false;
  }
  RECT window_rect{};
  GetWindowRect(hwnd_, &window_rect);
  return PtInRect(&window_rect, point) != FALSE;
}

LRESULT CALLBACK QuickPasteWindow::StaticWindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
  QuickPasteWindow* self = nullptr;
  if (message == WM_NCCREATE) {
    auto* create_struct = reinterpret_cast<CREATESTRUCTW*>(l_param);
    self = static_cast<QuickPasteWindow*>(create_struct->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  } else {
    self = reinterpret_cast<QuickPasteWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  }
  if (self != nullptr) {
    self->hwnd_ = hwnd;
    return self->WindowProc(message, w_param, l_param);
  }
  return DefWindowProcW(hwnd, message, w_param, l_param);
}

LRESULT QuickPasteWindow::WindowProc(UINT message, WPARAM w_param, LPARAM l_param) {
  switch (message) {
    case WM_MOUSEACTIVATE:
      return MA_NOACTIVATE;
    case WM_ERASEBKGND:
      return 1;
    case WM_NCCALCSIZE:
      return 0;
    case WM_NCHITTEST:
      return HTCLIENT;
    case WM_SIZE:
      d2d_shell_.Resize(LOWORD(l_param), HIWORD(l_param));
      return 0;
    case WM_PAINT: {
      PAINTSTRUCT paint{};
      BeginPaint(hwnd_, &paint);
      RenderFrame();
      EndPaint(hwnd_, &paint);
      return 0;
    }
    case WM_MOUSEMOVE: {
      POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
      const int index = HitTestCandidate(point);
      if (index != hover_index_) {
        hover_index_ = index;
        if (index >= 0) {
          selected_index_ = index;
        }
        InvalidateRect(hwnd_, nullptr, FALSE);
      }
      return 0;
    }
    case WM_LBUTTONUP: {
      POINT point{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
      const int index = HitTestCandidate(point);
      if (index >= 0) {
        AcceptIndex(index, false);
      }
      return 0;
    }
    case WM_RBUTTONUP:
      if (host_actions_.on_close) {
        host_actions_.on_close();
      }
      return 0;
    case kMoveSelectionMessage:
      MoveSelection(static_cast<int>(static_cast<short>(w_param)));
      return 0;
    case kAcceptCurrentMessage:
      AcceptSelected(l_param != 0);
      return 0;
    case kAcceptIndexMessage:
      AcceptIndex(static_cast<int>(w_param), l_param != 0);
      return 0;
    case kCloseWindowMessage:
      if (host_actions_.on_close) {
        host_actions_.on_close();
      }
      return 0;
  }
  return DefWindowProcW(hwnd_, message, w_param, l_param);
}

void QuickPasteWindow::LayoutWindow() {
  if (hwnd_ == nullptr) {
    return;
  }

  const int count = static_cast<int>(candidates_.size());
  const int content_height =
      kOuterPadding * 2 + std::max(0, count) * kRowHeight + std::max(0, count - 1) * kRowGap;
  const int window_height = std::max(48, content_height);

  HMONITOR monitor = MonitorFromPoint(anchor_screen_, MONITOR_DEFAULTTONEAREST);
  MONITORINFO monitor_info{};
  monitor_info.cbSize = sizeof(monitor_info);
  GetMonitorInfoW(monitor, &monitor_info);
  const RECT work = monitor_info.rcWork;

  int left = anchor_screen_.x;
  int top = anchor_screen_.y + kAnchorOffsetY;
  shown_above_anchor_ = false;
  if (left + kWindowWidth > work.right) {
    left = work.right - kWindowWidth;
  }
  if (left < work.left) {
    left = work.left;
  }
  if (top + window_height > work.bottom) {
    top = anchor_screen_.y - window_height - kAnchorGapAboveY;
    shown_above_anchor_ = true;
  }
  if (top < work.top) {
    top = work.top;
  }

  SetWindowPos(hwnd_, HWND_TOPMOST, left, top, kWindowWidth, window_height, SWP_NOACTIVATE | SWP_SHOWWINDOW);

  row_rects_.clear();
  row_rects_.reserve(candidates_.size());
  for (size_t index = 0; index < candidates_.size(); ++index) {
    const int visual_index = shown_above_anchor_
        ? static_cast<int>(candidates_.size() - 1 - index)
        : static_cast<int>(index);
    const int row_top = kOuterPadding + visual_index * (kRowHeight + kRowGap);
    row_rects_.push_back(MakeRect(kOuterPadding, row_top, kWindowWidth - kOuterPadding, row_top + kRowHeight));
  }
}

void QuickPasteWindow::RenderFrame() {
  // C1 暗色模式：主题纪元变化时重建刷子并刷新 DWM 暗色属性与描边。
  if (d2d_shell_.SyncTheme()) {
    ApplyDarkModeAttribute(hwnd_, clipd2d::ui::IsDarkThemeActive());
    ApplyPopupPanelWindowChrome(hwnd_, PopupPanelBorderColor());
  }
  ID2D1DeviceContext* target = d2d_shell_.BeginFrame1_1();
  if (target == nullptr) {
    return;
  }

  EnsureBitmaps(target);

  RECT client{};
  GetClientRect(hwnd_, &client);
  target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
  target->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

  float bg_alpha = 1.0f;
  if (window_blur_type_ != core::store::WindowBlurType::None) {
      bg_alpha = static_cast<float>(window_blur_opacity_) / 100.0f;
  }
  float old_opacity = d2d_shell_.surface_brush()->GetOpacity();
  d2d_shell_.surface_brush()->SetOpacity(bg_alpha);

  target->FillRectangle(
      D2D1::RectF(0.0f, 0.0f, static_cast<float>(client.right), static_cast<float>(client.bottom)),
      d2d_shell_.surface_brush());
      
  d2d_shell_.surface_brush()->SetOpacity(old_opacity);

  for (size_t index = 0; index < candidates_.size(); ++index) {
    const RECT& row = row_rects_[index];
    const bool selected = static_cast<int>(index) == selected_index_;
    const bool hovered = static_cast<int>(index) == hover_index_;
    FillRoundedPanel(
        target,
        row,
        12.0f,
        selected ? d2d_shell_.accent_soft_brush()
                 : (hovered ? d2d_shell_.surface_hover_brush() : d2d_shell_.surface_alt_brush()),
        nullptr,
        0.0f);

    const RECT number_slot_rect = MakeRect(
        row.left + 8,
        row.top,
        row.left + 8 + kNumberColumnWidth,
        row.bottom);
    const RECT number_rect = AlignRectVerticalCenter(number_slot_rect, 18);
    DrawTextLine(
        target,
        d2d_shell_.small_format(),
        selected ? d2d_shell_.accent_brush() : d2d_shell_.muted_text_brush(),
        number_rect,
        std::to_wstring(index + 1));

    const int source_icon_width = candidates_[index].candidate.source_icon_path.empty() ? 0 : kSourceIconSize;
    const RECT source_icon_rect = source_icon_width > 0
        ? MakeRect(row.right - 10 - source_icon_width, row.top + (kRowHeight - source_icon_width) / 2, row.right - 10, row.top + (kRowHeight - source_icon_width) / 2 + source_icon_width)
        : RECT{};

    int content_left = number_slot_rect.right + 4;
    bool text_drawn_by_shared_file_template = false;
    if (candidates_[index].candidate.content_type == core::store::ClipContentType::Image) {
      const RECT image_rect = MakeRect(
          number_slot_rect.right + 4,
          row.top + 1,
          source_icon_width > 0 ? source_icon_rect.left - 8 : row.right - 10,
          row.bottom - 1);
      const auto image_layout = image_preview::ComputeLayout(
          candidates_[index].bitmap != nullptr ? static_cast<UINT>(candidates_[index].bitmap->GetSize().width) : 0,
          candidates_[index].bitmap != nullptr ? static_cast<UINT>(candidates_[index].bitmap->GetSize().height) : 0,
          image_rect);
      image_preview::Draw(
          target,
          d2d_shell_,
          candidates_[index].bitmap.Get(),
          candidates_[index].bitmap != nullptr ? static_cast<UINT>(candidates_[index].bitmap->GetSize().width) : 0,
          candidates_[index].bitmap != nullptr ? static_cast<UINT>(candidates_[index].bitmap->GetSize().height) : 0,
          image_rect);
      if (image_layout.has_bitmap) {
        content_left = static_cast<int>(std::ceil(image_layout.draw_rect.right)) + 8;
      }
    } else if (
        candidates_[index].candidate.visual_kind == core::browser::QuickPasteVisualKind::FileSystemThumbnail ||
        candidates_[index].candidate.visual_kind == core::browser::QuickPasteVisualKind::FileSystemIcon) {
      const RECT file_content_rect = MakeRect(
          number_slot_rect.right + 4,
          row.top + 2,
          source_icon_width > 0 ? source_icon_rect.left - 8 : row.right - 10,
          row.bottom - 2);
      file_content_preview::Draw(
          target,
          d2d_shell_,
          candidates_[index].bitmap.Get(),
          candidates_[index].bitmap != nullptr ? static_cast<UINT>(candidates_[index].bitmap->GetSize().width) : 0,
          candidates_[index].bitmap != nullptr ? static_cast<UINT>(candidates_[index].bitmap->GetSize().height) : 0,
          file_content_rect,
          candidates_[index].candidate.primary_text,
          candidates_[index].candidate.secondary_text,
          candidates_[index].candidate.summary.file_count,
          candidates_[index].candidate.visual_kind == core::browser::QuickPasteVisualKind::FileSystemThumbnail
              ? file_content_preview::FileVisualMode::ThumbnailCard
              : (candidates_[index].candidate.summary.file_count <= 1
                    ? file_content_preview::FileVisualMode::SingleFileIcon
                    : file_content_preview::FileVisualMode::IconOnly));
      text_drawn_by_shared_file_template = true;
    } else if (candidates_[index].candidate.visual_kind != core::browser::QuickPasteVisualKind::None) {
      const int preview_size = std::max(1, kRowHeight - kPreviewInset * 2);
      const RECT visual_slot_rect = MakeRect(
          number_slot_rect.right + 2,
          row.top + kPreviewInset,
          number_slot_rect.right + 2 + preview_size,
          row.top + kPreviewInset + preview_size);
      RECT visual_rect = visual_slot_rect;
      if (candidates_[index].bitmap != nullptr) {
        const int text_right_boundary = (source_icon_width > 0 ? row.right - 10 - source_icon_width - 8 : row.right - 10);
        const int max_visual_width_by_text = std::max(
            24,
            static_cast<int>(text_right_boundary - visual_slot_rect.left - 8 - kTextMinWidth));
        visual_rect = ComputeBitmapFitRect(
            visual_slot_rect,
            candidates_[index].bitmap.Get(),
            std::min(kPreviewMaxWidth, max_visual_width_by_text));
        target->DrawBitmap(candidates_[index].bitmap.Get(), ToRectF(visual_rect), 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
      } else {
        FillRoundedPanel(
            target,
            visual_slot_rect,
            10.0f,
            d2d_shell_.surface_hover_brush(),
            d2d_shell_.border_brush(),
            1.0f);
        DrawFluentIcon(
            d2d_shell_.d2d_factory(),
            target,
            ResolveGlyphIcon(candidates_[index].candidate.visual_kind, candidates_[index].candidate.content_type),
            ToRectF(InsetRectCopy(visual_slot_rect, 7, 7)),
            selected ? d2d_shell_.accent_brush() : d2d_shell_.muted_text_brush(),
            0.0f);
        visual_rect = visual_slot_rect;
      }
      content_left = visual_rect.right + 8;
    }

    const RECT text_rect = MakeRect(
        content_left,
        row.top + kTextTopInset,
        source_icon_width > 0 ? source_icon_rect.left - 8 : row.right - 10,
        row.bottom - kTextBottomInset);
    const int text_rect_width = std::max(0, static_cast<int>(text_rect.right - text_rect.left));

    if (text_drawn_by_shared_file_template) {
      // Shared file visual template already rendered the primary/secondary text.
    } else if (candidates_[index].candidate.content_type == core::store::ClipContentType::Text ||
        candidates_[index].candidate.content_type == core::store::ClipContentType::Html ||
        candidates_[index].candidate.content_type == core::store::ClipContentType::Rtf) {
      DrawWrappedTextBlock(
          d2d_shell_.dwrite_factory(),
          target,
          d2d_shell_.small_format(),
          d2d_shell_.text_brush(),
          text_rect,
          candidates_[index].candidate.primary_text);
    } else if (text_rect_width > 0) {
      const RECT primary_rect = MakeRect(text_rect.left, text_rect.top, text_rect.right, text_rect.top + 18);
      const RECT secondary_rect = MakeRect(text_rect.left, primary_rect.bottom + 3, text_rect.right, primary_rect.bottom + 18);
      DrawTextLine(
          target,
          d2d_shell_.body_bold_format(),
          d2d_shell_.text_brush(),
          primary_rect,
          candidates_[index].candidate.primary_text);
      DrawTextLine(
          target,
          d2d_shell_.tiny_format(),
          selected ? d2d_shell_.text_brush() : d2d_shell_.muted_text_brush(),
          secondary_rect,
          candidates_[index].candidate.secondary_text);
    }

    if (candidates_[index].source_icon != nullptr && source_icon_width > 0) {
      target->DrawBitmap(
          candidates_[index].source_icon.Get(),
          ToRectF(source_icon_rect),
          1.0f,
          D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }
  }

  d2d_shell_.EndFrame1_1();
}

void QuickPasteWindow::EnsureBitmaps(ID2D1RenderTarget* target) {
  if (target == nullptr) {
    return;
  }
  if (bitmap_generation_ == d2d_shell_.target_generation()) {
    return;
  }

  ReleaseBitmaps();
  bitmap_generation_ = d2d_shell_.target_generation();
  for (auto& candidate_state : candidates_) {
    UINT width = 0;
    UINT height = 0;
    std::vector<std::uint8_t> pixels;

    bool loaded = false;
    const int preview_size = std::max(1, kRowHeight - kPreviewInset * 2);
    const auto request = visual_assets::BuildClipThumbnailLoadRequest(
        candidate_state.candidate.summary,
        visual_assets::ResolveFileThumbnailDecodeExtent(preview_size));
    if (request.kind == visual_assets::ThumbnailLoadKind::ScaledBitmap) {
      loaded = visual_assets::LoadScaledBitmapPixels(
          request.key,
          request.max_width,
          request.max_height,
          &width,
          &height,
          &pixels)
                   .ok();
    } else if (request.kind == visual_assets::ThumbnailLoadKind::SystemThumbnail) {
      loaded = visual_assets::LoadSystemThumbnailPixels(
          request.key,
          request.max_width,
          request.max_height,
          &width,
          &height,
          &pixels);
      if (!loaded && request.allow_fallback_decode) {
        loaded = visual_assets::LoadScaledBitmapPixels(
            request.key,
            request.max_width,
            request.max_height,
            &width,
            &height,
            &pixels)
                     .ok();
      }
    }

    if (loaded && !pixels.empty()) {
      ID2D1Bitmap* bitmap = nullptr;
      if (d2d_shell_.CreateBitmapFromBgra(width, height, pixels, &bitmap).ok()) {
        candidate_state.bitmap.Attach(bitmap);
      }
    }

    // 1. 如果是文件图标类型，同步加载文件本身的图标为 ID2D1Bitmap 存入 candidate_state.bitmap
    if (candidate_state.candidate.visual_kind == core::browser::QuickPasteVisualKind::FileSystemIcon) {
      if (candidate_state.bitmap == nullptr) {
        const std::wstring icon_path = visual_assets::FirstFilePathFromSummary(candidate_state.candidate.summary);
        if (!icon_path.empty()) {
          const RECT file_content_rect = MakeRect(0, 2, 100, kRowHeight - 2);
          const auto mode = candidate_state.candidate.summary.file_count <= 1
              ? file_content_preview::FileVisualMode::SingleFileIcon
              : file_content_preview::FileVisualMode::IconOnly;
          const auto layout = file_content_preview::ComputeLayout(
              file_content_rect,
              0,
              0,
              candidate_state.candidate.summary.file_count,
              mode);
          const int icon_size = std::min(
              layout.front_card_rect.right - layout.front_card_rect.left,
              layout.front_card_rect.bottom - layout.front_card_rect.top);
          const auto file_req = visual_assets::BuildFileIconRequest(
              icon_path,
              icon_size,
              d2d_shell_.dpi(),
              false);
          visual_assets::ShellIconPixels shell_icon;
          if (visual_assets::LoadShellIconPixels(file_req, &shell_icon) && !shell_icon.pixels.empty()) {
            ID2D1Bitmap* bitmap = nullptr;
            if (d2d_shell_.CreateBitmapFromBgra(shell_icon.width, shell_icon.height, shell_icon.pixels, &bitmap).ok()) {
              candidate_state.bitmap.Attach(bitmap);
            }
          }
        }
      }
    }

    // 2. 如果存在源应用图标，同步加载源进程图标为 ID2D1Bitmap 存入 candidate_state.source_icon
    if (!candidate_state.candidate.source_icon_path.empty()) {
      if (candidate_state.source_icon == nullptr) {
        const auto source_req = visual_assets::BuildSourceAppIconRequest(
            candidate_state.candidate.source_icon_path,
            kSourceIconSize,
            d2d_shell_.dpi());
        visual_assets::ShellIconPixels shell_icon;
        if (visual_assets::LoadShellIconPixels(source_req, &shell_icon) && !shell_icon.pixels.empty()) {
          ID2D1Bitmap* bitmap = nullptr;
          if (d2d_shell_.CreateBitmapFromBgra(shell_icon.width, shell_icon.height, shell_icon.pixels, &bitmap).ok()) {
            candidate_state.source_icon.Attach(bitmap);
          }
        }
      }
    }
  }
}

void QuickPasteWindow::ReleaseBitmaps() {
  for (auto& candidate_state : candidates_) {
    candidate_state.bitmap.Reset();
    candidate_state.source_icon.Reset();
  }
  bitmap_generation_ = 0;
}

void QuickPasteWindow::SetSelectedIndex(int index) {
  if (candidates_.empty()) {
    selected_index_ = -1;
  } else {
    selected_index_ = std::clamp(index, 0, static_cast<int>(candidates_.size()) - 1);
  }
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void QuickPasteWindow::MoveSelection(int delta) {
  if (candidates_.empty()) {
    return;
  }
  if (shown_above_anchor_) {
    delta = -delta;
  }
  int next = selected_index_;
  if (next < 0) {
    next = 0;
  } else if (delta < 0) {
    next = next == 0 ? static_cast<int>(candidates_.size()) - 1 : next - 1;
  } else if (delta > 0) {
    next = (next + 1) % static_cast<int>(candidates_.size());
  }
  SetSelectedIndex(next);
}

void QuickPasteWindow::AcceptSelected(bool plain_text) {
  AcceptIndex(selected_index_, plain_text);
}

void QuickPasteWindow::AcceptIndex(int index, bool plain_text) {
  if (index < 0 || index >= static_cast<int>(candidates_.size())) {
    if (host_actions_.on_close) {
      host_actions_.on_close();
    }
    return;
  }
  if (host_actions_.on_accept) {
    host_actions_.on_accept(index, plain_text);
  }
}

int QuickPasteWindow::HitTestCandidate(POINT client_point) const {
  for (size_t index = 0; index < row_rects_.size(); ++index) {
    if (PtInRect(&row_rects_[index], client_point)) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

}  // namespace clipd2d::ui
