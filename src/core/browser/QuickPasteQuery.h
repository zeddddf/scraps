#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/store/ClipTypes.h"

namespace clipd2d::core::browser {

enum class QuickPasteVisualKind {
  None = 0,
  TextGlyph,
  ImageGlyph,
  FolderGlyph,
  ImageThumbnail,
  FileSystemThumbnail,
  FileSystemIcon,
};

struct QuickPasteCandidate {
  std::int64_t clip_id = 0;
  store::ClipSummary summary;
  store::ClipContentType content_type = store::ClipContentType::Text;
  std::wstring primary_text;
  std::wstring secondary_text;
  std::wstring asset_path;
  std::wstring source_icon_path;
  QuickPasteVisualKind visual_kind = QuickPasteVisualKind::None;
  bool supports_plain_text_paste = false;
};

std::vector<QuickPasteCandidate> BuildQuickPasteCandidates(
    const std::vector<store::ClipSummary>& summaries);

}  // namespace clipd2d::core::browser
