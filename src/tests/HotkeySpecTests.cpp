#include "tests/TestSupport.h"

#include <cstdint>
#include <string>
#include <vector>

#include "common/ClipTextDisplay.h"
#include "common/Utf8.h"
#include "core/browser/QuickPasteQuery.h"
#include "platform/win32/HotkeySpec.h"

namespace {

using clipd2d::core::browser::BuildQuickPasteCandidates;
using clipd2d::core::browser::QuickPasteVisualKind;
using clipd2d::core::store::ClipContentType;
using clipd2d::core::store::ClipSummary;
using clipd2d::platform::win32::HotkeySpec;

ClipSummary MakeSummary(std::int64_t id, ClipContentType content_type, std::wstring title, std::wstring source_app = {}) {
  ClipSummary summary;
  summary.id = id;
  summary.content_type = content_type;
  summary.title = clipd2d::common::WideToUtf8(title).utf8;
  summary.source_app_name = std::move(source_app);
  return summary;
}

}  // namespace

CLIPD2D_TEST(ParseHotkeyTextSupportsAltTilde) {
  HotkeySpec spec;
  CLIPD2D_CHECK(clipd2d::platform::win32::ParseHotkeyText(L"Alt+~", &spec).ok());
  CLIPD2D_CHECK_EQ(spec.modifiers, static_cast<UINT>(MOD_ALT));
  CLIPD2D_CHECK_EQ(spec.virtual_key, static_cast<UINT>(VK_OEM_3));
  CLIPD2D_CHECK_EQ(spec.normalized_text, std::wstring(L"Alt+~"));
}

CLIPD2D_TEST(QuickPasteCandidatesKeepHistoryOrderAndLimitNineItems) {
  std::vector<ClipSummary> summaries;
  for (int index = 0; index < 10; ++index) {
    summaries.push_back(MakeSummary(index + 1, ClipContentType::Text, L"Candidate " + std::to_wstring(index + 1), L"Notepad"));
  }

  const auto candidates = BuildQuickPasteCandidates(summaries);
  CLIPD2D_CHECK_EQ(candidates.size(), static_cast<size_t>(9));
  for (size_t index = 0; index < candidates.size(); ++index) {
    CLIPD2D_CHECK_EQ(candidates[index].clip_id, static_cast<std::int64_t>(index + 1));
  }
}

CLIPD2D_TEST(QuickPasteCandidatesBuildVisualAndMetadataSummary) {
  ClipSummary image = MakeSummary(42, ClipContentType::Image, L"Image", L"Chrome");
  image.image_width = 1920;
  image.image_height = 1080;
  image.thumbnail_path = L"C:\\temp\\preview.png";
  image.source_app_path = L"C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe";
  image.tag_names = {L"Important", L"Shot"};

  const auto candidates = BuildQuickPasteCandidates({image});
  CLIPD2D_CHECK_EQ(candidates.size(), static_cast<size_t>(1));
  CLIPD2D_CHECK_EQ(candidates[0].visual_kind, QuickPasteVisualKind::ImageThumbnail);
  CLIPD2D_CHECK_EQ(candidates[0].asset_path, std::wstring(L"C:\\temp\\preview.png"));
  CLIPD2D_CHECK(candidates[0].secondary_text.find(L"1920 x 1080") != std::wstring::npos);
  CLIPD2D_CHECK(candidates[0].secondary_text.find(L"Important") != std::wstring::npos);
  CLIPD2D_CHECK_EQ(candidates[0].source_icon_path, std::wstring(L"C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe"));
}

CLIPD2D_TEST(QuickPasteTextCandidatesUseDirectContentWithoutLeadingIcon) {
  ClipSummary text = MakeSummary(7, ClipContentType::Text, L"Title");
  text.snippet = clipd2d::common::WideToUtf8(L"Direct content line").utf8;

  const auto candidates = BuildQuickPasteCandidates({text});
  CLIPD2D_CHECK_EQ(candidates.size(), static_cast<size_t>(1));
  CLIPD2D_CHECK_EQ(candidates[0].visual_kind, QuickPasteVisualKind::None);
  CLIPD2D_CHECK_EQ(candidates[0].primary_text, std::wstring(L"Direct content line"));
  CLIPD2D_CHECK(candidates[0].secondary_text.empty());
}

CLIPD2D_TEST(QuickPasteTextCandidatesRecoverFromInvalidUtf8Suffix) {
  ClipSummary text = MakeSummary(8, ClipContentType::Text, L"Title");
  text.snippet = std::string("First line\nSecond line\xF0");

  const auto candidates = BuildQuickPasteCandidates({text});
  CLIPD2D_CHECK_EQ(candidates.size(), static_cast<size_t>(1));
  CLIPD2D_CHECK_EQ(candidates[0].primary_text, std::wstring(L"First line\nSecond line"));
}

CLIPD2D_TEST(QuickPasteHtmlCandidatesFallBackToContentLabelWhenSnippetMissing) {
  ClipSummary html = MakeSummary(9, ClipContentType::Html, L"");

  const auto candidates = BuildQuickPasteCandidates({html});
  CLIPD2D_CHECK_EQ(candidates.size(), static_cast<size_t>(1));
  CLIPD2D_CHECK_EQ(candidates[0].visual_kind, QuickPasteVisualKind::None);
  CLIPD2D_CHECK_EQ(candidates[0].primary_text, std::wstring(L"HTML Content"));
  CLIPD2D_CHECK(candidates[0].secondary_text.empty());
}

CLIPD2D_TEST(QuickPasteCandidatesUseSharedPreviewSummaryLogic) {
  ClipSummary text = MakeSummary(10, ClipContentType::Text, L"Title fallback");
  text.snippet = clipd2d::common::WideToUtf8(L"Line 1\nLine 2\nLine 3").utf8;

  const auto candidates = BuildQuickPasteCandidates({text});
  CLIPD2D_CHECK_EQ(candidates.size(), static_cast<size_t>(1));
  CLIPD2D_CHECK_EQ(
      candidates[0].primary_text,
      clipd2d::common::clip_text_display::QuickPastePrimaryText(text, 2));
  CLIPD2D_CHECK_EQ(candidates[0].primary_text, std::wstring(L"Line 1\nLine 2"));
}

CLIPD2D_TEST(QuickPasteCandidatesShareFallbacksWithMainPanelTextHelpers) {
  ClipSummary text = MakeSummary(11, ClipContentType::Rtf, L"Fallback title");

  const auto candidates = BuildQuickPasteCandidates({text});
  CLIPD2D_CHECK_EQ(candidates.size(), static_cast<size_t>(1));
  CLIPD2D_CHECK_EQ(
      candidates[0].primary_text,
      clipd2d::common::clip_text_display::QuickPastePrimaryText(text, 2));
  CLIPD2D_CHECK_EQ(
      clipd2d::common::clip_text_display::RowContentText(text),
      std::wstring(L"Fallback title"));
}
