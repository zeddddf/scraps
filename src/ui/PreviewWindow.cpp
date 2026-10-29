#include "ui/PreviewWindow.h"
#include "ui/PanelWindowChrome.h"

#include <Richedit.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wincodec.h>


#include <algorithm>
#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <cwctype>

#include "common/AsyncRunner.h"
#include "common/Log.h"
#include "common/Stopwatch.h"
#include "common/StringUtil.h"
#include "common/Utf8.h"
#include "ui/ClipVisualAssets.h"
#include "ui/OwnedMessage.h"

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
// 内容自适应尺寸的上下限（compact=悬停路径，宽松=选中路径）。
constexpr int kPreviewMinWidth = 340;
constexpr int kPreviewMinHeight = 180;
// 图片模式专用：贴图优先，最小宽度放宽（340 是文本可读下限，图片不需要），
// 左右内边距收紧到 8，窗口尺寸 = 图片尺寸×fit + 8×2，杜绝 contain 居中的假边框。
constexpr int kImageMinWidth = 200;
constexpr float kImageEdgePad = 8.0f;
constexpr int kHoverMaxWidth = 560;
constexpr int kHoverMaxHeight = 560;
constexpr int kSelectionMaxWidth = 680;
constexpr int kSelectionMaxHeight = 760;
// 排版网格（单层边框：整窗 surface，无内嵌卡片）。
constexpr float kPad = 16.0f;          // 左右/底边距
constexpr float kBodyTop = 80.0f;      // 正文起始（标题 14 + meta 42 + 分隔线 68 之下）
constexpr float kFileStatsHeight = 26.0f;
// Files 清单行高与滚轮步进（像素）。
constexpr int kFileRowHeight = 26;
constexpr int kFileWheelStep = 40;
// 图片缩放每格 10%，范围 [1,5]（与竞品一致），由 ClampPreviewZoom 钳制。
constexpr float kZoomStep = 0.1f;

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

// Files 条目指向单个图片文件时可直接出图预览。
bool IsImageFilePath(const std::wstring& path) {
  std::wstring extension = std::filesystem::path(path).extension().wstring();
  for (wchar_t& c : extension) {
    c = static_cast<wchar_t>(std::towlower(c));
  }
  return extension == L".png" || extension == L".jpg" || extension == L".jpeg" ||
      extension == L".gif" || extension == L".webp" || extension == L".bmp";
}

std::wstring ResolveWebViewUserDataFolder() {
  wchar_t path[MAX_PATH]{};
  if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, path))) {
    return {};
  }
  std::wstring folder(path);
  if (!folder.empty() && folder.back() != L'\\') {
    folder += L'\\';
  }
  folder += L"Scraps\\WebView2";
  return folder;
}

// 图片后台解码结果载荷（UI 线程仅做 CreateBitmapFromBgra，微秒级）。
struct PreviewImageReadyPayload {
  std::uint64_t generation = 0;
  std::wstring path;
  UINT width = 0;
  UINT height = 0;
  std::vector<std::uint8_t> pixels;
};

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

// pill 按钮文字在矩形内水平+垂直居中：D2D DrawTextW 默认左上对齐，直接填
// rect 会导致文字偏上偏左（曾表现为"按钮文字不居中"）。先测文本尺寸再定位。
void DrawTextCenteredInRect(
    ID2D1HwndRenderTarget* target,
    IDWriteFactory* factory,
    const std::wstring& text,
    IDWriteTextFormat* format,
    const D2D1_RECT_F& rect,
    ID2D1SolidColorBrush* brush) {
  const float box_w = rect.right - rect.left;
  const float box_h = rect.bottom - rect.top;
  Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
  if (factory != nullptr && !text.empty() &&
      SUCCEEDED(factory->CreateTextLayout(
          text.c_str(), static_cast<UINT32>(text.size()), format, box_w, box_h,
          layout.GetAddressOf())) &&
      layout != nullptr) {
    DWRITE_TEXT_METRICS metrics{};
    if (SUCCEEDED(layout->GetMetrics(&metrics))) {
      const float left = rect.left + std::max(0.0f, (box_w - metrics.width) * 0.5f);
      const float top = rect.top + std::max(0.0f, (box_h - metrics.height) * 0.5f);
      target->DrawTextW(
          text.c_str(),
          static_cast<UINT32>(text.size()),
          format,
          D2D1::RectF(left, top, left + metrics.width + 2.0f, top + metrics.height + 2.0f),
          brush,
          D2D1_DRAW_TEXT_OPTIONS_NONE);
      return;
    }
  }
  target->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), format, rect, brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
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

  // 极简无边框：WS_POPUP 无系统标题栏，圆角交给 DWM（Win11 生效，Win10 静默忽略），
  // 阴影由 DWMWCP_ROUND 自带；边框感由 D2D 卡片自绘。
  hwnd_ = CreateWindowExW(
      WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
      kPreviewWindowClassName,
      L"Scraps 预览",
      WS_POPUP | WS_CLIPCHILDREN,
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
  constexpr DWORD kDwmwaCornerPreference = 33;
  constexpr int kDwmCornerRound = 2;  // DWMWCP_ROUND
  DwmSetWindowAttribute(hwnd_, kDwmwaCornerPreference, &kDwmCornerRound, sizeof(kDwmCornerRound));

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
  // 空闲预热 WebView2（2.5s 后创建环境/controller）：首次 HTML 预览只剩
  // 一次导航，不再摊上 ~300ms 创建成本。失败静默（预览回退纯文本）。
  SetTimer(hwnd_, kPrewarmTimerId, 2500, nullptr);
  ShowWindow(hwnd_, SW_HIDE);
  return common::Status::Ok();
}

void PreviewWindow::Destroy() {
  SetInteractionGuardActive(false);
  TeardownWebView();
  if (ui_font_ != nullptr) {
    DeleteObject(ui_font_);
    ui_font_ = nullptr;
  }
  if (rich_edit_ != nullptr) {
    DestroyWindow(rich_edit_);
    rich_edit_ = nullptr;
  }
  pill_buttons_.clear();
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
  InitForClip(clip);
  if (hwnd_ != nullptr) {
    const SIZE size = ComputePreferredSize(false);
    // 选中路径：旧位置可见且与主面板无交集 → 保留（连续选行不跳位）；
    // 否则（首次显示/旧位置压住主面板/主面板被拖动）重新定位到面板旁。
    bool keep_position = false;
    RECT current{};
    if (IsWindowVisible(hwnd_) && GetWindowRect(hwnd_, &current)) {
      RECT owner_rect{};
      if (owner_ != nullptr && GetWindowRect(owner_, &owner_rect)) {
        const bool overlap =
            current.left < owner_rect.right && owner_rect.left < current.right &&
            current.top < owner_rect.bottom && owner_rect.top < current.bottom;
        keep_position = !overlap;
      } else {
        keep_position = true;
      }
    }
    if (keep_position) {
      placement_anchor_ = current;
      SetWindowPos(hwnd_, HWND_TOP, 0, 0, size.cx, size.cy, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    } else {
      RECT anchor = current;
      if (owner_ != nullptr) {
        GetWindowRect(owner_, &anchor);
      }
      placement_anchor_ = anchor;
      const POINT pos = ComputePlacement(anchor, size.cx, size.cy);
      SetWindowPos(hwnd_, HWND_TOP, pos.x, pos.y, size.cx, size.cy, SWP_NOACTIVATE);
    }
    RenderFrame();  // 先绘后显：窗口出现的首帧即最终内容，无骨架闪帧
  }
  SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE);
  ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
}

// 统一防重叠定位：右侧 → 左侧 → 面板下方 → 贴右缘兜底。
// 基准全部叠加 owner（主面板）矩形，任何分支都不把预览窗放进面板范围内。
POINT PreviewWindow::ComputePlacement(const RECT& anchor_screen, int width, int height) const {
  RECT work{anchor_screen};
  const HMONITOR monitor = MonitorFromRect(&anchor_screen, MONITOR_DEFAULTTONEAREST);
  MONITORINFO monitor_info{};
  monitor_info.cbSize = sizeof(monitor_info);
  if (GetMonitorInfoW(monitor, &monitor_info)) {
    work = monitor_info.rcWork;
  }
  const int work_left = static_cast<int>(work.left);
  const int work_right = static_cast<int>(work.right);
  const int work_top = static_cast<int>(work.top);
  const int work_bottom = static_cast<int>(work.bottom);
  RECT owner_rect{};
  const bool have_owner = owner_ != nullptr && GetWindowRect(owner_, &owner_rect);
  const int owner_left = have_owner ? static_cast<int>(owner_rect.left) : static_cast<int>(anchor_screen.left);
  const int owner_right = have_owner ? static_cast<int>(owner_rect.right) : static_cast<int>(anchor_screen.right);
  const int owner_bottom = have_owner ? static_cast<int>(owner_rect.bottom) : static_cast<int>(anchor_screen.bottom);
  const int anchor_left = static_cast<int>(anchor_screen.left);
  const int anchor_right = static_cast<int>(anchor_screen.right);
  const int anchor_top = static_cast<int>(anchor_screen.top);
  const int anchor_bottom = static_cast<int>(anchor_screen.bottom);
  const auto clamp_y = [&](int x) -> POINT {
    const int y = std::max(work_top, std::min(anchor_top - 8, work_bottom - height));
    return {std::clamp(x, work_left, std::max(work_left, work_right - width)), y};
  };
  const auto log_result = [&](const char* branch, POINT pos) {
    char message[256]{};
    snprintf(
        message,
        sizeof(message),
        "branch=%s anchor=(%d,%d,%d,%d) owner=(%d,%d,%d,%d) work=(%d,%d,%d,%d) size=%dx%d -> (%d,%d)",
        branch,
        anchor_left, anchor_top, anchor_right, anchor_bottom,
        owner_left, static_cast<int>(owner_rect.top), owner_right, owner_bottom,
        work_left, work_top, work_right, work_bottom,
        width, height, pos.x, pos.y);
    common::WriteLog(common::LogLevel::Info, "preview.placement", message);
    return pos;
  };
  // 1) 右侧：面板右缘/行右缘之外。
  if (const int x = std::max(anchor_right, owner_right) + 8; x + width <= work_right) {
    return log_result("right", clamp_y(x));
  }
  // 2) 左侧：紧贴主面板左缘向左排（保持 8px 间隙、离锚点行最近），
  //    屏幕左缘放不下才落到下方分支。
  if (const int x = owner_left - width - 8; x >= work_left) {
    return log_result("left", clamp_y(x));
  }
  // 3) 下方：面板底缘之下，水平贴 anchor 左对齐。
  if (const int y = std::max(anchor_bottom, owner_bottom) + 8; y + height <= work_bottom) {
    const int x = std::clamp(anchor_left, work_left, std::max(work_left, work_right - width));
    return log_result("below", POINT{x, y});
  }
  // 4) 兜底：屏幕放不下时贴右缘 + 垂直钳工作区（遮挡不可避免，取最少干扰位）。
  return log_result("fallback", clamp_y(work_right - width));
}

void PreviewWindow::ShowClipAt(
    const core::store::ClipRecord& clip,
    HostActions actions,
    const RECT& anchor_screen,
    bool compact) {
  host_actions_ = std::move(actions);
  InitForClip(clip);
  if (hwnd_ == nullptr) {
    return;
  }
  const SIZE preferred = ComputePreferredSize(compact);
  placement_anchor_ = anchor_screen;
  const POINT pos = ComputePlacement(anchor_screen, preferred.cx, preferred.cy);
  SetWindowPos(hwnd_, HWND_TOP, pos.x, pos.y, preferred.cx, preferred.cy, SWP_NOACTIVATE);  // 先落位
  RenderFrame();  // 先绘后显：悬停弹出零骨架
  SetWindowPos(hwnd_, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void PreviewWindow::Hide() {
  if (hwnd_ == nullptr || !IsWindowVisible(hwnd_)) {
    return;
  }
  // 隐藏前提交未保存的编辑（自动保存语义：内容已在屏幕上消失前写回剪贴板）。
  ExitEditMode(true);
  ShowWindow(hwnd_, SW_HIDE);
  SetWebViewVisible(false);
  // 挂起延迟 5s：TrySuspend 是异步的，若挂起流程未完成时下一次预览的
  // 导航已发出，get_IsSuspended 会误报 false 漏调 Resume——WebView 卡在
  // 挂起态、导航永不完成、富文本内容永不显示（"有些条目不显示"的元凶）。
  // 延迟到确认不会马上重现时才挂起。
  KillTimer(hwnd_, kSuspendTimerId);
  SetTimer(hwnd_, kSuspendTimerId, 5000, nullptr);
  SetInteractionGuardActive(false);
}

bool PreviewWindow::ShowingClip(std::int64_t clip_id) const {
  return clip_.has_value() && clip_->summary.id == clip_id;
}

bool PreviewWindow::ContainsCursor() const {
  if (hwnd_ == nullptr || !IsWindowVisible(hwnd_)) {
    return false;
  }
  POINT cursor{};
  if (!GetCursorPos(&cursor)) {
    return false;
  }
  RECT bounds{};
  if (!GetWindowRect(hwnd_, &bounds)) {
    return false;
  }
  return cursor.x >= bounds.left && cursor.x < bounds.right &&
      cursor.y >= bounds.top && cursor.y < bounds.bottom;
}

void PreviewWindow::EnsureNotCoveringOwner() {
  if (hwnd_ == nullptr || !IsWindowVisible(hwnd_) || owner_ == nullptr) {
    return;
  }
  RECT self{};
  RECT owner_rect{};
  if (!GetWindowRect(hwnd_, &self) || !GetWindowRect(owner_, &owner_rect)) {
    return;
  }
  const bool overlap =
      self.left < owner_rect.right && owner_rect.left < self.right &&
      self.top < owner_rect.bottom && owner_rect.top < self.bottom;
  if (!overlap) {
    return;
  }
  // 压住了面板（面板被拖动/重新弹出，预览窗还停在旧位置）：以面板为锚重定位；
  // 重定位后仍与面板相交（屏幕太挤无合法位置）则直接隐藏——覆盖比消失更糟。
  const SIZE size = ComputePreferredSize(false);
  const POINT pos = ComputePlacement(owner_rect, size.cx, size.cy);
  const RECT probe{pos.x, pos.y, pos.x + size.cx, pos.y + size.cy};
  const bool still_overlap =
      probe.left < owner_rect.right && owner_rect.left < probe.right &&
      probe.top < owner_rect.bottom && owner_rect.top < probe.bottom;
  if (still_overlap) {
    Hide();
    return;
  }
  SetWindowPos(hwnd_, HWND_TOP, pos.x, pos.y, size.cx, size.cy, SWP_NOACTIVATE);
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
  // 全部按钮为 D2D 自绘 pill（RenderFrame 绘制 + WM_LBUTTONDOWN 命中），
  // 不再创建任何 Win32 BUTTON 子控件；RichEdit 仅保留 RTF 渲染职责。
  pill_buttons_ = {
      {kIdHtmlRich, L"富文本"},
      {kIdHtmlPlain, L"纯文本"},
      {kIdHtmlSource, L"HTML 源码"},
      {kIdEditToggle, L"编辑"},
      {kIdImageCopy, L"复制图片"},
      {kIdOcrRerun, L"重新识别"},
      {kIdOcrCopy, L"复制文字"},
      {kIdOcrPaste, L"粘贴文字"},
      {kIdOpenExternal, L"系统默认打开"},
  };
  // RichEdit 仅保留 RTF 渲染职责：无 CLIENTEDGE 立体边框（GDI 浮雕与 D2D
  // 界面割裂），背景/文字色随主题（ApplyPreviewRichEditTheme）。
  rich_edit_ = CreateWindowExW(
      0,
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
  SendMessageW(rich_edit_, WM_SETFONT, reinterpret_cast<WPARAM>(ui_font_), FALSE);
  // ENM_CHANGE：编辑防抖；ENM_KEYEVENTS：Esc 退出编辑态。
  SendMessageW(rich_edit_, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_KEYEVENTS);
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
  const float right = static_cast<float>(client.right);
  // HTML 三档 tab：meta 行右端（y 36..60），右对齐排布。
  if (PillButton* rich = FindButton(kIdHtmlRich)) {
    PillButton* plain = FindButton(kIdHtmlPlain);
    PillButton* source = FindButton(kIdHtmlSource);
    if (plain != nullptr && source != nullptr) {
      constexpr float kTabTop = 36.0f;
      constexpr float kTabHeight = 24.0f;
      constexpr float kGap = 6.0f;
      source->rect = D2D1::RectF(right - kPad - 104.0f, kTabTop, right - kPad, kTabTop + kTabHeight);
      plain->rect = D2D1::RectF(source->rect.left - kGap - 68.0f, kTabTop, source->rect.left - kGap, kTabTop + kTabHeight);
      rich->rect = D2D1::RectF(plain->rect.left - kGap - 68.0f, kTabTop, plain->rect.left - kGap, kTabTop + kTabHeight);
    }
  }
  // 编辑开关位置：纯文本条目（无 tab 占位）放 meta 行右端；HTML 条目 tab
  // 占了右端，编辑 pill 排到富文本 tab 左侧。
  if (PillButton* edit = FindButton(kIdEditToggle)) {
    if (PillButton* rich = FindButton(kIdHtmlRich);
        rich != nullptr && clip_.has_value() &&
        clip_->summary.content_type == core::store::ClipContentType::Html) {
      edit->rect = D2D1::RectF(rich->rect.left - 6.0f - 68.0f, 36.0f, rich->rect.left - 6.0f, 60.0f);
    } else {
      edit->rect = D2D1::RectF(right - kPad - 68.0f, 36.0f, right - kPad, 60.0f);
    }
  }
  // 图片操作行：分隔线之下 y 76..104；左起主操作链，右端"系统默认打开"。
  if (PillButton* image_copy = FindButton(kIdImageCopy)) {
    PillButton* rerun = FindButton(kIdOcrRerun);
    PillButton* ocr_copy = FindButton(kIdOcrCopy);
    PillButton* ocr_paste = FindButton(kIdOcrPaste);
    PillButton* open_external = FindButton(kIdOpenExternal);
    if (rerun != nullptr && ocr_copy != nullptr && ocr_paste != nullptr && open_external != nullptr) {
      constexpr float kRowTop = 76.0f;
      constexpr float kRowHeight = 28.0f;
      constexpr float kGap = 8.0f;
      float x = kPad;
      image_copy->rect = D2D1::RectF(x, kRowTop, x + 96.0f, kRowTop + kRowHeight);
      x = image_copy->rect.right + kGap;
      rerun->rect = D2D1::RectF(x, kRowTop, x + 96.0f, kRowTop + kRowHeight);
      x = rerun->rect.right + kGap;
      ocr_copy->rect = D2D1::RectF(x, kRowTop, x + 92.0f, kRowTop + kRowHeight);
      x = ocr_copy->rect.right + kGap;
      ocr_paste->rect = D2D1::RectF(x, kRowTop, x + 92.0f, kRowTop + kRowHeight);
      open_external->rect = D2D1::RectF(right - kPad - 128.0f, kRowTop, right - kPad, kRowTop + kRowHeight);
    }
  }
  // RichEdit 仅 RTF 渲染用，占满正文区。
  if (rich_edit_ != nullptr) {
    const int body_top = 80;
    MoveWindow(rich_edit_, 24, body_top, client.right - 48, client.bottom - body_top - 24, FALSE);
  }
  ApplyWebViewBounds();
}

void PreviewWindow::UpdateVisibleControls() {
  const bool is_html = clip_.has_value() && clip_->summary.content_type == core::store::ClipContentType::Html;
  const bool is_rtf = clip_.has_value() && clip_->summary.content_type == core::store::ClipContentType::Rtf;
  const bool is_image = clip_.has_value() && clip_->summary.content_type == core::store::ClipContentType::Image;
  const bool is_text = clip_.has_value() && clip_->summary.content_type == core::store::ClipContentType::Text;
  const bool enable_image_ocr = host_actions_.enable_image_ocr;
  const bool can_copy_image = is_image && static_cast<bool>(host_actions_.on_copy_image);
  const bool has_ocr_text = is_image && clip_.has_value() && !clip_->ocr_text.empty();
  const bool can_open_externally = is_image && clip_.has_value() && !detail::ResolveExternalImageOpenPath(*clip_).empty();
  // 文本类视图（RTF/HTML 纯文本/HTML 源码/纯文本条目/编辑中）由 RichEdit 呈现。
  const bool text_view = is_rtf || is_text || editing_ ||
      (is_html && (html_mode_ == PreviewMode::kPlainText || html_mode_ == PreviewMode::kSource));
  const auto set_button = [this](int id, bool visible, bool enabled = true) {
    if (PillButton* button = FindButton(id)) {
      button->visible = visible;
      button->enabled = enabled;
      if (!visible || !enabled) {
        button->hovered = false;
      }
    }
  };
  set_button(kIdHtmlRich, is_html);
  set_button(kIdHtmlPlain, is_html);
  set_button(kIdHtmlSource, is_html);
  set_button(kIdOpenExternal, can_open_externally);
  set_button(kIdImageCopy, can_copy_image);
  set_button(kIdOcrCopy, is_image && enable_image_ocr, has_ocr_text);
  set_button(kIdOcrPaste, is_image && enable_image_ocr, has_ocr_text);
  set_button(kIdOcrRerun, is_image && enable_image_ocr, ocr_provider_message_.empty());
  // 编辑开关：纯文本条目 + HTML 纯文本视图（编辑提交=把改后的纯文本写回
  // 剪贴板生成新 Text 条目，历史原样保留）。富文本/源码视图不可编辑。
  set_button(kIdEditToggle,
             (is_text || (is_html && html_mode_ == PreviewMode::kPlainText)) &&
                 static_cast<bool>(host_actions_.on_commit_text));
  if (PillButton* edit = FindButton(kIdEditToggle)) {
    edit->label = editing_ ? L"完成" : L"编辑";
  }
  if (PillButton* rerun = FindButton(kIdOcrRerun)) {
    if (is_image && enable_image_ocr) {
      rerun->label = clip_->ocr_state == "pending" ? L"识别中..."
          : (clip_->ocr_state == "none" ? L"开始识别" : L"重新识别");
    }
  }
  if (PillButton* image_copy = FindButton(kIdImageCopy)) {
    if (can_copy_image && !host_actions_.image_copy_label.empty()) {
      image_copy->label = host_actions_.image_copy_label;
    }
  }
  ShowWindow(rich_edit_, text_view ? SW_SHOW : SW_HIDE);
}

void PreviewWindow::LoadPreviewImage() {
  preview_bitmap_.Reset();
  preview_width_ = 0;
  preview_height_ = 0;
  image_zoom_ = 1.0f;
  image_load_pending_ = false;
  if (!clip_.has_value()) {
    return;
  }
  std::wstring path;
  if (clip_->summary.content_type == core::store::ClipContentType::Image) {
    path = detail::ResolvePreviewImagePath(*clip_);
  } else if (
      clip_->summary.content_type == core::store::ClipContentType::Files &&
      clip_->summary.file_count == 1) {
    // Files 指向单个图片文件：复用同一条后台解码管线直接出图。
    const std::wstring first = FirstFilePath(*clip_);
    if (!first.empty() && IsImageFilePath(first) && std::filesystem::exists(first)) {
      path = first;
    }
  }
  if (path.empty()) {
    return;
  }
  RequestPreviewImageAsync(path);
}

void PreviewWindow::RequestPreviewImageAsync(const std::wstring& path) {
  ++image_generation_;
  const std::uint64_t generation = image_generation_;
  image_decode_path_ = path;
  if (host_actions_.work_pool == nullptr) {
    // 无线程池兜底：同步小图解码（封顶 2048，仍远快于旧全尺寸路径）。
    std::vector<std::uint8_t> pixels;
    UINT width = 0;
    UINT height = 0;
    if (visual_assets::LoadScaledBitmapPixels(path, 2048, 2048, &width, &height, &pixels).ok() && !pixels.empty()) {
      d2d_shell_.CreateBitmapFromBgra(width, height, pixels, preview_bitmap_.ReleaseAndGetAddressOf());
      preview_width_ = width;
      preview_height_ = height;
    }
    return;
  }
  image_load_pending_ = true;
  const HWND hwnd = hwnd_;
  host_actions_.work_pool->Post([hwnd, generation, path]() {
    auto payload = std::make_unique<PreviewImageReadyPayload>();
    payload->generation = generation;
    payload->path = path;
    (void)visual_assets::LoadScaledBitmapPixels(path, 2048, 2048, &payload->width, &payload->height, &payload->pixels);
    (void)PostOwnedMessage(hwnd, PreviewWindow::kImageReadyMessage, 0, std::move(payload));
  });
}

void PreviewWindow::HandleImageReady(LPARAM l_param) {
  auto payload = TakeOwnedMessagePayload<PreviewImageReadyPayload>(l_param);
  if (payload == nullptr || !clip_.has_value()) {
    return;
  }
  if (payload->generation != image_generation_ || payload->path != image_decode_path_) {
    return;  // hover 已换行：旧解码丢弃
  }
  image_load_pending_ = false;
  preview_bitmap_.Reset();
  if (!payload->pixels.empty()) {
    d2d_shell_.CreateBitmapFromBgra(payload->width, payload->height, payload->pixels, preview_bitmap_.ReleaseAndGetAddressOf());
    preview_width_ = payload->width;
    preview_height_ = payload->height;
  }
  // 自适应：按解码出的实际尺寸重算窗口大小。宽度变化时右缘会平移——
  // 若扩宽后压住面板或越出屏幕，按锚重定位（此前只 SWP_NOMOVE 改尺寸，
  // 图片条目解码完成后右缘向右扩展 110px 插进面板，正是"压面板"的元凶）。
  if (hwnd_ != nullptr && IsWindowVisible(hwnd_)) {
    const SIZE size = ComputePreferredSize(false);
    RECT self{};
    if (GetWindowRect(hwnd_, &self)) {
      const int new_right = static_cast<int>(self.left) + size.cx;
      const int new_bottom = static_cast<int>(self.top) + size.cy;
      MONITORINFO monitor_info{};
      monitor_info.cbSize = sizeof(monitor_info);
      RECT work{self};
      if (GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &monitor_info)) {
        work = monitor_info.rcWork;
      }
      RECT owner_rect{};
      const bool overlap = owner_ != nullptr && GetWindowRect(owner_, &owner_rect) &&
          static_cast<int>(self.left) < owner_rect.right && owner_rect.left < new_right &&
          static_cast<int>(self.top) < owner_rect.bottom && owner_rect.top < new_bottom;
      const bool offscreen = new_right > static_cast<int>(work.right) ||
          new_bottom > static_cast<int>(work.bottom);
      if (overlap || offscreen) {
        const RECT anchor = placement_anchor_.right > placement_anchor_.left
            ? placement_anchor_
            : owner_rect;
        const POINT pos = ComputePlacement(anchor, size.cx, size.cy);
        SetWindowPos(hwnd_, nullptr, pos.x, pos.y, size.cx, size.cy, SWP_NOZORDER | SWP_NOACTIVATE);
      } else {
        SetWindowPos(hwnd_, nullptr, 0, 0, size.cx, size.cy, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
      }
      LayoutControls();
    }
  }
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void PreviewWindow::ReloadFilesModel() {
  files_model_ = {};
  files_scroll_offset_ = 0;
  if (!clip_.has_value() || clip_->summary.content_type != core::store::ClipContentType::Files) {
    return;
  }
  files_model_ = BuildPreviewFileList(clip_->plain_text);
}

bool PreviewWindow::OnMouseWheel(WPARAM w_param) {
  if (!clip_.has_value()) {
    return false;
  }
  const short delta = GET_WHEEL_DELTA_WPARAM(w_param);
  const bool bitmap_ready = preview_bitmap_ != nullptr && preview_width_ > 0 && preview_height_ > 0;
  // 图片条目 / Files 单图片：Ctrl+滚轮缩放（与竞品一致 1~5 倍 0.1 步进）。
  if (bitmap_ready &&
      (clip_->summary.content_type == core::store::ClipContentType::Image ||
       clip_->summary.content_type == core::store::ClipContentType::Files) &&
      (GET_KEYSTATE_WPARAM(w_param) & MK_CONTROL) != 0) {
    const float next = ClampPreviewZoom(image_zoom_ + (delta > 0 ? kZoomStep : -kZoomStep));
    if (next != image_zoom_) {
      image_zoom_ = next;
      InvalidateRect(hwnd_, nullptr, FALSE);
    }
    return true;
  }
  if (clip_->summary.content_type == core::store::ClipContentType::Files) {
    const int next = files_scroll_offset_ - (delta > 0 ? kFileWheelStep : -kFileWheelStep);
    files_scroll_offset_ = std::max(0, next);
    InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
  }
  return false;
}

std::wstring PreviewWindow::OcrDisplayText() const {
  if (!clip_.has_value()) {
    return {};
  }
  if (!host_actions_.enable_image_ocr) {
    return common::Utf8ToWide(clip_->plain_text).wide;
  }
  if (!ocr_provider_message_.empty()) {
    return ocr_provider_message_;
  }
  if (clip_->ocr_state == "pending") {
    return L"正在识别图片中的文字...";
  }
  if (clip_->ocr_state == "failed") {
    return L"OCR 失败：\r\n" + common::Utf8ToWide(clip_->ocr_error).wide;
  }
  if (!clip_->ocr_text.empty()) {
    return common::Utf8ToWide(clip_->ocr_text).wide;
  }
  return L"尚未识别到文字。";
}

void PreviewWindow::UpdateRichEditContent() {
  if (rich_edit_ == nullptr || !clip_.has_value()) {
    return;
  }
  SetWindowTextW(rich_edit_, L"");
  const auto type = clip_->summary.content_type;
  if (type == core::store::ClipContentType::Rtf && !clip_->rtf_text.empty()) {
    // RichEdit 仅承担 RTF 富文本渲染（D2D 无法排版 RTF）。
    std::pair<const char*, size_t> state{clip_->rtf_text.data(), clip_->rtf_text.size()};
    EDITSTREAM stream{};
    stream.dwCookie = reinterpret_cast<DWORD_PTR>(&state);
    stream.pfnCallback = &RichEditStreamInCallback;
    SendMessageW(rich_edit_, EM_STREAMIN, SF_RTF, reinterpret_cast<LPARAM>(&stream));
    return;
  }
  // 文本类视图（纯文本条目 / HTML 纯文本 / HTML 源码）也走 RichEdit：
  // 只读态自带选择/复制/全选，D2D DrawTextW 直绘没有文本交互能力。
  if (type == core::store::ClipContentType::Text) {
    SetWindowTextW(rich_edit_, common::Utf8ToWide(clip_->plain_text).wide.c_str());
    return;
  }
  if (type == core::store::ClipContentType::Html) {
    if (html_mode_ == PreviewMode::kPlainText) {
      SetWindowTextW(rich_edit_, common::Utf8ToWide(clip_->plain_text).wide.c_str());
    } else if (html_mode_ == PreviewMode::kSource) {
      SetWindowTextW(rich_edit_, common::Utf8ToWide(html_fragment_).wide.c_str());
    }
  }
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

// ---- D2D 自绘 pill 按钮 ----

PreviewWindow::PillButton* PreviewWindow::FindButton(int id) {
  for (PillButton& button : pill_buttons_) {
    if (button.id == id) {
      return &button;
    }
  }
  return nullptr;
}

void PreviewWindow::UpdateHoverFromPoint(const POINT& pt) {
  bool changed = false;
  for (PillButton& button : pill_buttons_) {
    const bool inside = button.visible && button.enabled &&
        pt.x >= button.rect.left && pt.x < button.rect.right &&
        pt.y >= button.rect.top && pt.y < button.rect.bottom;
    if (button.hovered != inside) {
      button.hovered = inside;
      changed = true;
    }
  }
  if (changed) {
    InvalidateRect(hwnd_, nullptr, FALSE);
  }
}

void PreviewWindow::ActivateAction(int id) {
  switch (id) {
    case kIdHtmlRich:
      // 切模式先退出编辑（提交）：编辑中切到别的视图，防抖提交的是该视图
      // 的文本而不是用户想保存的内容。
      if (editing_) {
        ExitEditMode(true);
      }
      html_mode_ = PreviewMode::kRichText;
      UpdateWebContent();
      UpdateRichEditContent();  // 回切富文本：清掉纯文本视图残留，内容归 WebView
      UpdateVisibleControls();  // RichEdit/编辑 pill 的显隐随视图切换
      InvalidateRect(hwnd_, nullptr, FALSE);
      return;
    case kIdHtmlPlain:
      if (editing_) {
        ExitEditMode(true);
      }
      html_mode_ = PreviewMode::kPlainText;
      // 正文由 RichEdit 承担：切 tab 必须重新填充（InitForClip 时模式是
      // kRichText，RichEdit 此前没装纯文本内容）。
      UpdateRichEditContent();
      UpdateWebContent();
      UpdateVisibleControls();
      InvalidateRect(hwnd_, nullptr, FALSE);
      return;
    case kIdHtmlSource:
      if (editing_) {
        ExitEditMode(true);
      }
      html_mode_ = PreviewMode::kSource;
      UpdateRichEditContent();
      UpdateWebContent();
      UpdateVisibleControls();
      InvalidateRect(hwnd_, nullptr, FALSE);
      return;
    case kIdOpenExternal:
      OpenWithSystemDefaultApp();
      return;
    case kIdImageCopy:
      if (host_actions_.on_copy_image) {
        host_actions_.on_copy_image();
      }
      return;
    case kIdOcrCopy:
      if (clip_.has_value() && host_actions_.on_copy_ocr_text) {
        host_actions_.on_copy_ocr_text(clip_->summary.id);
      }
      return;
    case kIdOcrPaste:
      if (clip_.has_value() && host_actions_.on_paste_ocr_text) {
        host_actions_.on_paste_ocr_text(clip_->summary.id);
      }
      return;
    case kIdEditToggle:
      // 编辑开关：进入编辑（RichEdit 解锁+抢前台拿键盘焦点）或退出（提交）。
      if (editing_) {
        ExitEditMode(true);
      } else {
        EnterEditMode();
      }
      return;
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
      return;
    default:
      return;
  }
}

// ---- 悬停/选中共用的内容装配 ----

// ---- 文本编辑（自动保存=写回剪贴板生成新条目）----

void PreviewWindow::EnterEditMode() {
  if (!clip_.has_value() || editing_ || rich_edit_ == nullptr ||
      !host_actions_.on_commit_text) {
    return;
  }
  editing_ = true;
  SendMessageW(rich_edit_, EM_SETREADONLY, FALSE, 0);
  UpdateVisibleControls();
  InvalidateRect(hwnd_, nullptr, FALSE);
  // NOACTIVATE 窗口拿不到鼠标激活，编辑需要键盘焦点：可编程激活预览窗
  // （点击 pill 属于近期用户输入，SetForegroundWindow 允许），焦点给 RichEdit。
  SetForegroundWindow(hwnd_);
  SetFocus(rich_edit_);
}

void PreviewWindow::ExitEditMode(bool commit_pending) {
  if (!editing_) {
    return;
  }
  KillTimer(hwnd_, kEditSaveTimerId);
  if (commit_pending) {
    CommitEditIfChanged();
  }
  editing_ = false;
  SendMessageW(rich_edit_, EM_SETREADONLY, TRUE, 0);
  UpdateVisibleControls();
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void PreviewWindow::CommitEditIfChanged() {
  if (!clip_.has_value() || !host_actions_.on_commit_text) {
    return;
  }
  const int length = GetWindowTextLengthW(rich_edit_);
  if (length <= 0) {
    return;
  }
  std::wstring text(static_cast<size_t>(length) + 1, L'\0');
  const int copied = GetWindowTextW(rich_edit_, text.data(), length + 1);
  text.resize(static_cast<size_t>(copied));
  const std::wstring original = common::Utf8ToWide(clip_->plain_text).wide;
  if (text == original) {
    return;
  }
  host_actions_.on_commit_text(text);
}

void PreviewWindow::InitForClip(const core::store::ClipRecord& clip) {
  // 分段计时：悬停触发链全部在 UI 线程，任何一段超预算都会卡列表渲染帧。
  common::Stopwatch init_sw;
  std::int64_t fragment_us = 0;
  std::int64_t files_us = 0;
  std::int64_t web_us = 0;
  clip_ = clip;
  html_mode_ = PreviewMode::kRichText;
  // 切换条目：编辑态不提交直接退出（防抖已把输入写进历史，RichEdit 内容
  // 马上会被新条目覆盖，旧编辑态残留会污染新条目的事件流）。
  if (editing_) {
    editing_ = false;
    KillTimer(hwnd_, kEditSaveTimerId);
    SendMessageW(rich_edit_, EM_SETREADONLY, TRUE, 0);
  }
  // 存储层保留原始 CF_HTML（回写剪贴板需要头），展示层在此解析正文片段，
  // WebView 富文本/源码视图/尺寸测量共用同一份，避免每帧重复解析。
  html_fragment_ = ExtractHtmlFragment(clip_->html_text);
  fragment_us = init_sw.ElapsedUs();
  RefreshOcrState();
  ReloadFilesModel();  // 先建清单，LoadPreviewImage 依赖"单文件图片"判定
  files_us = init_sw.ElapsedUs() - fragment_us;
  LoadPreviewImage();
  UpdateRichEditContent();
  UpdateVisibleControls();
  LayoutControls();
  UpdateWebContent();
  web_us = init_sw.ElapsedUs() - fragment_us - files_us;
  SetInteractionGuardActive(true);
  InvalidateRect(hwnd_, nullptr, FALSE);
  TriggerAutoOcrIfNeeded();
  // 只在超过 4ms 预算时记日志（否则每行悬停刷屏），阈值判断用 QPC 微秒。
  if (init_sw.ElapsedUs() >= 4000) {
    char message[160]{};
    snprintf(
        message,
        sizeof(message),
        "init_for_clip slow total=%lldus fragment=%lldus files=%lldus web=%lldus type=%d",
        static_cast<long long>(init_sw.ElapsedUs()),
        static_cast<long long>(fragment_us),
        static_cast<long long>(files_us),
        static_cast<long long>(web_us),
        static_cast<int>(clip_->summary.content_type));
    common::WriteLog(common::LogLevel::Warning, "preview.show", message);
  }
}

// ---- WebView2 富文本渲染 ----

// 事件回调的共享状态（WebViewState 定义于头文件）：
// 回调只持有 shared_ptr，PreviewWindow 析构后 alive=false，回调安全空转，杜绝 this 悬垂。

void PreviewWindow::OnWebViewControllerReady(ICoreWebView2Controller* controller) {
  webview_controller_ = controller;
  controller->get_CoreWebView2(&webview_);
  if (webview_ == nullptr) {
    webview_failed_ = true;
    InvalidateRect(hwnd_, nullptr, FALSE);
    return;
  }
  // 安全边界：脚本不执行 + 调试/宿主对象/弹窗全禁（水位高于 DOMPurify 消毒）。
  ICoreWebView2Settings* settings = nullptr;
  if (SUCCEEDED(webview_->get_Settings(&settings)) && settings != nullptr) {
    settings->put_IsScriptEnabled(FALSE);
    settings->put_AreDevToolsEnabled(FALSE);
    settings->put_AreHostObjectsAllowed(FALSE);
    settings->put_AreDefaultScriptDialogsEnabled(FALSE);
    settings->put_IsStatusBarEnabled(FALSE);
    settings->put_IsZoomControlEnabled(FALSE);
    settings->put_IsBuiltInErrorPageEnabled(FALSE);
    settings->Release();
  }
  auto state = webview_state_;
  auto nav_handler = Microsoft::WRL::Callback<ICoreWebView2NavigationStartingEventHandler>(
      [state](ICoreWebView2* sender, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
        if (state != nullptr && !state->doc_loading.load()) {
          args->put_Cancel(TRUE);
        }
        return S_OK;
      });
  webview_->add_NavigationStarting(nav_handler.Get(), nullptr);
  auto nav_done_handler = Microsoft::WRL::Callback<ICoreWebView2NavigationCompletedEventHandler>(
      [state, generation = webview_generation_](ICoreWebView2* sender, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
        if (state != nullptr) {
          state->doc_loading.store(false);
          // 回 UI 线程再显示：导航完成前 WebView 保持隐藏，D2D 纯文本兜底，
          // 消除"旧内容/白屏先闪现再跳新内容"的不同步感。
          if (state->hwnd != nullptr) {
            PostMessageW(state->hwnd, PreviewWindow::kWebViewReadyMessage, 0,
                         static_cast<LPARAM>(generation));
            char message[96]{};
            snprintf(message, sizeof(message), "navigation completed in %u ms",
                     static_cast<unsigned>(GetTickCount64() - state->nav_start.load()));
            common::WriteLog(common::LogLevel::Info, "preview.webview", message);
          }
        }
        return S_OK;
      });
  webview_->add_NavigationCompleted(nav_done_handler.Get(), nullptr);
  auto new_window_handler = Microsoft::WRL::Callback<ICoreWebView2NewWindowRequestedEventHandler>(
      [](ICoreWebView2* sender, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
        args->put_Handled(TRUE);
        return S_OK;
      });
  webview_->add_NewWindowRequested(new_window_handler.Get(), nullptr);
  LayoutControls();
  UpdateWebContent();
  InvalidateRect(hwnd_, nullptr, FALSE);
}

void PreviewWindow::PrewarmWebView() {
  // 幂等：EnsureWebView 内部有 attempted/failed 检查，重复调用无害。
  if (hwnd_ == nullptr) {
    return;
  }
  EnsureWebView();
}

void PreviewWindow::EnsureWebView() {
  if (webview_init_attempted_ || webview_failed_) {
    return;
  }
  webview_init_attempted_ = true;
  ++webview_generation_;
  const std::uint64_t generation = webview_generation_;
  webview_state_ = std::make_shared<WebViewState>();
  webview_state_->alive.store(true);
  webview_state_->hwnd = hwnd_;
  auto alive = webview_state_;
  const std::wstring user_data_folder = ResolveWebViewUserDataFolder();
  common::WriteLog(
      common::LogLevel::Info,
      "preview.webview",
      "environment create begin folder=" + common::WideToUtf8(user_data_folder).utf8);

  auto on_controller =
      Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
          [this, alive, generation](HRESULT controller_result, ICoreWebView2Controller* controller) -> HRESULT {
            if (alive == nullptr || !alive->alive.load() || generation != webview_generation_ ||
                FAILED(controller_result) || controller == nullptr) {
              if (alive != nullptr && alive->alive.load()) {
                char message[96]{};
                snprintf(
                    message,
                    sizeof(message),
                    "controller create failed hr=0x%08lX",
                    static_cast<unsigned long>(controller_result));
                common::WriteLog(common::LogLevel::Error, "preview.webview", message);
                webview_failed_ = true;
                InvalidateRect(hwnd_, nullptr, FALSE);
              }
              return S_OK;
            }
            common::WriteLog(common::LogLevel::Info, "preview.webview", "controller ready");
            OnWebViewControllerReady(controller);
            return S_OK;
          });

  auto on_environment =
      Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
          [this, alive, generation, on_controller](
              HRESULT result, ICoreWebView2Environment* environment) -> HRESULT {
            if (alive == nullptr || !alive->alive.load() || generation != webview_generation_ || FAILED(result)) {
              if (alive != nullptr && alive->alive.load()) {
                char message[96]{};
                snprintf(
                    message,
                    sizeof(message),
                    "environment create failed hr=0x%08lX",
                    static_cast<unsigned long>(result));
                common::WriteLog(common::LogLevel::Error, "preview.webview", message);
                webview_failed_ = true;
                InvalidateRect(hwnd_, nullptr, FALSE);
              }
              return S_OK;
            }
            common::WriteLog(common::LogLevel::Info, "preview.webview", "environment ready");
            environment->CreateCoreWebView2Controller(hwnd_, on_controller.Get());
            return S_OK;
          });
  const HRESULT create_hr =
      CreateCoreWebView2EnvironmentWithOptions(nullptr, user_data_folder.c_str(), nullptr, on_environment.Get());
  if (FAILED(create_hr)) {
    char message[96]{};
    snprintf(
        message,
        sizeof(message),
        "CreateCoreWebView2EnvironmentWithOptions failed hr=0x%08lX",
        static_cast<unsigned long>(create_hr));
    common::WriteLog(common::LogLevel::Error, "preview.webview", message);
    webview_failed_ = true;
  }
}

void PreviewWindow::TeardownWebView() {
  if (webview_state_ != nullptr) {
    webview_state_->alive.store(false);
    webview_state_.reset();
  }
  if (webview_controller_ != nullptr) {
    webview_controller_->put_Bounds(RECT{0, 0, 0, 0});
    webview_controller_->Close();
    webview_controller_.Reset();
  }
  webview_.Reset();
  webview_init_attempted_ = false;
}

void PreviewWindow::UpdateWebContent() {
  const bool want_web =
      clip_.has_value() &&
      clip_->summary.content_type == core::store::ClipContentType::Html &&
      html_mode_ == PreviewMode::kRichText;
  if (!want_web) {
    ApplyWebViewBounds();
    return;
  }
  // WebView 创建失败（环境/controller 均不可用）：强制切纯文本模式，让
  // RichEdit 接管正文——否则富文本模式下既没 WebView 又没有文本视图，正文空白。
  if (webview_failed_) {
    html_mode_ = PreviewMode::kPlainText;
    UpdateRichEditContent();
    UpdateVisibleControls();
    ApplyWebViewBounds();
    return;
  }
  if (!WebViewUsable()) {
    EnsureWebView();
    // 未就绪/失败时 D2D 侧按纯文本兜底绘制，WebView 就绪回调后再接管。
    return;
  }
  // 空 fragment（只有头没有正文的 CF_HTML）：WebView 只能渲染空文档，
  // 回退纯文本模式，避免"空白预览"。
  if (html_fragment_.empty()) {
    html_mode_ = PreviewMode::kPlainText;
    UpdateRichEditContent();
    UpdateVisibleControls();
    ApplyWebViewBounds();
    return;
  }
  const bool dark = IsDarkThemeActive();
  // 内容指纹相同（切回同一条目/同模式/同主题）：WebView 里就是这份内容，
  // 跳过导航直接显示——来回切换两条 HTML 时第二次是即时的。
  if (webview_clip_id_ == clip_->summary.id && webview_clip_mode_ == html_mode_ &&
      webview_clip_dark_ == dark) {
    ApplyWebViewBounds();
    return;
  }
  webview_clip_id_ = clip_->summary.id;
  webview_clip_mode_ = html_mode_;
  webview_clip_dark_ = dark;
  // 预览用途裁剪 fragment 到 128KB：导航/解析成本与大小成正比，网页复制的
  // 几百 KB DOM 是导航延迟大头；截断回退到最近 UTF-8 边界，尾部标签由
  // 浏览器容错闭合。
  constexpr size_t kPreviewFragmentCap = 128 * 1024;
  // 拷贝而非 move：html_fragment_ 是条目级缓存（源码视图/尺寸测量还要用），
  // move 出去后源码 tab 只能拿到空串。
  std::string fragment = html_fragment_;
  if (fragment.size() > kPreviewFragmentCap) {
    size_t cut = kPreviewFragmentCap;
    while (cut > 0 && (static_cast<unsigned char>(fragment[cut]) & 0xC0) == 0x80) {
      --cut;
    }
    fragment.resize(cut);
    common::WriteLog(common::LogLevel::Info, "preview.webview",
                     "fragment truncated to 128KB for fast navigation");
  }
  std::wstring doc;
  if (BuildPreviewHtmlDoc(fragment, dark, &doc) == PreviewDocStatus::kTooLarge) {
    html_mode_ = PreviewMode::kPlainText;
    webview_clip_id_ = -1;
    UpdateRichEditContent();
    UpdateVisibleControls();
    ApplyWebViewBounds();
    return;
  }
  webview_state_->doc_loading.store(true);
  webview_state_->nav_start.store(GetTickCount64());
  webview_->NavigateToString(doc.c_str());
  SetTimer(hwnd_, kWebViewTimeoutTimerId, 800, nullptr);
  // 导航完成前 WebView 保持隐藏（ApplyWebViewBounds 按 doc_loading 抑制），
  // 期间 D2D 侧纯文本兜底，完成后 kWebViewReadyMessage 再显示。
  ApplyWebViewBounds();
}

void PreviewWindow::SetWebViewVisible(bool visible) {
  if (webview_controller_ == nullptr) {
    return;
  }
  webview_controller_->put_IsVisible(visible ? TRUE : FALSE);
  if (visible) {
    // 显示即无条件 Resume：Resume 对未挂起的 WebView 是无害 no-op，
    // 而 get_IsSuspended 在 TrySuspend 异步流程中会误报 false（漏恢复
    // 即永久挂起）。恢复后取消延迟挂起计时。
    KillTimer(hwnd_, kSuspendTimerId);
    if (webview_ != nullptr) {
      Microsoft::WRL::ComPtr<ICoreWebView2_3> web3;
      if (SUCCEEDED(webview_.As(&web3))) {
        web3->Resume();
      }
    }
  }
}

void PreviewWindow::SuspendWebView() {
  if (webview_ == nullptr) {
    return;
  }
  Microsoft::WRL::ComPtr<ICoreWebView2_3> web3;
  if (FAILED(webview_.As(&web3))) {
    return;
  }
  BOOL suspended = FALSE;
  if (SUCCEEDED(web3->get_IsSuspended(&suspended)) && suspended) {
    return;
  }
  // 前置条件：IsVisible=false（Hide() 已先行设置）。回调只持有共享状态。
  auto state = webview_state_;
  auto handler = Microsoft::WRL::Callback<ICoreWebView2TrySuspendCompletedHandler>(
      [state](HRESULT, BOOL) -> HRESULT { return S_OK; });
  web3->TrySuspend(handler.Get());
}

void PreviewWindow::ApplyWebViewBounds() {
  if (webview_controller_ == nullptr) {
    return;
  }
  const bool loading = webview_state_ != nullptr && webview_state_->doc_loading.load();
  const bool show_web =
      WebViewUsable() &&
      !loading &&
      clip_.has_value() &&
      clip_->summary.content_type == core::store::ClipContentType::Html &&
      html_mode_ == PreviewMode::kRichText &&
      hwnd_ != nullptr;
  if (!show_web) {
    webview_controller_->put_Bounds(RECT{0, 0, 0, 0});
    webview_controller_->put_IsVisible(FALSE);
    return;
  }
  webview_controller_->put_IsVisible(TRUE);
  RECT client{};
  GetClientRect(hwnd_, &client);
  // 全出血：左右/底部贴窗缘（网页自身只留 8px padding），只让开头部 80px。
  // 此前 bounds 四周缩 24px + 网页 padding 14px，文字离窗缘 38px——用户眼中
  // 的"巨大边框"就是这圈叠加留白。
  const int body_top = 80;
  const RECT bounds{
      0,
      body_top,
      std::max(0, static_cast<int>(client.right)),
      std::max(body_top, static_cast<int>(client.bottom))};
  webview_controller_->put_Bounds(bounds);
}

// ---- 绘制 ----

// ---- 内容自适应尺寸 ----

void PreviewWindow::MeasureTextExtent(
    const std::wstring& text,
    IDWriteTextFormat* format,
    float wrap_width,
    float* longest_line,
    float* wrapped_height) const {
  if (longest_line != nullptr) *longest_line = 0.0f;
  if (wrapped_height != nullptr) *wrapped_height = 0.0f;
  if (format == nullptr || text.empty()) {
    return;
  }
  IDWriteFactory* factory = d2d_shell_.dwrite_factory();
  if (factory == nullptr) {
    return;
  }
  // 超长文本测量封顶（高度有 clamp，近似即可）。
  const std::wstring measured = text.substr(0, 20000);
  // Pass 1：nowrap 一次布局（布局宽 100000 保证不换行）——GetMetrics 的
  // width 即最长行宽、height 即不换行总高。旧实现逐行 CreateTextLayout
  // （O(n²)），长文本把主线程卡几十毫秒——正是文本预览延迟/闪烁的元凶。
  Microsoft::WRL::ComPtr<IDWriteTextLayout> nowrap;
  float longest = 0.0f;
  if (SUCCEEDED(factory->CreateTextLayout(
          measured.c_str(), static_cast<UINT32>(measured.size()), format,
          100000.0f, 100000.0f, nowrap.GetAddressOf())) &&
      nowrap != nullptr) {
    DWRITE_TEXT_METRICS metrics{};
    if (SUCCEEDED(nowrap->GetMetrics(&metrics))) {
      longest = metrics.width;
    }
  }
  if (longest_line != nullptr) {
    *longest_line = longest;
  }
  if (wrapped_height == nullptr) {
    return;
  }
  // Pass 2：按"最终窗口内宽"（min(最长行, wrap_width)）换行测高。窗口未被
  // 钳上限时渲染不换行，高度即真实行高合计；被钳上限时按上限宽换行。
  const float effective_width =
      longest > 0.0f ? std::min(longest + 2.0f, wrap_width) : wrap_width;
  Microsoft::WRL::ComPtr<IDWriteTextLayout> wrapped;
  if (SUCCEEDED(factory->CreateTextLayout(
          measured.c_str(), static_cast<UINT32>(measured.size()), format,
          effective_width, 100000.0f, wrapped.GetAddressOf())) &&
      wrapped != nullptr) {
    DWRITE_TEXT_METRICS metrics{};
    if (SUCCEEDED(wrapped->GetMetrics(&metrics))) {
      *wrapped_height = metrics.height;
    }
  }
}

SIZE PreviewWindow::ComputePreferredSize(bool compact) const {
  const int max_w = compact ? kHoverMaxWidth : kSelectionMaxWidth;
  const int max_h = compact ? kHoverMaxHeight : kSelectionMaxHeight;
  const int content_max_w = max_w - static_cast<int>(kPad) * 2;
  int width = compact ? 460 : 520;
  int height = 420;
  if (!clip_.has_value()) {
    return {width, height};
  }
  const auto type = clip_->summary.content_type;
  // Files 指向单图片且解码就绪 → 与图片条目同一套按比例算尺寸。
  const bool files_image_ready =
      type == core::store::ClipContentType::Files && preview_width_ > 0 && preview_height_ > 0;
  if (type == core::store::ClipContentType::Files && !files_image_ready) {
    // 统计行 + 行清单。
    const int rows = static_cast<int>(files_model_.entries.size());
    width = 480;
    height = static_cast<int>(kBodyTop) + static_cast<int>(kFileStatsHeight) +
        rows * kFileRowHeight + 12 + static_cast<int>(kPad);
  } else if (type == core::store::ClipContentType::Image || files_image_ready) {
    const float body_top = files_image_ready ? kBodyTop : 114.0f;
    int extra = 0;
    if (!files_image_ready) {
      const std::wstring ocr = OcrDisplayText();
      if (!ocr.empty()) {
        float longest = 0.0f;
        float wrapped = 0.0f;
        MeasureTextExtent(ocr, d2d_shell_.body_format(),
                          static_cast<float>(content_max_w), &longest, &wrapped);
        extra = static_cast<int>(std::min(wrapped, 140.0f)) + 12;
      }
    }
    int image_width = preview_width_;
    int image_height = preview_height_;
    if (image_width <= 0 || image_height <= 0) {
      image_width = clip_->summary.image_width;
      image_height = clip_->summary.image_height;
    }
    if (image_width > 0 && image_height > 0) {
      // 与 RenderFrame 的 contain 完全同一套可用区域：fit 由（上限窗口 - 头部/
      // 底边距 - OCR 区）决定，窗口尺寸 = 图片×fit + 边距常量，保证绘制阶段
      // contain 后零残余空白（此前 fit 与渲染 contain 脱节，曾出现大片假边框）。
      const float avail_w = static_cast<float>(max_w) - kImageEdgePad * 2.0f;
      const float avail_h = static_cast<float>(max_h) - body_top -
          static_cast<float>(kPad) - static_cast<float>(extra);
      const float fit = std::min(
          1.0f,
          std::min(
              avail_w / static_cast<float>(image_width),
              avail_h / static_cast<float>(image_height)));
      width = static_cast<int>(image_width * fit) + static_cast<int>(kImageEdgePad) * 2;
      height = static_cast<int>(body_top) + static_cast<int>(image_height * fit) +
          static_cast<int>(kPad) + extra;
      // 图片模式贴图优先：最小宽度放宽，绕过文本用的 340 下限（末尾统一 clamp）。
      width = std::clamp(width, kImageMinWidth, max_w);
      height = std::clamp(height, kPreviewMinHeight, max_h);
      return {width, height};
    }
    width = 480;
    height = 380 + extra;
  } else if (type == core::store::ClipContentType::Rtf) {
    width = 560;
    height = 480;
  } else {
    // 文本类（含 HTML 三档）：按最长行定宽、按换行总高定高。
    // HTML 富文本（WebView）无 DOM 测量（安全边界禁脚本，异步测量会跳窗），
    // 用 plain_text 近似 + 富文本排版余量——内容少时窗口随之收紧。
    const bool is_html_rich =
        type == core::store::ClipContentType::Html && html_mode_ == PreviewMode::kRichText;
    const std::wstring body = common::Utf8ToWide(
        clip_->summary.content_type == core::store::ClipContentType::Html &&
                html_mode_ == PreviewMode::kSource
            ? html_fragment_
            : clip_->plain_text).wide;
    float longest = 0.0f;
    float wrapped = 0.0f;
    if (text_measure_cache_.valid && text_measure_cache_.clip_id == clip_->summary.id &&
        text_measure_cache_.mode == html_mode_) {
      longest = text_measure_cache_.longest;
      wrapped = text_measure_cache_.wrapped;
    } else {
      MeasureTextExtent(body, d2d_shell_.body_format(),
                        static_cast<float>(content_max_w), &longest, &wrapped);
      text_measure_cache_ = {clip_->summary.id, html_mode_, longest, wrapped, true};
    }
    width = static_cast<int>(longest) + static_cast<int>(kPad) * 2 + 8;
    height = static_cast<int>(kBodyTop) + static_cast<int>(wrapped) + static_cast<int>(kPad);
    if (is_html_rich) {
      // 网页侧 padding 12×2 + line-height 1.5 与 D2D 测量的差值余量。
      width += 48;
      height += 40;
    }
  }
  width = std::clamp(width, kPreviewMinWidth, max_w);
  height = std::clamp(height, kPreviewMinHeight, max_h);
  return {width, height};
}

void PreviewWindow::RenderFrame() {
  // C1 暗色模式：主题纪元变化时重建刷子并刷新 DWM 暗色属性与 rich-edit 配色。
  if (d2d_shell_.SyncTheme()) {
    ApplyDarkModeAttribute(hwnd_, clipd2d::ui::IsDarkThemeActive());
    ApplyPreviewRichEditTheme();
    // 主题切换：WebView 内文档的配色壳已过期，强制下次重新导航。
    webview_clip_id_ = -1;
    UpdateWebContent();
  }
  ID2D1HwndRenderTarget* target = d2d_shell_.BeginFrame();
  if (target == nullptr) {
    return;
  }
  RECT client{};
  GetClientRect(hwnd_, &client);
  // 彻底无边框：整窗 surface 填充，外轮廓由 DWM 圆角负责，不再描内边框
  // （圆角描边与 DWM 抗锯齿叠加会产生双层边框观感）。
  const D2D1_RECT_F full = D2D1::RectF(0.0f, 0.0f, static_cast<float>(client.right), static_cast<float>(client.bottom));
  target->FillRectangle(full, d2d_shell_.surface_brush());

  if (!clip_.has_value()) {
    d2d_shell_.EndFrame();
    return;
  }

  // 头部：标题 + 类型·来源 meta + 分隔线。
  const float content_right = full.right - kPad;
  const std::wstring title = common::Utf8ToWide(clip_->summary.title.empty() ? "Preview" : clip_->summary.title).wide;
  const std::wstring meta = FormatHeader(*clip_);
  target->DrawTextW(title.c_str(), static_cast<UINT32>(title.size()), d2d_shell_.section_format(), D2D1::RectF(kPad, 14.0f, content_right, 40.0f), d2d_shell_.text_brush(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
  target->DrawTextW(meta.c_str(), static_cast<UINT32>(meta.size()), d2d_shell_.meta_format(), D2D1::RectF(kPad, 42.0f, content_right, 62.0f), d2d_shell_.muted_text_brush(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
  target->DrawLine(D2D1::Point2F(kPad, 68.0f), D2D1::Point2F(content_right, 68.0f), d2d_shell_.border_brush(), 1.0f);

  // D2D 自绘 pill 按钮：格式 tab（meta 行右端）+ 图片操作行（分隔线下），
  // 激活 tab 用 accent 弱化底、悬停用 surface 高亮、禁用用 subtle 文本。
  const auto draw_pills = [&]() {
    constexpr float kRadius = 12.0f;
    for (const PillButton& button : pill_buttons_) {
      if (!button.visible) {
        continue;
      }
      const bool is_tab =
          button.id == kIdHtmlRich || button.id == kIdHtmlPlain || button.id == kIdHtmlSource;
      const bool active_tab = is_tab &&
          ((button.id == kIdHtmlRich && html_mode_ == PreviewMode::kRichText) ||
           (button.id == kIdHtmlPlain && html_mode_ == PreviewMode::kPlainText) ||
           (button.id == kIdHtmlSource && html_mode_ == PreviewMode::kSource));
      if (active_tab) {
        target->FillRoundedRectangle(
            D2D1::RoundedRect(button.rect, kRadius, kRadius), d2d_shell_.accent_soft_brush());
      } else if (button.hovered && button.enabled) {
        target->FillRoundedRectangle(
            D2D1::RoundedRect(button.rect, kRadius, kRadius), d2d_shell_.surface_hover_brush());
      } else if (!is_tab) {
        target->FillRoundedRectangle(
            D2D1::RoundedRect(button.rect, kRadius, kRadius), d2d_shell_.surface_alt_brush());
      }
      ID2D1SolidColorBrush* label_brush = !button.enabled
          ? d2d_shell_.subtle_text_brush()
          : (active_tab ? d2d_shell_.accent_brush() : d2d_shell_.text_brush());
      DrawTextCenteredInRect(
          target, d2d_shell_.dwrite_factory(), button.label, d2d_shell_.small_format(),
          button.rect, label_brush);
    }
  };
  draw_pills();

  const bool webview_active =
      clip_->summary.content_type == core::store::ClipContentType::Html &&
      html_mode_ == PreviewMode::kRichText && WebViewUsable();
  const D2D1_RECT_F body_rect = D2D1::RectF(kPad, kBodyTop, content_right, full.bottom - kPad);
  const bool is_html = clip_->summary.content_type == core::store::ClipContentType::Html;
  const bool is_image = clip_->summary.content_type == core::store::ClipContentType::Image;
  const bool is_files = clip_->summary.content_type == core::store::ClipContentType::Files;

  // Bitmap contain 绘制（图片条目与 Files 单图片共用）；Ctrl 滚轮缩放生效。
  const auto draw_contained_bitmap = [&](const D2D1_RECT_F& rect) {
    const float available_width = rect.right - rect.left;
    const float available_height = rect.bottom - rect.top;
    const float fit_scale = std::min(
        1.0f,
        std::min(
            available_width / static_cast<float>(preview_width_),
            available_height / static_cast<float>(preview_height_)));
    const float scale = fit_scale * image_zoom_;
    const float draw_width = static_cast<float>(preview_width_) * scale;
    const float draw_height = static_cast<float>(preview_height_) * scale;
    const float left = rect.left + (available_width - draw_width) * 0.5f;
    const float top = rect.top + (available_height - draw_height) * 0.5f;
    target->PushAxisAlignedClip(rect, D2D1_ANTIALIAS_MODE_ALIASED);
    target->DrawBitmap(
        preview_bitmap_.Get(),
        D2D1::RectF(left, top, left + draw_width, top + draw_height),
        1.0f,
        D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    target->PopAxisAlignedClip();
    if (image_zoom_ > 1.001f) {
      wchar_t zoom_label[16]{};
      swprintf_s(zoom_label, L"%d%%", static_cast<int>(image_zoom_ * 100.0f + 0.5f));
      target->DrawTextW(
          zoom_label,
          static_cast<UINT32>(wcslen(zoom_label)),
          d2d_shell_.meta_format(),
          D2D1::RectF(rect.right - 64.0f, rect.top + 4.0f, rect.right - 8.0f, rect.top + 24.0f),
          d2d_shell_.muted_text_brush(),
          D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
  };

  if (is_image) {
    // OCR 文本区（D2D 绘制，替代原 RichEdit 子控件）。
    const std::wstring ocr_text = OcrDisplayText();
    float ocr_height = 0.0f;
    if (!ocr_text.empty()) {
      float longest = 0.0f;
      float wrapped = 0.0f;
      // 测量宽度与下方 ocr_rect 实际渲染宽度一致（kPad..content_right）。
      MeasureTextExtent(
          ocr_text, d2d_shell_.body_format(), content_right - kPad, &longest, &wrapped);
      ocr_height = std::min(wrapped, 140.0f);
    }
    // 排版网格：标题 14 / meta 42 / tab 36 / 分隔线 68 / 操作行 76..104 / 图片起 114。
    // 图片区左右贴边（kImageEdgePad=8，与 ComputePreferredSize 同一套常量）。
    const D2D1_RECT_F image_rect = D2D1::RectF(
        kImageEdgePad, 114.0f, full.right - kImageEdgePad,
        full.bottom - kPad - (ocr_height > 0.0f ? ocr_height + 10.0f : 0.0f));
    if (preview_bitmap_ != nullptr && preview_width_ > 0 && preview_height_ > 0) {
      draw_contained_bitmap(image_rect);
    } else {
      const std::wstring waiting = image_load_pending_ ? L"图片解码中…" : L"图片不可用";
      target->DrawTextW(
          waiting.c_str(),
          static_cast<UINT32>(waiting.size()),
          d2d_shell_.meta_format(),
          D2D1::RectF(image_rect.left, (image_rect.top + image_rect.bottom) / 2 - 10.0f, image_rect.right, (image_rect.top + image_rect.bottom) / 2 + 10.0f),
          d2d_shell_.muted_text_brush(),
          D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    if (ocr_height > 0.0f && !ocr_text.empty()) {
      const D2D1_RECT_F ocr_rect = D2D1::RectF(
          kPad, image_rect.bottom + 10.0f, content_right, image_rect.bottom + 10.0f + ocr_height);
      target->DrawTextW(
          ocr_text.c_str(),
          static_cast<UINT32>(ocr_text.size()),
          d2d_shell_.body_format(),
          ocr_rect,
          d2d_shell_.muted_text_brush(),
          D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    d2d_shell_.EndFrame();
    return;
  }

  if (is_files) {
    if (preview_bitmap_ != nullptr && preview_width_ > 0 && preview_height_ > 0) {
      // 单文件指向图片：整幅贴边显示（与 ComputePreferredSize 同一套可用区域，
      // contain 后无残余空白；Ctrl 滚轮缩放与图片条目同款）。
      const D2D1_RECT_F image_rect = D2D1::RectF(
          kImageEdgePad, kBodyTop, full.right - kImageEdgePad, full.bottom - kImageEdgePad);
      draw_contained_bitmap(image_rect);
      d2d_shell_.EndFrame();
      return;
    }
    // 统计行；单文件且存在时补一行所在目录。
    std::wstring stats = L"\u5171 " + std::to_wstring(static_cast<int>(files_model_.entries.size())) + L" \u9879 \u00b7 \u603b\u5927\u5c0f " +
        (files_model_.entries.empty() ? L"\u2014" : FormatPreviewFileSize(files_model_.total_bytes));
    if (files_model_.missing_count > 0) {
      stats += L" \u00b7 \u7f3a\u5931 " + std::to_wstring(files_model_.missing_count) + L" \u9879";
    }
    float list_top = body_rect.top + kFileStatsHeight;
    target->DrawTextW(
        stats.c_str(),
        static_cast<UINT32>(stats.size()),
        d2d_shell_.meta_format(),
        D2D1::RectF(body_rect.left, body_rect.top, body_rect.right, body_rect.top + kFileStatsHeight),
        d2d_shell_.muted_text_brush(),
        D2D1_DRAW_TEXT_OPTIONS_CLIP);
    if (files_model_.entries.size() == 1 && files_model_.entries[0].exists &&
        !files_model_.entries[0].directory.empty()) {
      target->DrawTextW(
          files_model_.entries[0].directory.c_str(),
          static_cast<UINT32>(files_model_.entries[0].directory.size()),
          d2d_shell_.meta_format(),
          D2D1::RectF(body_rect.left, list_top, body_rect.right, list_top + 22.0f),
          d2d_shell_.muted_text_brush(),
          D2D1_DRAW_TEXT_OPTIONS_CLIP);
      list_top += 26.0f;
    }
    const float list_height = std::max(0.0f, body_rect.bottom - list_top);
    if (list_height > 0.0f && !files_model_.entries.empty()) {
      const int total_height = static_cast<int>(files_model_.entries.size()) * kFileRowHeight;
      const int max_scroll = std::max(0, total_height - static_cast<int>(list_height));
      files_scroll_offset_ = std::min(files_scroll_offset_, max_scroll);
      target->PushAxisAlignedClip(
          D2D1::RectF(body_rect.left, list_top, body_rect.right, body_rect.bottom),
          D2D1_ANTIALIAS_MODE_ALIASED);
      const float size_column = 76.0f;
      for (size_t index = static_cast<size_t>(files_scroll_offset_ / kFileRowHeight);
           index < files_model_.entries.size(); ++index) {
        const PreviewFileEntry& entry = files_model_.entries[index];
        const float row_top = list_top + static_cast<float>(static_cast<int>(index) * kFileRowHeight - files_scroll_offset_);
        if (row_top > body_rect.bottom) {
          break;
        }
        std::wstring label = entry.name;
        if (!entry.exists) {
          label += L"\uff08\u7f3a\u5931\uff09";
        }
        target->DrawTextW(
            label.c_str(),
            static_cast<UINT32>(label.size()),
            d2d_shell_.body_format(),
            D2D1::RectF(body_rect.left, row_top, body_rect.right - size_column, row_top + static_cast<float>(kFileRowHeight)),
            d2d_shell_.text_brush(),
            D2D1_DRAW_TEXT_OPTIONS_CLIP);
        const std::wstring size_text = entry.exists ? FormatPreviewFileSize(entry.size_bytes) : L"\u2014";
        target->DrawTextW(
            size_text.c_str(),
            static_cast<UINT32>(size_text.size()),
            d2d_shell_.meta_format(),
            D2D1::RectF(body_rect.right - size_column, row_top + 2.0f, body_rect.right, row_top + static_cast<float>(kFileRowHeight)),
            d2d_shell_.muted_text_brush(),
            D2D1_DRAW_TEXT_OPTIONS_CLIP);
      }
      target->PopAxisAlignedClip();
    }
    d2d_shell_.EndFrame();
    return;
  }

  // 文本类：RichText 走 WebView2（D2D 只画头部分隔），其余画 D2D 文本。
  if (webview_active) {
    d2d_shell_.EndFrame();
    return;
  }
  // 正文归属：纯文本/RTF/HTML 纯文本·源码全部由 RichEdit 呈现（只读态自带
  // 选择/复制，D2D 再画一遍就是双重绘制）；D2D 只在 HTML 富文本 WebView
  // 尚未就绪时垫一层纯文本兜底，避免加载期空白。
  std::wstring body;
  if (is_html && html_mode_ == PreviewMode::kRichText) {
    body = common::Utf8ToWide(clip_->plain_text).wide;
  }
  if (!body.empty()) {
    target->DrawTextW(
        body.c_str(),
        static_cast<UINT32>(body.size()),
        d2d_shell_.body_format(),
        body_rect,
        d2d_shell_.text_brush(),
        D2D1_DRAW_TEXT_OPTIONS_CLIP);
  }
  d2d_shell_.EndFrame();
}

LRESULT PreviewWindow::WindowProc(UINT message, WPARAM w_param, LPARAM l_param) {
  switch (message) {
    case WM_MOUSEACTIVATE:
      return MA_NOACTIVATE;
    case WM_MOUSEMOVE: {
      if (!preview_mouse_tracking_) {
        TRACKMOUSEEVENT track{};
        track.cbSize = sizeof(track);
        track.dwFlags = TME_LEAVE;
        track.hwndTrack = hwnd_;
        TrackMouseEvent(&track);
        preview_mouse_tracking_ = true;
      }
      const POINT pt{static_cast<LONG>(static_cast<short>(LOWORD(l_param))),
                     static_cast<LONG>(static_cast<short>(HIWORD(l_param)))};
      UpdateHoverFromPoint(pt);
      break;
    }
    case WM_MOUSELEAVE: {
      preview_mouse_tracking_ = false;
      bool had_hover = false;
      for (PillButton& button : pill_buttons_) {
        if (button.hovered) {
          button.hovered = false;
          had_hover = true;
        }
      }
      if (had_hover) {
        InvalidateRect(hwnd_, nullptr, FALSE);
      }
      if (host_actions_.on_mouse_left) {
        host_actions_.on_mouse_left();
      }
      return 0;
    }
    case WM_SIZE:
      if (d2d_shell_.hwnd() != nullptr) {
        d2d_shell_.Resize(LOWORD(l_param), HIWORD(l_param));
      }
      LayoutControls();
      InvalidateRect(hwnd_, nullptr, FALSE);
      return 0;
    case WM_MOUSEWHEEL:
      if (OnMouseWheel(w_param)) {
        return 0;
      }
      break;
    case PreviewWindow::kImageReadyMessage:
      HandleImageReady(l_param);
      return 0;
    case PreviewWindow::kWebViewReadyMessage:
      // 导航完成（UI 线程）：恢复 WebView 显示。generation 不匹配（已切换/
      // 已重建）时忽略——旧内容的 ready 不能把新内容的兜底顶掉。
      KillTimer(hwnd_, kWebViewTimeoutTimerId);
      if (static_cast<std::uint64_t>(l_param) == webview_generation_ &&
          WebViewUsable() && IsWindowVisible(hwnd_)) {
        ApplyWebViewBounds();
        InvalidateRect(hwnd_, nullptr, FALSE);
      }
      return 0;
    case WM_TIMER:
      if (w_param == kPrewarmTimerId) {
        KillTimer(hwnd_, kPrewarmTimerId);
        PrewarmWebView();
        return 0;
      }
      if (w_param == kSuspendTimerId) {
        KillTimer(hwnd_, kSuspendTimerId);
        if (!IsWindowVisible(hwnd_)) {
          SuspendWebView();  // 隐藏持续 5s：安全挂起降内存
        }
        return 0;
      }
      if (w_param == kWebViewTimeoutTimerId) {
        // 导航超时兜底：挂起竞争/导航卡死时强制显示（哪怕内容没渲染完），
        // 绝不让富文本预览空白卡死。
        KillTimer(hwnd_, kWebViewTimeoutTimerId);
        if (webview_state_ != nullptr && webview_state_->doc_loading.load()) {
          webview_state_->doc_loading.store(false);
          common::WriteLog(common::LogLevel::Warning, "preview.webview",
                           "navigation timeout, forcing display");
          ApplyWebViewBounds();
          InvalidateRect(hwnd_, nullptr, FALSE);
        }
        return 0;
      }
      if (w_param == kEditSaveTimerId) {
        // 编辑防抖到期：文本有变化才写回剪贴板（生成新条目）。
        KillTimer(hwnd_, kEditSaveTimerId);
        if (editing_) {
          CommitEditIfChanged();
        }
        return 0;
      }
      break;
    case WM_COMMAND:
      // RichEdit 的 EN_CHANGE（编辑防抖入口）。非编辑态忽略——只读控件
      // 也会因 SetWindowText 等内容重置发 EN_CHANGE，不能据此开防抖。
      if (LOWORD(w_param) == kIdRichEdit && HIWORD(w_param) == EN_CHANGE && editing_) {
        SetTimer(hwnd_, kEditSaveTimerId, 800, nullptr);
      }
      return 0;
    case WM_NOTIFY: {
      // RichEdit 的 EN_MSGFILTER（键事件）：Esc 退出编辑并提交。
      const NMHDR* header = reinterpret_cast<const NMHDR*>(l_param);
      if (header != nullptr && header->idFrom == static_cast<UINT_PTR>(kIdRichEdit) &&
          header->code == EN_MSGFILTER && editing_) {
        const MSGFILTER* filter = reinterpret_cast<const MSGFILTER*>(l_param);
        if (filter->msg == WM_KEYDOWN && filter->wParam == VK_ESCAPE) {
          ExitEditMode(true);
          return 0;
        }
      }
      break;
    }
    case WM_PAINT: {
      PAINTSTRUCT ps{};
      BeginPaint(hwnd_, &ps);
      EndPaint(hwnd_, &ps);
      RenderFrame();
      return 0;
    }
    case WM_LBUTTONDOWN: {
      // D2D pill 按钮命中（不再有 Win32 BUTTON 子控件发 WM_COMMAND）。
      const POINT pt{static_cast<LONG>(static_cast<short>(LOWORD(l_param))),
                     static_cast<LONG>(static_cast<short>(HIWORD(l_param)))};
      for (const PillButton& button : pill_buttons_) {
        if (button.visible && button.enabled &&
            pt.x >= button.rect.left && pt.x < button.rect.right &&
            pt.y >= button.rect.top && pt.y < button.rect.bottom) {
          ActivateAction(button.id);
          return 0;
        }
      }
      return 0;
    }
    case WM_CLOSE:
      Hide();
      return 0;
    case WM_ERASEBKGND:
      return 1;
  }
  return DefWindowProcW(hwnd_, message, w_param, l_param);
}

}  // namespace clipd2d::ui
