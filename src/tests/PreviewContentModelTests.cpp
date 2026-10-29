#include "tests/TestSupport.h"

#include <fstream>

#include "common/Utf8.h"
#include "ui/PreviewContentModel.h"

namespace clipd2d::tests {

using clipd2d::ui::BuildPreviewFileList;
using clipd2d::ui::BuildPreviewHtmlDoc;
using clipd2d::ui::ClampPreviewZoom;
using clipd2d::ui::ExtractHtmlFragment;
using clipd2d::ui::FormatPreviewFileSize;
using clipd2d::ui::PreviewDocStatus;

CLIPD2D_TEST(PreviewFileListParsesPathsAndCountsSizes) {
  const auto temp_dir = CreateUniqueTempDirectory(L"preview-files");
  auto cleanup = [&]() { DeleteDirectoryIfExists(temp_dir); };
  const auto file_a = temp_dir / L"a.txt";
  const auto file_b = temp_dir / L"b.log";
  {
    std::ofstream out(file_a, std::ios::binary);
    out << "0123456789";  // 10 B
  }
  {
    std::ofstream out(file_b, std::ios::binary);
    out << "01234567890123456789";  // 20 B
  }
  const std::string utf8 =
      common::WideToUtf8(file_a.wstring()).utf8 + "\r\n" +
      common::WideToUtf8(file_b.wstring()).utf8 + "\n\n";

  const auto result = BuildPreviewFileList(utf8);

  CLIPD2D_CHECK_EQ(result.entries.size(), static_cast<size_t>(2));
  CLIPD2D_CHECK(result.entries[0].exists);
  CLIPD2D_CHECK_EQ(result.entries[0].name, std::wstring(L"a.txt"));
  CLIPD2D_CHECK_EQ(result.entries[0].size_bytes, static_cast<std::int64_t>(10));
  CLIPD2D_CHECK_EQ(result.entries[1].size_bytes, static_cast<std::int64_t>(20));
  CLIPD2D_CHECK_EQ(result.total_bytes, static_cast<std::int64_t>(30));
  CLIPD2D_CHECK_EQ(result.missing_count, 0);
  cleanup();
}

CLIPD2D_TEST(PreviewFileListKeepsMissingPathsAndCountsThem) {
  const auto result = BuildPreviewFileList("Z:\\not-exist\\ghost.bin\n");
  CLIPD2D_CHECK_EQ(result.entries.size(), static_cast<size_t>(1));
  CLIPD2D_CHECK(!result.entries[0].exists);
  CLIPD2D_CHECK_EQ(result.missing_count, 1);
  CLIPD2D_CHECK_EQ(result.total_bytes, static_cast<std::int64_t>(0));
}

CLIPD2D_TEST(PreviewFileListIgnoresBlankLines) {
  const auto result = BuildPreviewFileList("\n  \n\n");
  CLIPD2D_CHECK(result.entries.empty());
}

CLIPD2D_TEST(PreviewFileSizeFormatsHumanReadable) {
  CLIPD2D_CHECK_EQ(FormatPreviewFileSize(512), std::wstring(L"512 B"));
  CLIPD2D_CHECK_EQ(FormatPreviewFileSize(2048), std::wstring(L"2.0 KB"));
  CLIPD2D_CHECK_EQ(FormatPreviewFileSize(5 * 1024 * 1024), std::wstring(L"5.0 MB"));
  CLIPD2D_CHECK_EQ(FormatPreviewFileSize(-1), std::wstring(L"—"));
}

CLIPD2D_TEST(PreviewZoomClampsToOneAndFiveWithStepSnapping) {
  CLIPD2D_CHECK_EQ(ClampPreviewZoom(0.2f), 1.0f);
  CLIPD2D_CHECK_EQ(ClampPreviewZoom(9.0f), 5.0f);
  CLIPD2D_CHECK_EQ(ClampPreviewZoom(1.7000002f), 1.7f);
  CLIPD2D_CHECK_EQ(ClampPreviewZoom(-1.0f), 1.0f);
}

CLIPD2D_TEST(PreviewHtmlDocWrapsFragmentWithThemeColors) {
  std::wstring doc;
  const PreviewDocStatus status = BuildPreviewHtmlDoc("<b>hi</b>", true, &doc);
  CLIPD2D_CHECK(status == PreviewDocStatus::kOk);
  CLIPD2D_CHECK(doc.find(L"<b>hi</b>") != std::wstring::npos);
  CLIPD2D_CHECK(doc.find(L"background:#2c2c2e") != std::wstring::npos);
  std::wstring light_doc;
  (void)BuildPreviewHtmlDoc("<i>x</i>", false, &light_doc);
  CLIPD2D_CHECK(light_doc.find(L"background:#f7f9fd") != std::wstring::npos);
}

CLIPD2D_TEST(PreviewHtmlDocRejectsOversizedFragment) {
  std::string huge(600 * 1024, 'x');
  std::wstring doc;
  const PreviewDocStatus status = BuildPreviewHtmlDoc(huge, true, &doc);
  CLIPD2D_CHECK(status == PreviewDocStatus::kTooLarge);
  CLIPD2D_CHECK(doc.empty());
}

CLIPD2D_TEST(ExtractHtmlFragmentPrefersStartEndFragmentMarkers) {
  const std::string raw =
      "Version:0.9\r\nStartHTML:0000000105\r\nEndHTML:0000000191\r\n"
      "StartFragment:0000000139\r\nEndFragment:0000000161\r\n"
      "<html><body><!--StartFragment--><b>hello</b><!--EndFragment--></body></html>";
  CLIPD2D_CHECK_EQ(ExtractHtmlFragment(raw), std::string("<b>hello</b>"));
}

CLIPD2D_TEST(ExtractHtmlFragmentFallsBackToHeaderOffsets) {
  // 无 fragment 标记：按 StartHTML/EndHTML 偏移截取正文。
  // 头长 57 字节（"Version:0.9\r\n"=13 + 2 行头各 22/20 + 空行 2），正文 38 字节。
  const std::string body = "<html><body><p>plain</p></body></html>";
  const std::string header = "Version:0.9\r\nStartHTML:0000000057\r\nEndHTML:0000000095\r\n\r\n";
  const std::string raw = header + body;
  CLIPD2D_CHECK_EQ(ExtractHtmlFragment(raw), body);
}

CLIPD2D_TEST(ExtractHtmlFragmentPrefersFragmentOffsetsOverHtmlOffsets) {
  // 无标记但头里有 StartFragment/EndFragment：截正文而非整个 <html> 文档
  // （整文档嵌套进包壳会撑高页面、滚动条外露）。
  // 头：Version 行 13 + fragment 两行 26/24 + html 两行 22/20 + 空行 2 = 107。
  // 正文 "<html><body><p>x</p></body></html>" 34 字节；fragment "<p>x</p>" 8 字节，
  // 位于正文偏移 12，即载荷偏移 119，结束 127。
  const std::string body = "<html><body><p>x</p></body></html>";
  const std::string header =
      "Version:0.9\r\nStartFragment:0000000119\r\nEndFragment:0000000127\r\n"
      "StartHTML:0000000107\r\nEndHTML:0000000141\r\n\r\n";
  const std::string raw = header + body;
  CLIPD2D_CHECK_EQ(header.size(), static_cast<size_t>(107));
  CLIPD2D_CHECK_EQ(ExtractHtmlFragment(raw), std::string("<p>x</p>"));
}

CLIPD2D_TEST(ExtractHtmlFragmentReturnsRawWhenNoMarkersOrHeader) {
  const std::string raw = "<div>no header at all</div>";
  CLIPD2D_CHECK_EQ(ExtractHtmlFragment(raw), raw);
  CLIPD2D_CHECK(ExtractHtmlFragment("").empty());
}

}  // namespace clipd2d::tests
