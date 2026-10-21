#include "platform/win32/HotkeySpec.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "common/StringUtil.h"
#include "common/Utf8.h"

namespace clipd2d::platform::win32 {

namespace {

struct NamedVirtualKey {
  std::wstring_view token;
  UINT virtual_key;
  std::wstring_view display;
};

constexpr std::array<NamedVirtualKey, 22> kNamedKeys{{
    {L"space", VK_SPACE, L"Space"},
    {L"tab", VK_TAB, L"Tab"},
    {L"enter", VK_RETURN, L"Enter"},
    {L"return", VK_RETURN, L"Enter"},
    {L"esc", VK_ESCAPE, L"Esc"},
    {L"escape", VK_ESCAPE, L"Esc"},
    {L"backspace", VK_BACK, L"Backspace"},
    {L"delete", VK_DELETE, L"Delete"},
    {L"del", VK_DELETE, L"Delete"},
    {L"insert", VK_INSERT, L"Insert"},
    {L"ins", VK_INSERT, L"Insert"},
    {L"home", VK_HOME, L"Home"},
    {L"end", VK_END, L"End"},
    {L"pageup", VK_PRIOR, L"PageUp"},
    {L"pgup", VK_PRIOR, L"PageUp"},
    {L"pagedown", VK_NEXT, L"PageDown"},
    {L"pgdn", VK_NEXT, L"PageDown"},
    {L"up", VK_UP, L"Up"},
    {L"down", VK_DOWN, L"Down"},
    {L"left", VK_LEFT, L"Left"},
    {L"right", VK_RIGHT, L"Right"},
    {L"apps", VK_APPS, L"Apps"},
}};

constexpr std::array<NamedVirtualKey, 23> kSymbolKeys{{
    {L"`", VK_OEM_3, L"~"},
    {L"~", VK_OEM_3, L"~"},
    {L"-", VK_OEM_MINUS, L"-"},
    {L"_", VK_OEM_MINUS, L"-"},
    {L"=", VK_OEM_PLUS, L"="},
    {L"+", VK_OEM_PLUS, L"="},
    {L"[", VK_OEM_4, L"["},
    {L"{", VK_OEM_4, L"["},
    {L"]", VK_OEM_6, L"]"},
    {L"}", VK_OEM_6, L"]"},
    {L"\\", VK_OEM_5, L"\\"},
    {L"|", VK_OEM_5, L"\\"},
    {L";", VK_OEM_1, L";"},
    {L":", VK_OEM_1, L";"},
    {L"'", VK_OEM_7, L"'"},
    {L"\"", VK_OEM_7, L"'"},
    {L",", VK_OEM_COMMA, L","},
    {L"<", VK_OEM_COMMA, L","},
    {L".", VK_OEM_PERIOD, L"."},
    {L">", VK_OEM_PERIOD, L"."},
    {L"/", VK_OEM_2, L"/"},
    {L"?", VK_OEM_2, L"/"},
    {L"oem3", VK_OEM_3, L"~"},
}};

std::optional<UINT> ParseFunctionKey(std::wstring_view token) {
  if (token.size() < 2 || token.front() != L'f') {
    return std::nullopt;
  }

  const auto parsed = common::ParseInt64(common::WideToUtf8(std::wstring(token.substr(1))).utf8);
  if (!parsed.has_value() || *parsed < 1 || *parsed > 24) {
    return std::nullopt;
  }

  return static_cast<UINT>(VK_F1 + (*parsed - 1));
}

std::optional<std::wstring> DisplayKeyName(UINT virtual_key) {
  if (virtual_key >= 'A' && virtual_key <= 'Z') {
    return std::wstring(1, static_cast<wchar_t>(virtual_key));
  }
  if (virtual_key >= '0' && virtual_key <= '9') {
    return std::wstring(1, static_cast<wchar_t>(virtual_key));
  }
  if (virtual_key >= VK_F1 && virtual_key <= VK_F24) {
    return L"F" + std::to_wstring(virtual_key - VK_F1 + 1);
  }
  for (const auto& named_key : kNamedKeys) {
    if (named_key.virtual_key == virtual_key) {
      return std::wstring(named_key.display);
    }
  }
  for (const auto& symbol_key : kSymbolKeys) {
    if (symbol_key.virtual_key == virtual_key) {
      return std::wstring(symbol_key.display);
    }
  }
  return std::nullopt;
}

std::optional<UINT> ParseVirtualKey(std::wstring_view token, std::wstring* display_name) {
  const std::wstring normalized = common::ToLowerWide(std::wstring(token));
  if (normalized.size() == 1) {
    const wchar_t ch = normalized.front();
    if (ch >= L'a' && ch <= L'z') {
      if (display_name != nullptr) {
        *display_name = std::wstring(1, static_cast<wchar_t>(ch - L'a' + L'A'));
      }
      return static_cast<UINT>(ch - L'a' + L'A');
    }
    if (ch >= L'0' && ch <= L'9') {
      if (display_name != nullptr) {
        *display_name = std::wstring(1, ch);
      }
      return static_cast<UINT>(ch);
    }
  }

  if (const auto function_key = ParseFunctionKey(normalized); function_key.has_value()) {
    if (display_name != nullptr) {
      *display_name = *DisplayKeyName(*function_key);
    }
    return function_key;
  }

  for (const auto& named_key : kNamedKeys) {
    if (normalized == named_key.token) {
      if (display_name != nullptr) {
        *display_name = std::wstring(named_key.display);
      }
      return named_key.virtual_key;
    }
  }

  for (const auto& symbol_key : kSymbolKeys) {
    if (normalized == symbol_key.token) {
      if (display_name != nullptr) {
        *display_name = std::wstring(symbol_key.display);
      }
      return symbol_key.virtual_key;
    }
  }

  return std::nullopt;
}

std::wstring BuildHotkeyText(UINT modifiers, UINT virtual_key) {
  std::wstring normalized;
  if ((modifiers & MOD_CONTROL) != 0) {
    normalized += L"Ctrl+";
  }
  if ((modifiers & MOD_SHIFT) != 0) {
    normalized += L"Shift+";
  }
  if ((modifiers & MOD_ALT) != 0) {
    normalized += L"Alt+";
  }
  if ((modifiers & MOD_WIN) != 0) {
    normalized += L"Win+";
  }

  const auto display = DisplayKeyName(virtual_key);
  if (!display.has_value()) {
    return {};
  }

  normalized += *display;
  return normalized;
}

common::Status ParseHotkeyTextInternal(std::wstring_view text, bool require_modifier, HotkeySpec* hotkey) {
  if (hotkey == nullptr) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "Hotkey output cannot be null.");
  }

  const std::wstring trimmed = common::TrimWide(std::wstring(text));
  if (trimmed.empty()) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "Hotkey cannot be empty.");
  }

  UINT modifiers = 0;
  UINT virtual_key = 0;
  bool has_key = false;
  std::wstring display_name;

  for (const std::wstring& raw_token : common::SplitWide(trimmed, L'+')) {
    const std::wstring token = common::TrimWide(raw_token);
    if (token.empty()) {
      return common::Status::Error(common::StatusCode::InvalidArgument, "Invalid hotkey format.");
    }

    const std::wstring normalized = common::ToLowerWide(token);
    if (normalized == L"ctrl" || normalized == L"control") {
      modifiers |= MOD_CONTROL;
      continue;
    }
    if (normalized == L"shift") {
      modifiers |= MOD_SHIFT;
      continue;
    }
    if (normalized == L"alt") {
      modifiers |= MOD_ALT;
      continue;
    }
    if (normalized == L"win" || normalized == L"windows") {
      modifiers |= MOD_WIN;
      continue;
    }

    if (has_key) {
      return common::Status::Error(common::StatusCode::InvalidArgument, "Hotkey must contain exactly one main key.");
    }

    const auto parsed_virtual_key = ParseVirtualKey(token, &display_name);
    if (!parsed_virtual_key.has_value()) {
      return common::Status::Error(common::StatusCode::InvalidArgument, "Unsupported hotkey key.");
    }
    virtual_key = *parsed_virtual_key;
    has_key = true;
  }

  if (!has_key || virtual_key == 0 || (require_modifier && modifiers == 0)) {
    return common::Status::Error(
        common::StatusCode::InvalidArgument,
        require_modifier ? "Global hotkey must contain modifiers." : "Shortcut must contain a main key.");
  }

  hotkey->modifiers = modifiers;
  hotkey->virtual_key = virtual_key;
  hotkey->normalized_text = BuildHotkeyText(modifiers, virtual_key);
  return common::Status::Ok();
}

bool RequiresExtendedKey(UINT virtual_key) {
  switch (virtual_key) {
    case VK_RMENU:
    case VK_RCONTROL:
    case VK_INSERT:
    case VK_DELETE:
    case VK_HOME:
    case VK_END:
    case VK_PRIOR:
    case VK_NEXT:
    case VK_LEFT:
    case VK_RIGHT:
    case VK_UP:
    case VK_DOWN:
    case VK_NUMLOCK:
    case VK_DIVIDE:
    case VK_SNAPSHOT:
    case VK_APPS:
    case VK_LWIN:
    case VK_RWIN:
      return true;
    default:
      return false;
  }
}

void AppendKeyInput(std::vector<INPUT>* inputs, WORD virtual_key, bool key_up) {
  INPUT input{};
  input.type = INPUT_KEYBOARD;
  input.ki.wVk = virtual_key;
  input.ki.dwFlags = key_up ? KEYEVENTF_KEYUP : 0;
  if (RequiresExtendedKey(virtual_key)) {
    input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
  }
  inputs->push_back(input);
}

}  // namespace

std::wstring FormatHotkeyText(UINT modifiers, UINT virtual_key) {
  return BuildHotkeyText(modifiers, virtual_key);
}

common::Status ParseHotkeyText(std::wstring_view text, HotkeySpec* hotkey) {
  return ParseHotkeyTextInternal(text, true, hotkey);
}

common::Status ParseLocalHotkeyText(std::wstring_view text, HotkeySpec* hotkey) {
  return ParseHotkeyTextInternal(text, false, hotkey);
}

UINT GetPressedHotkeyModifiers() {
  UINT modifiers = 0;
  if ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0) {
    modifiers |= MOD_CONTROL;
  }
  if ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0) {
    modifiers |= MOD_SHIFT;
  }
  if ((GetAsyncKeyState(VK_MENU) & 0x8000) != 0) {
    modifiers |= MOD_ALT;
  }
  if ((GetAsyncKeyState(VK_LWIN) & 0x8000) != 0 || (GetAsyncKeyState(VK_RWIN) & 0x8000) != 0) {
    modifiers |= MOD_WIN;
  }
  return modifiers;
}

bool HotkeyMatches(UINT virtual_key, UINT modifiers, const HotkeySpec& hotkey) {
  return virtual_key == hotkey.virtual_key && modifiers == hotkey.modifiers;
}

common::Status SendHotkeyInput(const HotkeySpec& hotkey) {
  if (hotkey.virtual_key == 0) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "Hotkey main key cannot be empty.");
  }

  std::vector<INPUT> inputs;
  inputs.reserve(10);
  const std::vector<WORD> modifiers = {
      (hotkey.modifiers & MOD_CONTROL) != 0 ? static_cast<WORD>(VK_CONTROL) : static_cast<WORD>(0),
      (hotkey.modifiers & MOD_SHIFT) != 0 ? static_cast<WORD>(VK_SHIFT) : static_cast<WORD>(0),
      (hotkey.modifiers & MOD_ALT) != 0 ? static_cast<WORD>(VK_MENU) : static_cast<WORD>(0),
      (hotkey.modifiers & MOD_WIN) != 0 ? static_cast<WORD>(VK_LWIN) : static_cast<WORD>(0)};

  for (WORD modifier : modifiers) {
    if (modifier != 0) {
      AppendKeyInput(&inputs, modifier, false);
    }
  }
  AppendKeyInput(&inputs, static_cast<WORD>(hotkey.virtual_key), false);
  AppendKeyInput(&inputs, static_cast<WORD>(hotkey.virtual_key), true);
  for (auto it = modifiers.rbegin(); it != modifiers.rend(); ++it) {
    if (*it != 0) {
      AppendKeyInput(&inputs, *it, true);
    }
  }

  if (inputs.empty()) {
    return common::Status::Error(common::StatusCode::InvalidArgument, "Hotkey input cannot be empty.");
  }

  const UINT sent = SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
  if (sent != inputs.size()) {
    return common::Status::Error(common::StatusCode::PlatformError, "SendInput failed.");
  }
  return common::Status::Ok();
}

HotkeySpec DefaultHotkeySpec() {
  HotkeySpec hotkey;
  hotkey.modifiers = MOD_CONTROL | MOD_SHIFT;
  hotkey.virtual_key = 'V';
  hotkey.normalized_text = L"Ctrl+Shift+V";
  return hotkey;
}

}  // namespace clipd2d::platform::win32
