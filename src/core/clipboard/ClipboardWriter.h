#pragma once

#include <string_view>

#include "common/Status.h"
#include "core/store/ClipTypes.h"

namespace clipd2d::core::clipboard {

enum class PasteRoute {
  Fast,
  Recovery,
};

struct PasteTargetContext {
  HWND root_window = nullptr;
  HWND focus_window = nullptr;
  DWORD thread_id = 0;
};

common::Status SetClipboardText(
    std::string_view text,
    std::string_view html_text,
    std::string_view rtf_text = {});
common::Status SetClipboardFiles(const std::wstring& joined_paths);
common::Status SetClipboardImageFromFile(const std::wstring& path);
common::Status SetClipboardImageFromPng(const std::wstring& path);
bool IsValidPasteTargetContext(const PasteTargetContext& context);
PasteTargetContext CapturePasteTargetContext(HWND preferred_window = nullptr);
common::Status PerformPasteToTarget(
    const PasteTargetContext& context,
    PasteRoute route,
    std::string_view text_fallback = {});
common::Status WriteClipToClipboard(
    const store::ClipRecord& clip,
    std::string_view transformed_text,
    bool use_transformed_text);

}  // namespace clipd2d::core::clipboard
