#include "ui/MainWindowInternal.h"
#include "core/clipboard/OpenTargetResolver.h"
#include "ui/PanelConfirmDialog.h"
#include "ui/PanelWindowChrome.h"

namespace clipd2d::ui {

struct PopupMenuItemData {
  UINT id = 0;
  std::wstring text;
  std::shared_ptr<std::vector<PopupMenuItemData>> submenu;
  bool separator = false;
  bool enabled = true;
  bool checked = false;
  RECT rect{};
};

namespace {

constexpr wchar_t kPopupMenuWindowClassName[] = L"Scraps.PopupMenuWindow";
constexpr int kPopupMenuBorder = 1;
constexpr int kPopupMenuHorizontalPadding = 12;
constexpr int kPopupMenuVerticalPadding = 6;
constexpr int kPopupMenuGutterWidth = 18;
constexpr int kPopupMenuArrowWidth = 18;
constexpr int kPopupMenuMinWidth = 160;
constexpr int kPopupMenuSeparatorHeight = 9;
constexpr int kPopupMenuItemHeight = 28;
constexpr int kPopupMenuCornerRadius = 12;

struct PopupMenuWindowState {
  MainWindow* owner = nullptr;
  HWND hwnd = nullptr;
  PopupMenuWindowState* parent = nullptr;
  std::unique_ptr<PopupMenuWindowState> child;
  std::vector<PopupMenuItemData> items;
  int hot_index = -1;
  int width = 0;
  int height = 0;
  D2dShell d2d_shell;
  bool d2d_ready = false;
  Microsoft::WRL::ComPtr<IDWriteTextFormat> text_format;
  Microsoft::WRL::ComPtr<IDWriteTextFormat> symbol_format;
};

struct PopupMenuSession {
  MainWindow* owner = nullptr;
  HINSTANCE instance = nullptr;
  std::unique_ptr<PopupMenuWindowState> root;
  int selected_command = 0;
  int open_window_count = 0;
  bool running = false;
  bool quit_received = false;
  WPARAM quit_code = 0;
};

PopupMenuSession* g_popup_menu_session = nullptr;

PopupMenuItemData MakePopupMenuItem(UINT id, std::wstring text, bool enabled = true, bool checked = false) {
  PopupMenuItemData item;
  item.id = id;
  item.text = std::move(text);
  item.enabled = enabled;
  item.checked = checked;
  return item;
}

PopupMenuItemData MakePopupMenuSeparator() {
  PopupMenuItemData item;
  item.separator = true;
  item.enabled = false;
  return item;
}

PopupMenuItemData MakePopupMenuSubmenu(std::wstring text, std::vector<PopupMenuItemData> items) {
  PopupMenuItemData item;
  item.text = std::move(text);
  item.submenu = std::make_shared<std::vector<PopupMenuItemData>>(std::move(items));
  return item;
}

std::vector<PopupMenuItemData> BuildGroupMenuItems(const std::vector<core::store::GroupRecord>& groups) {
  std::vector<PopupMenuItemData> items;
  items.push_back(MakePopupMenuItem(1, L"全部资料"));
  if (!groups.empty()) {
    items.push_back(MakePopupMenuSeparator());
    UINT command = 10;
    for (const auto& group : groups) {
      items.push_back(MakePopupMenuItem(command++, group.name));
    }
  }

  items.push_back(MakePopupMenuSeparator());
  items.push_back(MakePopupMenuItem(2000, L"新建资料夹..."));

  std::vector<PopupMenuItemData> manage_items;
  if (groups.empty()) {
    manage_items.push_back(MakePopupMenuItem(0, L"暂无资料夹", false));
  } else {
    for (size_t index = 0; index < groups.size(); ++index) {
      manage_items.push_back(MakePopupMenuItem(3000 + static_cast<UINT>(index), L"重命名： " + groups[index].name));
      manage_items.push_back(MakePopupMenuItem(4000 + static_cast<UINT>(index), L"删除： " + groups[index].name));
      if (index + 1 < groups.size()) {
        manage_items.push_back(MakePopupMenuSeparator());
      }
    }
  }
  items.push_back(MakePopupMenuSubmenu(L"管理资料夹", std::move(manage_items)));
  return items;
}


void ApplyPopupMenuRoundedRegion(HWND hwnd) {
  ApplyPopupPanelWindowChrome(hwnd);
}

bool EnsurePopupMenuWindowClass(HINSTANCE instance) {
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  if (GetClassInfoExW(instance, kPopupMenuWindowClassName, &wc)) {
    return true;
  }

  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = [](HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) -> LRESULT {
    auto* state = reinterpret_cast<PopupMenuWindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (message) {
      case WM_NCCREATE: {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(l_param);
        state = reinterpret_cast<PopupMenuWindowState*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        return TRUE;
      }
      case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
      case WM_CREATE:
        if (state != nullptr) {
          state->hwnd = hwnd;
          state->d2d_ready = state->d2d_shell.Initialize(hwnd).ok();
          if (state->d2d_ready) {
            state->d2d_shell.EnsureSecondaryTextFormats();
            if (auto* factory = state->d2d_shell.dwrite_factory()) {
              factory->CreateTextFormat(
                  L"Segoe UI",
                  nullptr,
                  DWRITE_FONT_WEIGHT_NORMAL,
                  DWRITE_FONT_STYLE_NORMAL,
                  DWRITE_FONT_STRETCH_NORMAL,
                  12.0f,
                  L"",
                  state->text_format.ReleaseAndGetAddressOf());
              factory->CreateTextFormat(
                  L"Segoe UI Symbol",
                  nullptr,
                  DWRITE_FONT_WEIGHT_NORMAL,
                  DWRITE_FONT_STYLE_NORMAL,
                  DWRITE_FONT_STRETCH_NORMAL,
                  12.0f,
                  L"",
                  state->symbol_format.ReleaseAndGetAddressOf());
              if (state->text_format) {
                state->text_format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                state->text_format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
              }
              if (state->symbol_format) {
                state->symbol_format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                state->symbol_format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                state->symbol_format->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
              }
            }
          }
          ApplyPopupMenuRoundedRegion(hwnd);
        }
        return 0;
      case WM_NCHITTEST:
        return HTCLIENT;
      case WM_SIZE:
        if (state != nullptr && state->d2d_ready) {
          state->d2d_shell.Resize(static_cast<UINT>(LOWORD(l_param)), static_cast<UINT>(HIWORD(l_param)));
        }
        ApplyPopupMenuRoundedRegion(hwnd);
        return 0;
      case WM_MOUSEMOVE: {
        if (state == nullptr) {
          break;
        }
        const POINT pt{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
        int hot_index = -1;
        for (size_t i = 0; i < state->items.size(); ++i) {
          if (!state->items[i].separator && RectContains(state->items[i].rect, pt)) {
            hot_index = static_cast<int>(i);
            break;
          }
        }
        if (state->hot_index != hot_index) {
          state->hot_index = hot_index;
          InvalidateRect(hwnd, nullptr, FALSE);

          if (state->child) {
            DestroyWindow(state->child->hwnd);
            state->child.reset();
          }

          if (hot_index >= 0) {
            const auto& item = state->items[static_cast<size_t>(hot_index)];
            if (item.submenu && g_popup_menu_session != nullptr) {
              RECT item_rect = item.rect;
              MapWindowPoints(hwnd, HWND_DESKTOP, reinterpret_cast<LPPOINT>(&item_rect), 2);

              auto child = std::make_unique<PopupMenuWindowState>();
              child->owner = state->owner;
              child->parent = state;
              child->items = *item.submenu;

              int max_text_width = 0;
              HDC hdc = GetDC(hwnd);
              HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
              HFONT old_font = static_cast<HFONT>(SelectObject(hdc, font));
              for (const auto& entry : child->items) {
                if (!entry.separator) {
                  SIZE size{};
                  GetTextExtentPoint32W(hdc, entry.text.c_str(), static_cast<int>(entry.text.size()), &size);
                  max_text_width = (std::max)(max_text_width, static_cast<int>(size.cx));
                }
              }
              SelectObject(hdc, old_font);
              ReleaseDC(hwnd, hdc);

              int y = kPopupMenuBorder;
              for (auto& entry : child->items) {
                const int item_height = entry.separator ? kPopupMenuSeparatorHeight : kPopupMenuItemHeight;
                entry.rect = RECT{
                    kPopupMenuBorder,
                    y,
                    kPopupMenuMinWidth + (std::max)(0, max_text_width) + kPopupMenuGutterWidth + kPopupMenuArrowWidth,
                    y + item_height};
                y += item_height;
              }
              child->width =
                  kPopupMenuMinWidth + (std::max)(0, max_text_width) + kPopupMenuGutterWidth + kPopupMenuArrowWidth +
                  kPopupMenuBorder * 2;
              child->height = y + kPopupMenuBorder;
              if (child->width < 180) {
                child->width = 180;
              }

              child->hwnd = CreateWindowExW(
                  WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                  kPopupMenuWindowClassName,
                  L"",
                  WS_POPUP,
                  item_rect.right - 2,
                  item_rect.top,
                  child->width,
                  child->height,
                  state->owner->hwnd(),
                  nullptr,
                  g_popup_menu_session->instance,
                  child.get());
              if (child->hwnd != nullptr) {
                ++g_popup_menu_session->open_window_count;
                ShowWindow(child->hwnd, SW_SHOWNOACTIVATE);
                state->child = std::move(child);
              }
            }
          }
        }
        return 0;
      }
      case WM_LBUTTONUP: {
        if (state == nullptr || g_popup_menu_session == nullptr) {
          break;
        }
        const POINT pt{GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param)};
        for (const auto& item : state->items) {
          if (item.separator || !item.enabled || !RectContains(item.rect, pt)) {
            continue;
          }
          if (item.submenu != nullptr) {
            return 0;
          }
          g_popup_menu_session->selected_command = static_cast<int>(item.id);
          if (g_popup_menu_session->root && g_popup_menu_session->root->hwnd != nullptr) {
            PostMessageW(g_popup_menu_session->root->hwnd, WM_CLOSE, 0, 0);
          }
          return 0;
        }
        return 0;
      }
      case WM_PAINT: {
        if (state == nullptr) {
          break;
        }
        PAINTSTRUCT ps{};
        BeginPaint(hwnd, &ps);
        if (state->d2d_ready) {
          if (auto* target = state->d2d_shell.BeginFrame()) {
            target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            RECT client{};
            GetClientRect(hwnd, &client);
            const D2D1_RECT_F client_rect = D2D1::RectF(
                static_cast<FLOAT>(client.left),
                static_cast<FLOAT>(client.top),
                static_cast<FLOAT>(client.right),
                static_cast<FLOAT>(client.bottom));
            target->Clear(D2D1::ColorF(0xFFFFFF));
            target->FillRectangle(client_rect, state->d2d_shell.surface_brush());
            const D2D1_RECT_F border_rect = D2D1::RectF(
                client_rect.left + 0.5f,
                client_rect.top + 0.5f,
                client_rect.right - 0.5f,
                client_rect.bottom - 0.5f);
            target->DrawRectangle(border_rect, state->d2d_shell.border_brush(), 1.0f);

            for (size_t i = 0; i < state->items.size(); ++i) {
              const auto& item = state->items[i];
              const D2D1_RECT_F item_rect = D2D1::RectF(
                  static_cast<FLOAT>(item.rect.left),
                  static_cast<FLOAT>(item.rect.top),
                  static_cast<FLOAT>(item.rect.right),
                  static_cast<FLOAT>(item.rect.bottom));
              if (item.separator) {
                const FLOAT y = (item_rect.top + item_rect.bottom) * 0.5f;
                target->DrawLine(
                    D2D1::Point2F(item_rect.left + 8.0f, y),
                    D2D1::Point2F(item_rect.right - 8.0f, y),
                    state->d2d_shell.border_brush(),
                    1.0f);
                continue;
              }

              const bool hot = static_cast<int>(i) == state->hot_index;
              if (hot) {
                target->FillRectangle(item_rect, state->d2d_shell.accent_brush());
              }

              ID2D1SolidColorBrush* text_brush = nullptr;
              if (!item.enabled) {
                text_brush = state->d2d_shell.subtle_text_brush();
              } else if (hot) {
                text_brush = state->d2d_shell.white_brush();
              } else {
                text_brush = state->d2d_shell.text_brush();
              }

              const D2D1_RECT_F text_rect = D2D1::RectF(
                  item_rect.left + static_cast<FLOAT>(kPopupMenuGutterWidth + kPopupMenuHorizontalPadding),
                  item_rect.top,
                  item_rect.right - static_cast<FLOAT>(kPopupMenuArrowWidth),
                  item_rect.bottom);
              target->DrawTextW(
                  item.text.c_str(),
                  static_cast<UINT32>(item.text.size()),
                  state->text_format ? state->text_format.Get() : state->d2d_shell.small_format(),
                  text_rect,
                  text_brush,
                  D2D1_DRAW_TEXT_OPTIONS_CLIP,
                  DWRITE_MEASURING_MODE_NATURAL);

              if (item.checked) {
                const D2D1_RECT_F check_rect = D2D1::RectF(
                    item_rect.left + 4.0f,
                    item_rect.top,
                    item_rect.left + static_cast<FLOAT>(kPopupMenuGutterWidth),
                    item_rect.bottom);
                target->DrawTextW(
                    L"*",
                    1,
                    state->symbol_format ? state->symbol_format.Get() : state->d2d_shell.small_format(),
                    check_rect,
                    text_brush,
                    D2D1_DRAW_TEXT_OPTIONS_CLIP,
                    DWRITE_MEASURING_MODE_NATURAL);
              }
              if (item.submenu) {
                const D2D1_RECT_F arrow_rect = D2D1::RectF(
                    item_rect.right - static_cast<FLOAT>(kPopupMenuArrowWidth + 6),
                    item_rect.top,
                    item_rect.right - 6.0f,
                    item_rect.bottom);
                target->DrawTextW(
                    L">",
                    1,
                    state->symbol_format ? state->symbol_format.Get() : state->d2d_shell.small_format(),
                    arrow_rect,
                    text_brush,
                    D2D1_DRAW_TEXT_OPTIONS_CLIP,
                    DWRITE_MEASURING_MODE_NATURAL);
              }
            }
            state->d2d_shell.EndFrame();
          }
        }
        EndPaint(hwnd, &ps);
        return 0;
      }
      case WM_CLOSE:
        if (state != nullptr && state->child) {
          DestroyWindow(state->child->hwnd);
          state->child.reset();
        }
        DestroyWindow(hwnd);
        return 0;
      case WM_DESTROY:
        if (state != nullptr && state->d2d_ready) {
          state->text_format.Reset();
          state->symbol_format.Reset();
          state->d2d_shell.Shutdown();
          state->d2d_ready = false;
        }
        if (g_popup_menu_session != nullptr) {
          g_popup_menu_session->open_window_count = std::max(0, g_popup_menu_session->open_window_count - 1);
          if (g_popup_menu_session->open_window_count == 0) {
            g_popup_menu_session->running = false;
          }
        }
        return 0;
      case WM_NCDESTROY:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return 0;
    }
    return DefWindowProcW(hwnd, message, w_param, l_param);
  };
  wc.hInstance = instance;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.hbrBackground = nullptr;
  wc.lpszClassName = kPopupMenuWindowClassName;
  return RegisterClassExW(&wc) != 0;
}

bool BuildPopupMenuWindowItems(HWND reference_hwnd, const std::vector<PopupMenuItemData>& source_items, std::vector<PopupMenuItemData>* items, int* width, int* height) {
  if (items == nullptr || width == nullptr || height == nullptr) {
    return false;
  }
  items->clear();
  *width = 0;
  *height = 0;

  int max_text_width = 0;
  HDC hdc = GetDC(reference_hwnd);
  HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
  HFONT old_font = static_cast<HFONT>(SelectObject(hdc, font));
  for (const auto& source_item : source_items) {
    PopupMenuItemData entry = source_item;
    if (!entry.separator) {
      SIZE size{};
      GetTextExtentPoint32W(hdc, entry.text.c_str(), static_cast<int>(entry.text.size()), &size);
      max_text_width = (std::max)(max_text_width, static_cast<int>(size.cx));
    }
    items->push_back(std::move(entry));
  }
  SelectObject(hdc, old_font);
  ReleaseDC(reference_hwnd, hdc);

  const int content_width = (std::max)(kPopupMenuMinWidth, max_text_width + kPopupMenuHorizontalPadding * 2);
  int y = kPopupMenuBorder;
  for (auto& entry : *items) {
    const int item_height = entry.separator ? kPopupMenuSeparatorHeight : kPopupMenuItemHeight;
    entry.rect = RECT{
        kPopupMenuBorder,
        y,
        kPopupMenuBorder + kPopupMenuGutterWidth + content_width + kPopupMenuArrowWidth,
        y + item_height};
    y += item_height;
  }
  *width = kPopupMenuBorder * 2 + kPopupMenuGutterWidth + content_width + kPopupMenuArrowWidth;
  *height = y + kPopupMenuBorder;
  return true;
}

}  // namespace

int MainWindow::GetItemHeight() const {
  return controller_ != nullptr && controller_->current_settings().list_density == core::store::ListDensity::Compact ? 84 : 96;
}

int MainWindow::GetTotalContentHeight() const {
  return viewport_.TotalHeight();
}

int MainWindow::GetCollapsedWidth() const {
  return window_width_;
}

int MainWindow::GetExpandedWidth() const {
  return window_width_;
}

int MainWindow::GetDrawerWidth() const {
  return 0;
}

const core::store::ClipSummary* MainWindow::GetSummaryAt(int index) {
  if (index < 0 || index >= item_count_) {
    return nullptr;
  }
  const int page = index / page_size_;
  const int offset = index % page_size_;
  auto it = page_cache_.find(page);
  if (it == page_cache_.end()) {
    RequestPageAsync(page);
    return nullptr;
  }
  TouchPage(page);
  if (offset < 0 || offset >= static_cast<int>(it->second.size())) {
    return nullptr;
  }
  return &it->second[static_cast<size_t>(offset)];
}

const core::store::ClipSummary* MainWindow::GetSelectedSummary() const {
  if (selected_index_ < 0 || selected_index_ >= item_count_) {
    return nullptr;
  }
  const int page = selected_index_ / page_size_;
  const int offset = selected_index_ % page_size_;
  auto it = page_cache_.find(page);
  if (it == page_cache_.end()) {
    return nullptr;
  }
  if (offset < 0 || offset >= static_cast<int>(it->second.size())) {
    return nullptr;
  }
  return &it->second[static_cast<size_t>(offset)];
}

void MainWindow::SelectIndex(int index, bool open_drawer) {
  if (index < 0 || index >= item_count_) {
    return;
  }
  const int previous_index = selected_index_;
  selected_index_ = index;
  EnsureSelectionVisible();
  LoadSelection();
  if (open_drawer) {
    StartDrawerAnimation(true);
  }
  MarkRowDirty(previous_index);
  MarkRowDirty(selected_index_);
  InvalidateListRegion();
}

void MainWindow::LoadSelection() {
  const auto* summary = GetSummaryAt(selected_index_);
  if (summary != nullptr) {
    RequestSelectedClipAsync(summary->id);
  }
}

void MainWindow::ToggleFavoriteSelected() {
  const auto* summary = GetSelectedSummary();
  if (summary == nullptr || controller_ == nullptr) {
    return;
  }
  const bool favorite = !summary->is_favorite;
  const common::Status status = controller_->ToggleFavorite(summary->id, favorite);
  if (!status.ok()) {
    RefreshStatus(L"Update favorite failed");
    return;
  }
  const int page = selected_index_ / page_size_;
  const int offset = selected_index_ % page_size_;
  auto it = page_cache_.find(page);
  if (it != page_cache_.end() && offset >= 0 && offset < static_cast<int>(it->second.size())) {
    it->second[static_cast<size_t>(offset)].is_favorite = favorite;
  }
  if (selected_clip_.has_value()) {
    selected_clip_->summary.is_favorite = favorite;
  }
  if (current_view_ == core::store::ClipView::Favorites && !favorite) {
    RefreshData(false);
  } else {
    RefreshData(true);
  }
}

void MainWindow::TogglePinnedSelected() {
  const auto* summary = GetSelectedSummary();
  if (summary == nullptr || controller_ == nullptr) {
    return;
  }
  const bool pinned = !summary->is_pinned;
  const common::Status status = controller_->TogglePinned(summary->id, pinned);
  if (!status.ok()) {
    RefreshStatus(L"Update pinned failed");
    return;
  }
  const int page = selected_index_ / page_size_;
  const int offset = selected_index_ % page_size_;
  auto it = page_cache_.find(page);
  if (it != page_cache_.end() && offset >= 0 && offset < static_cast<int>(it->second.size())) {
    it->second[static_cast<size_t>(offset)].is_pinned = pinned;
  }
  if (selected_clip_.has_value()) {
    selected_clip_->summary.is_pinned = pinned;
  }
  if (current_view_ == core::store::ClipView::Pinned && !pinned) {
    RefreshData(false);
  } else {
    RefreshData(true);
  }
}

void MainWindow::DeleteSelected() {
  const auto* summary = GetSelectedSummary();
  if (summary == nullptr || controller_ == nullptr) {
    return;
  }

  std::optional<std::int64_t> replacement_clip_id;
  const int deleted_index = selected_index_;
  if (deleted_index >= 0) {
    if (const auto* next_summary = GetSummaryAt(deleted_index + 1); next_summary != nullptr && next_summary->id != summary->id) {
      replacement_clip_id = next_summary->id;
    } else if (deleted_index > 0) {
      if (const auto* previous_summary = GetSummaryAt(deleted_index - 1); previous_summary != nullptr && previous_summary->id != summary->id) {
        replacement_clip_id = previous_summary->id;
      }
    }
  }

  const common::Status status = controller_->DeleteClip(summary->id);
  if (!status.ok()) {
    RefreshStatus(L"\u5220\u9664\u5931\u8d25");
    return;
  }
  selected_clip_.reset();
  actions_.clear();
  explicit_selected_clip_id_ = replacement_clip_id;
  RefreshData(true);
}

void MainWindow::OpenSelectedFolder() {
  if (!selected_clip_.has_value()) {
    return;
  }
  std::wstring path;
  if (selected_clip_->summary.content_type == core::store::ClipContentType::Image) {
    path = DirectoryFromPath(selected_clip_->storage_path);
  } else if (selected_clip_->summary.content_type == core::store::ClipContentType::Files) {
    path = DirectoryFromPath(FirstFilePathFromClip(*selected_clip_));
  } else {
    path = DirectoryFromPath(selected_clip_->summary.source_app_path);
  }
  OpenDirectoryPath(path);
}

void MainWindow::CopySelectedPath() {
  if (!selected_clip_.has_value()) {
    return;
  }
  std::wstring path;
  if (selected_clip_->summary.content_type == core::store::ClipContentType::Image) {
    path = selected_clip_->storage_path;
  } else if (selected_clip_->summary.content_type == core::store::ClipContentType::Files) {
    path = FirstFilePathFromClip(*selected_clip_);
  }
  if (path.empty()) {
    return;
  }
  SuppressClipboardCapture();
  if (CopyTextToClipboard(hwnd_, path)) {
    RefreshStatus(L"Copied path");
  } else {
    RefreshStatus(L"\u590d\u5236\u8def\u5f84\u5931\u8d25");
  }
}

void MainWindow::OpenSelectedPreview() {
  EnsureSelectedClipLoaded();
  if (!selected_clip_.has_value()) {
    return;
  }
  const common::Status status = EnsurePreviewWindowCreated();
  if (!status.ok()) {
    RefreshStatus(L"\u6253\u5f00\u9884\u89c8\u5931\u8d25");
    return;
  }
  preview_window_.ShowClip(*selected_clip_, BuildPreviewHostActions());
}

void MainWindow::OpenSelectedWithDefaultApp() {
  EnsureSelectedClipLoaded();
  if (!selected_clip_.has_value()) {
    return;
  }

  std::wstring path;
  if (selected_clip_->summary.content_type == core::store::ClipContentType::Files) {
    path = FirstFilePathFromClip(*selected_clip_);
  } else if (selected_clip_->summary.content_type == core::store::ClipContentType::Image) {
    if (!selected_clip_->secondary_path.empty()) {
      path = selected_clip_->secondary_path;
    } else {
      path = selected_clip_->storage_path;
    }
  }

  if (path.empty()) {
    RefreshStatus(L"\u6ca1\u6709\u53ef\u6253\u5f00\u7684\u8def\u5f84");
    return;
  }

  const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
  if (result <= 32) {
    RefreshStatus(L"\u6253\u5f00\u5931\u8d25");
  }
}

void MainWindow::OpenSelectedResolvedTarget() {
  EnsureSelectedClipLoaded();
  if (!selected_clip_.has_value()) {
    return;
  }

  const core::clipboard::OpenTarget target =
      core::clipboard::ResolveOpenTargetFromText(selected_clip_->plain_text);
  if (target.kind == core::clipboard::OpenTargetKind::None || target.value.empty()) {
    RefreshStatus(L"No resolvable target");
    return;
  }

  const auto result =
      reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", target.value.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
  if (result <= 32) {
    RefreshStatus(L"\u6253\u5f00\u5931\u8d25");
  }
}

void MainWindow::PerformThirdPartySticker() {
  const auto* summary = GetSelectedSummary();
  if (summary == nullptr || controller_ == nullptr) {
    return;
  }

  if (!third_party_sticker_enabled_ || !third_party_sticker_hotkey_valid_) {
    RefreshStatus(L"\u8bf7\u5148\u5728\u8bbe\u7f6e\u4e2d\u542f\u7528\u5e76\u914d\u7f6e\u7b2c\u4e09\u65b9\u8d34\u56fe\u5feb\u6377\u952e");
    return;
  }
  if (!ShouldShowThirdPartyStickerButton(*summary)) {
    RefreshStatus(L"\u5f53\u524d\u6761\u76ee\u4e0d\u652f\u6301\u8d34\u56fe");
    return;
  }

  const common::Status copy_status = controller_->CopyClipForStickerById(summary->id);
  if (!copy_status.ok()) {
    RefreshStatus(L"\u51c6\u5907\u8d34\u56fe\u526a\u8d34\u677f\u5931\u8d25");
    return;
  }

  const common::Status hotkey_status = platform::win32::SendHotkeyInput(third_party_sticker_hotkey_);
  if (!hotkey_status.ok()) {
    RefreshStatus(L"\u89e6\u53d1\u7b2c\u4e09\u65b9\u8d34\u56fe\u5feb\u6377\u952e\u5931\u8d25");
    return;
  }
}

void MainWindow::QueuePasteRequest(PendingPasteKind kind, std::int64_t clip_id, std::string action_id) {
  if (controller_ == nullptr || clip_id <= 0) {
    return;
  }

  pending_paste_request_.active = true;
  pending_paste_request_.kind = kind;
  pending_paste_request_.clip_id = clip_id;
  pending_paste_request_.action_id = std::move(action_id);
  pending_paste_request_.target_context =
      core::clipboard::IsValidPasteTargetContext(focus_coordinator_.paste_target_context())
          ? focus_coordinator_.paste_target_context()
          : CapturePasteTargetContext();
  pending_paste_request_.route = paste_route_requires_recovery_ ? core::clipboard::PasteRoute::Recovery
                                                                : core::clipboard::PasteRoute::Fast;
  common::WriteLog(
      common::LogLevel::Info,
      "ui.paste_target",
      "queue kind=" + std::to_string(static_cast<int>(kind)) +
          " clip_id=" + std::to_string(clip_id) +
          " route=" + std::to_string(static_cast<int>(pending_paste_request_.route)) +
          " stored_root=" + std::to_string(reinterpret_cast<std::uintptr_t>(pending_paste_request_.target_context.root_window)) +
          " stored_focus=" + std::to_string(reinterpret_cast<std::uintptr_t>(pending_paste_request_.target_context.focus_window)));

  if (!panel_locked_) {
    HideWindow();
  }
  PostMessageW(hwnd_, kCommitPasteMessage, 0, 0);
}

void MainWindow::CommitPendingPasteRequest() {
  if (!pending_paste_request_.active || controller_ == nullptr) {
    return;
  }

  PendingPasteRequest request = std::move(pending_paste_request_);
  pending_paste_request_ = {};

  const HWND foreground = GetForegroundWindow();
  core::clipboard::PasteTargetContext current_external_context{};
  if (foreground != nullptr && !IsOwnedWindow(foreground)) {
    current_external_context = core::clipboard::CapturePasteTargetContext(foreground);
    if (core::clipboard::IsValidPasteTargetContext(current_external_context)) {
      focus_coordinator_.ObserveExternalWindow(
          foreground,
          [this](HWND hwnd) { return IsOwnedWindow(hwnd); },
          [this](HWND hwnd) { return IsMenuPopupWindow(hwnd); });
    }
  }

  core::clipboard::PasteTargetContext target_context{};
  core::clipboard::PasteRoute route = core::clipboard::PasteRoute::Fast;
  if (core::clipboard::IsValidPasteTargetContext(current_external_context)) {
    target_context = current_external_context;
  } else if (core::clipboard::IsValidPasteTargetContext(request.target_context)) {
    target_context = request.target_context;
    route = core::clipboard::PasteRoute::Recovery;
  } else {
    target_context = CapturePasteTargetContext();
    route = core::clipboard::PasteRoute::Recovery;
  }
  common::WriteLog(
      common::LogLevel::Info,
      "ui.paste_target",
      "commit kind=" + std::to_string(static_cast<int>(request.kind)) +
          " clip_id=" + std::to_string(request.clip_id) +
          " foreground=" + std::to_string(reinterpret_cast<std::uintptr_t>(foreground)) +
          " current_root=" + std::to_string(reinterpret_cast<std::uintptr_t>(current_external_context.root_window)) +
          " request_root=" + std::to_string(reinterpret_cast<std::uintptr_t>(request.target_context.root_window)) +
          " final_root=" + std::to_string(reinterpret_cast<std::uintptr_t>(target_context.root_window)) +
          " final_focus=" + std::to_string(reinterpret_cast<std::uintptr_t>(target_context.focus_window)) +
          " route=" + std::to_string(static_cast<int>(route)));

  common::Status status = common::Status::Ok();
  switch (request.kind) {
    case PendingPasteKind::Default:
      status = controller_->PasteById(request.clip_id, target_context, route);
      break;
    case PendingPasteKind::PlainText:
      status = controller_->PastePlainTextById(request.clip_id, target_context, route);
      break;
    case PendingPasteKind::Action:
      status = controller_->TransformPasteById(request.clip_id, request.action_id, target_context, route);
      break;
  }

  if (status.ok()) {
    return;
  }

  if (!panel_locked_ && !IsWindowVisible(hwnd_)) {
    ShowAndActivate();
  }
  RefreshStatus(L"\u7c98\u8d34\u5931\u8d25");
}

core::clipboard::PasteTargetContext MainWindow::CapturePasteTargetContext() const {
  return focus_coordinator_.CapturePreferredPasteTarget(
      GetForegroundWindow(),
      [this](HWND hwnd) { return IsOwnedWindow(hwnd); },
      [this](HWND hwnd) { return IsMenuPopupWindow(hwnd); });
}

void MainWindow::PerformDefaultPaste() {
  const auto* summary = GetSelectedSummary();
  if (summary == nullptr || controller_ == nullptr) {
    return;
  }
  QueuePasteRequest(PendingPasteKind::Default, summary->id);
}

void MainWindow::PerformPlainTextPaste() {
  const auto* summary = GetSelectedSummary();
  if (summary == nullptr || controller_ == nullptr) {
    return;
  }
  QueuePasteRequest(PendingPasteKind::PlainText, summary->id);
}

void MainWindow::PerformActionById(const std::string& action_id) {
  const auto* summary = GetSelectedSummary();
  if (summary == nullptr || controller_ == nullptr) {
    return;
  }

  if (action_id == "paste-original") {
    QueuePasteRequest(PendingPasteKind::Default, summary->id);
  } else if (action_id == "paste-plain-text" || action_id == "paste-path") {
    QueuePasteRequest(PendingPasteKind::PlainText, summary->id);
  } else if (action_id == "open-target") {
    OpenSelectedResolvedTarget();
    return;
  } else if (action_id == "show-qr") {
    const core::clipboard::OpenTarget target =
        core::clipboard::ResolveOpenTargetFromText(selected_clip_->plain_text);
    if (target.kind != core::clipboard::OpenTargetKind::Url || target.value.empty() || controller_ == nullptr) {
      RefreshStatus(L"Current item is not a QR-capable link");
      return;
    }
    std::wstring qr_path;
    const common::Status qr_status = controller_->GenerateQrCodeForUrl(target.value, &qr_path);
    if (!qr_status.ok()) {
      RefreshStatus(L"Generate QR code failed");
      return;
    }
    const common::Status preview_status = EnsurePreviewWindowCreated();
    if (!preview_status.ok()) {
      RefreshStatus(L"Open QR preview failed");
      return;
    }
    core::store::ClipRecord qr_clip;
    qr_clip.summary.content_type = core::store::ClipContentType::Image;
    qr_clip.summary.title = "Link QR";
    qr_clip.storage_path = qr_path;
    qr_clip.summary.storage_path = qr_path;
    qr_clip.plain_text = selected_clip_->plain_text;
    PreviewWindow::HostActions host_actions;
    host_actions.on_show = [this]() {
      NotifyPreviewWindowShown();
    };
    host_actions.on_hide = [this]() {
      NotifyPreviewWindowHidden();
    };
    host_actions.enable_image_ocr = false;
    host_actions.image_copy_label = L"Copy QR";
    host_actions.on_copy_image = [qr_path]() {
      SuppressClipboardCapture();
      return core::clipboard::SetClipboardImageFromPng(qr_path);
    };
    preview_window_.ShowClip(qr_clip, std::move(host_actions));
    return;
  } else if (action_id == "open-default") {
    OpenSelectedWithDefaultApp();
    return;
  } else if (action_id == "copy-path") {
    CopySelectedPath();
    return;
  } else if (action_id == "open-folder") {
    OpenSelectedFolder();
    return;
  } else {
    QueuePasteRequest(PendingPasteKind::Action, summary->id, action_id);
  }
}

void MainWindow::RefreshStatus(const std::wstring& text) {
  status_text_ = text;
  InvalidateRect(hwnd_, nullptr, FALSE);
}



bool MainWindow::ShouldShowTimeGroupsInCurrentState() const {
  return browser_service_.BuildPresentation(BuildBrowserState()).show_time_groups;
}

std::optional<MainWindow::HitRegion> MainWindow::ResolveStaticHit(POINT point) const {
  if (hwnd_ == nullptr) {
    return std::nullopt;
  }

  RECT client{};
  GetClientRect(hwnd_, &client);
  const PanelLayout layout = ComputeLayout(client.right, client.bottom);
  const bool has_search_text = !search_textbox_.GetText().empty();
  if (has_search_text && RectContains(SearchClearButtonRect(layout), point)) {
    return HitRegion{SearchClearButtonRect(layout), HitKind::SearchClear, 0};
  }
  for (int index = 0; index < static_cast<int>(std::size(layout.toolbar)); ++index) {
    if (RectContains(layout.toolbar[index], point)) {
      return HitRegion{layout.toolbar[index], HitKind::ToolbarButton, index};
    }
  }
  for (int index = 0; index < static_cast<int>(std::size(kVisibleTabs)); ++index) {
    if (RectContains(layout.tabs[index], point)) {
      return HitRegion{layout.tabs[index], HitKind::Tab, index};
    }
  }
  for (int index = 0; index < static_cast<int>(std::size(layout.nav_actions)); ++index) {
    if (RectContains(layout.nav_actions[index], point)) {
      return HitRegion{layout.nav_actions[index], HitKind::NavButton, index};
    }
  }
  if (ShouldShowBackToTopButton(layout.list) && RectContains(layout.back_to_top, point)) {
    return HitRegion{layout.back_to_top, HitKind::BackToTop, 0};
  }
  return std::nullopt;
}

bool MainWindow::IsPressedStaticHit(HitKind kind, int index) const {
  return pressed_static_hit_.has_value() &&
      pressed_static_hit_->inside &&
      pressed_static_hit_->hit.kind == kind &&
      pressed_static_hit_->hit.index == index;
}

bool MainWindow::ShouldShowBackToTopButton(const RECT& list_rect) const {
  const int viewport = std::max(1L, list_rect.bottom - list_rect.top);
  if (scroll_controller_->CurrentScrollOffset() <= viewport) {
    return false;
  }
  const RECT reveal_rect{
      list_rect.left,
      list_rect.top,
      list_rect.right,
      std::min(list_rect.bottom, list_rect.top + kBackToTopRevealHeight)};
  return scroll_controller_->GetManualScrollDirection() < 0 || RectContains(reveal_rect, hover_point_);
}

const core::store::ClipSummary* MainWindow::GetBottomTimeSummary(const RECT& list_rect) {
  if (!ShouldShowTimeGroupsInCurrentState() || item_count_ <= 0) {
    return nullptr;
  }

  const int viewport = std::max(1L, list_rect.bottom - list_rect.top);
  const float visual_scroll_offset = scroll_controller_->VisualScrollOffset();
  const int first_visible_index = std::clamp(
      viewport_.IndexAtOffset(static_cast<int>(visual_scroll_offset)),
      0,
      std::max(0, item_count_ - 1));
  const int last_visible_index = std::clamp(
      viewport_.IndexAtOffset(
          static_cast<int>(visual_scroll_offset) + std::max(1, viewport) - 1),
      0,
      std::max(0, item_count_ - 1));
  if (const auto* summary = GetSummaryAt(last_visible_index)) {
    return summary;
  }
  if (last_visible_index != first_visible_index) {
    return GetSummaryAt(first_visible_index);
  }
  return nullptr;
}





void MainWindow::EnsureAnimationTimer() {
  // L6:统一经 has_pending_work() 判断是否需要下一帧(硬规则 3),替代原先
  // 各动画启动点各自为战的条件(滚动/拖拽/骨架 shimmer/加载在途)。
  if (hwnd_ == nullptr || !has_pending_work()) {
    return;
  }
  if (animation_timer_active_) {
    return;
  }
  UINT interval = frame_interval_ms_;
  if (interval == 0) {
    interval = 8;
  }
  if (row_drag_active_ || row_reorder_animation_active_) {
    interval = std::min<UINT>(interval, 3);
  } else if (scroll_controller_->IsListScrollAnimating()) {
    interval = std::min<UINT>(interval, 5);
  }
  if (SetTimer(hwnd_, kAnimationTimerId, interval, nullptr)) {
    animation_timer_active_ = true;
  }
}

void MainWindow::OnMouseMove(POINT point) {
  if (IsOverlayDialogVisible()) {
    hover_point_ = point;
    if (!mouse_tracking_) {
      TRACKMOUSEEVENT track{};
      track.cbSize = sizeof(track);
      track.dwFlags = TME_LEAVE;
      track.hwndTrack = hwnd_;
      TrackMouseEvent(&track);
      mouse_tracking_ = true;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
    return;
  }
  if (pressed_static_hit_.has_value()) {
    bool inside = false;
    if (const auto static_hit = ResolveStaticHit(point); static_hit.has_value()) {
      inside = static_hit->kind == pressed_static_hit_->hit.kind &&
          static_hit->index == pressed_static_hit_->hit.index;
    }
    if (inside != pressed_static_hit_->inside) {
      pressed_static_hit_->inside = inside;
      InvalidateRect(hwnd_, nullptr, FALSE);
    }
  }
  if (row_drag_active_) {
    RECT client{};
    GetClientRect(hwnd_, &client);
    if (!RectContains(client, point) && TryStartExternalDrag(row_drag_item_index_)) {
      return;
    }
    hover_point_ = point;
    UpdateRowDrag(point);
    return;
  }
  if (MaybeStartRowDrag(point)) {
    return;
  }

  auto resolve_hover_token = [&](POINT probe) -> std::int64_t {
    if (const auto hit = ResolveStaticHit(probe)) {
      return (static_cast<std::int64_t>(static_cast<int>(hit->kind)) << 32) |
          static_cast<std::uint32_t>(hit->index + 1);
    }
    for (auto it = hit_regions_.rbegin(); it != hit_regions_.rend(); ++it) {
      const auto& hit = *it;
      if (RectContains(hit.rect, probe)) {
        return (static_cast<std::int64_t>(static_cast<int>(hit.kind)) << 32) |
            static_cast<std::uint32_t>(hit.index + 1);
      }
    }
    RECT client{};
    GetClientRect(hwnd_, &client);
    const PanelLayout layout = ComputeLayout(client.right, client.bottom);
    const int viewport = std::max(1, static_cast<int>(layout.list.bottom - layout.list.top));
    if (GetTotalContentHeight() > viewport &&
        RectContains(ScrollbarHitRect(layout.scrollbar_track, drag_scrollbar_ || RectContains(layout.list, probe)), probe)) {
      return -2;
    }
    return -1;
  };

  const std::int64_t previous_token = resolve_hover_token(hover_point_);
  const int previous_row = RowIndexAtPoint(hover_point_);
  hover_point_ = point;
  const int current_row = RowIndexAtPoint(point);
  if (!mouse_tracking_) {
    TRACKMOUSEEVENT track{};
    track.cbSize = sizeof(track);
    track.dwFlags = TME_LEAVE;
    track.hwndTrack = hwnd_;
    TrackMouseEvent(&track);
    mouse_tracking_ = true;
  }
  if (drag_scrollbar_) {
    RECT client{};
    GetClientRect(hwnd_, &client);
    const PanelLayout layout = ComputeLayout(client.right, client.bottom);
    const int viewport = std::max(1, static_cast<int>(layout.list.bottom - layout.list.top));
    const int content_height = GetTotalContentHeight();
    if (content_height > viewport) {
      const float track_top = static_cast<float>(layout.scrollbar_track.top);
      const float track_height = static_cast<float>(layout.scrollbar_track.bottom - layout.scrollbar_track.top);
      const float thumb_height = std::max(44.0f, track_height * (static_cast<float>(viewport) / static_cast<float>(content_height)));
      const float travel = std::max(1.0f, track_height - thumb_height);
      const float centered = static_cast<float>(point.y) - track_top - thumb_height * 0.5f;
      const float ratio = std::clamp(centered / travel, 0.0f, 1.0f);
      const int next_offset = static_cast<int>(std::round(ratio * std::max(0, content_height - viewport)));
      if (next_offset != scroll_controller_->GetTargetScrollOffset()) {
        scroll_controller_->GetManualScrollDirection() = next_offset < scroll_controller_->GetTargetScrollOffset() ? -1 : 1;
      }
      scroll_controller_->SetScrollOffsetTarget(next_offset, true);
      scroll_controller_->SetShowScrollbarUntilMs(NowEpochMs() + 1200);
      ::SetTimer(hwnd_, kDragDecayTimerId, 80, nullptr);
    }
    // Fetch the newly visible pages right away instead of waiting for a
    // skeleton frame to trigger the animation timer. Thumbnails stay off —
    // they would churn the thumbnail request epoch on every mouse move.
    RequestVisiblePages(false);
    InvalidateListRegion();
    HideHoverTooltip();
    return;
  }
  if (resolve_hover_token(point) != previous_token) {
    const bool previous_in_list = previous_row >= 0 || previous_token == -2;
    const bool current_in_list = current_row >= 0 || resolve_hover_token(point) == -2;
    if (previous_in_list || current_in_list) {
      if (previous_row >= 0) {
        MarkRowDirty(previous_row);
      }
      if (current_row >= 0) {
        MarkRowDirty(current_row);
      }
      last_hovered_list_index_ = current_row;
      InvalidateListRegion();
      UpdateHoverTooltip(point);
      return;
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
  }
  UpdateHoverTooltip(point);
}

void MainWindow::OnLeftButtonDown(POINT point, bool double_click) {
  (void)double_click;
  if (IsOverlayDialogVisible()) {
    armed_click_item_index_ = -1;
    row_drag_candidate_active_ = false;
    pressed_static_hit_.reset();
    if (overlay_dialog_.has_text_input && RectContains(overlay_dialog_.input_rect, point)) {
      FocusOverlayDialogInput(false);
      return;
    }
    for (auto it = hit_regions_.rbegin(); it != hit_regions_.rend(); ++it) {
      if (RectContains(it->rect, point) &&
          (it->kind == HitKind::DialogConfirm || it->kind == HitKind::DialogCancel || it->kind == HitKind::DialogColorChip)) {
        PerformHitAction(*it);
        return;
      }
    }
    InvalidateRect(hwnd_, nullptr, FALSE);
    return;
  }
  if (scroll_controller_->IsInertialScrollActive()) {
    scroll_controller_->StopInertialScroll(false);
    InvalidateListRegion();
  }
  armed_click_item_index_ = -1;
  row_drag_candidate_active_ = false;
  pressed_static_hit_.reset();
  RECT client{};
  GetClientRect(hwnd_, &client);
  const PanelLayout layout = ComputeLayout(client.right, client.bottom);
  if (const auto static_hit = ResolveStaticHit(point)) {
    pressed_static_hit_ = StaticPressState{*static_hit, true};
    SetCapture(hwnd_);
    InvalidateRect(hwnd_, nullptr, FALSE);
    return;
  }
  if (RectContains(layout.search_box, point)) {
    FocusSearchBox(false);
    return;
  }
  const int viewport = std::max(1, static_cast<int>(layout.list.bottom - layout.list.top));
  const int content_height = GetTotalContentHeight();
  if (content_height > viewport) {
    const float track_top = static_cast<float>(layout.scrollbar_track.top);
    const float track_height = static_cast<float>(layout.scrollbar_track.bottom - layout.scrollbar_track.top);
    const float thumb_height = std::max(44.0f, track_height * (static_cast<float>(viewport) / static_cast<float>(content_height)));
    const float travel = std::max(1.0f, track_height - thumb_height);
    const float ratio = content_height > viewport ? scroll_controller_->VisualScrollOffset() / static_cast<float>(content_height - viewport) : 0.0f;
    RECT thumb_rect{
        layout.scrollbar_track.left,
        static_cast<int>(std::round(track_top + travel * ratio)),
        layout.scrollbar_track.right,
        static_cast<int>(std::round(track_top + travel * ratio + thumb_height))};
    if ((scroll_controller_->ShouldShowScrollThumb() && RectContains(ScrollbarHitRect(thumb_rect, true), point)) ||
        RectContains(ScrollbarHitRect(layout.scrollbar_track, true), point)) {
      drag_scrollbar_ = true;
      SetCapture(hwnd_);
      OnMouseMove(point);
      return;
    }
  }
  if (TryPerformRowActionAtPoint(point)) {
    return;
  }
  for (auto it = hit_regions_.rbegin(); it != hit_regions_.rend(); ++it) {
    if (RectContains(it->rect, point)) {
      HitRegion hit = *it;
      if (double_click && hit.kind == HitKind::Item) {
        hit.index = -hit.index - 1;
      }
      if (PerformHitAction(hit)) {
        if (!double_click && hit.kind == HitKind::Item) {
          const int item_index = hit.index < 0 ? (-hit.index - 1) : hit.index;
          ArmRowDrag(point, item_index);
        }
        return;
      }
    }
  }
  UpdateHoverTooltip(point);
}

void MainWindow::OnRightButtonUp(POINT point) {
  if (IsOverlayDialogVisible()) {
    return;
  }
  for (auto it = hit_regions_.rbegin(); it != hit_regions_.rend(); ++it) {
    if ((it->kind == HitKind::Item || it->kind == HitKind::ItemSticker || it->kind == HitKind::ItemFavorite || it->kind == HitKind::ItemPinned ||
         it->kind == HitKind::ItemDelete) &&
        RectContains(it->rect, point)) {
      SelectIndex(it->index, false);
      POINT screen_point = point;
      ClientToScreen(hwnd_, &screen_point);
      OpenSelectedContextMenu(screen_point);
      return;
    }
  }
}

void MainWindow::OnMouseWheel(short delta) {
  if (IsOverlayDialogVisible()) {
    return;
  }
  RECT client{};
  GetClientRect(hwnd_, &client);
  const PanelLayout layout = ComputeLayout(client.right, client.bottom);
  if (!RectContains(layout.list, hover_point_)) {
    return;
  }
  scroll_controller_->OnMouseWheel(delta, NowEpochMs());
}

void MainWindow::OnKeyboardNavigate(UINT virtual_key, bool shift_down) {
  if (IsOverlayDialogVisible()) {
    switch (virtual_key) {
      case VK_ESCAPE:
        CloseOverlayDialog(true);
        return;
      case VK_RETURN:
        CommitOverlayDialog();
        return;
      default:
        return;
    }
  }
  switch (virtual_key) {
    case VK_ESCAPE:
      if (search_textbox_.IsFocused()) {
        if (!search_textbox_.GetText().empty()) {
          ClearSearch();
          return;
        }
        search_focus_requested_ = false;
        SetPanelAllowsActivation(false);
        search_textbox_.SetFocused(false, hwnd_);
        RestoreExternalFocusRestoreContext();
        InvalidateRect(hwnd_, nullptr, FALSE);
        return;
      }
      HideWindow();
      return;
    case VK_RETURN:
      if (shift_down) {
        PerformPlainTextPaste();
      } else {
        PerformDefaultPaste();
      }
      return;
    case VK_SPACE:
      OpenSelectedPreview();
      return;
    case VK_UP:
      if (selected_index_ > 0) {
        SelectIndex(selected_index_ - 1, false);
      }
      return;
    case VK_DOWN:
      if (selected_index_ + 1 < item_count_) {
        SelectIndex(selected_index_ + 1, false);
      }
      return;
    case VK_PRIOR:
      if (selected_index_ >= 0) {
        SelectIndex(std::max(0, selected_index_ - 6), false);
      }
      return;
    case VK_NEXT:
      if (selected_index_ >= 0) {
        SelectIndex(std::min(item_count_ - 1, selected_index_ + 6), false);
      }
      return;
    case VK_HOME:
      if (item_count_ > 0) {
        SelectIndex(0, false);
      }
      return;
    case VK_END:
      if (item_count_ > 0) {
        SelectIndex(item_count_ - 1, false);
      }
      return;
    case VK_DELETE:
      DeleteSelected();
      return;
  }
}

bool MainWindow::TryPerformRowActionAtPoint(POINT point) {
  if (hwnd_ == nullptr) {
    return false;
  }
  const core::store::AppSettings render_settings =
      controller_ != nullptr ? controller_->current_settings() : core::store::AppSettings{};

  RECT client{};
  GetClientRect(hwnd_, &client);
  const PanelLayout layout = ComputeLayout(client.right, client.bottom);
  if (!RectContains(layout.list, point)) {
    return false;
  }

  const float visual_scroll_offset = scroll_controller_->VisualScrollOffset();
  const int list_height = layout.list.bottom - layout.list.top;
  const int item_height = GetItemHeight();
  const vlist::VisibleRange visible = viewport_.RangeAt(
      visual_scroll_offset, list_height, item_height, item_height);
  const int first_index = std::max(0, visible.first - 1);
  const int last_index = std::min(item_count_ - 1, visible.last + 1);
  for (int index = first_index; index <= last_index; ++index) {
    const vlist::RowRect row_pos = vlist::RowRectFor(
        viewport_.heights(), index, static_cast<float>(layout.list.top), visual_scroll_offset);
    RECT row{
        layout.list.left,
        static_cast<LONG>(std::lround(row_pos.top)),
        layout.list.right,
        static_cast<LONG>(std::lround(row_pos.bottom))};
    if (!RectContains(row, point)) {
      continue;
    }

    const auto* summary = GetSummaryAt(index);
    if (summary == nullptr) {
      return false;
    }

    const RECT header_rect{
        row.left + kRowPaddingX,
        row.top + 2,
        row.right - kRowPaddingX,
        std::min(row.bottom - 2, row.top + 2 + kRowHeaderHeight)};
    RECT number_rect{header_rect.right - 22, header_rect.top + 4, header_rect.right, header_rect.bottom - 4};

    int action_right = number_rect.left - 6;
    if (render_settings.show_source_icon) {
      action_right -= (kRowHeaderIconSize + 12);
    }

    const bool show_sticker = ShouldShowThirdPartyStickerButton(*summary);
    const RECT delete_rect{
        action_right - kRowActionButtonSize,
        header_rect.top + (kRowHeaderHeight - kRowActionButtonSize) / 2,
        action_right,
        header_rect.top + (kRowHeaderHeight - kRowActionButtonSize) / 2 + kRowActionButtonSize};
    action_right = delete_rect.left - kRowActionGap;
    const RECT pinned_rect{
        action_right - kRowActionButtonSize,
        header_rect.top + (kRowHeaderHeight - kRowActionButtonSize) / 2,
        action_right,
        header_rect.top + (kRowHeaderHeight - kRowActionButtonSize) / 2 + kRowActionButtonSize};
    action_right = pinned_rect.left - kRowActionGap;
    const RECT favorite_rect{
        action_right - kRowActionButtonSize,
        header_rect.top + (kRowHeaderHeight - kRowActionButtonSize) / 2,
        action_right,
        header_rect.top + (kRowHeaderHeight - kRowActionButtonSize) / 2 + kRowActionButtonSize};
    RECT sticker_rect{};
    if (show_sticker) {
      action_right = favorite_rect.left - kRowActionGap;
      sticker_rect = RECT{
          action_right - kRowActionButtonSize,
          header_rect.top + (kRowHeaderHeight - kRowActionButtonSize) / 2,
          action_right,
          header_rect.top + (kRowHeaderHeight - kRowActionButtonSize) / 2 + kRowActionButtonSize};
    }

    if (show_sticker && RectContains(sticker_rect, point)) {
      SelectIndex(index, false);
      PerformThirdPartySticker();
      return true;
    }

    if (RectContains(favorite_rect, point)) {
      SelectIndex(index, false);
      ToggleFavoriteSelected();
      return true;
    }
    if (RectContains(pinned_rect, point)) {
      SelectIndex(index, false);
      TogglePinnedSelected();
      return true;
    }
    if (RectContains(delete_rect, point)) {
      SelectIndex(index, false);
      DeleteSelected();
      return true;
    }
    return false;
  }

  return false;
}

bool MainWindow::PerformHitAction(const HitRegion& hit) {
  switch (hit.kind) {
    case HitKind::DialogConfirm:
      CommitOverlayDialog();
      return true;
    case HitKind::DialogCancel:
      CloseOverlayDialog(true);
      return true;
    case HitKind::DialogColorChip:
      if (hit.index >= 0 && hit.index < static_cast<int>(overlay_dialog_.color_options.size())) {
        overlay_dialog_.selected_color = overlay_dialog_.color_options[static_cast<size_t>(hit.index)].value;
        InvalidateRect(hwnd_, nullptr, FALSE);
      }
      return true;
    case HitKind::ToolbarButton:
      switch (static_cast<ToolbarCommand>(hit.index)) {
        case ToolbarCommand::PanelLock:
          panel_locked_ = !panel_locked_;
          UpdateAutoHideHooks();
          InvalidateRect(hwnd_, nullptr, FALSE);
          return true;
        case ToolbarCommand::Settings:
          if (open_settings_callback_) {
            open_settings_callback_();
          }
          return true;
        case ToolbarCommand::More:
          if (active_menu_kind_ == PanelMenuKind::More) {
            DismissOwnedPopupMenus();
            return true;
          }
          OpenMoreMenu();
          return true;
      }
      break;

    case HitKind::Tab:
      if (kVisibleTabs[hit.index].opens_menu) {
        if (active_menu_kind_ == PanelMenuKind::Group) {
          DismissOwnedPopupMenus();
          return true;
        }
        OpenGroupMenu();
      } else {
        core::browser::BrowserCommand command;
        command.kind = (kVisibleTabs[hit.index].view == core::store::ClipView::Favorites)
            ? core::browser::BrowserCommandKind::SetScopeFavorites
            : core::browser::BrowserCommandKind::SetScopeHistory;
        ApplyBrowserCommand(command);
        RefreshData(false);
      }
      return true;

    case HitKind::NavButton:
      if (hit.index == 0) {
        ApplyBrowserCommand(core::browser::BrowserCommand{
            core::browser::BrowserCommandKind::SetScopeQueue});
        RefreshData(false);
      } else {
        if (active_menu_kind_ == PanelMenuKind::Filter) {
          DismissOwnedPopupMenus();
          return true;
        }
        OpenFilterMenu();
      }
      return true;
    case HitKind::ClearFilters:
      ApplyBrowserCommand(core::browser::BrowserCommand{
          core::browser::BrowserCommandKind::ClearAllStructuredFilters});
      search_textbox_.SetText(L"");
      ApplyBrowserCommand(core::browser::BrowserCommand{
          core::browser::BrowserCommandKind::ClearSearchText});
      RefreshData(false);
      InvalidateRect(hwnd_, nullptr, FALSE);
      return true;

    case HitKind::Item:
      {
        const bool is_double_click = hit.index < 0;
        const int item_index = is_double_click ? (-hit.index - 1) : hit.index;
        SelectIndex(item_index, false);
        const auto settings = controller_ != nullptr ? controller_->current_settings() : core::store::AppSettings{};
        if (settings.click_paste_mode == core::store::ClickPasteMode::Single) {
          if (!is_double_click) {
            armed_click_item_index_ = item_index;
          }
        } else if (is_double_click) {
          PostMessageW(hwnd_, kInvokeDefaultPasteMessage, 0, 0);
        }
      }
      return true;
    case HitKind::ItemFavorite:
      SelectIndex(hit.index, false);
      ToggleFavoriteSelected();
      return true;
    case HitKind::SearchClear:
      ClearSearch();
      return true;
    case HitKind::ItemSticker:
      SelectIndex(hit.index, false);
      PerformThirdPartySticker();
      return true;
    case HitKind::ItemPinned:
      SelectIndex(hit.index, false);
      TogglePinnedSelected();
      return true;
    case HitKind::ItemDelete:
      SelectIndex(hit.index, false);
      DeleteSelected();
      return true;
    case HitKind::TagChip:
      {
        core::browser::BrowserCommand command;
        command.kind = core::browser::BrowserCommandKind::SelectTag;
        command.tag_id = hit.tag_id;
        ApplyBrowserCommand(command);
      }
      RefreshData(false);
      return true;
    case HitKind::FilterChip: {
      core::browser::FilterChip chip;
      chip.kind = static_cast<core::browser::FilterChipKind>(hit.index);
      chip.id = hit.tag_id;
      chip.removable = true;
      if (const auto command = browser_service_.BuildRemoveChipCommand(BuildBrowserState(), chip); command.has_value()) {
        ApplyBrowserCommand(*command);
        if (chip.kind == core::browser::FilterChipKind::Search) {
          search_textbox_.SetText(L"");
        }
        RefreshData(false);
      }
      return true;
    }
    case HitKind::BottomButton:
      return false;
    case HitKind::BackToTop:
      scroll_controller_->GetManualScrollDirection() = -1;
      scroll_controller_->SetScrollOffsetTarget(0, false);
      InvalidateListRegion();
      return true;
    case HitKind::None:
      break;
  }
  return false;
}

bool MainWindow::HandleOverlayDialogHit(const HitRegion& hit) {
  switch (hit.kind) {
    case HitKind::DialogConfirm:
    case HitKind::DialogCancel:
    case HitKind::DialogColorChip:
      return PerformHitAction(hit);
    default:
      return false;
  }
}

void MainWindow::CommitOverlayDialog() {
  if (!overlay_dialog_.visible || controller_ == nullptr) {
    return;
  }
  SyncOverlayDialogInputText();
  const OverlayDialogAction action = overlay_dialog_.action;
  const std::wstring input_text = common::TrimWide(overlay_dialog_.input_text);
  const std::wstring selected_color = overlay_dialog_.selected_color.empty() ? L"#3B82F6" : overlay_dialog_.selected_color;
  const std::int64_t target_id = overlay_dialog_.target_id;
  const std::int64_t related_clip_id = overlay_dialog_.related_clip_id;
  const bool apply_filter_after_submit = overlay_dialog_.apply_filter_after_submit;

  const auto set_input_error = [this](std::wstring text) {
    overlay_dialog_.error_text = std::move(text);
    InvalidateRect(hwnd_, nullptr, FALSE);
    FocusOverlayDialogInput(false);
  };

  switch (action) {
    case OverlayDialogAction::CreateGroup: {
      if (input_text.empty()) {
        set_input_error(L"\u8d44\u6599\u5939\u540d\u79f0\u4e0d\u80fd\u4e3a\u7a7a\u3002");
        return;
      }
      std::int64_t group_id = 0;
      const common::Status status = controller_->EnsureGroup(input_text, &group_id);
      if (!status.ok() || group_id <= 0) {
        set_input_error(L"\u65b0\u5efa\u8d44\u6599\u5939\u5931\u8d25");
        return;
      }
      RefreshGroups();
      CloseOverlayDialog(true);
      if (related_clip_id > 0) {
        MoveClipToGroupById(related_clip_id, group_id, L"\u5df2\u79fb\u52a8\u5230\u65b0\u8d44\u6599\u5939");
      } else {
        RefreshStatus(L"\u5df2\u65b0\u5efa\u8d44\u6599\u5939");
        SelectGroupById(group_id);
      }
      return;
    }
    case OverlayDialogAction::RenameGroup: {
      if (input_text.empty()) {
        set_input_error(L"\u8d44\u6599\u5939\u540d\u79f0\u4e0d\u80fd\u4e3a\u7a7a\u3002");
        return;
      }
      const common::Status status = controller_->RenameGroup(target_id, input_text);
      if (!status.ok()) {
        set_input_error(L"\u91cd\u547d\u540d\u8d44\u6599\u5939\u5931\u8d25");
        return;
      }
      if (current_group_id_ == target_id) {
        browser_state_.group_name = input_text;
        current_group_name_ = input_text;
      }
      RefreshGroups();
      CloseOverlayDialog(true);
      RefreshStatus(L"\u5df2\u91cd\u547d\u540d\u8d44\u6599\u5939");
      RefreshData(true);
      return;
    }
    case OverlayDialogAction::CreateTag: {
      if (input_text.empty()) {
        set_input_error(L"\u6807\u7b7e\u540d\u79f0\u4e0d\u80fd\u4e3a\u7a7a\u3002");
        return;
      }
      std::int64_t tag_id = 0;
      common::Status status = controller_->EnsureTag(input_text, selected_color, &tag_id);
      if (!status.ok() || tag_id <= 0) {
        set_input_error(L"\u65b0\u5efa\u6807\u7b7e\u5931\u8d25");
        return;
      }
      if (related_clip_id > 0) {
        status = controller_->AddTag(related_clip_id, tag_id);
        if (!status.ok()) {
          set_input_error(L"\u6dfb\u52a0\u6807\u7b7e\u5931\u8d25");
          return;
        }
      }
      RefreshTags();
      CloseOverlayDialog(true);
      if (apply_filter_after_submit || related_clip_id == 0) {
        core::browser::BrowserCommand select_tag;
        select_tag.kind = core::browser::BrowserCommandKind::SelectTag;
        select_tag.tag_id = tag_id;
        ApplyBrowserCommand(select_tag);
      }
      RefreshStatus(L"Created tag");
      RefreshData(true);
      return;
    }
    case OverlayDialogAction::RenameTag: {
      if (input_text.empty()) {
        set_input_error(L"\u6807\u7b7e\u540d\u79f0\u4e0d\u80fd\u4e3a\u7a7a\u3002");
        return;
      }
      const common::Status status = controller_->RenameTag(target_id, input_text, selected_color);
      if (!status.ok()) {
        set_input_error(L"Rename tag failed");
        return;
      }
      core::browser::BrowserCommand select_tag;
      select_tag.kind = core::browser::BrowserCommandKind::SelectTag;
      select_tag.tag_id = target_id;
      ApplyBrowserCommand(select_tag);
      RefreshTags();
      CloseOverlayDialog(true);
      RefreshStatus(L"Renamed tag");
      RefreshData(true);
      return;
    }
    case OverlayDialogAction::DeleteGroup: {
      const common::Status status = controller_->DeleteGroup(target_id);
      if (!status.ok()) {
        CloseOverlayDialog(true);
        RefreshStatus(L"\u5220\u9664\u8d44\u6599\u5939\u5931\u8d25");
        return;
      }
      if (current_group_id_ == target_id) {
        ApplyBrowserCommand(core::browser::BrowserCommand{core::browser::BrowserCommandKind::SetScopeLibrary});
      }
      RefreshGroups();
      CloseOverlayDialog(true);
      RefreshStatus(L"\u5df2\u5220\u9664\u8d44\u6599\u5939");
      RefreshData(true);
      return;
    }
    case OverlayDialogAction::DeleteTag: {
      const common::Status status = controller_->DeleteTag(target_id);
      if (!status.ok()) {
        CloseOverlayDialog(true);
        RefreshStatus(L"\u5220\u9664\u6807\u7b7e\u5931\u8d25");
        return;
      }
      ApplyBrowserCommand(core::browser::BrowserCommand{
          core::browser::BrowserCommandKind::ClearTags});
      RefreshTags();
      CloseOverlayDialog(true);
      RefreshStatus(L"Deleted tag");
      RefreshData(true);
      return;
    }
    case OverlayDialogAction::None:
    default:
      CloseOverlayDialog(true);
      return;
  }
}

bool MainWindow::SelectGroupById(std::int64_t group_id) {
  const core::store::GroupRecord* group = FindGroupById(group_id);
  if (group == nullptr) {
    return false;
  }

  core::browser::BrowserCommand select_group;
  select_group.kind = core::browser::BrowserCommandKind::SelectGroup;
  select_group.group_id = group->id;
  select_group.group_name = group->name;
  ApplyBrowserCommand(select_group);
  RefreshData(false);
  return true;
}

bool MainWindow::SelectGroupByIndex(size_t index) {
  if (index >= groups_.size()) {
    return false;
  }
  return SelectGroupById(groups_[index].id);
}

bool MainWindow::IsPanelMenuTogglePoint(POINT screen_point, PanelMenuKind* kind) const {
  if (hwnd_ == nullptr) {
    return false;
  }

  RECT client{};
  GetClientRect(hwnd_, &client);
  const PanelLayout layout = ComputeLayout(client.right, client.bottom);
  auto to_screen = [this](RECT rect) {
    MapWindowPoints(hwnd_, HWND_DESKTOP, reinterpret_cast<LPPOINT>(&rect), 2);
    return rect;
  };

  const RECT more_rect = to_screen(layout.toolbar[2]);
  if (RectContains(more_rect, screen_point)) {
    if (kind != nullptr) {
      *kind = PanelMenuKind::More;
    }
    return true;
  }

  const RECT filter_rect = to_screen(layout.nav_actions[1]);
  if (RectContains(filter_rect, screen_point)) {
    if (kind != nullptr) {
      *kind = PanelMenuKind::Filter;
    }
    return true;
  }

  const int group_tab_index = static_cast<int>(std::size(kVisibleTabs) - 1);
  const RECT group_rect = to_screen(layout.tabs[group_tab_index]);
  if (RectContains(group_rect, screen_point)) {
    if (kind != nullptr) {
      *kind = PanelMenuKind::Group;
    }
    return true;
  }

  return false;
}

void MainWindow::OpenGroupMenu() {
  const std::vector<PopupMenuItemData> menu = BuildGroupMenuItems(groups_);

  RECT client{};
  GetClientRect(hwnd_, &client);
  const PanelLayout layout = ComputeLayout(client.right, client.bottom);
  const int group_tab_index = static_cast<int>(std::size(kVisibleTabs) - 1);
  POINT point{layout.tabs[group_tab_index].left, layout.tabs[group_tab_index].bottom + 4};
  ClientToScreen(hwnd_, &point);
  const int command = TrackPopupMenuForPanel(PanelMenuKind::Group, menu, point);
  const GroupMenuCommand decoded_command = DecodeGroupMenuCommand(command, groups_.size());

  switch (decoded_command.kind) {
    case GroupMenuCommandKind::SelectLibrary:
      ApplyBrowserCommand(core::browser::BrowserCommand{core::browser::BrowserCommandKind::SetScopeLibrary});
      RefreshData(false);
      return;
    case GroupMenuCommandKind::SelectExistingGroup:
      SelectGroupByIndex(decoded_command.group_index);
      return;
    case GroupMenuCommandKind::CreateGroup:
      CreateGroupAndSelect();
      return;
    case GroupMenuCommandKind::RenameGroup:
      if (decoded_command.group_index < groups_.size()) {
        RenameGroupById(groups_[decoded_command.group_index].id);
      }
      return;
    case GroupMenuCommandKind::DeleteGroup:
      if (decoded_command.group_index < groups_.size()) {
        DeleteGroupById(groups_[decoded_command.group_index].id);
      }
      return;
    case GroupMenuCommandKind::None:
      return;
  }
}

void MainWindow::OpenMoreMenu() {
  std::vector<PopupMenuItemData> menu;
  menu.push_back(MakePopupMenuItem(1, L"\u5237\u65b0"));
  menu.push_back(MakePopupMenuItem(2, L"\u6e05\u7406\u5386\u53f2"));
  menu.push_back(MakePopupMenuSeparator());
  menu.push_back(MakePopupMenuItem(3, L"\u8bbe\u7f6e"));
  menu.push_back(MakePopupMenuItem(4, L"\u9690\u85cf"));

  RECT client{};
  GetClientRect(hwnd_, &client);
  const PanelLayout layout = ComputeLayout(client.right, client.bottom);
  POINT point{layout.toolbar[2].left, layout.toolbar[2].bottom + 4};
  ClientToScreen(hwnd_, &point);
  const int command = TrackPopupMenuForPanel(PanelMenuKind::More, menu, point);

  switch (command) {
    case 1:
      RefreshData(true);
      break;
    case 2:
      if (controller_ != nullptr && controller_->ClearHistory().ok()) {
        RefreshData(false);
      }
      break;
    case 3:
      if (open_settings_callback_) {
        open_settings_callback_();
      }
      break;
    case 4:
      HideWindow();
      break;
    default:
      break;
  }
}

void MainWindow::OpenFilterMenu() {
  const auto browser_state = BuildBrowserState();
  const auto collect_visible_source_apps = [this]() {
    std::vector<std::wstring> values;
    std::unordered_set<std::wstring> seen;
    for (const auto& [page_index, clips] : page_cache_) {
      (void)page_index;
      for (const auto& clip : clips) {
        if (clip.source_app_name.empty()) {
          continue;
        }
        if (seen.insert(clip.source_app_name).second) {
          values.push_back(clip.source_app_name);
        }
      }
    }
    std::sort(values.begin(), values.end());
    if (values.size() > 8) {
      values.resize(8);
    }
    return values;
  };

  std::vector<PopupMenuItemData> type_menu;
  type_menu.push_back(MakePopupMenuItem(30, strings::kFilterTypeAll, true, !browser_state.filters.type.has_value()));
  type_menu.push_back(MakePopupMenuItem(
      31,
      strings::kFilterTypeText,
      true,
      browser_state.filters.type == core::store::ClipContentType::Text));
  type_menu.push_back(MakePopupMenuItem(
      32,
      strings::kFilterTypeHtml,
      true,
      browser_state.filters.type == core::store::ClipContentType::Html));
  type_menu.push_back(MakePopupMenuItem(
      33,
      strings::kFilterTypeImage,
      true,
      browser_state.filters.type == core::store::ClipContentType::Image));
  type_menu.push_back(MakePopupMenuItem(
      34,
      strings::kFilterTypeFiles,
      true,
      browser_state.filters.type == core::store::ClipContentType::Files));

  std::vector<PopupMenuItemData> date_menu;
  date_menu.push_back(MakePopupMenuItem(20, strings::kFilterDateAll, true, current_date_range_ == core::store::DateRangeFilter::All));
  date_menu.push_back(MakePopupMenuItem(21, strings::kFilterDateToday, true, current_date_range_ == core::store::DateRangeFilter::Today));
  date_menu.push_back(MakePopupMenuItem(22, strings::kFilterDateLast7Days, true, current_date_range_ == core::store::DateRangeFilter::Last7Days));
  date_menu.push_back(MakePopupMenuItem(23, strings::kFilterDateLast30Days, true, current_date_range_ == core::store::DateRangeFilter::Last30Days));

  std::vector<PopupMenuItemData> tag_menu;
  tag_menu.push_back(MakePopupMenuItem(1100, strings::kFilterTagsAny, true, current_tag_id_ == 0));
  if (!tags_.empty()) {
    tag_menu.push_back(MakePopupMenuSeparator());
    UINT tag_command = 1000;
    for (const auto& tag : tags_) {
      tag_menu.push_back(MakePopupMenuItem(tag_command++, tag.name, true, current_tag_id_ == tag.id));
    }
  }
  tag_menu.push_back(MakePopupMenuSeparator());
  tag_menu.push_back(MakePopupMenuItem(1200, strings::kTagCreate));
  if (current_tag_id_ > 0) {
    tag_menu.push_back(MakePopupMenuItem(1201, strings::kTagRenameCurrent));
    tag_menu.push_back(MakePopupMenuItem(1202, strings::kTagDeleteCurrent));
  }

  std::vector<PopupMenuItemData> source_menu;
  source_menu.push_back(MakePopupMenuItem(
      2100,
      strings::kFilterSourceAny,
      true,
      !browser_state.filters.source_app_name.has_value() || browser_state.filters.source_app_name->empty()));
  const auto visible_source_apps = collect_visible_source_apps();
  if (!visible_source_apps.empty()) {
    source_menu.push_back(MakePopupMenuSeparator());
    UINT source_command = 2000;
    for (const auto& source_app : visible_source_apps) {
      source_menu.push_back(MakePopupMenuItem(
          source_command++,
          source_app,
          true,
          browser_state.filters.source_app_name.has_value() && *browser_state.filters.source_app_name == source_app));
    }
  }

  std::vector<PopupMenuItemData> menu;
  menu.push_back(MakePopupMenuItem(30, strings::kFilterTypeAll, true, !browser_state.filters.type.has_value()));
  menu.push_back(MakePopupMenuItem(
      31,
      strings::kFilterTypeText,
      true,
      browser_state.filters.type == core::store::ClipContentType::Text));
  menu.push_back(MakePopupMenuItem(
      32,
      strings::kFilterTypeHtml,
      true,
      browser_state.filters.type == core::store::ClipContentType::Html));
  menu.push_back(MakePopupMenuItem(
      33,
      strings::kFilterTypeImage,
      true,
      browser_state.filters.type == core::store::ClipContentType::Image));
  menu.push_back(MakePopupMenuItem(
      34,
      strings::kFilterTypeFiles,
      true,
      browser_state.filters.type == core::store::ClipContentType::Files));
  menu.push_back(MakePopupMenuSeparator());
  menu.push_back(MakePopupMenuSubmenu(strings::kFilterDateMenu, std::move(date_menu)));
  menu.push_back(MakePopupMenuSubmenu(strings::kFilterSourceMenu, std::move(source_menu)));
  menu.push_back(MakePopupMenuSubmenu(strings::kFilterTagMenu, std::move(tag_menu)));
  menu.push_back(MakePopupMenuItem(10, strings::kFilterFrequentOnly, true, browser_state.filters.frequent_only));
  menu.push_back(MakePopupMenuItem(11, strings::kFilterPinnedOnly, true, browser_state.filters.pinned_only));
  menu.push_back(MakePopupMenuSeparator());
  menu.push_back(MakePopupMenuItem(12, strings::kFilterClearAll));

  RECT client{};
  GetClientRect(hwnd_, &client);
  const PanelLayout layout = ComputeLayout(client.right, client.bottom);
  POINT point{layout.nav_actions[1].left, layout.nav_actions[1].bottom + 4};
  ClientToScreen(hwnd_, &point);
  const int command = TrackPopupMenuForPanel(PanelMenuKind::Filter, menu, point);

  switch (command) {
    case 10:
      ApplyBrowserCommand(core::browser::BrowserCommand{
          core::browser::BrowserCommandKind::ToggleFrequentOnly});
      RefreshData(false);
      break;
    case 11:
      ApplyBrowserCommand(core::browser::BrowserCommand{
          core::browser::BrowserCommandKind::TogglePinnedOnly});
      RefreshData(false);
      break;
    case 12:
      ApplyBrowserCommand(core::browser::BrowserCommand{
          core::browser::BrowserCommandKind::ClearAllStructuredFilters});
      RefreshData(false);
      break;
    case 20:
      ApplyBrowserCommand(core::browser::BrowserCommand{
          core::browser::BrowserCommandKind::SetDateRange,
          std::nullopt,
          L"",
          L"",
          std::nullopt,
          core::store::DateRangeFilter::All});
      RefreshData(false);
      break;
    case 21:
      ApplyBrowserCommand(core::browser::BrowserCommand{
          core::browser::BrowserCommandKind::SetDateRange,
          std::nullopt,
          L"",
          L"",
          std::nullopt,
          core::store::DateRangeFilter::Today});
      RefreshData(false);
      break;
    case 22:
      ApplyBrowserCommand(core::browser::BrowserCommand{
          core::browser::BrowserCommandKind::SetDateRange,
          std::nullopt,
          L"",
          L"",
          std::nullopt,
          core::store::DateRangeFilter::Last7Days});
      RefreshData(false);
      break;
    case 23:
      ApplyBrowserCommand(core::browser::BrowserCommand{
          core::browser::BrowserCommandKind::SetDateRange,
          std::nullopt,
          L"",
          L"",
          std::nullopt,
          core::store::DateRangeFilter::Last30Days});
      RefreshData(false);
      break;
    case 30:
      ApplyBrowserCommand(core::browser::BrowserCommand{
          core::browser::BrowserCommandKind::ClearTypeFilter});
      RefreshData(false);
      break;
    case 2100:
      ApplyBrowserCommand(core::browser::BrowserCommand{
          core::browser::BrowserCommandKind::ClearSourceAppFilter});
      RefreshData(false);
      break;
    case 31:
    case 32:
    case 33:
    case 34: {
      core::browser::BrowserCommand filter_command{};
      filter_command.kind = core::browser::BrowserCommandKind::SetTypeFilter;
      switch (command) {
        case 31:
          filter_command.type = core::store::ClipContentType::Text;
          break;
        case 32:
          filter_command.type = core::store::ClipContentType::Html;
          break;
        case 33:
          filter_command.type = core::store::ClipContentType::Image;
          break;
        case 34:
          filter_command.type = core::store::ClipContentType::Files;
          break;
      }
      ApplyBrowserCommand(filter_command);
      RefreshData(false);
      break;
    }
    case 1100:
      ApplyBrowserCommand(core::browser::BrowserCommand{
          core::browser::BrowserCommandKind::ClearTags});
      RefreshData(false);
      break;
    case 1200:
      PromptCreateTag(0, false);
      break;
    case 1201:
      RenameCurrentTag();
      break;
    case 1202:
      DeleteCurrentTag();
      break;
    default:
      if (command >= 2000 && command < 2000 + static_cast<int>(visible_source_apps.size())) {
        const auto& source_name = visible_source_apps[static_cast<size_t>(command - 2000)];
        core::browser::BrowserCommand source_command{};
        source_command.kind = core::browser::BrowserCommandKind::SetSourceAppFilter;
        source_command.source_app_name = source_name;
        ApplyBrowserCommand(source_command);
        RefreshData(false);
      } else if (command >= 1000 && command < 1000 + static_cast<int>(tags_.size())) {
        const size_t index = static_cast<size_t>(command - 1000);
        core::browser::BrowserCommand select_tag;
        select_tag.kind = core::browser::BrowserCommandKind::SelectTag;
        select_tag.tag_id = tags_[index].id;
        ApplyBrowserCommand(select_tag);
        RefreshData(false);
      }
      break;
  }
}

void MainWindow::EnsureSelectedClipLoaded() {
  const auto* summary = GetSelectedSummary();
  if (summary == nullptr || controller_ == nullptr) {
    return;
  }
  if (selected_clip_ && selected_clip_->summary.id == summary->id) {
    return;
  }
  core::store::ClipRecord clip;
  const common::Status status = controller_->GetClipById(summary->id, &clip);
  if (!status.ok()) {
    return;
  }
  selected_clip_ = std::move(clip);
  actions_ = controller_->ResolveActions(*selected_clip_);
  SortActions(&actions_);
}

void MainWindow::OpenSelectedContextMenu(POINT screen_point) {
  EnsureSelectedClipLoaded();
  const auto* summary = GetSelectedSummary();
  if (summary == nullptr) {
    return;
  }

  std::vector<PopupMenuItemData> menu;
  UINT command = 100;
  if (!actions_.empty()) {
    for (const auto& action : actions_) {
      const std::wstring label = ActionLabel(action);
      menu.push_back(MakePopupMenuItem(command++, label));
    }
    menu.push_back(MakePopupMenuSeparator());
  } else {
    menu.push_back(MakePopupMenuItem(command++, L"鍘熸牱绮樿创"));
    menu.push_back(MakePopupMenuSeparator());
  }

  const UINT favorite_command = 1000;
  const UINT pin_command = 1001;
  const UINT delete_command = 1002;
  const UINT add_to_queue_command = 1003;
  const UINT remove_from_queue_command = 1004;
  const UINT export_command = 1005;
  const UINT move_queue_front_command = 1006;
  const UINT move_to_library_command = 1007;
  const UINT move_to_history_command = 1008;
  const UINT move_to_library_root_command = 1009;
  const UINT move_to_group_command_base = 3000;
  const UINT create_group_and_move_command = 3100;
  std::vector<PopupMenuItemData> tag_menu;
  std::vector<PopupMenuItemData> folder_menu;
  std::vector<core::store::TagRecord> clip_tags;
  if (controller_ != nullptr) {
    controller_->ListClipTags(summary->id, &clip_tags);
  }
  bool in_queue = false;
  if (controller_ != nullptr) {
    std::vector<core::store::QueueEntry> queue_entries;
    if (controller_->ListQueue(&queue_entries).ok()) {
      in_queue = std::any_of(queue_entries.begin(), queue_entries.end(), [summary](const core::store::QueueEntry& entry) {
        return entry.clip_id == summary->id;
      });
    }
  }
  if (tags_.empty()) {
    tag_menu.push_back(MakePopupMenuItem(0, L"\u6682\u65e0\u6807\u7b7e", false));
  } else {
    UINT tag_command = 2000;
    for (const auto& tag : tags_) {
      const bool checked = std::any_of(clip_tags.begin(), clip_tags.end(), [&tag](const core::store::TagRecord& clip_tag) {
        return clip_tag.id == tag.id;
      });
      tag_menu.push_back(MakePopupMenuItem(tag_command++, tag.name, true, checked));
    }
  }
  tag_menu.push_back(MakePopupMenuSeparator());
  tag_menu.push_back(MakePopupMenuItem(2100, L"\u65b0\u5efa\u6807\u7b7e..."));
  folder_menu.push_back(MakePopupMenuItem(
      move_to_library_root_command,
      L"\u5168\u90e8\u8d44\u6599\u5e93",
      true,
      summary->storage_tier == core::store::StorageTier::Library && summary->group_id == 0));
  if (!groups_.empty()) {
    folder_menu.push_back(MakePopupMenuSeparator());
    UINT folder_command = move_to_group_command_base;
    for (const auto& group : groups_) {
      folder_menu.push_back(MakePopupMenuItem(
          folder_command++,
          group.name,
          true,
          summary->group_id == group.id));
    }
  }
  folder_menu.push_back(MakePopupMenuSeparator());
  folder_menu.push_back(MakePopupMenuItem(create_group_and_move_command, L"\u65b0\u5efa\u8d44\u6599\u5939..."));
  menu.push_back(MakePopupMenuSeparator());
  menu.push_back(MakePopupMenuItem(favorite_command, summary->is_favorite ? L"\u53d6\u6d88\u6536\u85cf" : L"\u52a0\u5165\u6536\u85cf"));
  menu.push_back(MakePopupMenuItem(pin_command, summary->is_pinned ? L"\u53d6\u6d88\u7f6e\u9876" : L"\u7f6e\u9876"));
  menu.push_back(MakePopupMenuItem(
      in_queue ? remove_from_queue_command : add_to_queue_command,
      in_queue ? L"\u79fb\u51fa\u961f\u5217" : L"\u52a0\u5165\u961f\u5217"));
  menu.push_back(MakePopupMenuItem(
      summary->storage_tier == core::store::StorageTier::Library ? move_to_history_command : move_to_library_command,
      summary->storage_tier == core::store::StorageTier::Library ? L"\u79fb\u56de\u5386\u53f2" : L"\u52a0\u5165\u8d44\u6599\u5e93"));
  menu.push_back(MakePopupMenuSubmenu(L"\u79fb\u52a8\u5230\u8d44\u6599\u5939", std::move(folder_menu)));
  if (in_queue) {
    menu.push_back(MakePopupMenuItem(move_queue_front_command, L"\u8bbe\u4e3a\u4e0b\u4e00\u9879"));
  }
  menu.push_back(MakePopupMenuSubmenu(L"\u6807\u7b7e", std::move(tag_menu)));
  menu.push_back(MakePopupMenuItem(export_command, L"\u5bfc\u51fa\u6761\u76ee"));
  menu.push_back(MakePopupMenuItem(delete_command, L"\u5220\u9664"));

  const UINT selected_command = static_cast<UINT>(TrackPopupMenuForPanel(PanelMenuKind::ItemContext, menu, screen_point));

  if (selected_command >= 100 && selected_command < 100 + actions_.size()) {
    PerformActionById(actions_[selected_command - 100].id);
    return;
  }
  switch (selected_command) {
    case 100:
      if (actions_.empty()) {
        PerformDefaultPaste();
      }
      break;
    case favorite_command:
      ToggleFavoriteSelected();
      break;
    case pin_command:
      TogglePinnedSelected();
      break;
    case add_to_queue_command:
      if (controller_ != nullptr && controller_->AddToQueue(summary->id).ok()) {
        RefreshStatus(L"\u5df2\u52a0\u5165\u961f\u5217");
        RefreshData(true);
      }
      break;
    case remove_from_queue_command:
      if (controller_ != nullptr && controller_->RemoveFromQueue(summary->id).ok()) {
        RefreshStatus(L"\u5df2\u79fb\u51fa\u961f\u5217");
        RefreshData(true);
      }
      break;
    case move_queue_front_command:
      if (controller_ != nullptr && controller_->MoveQueueToFront(summary->id).ok()) {
        RefreshStatus(L"Set as next queue item");
        RefreshData(true);
      }
      break;
    case move_to_library_command:
      if (controller_ != nullptr && controller_->SetStorageTier(summary->id, core::store::StorageTier::Library).ok()) {
        RefreshStatus(L"\u5df2\u52a0\u5165\u8d44\u6599\u5e93");
        RefreshData(true);
      }
      break;
    case move_to_history_command:
      if (controller_ != nullptr && controller_->SetStorageTier(summary->id, core::store::StorageTier::History).ok()) {
        RefreshStatus(L"\u5df2\u79fb\u56de\u5386\u53f2");
        RefreshData(true);
      }
      break;
    case move_to_library_root_command:
      if (controller_ != nullptr && controller_->MoveClipsToLibraryRoot({summary->id}).ok()) {
        RefreshStatus(L"\u5df2\u79fb\u52a8\u5230\u5168\u90e8\u8d44\u6599");
        RefreshData(true);
      }
      break;
    case export_command:
      if (controller_ != nullptr) {
        wchar_t temp_path[MAX_PATH]{};
        GetTempPathW(MAX_PATH, temp_path);
        std::wstring export_path = std::wstring(temp_path) + L"Scraps_" + std::to_wstring(summary->id);
        std::wstring exported_path;
        const common::Status export_status = controller_->ExportClipToFile(summary->id, export_path, &exported_path);
        if (export_status.ok()) {
          RefreshStatus(L"\u5df2\u5bfc\u51fa\u5230 " + exported_path);
        } else {
          RefreshStatus(L"\u5bfc\u51fa\u5931\u8d25");
        }
      }
      break;
    case delete_command:
      DeleteSelected();
      break;
    default:
      if (selected_command == 2100) {
        PromptCreateTag(summary->id, false);
      } else if (selected_command == create_group_and_move_command) {
        CreateGroupAndMoveClip(summary->id);
      } else if (selected_command >= move_to_group_command_base &&
                 selected_command < move_to_group_command_base + static_cast<UINT>(groups_.size()) &&
                 controller_ != nullptr) {
        const size_t group_index = static_cast<size_t>(selected_command - move_to_group_command_base);
        MoveClipToGroupByIndex(summary->id, group_index);
      } else if (selected_command >= 2000 && selected_command < 2000 + static_cast<UINT>(tags_.size()) && controller_ != nullptr) {
        const size_t tag_index = static_cast<size_t>(selected_command - 2000);
        const bool already_tagged =
            std::any_of(clip_tags.begin(), clip_tags.end(), [this, tag_index](const core::store::TagRecord& clip_tag) {
              return clip_tag.id == tags_[tag_index].id;
            });
        const common::Status tag_status = already_tagged
            ? controller_->RemoveTag(summary->id, tags_[tag_index].id)
            : controller_->AddTag(summary->id, tags_[tag_index].id);
        if (tag_status.ok()) {
          RefreshStatus(already_tagged ? L"\u5df2\u79fb\u9664\u6807\u7b7e" : L"\u5df2\u6dfb\u52a0\u6807\u7b7e");
          RefreshData(true);
        }
      }
      break;
  }
}

int MainWindow::TrackPopupMenuForPanel(PanelMenuKind kind, const std::vector<PopupMenuItemData>& items, POINT screen_point) {
  if (hwnd_ == nullptr || items.empty()) {
    return 0;
  }
  if (g_popup_menu_session != nullptr && g_popup_menu_session->owner == this) {
    if (active_menu_kind_ == kind) {
      DismissOwnedPopupMenus();
      return 0;
    }
    DismissOwnedPopupMenus();
    return 0;
  }

  const std::optional<core::clipboard::PasteTargetContext> previous_external_context = BuildExternalFocusRestoreContext();
  if (previous_external_context.has_value()) {
    focus_coordinator_.set_paste_target_context(*previous_external_context);
  }

  if (!EnsurePopupMenuWindowClass(instance_)) {
    return 0;
  }

  PopupMenuSession session;
  session.owner = this;
  session.instance = instance_;
  session.running = true;
  g_popup_menu_session = &session;
  active_menu_kind_ = kind;

  session.root = std::make_unique<PopupMenuWindowState>();
  session.root->owner = this;
  if (!BuildPopupMenuWindowItems(hwnd_, items, &session.root->items, &session.root->width, &session.root->height)) {
    g_popup_menu_session = nullptr;
    active_menu_kind_ = PanelMenuKind::None;
    return 0;
  }

  SetInteractionGuard(detail::InteractionGuardKind::PopupMenu, true);
  session.root->hwnd = CreateWindowExW(
      WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
      kPopupMenuWindowClassName,
      L"",
      WS_POPUP,
      screen_point.x,
      screen_point.y,
      session.root->width,
      session.root->height,
      hwnd_,
      nullptr,
      instance_,
      session.root.get());
  if (session.root->hwnd != nullptr) {
    session.open_window_count = 1;
    ShowWindow(session.root->hwnd, SW_SHOWNOACTIVATE);
    UpdateWindow(session.root->hwnd);
  } else {
    session.running = false;
    active_menu_kind_ = PanelMenuKind::None;
  }

  MSG msg{};
  while (session.running) {
    const BOOL result = GetMessageW(&msg, nullptr, 0, 0);
    if (result == -1) {
      session.running = false;
      break;
    }
    if (result == 0) {
      session.quit_received = true;
      session.quit_code = msg.wParam;
      session.running = false;
      break;
    }
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  SetInteractionGuard(detail::InteractionGuardKind::PopupMenu, false);
  g_popup_menu_session = nullptr;
  active_menu_kind_ = PanelMenuKind::None;
  if (session.quit_received) {
    PostQuitMessage(static_cast<int>(session.quit_code));
  }
  return session.selected_command;
}

void MainWindow::DismissOwnedPopupMenus() {
  if (g_popup_menu_session == nullptr || g_popup_menu_session->owner != this) {
    return;
  }
  active_menu_kind_ = PanelMenuKind::None;
  if (g_popup_menu_session->root && g_popup_menu_session->root->hwnd != nullptr) {
    PostMessageW(g_popup_menu_session->root->hwnd, WM_CLOSE, 0, 0);
  }
}

void MainWindow::PasteNextQueueItem() {
  if (controller_ == nullptr || queue_count_ <= 0) {
    RefreshStatus(L"\u961f\u5217\u4e3a\u7a7a");
    return;
  }
  const core::clipboard::PasteTargetContext target_context =
      core::clipboard::IsValidPasteTargetContext(focus_coordinator_.paste_target_context())
          ? focus_coordinator_.paste_target_context()
          : CapturePasteTargetContext();
  const core::clipboard::PasteRoute route =
      paste_route_requires_recovery_ ? core::clipboard::PasteRoute::Recovery : core::clipboard::PasteRoute::Fast;
  if (!panel_locked_) {
    HideWindow();
  }
  const common::Status status = controller_->PasteNextInQueue(target_context, route);
  if (!status.ok()) {
    if (!panel_locked_ && !IsWindowVisible(hwnd_)) {
      ShowAndActivate();
    }
    RefreshStatus(L"\u961f\u5217\u7c98\u8d34\u5931\u8d25");
    return;
  }
  RefreshStatus(L"Pasted next queue item");
  RefreshData(true);
}

MainWindow::ReorderScope MainWindow::CurrentReorderScope() const {
  if (!display_query_.search_text.empty() || current_date_range_ != core::store::DateRangeFilter::All) {
    return ReorderScope::None;
  }
  if (current_view_ == core::store::ClipView::Queue && current_tag_id_ == 0) {
    return ReorderScope::Queue;
  }
  if (current_view_ == core::store::ClipView::Favorites && current_tag_id_ == 0) {
    return ReorderScope::Favorites;
  }
  if (current_tag_id_ > 0) {
    return ReorderScope::Tag;
  }
  if (current_view_ == core::store::ClipView::All && current_group_id_ == 0) {
    return ReorderScope::Pinned;
  }
  return ReorderScope::None;
}

bool MainWindow::CanReorderRow(const core::store::ClipSummary& summary) const {
  switch (CurrentReorderScope()) {
    case ReorderScope::Queue:
      return true;
    case ReorderScope::Favorites:
      return summary.is_favorite;
    case ReorderScope::Tag:
      return current_tag_id_ > 0;
    case ReorderScope::Pinned:
      return summary.is_pinned;
    case ReorderScope::None:
    default:
      return false;
  }
}

void MainWindow::ArmRowDrag(POINT point, int index) {
  if (index < 0) {
    row_drag_candidate_active_ = false;
    row_drag_candidate_index_ = -1;
    row_drag_scope_ = ReorderScope::None;
    return;
  }
  const auto* summary = GetSummaryAt(index);
  if (summary == nullptr || (!CanReorderRow(*summary) &&
      (summary->content_type != core::store::ClipContentType::Files &&
       summary->content_type != core::store::ClipContentType::Image))) {
    row_drag_candidate_active_ = false;
    row_drag_candidate_index_ = -1;
    row_drag_scope_ = ReorderScope::None;
    return;
  }
  row_drag_candidate_active_ = true;
  row_drag_candidate_index_ = index;
  row_drag_start_point_ = point;
  row_drag_scope_ = CurrentReorderScope();
}

bool MainWindow::MaybeStartRowDrag(POINT point) {
  if (!row_drag_candidate_active_ || row_drag_active_ || row_drag_candidate_index_ < 0) {
    return false;
  }
  const int drag_dx = GetSystemMetrics(SM_CXDRAG);
  const int drag_dy = GetSystemMetrics(SM_CYDRAG);
  if (std::abs(point.x - row_drag_start_point_.x) < drag_dx && std::abs(point.y - row_drag_start_point_.y) < drag_dy) {
    return false;
  }

  const auto* summary = GetSummaryAt(row_drag_candidate_index_);
  if (summary == nullptr) {
    row_drag_candidate_active_ = false;
    row_drag_candidate_index_ = -1;
    row_drag_scope_ = ReorderScope::None;
    return false;
  }

  if (CanReorderRow(*summary)) {
    row_drag_active_ = true;
    row_drag_item_index_ = row_drag_candidate_index_;
    row_drop_index_ = row_drag_candidate_index_;
    row_drop_after_ = false;
    row_drag_point_ = point;
    RECT client{};
    GetClientRect(hwnd_, &client);
    const PanelLayout layout = ComputeLayout(client.right, client.bottom);
    const float visual_scroll_offset = scroll_controller_->VisualScrollOffset();
    const vlist::RowRect row_pos = vlist::RowRectFor(
        viewport_.heights(), row_drag_item_index_, static_cast<float>(layout.list.top), visual_scroll_offset);
    const int row_top = static_cast<int>(std::lround(row_pos.top));
    const int row_height_px = std::max(1, static_cast<int>(std::lround(row_pos.bottom - row_pos.top)));
    row_drag_pointer_offset_y_ = std::clamp(static_cast<int>(point.y) - row_top, 0, row_height_px - 1);
    row_drag_candidate_active_ = false;
    row_drag_candidate_index_ = -1;
    SelectIndex(row_drag_item_index_, false);
    SetCapture(hwnd_);
    BeginRowDragSession();
    UpdateRowReorderAnimationTargets();
    EnsureAnimationTimer();
    InvalidateListRegion();
    return true;
  }

  row_drag_candidate_active_ = false;
  const int candidate_index = row_drag_candidate_index_;
  row_drag_candidate_index_ = -1;
  row_drag_scope_ = ReorderScope::None;
  return TryStartExternalDrag(candidate_index);
}

void MainWindow::UpdateRowDrag(POINT point) {
  if (!row_drag_active_ || item_count_ <= 0 || !row_drag_session_.has_value()) {
    return;
  }
  const POINT previous_point = row_drag_point_;
  row_drag_point_ = point;

  const std::uint64_t now = GetTickCount64();
  float delta_ms = 0.0f;
  LARGE_INTEGER counter{};
  if (performance_counter_frequency_.QuadPart > 0 && QueryPerformanceCounter(&counter)) {
    if (row_drag_last_update_qpc_ != 0) {
      delta_ms = static_cast<float>(
          (static_cast<double>(counter.QuadPart - row_drag_last_update_qpc_) * 1000.0) /
          static_cast<double>(performance_counter_frequency_.QuadPart));
      delta_ms = std::clamp(delta_ms, 0.5f, 40.0f);
    }
    row_drag_last_update_qpc_ = counter.QuadPart;
  }
  if (delta_ms <= 0.0f && row_drag_last_update_tick_ms_ != 0) {
    delta_ms = static_cast<float>(std::max<std::uint64_t>(1, now - row_drag_last_update_tick_ms_));
  }
  if (delta_ms > 0.0f) {
    const float delta_seconds = std::clamp(delta_ms / 1000.0f, 0.001f, 0.04f);
    const float rows_per_second =
        std::fabs(static_cast<float>(point.y - previous_point.y)) /
        static_cast<float>(std::max(1, GetItemHeight())) /
        delta_seconds;
    row_drag_velocity_rows_per_s_ += (rows_per_second - row_drag_velocity_rows_per_s_) * 0.35f;
  }
  row_drag_last_update_tick_ms_ = now;

  RECT client{};
  GetClientRect(hwnd_, &client);
  const PanelLayout layout = ComputeLayout(client.right, client.bottom);
  constexpr float kAutoScrollEdgeBandPx = 32.0f;
  constexpr float kAutoScrollEntrySpeed = 150.0f;
  constexpr float kAutoScrollBoundarySpeed = 300.0f;
  constexpr float kAutoScrollMaxSpeed = 2600.0f;
  row_drag_auto_scroll_velocity_ = (0.0f);
  const float top_band_end = static_cast<float>(layout.list.top) + kAutoScrollEdgeBandPx;
  const float bottom_band_start = static_cast<float>(layout.list.bottom) - kAutoScrollEdgeBandPx;
  if (static_cast<float>(point.y) <= top_band_end) {
    const float contact_strength = std::clamp((top_band_end - static_cast<float>(point.y)) / kAutoScrollEdgeBandPx, 0.0f, 1.0f);
    const float contact_eased = contact_strength * contact_strength * (3.0f - 2.0f * contact_strength);
    const float overshoot = std::max(0.0f, static_cast<float>(layout.list.top) - static_cast<float>(point.y));
    const float overshoot_strength = std::clamp(overshoot / kAutoScrollEdgeBandPx, 0.0f, 1.0f);
    if (static_cast<float>(point.y) >= static_cast<float>(layout.list.top)) {
      row_drag_auto_scroll_velocity_ = (-std::lerp(kAutoScrollEntrySpeed, kAutoScrollBoundarySpeed, contact_eased));
    } else {
      row_drag_auto_scroll_velocity_ = (-std::lerp(kAutoScrollBoundarySpeed, kAutoScrollMaxSpeed, overshoot_strength * overshoot_strength));
    }
  } else if (static_cast<float>(point.y) >= bottom_band_start) {
    const float contact_strength = std::clamp((static_cast<float>(point.y) - bottom_band_start) / kAutoScrollEdgeBandPx, 0.0f, 1.0f);
    const float contact_eased = contact_strength * contact_strength * (3.0f - 2.0f * contact_strength);
    const float overshoot = std::max(0.0f, static_cast<float>(point.y) - static_cast<float>(layout.list.bottom));
    const float overshoot_strength = std::clamp(overshoot / kAutoScrollEdgeBandPx, 0.0f, 1.0f);
    if (static_cast<float>(point.y) <= static_cast<float>(layout.list.bottom)) {
      row_drag_auto_scroll_velocity_ = (std::lerp(kAutoScrollEntrySpeed, kAutoScrollBoundarySpeed, contact_eased));
    } else {
      row_drag_auto_scroll_velocity_ = (std::lerp(kAutoScrollBoundarySpeed, kAutoScrollMaxSpeed, overshoot_strength * overshoot_strength));
    }
  }
  const float visual_scroll_offset = scroll_controller_->VisualScrollOffset();
  row_drag_session_->desired_slot = ComputeDesiredRowDragSlot(layout.list, visual_scroll_offset);
  EnsureAnimationTimer();
  InvalidateListRegion();
}

common::Status MainWindow::BuildDragFileList(const core::store::ClipSummary& summary, std::vector<std::wstring>* paths) {
  if (controller_ == nullptr || paths == nullptr) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "鎷栨嫿瀵煎嚭鍙傛暟鏃犳晥");
  }
  paths->clear();

  core::store::ClipRecord clip;
  common::Status status = controller_->GetClipById(summary.id, &clip);
  if (!status.ok()) {
    return status;
  }

  if (summary.content_type == core::store::ClipContentType::Files) {
    for (const auto& path : ExtractFilePathsFromClip(clip)) {
      if (std::filesystem::exists(path)) {
        paths->push_back(path);
      }
    }
    if (!paths->empty()) {
      return common::Status::Ok();
    }
    return common::Status::Error(common::StatusCode::NotFound, "No exportable file found.");
  }

  if (summary.content_type != core::store::ClipContentType::Image) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "浠呮枃浠跺拰鍥剧墖鏉＄洰鏀寔鎷栨嫿瀵煎嚭");
  }

  for (const auto& path : ExtractFilePathsFromClip(clip)) {
    if (!path.empty() && std::filesystem::exists(path)) {
      paths->push_back(path);
      return common::Status::Ok();
    }
  }
  if (!clip.secondary_path.empty() && std::filesystem::exists(clip.secondary_path)) {
    paths->push_back(clip.secondary_path);
    return common::Status::Ok();
  }
  if (!clip.storage_path.empty() && std::filesystem::exists(clip.storage_path) && IsImageFilePath(clip.storage_path)) {
    paths->push_back(clip.storage_path);
    return common::Status::Ok();
  }

  common::AppPaths app_paths;
  status = common::ResolveAppPaths(&app_paths);
  if (!status.ok()) {
    return status;
  }
  const std::filesystem::path export_dir = std::filesystem::path(app_paths.cache_dir) / L"drag-export";
  std::error_code ec;
  std::filesystem::create_directories(export_dir, ec);
  const std::wstring base_name = SanitizeFileName(
      !summary.title.empty() ? common::Utf8ToWide(summary.title).wide : L"image");
  std::wstring exported_path;
  status = controller_->ExportClipToFile(
      summary.id,
      (export_dir / (base_name + L"-" + std::to_wstring(summary.id) + L"-" + std::to_wstring(NowEpochMs()))).wstring(),
      &exported_path);
  if (!status.ok()) {
    return status;
  }
  if (!exported_path.empty() && std::filesystem::exists(exported_path)) {
    paths->push_back(exported_path);
    return common::Status::Ok();
  }
  return common::Status::Error(common::StatusCode::NotFound, "No exported image file was produced.");
}

bool MainWindow::TryStartExternalDrag(int index) {
  if (index < 0) {
    index = row_drag_candidate_index_;
  }
  const auto* summary = GetSummaryAt(index);
  if (summary == nullptr) {
    return false;
  }
  if (summary->content_type != core::store::ClipContentType::Files && summary->content_type != core::store::ClipContentType::Image) {
    return false;
  }

  std::vector<std::wstring> paths;
  const common::Status status = BuildDragFileList(*summary, &paths);
  if (!status.ok() || paths.empty()) {
    RefreshStatus(L"Cannot drag out this item");
    return false;
  }

  const bool was_row_drag_active = row_drag_active_;
  const int dragged_row_index = row_drag_item_index_;
  const POINT dragged_row_point = row_drag_point_;
  const int dragged_row_pointer_offset_y = row_drag_pointer_offset_y_;

  row_drag_active_ = false;
  row_drag_candidate_active_ = false;
  row_drag_candidate_index_ = -1;
  row_drag_item_index_ = -1;
  row_drop_index_ = -1;
  row_drop_after_ = false;
  row_drag_point_ = POINT{-1, -1};
  row_drag_pointer_offset_y_ = 0;
  row_drag_scope_ = ReorderScope::None;
  armed_click_item_index_ = -1;
  if (GetCapture() == hwnd_) {
    ReleaseCapture();
  }
  ClearRowDragSession();

  HGLOBAL drop_handle = CreateFileDropHandle(paths);
  if (drop_handle == nullptr) {
    RefreshStatus(L"\u51c6\u5907\u62d6\u62fd\u6570\u636e\u5931\u8d25");
    return false;
  }

  auto* data_object = new (std::nothrow) FileDropDataObject(drop_handle);
  if (data_object == nullptr) {
    GlobalFree(drop_handle);
    RefreshStatus(L"\u51c6\u5907\u62d6\u62fd\u6570\u636e\u5931\u8d25");
    return false;
  }
  (void)was_row_drag_active;
  (void)dragged_row_index;
  (void)dragged_row_point;
  (void)dragged_row_pointer_offset_y;
  HBITMAP preview_bitmap = CreateDragPreviewCardBitmap(*summary, paths);
  SIZE preview_size{0, 0};
  if (preview_bitmap != nullptr) {
    BITMAP preview_object{};
    GetObject(preview_bitmap, sizeof(preview_object), &preview_object);
    preview_size.cx = std::max(1, static_cast<int>(preview_object.bmWidth));
    preview_size.cy = std::max(1, static_cast<int>(preview_object.bmHeight));
  }
  HWND preview_hwnd = preview_bitmap != nullptr
      ? CreateDragPreviewWindow(hwnd_, preview_bitmap, preview_size, POINT{14, 14})
      : nullptr;
  auto* drop_source = new (std::nothrow) SimpleDropSource(preview_hwnd, preview_bitmap, POINT{14, 14});
  if (drop_source == nullptr) {
    if (preview_hwnd != nullptr) {
      DestroyWindow(preview_hwnd);
    }
    if (preview_bitmap != nullptr) {
      DeleteObject(preview_bitmap);
    }
    data_object->Release();
    RefreshStatus(L"Prepare drag source failed");
    return false;
  }

  DWORD effect = DROPEFFECT_NONE;
  const HRESULT hr = DoDragDrop(data_object, drop_source, DROPEFFECT_COPY, &effect);
  drop_source->Release();
  data_object->Release();

  if (SUCCEEDED(hr) && effect != DROPEFFECT_NONE) {
    RefreshStatus(L"Drag export completed");
    return true;
  }
  if (hr == DRAGDROP_S_CANCEL || effect == DROPEFFECT_NONE) {
    RefreshStatus(L"Drag export cancelled");
  } else {
    RefreshStatus(L"\u62d6\u62fd\u5bfc\u51fa\u5931\u8d25");
  }
  return false;
}

void MainWindow::FinishRowDrag(POINT, bool cancel) {
  const bool was_active = row_drag_active_;
  const int drag_index = row_drag_item_index_;
  const int drop_index = row_drop_index_;
  const ReorderScope scope = row_drag_scope_;

  row_drag_active_ = false;
  row_drag_candidate_active_ = false;
  row_drag_candidate_index_ = -1;
  row_drag_item_index_ = -1;
  row_drop_index_ = -1;
  row_drop_after_ = false;
  row_drag_point_ = POINT{-1, -1};
  row_drag_pointer_offset_y_ = 0;
  row_drag_scope_ = ReorderScope::None;
  auto drag_session = row_drag_session_;
  ClearRowDragSession();
  if (GetCapture() == hwnd_) {
    ReleaseCapture();
  }

  if (!was_active) {
    return;
  }

  if (cancel || controller_ == nullptr || drag_index < 0 || drop_index < 0 || item_count_ <= 1 || scope == ReorderScope::None) {
    InvalidateListRegion();
    return;
  }

  if (!drag_session.has_value()) {
    InvalidateListRegion();
    return;
  }
  std::vector<std::int64_t> ordered_ids;
  ordered_ids.reserve(drag_session->ordered_indexes.size() + 1);
  for (int slot = 0; slot <= static_cast<int>(drag_session->ordered_indexes.size()); ++slot) {
    if (slot == drag_session->desired_slot) {
      const auto* dragged_summary = GetSummaryAt(drag_index);
      if (dragged_summary != nullptr) {
        ordered_ids.push_back(dragged_summary->id);
      }
    }
    if (slot < static_cast<int>(drag_session->ordered_indexes.size())) {
      const auto* summary = GetSummaryAt(drag_session->ordered_indexes[slot]);
      if (summary != nullptr) {
        ordered_ids.push_back(summary->id);
      }
    }
  }
  std::vector<std::int64_t> original_ids;
  original_ids.reserve(drag_session->original_ordered_indexes.size() + 1);
  for (int slot = 0; slot <= static_cast<int>(drag_session->original_ordered_indexes.size()); ++slot) {
    if (slot == drag_session->original_gap_slot) {
      const auto* dragged_summary = GetSummaryAt(drag_index);
      if (dragged_summary != nullptr) {
        original_ids.push_back(dragged_summary->id);
      }
    }
    if (slot < static_cast<int>(drag_session->original_ordered_indexes.size())) {
      const auto* summary = GetSummaryAt(drag_session->original_ordered_indexes[slot]);
      if (summary != nullptr) {
        original_ids.push_back(summary->id);
      }
    }
  }
  if (ordered_ids.empty() || ordered_ids.size() != original_ids.size()) {
    InvalidateListRegion();
    return;
  }
  const bool changed = ordered_ids != original_ids;
  if (!changed) {
    InvalidateListRegion();
    return;
  }

  for (size_t i = 0; i < ordered_ids.size(); ++i) {
    const int sort_order = static_cast<int>((i + 1) * 10);
    switch (scope) {
      case ReorderScope::Queue:
        controller_->ReorderQueue(ordered_ids[i], sort_order);
        break;
      case ReorderScope::Favorites:
        controller_->ReorderFavorites(ordered_ids[i], sort_order);
        break;
      case ReorderScope::Tag:
        controller_->ReorderTagItems(current_tag_id_, ordered_ids[i], sort_order);
        break;
      case ReorderScope::Pinned:
        controller_->ReorderPinned(ordered_ids[i], sort_order);
        break;
      case ReorderScope::None:
      default:
        break;
    }
  }

  switch (scope) {
    case ReorderScope::Queue:
      RefreshStatus(L"Queue order updated");
      break;
    case ReorderScope::Favorites:
      RefreshStatus(L"Favorite order updated");
      break;
    case ReorderScope::Tag:
      RefreshStatus(L"Tag order updated");
      break;
    case ReorderScope::Pinned:
      RefreshStatus(L"Pinned order updated");
      break;
    case ReorderScope::None:
    default:
      break;
  }
  RefreshData(true);
}


}  // namespace clipd2d::ui
