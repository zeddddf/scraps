#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <windows.h>
#include <wrl/client.h>

#include "common/Status.h"
#include "core/store/ClipTypes.h"
#include "ui/D2dShell.h"
#include "ui/PreviewContentModel.h"

struct ID2D1Bitmap;

namespace clipd2d::common {
class AsyncRunner;
}

// 直接引入完整定义：MainWindow 持有 PreviewWindow 成员，其析构会实例化
// ComPtr<ICoreWebView2*> 的 Release，需要完整类型（client.h InternalRelease）。
#include <WebView2.h>

#include <wrl/event.h>

namespace clipd2d::ui {

namespace detail {

std::wstring ResolvePreviewImagePath(const core::store::ClipRecord& clip);
std::wstring ResolveExternalImageOpenPath(const core::store::ClipRecord& clip);

}  // namespace detail

class PreviewWindow {
 public:
  struct HostActions {
    std::function<common::Status(std::int64_t, bool)> on_request_ocr;
    std::function<common::Status(std::int64_t)> on_copy_ocr_text;
    std::function<common::Status(std::int64_t)> on_paste_ocr_text;
    std::function<common::Status()> on_copy_image;
    std::function<common::Status(std::wstring*)> query_ocr_provider_status;
    std::function<void()> on_show;
    std::function<void()> on_hide;
    // 鼠标离开预览窗（悬停预览缓冲判定用；光标可能只是穿过缝隙）。
    std::function<void()> on_mouse_left;
    // 图片后台解码线程池（空则回退 UI 线程同步小图解码）。
    common::AsyncRunner* work_pool = nullptr;
    bool enable_image_ocr = true;
    std::wstring image_copy_label;
    // 编辑防抖到期提交：宿主写回剪贴板，由捕获管线自然生成新条目。
    std::function<void(const std::wstring&)> on_commit_text;
  };

  // HTML 条目的三档预览（对齐竞品多格式切换）。
  enum class PreviewMode {
    kRichText,   // WebView2 渲染（不可用时回退 kPlainText）
    kPlainText,  // D2D 文本绘制 plain_text
    kSource,     // D2D 文本绘制 html_text
  };

  PreviewWindow() = default;
  ~PreviewWindow();

  common::Status Create(HINSTANCE instance, HWND owner);
  void Destroy();
  // 选中态路径：保持窗口既有尺寸/位置。
  void ShowClip(const core::store::ClipRecord& clip, HostActions actions);
  // 悬停路径：compact 窗体定位到行旁（anchor_screen 为行屏幕矩形）。
  void ShowClipAt(
      const core::store::ClipRecord& clip,
      HostActions actions,
      const RECT& anchor_screen,
      bool compact);
  void Hide();
  HWND hwnd() const { return hwnd_; }
  bool ShowingClip(std::int64_t clip_id) const;
  // 编辑态：主面板失焦自动隐藏时必须豁免预览窗（编辑需要前台+键盘焦点）。
  bool editing() const { return editing_; }
  // 退出编辑态：commit_pending=true 时先提交未保存内容（写回剪贴板生成
  // 新条目）。宿主显式隐藏（HideWindow）前必须调用，防内容丢失。
  void ExitEditMode(bool commit_pending);
  // 悬停预览协作：光标当前是否落在预览窗范围内（用于主窗决定是否保留显示）。
  bool ContainsCursor() const;
  // 持续不变量：可见且与主面板（owner）相交时立即重定位；屏幕放不下则隐藏。
  // 主面板被拖动/重新弹出后调用（预览窗是 TOPMOST，盖在面板上即是事故）。
  void EnsureNotCoveringOwner();

 private:
  static constexpr int kIdHtmlRich = 3009;
  static constexpr int kIdHtmlPlain = 3001;
  static constexpr int kIdHtmlSource = 3002;
  static constexpr int kIdOpenExternal = 3003;
  static constexpr int kIdRichEdit = 3004;
  static constexpr int kIdOcrCopy = 3005;
  static constexpr int kIdOcrPaste = 3006;
  static constexpr int kIdOcrRerun = 3007;
  static constexpr int kIdImageCopy = 3008;
  static constexpr int kIdEditToggle = 3010;

  static LRESULT CALLBACK StaticWindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param);
  LRESULT WindowProc(UINT message, WPARAM w_param, LPARAM l_param);

  // ---- 悬停/选中共用的内容装配 ----
  void InitForClip(const core::store::ClipRecord& clip);

  // 悬停/选中显示的公共入口（ShowClip/ShowClipAt 只负责窗口摆放）。
  common::Status CreateControls();
  void ApplyPreviewRichEditTheme();
  void LayoutControls();
  // 统一防重叠定位：右侧 → 左侧 → 面板下方 → 贴右缘兜底，
  // 任何情况下预览窗都不覆盖 owner（主面板）。anchor_screen 同时是
  // 选屏与垂直基准；width/height 为已定的窗口尺寸。
  POINT ComputePlacement(const RECT& anchor_screen, int width, int height) const;
  // D2D 自绘 pill 按钮：命中测试/悬停/激活（替代 Win32 BUTTON 子控件）。
  struct PillButton {
    int id = 0;
    std::wstring label;
    D2D1_RECT_F rect{};
    bool visible = false;
    bool enabled = true;
    bool hovered = false;
  };
  PillButton* FindButton(int id);
  void ActivateAction(int id);
  void UpdateHoverFromPoint(const POINT& client_pt);
  // 内容自适应尺寸：按类型（文本测量/文件行数/图片比例）算理想窗口大小。
  SIZE ComputePreferredSize(bool compact) const;
  // 文本测量：wrap_width 内换行总高 + 最长单行宽（nowrap）。
  void MeasureTextExtent(
      const std::wstring& text,
      IDWriteTextFormat* format,
      float wrap_width,
      float* longest_line,
      float* wrapped_height) const;  void RenderFrame();
  void UpdateVisibleControls();
  void LoadPreviewImage();
  void UpdateRichEditContent();
  std::wstring OcrDisplayText() const;
  void RefreshOcrState();
  void TriggerAutoOcrIfNeeded();
  void SetInteractionGuardActive(bool active);
  void OpenWithSystemDefaultApp();

  // WebView2 富文本渲染（懒加载；失败静默回退纯文本）。
  // WebViewState：事件回调只持有该共享对象，析构后置 alive=false 防悬垂。
  struct WebViewState {
    std::atomic<bool> alive{false};
    std::atomic<bool> doc_loading{false};
    // NavigationCompleted 回调经 PostMessage 回 UI 线程再显示（回调不持 this）。
    HWND hwnd{nullptr};
    // 导航起始时刻（诊断耗时用，回调读不到 this）。
    std::atomic<std::uint64_t> nav_start{0};
  };
  // WebView 当前内容指纹：切回同一条目/模式/主题时跳过导航即时显示。
  std::int64_t webview_clip_id_ = -1;
  PreviewMode webview_clip_mode_ = PreviewMode::kRichText;
  bool webview_clip_dark_ = false;
  // NavigationCompleted → UI 线程再显示 WebView（导航期间 D2D 纯文本兜底，
  // 消除旧内容/白屏闪现）。
  static constexpr UINT kWebViewReadyMessage = WM_APP + 8042;
  static constexpr UINT_PTR kPrewarmTimerId = 2;
  // 挂起延迟：隐藏 5s 后才 TrySuspend（异步挂起与快速重现的导航竞争
  // 会漏 Resume——WebView 卡挂起态、导航永不完成、内容永不显示）。
  static constexpr UINT_PTR kSuspendTimerId = 3;
  // 导航超时兜底：超时未完成强制显示（挂起竞争/导航卡死时不至于空白）。
  static constexpr UINT_PTR kWebViewTimeoutTimerId = 4;
  // 编辑态防抖保存：EN_CHANGE 后 800ms 无输入才提交（写回剪贴板生成新条目）。
  static constexpr UINT_PTR kEditSaveTimerId = 5;
  void EnterEditMode();
  // CommitEditIfChanged：读取 RichEdit 当前文本，与原条目不同时经
  // on_commit_text 写回剪贴板（由捕获管线自然生成新条目，历史原样保留）。
  void CommitEditIfChanged();
  bool editing_ = false;
  void UpdateWebContent();
  void EnsureWebView();
  void OnWebViewControllerReady(ICoreWebView2Controller* controller);
  void ApplyWebViewBounds();
  void TeardownWebView();
  // WebView2 生命周期：隐藏时 IsVisible=false + TrySuspend 让浏览器进程降内存，
  // 显示时 Resume；TeardownWebView（应用退出）Close 最后一组 controller 后
  // 浏览器进程由运行时自行退出（无强杀 API）。
  void SetWebViewVisible(bool visible);
  void SuspendWebView();
  bool WebViewUsable() const { return webview_ != nullptr && !webview_failed_; }
  // 启动后空闲预热：后台创建 WebView2 环境/controller，首次 HTML 预览
  // 只剩一次导航（~50ms），不再摊上 ~300ms 的创建成本。
  void PrewarmWebView();

  // 图片异步解码：UI 线程零解码（4K 图全尺寸解码曾致数十毫秒卡顿）。
  // 后台 LoadScaledBitmapPixels（封顶 2048）→ PostOwnedMessage 回投 → 建图。
  // 世代+路径双重防串：hover 快速换行时旧解码一律丢弃。
  static constexpr UINT kImageReadyMessage = WM_APP + 8041;
  void RequestPreviewImageAsync(const std::wstring& path);
  void HandleImageReady(LPARAM l_param);
  // 最近一次落位的锚（悬停=行矩形，选中=面板或旧位置矩形）；
  // 图片解码完成后窗口变宽，需按锚重排避免右缘侵入面板。
  RECT placement_anchor_{};
  // 文本测量缓存（ComputePreferredSize 是 const，悬停缓冲期内重复调用
  // 不重测）：同 clip + 同显示模式直接复用。
  struct TextMeasureCache {
    std::int64_t clip_id = -1;
    PreviewMode mode = PreviewMode::kRichText;
    float longest = 0.0f;
    float wrapped = 0.0f;
    bool valid = false;
  };
  mutable TextMeasureCache text_measure_cache_;

  // Files 清单与滚轮。
  void ReloadFilesModel();
  bool OnMouseWheel(WPARAM w_param);

  HINSTANCE instance_ = nullptr;
  HWND owner_ = nullptr;
  HWND hwnd_ = nullptr;
  // 唯一保留的子控件：RichEdit 仅用于 RTF 富文本渲染（D2D 无法排版 RTF）。
  // 其余按钮全部为 D2D 自绘 pill（见 pill_buttons_），预览窗内零 GDI 按钮。
  HWND rich_edit_ = nullptr;
  HFONT ui_font_ = nullptr;
  HMODULE rich_edit_module_ = nullptr;
  D2dShell d2d_shell_;
  HostActions host_actions_;
  std::optional<core::store::ClipRecord> clip_;
  bool interaction_guard_active_ = false;
  bool preview_mouse_tracking_ = false;
  PreviewMode html_mode_ = PreviewMode::kRichText;
  // InitForClip 时从 html_text（原始 CF_HTML）解析出的正文片段，供
  // WebView 富文本/源码视图/尺寸测量共用（避免每帧重复解析）。
  std::string html_fragment_;
  float image_zoom_ = 1.0f;
  int files_scroll_offset_ = 0;
  PreviewFileList files_model_;
  std::vector<PillButton> pill_buttons_;
  std::wstring ocr_provider_message_;
  Microsoft::WRL::ComPtr<ID2D1Bitmap> preview_bitmap_;
  UINT preview_width_ = 0;
  UINT preview_height_ = 0;
  std::uint64_t image_generation_ = 0;
  std::wstring image_decode_path_;
  bool image_load_pending_ = false;

  Microsoft::WRL::ComPtr<ICoreWebView2> webview_;
  Microsoft::WRL::ComPtr<ICoreWebView2Controller> webview_controller_;
  // 异步创建/事件回调的生命周期哨兵：Destroy 后回调一律丢弃，防 this 悬垂。
  std::shared_ptr<WebViewState> webview_state_;
  bool webview_init_attempted_ = false;
  bool webview_failed_ = false;
  // 每次内容切换递增；异步创建回调回来时 generation 不匹配即丢弃。
  std::uint64_t webview_generation_ = 0;
  // NavigateToString 前置位、NavigationCompleted 后清位；期间外的导航一律拦截。
  bool webview_doc_loading_ = false;
  std::wstring webview_user_data_folder_;
};

}  // namespace clipd2d::ui
