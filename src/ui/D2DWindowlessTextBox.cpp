#include "D2DWindowlessTextBox.h"
#include <windows.h>
#include <windowsx.h>
#include <imm.h>
#include <algorithm>

#pragma comment(lib, "imm32.lib")
#pragma comment(lib, "dwrite.lib")

D2DWindowlessTextBox::D2DWindowlessTextBox() {
    fontSize_ = 28.0f; // Premium high-DPI friendly default font size
}

D2DWindowlessTextBox::~D2DWindowlessTextBox() {
    if (hwndParent_) {
        KillTimer(hwndParent_, 0x5C4C0001);
        if (hSavedIMC_) {
            ImmAssociateContext(hwndParent_, reinterpret_cast<HIMC>(hSavedIMC_));
            hSavedIMC_ = nullptr;
        }
    }
}

bool D2DWindowlessTextBox::Initialize(IDWriteFactory* dwriteFactory) {
    dwriteFactory_ = dwriteFactory;
    return dwriteFactory_ != nullptr;
}

void D2DWindowlessTextBox::SetRect(const D2D1_RECT_F& rect) {
    rect_ = rect;
    UpdateLayoutMetrics();
}

void D2DWindowlessTextBox::SetText(const std::wstring& text) {
    textBuffer_.Clear();
    textBuffer_.Insert(0, text);
    caretIndex_ = textBuffer_.GetLength();
    selectionAnchor_ = caretIndex_;
    selectionActive_ = caretIndex_;
    undoStack_.clear();
    redoStack_.clear();
    scrollOffsetX_ = 0.0f;
    scrollOffsetY_ = 0.0f;
    UpdateLayoutMetrics();
    if (onSelectionChanged_) {
        onSelectionChanged_(selectionAnchor_, selectionActive_);
    }
}

void D2DWindowlessTextBox::SetPlaceholder(const std::wstring& placeholder) {
    placeholder_ = placeholder;
}

void D2DWindowlessTextBox::SetFontSize(float size) {
    fontSize_ = size;
    textFormat_.Reset(); // Reset format so it gets rebuilt with the new size
    placeholderFormat_.Reset();
    UpdateLayoutMetrics();
}

void D2DWindowlessTextBox::SetTextColor(const D2D1_COLOR_F& color) {
    textColor_ = color;
    textBrush_.Reset();
}

void D2DWindowlessTextBox::SetPlaceholderColor(const D2D1_COLOR_F& color) {
    placeholderColor_ = color;
    placeholderBrush_.Reset();
}

void D2DWindowlessTextBox::SetFontFamily(const std::wstring& family) {
    fontFamily_ = family;
    textFormat_.Reset(); // Reset format so it gets rebuilt with the new font family
    placeholderFormat_.Reset();
    UpdateLayoutMetrics();
}

void D2DWindowlessTextBox::SetSelectionColor(const D2D1_COLOR_F& color) {
    selectionColor_ = color;
    selectionBrush_.Reset();
}

void D2DWindowlessTextBox::SetBorderColor(const D2D1_COLOR_F& color) {
    borderColor_ = color;
    borderBrush_.Reset();
}

void D2DWindowlessTextBox::SetMultiLine(bool multiLine) {
    if (isMultiLine_ == multiLine) return;
    isMultiLine_ = multiLine;
    textFormat_.Reset(); // Rebuild format to update word wrapping property
    UpdateLayoutMetrics();
}

void D2DWindowlessTextBox::SetFocused(bool focused, HWND hwnd) {
    if (hwnd) {
        hwndParent_ = hwnd;
    }
    if (isFocused_ == focused) return;
    isFocused_ = focused;
    if (!isFocused_) {
        imeCompositionString_ = L"";
        selectionAnchor_ = caretIndex_;
        selectionActive_ = caretIndex_;
        if (hwndParent_) {
            KillTimer(hwndParent_, 0x5C4C0001);
            if (hSavedIMC_) {
                ImmAssociateContext(hwndParent_, reinterpret_cast<HIMC>(hSavedIMC_));
                hSavedIMC_ = nullptr;
            }
        }
    } else {
        if (hwndParent_) {
            if (isPasswordMode_) {
                if (!hSavedIMC_) {
                    hSavedIMC_ = reinterpret_cast<void*>(ImmAssociateContext(hwndParent_, NULL));
                }
            } else {
                HIMC hIMC = ImmGetContext(hwndParent_);
                if (hIMC) {
                    ImmAssociateContext(hwndParent_, hIMC);
                    ImmReleaseContext(hwndParent_, hIMC);
                }
            }
            UpdateImePosition(hwndParent_);
            
            // Cache system caret settings
            cachedCaretBlinkTime_ = GetCaretBlinkTime();
            DWORD systemCaretWidth = 1;
            if (SystemParametersInfoW(SPI_GETCARETWIDTH, 0, &systemCaretWidth, 0) && systemCaretWidth >= 1) {
                cachedCaretWidth_ = static_cast<float>(systemCaretWidth);
            } else {
                cachedCaretWidth_ = 1.0f;
            }
            
            ResetCaretBlink();
        }
    }
}

void D2DWindowlessTextBox::Reset() {
    textBuffer_.Clear();
    caretIndex_ = 0;
    selectionAnchor_ = 0;
    selectionActive_ = 0;
    imeCompositionString_.clear();
    undoStack_.clear();
    redoStack_.clear();
    textLayout_.Reset();
    scrollOffsetX_ = 0.0f;
    scrollOffsetY_ = 0.0f;
    if (onSelectionChanged_) {
        onSelectionChanged_(selectionAnchor_, selectionActive_);
    }
}

void D2DWindowlessTextBox::SelectAll() {
    selectionAnchor_ = 0;
    selectionActive_ = textBuffer_.GetLength();
    caretIndex_ = textBuffer_.GetLength();
    UpdateLayoutMetrics();
    if (onSelectionChanged_) {
        onSelectionChanged_(selectionAnchor_, selectionActive_);
    }
}

bool D2DWindowlessTextBox::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (hwnd) {
        hwndParent_ = hwnd;
    }
    if (isDisabled_) return false;

    struct SelectionScopeGuard {
        D2DWindowlessTextBox* self;
        size_t prevAnchor;
        size_t prevActive;
        size_t prevCaret;
        SelectionScopeGuard(D2DWindowlessTextBox* s)
            : self(s), prevAnchor(s->selectionAnchor_), prevActive(s->selectionActive_), prevCaret(s->caretIndex_) {}
        ~SelectionScopeGuard() {
            if (self->onSelectionChanged_ && 
                (self->selectionAnchor_ != prevAnchor || 
                 self->selectionActive_ != prevActive || 
                 self->caretIndex_ != prevCaret)) {
                self->onSelectionChanged_(self->selectionAnchor_, self->selectionActive_);
            }
        }
    } guard(this);
    
    if (msg == WM_TIMER && wParam == 0x5C4C0001) {
        return true;
    }

    if (msg == WM_MOUSEWHEEL) {
        POINT ptCursor;
        bool shouldScroll = false;
        if (GetCursorPos(&ptCursor)) {
            ScreenToClient(hwnd, &ptCursor);
            shouldScroll = (ptCursor.x >= rect_.left && ptCursor.x <= rect_.right &&
                            ptCursor.y >= rect_.top && ptCursor.y <= rect_.bottom);
        }
        if (shouldScroll || isFocused_) {
            short delta = GET_WHEEL_DELTA_WPARAM(wParam);
            float scrollAmount = (static_cast<float>(delta) / WHEEL_DELTA) * 3.0f * fontSize_;
            
            if (isMultiLine_) {
                float physicalHeight = rect_.bottom - rect_.top;
                if (physicalHeight < 10.0f) physicalHeight = fontSize_ + 8.0f;
                
                DWRITE_TEXT_METRICS metrics = {};
                if (textLayout_ && SUCCEEDED(textLayout_->GetMetrics(&metrics))) {
                    scrollOffsetY_ -= scrollAmount;
                    if (metrics.height < physicalHeight) {
                        scrollOffsetY_ = 0.0f;
                    } else {
                        scrollOffsetY_ = std::clamp(scrollOffsetY_, 0.0f, metrics.height - physicalHeight);
                    }
                }
            } else {
                float viewportW = rect_.right - rect_.left;
                DWRITE_TEXT_METRICS metrics = {};
                if (textLayout_ && SUCCEEDED(textLayout_->GetMetrics(&metrics))) {
                    scrollOffsetX_ -= scrollAmount;
                    if (metrics.width < viewportW) {
                        scrollOffsetX_ = 0.0f;
                    } else {
                        scrollOffsetX_ = std::clamp(scrollOffsetX_, 0.0f, metrics.width - viewportW);
                    }
                }
            }
            if (hwnd) {
                UpdateImePosition(hwnd);
            }
            return true;
        }
    }

    if (msg == WM_CONTEXTMENU) {
        int x = GET_X_LPARAM(lParam);
        int y = GET_Y_LPARAM(lParam);
        bool trigger = false;
        
        if (x == -1 && y == -1) {
            if (isFocused_) {
                float caretX = rect_.left - scrollOffsetX_;
                float caretY = rect_.top - scrollOffsetY_;
                if (textLayout_ && caretIndex_ > 0) {
                    float localX = 0, localY = 0;
                    DWRITE_HIT_TEST_METRICS metrics = {};
                    UINT32 pos = static_cast<UINT32>(caretIndex_ - 1);
                    if (SUCCEEDED(textLayout_->HitTestTextPosition(pos, TRUE, &localX, &localY, &metrics))) {
                        caretX = rect_.left - scrollOffsetX_ + localX;
                        caretY = rect_.top - scrollOffsetY_ + localY;
                    }
                }
                POINT ptCaret = { static_cast<LONG>(caretX), static_cast<LONG>(caretY + fontSize_) };
                ClientToScreen(hwnd, &ptCaret);
                x = ptCaret.x;
                y = ptCaret.y;
                trigger = true;
            }
        } else {
            POINT pt = { x, y };
            ScreenToClient(hwnd, &pt);
            if (pt.x >= rect_.left && pt.x <= rect_.right && pt.y >= rect_.top && pt.y <= rect_.bottom) {
                if (!isFocused_) {
                    SetFocused(true, hwnd);
                }
                trigger = true;
            }
        }
        
        if (trigger) {
            ShowContextMenu(hwnd, x, y);
            return true;
        }
    }

    if (!isFocused_) return false;

    if (msg == WM_CHAR) {
        if (isReadOnly_) {
            wchar_t ch = static_cast<wchar_t>(wParam);
            if (ch == L'\b' || ch == L'\r' || ch == L'\n' || ch == L'\t' || ch >= 32) {
                return true;
            }
            return false;
        }

        wchar_t ch = static_cast<wchar_t>(wParam);
        if (ch >= 32 && charFilter_ && !charFilter_(ch)) {
            return true;
        }
        
        if (ch == L'\b') { // Backspace
            if (selectionAnchor_ != selectionActive_) {
                size_t start = std::min(selectionAnchor_, selectionActive_);
                size_t end = std::max(selectionAnchor_, selectionActive_);
                std::wstring deleted = textBuffer_.GetText().substr(start, end - start);
                RecordAction(false, start, deleted);
                DeleteActiveSelection();
                UpdateLayoutMetrics(hwnd);
                caretVisible_ = true;
            } else if (caretIndex_ > 0 && textBuffer_.GetLength() > 0) {
                wchar_t delChar = textBuffer_.GetCharAt(caretIndex_ - 1);
                RecordAction(false, caretIndex_ - 1, std::wstring(1, delChar));
                textBuffer_.Delete(caretIndex_ - 1, 1);
                caretIndex_--;
                selectionAnchor_ = caretIndex_;
                selectionActive_ = caretIndex_;
                UpdateLayoutMetrics(hwnd);
                caretVisible_ = true;
                if (onTextChanged_) onTextChanged_(textBuffer_.GetText());
            }
        } else if (ch == L'\r' || ch == L'\n') { // Enter
            if (selectionAnchor_ != selectionActive_) {
                size_t start = std::min(selectionAnchor_, selectionActive_);
                size_t end = std::max(selectionAnchor_, selectionActive_);
                std::wstring deleted = textBuffer_.GetText().substr(start, end - start);
                RecordAction(false, start, deleted);
                DeleteActiveSelection();
            }
            if (onEnterPressed_) {
                onEnterPressed_();
            } else {
                RecordAction(true, caretIndex_, L"\n");
                textBuffer_.Insert(caretIndex_, L"\n");
                caretIndex_++;
                selectionAnchor_ = caretIndex_;
                selectionActive_ = caretIndex_;
                UpdateLayoutMetrics(hwnd);
                caretVisible_ = true;
                if (onTextChanged_) onTextChanged_(textBuffer_.GetText());
            }
        } else if (ch == L'\x1B') { // Escape
            SetFocused(false, hwnd);
        } else if (ch == L'\t') { // Tab
            if (selectionAnchor_ != selectionActive_) {
                size_t start = std::min(selectionAnchor_, selectionActive_);
                size_t end = std::max(selectionAnchor_, selectionActive_);
                std::wstring deleted = textBuffer_.GetText().substr(start, end - start);
                RecordAction(false, start, deleted);
                DeleteActiveSelection();
            }
            RecordAction(true, caretIndex_, L"\t");
            textBuffer_.Insert(caretIndex_, L"\t");
            caretIndex_ += 1;
            selectionAnchor_ = caretIndex_;
            selectionActive_ = caretIndex_;
            UpdateLayoutMetrics(hwnd);
            caretVisible_ = true;
            if (onTextChanged_) onTextChanged_(textBuffer_.GetText());
        } else if (ch >= 32) { // Printable character
            if (selectionAnchor_ != selectionActive_) {
                size_t start = std::min(selectionAnchor_, selectionActive_);
                size_t end = std::max(selectionAnchor_, selectionActive_);
                std::wstring deleted = textBuffer_.GetText().substr(start, end - start);
                RecordAction(false, start, deleted);
                DeleteActiveSelection();
            }
            RecordAction(true, caretIndex_, std::wstring(1, ch));
            textBuffer_.Insert(caretIndex_, std::wstring(1, ch));
            caretIndex_++;
            selectionAnchor_ = caretIndex_;
            selectionActive_ = caretIndex_;
            UpdateLayoutMetrics(hwnd);
            caretVisible_ = true;
            if (onTextChanged_) onTextChanged_(textBuffer_.GetText());
        }
        return true;
    }

    if (msg == WM_KEYDOWN) {
        if (isReadOnly_) {
            if (wParam == VK_BACK || wParam == VK_DELETE) {
                return true;
            }
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                if (wParam == 'X' || wParam == 'V' || wParam == 'Z' || wParam == 'Y') {
                    return true;
                }
            }
        }
        if (isPasswordMode_) {
            if (GetKeyState(VK_CONTROL) & 0x8000) {
                if (wParam == 'C' || wParam == 'X') {
                    return true;
                }
            }
        }
        switch (wParam) {
            case VK_LEFT: {
                bool shiftHeld = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                bool ctrlHeld = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                size_t newCaret = caretIndex_;
                if (ctrlHeld) {
                    newCaret = FindWordBoundaryLeft(caretIndex_);
                } else if (caretIndex_ > 0) {
                    newCaret = caretIndex_ - 1;
                }
                caretIndex_ = newCaret;
                if (shiftHeld) {
                    selectionActive_ = caretIndex_;
                } else {
                    selectionAnchor_ = caretIndex_;
                    selectionActive_ = caretIndex_;
                }
                caretVisible_ = true;
                UpdateLayoutMetrics(hwnd);
                return true;
            }
            case VK_RIGHT: {
                bool shiftHeld = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                bool ctrlHeld = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                size_t newCaret = caretIndex_;
                if (ctrlHeld) {
                    newCaret = FindWordBoundaryRight(caretIndex_);
                } else if (caretIndex_ < textBuffer_.GetLength()) {
                    newCaret = caretIndex_ + 1;
                }
                caretIndex_ = newCaret;
                if (shiftHeld) {
                    selectionActive_ = caretIndex_;
                } else {
                    selectionAnchor_ = caretIndex_;
                    selectionActive_ = caretIndex_;
                }
                caretVisible_ = true;
                UpdateLayoutMetrics(hwnd);
                return true;
            }
            case VK_UP: {
                bool shiftHeld = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                if (textLayout_) {
                    float localX = 0, localY = 0;
                    DWRITE_HIT_TEST_METRICS hitMetrics = {};
                    UINT32 pos = (caretIndex_ > 0) ? static_cast<UINT32>(caretIndex_ - 1) : 0;
                    BOOL trail = (caretIndex_ > 0) ? TRUE : FALSE;
                    if (SUCCEEDED(textLayout_->HitTestTextPosition(pos, trail, &localX, &localY, &hitMetrics))) {
                        float targetY = localY - hitMetrics.height / 2.0f - 2.0f;
                        size_t newCaret = 0;
                        if (targetY >= 0.0f) {
                            BOOL isTrailingNew = FALSE;
                            BOOL isInsideNew = FALSE;
                            DWRITE_HIT_TEST_METRICS hitMetricsNew = {};
                            if (SUCCEEDED(textLayout_->HitTestPoint(localX, targetY, &isTrailingNew, &isInsideNew, &hitMetricsNew))) {
                                newCaret = hitMetricsNew.textPosition + (isTrailingNew ? 1 : 0);
                            }
                        } else {
                            newCaret = 0;
                        }
                        caretIndex_ = newCaret;
                        if (shiftHeld) {
                            selectionActive_ = caretIndex_;
                        } else {
                            selectionAnchor_ = caretIndex_;
                            selectionActive_ = caretIndex_;
                        }
                    }
                }
                caretVisible_ = true;
                UpdateLayoutMetrics(hwnd);
                return true;
            }
            case VK_DOWN: {
                bool shiftHeld = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                if (textLayout_) {
                    float localX = 0, localY = 0;
                    DWRITE_HIT_TEST_METRICS hitMetrics = {};
                    UINT32 pos = (caretIndex_ > 0) ? static_cast<UINT32>(caretIndex_ - 1) : 0;
                    BOOL trail = (caretIndex_ > 0) ? TRUE : FALSE;
                    if (SUCCEEDED(textLayout_->HitTestTextPosition(pos, trail, &localX, &localY, &hitMetrics))) {
                        float targetY = localY + hitMetrics.height + 2.0f;
                        size_t newCaret = textBuffer_.GetLength();
                        BOOL isTrailingNew = FALSE;
                        BOOL isInsideNew = FALSE;
                        DWRITE_HIT_TEST_METRICS hitMetricsNew = {};
                        if (SUCCEEDED(textLayout_->HitTestPoint(localX, targetY, &isTrailingNew, &isInsideNew, &hitMetricsNew))) {
                            newCaret = hitMetricsNew.textPosition + (isTrailingNew ? 1 : 0);
                        }
                        caretIndex_ = newCaret;
                        if (shiftHeld) {
                            selectionActive_ = caretIndex_;
                        } else {
                            selectionAnchor_ = caretIndex_;
                            selectionActive_ = caretIndex_;
                        }
                    }
                }
                caretVisible_ = true;
                UpdateLayoutMetrics(hwnd);
                return true;
            }
            case VK_PRIOR: {
                bool shiftHeld = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                if (textLayout_) {
                    float localX = 0, localY = 0;
                    DWRITE_HIT_TEST_METRICS hitMetrics = {};
                    UINT32 pos = (caretIndex_ > 0) ? static_cast<UINT32>(caretIndex_ - 1) : 0;
                    BOOL trail = (caretIndex_ > 0) ? TRUE : FALSE;
                    if (SUCCEEDED(textLayout_->HitTestTextPosition(pos, trail, &localX, &localY, &hitMetrics))) {
                        float physicalHeight = rect_.bottom - rect_.top;
                        float targetY = localY - physicalHeight + hitMetrics.height / 2.0f;
                        size_t newCaret = 0;
                        if (targetY >= 0.0f) {
                            BOOL isTrailingNew = FALSE;
                            BOOL isInsideNew = FALSE;
                            DWRITE_HIT_TEST_METRICS hitMetricsNew = {};
                            if (SUCCEEDED(textLayout_->HitTestPoint(localX, targetY, &isTrailingNew, &isInsideNew, &hitMetricsNew))) {
                                newCaret = hitMetricsNew.textPosition + (isTrailingNew ? 1 : 0);
                            }
                        } else {
                            newCaret = 0;
                        }
                        caretIndex_ = newCaret;
                        if (shiftHeld) {
                            selectionActive_ = caretIndex_;
                        } else {
                            selectionAnchor_ = caretIndex_;
                            selectionActive_ = caretIndex_;
                        }
                    }
                }
                caretVisible_ = true;
                UpdateLayoutMetrics(hwnd);
                return true;
            }
            case VK_NEXT: {
                bool shiftHeld = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                if (textLayout_) {
                    float localX = 0, localY = 0;
                    DWRITE_HIT_TEST_METRICS hitMetrics = {};
                    UINT32 pos = (caretIndex_ > 0) ? static_cast<UINT32>(caretIndex_ - 1) : 0;
                    BOOL trail = (caretIndex_ > 0) ? TRUE : FALSE;
                    if (SUCCEEDED(textLayout_->HitTestTextPosition(pos, trail, &localX, &localY, &hitMetrics))) {
                        float physicalHeight = rect_.bottom - rect_.top;
                        float targetY = localY + physicalHeight + hitMetrics.height / 2.0f;
                        size_t newCaret = textBuffer_.GetLength();
                        BOOL isTrailingNew = FALSE;
                        BOOL isInsideNew = FALSE;
                        DWRITE_HIT_TEST_METRICS hitMetricsNew = {};
                        if (SUCCEEDED(textLayout_->HitTestPoint(localX, targetY, &isTrailingNew, &isInsideNew, &hitMetricsNew))) {
                            newCaret = hitMetricsNew.textPosition + (isTrailingNew ? 1 : 0);
                        }
                        caretIndex_ = newCaret;
                        if (shiftHeld) {
                            selectionActive_ = caretIndex_;
                        } else {
                            selectionAnchor_ = caretIndex_;
                            selectionActive_ = caretIndex_;
                        }
                    }
                }
                caretVisible_ = true;
                UpdateLayoutMetrics(hwnd);
                return true;
            }
            case VK_BACK: {
                bool ctrlHeld = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                if (ctrlHeld) {
                    if (selectionAnchor_ != selectionActive_) {
                        size_t start = std::min(selectionAnchor_, selectionActive_);
                        size_t end = std::max(selectionAnchor_, selectionActive_);
                        std::wstring deleted = textBuffer_.GetText().substr(start, end - start);
                        RecordAction(false, start, deleted);
                        DeleteActiveSelection();
                        UpdateLayoutMetrics(hwnd);
                        caretVisible_ = true;
                    } else if (caretIndex_ > 0) {
                        size_t left = FindWordBoundaryLeft(caretIndex_);
                        if (left < caretIndex_) {
                            std::wstring deleted = textBuffer_.GetText().substr(left, caretIndex_ - left);
                            RecordAction(false, left, deleted);
                            textBuffer_.Delete(left, caretIndex_ - left);
                            caretIndex_ = left;
                            selectionAnchor_ = left;
                            selectionActive_ = left;
                            UpdateLayoutMetrics(hwnd);
                            caretVisible_ = true;
                            if (onTextChanged_) onTextChanged_(textBuffer_.GetText());
                        }
                    }
                    return true;
                }
                break;
            }
            case VK_DELETE: {
                bool ctrlHeld = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                if (selectionAnchor_ != selectionActive_) {
                    size_t start = std::min(selectionAnchor_, selectionActive_);
                    size_t end = std::max(selectionAnchor_, selectionActive_);
                    std::wstring deleted = textBuffer_.GetText().substr(start, end - start);
                    RecordAction(false, start, deleted);
                    DeleteActiveSelection();
                    UpdateLayoutMetrics(hwnd);
                    caretVisible_ = true;
                } else if (caretIndex_ < textBuffer_.GetLength()) {
                    if (ctrlHeld) {
                        size_t right = FindWordBoundaryRight(caretIndex_);
                        if (right > caretIndex_) {
                            std::wstring deleted = textBuffer_.GetText().substr(caretIndex_, right - caretIndex_);
                            RecordAction(false, caretIndex_, deleted);
                            textBuffer_.Delete(caretIndex_, right - caretIndex_);
                            UpdateLayoutMetrics(hwnd);
                            caretVisible_ = true;
                            if (onTextChanged_) onTextChanged_(textBuffer_.GetText());
                        }
                    } else {
                        wchar_t delChar = textBuffer_.GetCharAt(caretIndex_);
                        RecordAction(false, caretIndex_, std::wstring(1, delChar));
                        textBuffer_.Delete(caretIndex_, 1);
                        UpdateLayoutMetrics(hwnd);
                        caretVisible_ = true;
                        if (onTextChanged_) onTextChanged_(textBuffer_.GetText());
                    }
                }
                return true;
            }
            case VK_HOME: {
                bool shiftHeld = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                bool ctrlHeld = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                size_t newCaret = 0;
                
                if (isMultiLine_ && !ctrlHeld && textLayout_) {
                    UINT32 lineCount = 0;
                    textLayout_->GetLineMetrics(nullptr, 0, &lineCount);
                    if (lineCount > 0) {
                        std::vector<DWRITE_LINE_METRICS> lineMetrics(lineCount);
                        if (SUCCEEDED(textLayout_->GetLineMetrics(lineMetrics.data(), lineCount, &lineCount))) {
                            UINT32 lineIndex = lineCount - 1;
                            size_t accumLength = 0;
                            for (UINT32 i = 0; i < lineCount; ++i) {
                                accumLength += lineMetrics[i].length;
                                if (caretIndex_ < accumLength) {
                                    lineIndex = i;
                                    break;
                                }
                            }
                            size_t lineStart = 0;
                            for (UINT32 i = 0; i < lineIndex; ++i) {
                                lineStart += lineMetrics[i].length;
                            }
                            newCaret = lineStart;
                        }
                    }
                } else {
                    newCaret = 0;
                }
                
                caretIndex_ = newCaret;
                if (shiftHeld) {
                    selectionActive_ = caretIndex_;
                } else {
                    selectionAnchor_ = caretIndex_;
                    selectionActive_ = caretIndex_;
                }
                caretVisible_ = true;
                UpdateLayoutMetrics(hwnd);
                return true;
            }
            case VK_END: {
                bool shiftHeld = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
                bool ctrlHeld = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
                size_t newCaret = textBuffer_.GetLength();
                
                if (isMultiLine_ && !ctrlHeld && textLayout_) {
                    UINT32 lineCount = 0;
                    textLayout_->GetLineMetrics(nullptr, 0, &lineCount);
                    if (lineCount > 0) {
                        std::vector<DWRITE_LINE_METRICS> lineMetrics(lineCount);
                        if (SUCCEEDED(textLayout_->GetLineMetrics(lineMetrics.data(), lineCount, &lineCount))) {
                            UINT32 lineIndex = lineCount - 1;
                            size_t accumLength = 0;
                            for (UINT32 i = 0; i < lineCount; ++i) {
                                accumLength += lineMetrics[i].length;
                                if (caretIndex_ < accumLength) {
                                    lineIndex = i;
                                    break;
                                }
                            }
                            size_t lineStart = 0;
                            for (UINT32 i = 0; i < lineIndex; ++i) {
                                lineStart += lineMetrics[i].length;
                            }
                            size_t lineLength = lineMetrics[lineIndex].length;
                            std::wstring fullText = textBuffer_.GetText();
                            while (lineLength > 0) {
                                wchar_t lastCh = fullText[lineStart + lineLength - 1];
                                if (lastCh == L'\n' || lastCh == L'\r') {
                                    lineLength--;
                                } else {
                                    break;
                                }
                            }
                            newCaret = lineStart + lineLength;
                        }
                    }
                } else {
                    newCaret = textBuffer_.GetLength();
                }
                
                caretIndex_ = newCaret;
                if (shiftHeld) {
                    selectionActive_ = caretIndex_;
                } else {
                    selectionAnchor_ = caretIndex_;
                    selectionActive_ = caretIndex_;
                }
                caretVisible_ = true;
                UpdateLayoutMetrics(hwnd);
                return true;
            }
            case 'A': {
                if (GetKeyState(VK_CONTROL) & 0x8000) {
                    SelectAll();
                    if (hwnd) {
                        UpdateImePosition(hwnd);
                    }
                    return true;
                }
                break;
            }
            case 'C': {
                if (GetKeyState(VK_CONTROL) & 0x8000) {
                    if (selectionAnchor_ != selectionActive_) {
                        size_t start = std::min(selectionAnchor_, selectionActive_);
                        size_t end = std::max(selectionAnchor_, selectionActive_);
                        std::wstring selText = textBuffer_.GetText().substr(start, end - start);
                        if (OpenClipboard(hwnd)) {
                            EmptyClipboard();
                            size_t sz = (selText.length() + 1) * sizeof(wchar_t);
                            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, sz);
                            if (hMem) {
                                void* ptr = GlobalLock(hMem);
                                memcpy(ptr, selText.c_str(), sz);
                                GlobalUnlock(hMem);
                                SetClipboardData(CF_UNICODETEXT, hMem);
                            }
                            CloseClipboard();
                        }
                    }
                    return true;
                }
                break;
            }
            case 'X': {
                if (GetKeyState(VK_CONTROL) & 0x8000) {
                    if (selectionAnchor_ != selectionActive_) {
                        size_t start = std::min(selectionAnchor_, selectionActive_);
                        size_t end = std::max(selectionAnchor_, selectionActive_);
                        std::wstring selText = textBuffer_.GetText().substr(start, end - start);
                        RecordAction(false, start, selText);
                        if (OpenClipboard(hwnd)) {
                            EmptyClipboard();
                            size_t sz = (selText.length() + 1) * sizeof(wchar_t);
                            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, sz);
                            if (hMem) {
                                void* ptr = GlobalLock(hMem);
                                memcpy(ptr, selText.c_str(), sz);
                                GlobalUnlock(hMem);
                                SetClipboardData(CF_UNICODETEXT, hMem);
                            }
                            CloseClipboard();
                        }
                        DeleteActiveSelection();
                        UpdateLayoutMetrics(hwnd);
                        caretVisible_ = true;
                    }
                    return true;
                }
                break;
            }
            case 'V': {
                if (GetKeyState(VK_CONTROL) & 0x8000) {
                    if (selectionAnchor_ != selectionActive_) {
                        size_t start = std::min(selectionAnchor_, selectionActive_);
                        size_t end = std::max(selectionAnchor_, selectionActive_);
                        std::wstring deleted = textBuffer_.GetText().substr(start, end - start);
                        RecordAction(false, start, deleted);
                        DeleteActiveSelection();
                    }
                    if (OpenClipboard(hwnd)) {
                        HANDLE hData = GetClipboardData(CF_UNICODETEXT);
                        if (hData) {
                            wchar_t* pText = static_cast<wchar_t*>(GlobalLock(hData));
                            if (pText) {
                                std::wstring pasted(pText);
                                pasted.erase(std::remove(pasted.begin(), pasted.end(), L'\r'), pasted.end());
                                RecordAction(true, caretIndex_, pasted);
                                textBuffer_.Insert(caretIndex_, pasted);
                                caretIndex_ += pasted.length();
                                selectionAnchor_ = caretIndex_;
                                selectionActive_ = caretIndex_;
                                UpdateLayoutMetrics(hwnd);
                                caretVisible_ = true;
                                if (onTextChanged_) onTextChanged_(textBuffer_.GetText());
                            }
                            GlobalUnlock(hData);
                        }
                        CloseClipboard();
                    }
                    return true;
                }
                break;
            }
            case 'Z': {
                if (GetKeyState(VK_CONTROL) & 0x8000) {
                    if (!undoStack_.empty()) {
                        auto action = undoStack_.back();
                        undoStack_.pop_back();
                        
                        if (action.isInsert) {
                            textBuffer_.Delete(action.index, action.text.length());
                        } else {
                            textBuffer_.Insert(action.index, action.text);
                        }
                        
                        caretIndex_ = action.caretBefore;
                        selectionAnchor_ = action.selectionAnchorBefore;
                        selectionActive_ = action.selectionActiveBefore;
                        
                        redoStack_.push_back(action);
                        
                        UpdateLayoutMetrics(hwnd);
                        caretVisible_ = true;
                        if (onTextChanged_) onTextChanged_(textBuffer_.GetText());
                    }
                    return true;
                }
                break;
            }
            case 'Y': {
                if (GetKeyState(VK_CONTROL) & 0x8000) {
                    if (!redoStack_.empty()) {
                        auto action = redoStack_.back();
                        redoStack_.pop_back();
                        
                        if (action.isInsert) {
                            textBuffer_.Insert(action.index, action.text);
                        } else {
                            textBuffer_.Delete(action.index, action.text.length());
                        }
                        
                        caretIndex_ = action.caretAfter;
                        selectionAnchor_ = action.caretAfter;
                        selectionActive_ = action.caretAfter;
                        
                        undoStack_.push_back(action);
                        
                        UpdateLayoutMetrics(hwnd);
                        caretVisible_ = true;
                        if (onTextChanged_) onTextChanged_(textBuffer_.GetText());
                    }
                    return true;
                }
                break;
            }
        }
    }

    if (msg == WM_IME_STARTCOMPOSITION) {
        UpdateImePosition(hwnd);
        return true;
    }

    if (msg == WM_IME_COMPOSITION) {
        if (lParam & GCS_COMPSTR) {
            HIMC hIMC = ImmGetContext(hwnd);
            if (hIMC) {
                LONG size = ImmGetCompositionStringW(hIMC, GCS_COMPSTR, nullptr, 0);
                if (size >= 0) {
                    std::vector<wchar_t> buf(size / sizeof(wchar_t) + 1);
                    ImmGetCompositionStringW(hIMC, GCS_COMPSTR, buf.data(), size);
                    buf[size / sizeof(wchar_t)] = L'\0';
                    imeCompositionString_ = buf.data();
                }
                ImmReleaseContext(hwnd, hIMC);
            }
        }
        if (lParam & GCS_RESULTSTR) {
            imeCompositionString_ = L"";
            UpdateLayoutMetrics(hwnd);
            return false; // Yield to DefWindowProc to trigger WM_CHAR for committed glyphs
        }
        UpdateLayoutMetrics(hwnd);
        UpdateImePosition(hwnd);
        return true;
    }

    if (msg == WM_IME_ENDCOMPOSITION) {
        imeCompositionString_ = L"";
        UpdateLayoutMetrics(hwnd);
        return false;
    }

    return false;
}

bool D2DWindowlessTextBox::HandleMouseInput(HWND hwnd, UINT msg, D2D1_POINT_2F clientPt) {
    if (hwnd) {
        hwndParent_ = hwnd;
    }
    if (isDisabled_) return false;

    struct SelectionScopeGuard {
        D2DWindowlessTextBox* self;
        size_t prevAnchor;
        size_t prevActive;
        size_t prevCaret;
        SelectionScopeGuard(D2DWindowlessTextBox* s)
            : self(s), prevAnchor(s->selectionAnchor_), prevActive(s->selectionActive_), prevCaret(s->caretIndex_) {}
        ~SelectionScopeGuard() {
            if (self->onSelectionChanged_ && 
                (self->selectionAnchor_ != prevAnchor || 
                 self->selectionActive_ != prevActive || 
                 self->caretIndex_ != prevCaret)) {
                self->onSelectionChanged_(self->selectionAnchor_, self->selectionActive_);
            }
        }
    } guard(this);
    float frameRight = rect_.right + 4.0f;
    float frameTop = rect_.top - 4.0f;
    float closeR = 8.0f;
    float dxClose = clientPt.x - frameRight;
    float dyClose = clientPt.y - frameTop;
    bool isOverClose = drawCloseButton_ && (dxClose * dxClose + dyClose * dyClose <= closeR * closeR * 1.5f);

    // Update close button hover state and invalidate background brush on transition
    if (isCloseHovered_ != isOverClose) {
        isCloseHovered_ = isOverClose;
        closeBgBrush_.Reset();
        if (hwnd) {
            InvalidateRect(hwnd, nullptr, FALSE);
        }
    }

    if (isOverClose) {
        if (msg == WM_LBUTTONDOWN) {
            Reset();
            if (onTextChanged_) {
                onTextChanged_(textBuffer_.GetText());
            }
            return true;
        }
        return true;
    }

    bool insideText = (clientPt.x >= rect_.left && clientPt.x <= rect_.right &&
                       clientPt.y >= rect_.top && clientPt.y <= rect_.bottom);

    if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONDBLCLK) {
        if (insideText) {
            SetFocused(true, hwnd);
            if (msg == WM_LBUTTONDOWN) {
                SetCapture(hwnd);
            }

            DWORD currentTick = GetTickCount();
            float dx = clientPt.x - lastClickPos_.x;
            float dy = clientPt.y - lastClickPos_.y;
            bool isMultiClick = (currentTick - lastClickTick_ <= GetDoubleClickTime()) && 
                                (dx * dx + dy * dy <= 100.0f);
            if (isMultiClick) {
                clickCount_++;
            } else {
                clickCount_ = (msg == WM_LBUTTONDBLCLK) ? 2 : 1;
            }
            lastClickTick_ = currentTick;
            lastClickPos_ = clientPt;

            if (textLayout_) {
                BOOL isTrailing = FALSE;
                BOOL isInside = FALSE;
                DWRITE_HIT_TEST_METRICS hitMetrics = {};
                float localX = clientPt.x - rect_.left + scrollOffsetX_;
                float localY = clientPt.y - rect_.top + scrollOffsetY_;
                if (localX < 0.0f) localX = 0.0f;
                if (localY < 0.0f) localY = 0.0f;
                textLayout_->HitTestPoint(localX, localY, &isTrailing, &isInside, &hitMetrics);
                size_t hitIndex = hitMetrics.textPosition + (isTrailing ? 1 : 0);

                int clickType = clickCount_ % 3;
                if (clickType == 0) { // Triple click
                    SelectLine(hitIndex);
                } else if (clickType == 2) { // Double click
                    SelectWord(hitIndex);
                } else { // Single click
                    if (!(GetKeyState(VK_SHIFT) & 0x8000)) {
                        selectionAnchor_ = hitIndex;
                    }
                    selectionActive_ = hitIndex;
                    caretIndex_ = hitIndex;
                }
                
                if (msg == WM_LBUTTONDOWN) {
                    isDragSelecting_ = true;
                }
                ResetCaretBlink();
                UpdateImePosition(hwnd);
            }
            return true;
        } else {
            if (isFocused_ && msg == WM_LBUTTONDOWN) {
                SetFocused(false, hwnd);
            }
        }
    } else if (msg == WM_MOUSEMOVE) {
        if (isDragSelecting_) {
            if (textLayout_) {
                BOOL isTrailing = FALSE;
                BOOL isInside = FALSE;
                DWRITE_HIT_TEST_METRICS hitMetrics = {};
                float localX = clientPt.x - rect_.left + scrollOffsetX_;
                float localY = clientPt.y - rect_.top + scrollOffsetY_;
                if (localX < 0.0f) localX = 0.0f;
                if (localY < 0.0f) localY = 0.0f;
                textLayout_->HitTestPoint(localX, localY, &isTrailing, &isInside, &hitMetrics);
                size_t newCaret = hitMetrics.textPosition + (isTrailing ? 1 : 0);
                selectionActive_ = newCaret;
                caretIndex_ = newCaret;
                ResetCaretBlink();
                UpdateLayoutMetrics(hwnd);
                UpdateImePosition(hwnd);
            }
            return true;
        }
    } else if (msg == WM_LBUTTONUP) {
        if (isDragSelecting_) {
            isDragSelecting_ = false;
            ReleaseCapture();
            return true;
        }
    }

    return insideText;
}

void D2DWindowlessTextBox::Draw(ID2D1RenderTarget* context) {
    if (!context || !dwriteFactory_) return;

    D2D1_TEXT_ANTIALIAS_MODE oldTextMode = context->GetTextAntialiasMode();
    context->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);

    // 1. Device lost or render target switch check
    if (lastRenderTarget_ != context) {
        lastRenderTarget_ = context;
        textBrush_.Reset();
        placeholderBrush_.Reset();
        selectionBrush_.Reset();
        borderBrush_.Reset();
        closeBgBrush_.Reset();
        closeFgBrush_.Reset();
        knobBrush_.Reset();
    }

    // 2. Rebuild brushes if null
    if (!textBrush_) {
        if (isDisabled_) {
            D2D1_COLOR_F disabledText = D2D1::ColorF(textColor_.r, textColor_.g, textColor_.b, textColor_.a * 0.4f);
            context->CreateSolidColorBrush(disabledText, &textBrush_);
        } else {
            context->CreateSolidColorBrush(textColor_, &textBrush_);
        }
    }
    if (!placeholderBrush_) {
        if (isDisabled_) {
            D2D1_COLOR_F disabledPlaceholder = D2D1::ColorF(placeholderColor_.r, placeholderColor_.g, placeholderColor_.b, placeholderColor_.a * 0.4f);
            context->CreateSolidColorBrush(disabledPlaceholder, &placeholderBrush_);
        } else {
            context->CreateSolidColorBrush(placeholderColor_, &placeholderBrush_);
        }
    }
    if (!borderBrush_) {
        if (isDisabled_) {
            D2D1_COLOR_F disabledBorder = D2D1::ColorF(borderColor_.r, borderColor_.g, borderColor_.b, borderColor_.a * 0.3f);
            context->CreateSolidColorBrush(disabledBorder, &borderBrush_);
        } else {
            context->CreateSolidColorBrush(borderColor_, &borderBrush_);
        }
    }
    if (!selectionBrush_) {
        context->CreateSolidColorBrush(selectionColor_, &selectionBrush_);
    }

    D2D1_RECT_F frameRect = D2D1::RectF(
        rect_.left - 4.0f,
        rect_.top - 4.0f,
        rect_.right + 4.0f,
        rect_.bottom + 4.0f
    );
    
    if (isFocused_) {
        if (drawBorder_) {
            context->DrawRectangle(frameRect, borderBrush_.Get(), 1.0f);
        }

        if (drawKnob_) {
            float knobX = rect_.right + 4.0f;
            float knobY = (rect_.top + rect_.bottom) / 2.0f;
            D2D1_ELLIPSE knob = D2D1::Ellipse(D2D1::Point2F(knobX, knobY), 4.0f, 4.0f);
            
            if (!knobBrush_) {
                context->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &knobBrush_);
            }
            if (knobBrush_) {
                context->FillEllipse(&knob, knobBrush_.Get());
            }
            context->DrawEllipse(&knob, borderBrush_.Get(), 1.0f);
        }
    }

    std::wstring contentText = textBuffer_.GetText();
    std::wstring displayText = contentText.substr(0, caretIndex_) + imeCompositionString_ + contentText.substr(caretIndex_);

    context->PushAxisAlignedClip(rect_, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    if (displayText.empty() && !placeholder_.empty()) {
        if (!placeholderFormat_) {
            dwriteFactory_->CreateTextFormat(
                fontFamily_.c_str(),
                nullptr,
                DWRITE_FONT_WEIGHT_NORMAL,
                DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL,
                fontSize_ - 2.0f,
                L"zh-cn",
                &placeholderFormat_
            );
            if (placeholderFormat_) {
                placeholderFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                placeholderFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            }
        }
        if (placeholderFormat_) {
            context->DrawTextW(
                placeholder_.c_str(),
                static_cast<UINT32>(placeholder_.length()),
                placeholderFormat_.Get(),
                &rect_,
                placeholderBrush_.Get()
            );
        }
    } else {
        if (textLayout_) {
            // Draw selection highlight behind text
            if (selectionAnchor_ != selectionActive_ && textBuffer_.GetLength() > 0 && !isDisabled_) {
                size_t start = std::min(selectionAnchor_, selectionActive_);
                size_t end = std::max(selectionAnchor_, selectionActive_);
                if (end > textBuffer_.GetLength()) end = textBuffer_.GetLength();
                if (start < end) {
                    UINT32 selStart = static_cast<UINT32>(start);
                    UINT32 selLen = static_cast<UINT32>(end - start);
                    
                    UINT32 maxRects = 32;
                    std::vector<DWRITE_HIT_TEST_METRICS> hitMetrics(maxRects);
                    UINT32 actualRectCount = 0;
                    
                    if (SUCCEEDED(textLayout_->HitTestTextRange(
                            selStart,
                            selLen,
                            0.0f,
                            0.0f,
                            hitMetrics.data(),
                            maxRects,
                            &actualRectCount))) {
                        
                        for (UINT32 i = 0; i < actualRectCount; ++i) {
                            // Pixel alignment for selection rectangles to ensure sharp boundaries
                            float left = floorf(rect_.left - scrollOffsetX_ + hitMetrics[i].left + 0.5f);
                            float top = floorf(rect_.top - scrollOffsetY_ + hitMetrics[i].top + 0.5f);
                            float right = floorf(rect_.left - scrollOffsetX_ + hitMetrics[i].left + hitMetrics[i].width + 0.5f);
                            float bottom = floorf(rect_.top - scrollOffsetY_ + hitMetrics[i].top + hitMetrics[i].height + 0.5f);
                            
                            D2D1_RECT_F highlightRect = D2D1::RectF(left, top, right, bottom);
                            context->FillRectangle(highlightRect, selectionBrush_.Get());
                        }
                    }
                }
            }

            if (!imeCompositionString_.empty()) {
                UINT32 compStart = static_cast<UINT32>(caretIndex_);
                UINT32 compLen = static_cast<UINT32>(imeCompositionString_.length());
                
                UINT32 maxRects = 16;
                std::vector<DWRITE_HIT_TEST_METRICS> hitMetrics(maxRects);
                UINT32 actualRectCount = 0;
                
                if (SUCCEEDED(textLayout_->HitTestTextRange(
                        compStart,
                        compLen,
                        0.0f,
                        0.0f,
                        hitMetrics.data(),
                        maxRects,
                        &actualRectCount))) {
                    
                    for (UINT32 i = 0; i < actualRectCount; ++i) {
                        // Pixel alignment for composition underlines (exact 1px line positioning)
                        float lineY = floorf(rect_.top - scrollOffsetY_ + hitMetrics[i].top + hitMetrics[i].height - 0.5f);
                        float lineX1 = floorf(rect_.left - scrollOffsetX_ + hitMetrics[i].left + 0.5f);
                        float lineX2 = floorf(rect_.left - scrollOffsetX_ + hitMetrics[i].left + hitMetrics[i].width + 0.5f);
                        
                        context->DrawLine(
                            D2D1::Point2F(lineX1, lineY),
                            D2D1::Point2F(lineX2, lineY),
                            textBrush_.Get(),
                            1.0f
                        );
                    }
                }
            }

            // Pixel alignment for main text rendering starting origin
            float textOriginX = floorf(rect_.left - scrollOffsetX_ + 0.5f);
            float textOriginY = floorf(rect_.top - scrollOffsetY_ + 0.5f);
            context->DrawTextLayout(
                D2D1::Point2F(textOriginX, textOriginY),
                textLayout_.Get(),
                textBrush_.Get()
            );
        }
    }

    // Draw caret
    if (isFocused_ && !isDisabled_ && !isReadOnly_) {
        DWORD currentTick = GetTickCount();
        if (currentTick - lastActivityTick_ < cachedCaretBlinkTime_) {
            caretVisible_ = true;
        } else {
            if (cachedCaretBlinkTime_ == 0) {
                caretVisible_ = ((currentTick / 500) % 2) == 0;
            } else if (cachedCaretBlinkTime_ == INFINITE) {
                caretVisible_ = true;
            } else {
                caretVisible_ = (((currentTick - lastActivityTick_) / cachedCaretBlinkTime_) % 2) == 0;
            }
        }

        if (caretVisible_) {
            float caretX = rect_.left - scrollOffsetX_;
            float caretY = rect_.top - scrollOffsetY_;
            float caretH = fontSize_;
            
            if (!displayText.empty() && textLayout_) {
                DWRITE_TEXT_METRICS layoutMetrics = {};
                if (SUCCEEDED(textLayout_->GetMetrics(&layoutMetrics))) {
                    caretY = rect_.top - scrollOffsetY_ + layoutMetrics.top;
                    caretH = layoutMetrics.height;
                }
                
                DWRITE_HIT_TEST_METRICS metrics = {};
                float localX = 0, localY = 0;
                size_t visualCaret = caretIndex_ + imeCompositionString_.length();
                UINT32 pos = (visualCaret > 0) ? static_cast<UINT32>(visualCaret - 1) : 0;
                BOOL trail = (visualCaret > 0) ? TRUE : FALSE;
                
                if (SUCCEEDED(textLayout_->HitTestTextPosition(pos, trail, &localX, &localY, &metrics))) {
                    caretX = rect_.left - scrollOffsetX_ + localX;
                    caretY = rect_.top - scrollOffsetY_ + localY;
                    caretH = metrics.height;
                }
            } else {
                // When empty, center the caret vertically inside rect_
                float rectH = rect_.bottom - rect_.top;
                caretY = rect_.top + (rectH - caretH) / 2.0f;
            }
            
            // Pixel alignment for vertical caret line horizontal position
            float alignCaretX = floorf(caretX + 0.5f);
            context->DrawLine(
                D2D1::Point2F(alignCaretX, caretY),
                D2D1::Point2F(alignCaretX, caretY + caretH),
                textBrush_.Get(),
                cachedCaretWidth_
            );
        }
    }

    context->PopAxisAlignedClip();

    if (drawCloseButton_ && (isFocused_ || textBuffer_.GetLength() > 0)) {
        float closeR = 8.0f;
        float closeX = frameRect.right;
        float closeY = frameRect.top;
        
        D2D1_ELLIPSE closeCircle = D2D1::Ellipse(D2D1::Point2F(closeX, closeY), closeR, closeR);
        
        if (!closeBgBrush_ || !closeFgBrush_) {
            D2D1_COLOR_F bgColor = isCloseHovered_ ? D2D1::ColorF(0.9f, 0.1f, 0.1f, 0.9f) : D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.6f);
            D2D1_COLOR_F fgColor = D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.00f);
            context->CreateSolidColorBrush(bgColor, &closeBgBrush_);
            context->CreateSolidColorBrush(fgColor, &closeFgBrush_);
        }

        if (closeBgBrush_ && closeFgBrush_) {
            context->FillEllipse(&closeCircle, closeBgBrush_.Get());
            context->DrawEllipse(&closeCircle, borderBrush_.Get(), 0.5f);
            
            float len = 3.0f;
            context->DrawLine(
                D2D1::Point2F(closeX - len, closeY - len),
                D2D1::Point2F(closeX + len, closeY + len),
                closeFgBrush_.Get(),
                1.5f
            );
            context->DrawLine(
                D2D1::Point2F(closeX + len, closeY - len),
                D2D1::Point2F(closeX - len, closeY + len),
                closeFgBrush_.Get(),
                1.5f
            );
        }
    }

    context->SetTextAntialiasMode(oldTextMode);
}

void D2DWindowlessTextBox::ResetCaretBlink() {
    lastActivityTick_ = GetTickCount();
    caretVisible_ = true;
    if (isFocused_ && hwndParent_) {
        if (cachedCaretBlinkTime_ != 0 && cachedCaretBlinkTime_ != INFINITE) {
            SetTimer(hwndParent_, 0x5C4C0001, cachedCaretBlinkTime_, nullptr);
        }
    }
}

void D2DWindowlessTextBox::RecordAction(bool isInsert, size_t index, const std::wstring& actionText) {
    // 1. Group consecutive inserts (typing character by character)
    if (isInsert && !undoStack_.empty()) {
        auto& last = undoStack_.back();
        if (last.isInsert &&
            (last.index + last.text.length()) == index &&
            actionText.length() == 1 &&
            !iswspace(actionText[0]) &&
            !last.text.empty() &&
            !iswspace(last.text.back()) &&
            last.selectionAnchorBefore == last.caretBefore &&
            last.selectionActiveBefore == last.caretBefore &&
            selectionAnchor_ == caretIndex_ &&
            selectionActive_ == caretIndex_) {
            
            last.text += actionText;
            last.caretAfter = index + 1;
            redoStack_.clear();
            return;
        }
    }

    // 2. Group consecutive backspaces (deleting character by character to the left)
    if (!isInsert && !undoStack_.empty()) {
        auto& last = undoStack_.back();
        if (!last.isInsert &&
            last.index == (index + actionText.length()) &&
            actionText.length() == 1 &&
            !iswspace(actionText[0]) &&
            !last.text.empty() &&
            !iswspace(last.text.front()) &&
            last.selectionAnchorBefore == last.caretBefore &&
            last.selectionActiveBefore == last.caretBefore &&
            selectionAnchor_ == caretIndex_ &&
            selectionActive_ == caretIndex_) {
            
            last.text = actionText + last.text;
            last.index = index;
            last.caretAfter = index;
            redoStack_.clear();
            return;
        }
    }

    // 3. Fallback to standard recording
    TextAction action;
    action.isInsert = isInsert;
    action.index = index;
    action.text = actionText;
    action.caretBefore = caretIndex_;
    action.selectionAnchorBefore = selectionAnchor_;
    action.selectionActiveBefore = selectionActive_;
    action.caretAfter = isInsert ? (index + actionText.length()) : index;
    
    undoStack_.push_back(action);
    if (undoStack_.size() > 50) {
        undoStack_.erase(undoStack_.begin());
    }
    redoStack_.clear();
}

void D2DWindowlessTextBox::DeleteActiveSelection() {
    if (selectionAnchor_ == selectionActive_) return;
    size_t start = std::min(selectionAnchor_, selectionActive_);
    size_t end = std::max(selectionAnchor_, selectionActive_);
    if (end > textBuffer_.GetLength()) end = textBuffer_.GetLength();
    if (start < end) {
        textBuffer_.Delete(start, end - start);
    }
    caretIndex_ = start;
    selectionAnchor_ = caretIndex_;
    selectionActive_ = caretIndex_;
    if (onTextChanged_) {
        onTextChanged_(textBuffer_.GetText());
    }
}

void D2DWindowlessTextBox::UpdateLayoutMetrics(HWND hwnd) {
    if (hwnd) {
        hwndParent_ = hwnd;
    }
    ResetCaretBlink();
    if (!dwriteFactory_) return;

    float w = rect_.right - rect_.left;
    if (w < 50.0f) w = 50.0f;

    std::wstring contentText = textBuffer_.GetText();
    std::wstring displayText = contentText.substr(0, caretIndex_) + imeCompositionString_ + contentText.substr(caretIndex_);

    if (isPasswordMode_) {
        std::wstring maskedText = displayText;
        for (auto& ch : maskedText) {
            if (ch != L'\r' && ch != L'\n') {
                ch = L'•';
            }
        }
        displayText = maskedText;
    }

    // 1. Rebuild format if dirty
    if (!textFormat_) {
        dwriteFactory_->CreateTextFormat(
            fontFamily_.c_str(),
            nullptr,
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            fontSize_,
            L"zh-cn",
            &textFormat_
        );
        
        // Config wrapping mode and tab stops / alignment
        if (textFormat_) {
            if (isMultiLine_) {
                textFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
                textFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
                if (lineSpacing_ > 0.0f) {
                    textFormat_->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, fontSize_ * lineSpacing_, fontSize_ * lineSpacing_ * 0.8f);
                }
            } else {
                textFormat_->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
                textFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
            }
            textFormat_->SetIncrementalTabStop(fontSize_ * 2.0f);
        }
    }

    // 2. Rebuild layout
    if (textFormat_) {
        float layoutWidth = isMultiLine_ ? w : 20000.0f; // Single line uses a huge virtual width
        float layoutHeight = isMultiLine_ ? 10000.0f : (rect_.bottom - rect_.top);
        
        if (isMultiLine_) {
            textFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            if (lineSpacing_ > 0.0f) {
                textFormat_->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, fontSize_ * lineSpacing_, fontSize_ * lineSpacing_ * 0.8f);
            }
        } else {
            textFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
        textFormat_->SetIncrementalTabStop(fontSize_ * 2.0f);

        dwriteFactory_->CreateTextLayout(
            displayText.c_str(),
            static_cast<UINT32>(displayText.length()),
            textFormat_.Get(),
            layoutWidth,
            layoutHeight,
            &textLayout_
        );
    }

    // 3. Update heights or vertical/horizontal scroll calculations
    if (textLayout_) {
        DWRITE_TEXT_METRICS metrics = {};
        if (SUCCEEDED(textLayout_->GetMetrics(&metrics))) {
            if (isMultiLine_) {
                float physicalHeight = rect_.bottom - rect_.top;
                if (physicalHeight < 10.0f) physicalHeight = fontSize_ + 8.0f;
                
                // Track caret to update scrollOffsetY_
                float caretY = 0.0f;
                float caretH = fontSize_;
                size_t visualCaret = caretIndex_ + imeCompositionString_.length();
                if (visualCaret > 0 && !displayText.empty()) {
                    DWRITE_HIT_TEST_METRICS careMetrics = {};
                    float cx = 0, cy = 0;
                    UINT32 pos = static_cast<UINT32>(visualCaret - 1);
                    if (SUCCEEDED(textLayout_->HitTestTextPosition(pos, TRUE, &cx, &cy, &careMetrics))) {
                        caretY = cy;
                        caretH = careMetrics.height;
                    }
                }
                
                // Keep caret in view vertically
                if (caretY - scrollOffsetY_ > physicalHeight - caretH - 4.0f) {
                    scrollOffsetY_ = caretY - physicalHeight + caretH + 4.0f;
                }
                if (caretY - scrollOffsetY_ < 4.0f) {
                    scrollOffsetY_ = caretY - 4.0f;
                    if (scrollOffsetY_ < 0.0f) scrollOffsetY_ = 0.0f;
                }
                
                if (metrics.height < physicalHeight) {
                    scrollOffsetY_ = 0.0f;
                } else if (scrollOffsetY_ > metrics.height - physicalHeight) {
                    scrollOffsetY_ = metrics.height - physicalHeight;
                }
                
                scrollOffsetX_ = 0.0f; // Multi line has no horizontal scrolling
            } else {
                // Track caret to update scrollOffsetX_
                float caretX = 0.0f;
                size_t visualCaret = caretIndex_ + imeCompositionString_.length();
                if (visualCaret > 0 && !displayText.empty()) {
                    DWRITE_HIT_TEST_METRICS careMetrics = {};
                    float cx = 0, cy = 0;
                    UINT32 pos = static_cast<UINT32>(visualCaret - 1);
                    if (SUCCEEDED(textLayout_->HitTestTextPosition(pos, TRUE, &cx, &cy, &careMetrics))) {
                        caretX = cx;
                    }
                }
                
                float viewportW = rect_.right - rect_.left;
                if (caretX - scrollOffsetX_ > viewportW - 10.0f) {
                    scrollOffsetX_ = caretX - viewportW + 10.0f;
                }
                if (caretX - scrollOffsetX_ < 10.0f) {
                    scrollOffsetX_ = caretX - 10.0f;
                    if (scrollOffsetX_ < 0.0f) scrollOffsetX_ = 0.0f;
                }
                
                if (metrics.width < viewportW) {
                    scrollOffsetX_ = 0.0f;
                } else if (scrollOffsetX_ > metrics.width - viewportW) {
                    scrollOffsetX_ = metrics.width - viewportW;
                }
                
                scrollOffsetY_ = 0.0f; // Single line has no vertical scrolling
            }
        }
    }
    if (hwnd) {
        UpdateImePosition(hwnd);
    }
}

void D2DWindowlessTextBox::UpdateImePosition(HWND hwnd) {
    static bool inUpdateIme = false;
    if (inUpdateIme) return;
    inUpdateIme = true;

    struct ReentrancyGuard {
        bool* flag;
        ~ReentrancyGuard() { *flag = false; }
    } guard{ &inUpdateIme };

    if (!hwnd || !dwriteFactory_ || !textLayout_) return;

    float caretX = rect_.left - scrollOffsetX_;
    float caretY = rect_.top - scrollOffsetY_;
    float caretH = fontSize_;

    DWRITE_TEXT_METRICS layoutMetrics = {};
    if (SUCCEEDED(textLayout_->GetMetrics(&layoutMetrics))) {
        caretY = rect_.top - scrollOffsetY_ + layoutMetrics.top;
        caretH = layoutMetrics.height;
    }

    size_t visualPos = caretIndex_;
    std::wstring contentText = textBuffer_.GetText();
    std::wstring displayText = contentText.substr(0, caretIndex_) + imeCompositionString_ + contentText.substr(caretIndex_);

    if (!displayText.empty()) {
        float localX = 0, localY = 0;
        DWRITE_HIT_TEST_METRICS metrics = {};
        UINT32 pos = (visualPos > 0) ? static_cast<UINT32>(visualPos - 1) : 0;
        BOOL trail = (visualPos > 0) ? TRUE : FALSE;
        if (SUCCEEDED(textLayout_->HitTestTextPosition(pos, trail, &localX, &localY, &metrics))) {
            caretX = rect_.left - scrollOffsetX_ + localX;
            caretY = rect_.top - scrollOffsetY_ + localY;
            caretH = metrics.height;
        }
    }

    int clientX = static_cast<int>(caretX);
    int clientY = static_cast<int>(caretY);

    HIMC hIMC = ImmGetContext(hwnd);
    if (hIMC) {
        LOGFONTW lf = {};
        lf.lfHeight = -static_cast<LONG>(fontSize_);
        lf.lfWeight = FW_NORMAL;
        lf.lfItalic = FALSE;
        lf.lfCharSet = DEFAULT_CHARSET;
        wcscpy_s(lf.lfFaceName, fontFamily_.c_str());
        ImmSetCompositionFontW(hIMC, &lf);

        COMPOSITIONFORM compForm = {};
        compForm.dwStyle = CFS_POINT;
        compForm.ptCurrentPos.x = clientX;
        compForm.ptCurrentPos.y = clientY;
        ImmSetCompositionWindow(hIMC, &compForm);

        CANDIDATEFORM candForm = {};
        candForm.dwIndex = 0;
        candForm.dwStyle = CFS_EXCLUDE;
        candForm.rcArea.left = clientX;
        candForm.rcArea.top = clientY;
        candForm.rcArea.right = clientX + static_cast<int>(fontSize_);
        candForm.rcArea.bottom = clientY + static_cast<int>(caretH);
        candForm.ptCurrentPos.x = clientX;
        candForm.ptCurrentPos.y = clientY + static_cast<int>(caretH);
        ImmSetCandidateWindow(hIMC, &candForm);

        ImmReleaseContext(hwnd, hIMC);
    }
}

Microsoft::WRL::ComPtr<IDWriteTextLayout> D2DWindowlessTextBox::CreateTextLayout(const std::wstring& content) {
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    if (!dwriteFactory_) return layout;

    Microsoft::WRL::ComPtr<IDWriteTextFormat> fmt;
    HRESULT hr = dwriteFactory_->CreateTextFormat(
        fontFamily_.c_str(),
        nullptr,
        DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        fontSize_,
        L"zh-cn",
        &fmt
    );

    if (SUCCEEDED(hr) && fmt) {
        float w = rect_.right - rect_.left;
        if (w < 50.0f) w = 50.0f;
        dwriteFactory_->CreateTextLayout(
            content.c_str(),
            static_cast<UINT32>(content.length()),
            fmt.Get(),
            w,
            10000.0f,
            &layout
        );
    }
    return layout;
}

void D2DWindowlessTextBox::SelectWord(size_t clickPos) {
    std::wstring contentText = textBuffer_.GetText();
    if (!dwriteFactory_ || contentText.empty() || !textLayout_) return;

    UINT32 maxCount = static_cast<UINT32>(contentText.length()) + 2;
    std::vector<DWRITE_CLUSTER_METRICS> clusterMetrics(maxCount);
    UINT32 actualCount = 0;
    if (SUCCEEDED(textLayout_->GetClusterMetrics(clusterMetrics.data(), maxCount, &actualCount))) {
        size_t currentPos = 0;
        size_t targetClusterIndex = 0;
        for (size_t i = 0; i < actualCount; ++i) {
            size_t nextPos = currentPos + clusterMetrics[i].length;
            if (clickPos >= currentPos && clickPos < nextPos) {
                targetClusterIndex = i;
                break;
            }
            currentPos = nextPos;
        }

        size_t wordStart = currentPos;
        size_t tempPos = currentPos;
        for (int i = static_cast<int>(targetClusterIndex); i >= 0; --i) {
            wchar_t ch = contentText[tempPos];
            if (iswspace(ch) || iswpunct(ch)) {
                if (i != static_cast<int>(targetClusterIndex)) {
                    wordStart = tempPos + clusterMetrics[i].length;
                } else {
                    wordStart = tempPos;
                }
                break;
            }
            if (i == 0) {
                wordStart = 0;
            }
            if (i > 0) {
                tempPos -= clusterMetrics[i - 1].length;
            }
        }

        size_t wordEnd = wordStart;
        tempPos = wordStart;
        size_t startCluster = 0;
        size_t acc = 0;
        for (size_t i = 0; i < actualCount; ++i) {
            if (tempPos == acc) {
                startCluster = i;
                break;
            }
            acc += clusterMetrics[i].length;
        }

        for (size_t i = startCluster; i < actualCount; ++i) {
            wchar_t ch = contentText[tempPos];
            if (iswspace(ch) || iswpunct(ch)) {
                if (i != startCluster) {
                    wordEnd = tempPos;
                } else {
                    wordEnd = tempPos + clusterMetrics[i].length;
                }
                break;
            }
            tempPos += clusterMetrics[i].length;
            if (i == actualCount - 1) {
                wordEnd = contentText.length();
            }
        }

        if (wordStart < wordEnd) {
            selectionAnchor_ = wordStart;
            selectionActive_ = wordEnd;
            caretIndex_ = wordEnd;
        }
    }
}

void D2DWindowlessTextBox::SelectLine(size_t clickPos) {
    if (!textLayout_ || textBuffer_.GetLength() == 0) return;

    UINT32 lineCount = 0;
    textLayout_->GetLineMetrics(nullptr, 0, &lineCount);
    if (lineCount == 0) return;

    std::vector<DWRITE_LINE_METRICS> lineMetrics(lineCount);
    if (SUCCEEDED(textLayout_->GetLineMetrics(lineMetrics.data(), lineCount, &lineCount))) {
        size_t currentPos = 0;
        for (UINT32 i = 0; i < lineCount; ++i) {
            size_t nextPos = currentPos + lineMetrics[i].length;
            if (clickPos >= currentPos && clickPos < nextPos) {
                size_t lineEnd = nextPos;
                std::wstring fullText = textBuffer_.GetText();
                // Exclude newline characters from selection
                while (lineEnd > currentPos) {
                    wchar_t lastCh = fullText[lineEnd - 1];
                    if (lastCh == L'\n' || lastCh == L'\r') {
                        lineEnd--;
                    } else {
                        break;
                    }
                }
                selectionAnchor_ = currentPos;
                selectionActive_ = lineEnd;
                caretIndex_ = lineEnd;
                break;
            }
            currentPos = nextPos;
        }
    }
}

bool D2DWindowlessTextBox::IsWordChar(wchar_t ch) const {
    return iswalnum(ch) || ch == L'_';
}

size_t D2DWindowlessTextBox::FindWordBoundaryLeft(size_t start) const {
    if (start == 0) return 0;
    size_t pos = start;
    
    // Skip whitespaces to the left
    while (pos > 0 && iswspace(textBuffer_.GetCharAt(pos - 1))) {
        pos--;
    }
    if (pos == 0) return 0;
    
    wchar_t ch = textBuffer_.GetCharAt(pos - 1);
    if (IsWordChar(ch)) {
        while (pos > 0 && IsWordChar(textBuffer_.GetCharAt(pos - 1))) {
            pos--;
        }
    } else {
        while (pos > 0 && !IsWordChar(textBuffer_.GetCharAt(pos - 1)) && !iswspace(textBuffer_.GetCharAt(pos - 1))) {
            pos--;
        }
    }
    return pos;
}

size_t D2DWindowlessTextBox::FindWordBoundaryRight(size_t start) const {
    size_t len = textBuffer_.GetLength();
    if (start >= len) return len;
    size_t pos = start;
    
    // Skip whitespaces to the right
    while (pos < len && iswspace(textBuffer_.GetCharAt(pos))) {
        pos++;
    }
    if (pos == len) return len;
    
    wchar_t ch = textBuffer_.GetCharAt(pos);
    if (IsWordChar(ch)) {
        while (pos < len && IsWordChar(textBuffer_.GetCharAt(pos))) {
            pos++;
        }
    } else {
        while (pos < len && !IsWordChar(textBuffer_.GetCharAt(pos)) && !iswspace(textBuffer_.GetCharAt(pos))) {
            pos++;
        }
    }
    return pos;
}

void D2DWindowlessTextBox::ShowContextMenu(HWND hwnd, int screenX, int screenY) {
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    enum {
        ID_UNDO = 1001,
        ID_REDO,
        ID_CUT,
        ID_COPY,
        ID_PASTE,
        ID_DELETE,
        ID_SELECTALL
    };

    UINT undoFlags = (undoStack_.empty() || isReadOnly_) ? (MF_STRING | MF_GRAYED) : (MF_STRING | MF_ENABLED);
    UINT redoFlags = (redoStack_.empty() || isReadOnly_) ? (MF_STRING | MF_GRAYED) : (MF_STRING | MF_ENABLED);
    
    bool hasSelection = (selectionAnchor_ != selectionActive_);
    UINT cutFlags = (hasSelection && !isReadOnly_ && !isPasswordMode_) ? (MF_STRING | MF_ENABLED) : (MF_STRING | MF_GRAYED);
    UINT copyFlags = (hasSelection && !isPasswordMode_) ? (MF_STRING | MF_ENABLED) : (MF_STRING | MF_GRAYED);
    UINT deleteFlags = (hasSelection && !isReadOnly_) ? (MF_STRING | MF_ENABLED) : (MF_STRING | MF_GRAYED);
    
    UINT pasteFlags = MF_STRING | MF_GRAYED;
    if (IsClipboardFormatAvailable(CF_UNICODETEXT) && !isReadOnly_) {
        pasteFlags = MF_STRING | MF_ENABLED;
    }

    UINT selectAllFlags = (textBuffer_.GetLength() > 0) ? (MF_STRING | MF_ENABLED) : (MF_STRING | MF_GRAYED);

    AppendMenuW(hMenu, undoFlags, ID_UNDO, L"撤销 (&U)\tCtrl+Z");
    AppendMenuW(hMenu, redoFlags, ID_REDO, L"重做 (&R)\tCtrl+Y");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, cutFlags, ID_CUT, L"剪切 (&T)\tCtrl+X");
    AppendMenuW(hMenu, copyFlags, ID_COPY, L"复制 (&C)\tCtrl+C");
    AppendMenuW(hMenu, pasteFlags, ID_PASTE, L"粘贴 (&P)\tCtrl+V");
    AppendMenuW(hMenu, deleteFlags, ID_DELETE, L"删除 (&D)\tDel");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, selectAllFlags, ID_SELECTALL, L"全选 (&A)\tCtrl+A");

    int selection = TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_RIGHTBUTTON, screenX, screenY, 0, hwnd, nullptr);
    DestroyMenu(hMenu);

    if (selection == 0) return;

    // Simulate standard keys to trigger corresponding actions
    switch (selection) {
        case ID_UNDO:
        case ID_REDO:
        case ID_CUT:
        case ID_COPY:
        case ID_PASTE:
        case ID_SELECTALL:
            {
                BYTE orig[256];
                GetKeyboardState(orig);
                BYTE mock[256];
                memcpy(mock, orig, 256);
                mock[VK_CONTROL] = 0x80;
                SetKeyboardState(mock);
                
                if (selection == ID_UNDO) HandleMessage(hwnd, WM_KEYDOWN, 'Z', 0);
                else if (selection == ID_REDO) HandleMessage(hwnd, WM_KEYDOWN, 'Y', 0);
                else if (selection == ID_CUT) HandleMessage(hwnd, WM_KEYDOWN, 'X', 0);
                else if (selection == ID_COPY) HandleMessage(hwnd, WM_KEYDOWN, 'C', 0);
                else if (selection == ID_PASTE) HandleMessage(hwnd, WM_KEYDOWN, 'V', 0);
                else if (selection == ID_SELECTALL) HandleMessage(hwnd, WM_KEYDOWN, 'A', 0);
                
                SetKeyboardState(orig);
            }
            break;
        case ID_DELETE:
            HandleMessage(hwnd, WM_KEYDOWN, VK_DELETE, 0);
            break;
    }
}

void D2DWindowlessTextBox::SetPasswordMode(bool enable) {
    if (isPasswordMode_ != enable) {
        isPasswordMode_ = enable;
        if (isFocused_ && hwndParent_) {
            if (isPasswordMode_) {
                if (!hSavedIMC_) {
                    hSavedIMC_ = reinterpret_cast<void*>(ImmAssociateContext(hwndParent_, NULL));
                }
            } else {
                if (hSavedIMC_) {
                    ImmAssociateContext(hwndParent_, reinterpret_cast<HIMC>(hSavedIMC_));
                    hSavedIMC_ = nullptr;
                }
            }
        }
        UpdateLayoutMetrics(hwndParent_);
    }
}

void D2DWindowlessTextBox::SetDisabled(bool disabled) {
    if (isDisabled_ != disabled) {
        isDisabled_ = disabled;
        if (isDisabled_) {
            SetFocused(false, hwndParent_);
        }
        textBrush_.Reset();
        placeholderBrush_.Reset();
        borderBrush_.Reset();
    }
}
D2DWindowlessTextBox::CaretMetrics D2DWindowlessTextBox::GetCaretMetrics() const {
    float caretX = rect_.left - scrollOffsetX_;
    float caretY = rect_.top - scrollOffsetY_;
    float caretH = fontSize_;

    DWRITE_TEXT_METRICS layoutMetrics = {};
    if (textLayout_ && SUCCEEDED(textLayout_->GetMetrics(&layoutMetrics))) {
        caretY = rect_.top - scrollOffsetY_ + layoutMetrics.top;
        caretH = layoutMetrics.height;
    }

    std::wstring contentText = textBuffer_.GetText();
    std::wstring displayText = contentText.substr(0, caretIndex_) + imeCompositionString_ + contentText.substr(caretIndex_);
    if (isPasswordMode_) {
        std::wstring maskedText = displayText;
        for (auto& ch : maskedText) {
            if (ch != L'\r' && ch != L'\n') {
                ch = L'•';
            }
        }
        displayText = maskedText;
    }

    if (textLayout_ && !displayText.empty()) {
        DWRITE_HIT_TEST_METRICS metrics = {};
        float localX = 0, localY = 0;
        size_t visualCaret = caretIndex_ + imeCompositionString_.length();
        UINT32 pos = (visualCaret > 0) ? static_cast<UINT32>(visualCaret - 1) : 0;
        BOOL trail = (visualCaret > 0) ? TRUE : FALSE;
        if (SUCCEEDED(textLayout_->HitTestTextPosition(pos, trail, &localX, &localY, &metrics))) {
            caretX = rect_.left - scrollOffsetX_ + localX;
            caretY = rect_.top - scrollOffsetY_ + localY;
            caretH = metrics.height;
        }
    }
    return CaretMetrics{ D2D1::Point2F(caretX, caretY), caretH };
}

void D2DWindowlessTextBox::SetLineSpacing(float lineSpacing) {
    if (lineSpacing_ != lineSpacing) {
        lineSpacing_ = lineSpacing;
        textFormat_.Reset();
        UpdateLayoutMetrics(hwndParent_);
    }
}

void D2DWindowlessTextBox::AppendText(const std::wstring& chunk, bool autoScroll) {
    size_t oldLen = textBuffer_.GetLength();
    
    // 1. Directly insert text at the end of the buffer
    textBuffer_.Insert(oldLen, chunk);
    
    // 2. Adjust caret position if it was at the end of the text prior to appending
    if (caretIndex_ == oldLen) {
        caretIndex_ = textBuffer_.GetLength();
        selectionAnchor_ = caretIndex_;
        selectionActive_ = caretIndex_;
    }
    
    // 3. Determine if scroll should be pinned to the bottom
    bool shouldPinBottom = false;
    if (autoScroll && textLayout_ && isMultiLine_) {
        DWRITE_TEXT_METRICS metrics = {};
        if (SUCCEEDED(textLayout_->GetMetrics(&metrics))) {
            float physicalHeight = rect_.bottom - rect_.top;
            if (physicalHeight < 10.0f) physicalHeight = fontSize_ + 8.0f;
            float scrollBottom = scrollOffsetY_ + physicalHeight;
            if (scrollBottom >= metrics.height - fontSize_ - 8.0f) {
                shouldPinBottom = true;
            }
        }
    }
    
    // 4. Update the layout
    UpdateLayoutMetrics(hwndParent_);
    
    // 5. Scroll to the bottom if pinned
    if (shouldPinBottom && textLayout_ && isMultiLine_) {
        DWRITE_TEXT_METRICS metrics = {};
        if (SUCCEEDED(textLayout_->GetMetrics(&metrics))) {
            float physicalHeight = rect_.bottom - rect_.top;
            if (physicalHeight < 10.0f) physicalHeight = fontSize_ + 8.0f;
            if (metrics.height > physicalHeight) {
                scrollOffsetY_ = metrics.height - physicalHeight;
            } else {
                scrollOffsetY_ = 0.0f;
            }
        }
    }
}
