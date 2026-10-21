#pragma once

#include <dwmapi.h>
#include <windows.h>

namespace clipd2d::ui {

constexpr UINT kPopupPanelClassStyle = CS_DROPSHADOW;
constexpr DWORD kPopupPanelWindowExStyle = WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE;
constexpr DWORD kPopupPanelWindowStyle = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
constexpr COLORREF kPopupPanelNoBorderColor = 0xFFFFFFFE;
constexpr COLORREF kPopupPanelLightBorderColor = RGB(218, 218, 218);

inline void ApplyPopupPanelWindowChrome(HWND hwnd, COLORREF border_color = kPopupPanelNoBorderColor) {
  if (hwnd == nullptr) {
    return;
  }

  constexpr DWORD kDwmWindowCornerPreferenceValue = 33;
  constexpr DWORD kDwmBorderColorValue = 34;
  constexpr int kDwmCornerRoundValue = 2;

  DwmSetWindowAttribute(
      hwnd,
      kDwmWindowCornerPreferenceValue,
      &kDwmCornerRoundValue,
      sizeof(kDwmCornerRoundValue));

  DwmSetWindowAttribute(hwnd, kDwmBorderColorValue, &border_color, sizeof(border_color));
}

#include "core/store/ClipTypes.h"

inline void ApplyWindowBlur(HWND hwnd, core::store::WindowBlurType type) {
  if (hwnd == nullptr) return;
  constexpr DWORD DWMWA_SYSTEMBACKDROP_TYPE = 38;
  constexpr DWORD DWMSBT_NONE = 1;
  constexpr DWORD DWMSBT_MAINWINDOW = 2; // Mica
  constexpr DWORD DWMSBT_TRANSIENTWINDOW = 3; // Acrylic
  DWORD backdrop_type = DWMSBT_NONE;
  if (type == core::store::WindowBlurType::Mica) {
    backdrop_type = DWMSBT_MAINWINDOW;
  } else if (type == core::store::WindowBlurType::Acrylic) {
    backdrop_type = DWMSBT_TRANSIENTWINDOW;
  }
  

  
  HRESULT hr = DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop_type, sizeof(backdrop_type));

  if (FAILED(hr) || type == core::store::WindowBlurType::Mica) {
    constexpr DWORD DWMWA_MICA_EFFECT = 1029;
    DWORD mica_enabled = (type == core::store::WindowBlurType::Mica) ? 1 : 0;
    HRESULT hr_mica = DwmSetWindowAttribute(hwnd, DWMWA_MICA_EFFECT, &mica_enabled, sizeof(mica_enabled));
    if (SUCCEEDED(hr_mica)) {
      hr = S_OK;
    }
  }

  if (FAILED(hr)) {
    HMODULE hUser = GetModuleHandleW(L"user32.dll");
    if (hUser) {
      enum WINDOWCOMPOSITIONATTRIB { WCA_ACCENT_POLICY = 19 };
      enum ACCENT_STATE {
        ACCENT_DISABLED = 0,
        ACCENT_ENABLE_BLURBEHIND = 3,
        ACCENT_ENABLE_ACRYLICBLURBEHIND = 4
      };
      struct ACCENT_POLICY {
        ACCENT_STATE AccentState;
        DWORD AccentFlags;
        DWORD GradientColor;
        DWORD AnimationId;
      };
      struct WINDOWCOMPOSITIONATTRIBDATA {
        WINDOWCOMPOSITIONATTRIB Attrib;
        PVOID pvData;
        SIZE_T cbData;
      };
      typedef BOOL(WINAPI* pfnSetWindowCompositionAttribute)(HWND, WINDOWCOMPOSITIONATTRIBDATA*);
      
      auto pSetWindowCompositionAttribute = (pfnSetWindowCompositionAttribute)GetProcAddress(hUser, "SetWindowCompositionAttribute");
      if (pSetWindowCompositionAttribute) {
        ACCENT_POLICY policy = { ACCENT_DISABLED, 0, 0, 0 };
        if (type == core::store::WindowBlurType::Acrylic) {
            policy.AccentState = ACCENT_ENABLE_BLURBEHIND; // Persistent blur fallback for Win10
            policy.AccentFlags = 2; 
            policy.GradientColor = 0x01000000;
        }
        WINDOWCOMPOSITIONATTRIBDATA data = { WCA_ACCENT_POLICY, &policy, sizeof(policy) };
        pSetWindowCompositionAttribute(hwnd, &data);
      }
    }
  }
}

}  // namespace clipd2d::ui
