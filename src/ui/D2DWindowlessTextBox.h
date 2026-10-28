#pragma once

#ifndef UNICODE
#define UNICODE
#endif

#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <string>
#include <vector>
#include <functional>
#include "ui/text/GapBuffer.h"

/**
 * @brief 工业级无窗口 Direct2D + DirectWrite 文本输入框组件
 * 支持：行内中文输入法跟随及拼音透明渲染、下划线指示器、选区高亮、
 * 剪贴板复制/剪切/粘贴、本地撤销/重做快照栈、双击选词、圆角关闭按钮与 Hover 手指光标。
 */
class D2DWindowlessTextBox {
public:
    D2DWindowlessTextBox();
    ~D2DWindowlessTextBox();

    // ── 1. 初始化与布局 ──────────────────────────────────────────────────
    /**
     * @brief 初始化控件绑定的 DirectWrite 工厂
     */
    bool Initialize(IDWriteFactory* dwriteFactory);

    /**
     * @brief 设置输入框在宿主 D2D 视口中的物理坐标矩形
     */
    void SetRect(const D2D1_RECT_F& rect);
    
    /**
     * @brief 获取当前坐标矩形
     */
    D2D1_RECT_F GetRect() const { return rect_; }

    // ── 2. 样式设定 ──────────────────────────────────────────────────────
    void SetText(const std::wstring& text);
    std::wstring GetText() const { return textBuffer_.GetText(); }
    
    void SetPlaceholder(const std::wstring& placeholder);
    void SetFontSize(float size);
    void SetTextColor(const D2D1_COLOR_F& color);
    void SetPlaceholderColor(const D2D1_COLOR_F& color);
    void SetFontFamily(const std::wstring& family);
    void SetSelectionColor(const D2D1_COLOR_F& color);
    void SetBorderColor(const D2D1_COLOR_F& color);
    void SetMultiLine(bool multiLine);
    bool IsMultiLine() const { return isMultiLine_; }
    float GetScrollOffsetX() const { return scrollOffsetX_; }
    float GetScrollOffsetY() const { return scrollOffsetY_; }

    // ── 3. 焦点状态 ──────────────────────────────────────────────────────
    /**
     * @brief 设置焦点状态并自动更新 IME 关联的上下文属性
     */
    void SetFocused(bool focused, HWND hwnd = nullptr);
    bool IsFocused() const { return isFocused_; }
    void Reset();        // 清空内容与选区状态
    void SelectAll();    // 全选文本

    // ── 4. 核心事件路由（在宿主 WndProc 中直接被转发调用） ──────────────────
    /**
     * @brief 处理所有的键盘与 IME 消息路由。
     * @return 若消息被文本框处理并拦截，返回 true；否则返回 false 供父窗口继续分发。
     */
    bool HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    /**
     * @brief 处理鼠标点击与移动拖拽框选。
     * @return 若点击落在文本框或关闭按钮的有效响应区域内，返回 true；否则返回 false。
     */
    bool HandleMouseInput(HWND hwnd, UINT msg, D2D1_POINT_2F clientPt);

    // ── 5. 自定义图形绘制 ────────────────────────────────────────────────
    /**
     * @brief 绘制输入框内容（包含背景填充、Placeholder、正文、行内拼音、下划线、选区、Caret 及右上角关闭按钮）
     */
    void Draw(ID2D1RenderTarget* context);

    // ── 6. 外部关联回调与增强特性 API ──────────────────────────────────────
    void SetOnTextChanged(std::function<void(const std::wstring&)> cb) { onTextChanged_ = cb; }
    void SetOnEnterPressed(std::function<void()> cb) { onEnterPressed_ = cb; }
    void SetCharFilter(std::function<bool(wchar_t)> cb) { charFilter_ = cb; }
    void SetOnSelectionChanged(std::function<void(size_t, size_t)> cb) { onSelectionChanged_ = cb; }

    void SetPasswordMode(bool enable);
    bool IsPasswordMode() const { return isPasswordMode_; }

    void SetReadOnly(bool readOnly) { isReadOnly_ = readOnly; }
    bool IsReadOnly() const { return isReadOnly_; }

    void SetDisabled(bool disabled);
    bool IsDisabled() const { return isDisabled_; }

    struct CaretMetrics {
        D2D1_POINT_2F position;
        float height;
    };
    CaretMetrics GetCaretMetrics() const;

    void SetLineSpacing(float lineSpacing);
    float GetLineSpacing() const { return lineSpacing_; }

    void AppendText(const std::wstring& chunk, bool autoScroll = true);

    void SetDrawBorder(bool draw) { drawBorder_ = draw; }
    bool GetDrawBorder() const { return drawBorder_; }
    void SetDrawCloseButton(bool draw) { drawCloseButton_ = draw; }
    bool GetDrawCloseButton() const { return drawCloseButton_; }
    void SetDrawKnob(bool draw) { drawKnob_ = draw; }
    bool GetDrawKnob() const { return drawKnob_; }

private:
    // DWrite 渲染工厂与缓存刷子
    Microsoft::WRL::ComPtr<IDWriteFactory> dwriteFactory_;
    
    // 文本属性与样式参数
    D2D1_RECT_F rect_{};
    wield::text::GapBuffer textBuffer_;
    std::wstring placeholder_;
    float fontSize_ = 14.0f;
    D2D1_COLOR_F textColor_ = D2D1::ColorF(D2D1::ColorF::White);
    D2D1_COLOR_F placeholderColor_ = D2D1::ColorF(0.6f, 0.6f, 0.6f, 1.0f);
    D2D1_COLOR_F selectionColor_ = D2D1::ColorF(0.0f, 0.47f, 0.83f, 0.28f);
    D2D1_COLOR_F borderColor_ = D2D1::ColorF(0.3f, 0.3f, 0.3f, 1.0f);
    std::wstring fontFamily_ = L"Segoe UI";
    float lineSpacing_ = 1.15f;
    
    // 编辑器内部核心状态
    bool isFocused_ = false;
    size_t caretIndex_ = 0;
    size_t selectionAnchor_ = 0;
    size_t selectionActive_ = 0;
    bool isDragSelecting_ = false;
    bool caretVisible_ = true;
    
    // 拼音输入法状态 (IME Inline Composition String)
    std::wstring imeCompositionString_;
    
    // 本地撤销与重做增量动作栈
    struct TextAction {
        bool isInsert;
        size_t index;
        std::wstring text;
        size_t caretBefore;
        size_t caretAfter;
        size_t selectionAnchorBefore;
        size_t selectionActiveBefore;
    };
    std::vector<TextAction> undoStack_;
    std::vector<TextAction> redoStack_;

    // 事件触发器与特征控制
    std::function<void(const std::wstring&)> onTextChanged_;
    std::function<void()> onEnterPressed_;
    std::function<bool(wchar_t)> charFilter_;
    std::function<void(size_t, size_t)> onSelectionChanged_;

    bool isPasswordMode_ = false;
    bool isReadOnly_ = false;
    bool isDisabled_ = false;
    void* hSavedIMC_ = nullptr;

    // 缓存的格式与布局资源 (避免每帧重复创建)
    Microsoft::WRL::ComPtr<IDWriteTextFormat> textFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextFormat> placeholderFormat_;
    Microsoft::WRL::ComPtr<IDWriteTextLayout> textLayout_;

    // 缓存的 D2D 设备相关绘图资源
    ID2D1RenderTarget* lastRenderTarget_ = nullptr;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> textBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> placeholderBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> selectionBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> borderBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> closeBgBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> closeFgBrush_;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> knobBrush_;

    // 关闭按钮 Hover 状态缓存 (事件驱动)
    bool isCloseHovered_ = false;

    // 滚动与多行标记变量
    bool isMultiLine_ = false;
    float scrollOffsetX_ = 0.0f;
    float scrollOffsetY_ = 0.0f;

    // 窗口与光标闪烁同步变量
    HWND hwndParent_ = nullptr;
    DWORD lastActivityTick_ = 0;
    UINT cachedCaretBlinkTime_ = 500;
    float cachedCaretWidth_ = 1.0f;

    // 鼠标连击状态
    DWORD lastClickTick_ = 0;
    D2D1_POINT_2F lastClickPos_ = { -1.0f, -1.0f };
    int clickCount_ = 0;

    bool drawBorder_ = true;
    bool drawCloseButton_ = true;
    bool drawKnob_ = true;

    // 私有辅助方法
    void ResetCaretBlink();
    void RecordAction(bool isInsert, size_t index, const std::wstring& actionText);
    void DeleteActiveSelection();
    void UpdateLayoutMetrics(HWND hwnd = nullptr);
    void UpdateImePosition(HWND hwnd);
    Microsoft::WRL::ComPtr<IDWriteTextLayout> CreateTextLayout(const std::wstring& content);
    void SelectWord(size_t clickPos);
    void SelectLine(size_t clickPos);
    size_t FindWordBoundaryLeft(size_t start) const;
    size_t FindWordBoundaryRight(size_t start) const;
    bool IsWordChar(wchar_t ch) const;
    void ShowContextMenu(HWND hwnd, int screenX, int screenY);
};
