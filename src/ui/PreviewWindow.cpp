#include "ui/PreviewWindow.h"
#include "ui/PanelWindowChrome.h"

#include <Richedit.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wincodec.h>

#include <algorithm>
#include <filesystem>
#include <string>

#include "common/StringUtil.h"
#include "common/Utf8.h"

namespace clipd2d::ui {

namespace detail {

std::wstring ResolvePreviewImagePath(const core::store::ClipRecord& clip) {
  if (clip.summary.content_type != core::store::ClipContentType::Image) {
    return {};
  }
  if (!clip.preview_path.empty() && std::filesystem::exists(clip.preview_path)) {
    return clip.preview_path;
  }
  if (!clip.storage_path.empty() && std::filesystem::exists(clip.storage_path)) {
    return clip.storage_path;
  }
  return !clip.preview_path.empty() ? clip.preview_path : clip.storage_path;
}

std::wstring ResolveExternalImageOpenPath(const core::store::ClipRecord& clip) {
  if (clip.summary.content_type != core::store::ClipContentType::Image) {
    return {};
  }
  if (!clip.storage_path.empty() && std::filesystem::exists(clip.storage_path)) {
    return clip.storage_path;
  }
  if (!clip.preview_path.empty() && std::filesystem::exists(clip.preview_path)) {
    return clip.preview_path;
  }
  return {};
}

}  // namespace detail

namespace {

constexpr wchar_t kPreviewWindowClassName[] = L"Scraps.PreviewWindow";

std::wstring FormatHeader(const core::store::ClipRecord& clip) {
  std::wstring header = core::store::ToDisplayLabel(clip.summary.content_type);
  if (!clip.summary.source_app_name.empty()) {
    header += L" · ";
    header += clip.summary.source_app_name;
  }
  return header;
}

std::wstring FirstFilePath(const core::store::ClipRecord& clip) {
  const std::wstring wide = common::Utf8ToWide(clip.plain_text).wide;
  for (const auto& part : common::SplitWide(wide, L'\n')) {
    const std::wstring trimmed = common::TrimWide(part);
    if (!trimmed.empty()) {
      return trimmed;
    }
  }
  return {};
}

std::wstring FileTitle(const core::store::ClipRecord& clip) {
  const std::wstring path = FirstFilePath(clip);
  if (path.empty()) {
    return L"文件";
  }
  const std::filesystem::path fs_path(path);
  return fs_path.filename().empty() ? fs_path.wstring() : fs_path.filename().wstring();
}

std::wstring FileDetail(const core::store::ClipRecord& clip) {
  if (clip.summary.file_count > 1) {
    return L"共 " + std::to_wstring(clip.summary.file_count) + L" 项";
  }
  return FirstFilePath(clip);
}

common::Status LoadPreviewBitmap(
    ID2D1HwndRenderTarget* target,
    const std::wstring& path,
    ID2D1Bitmap** bitmap,
    UINT* width,
    UINT* height) {
  if (bitmap == nullptr || target == nullptr) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "Preview bitmap outputs are invalid.");
  }
  *bitmap = nullptr;
  if (width != nullptr) *width = 0;
  if (height != nullptr) *height = 0;

  IWICImagingFactory* factory = nullptr;
  HRESULT hr = CoCreateInstance(
      CLSID_WICImagingFactory,
      nullptr,
      CLSCTX_INPROC_SERVER,
      IID_PPV_ARGS(&factory));
  if (FAILED(hr)) {
    return common::Status::Error(common::StatusCode::PlatformError, "Create WIC imaging factory failed.");
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
    return common::Status::Error(common::StatusCode::NotFound, "Open preview image failed.");
  }

  IWICBitmapFrameDecode* frame = nullptr;
  hr = decoder->GetFrame(0, &frame);
  IWICFormatConverter* converter = nullptr;
  if (SUCCEEDED(hr)) {
    hr = factory->CreateFormatConverter(&converter);
  }
  if (SUCCEEDED(hr)) {
    hr = converter->Initialize(
        frame,
        GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0,
        WICBitmapPaletteTypeCustom);
  }
  if (FAILED(hr)) {
    if (converter != nullptr) converter->Release();
    if (frame != nullptr) frame->Release();
    decoder->Release();
    factory->Release();
    return common::Status::Error(common::StatusCode::PlatformError, "Decode preview image failed.");
  }

  UINT image_width = 0;
  UINT image_height = 0;
  hr = converter->GetSize(&image_width, &image_height);
  if (FAILED(hr)) {
    converter->Release();
    frame->Release();
    decoder->Release();
    factory->Release();
    return common::Status::Error(common::StatusCode::PlatformError, "Read preview image size failed.");
  }

  std::vector<std::uint8_t> pixels(static_cast<size_t>(image_width) * static_cast<size_t>(image_height) * 4u);
  hr = converter->CopyPixels(nullptr, image_width * 4, static_cast<UINT>(pixels.size()), pixels.data());
  converter->Release();
  frame->Release();
  decoder->Release();
  factory->Release();
  if (FAILED(hr)) {
    return common::Status::Error(common::StatusCode::PlatformError, "Copy preview image pixels failed.");
  }

  hr = target->CreateBitmap(
      D2D1::SizeU(image_width, image_height),
      pixels.data(),
      image_width * 4,
      D2D1::BitmapProperties(
          D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)),
      bitmap);
  if (FAILED(hr)) {
    return common::Status::Error(common::StatusCode::PlatformError, "Create preview bitmap failed.");
  }
  if (width != nullptr) *width = image_width;
  if (height != nullptr) *height = image_height;
  return common::Status::Ok();
}

DWORD CALLBACK RichEditStreamInCallback(DWORD_PTR cookie, LPBYTE buffer, LONG cb, LONG* transferred) {
  auto* state = reinterpret_cast<std::pair<const char*, size_t>*>(cookie);
  if (state == nullptr || transferred == nullptr) {
    return 1;
  }
  const size_t remaining = state->second;
  const size_t count = std::min(static_cast<size_t>(cb), remaining);
  if (count > 0) {
    memcpy(buffer, state->first, count);
    state->first += count;
    state->second -= count;
  }
  *transferred = static_cast<LONG>(count);
  return 0;
}

}  // namespace

PreviewWindow::~PreviewWindow() {
  Destroy();
}

common::Status PreviewWindow::Create(HINSTANCE instance, HWND owner) {
  instance_ = instance;
  owner_ = owner;
  rich_edit_module_ = LoadLibraryW(L"Msftedit.dll");

  WNDCLASSEXW window_class{};
  window_class.cbSize = sizeof(window_class);
  window_class.lpfnWndProc = &PreviewWindow::StaticWindowProc;
  window_class.hInstance = instance_;
  window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  window_class.hbrBackground = nullptr;
  window_class.lpszClassName = kPreviewWindowClassName;
  RegisterClassExW(&window_class);

  hwnd_ = CreateWindowExW(
      WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
      kPreviewWindowClassName,
      L"Scraps 预览",
      WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
      CW_USEDEFAULT,
      CW_USEDEFAULT,
      900,
      680,
      owner_,
      nullptr,
      instance_,
      this);
  if (hwnd_ == nullptr) {
    return common::Status::Error(common::StatusCode::PlatformError, "Create preview window failed.");
  }

  common::Status status = d2d_shell_.Initialize(hwnd_);
  if (!status.ok()) {
    return status;
  }
  status = d2d_shell_.EnsureSecondaryTextFormats();
  if (!status.ok()) {
    return status;
  }
  status = CreateControls();
  if (!status.ok()) {
    return status;
  }
  LayoutControls();
  ShowWindow(hwnd_, SW_HIDE);
  return common::Status::Ok();
}

void PreviewWindow::Destroy() {
  SetInteractionGuardActive(false);
  if (ui_font_ != nullptr) {
    DeleteObject(ui_font_);
    ui_font_ = nullptr;
  }
  for (HWND* control : {&rich_edit_, &html_plain_button_, &html_source_button_, &open_external_button_, &ocr_copy_button_, &ocr_paste_button_, &ocr_rerun_button_, &image_copy_button_}) {
    if (*control != nullptr) {
      DestroyWindow(*control);
      *control = nullptr;
    }
  }
  preview_bitmap_.Reset();
  d2d_shell_.Shutdown();
  if (hwnd_ != nullptr) {
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
  }
  if (rich_edit_module_ != nullptr) {
    FreeLibrary(rich_edit_module_);
    rich_edit_module_ = nullptr;
  }
}

void PreviewWindow::ShowClip(const core::store::ClipRecord& clip, HostActions actions) {
  host_actions_ = std::move(actions);
  clip_ = clip;
  show_html_source_ = false;
  RefreshOcrState();
  LoadPreviewImage();
  UpdateRichEditContent();
  UpdateVisibleControls();
  LayoutControls();
  SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE);
  ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
  SetInteractionGuardActive(true);
  InvalidateRect(hwnd_, nullptr, FALSE);
  TriggerAutoOcrIfNeeded();
}

bool PreviewWindow::ShowingClip(std::int64_t clip_id) const {
  return clip_.has_value() && clip_->summary.id == clip_id;
}

LRESULT CALLBACK PreviewWindow::StaticWindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
  PreviewWindow* self = nullptr;
  if (message == WM_NCCREATE) {
    auto* create_struct = reinterpret_cast<CREATESTRUCTW*>(l_param);
    self = static_cast<PreviewWindow*>(create_struct->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  } else {
    self = reinterpret_cast<PreviewWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  }
  if (self != nullptr) {
    self->hwnd_ = hwnd;
    return self->WindowProc(message, w_param, l_param);
  }
  return DefWindowProcW(hwnd, message, w_param, l_param);
}

common::Status PreviewWindow::CreateControls() {
  html_plain_button_ = CreateWindowW(L"BUTTON", L"纯文本", WS_CHILD | BS_PUSHBUTTON, 0, 0, 80, 28, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdHtmlPlain)), instance_, nullptr);
  html_source_button_ = CreateWindowW(L"BUTTON", L"HTML 源码", WS_CHILD | BS_PUSHBUTTON, 0, 0, 92, 28, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdHtmlSource)), instance_, nullptr);
  open_external_button_ = CreateWindowW(L"BUTTON", L"系统默认打开", WS_CHILD | BS_PUSHBUTTON, 0, 0, 180, 30, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdOpenExternal)), instance_, nullptr);
  image_copy_button_ = CreateWindowW(L"BUTTON", L"复制图片", WS_CHILD | BS_PUSHBUTTON, 0, 0, 96, 28, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdImageCopy)), instance_, nullptr);
  ocr_copy_button_ = CreateWindowW(L"BUTTON", L"复制文字", WS_CHILD | BS_PUSHBUTTON, 0, 0, 96, 28, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdOcrCopy)), instance_, nullptr);
  ocr_paste_button_ = CreateWindowW(L"BUTTON", L"粘贴文字", WS_CHILD | BS_PUSHBUTTON, 0, 0, 96, 28, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdOcrPaste)), instance_, nullptr);
  ocr_rerun_button_ = CreateWindowW(L"BUTTON", L"重新识别", WS_CHILD | BS_PUSHBUTTON, 0, 0, 96, 28, hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdOcrRerun)), instance_, nullptr);
  rich_edit_ = CreateWindowExW(
      WS_EX_CLIENTEDGE,
      MSFTEDIT_CLASS,
      L"",
      WS_CHILD | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL,
      0,
      0,
      100,
      100,
      hwnd_,
      reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdRichEdit)),
      instance_,
      nullptr);
  if (rich_edit_ == nullptr) {
    return common::Status::Error(common::StatusCode::PlatformError, "Create preview rich-edit control failed.");
  }

  NONCLIENTMETRICSW metrics{};
  metrics.cbSize = sizeof(metrics);
  SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0);
  ui_font_ = CreateFontIndirectW(&metrics.lfMessageFont);
  for (HWND control : {html_plain_button_, html_source_button_, open_external_button_, image_copy_button_, ocr_copy_button_, ocr_paste_button_, ocr_rerun_button_, rich_edit_}) {
    SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font_), TRUE);
  }
  ApplyPreviewRichEditTheme();
  SendMessageW(rich_edit_, EM_SETREADONLY, TRUE, 0);
  return common::Status::Ok();
}

void PreviewWindow::ApplyPreviewRichEditTheme() {
  // C1：rich-edit 是 GDI 控件，不随 D2dShell 刷子，需单独随主题上色。
  const bool dark = IsDarkThemeActive();
  SendMessageW(rich_edit_, EM_SETBKGNDCOLOR, 0, dark ? RGB(0x2C, 0x2C, 0x2E) : RGB(247, 249, 253));
  CHARFORMAT2W format{};
  format.cbSize = sizeof(format);
  format.dwMask = CFM_COLOR;
  format.crTextColor = dark ? RGB(0xE8, 0xE8, 0xEA) : RGB(0x23, 0x26, 0x2D);
  SendMessageW(rich_edit_, EM_SETCHARFORMAT, SCF_DEFAULT, reinterpret_cast<LPARAM>(&format));
}

void PreviewWindow::LayoutControls() {
  if (hwnd_ == nullptr) {
    return;
  }
  RECT client{};
  GetClientRect(hwnd_, &client);
  const bool is_image = clip_.has_value() && clip_->summary.content_type == core::store::ClipContentType::Image;
  if (is_image) {
    const int preview_bottom = std::max(304, static_cast<int>(client.bottom) - 240);
    MoveWindow(image_copy_button_, 24, 78, 96, 28, TRUE);
    MoveWindow(ocr_rerun_button_, 128, 78, 96, 28, TRUE);
    MoveWindow(ocr_copy_button_, 232, 78, 96, 28, TRUE);
    MoveWindow(ocr_paste_button_, 336, 78, 96, 28, TRUE);
    MoveWindow(open_external_button_, client.right - 220, 76, 190, 30, TRUE);
    MoveWindow(rich_edit_, 24, preview_bottom + 12, client.right - 48, client.bottom - preview_bottom - 36, TRUE);
  } else {
    const int body_top = 72;
    MoveWindow(html_plain_button_, 24, 20, 80, 28, TRUE);
    MoveWindow(html_source_button_, 112, 20, 92, 28, TRUE);
    MoveWindow(open_external_button_, client.right - 220, 18, 190, 30, TRUE);
    MoveWindow(rich_edit_, 24, body_top, client.right - 48, client.bottom - body_top - 24, TRUE);
  }
}

void PreviewWindow::UpdateVisibleControls() {
  const bool is_html = clip_.has_value() && clip_->summary.content_type == core::store::ClipContentType::Html;
  const bool is_rtf = clip_.has_value() && clip_->summary.content_type == core::store::ClipContentType::Rtf;
  const bool is_image = clip_.has_value() && clip_->summary.content_type == core::store::ClipContentType::Image;
  const bool enable_image_ocr = host_actions_.enable_image_ocr;
  const bool can_copy_image = is_image && static_cast<bool>(host_actions_.on_copy_image);
  const bool has_ocr_text = is_image && clip_.has_value() && !clip_->ocr_text.empty();
  const bool can_open_externally = is_image && clip_.has_value() && !detail::ResolveExternalImageOpenPath(*clip_).empty();
  ShowWindow(html_plain_button_, is_html ? SW_SHOW : SW_HIDE);
  ShowWindow(html_source_button_, is_html ? SW_SHOW : SW_HIDE);
  ShowWindow(open_external_button_, can_open_externally ? SW_SHOW : SW_HIDE);
  ShowWindow(image_copy_button_, can_copy_image ? SW_SHOW : SW_HIDE);
  ShowWindow(ocr_copy_button_, (is_image && enable_image_ocr) ? SW_SHOW : SW_HIDE);
  ShowWindow(ocr_paste_button_, (is_image && enable_image_ocr) ? SW_SHOW : SW_HIDE);
  ShowWindow(ocr_rerun_button_, (is_image && enable_image_ocr) ? SW_SHOW : SW_HIDE);
  ShowWindow(rich_edit_, (is_rtf || is_image) ? SW_SHOW : SW_HIDE);
  if (image_copy_button_ != nullptr && can_copy_image) {
    const std::wstring label = host_actions_.image_copy_label.empty() ? L"复制图片" : host_actions_.image_copy_label;
    SetWindowTextW(image_copy_button_, label.c_str());
    EnableWindow(image_copy_button_, TRUE);
  }
  if (ocr_copy_button_ != nullptr) {
    EnableWindow(ocr_copy_button_, has_ocr_text ? TRUE : FALSE);
  }
  if (ocr_paste_button_ != nullptr) {
    EnableWindow(ocr_paste_button_, has_ocr_text ? TRUE : FALSE);
  }
  if (ocr_rerun_button_ != nullptr) {
    EnableWindow(ocr_rerun_button_, (is_image && enable_image_ocr && ocr_provider_message_.empty()) ? TRUE : FALSE);
    if (is_image && enable_image_ocr) {
      const wchar_t* text = clip_->ocr_state == "pending" ? L"识别中..." : (clip_->ocr_state == "none" ? L"开始识别" : L"重新识别");
      SetWindowTextW(ocr_rerun_button_, text);
    }
  }
}

void PreviewWindow::LoadPreviewImage() {
  preview_bitmap_.Reset();
  preview_width_ = 0;
  preview_height_ = 0;
  if (!clip_.has_value() || clip_->summary.content_type != core::store::ClipContentType::Image) {
    return;
  }
  const std::wstring path = detail::ResolvePreviewImagePath(*clip_);
  if (path.empty()) {
    return;
  }
  LoadPreviewBitmap(
      d2d_shell_.target(),
      path,
      preview_bitmap_.ReleaseAndGetAddressOf(),
      &preview_width_,
      &preview_height_);
}

void PreviewWindow::UpdateRichEditContent() {
  if (rich_edit_ == nullptr || !clip_.has_value()) {
    return;
  }
  SetWindowTextW(rich_edit_, L"");
  if (clip_->summary.content_type == core::store::ClipContentType::Image) {
    std::wstring text;
    if (!host_actions_.enable_image_ocr) {
      text = common::Utf8ToWide(clip_->plain_text).wide;
    } else if (!ocr_provider_message_.empty()) {
      text = ocr_provider_message_;
    } else if (clip_->ocr_state == "pending") {
      text = L"正在识别图片中的文字...";
    } else if (clip_->ocr_state == "failed") {
      text = L"OCR 失败：\r\n" + common::Utf8ToWide(clip_->ocr_error).wide;
    } else if (!clip_->ocr_text.empty()) {
      text = common::Utf8ToWide(clip_->ocr_text).wide;
    } else {
      text = L"尚未识别到文字。";
    }
    SetWindowTextW(rich_edit_, text.c_str());
    return;
  }
  if (clip_->summary.content_type != core::store::ClipContentType::Rtf || clip_->rtf_text.empty()) {
    return;
  }
  std::pair<const char*, size_t> state{clip_->rtf_text.data(), clip_->rtf_text.size()};
  EDITSTREAM stream{};
  stream.dwCookie = reinterpret_cast<DWORD_PTR>(&state);
  stream.pfnCallback = &RichEditStreamInCallback;
  SendMessageW(rich_edit_, EM_STREAMIN, SF_RTF, reinterpret_cast<LPARAM>(&stream));
}

void PreviewWindow::RefreshOcrState() {
  ocr_provider_message_.clear();
  if (!clip_.has_value() ||
      clip_->summary.content_type != core::store::ClipContentType::Image ||
      !host_actions_.enable_image_ocr) {
    return;
  }
  if (!host_actions_.query_ocr_provider_status) {
    ocr_provider_message_ = L"OCR 引擎不可用。";
    return;
  }
  std::wstring message;
  const common::Status status = host_actions_.query_ocr_provider_status(&message);
  if (!status.ok()) {
    ocr_provider_message_ = message.empty() ? common::Utf8ToWide(status.message).wide : message;
  }
}

void PreviewWindow::TriggerAutoOcrIfNeeded() {
  if (!clip_.has_value() ||
      clip_->summary.content_type != core::store::ClipContentType::Image ||
      !host_actions_.enable_image_ocr ||
      clip_->ocr_state != "none" ||
      !ocr_provider_message_.empty() ||
      !host_actions_.on_request_ocr) {
    return;
  }
  const common::Status status = host_actions_.on_request_ocr(clip_->summary.id, false);
  if (status.ok()) {
    clip_->ocr_state = "pending";
    UpdateRichEditContent();
    UpdateVisibleControls();
    InvalidateRect(hwnd_, nullptr, FALSE);
  }
}

void PreviewWindow::SetInteractionGuardActive(bool active) {
  if (interaction_guard_active_ == active) {
    return;
  }
  interaction_guard_active_ = active;
  if (active) {
    if (host_actions_.on_show) {
      host_actions_.on_show();
    }
  } else if (host_actions_.on_hide) {
    host_actions_.on_hide();
  }
}

void PreviewWindow::OpenWithSystemDefaultApp() {
  if (!clip_.has_value()) {
    return;
  }
  const std::wstring path = detail::ResolveExternalImageOpenPath(*clip_);
  if (path.empty()) {
    return;
  }
  ShellExecuteW(hwnd_, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void PreviewWindow::RenderFrame() {
  // C1 暗色模式：主题纪元变化时重建刷子并刷新 DWM 暗色属性与 rich-edit 配色。
  if (d2d_shell_.SyncTheme()) {
    ApplyDarkModeAttribute(hwnd_, clipd2d::ui::IsDarkThemeActive());
    ApplyPreviewRichEditTheme();
  }
  ID2D1HwndRenderTarget* target = d2d_shell_.BeginFrame();
  if (target == nullptr) {
    return;
  }
  RECT client{};
  GetClientRect(hwnd_, &client);
  target->Clear(d2d_shell_.ColorBackground());

  D2D1_RECT_F card = D2D1::RectF(16.0f, 16.0f, static_cast<float>(client.right - 16), static_cast<float>(client.bottom - 16));
  target->FillRoundedRectangle(D2D1::RoundedRect(card, 18.0f, 18.0f), d2d_shell_.surface_brush());
  target->DrawRoundedRectangle(D2D1::RoundedRect(card, 18.0f, 18.0f), d2d_shell_.border_brush(), 1.0f);

  if (!clip_.has_value()) {
    d2d_shell_.EndFrame();
    return;
  }

  RECT title_rect{28, 20, client.right - 28, 48};
  RECT meta_rect{28, 48, client.right - 28, 68};
  const std::wstring title = common::Utf8ToWide(clip_->summary.title.empty() ? "Preview" : clip_->summary.title).wide;
  const std::wstring meta = FormatHeader(*clip_);
  target->DrawTextW(title.c_str(), static_cast<UINT32>(title.size()), d2d_shell_.section_format(), D2D1::RectF(static_cast<float>(title_rect.left), static_cast<float>(title_rect.top), static_cast<float>(title_rect.right), static_cast<float>(title_rect.bottom)), d2d_shell_.text_brush(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
  target->DrawTextW(meta.c_str(), static_cast<UINT32>(meta.size()), d2d_shell_.meta_format(), D2D1::RectF(static_cast<float>(meta_rect.left), static_cast<float>(meta_rect.top), static_cast<float>(meta_rect.right), static_cast<float>(meta_rect.bottom)), d2d_shell_.muted_text_brush(), D2D1_DRAW_TEXT_OPTIONS_CLIP);

  if (clip_->summary.content_type == core::store::ClipContentType::Rtf) {
    d2d_shell_.EndFrame();
    return;
  }

  RECT body_rect{28, 84, client.right - 28, client.bottom - 28};
  if (clip_->summary.content_type == core::store::ClipContentType::Image) {
    body_rect.top = 124;
    body_rect.bottom = std::max(body_rect.top + 180, client.bottom - 236);
    if (preview_bitmap_ == nullptr) {
      LoadPreviewImage();
    }
    if (preview_bitmap_ != nullptr && preview_width_ > 0 && preview_height_ > 0) {
      const float available_width = static_cast<float>(body_rect.right - body_rect.left);
      const float available_height = static_cast<float>(body_rect.bottom - body_rect.top);
      const float scale = std::min(
          1.0f,
          std::min(
              available_width / static_cast<float>(preview_width_),
              available_height / static_cast<float>(preview_height_)));
      const float draw_width = static_cast<float>(preview_width_) * scale;
      const float draw_height = static_cast<float>(preview_height_) * scale;
      const float left = static_cast<float>(body_rect.left) + (available_width - draw_width) * 0.5f;
      const float top = static_cast<float>(body_rect.top) + (available_height - draw_height) * 0.5f;
      target->DrawBitmap(
          preview_bitmap_.Get(),
          D2D1::RectF(left, top, left + draw_width, top + draw_height),
          1.0f,
          D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    }
  } else {
    std::wstring body;
    if (clip_->summary.content_type == core::store::ClipContentType::Html) {
      body = common::Utf8ToWide(show_html_source_ ? clip_->html_text : clip_->plain_text).wide;
    } else if (clip_->summary.content_type == core::store::ClipContentType::Files) {
      body = FileTitle(*clip_) + L"\n" + FileDetail(*clip_);
    } else {
      body = common::Utf8ToWide(clip_->plain_text).wide;
    }
    target->DrawTextW(
        body.c_str(),
        static_cast<UINT32>(body.size()),
        d2d_shell_.body_format(),
        D2D1::RectF(static_cast<float>(body_rect.left), static_cast<float>(body_rect.top), static_cast<float>(body_rect.right), static_cast<float>(body_rect.bottom)),
        d2d_shell_.text_brush(),
        D2D1_DRAW_TEXT_OPTIONS_CLIP);
  }
  d2d_shell_.EndFrame();
}

LRESULT PreviewWindow::WindowProc(UINT message, WPARAM w_param, LPARAM l_param) {
  switch (message) {
    case WM_MOUSEACTIVATE:
      return MA_NOACTIVATE;
    case WM_SIZE:
      if (d2d_shell_.hwnd() != nullptr) {
        d2d_shell_.Resize(LOWORD(l_param), HIWORD(l_param));
      }
      LayoutControls();
      InvalidateRect(hwnd_, nullptr, FALSE);
      return 0;
    case WM_PAINT: {
      PAINTSTRUCT ps{};
      BeginPaint(hwnd_, &ps);
      EndPaint(hwnd_, &ps);
      RenderFrame();
      return 0;
    }
    case WM_COMMAND:
      switch (LOWORD(w_param)) {
        case kIdHtmlPlain:
          show_html_source_ = false;
          InvalidateRect(hwnd_, nullptr, FALSE);
          return 0;
        case kIdHtmlSource:
          show_html_source_ = true;
          InvalidateRect(hwnd_, nullptr, FALSE);
          return 0;
        case kIdOpenExternal:
          OpenWithSystemDefaultApp();
          return 0;
        case kIdImageCopy:
          if (host_actions_.on_copy_image) {
            host_actions_.on_copy_image();
          }
          return 0;
        case kIdOcrCopy:
          if (clip_.has_value() && host_actions_.on_copy_ocr_text) {
            host_actions_.on_copy_ocr_text(clip_->summary.id);
          }
          return 0;
        case kIdOcrPaste:
          if (clip_.has_value() && host_actions_.on_paste_ocr_text) {
            host_actions_.on_paste_ocr_text(clip_->summary.id);
          }
          return 0;
        case kIdOcrRerun:
          if (clip_.has_value() && host_actions_.on_request_ocr) {
            const common::Status status = host_actions_.on_request_ocr(clip_->summary.id, true);
            if (status.ok()) {
              clip_->ocr_state = "pending";
              clip_->ocr_error.clear();
              ocr_provider_message_.clear();
              UpdateRichEditContent();
              UpdateVisibleControls();
              InvalidateRect(hwnd_, nullptr, FALSE);
            }
          }
          return 0;
      }
      break;
    case WM_CLOSE:
      ShowWindow(hwnd_, SW_HIDE);
      SetInteractionGuardActive(false);
      return 0;
    case WM_ERASEBKGND:
      return 1;
  }
  return DefWindowProcW(hwnd_, message, w_param, l_param);
}

}  // namespace clipd2d::ui
