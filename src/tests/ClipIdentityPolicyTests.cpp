#include "tests/TestSupport.h"

#include <windows.h>

#include <filesystem>
#include <fstream>

#include "common/FileHash.h"
#include "common/StringUtil.h"
#include "core/policies/ClipIdentityPolicy.h"

namespace {

using clipd2d::core::policies::BuildImageFileSnapshotIdentityKey;
using clipd2d::core::policies::BuildTextPayloadSetIdentityKey;
using clipd2d::core::policies::ReadFileSnapshotIdentityData;
using clipd2d::core::store::CapturedClip;
using clipd2d::core::store::ClipContentType;

CapturedClip MakeTextClip(std::string plain, std::string html = {}, std::string rtf = {}) {
  CapturedClip clip;
  clip.content_type = html.empty() ? (rtf.empty() ? ClipContentType::Text : ClipContentType::Rtf) : ClipContentType::Html;
  clip.title = plain;
  clip.plain_text = std::move(plain);
  clip.html_text = std::move(html);
  clip.rtf_text = std::move(rtf);
  clip.content_hash = clipd2d::common::HashBytes(clip.plain_text + clip.html_text + clip.rtf_text);
  return clip;
}

}  // namespace

CLIPD2D_TEST(TextPayloadSetIdentityMatchesEquivalentPlainText) {
  const CapturedClip clip_a = MakeTextClip("hello\r\nworld");
  const CapturedClip clip_b = MakeTextClip("hello\nworld");
  CLIPD2D_CHECK_EQ(BuildTextPayloadSetIdentityKey(clip_a), BuildTextPayloadSetIdentityKey(clip_b));
}

CLIPD2D_TEST(TextPayloadSetIdentitySeparatesDifferentRichPayloads) {
  const CapturedClip clip_a = MakeTextClip("same", {}, "{\\rtf1 one}");
  const CapturedClip clip_b = MakeTextClip("same", {}, "{\\rtf1\\b two}");
  CLIPD2D_CHECK(BuildTextPayloadSetIdentityKey(clip_a) != BuildTextPayloadSetIdentityKey(clip_b));
}

CLIPD2D_TEST(ImageFileSnapshotIdentityChangesWhenFileChanges) {
  const std::filesystem::path temp_dir = clipd2d::tests::CreateUniqueTempDirectory(L"clipidentity");
  const std::filesystem::path file = temp_dir / L"sample.png";
  {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << "first";
  }
  const auto first = ReadFileSnapshotIdentityData(file.wstring());
  CLIPD2D_CHECK(first.has_value());
  const std::string key_a = BuildImageFileSnapshotIdentityKey(*first);
  Sleep(20);
  {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << "second-version";
  }
  const auto second = ReadFileSnapshotIdentityData(file.wstring());
  CLIPD2D_CHECK(second.has_value());
  const std::string key_b = BuildImageFileSnapshotIdentityKey(*second);
  clipd2d::tests::DeleteDirectoryIfExists(temp_dir);
  CLIPD2D_CHECK(key_a != key_b);
}
