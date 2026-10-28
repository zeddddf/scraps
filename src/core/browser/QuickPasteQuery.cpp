#include "core/browser/QuickPasteQuery.h"

#include <algorithm>
#include <vector>

#include "core/browser/ClipTextDisplay.h"
#include "common/StringUtil.h"

namespace clipd2d::core::browser {

namespace {

std::wstring JoinTagNames(const store::ClipSummary& summary) {
  std::vector<std::wstring> tag_names;
  tag_names.reserve(summary.tag_names.size());
  for (const auto& tag_name : summary.tag_names) {
    const std::wstring trimmed = common::TrimWide(tag_name);
    if (!trimmed.empty()) {
      tag_names.push_back(trimmed);
    }
  }
  return common::JoinWide(tag_names, L" | ");
}

std::wstring BuildPrimaryText(const store::ClipSummary& summary) {
  if (summary.content_type == store::ClipContentType::Text ||
      summary.content_type == store::ClipContentType::Html ||
      summary.content_type == store::ClipContentType::Rtf) {
    return common::clip_text_display::QuickPastePrimaryText(summary, 2);
  }

  const std::wstring text = common::clip_text_display::SummaryTitle(summary);
  return text == L"Empty Clip"
      ? std::wstring(common::clip_text_display::ContentFallbackLabel(summary.content_type))
      : text;
}

std::wstring BuildSecondaryText(const store::ClipSummary& summary) {
  std::wstring left;
  if (summary.content_type == store::ClipContentType::Image) {
    if (summary.image_width > 0 && summary.image_height > 0) {
      left = std::to_wstring(summary.image_width) + L" x " + std::to_wstring(summary.image_height);
    } else {
      left = common::clip_text_display::ContentFallbackLabel(summary.content_type);
    }
  } else if (summary.content_type == store::ClipContentType::Files) {
    left = common::clip_text_display::NormalizeSingleLineText(summary.snippet);
    if (left.empty()) {
      if (summary.file_count > 1) {
        left = std::to_wstring(summary.file_count) + L" files";
      } else {
        left = common::clip_text_display::ContentFallbackLabel(summary.content_type);
      }
    }
  } else {
    return {};
  }

  const std::wstring tags = JoinTagNames(summary);
  if (left.empty()) {
    return tags;
  }
  if (tags.empty()) {
    return left;
  }
  return left + L" | " + tags;
}

QuickPasteVisualKind ResolveVisualKind(const store::ClipSummary& summary, std::wstring* asset_path) {
  if (asset_path != nullptr) {
    asset_path->clear();
  }

  if (summary.content_type == store::ClipContentType::Text ||
      summary.content_type == store::ClipContentType::Html ||
      summary.content_type == store::ClipContentType::Rtf) {
    return QuickPasteVisualKind::None;
  }

  if (summary.content_type == store::ClipContentType::Image) {
    if (asset_path != nullptr) {
      *asset_path = !summary.thumbnail_path.empty() ? summary.thumbnail_path : summary.storage_path;
    }
    return QuickPasteVisualKind::ImageThumbnail;
  }

  if (summary.content_type == store::ClipContentType::Files) {
    const std::wstring file_path = !summary.storage_path.empty() ? summary.storage_path : summary.secondary_path;
    if (asset_path != nullptr) {
      *asset_path = file_path;
    }
    if (file_path.empty()) {
      return QuickPasteVisualKind::FolderGlyph;
    }

    const std::wstring lowered = common::ToLowerWide(file_path);
    const bool supports_thumbnail =
        lowered.ends_with(L".png") || lowered.ends_with(L".jpg") || lowered.ends_with(L".jpeg") ||
        lowered.ends_with(L".bmp") || lowered.ends_with(L".gif") || lowered.ends_with(L".webp") ||
        lowered.ends_with(L".tif") || lowered.ends_with(L".tiff") || lowered.ends_with(L".ico") ||
        lowered.ends_with(L".heic") || lowered.ends_with(L".mp4") || lowered.ends_with(L".mov") ||
        lowered.ends_with(L".m4v") || lowered.ends_with(L".avi") || lowered.ends_with(L".mkv") ||
        lowered.ends_with(L".webm") || lowered.ends_with(L".wmv");
    return supports_thumbnail ? QuickPasteVisualKind::FileSystemThumbnail : QuickPasteVisualKind::FileSystemIcon;
  }

  return QuickPasteVisualKind::None;
}

}  // namespace

std::vector<QuickPasteCandidate> BuildQuickPasteCandidates(
    const std::vector<store::ClipSummary>& summaries) {
  std::vector<QuickPasteCandidate> candidates;
  candidates.reserve(std::min<size_t>(9, summaries.size()));

  for (const auto& summary : summaries) {
    QuickPasteCandidate candidate;
    candidate.clip_id = summary.id;
    candidate.summary = summary;
    candidate.content_type = summary.content_type;
    candidate.primary_text = BuildPrimaryText(summary);
    candidate.secondary_text = BuildSecondaryText(summary);
    candidate.visual_kind = ResolveVisualKind(summary, &candidate.asset_path);
    candidate.source_icon_path = summary.source_app_path;
    candidate.supports_plain_text_paste = summary.content_type != store::ClipContentType::Image;
    candidates.push_back(std::move(candidate));
    if (candidates.size() >= 9) {
      break;
    }
  }

  return candidates;
}

}  // namespace clipd2d::core::browser
