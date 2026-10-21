#pragma once

#include <string>
#include <string_view>

#include <windows.h>

#include "common/Status.h"

namespace clipd2d::platform::win32 {

struct HotkeySpec {
  UINT modifiers = 0;
  UINT virtual_key = 0;
  std::wstring normalized_text;
};

common::Status ParseHotkeyText(std::wstring_view text, HotkeySpec* hotkey);
common::Status ParseLocalHotkeyText(std::wstring_view text, HotkeySpec* hotkey);
std::wstring FormatHotkeyText(UINT modifiers, UINT virtual_key);
UINT GetPressedHotkeyModifiers();
bool HotkeyMatches(UINT virtual_key, UINT modifiers, const HotkeySpec& hotkey);
common::Status SendHotkeyInput(const HotkeySpec& hotkey);
HotkeySpec DefaultHotkeySpec();

}  // namespace clipd2d::platform::win32
