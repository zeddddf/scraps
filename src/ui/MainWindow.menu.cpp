#include "ui/MainWindowInternal.h"
#include "ui/PanelWindowChrome.h"
#include "core/commands/AppController.Interfaces.h"
#include "core/clipboard/OpenTargetResolver.h"

// 菜单/弹出层：自绘 PopupMenu 基础设施 + 面板菜单 + 命中动作分发 + 覆盖对话框提交。
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
            target->Clear(state->d2d_shell.ColorSurface());
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
        const auto settings = settings_store_ != nullptr ? settings_store_->current_settings() : core::store::AppSettings{};
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
  if (!overlay_dialog_.visible || library_mutator_ == nullptr) {
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
      // P0-1:提交即关对话框（消灭双击重复提交窗口），写库异步；失败改走状态栏
      // 提示（overlay 已关，无法回填内联错误）。
      auto* group_mutator = library_mutator_;
      auto group_id = std::make_shared<std::int64_t>(0);
      CloseOverlayDialog(true);
      PostLibraryMutation(
          [group_mutator, input_text, group_id]() -> common::Status {
            const common::Status status = group_mutator->EnsureGroup(input_text, &*group_id);
            if (status.ok() && *group_id <= 0) {
              return common::Status::Error(common::StatusCode::InternalError, "ensure group returned no id");
            }
            return status;
          },
          [this, related_clip_id, group_id](const common::Status& status) {
            if (!status.ok() || *group_id <= 0) {
              RefreshStatus(L"\u65b0\u5efa\u8d44\u6599\u5939\u5931\u8d25");
              return;
            }
            RefreshGroups();
            if (related_clip_id > 0) {
              MoveClipToGroupById(related_clip_id, *group_id, L"\u5df2\u79fb\u52a8\u5230\u65b0\u8d44\u6599\u5939");
            } else {
              RefreshStatus(L"\u5df2\u65b0\u5efa\u8d44\u6599\u5939");
              SelectGroupById(*group_id);
            }
          });
      return;
    }
    case OverlayDialogAction::RenameGroup: {
      if (input_text.empty()) {
        set_input_error(L"\u8d44\u6599\u5939\u540d\u79f0\u4e0d\u80fd\u4e3a\u7a7a\u3002");
        return;
      }
      auto* group_mutator = library_mutator_;
      CloseOverlayDialog(true);
      PostLibraryMutation(
          [group_mutator, target_id, input_text] { return group_mutator->RenameGroup(target_id, input_text); },
          [this, target_id, input_text](const common::Status& status) {
            if (!status.ok()) {
              RefreshStatus(L"\u91cd\u547d\u540d\u8d44\u6599\u5939\u5931\u8d25");
              return;
            }
            if (state_.current_group_id() == target_id) {
              state_.browser_state_.group_name = input_text;
            }
            RefreshGroups();
            RefreshStatus(L"\u5df2\u91cd\u547d\u540d\u8d44\u6599\u5939");
            RefreshData(true);
          });
      return;
    }
    case OverlayDialogAction::CreateTag: {
      if (input_text.empty()) {
        set_input_error(L"\u6807\u7b7e\u540d\u79f0\u4e0d\u80fd\u4e3a\u7a7a\u3002");
        return;
      }
      auto* tag_mutator = library_mutator_;
      auto tag_id = std::make_shared<std::int64_t>(0);
      CloseOverlayDialog(true);
      PostLibraryMutation(
          [tag_mutator, input_text, selected_color, tag_id]() -> common::Status {
            const common::Status status = tag_mutator->EnsureTag(input_text, selected_color, &*tag_id);
            if (status.ok() && *tag_id <= 0) {
              return common::Status::Error(common::StatusCode::InternalError, "ensure tag returned no id");
            }
            return status;
          },
          [this, related_clip_id, apply_filter_after_submit, tag_mutator, tag_id](const common::Status& status) {
            if (!status.ok() || *tag_id <= 0) {
              RefreshStatus(L"\u65b0\u5efa\u6807\u7b7e\u5931\u8d25");
              return;
            }
            const auto finish = [this, apply_filter_after_submit, related_clip_id, tag_id]() {
              RefreshTags();
              if (apply_filter_after_submit || related_clip_id == 0) {
                core::browser::BrowserCommand select_tag;
                select_tag.kind = core::browser::BrowserCommandKind::SelectTag;
                select_tag.tag_id = *tag_id;
                ApplyBrowserCommand(select_tag);
              }
              RefreshStatus(L"Created tag");
              RefreshData(true);
            };
            if (related_clip_id > 0) {
              const std::int64_t created_tag_id = *tag_id;
              PostLibraryMutation(
                  [tag_mutator, related_clip_id, created_tag_id] { return tag_mutator->AddTag(related_clip_id, created_tag_id); },
                  [this, finish](const common::Status& add_status) {
                    if (!add_status.ok()) {
                      RefreshStatus(L"\u6dfb\u52a0\u6807\u7b7e\u5931\u8d25");
                      return;
                    }
                    finish();
                  });
              return;
            }
            finish();
          });
      return;
    }
    case OverlayDialogAction::RenameTag: {
      if (input_text.empty()) {
        set_input_error(L"\u6807\u7b7e\u540d\u79f0\u4e0d\u80fd\u4e3a\u7a7a\u3002");
        return;
      }
      auto* tag_mutator = library_mutator_;
      CloseOverlayDialog(true);
      PostLibraryMutation(
          [tag_mutator, target_id, input_text, selected_color] { return tag_mutator->RenameTag(target_id, input_text, selected_color); },
          [this, target_id](const common::Status& status) {
            if (!status.ok()) {
              RefreshStatus(L"Rename tag failed");
              return;
            }
            core::browser::BrowserCommand select_tag;
            select_tag.kind = core::browser::BrowserCommandKind::SelectTag;
            select_tag.tag_id = target_id;
            ApplyBrowserCommand(select_tag);
            RefreshTags();
            RefreshStatus(L"Renamed tag");
            RefreshData(true);
          });
      return;
    }
    case OverlayDialogAction::DeleteGroup: {
      auto* group_mutator = library_mutator_;
      CloseOverlayDialog(true);
      PostLibraryMutation(
          [group_mutator, target_id] { return group_mutator->DeleteGroup(target_id); },
          [this, target_id](const common::Status& status) {
            if (!status.ok()) {
              RefreshStatus(L"\u5220\u9664\u8d44\u6599\u5939\u5931\u8d25");
              return;
            }
            if (state_.current_group_id() == target_id) {
              ApplyBrowserCommand(core::browser::BrowserCommand{core::browser::BrowserCommandKind::SetScopeLibrary});
            }
            RefreshGroups();
            RefreshStatus(L"\u5df2\u5220\u9664\u8d44\u6599\u5939");
            RefreshData(true);
          });
      return;
    }
    case OverlayDialogAction::DeleteTag: {
      auto* tag_mutator = library_mutator_;
      CloseOverlayDialog(true);
      PostLibraryMutation(
          [tag_mutator, target_id] { return tag_mutator->DeleteTag(target_id); },
          [this](const common::Status& status) {
            if (!status.ok()) {
              RefreshStatus(L"\u5220\u9664\u6807\u7b7e\u5931\u8d25");
              return;
            }
            ApplyBrowserCommand(core::browser::BrowserCommand{
                core::browser::BrowserCommandKind::ClearTags});
            RefreshTags();
            RefreshStatus(L"Deleted tag");
            RefreshData(true);
          });
      return;
    }
    case OverlayDialogAction::None:
    default:
      CloseOverlayDialog(true);
      return;
  }
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
  const std::vector<PopupMenuItemData> menu = BuildGroupMenuItems(state_.groups_);

  RECT client{};
  GetClientRect(hwnd_, &client);
  const PanelLayout layout = ComputeLayout(client.right, client.bottom);
  const int group_tab_index = static_cast<int>(std::size(kVisibleTabs) - 1);
  POINT point{layout.tabs[group_tab_index].left, layout.tabs[group_tab_index].bottom + 4};
  ClientToScreen(hwnd_, &point);
  const int command = TrackPopupMenuForPanel(PanelMenuKind::Group, menu, point);
  const GroupMenuCommand decoded_command = DecodeGroupMenuCommand(command, state_.groups_.size());

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
      if (decoded_command.group_index < state_.groups_.size()) {
        RenameGroupById(state_.groups_[decoded_command.group_index].id);
      }
      return;
    case GroupMenuCommandKind::DeleteGroup:
      if (decoded_command.group_index < state_.groups_.size()) {
        DeleteGroupById(state_.groups_[decoded_command.group_index].id);
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
      if (library_mutator_ != nullptr) {
        auto* clear_mutator = library_mutator_;
        PostLibraryMutation(
            [clear_mutator] { return clear_mutator->ClearHistory(); },
            [this](const common::Status& status) {
              if (status.ok()) {
                RefreshData(false);
              }
            });
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
    for (const auto& [page_index, clips] : state_.page_cache_) {
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
  date_menu.push_back(MakePopupMenuItem(20, strings::kFilterDateAll, true, state_.current_date_range() == core::store::DateRangeFilter::All));
  date_menu.push_back(MakePopupMenuItem(21, strings::kFilterDateToday, true, state_.current_date_range() == core::store::DateRangeFilter::Today));
  date_menu.push_back(MakePopupMenuItem(22, strings::kFilterDateLast7Days, true, state_.current_date_range() == core::store::DateRangeFilter::Last7Days));
  date_menu.push_back(MakePopupMenuItem(23, strings::kFilterDateLast30Days, true, state_.current_date_range() == core::store::DateRangeFilter::Last30Days));

  std::vector<PopupMenuItemData> tag_menu;
  tag_menu.push_back(MakePopupMenuItem(1100, strings::kFilterTagsAny, true, state_.current_tag_id() == 0));
  if (!state_.tags_.empty()) {
    tag_menu.push_back(MakePopupMenuSeparator());
    UINT tag_command = 1000;
    for (const auto& tag : state_.tags_) {
      tag_menu.push_back(MakePopupMenuItem(tag_command++, tag.name, true, state_.current_tag_id() == tag.id));
    }
  }
  tag_menu.push_back(MakePopupMenuSeparator());
  tag_menu.push_back(MakePopupMenuItem(1200, strings::kTagCreate));
  if (state_.current_tag_id() > 0) {
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
      } else if (command >= 1000 && command < 1000 + static_cast<int>(state_.tags_.size())) {
        const size_t index = static_cast<size_t>(command - 1000);
        core::browser::BrowserCommand select_tag;
        select_tag.kind = core::browser::BrowserCommandKind::SelectTag;
        select_tag.tag_id = state_.tags_[index].id;
        ApplyBrowserCommand(select_tag);
        RefreshData(false);
      }
      break;
  }
}

void MainWindow::OpenSelectedContextMenu(POINT screen_point) {
  EnsureSelectedClipLoaded();
  const auto* summary = GetSelectedSummary();
  if (summary == nullptr) {
    return;
  }

  std::vector<PopupMenuItemData> menu;
  UINT command = 100;
  if (!state_.actions_.empty()) {
    for (const auto& action : state_.actions_) {
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
  if (history_feed_ != nullptr) {
    history_feed_->ListClipTags(summary->id, &clip_tags);
  }
  bool in_queue = false;
  if (history_feed_ != nullptr) {
    std::vector<core::store::QueueEntry> queue_entries;
    if (history_feed_->ListQueue(&queue_entries).ok()) {
      in_queue = std::any_of(queue_entries.begin(), queue_entries.end(), [summary](const core::store::QueueEntry& entry) {
        return entry.clip_id == summary->id;
      });
    }
  }
  if (state_.tags_.empty()) {
    tag_menu.push_back(MakePopupMenuItem(0, L"\u6682\u65e0\u6807\u7b7e", false));
  } else {
    UINT tag_command = 2000;
    for (const auto& tag : state_.tags_) {
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
  if (!state_.groups_.empty()) {
    folder_menu.push_back(MakePopupMenuSeparator());
    UINT folder_command = move_to_group_command_base;
    for (const auto& group : state_.groups_) {
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

  if (selected_command >= 100 && selected_command < 100 + state_.actions_.size()) {
    PerformActionById(state_.actions_[selected_command - 100].id);
    return;
  }
  switch (selected_command) {
    case 100:
      if (state_.actions_.empty()) {
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
      if (library_mutator_ != nullptr) {
        const std::int64_t queued_id = summary->id;
        auto* queue_mutator = library_mutator_;
        PostLibraryMutation(
            [queue_mutator, queued_id] { return queue_mutator->AddToQueue(queued_id); },
            [this](const common::Status& status) {
              if (status.ok()) {
                RefreshStatus(L"\u5df2\u52a0\u5165\u961f\u5217");
                RefreshData(true);
              }
            });
      }
      break;
    case remove_from_queue_command:
      if (library_mutator_ != nullptr) {
        const std::int64_t queued_id = summary->id;
        auto* queue_mutator = library_mutator_;
        PostLibraryMutation(
            [queue_mutator, queued_id] { return queue_mutator->RemoveFromQueue(queued_id); },
            [this](const common::Status& status) {
              if (status.ok()) {
                RefreshStatus(L"\u5df2\u79fb\u51fa\u961f\u5217");
                RefreshData(true);
              }
            });
      }
      break;
    case move_queue_front_command:
      if (library_mutator_ != nullptr) {
        const std::int64_t queued_id = summary->id;
        auto* queue_mutator = library_mutator_;
        PostLibraryMutation(
            [queue_mutator, queued_id] { return queue_mutator->MoveQueueToFront(queued_id); },
            [this](const common::Status& status) {
              if (status.ok()) {
                RefreshStatus(L"Set as next queue item");
                RefreshData(true);
              }
            });
      }
      break;
    case move_to_library_command:
      if (library_mutator_ != nullptr) {
        const std::int64_t moved_id = summary->id;
        auto* tier_mutator = library_mutator_;
        PostLibraryMutation(
            [tier_mutator, moved_id] { return tier_mutator->SetStorageTier(moved_id, core::store::StorageTier::Library); },
            [this](const common::Status& status) {
              if (status.ok()) {
                RefreshStatus(L"\u5df2\u52a0\u5165\u8d44\u6599\u5e93");
                RefreshData(true);
              }
            });
      }
      break;
    case move_to_history_command:
      if (library_mutator_ != nullptr) {
        const std::int64_t moved_id = summary->id;
        auto* tier_mutator = library_mutator_;
        PostLibraryMutation(
            [tier_mutator, moved_id] { return tier_mutator->SetStorageTier(moved_id, core::store::StorageTier::History); },
            [this](const common::Status& status) {
              if (status.ok()) {
                RefreshStatus(L"\u5df2\u79fb\u56de\u5386\u53f2");
                RefreshData(true);
              }
            });
      }
      break;
    case move_to_library_root_command:
      if (library_mutator_ != nullptr) {
        const std::int64_t moved_id = summary->id;
        auto* move_mutator = library_mutator_;
        PostLibraryMutation(
            [move_mutator, moved_id] { return move_mutator->MoveClipsToLibraryRoot({moved_id}); },
            [this](const common::Status& status) {
              if (status.ok()) {
                RefreshStatus(L"\u5df2\u79fb\u52a8\u5230\u5168\u90e8\u8d44\u6599");
                RefreshData(true);
              }
            });
      }
      break;
    case export_command:
      if (maintenance_ != nullptr) {
        wchar_t temp_path[MAX_PATH]{};
        GetTempPathW(MAX_PATH, temp_path);
        std::wstring export_path = std::wstring(temp_path) + L"Scraps_" + std::to_wstring(summary->id);
        std::wstring exported_path;
        const common::Status export_status = maintenance_->ExportClipToFile(summary->id, export_path, &exported_path);
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
                 selected_command < move_to_group_command_base + static_cast<UINT>(state_.groups_.size()) &&
                 library_mutator_ != nullptr) {
        const size_t group_index = static_cast<size_t>(selected_command - move_to_group_command_base);
        MoveClipToGroupByIndex(summary->id, group_index);
      } else if (selected_command >= 2000 && selected_command < 2000 + static_cast<UINT>(state_.tags_.size()) && library_mutator_ != nullptr) {
        const size_t tag_index = static_cast<size_t>(selected_command - 2000);
        const bool already_tagged =
            std::any_of(clip_tags.begin(), clip_tags.end(), [this, tag_index](const core::store::TagRecord& clip_tag) {
              return clip_tag.id == state_.tags_[tag_index].id;
            });
        const std::int64_t tagged_clip_id = summary->id;
        const std::int64_t toggle_tag_id = state_.tags_[tag_index].id;
        auto* tag_mutator = library_mutator_;
        PostLibraryMutation(
            [tag_mutator, already_tagged, tagged_clip_id, toggle_tag_id] {
              return already_tagged
                  ? tag_mutator->RemoveTag(tagged_clip_id, toggle_tag_id)
                  : tag_mutator->AddTag(tagged_clip_id, toggle_tag_id);
            },
            [this, already_tagged](const common::Status& tag_status) {
              if (tag_status.ok()) {
                RefreshStatus(already_tagged ? L"\u5df2\u79fb\u9664\u6807\u7b7e" : L"\u5df2\u6dfb\u52a0\u6807\u7b7e");
                RefreshData(true);
              }
            });
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

}  // namespace clipd2d::ui
