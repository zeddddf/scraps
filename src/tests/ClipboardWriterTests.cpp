#include "core/clipboard/ClipboardWriter.h"

#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "common/Paths.h"
#include "core/clipboard/ClipboardCapture.h"
#include "core/qr/UrlQrGenerator.h"
#include "tests/TestSupport.h"

namespace {

using clipd2d::common::AppPaths;
using clipd2d::common::EnsureDirectory;

AppPaths MakeTempAppPaths(const std::filesystem::path& root) {
  AppPaths paths;
  paths.root_dir = root.wstring();
  paths.storage_dir = (root / L"storage").wstring();
  paths.image_dir = (root / L"storage" / L"images").wstring();
  paths.cache_dir = (root / L"cache").wstring();
  paths.database_path = (root / L"clipboard.db").wstring();
  EnsureDirectory(paths.root_dir);
  EnsureDirectory(paths.storage_dir);
  EnsureDirectory(paths.image_dir);
  EnsureDirectory(paths.cache_dir);
  return paths;
}

std::vector<std::wstring> GetClipboardFiles() {
  std::vector<std::wstring> files;
  if (!OpenClipboard(nullptr)) {
    return files;
  }
  HDROP hdrop = static_cast<HDROP>(GetClipboardData(CF_HDROP));
  if (hdrop != nullptr) {
    UINT count = DragQueryFileW(hdrop, 0xFFFFFFFF, nullptr, 0);
    for (UINT i = 0; i < count; ++i) {
      UINT length = DragQueryFileW(hdrop, i, nullptr, 0);
      std::wstring path(length + 1, L'\0');
      DragQueryFileW(hdrop, i, path.data(), length + 1);
      path.resize(length);
      files.push_back(path);
    }
  }
  CloseClipboard();
  return files;
}

bool HasClipboardFormat(UINT format) {
  if (!OpenClipboard(nullptr)) {
    return false;
  }
  bool found = IsClipboardFormatAvailable(format) == TRUE;
  CloseClipboard();
  return found;
}

HGLOBAL MakeGlobalMemory(const void* data, size_t bytes) {
  HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
  if (memory == nullptr) {
    return nullptr;
  }
  void* locked = GlobalLock(memory);
  if (locked == nullptr) {
    GlobalFree(memory);
    return nullptr;
  }
  std::memcpy(locked, data, bytes);
  GlobalUnlock(memory);
  return memory;
}

}  // namespace

namespace clipd2d::tests {

CLIPD2D_TEST(ClipboardWriterWritesHdropForImageClips) {
  HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(hr)) {
    throw std::runtime_error("CoInitializeEx failed");
  }

  const auto temp_dir = CreateUniqueTempDirectory(L"clipboard-writer-test");
  auto cleanup = [&]() {
    DeleteDirectoryIfExists(temp_dir);
    CoUninitialize();
  };
  const AppPaths paths = MakeTempAppPaths(temp_dir);

  // 1. Generate a valid PNG file
  std::wstring output_path;
  auto status = clipd2d::core::qr::GenerateUrlQrPng(
      paths,
      L"https://example.com/test",
      &output_path);
  if (!status.ok()) {
    cleanup();
    throw std::runtime_error("GenerateUrlQrPng failed: " + status.message);
  }
  CLIPD2D_CHECK(std::filesystem::exists(output_path));

  // 2. Clear clipboard first
  if (OpenClipboard(nullptr)) {
    EmptyClipboard();
    CloseClipboard();
  }

  // 3. Construct Image ClipRecord
  core::store::ClipRecord clip{};
  clip.summary.content_type = core::store::ClipContentType::Image;
  clip.storage_path = output_path;

  // 4. Write clip to clipboard
  status = core::clipboard::WriteClipToClipboard(clip, {}, false);
  if (!status.ok()) {
    cleanup();
    throw std::runtime_error("WriteClipToClipboard failed: " + status.message);
  }

  // 5. Verify CF_BITMAP and CF_HDROP formats are present
  CLIPD2D_CHECK(HasClipboardFormat(CF_BITMAP));
  CLIPD2D_CHECK(HasClipboardFormat(CF_HDROP));

  // 6. Verify CF_HDROP contains our PNG file path
  std::vector<std::wstring> files = GetClipboardFiles();
  CLIPD2D_CHECK_EQ(files.size(), 1u);
  CLIPD2D_CHECK_EQ(files[0], output_path);

  // 7. Clean up
  cleanup();
}

CLIPD2D_TEST(ClipboardReadGlobalTextTruncatesUnterminatedBlockAtGlobalSize) {
  // 无 NUL 的 CF_UNICODETEXT 块：必须按 GlobalSize 截断，不得越界扫描。
  const wchar_t payload[4] = {L'A', L'B', L'C', L'D'};
  HGLOBAL memory = MakeGlobalMemory(payload, sizeof(payload));
  CLIPD2D_CHECK(memory != nullptr);
  const auto result = clipd2d::core::clipboard::ReadGlobalText(memory);
  GlobalFree(memory);
  CLIPD2D_CHECK(result.status.ok());
  CLIPD2D_CHECK_EQ(result.text, std::string("ABCD"));
}

CLIPD2D_TEST(ClipboardReadGlobalTextStopsAtEmbeddedNulBeforeGlobalSize) {
  // 块内含 NUL 时在 NUL 处终止，NUL 之后的残留数据不得混入结果。
  const wchar_t payload[6] = {L'H', L'i', L'\0', L'X', L'Y', L'Z'};
  HGLOBAL memory = MakeGlobalMemory(payload, sizeof(payload));
  CLIPD2D_CHECK(memory != nullptr);
  const auto result = clipd2d::core::clipboard::ReadGlobalText(memory);
  GlobalFree(memory);
  CLIPD2D_CHECK(result.status.ok());
  CLIPD2D_CHECK_EQ(result.text, std::string("Hi"));
}

CLIPD2D_TEST(ClipboardReadGlobalTextTruncatesOversizedPayloadToGlobalSize) {
  // 逻辑文本长于 GlobalSize 时按 GlobalSize 截断（含非 wchar 对齐的尾部字节）。
  const wchar_t payload[6] = {L'A', L'B', L'C', L'D', L'E', L'\0'};
  HGLOBAL memory = MakeGlobalMemory(payload, 3 * sizeof(wchar_t));
  CLIPD2D_CHECK(memory != nullptr);
  const auto result = clipd2d::core::clipboard::ReadGlobalText(memory);
  GlobalFree(memory);
  CLIPD2D_CHECK(result.status.ok());
  CLIPD2D_CHECK_EQ(result.text, std::string("ABC"));

  const unsigned char odd_payload[7] = {'A', 0, 'B', 0, 'C', 0, 0xFF};
  HGLOBAL odd_memory = MakeGlobalMemory(odd_payload, sizeof(odd_payload));
  CLIPD2D_CHECK(odd_memory != nullptr);
  const auto odd_result = clipd2d::core::clipboard::ReadGlobalText(odd_memory);
  GlobalFree(odd_memory);
  CLIPD2D_CHECK(odd_result.status.ok());
  CLIPD2D_CHECK_EQ(odd_result.text, std::string("ABC"));
}

CLIPD2D_TEST(ClipboardReadAnsiGlobalTruncatesUnterminatedBlockAtGlobalSize) {
  // 无 NUL 的 ANSI 块（HTML/RTF 常见形态）：按 GlobalSize 截断且保留全块内容。
  const char payload[5] = {'h', 'e', 'l', 'l', 'o'};
  HGLOBAL memory = MakeGlobalMemory(payload, sizeof(payload));
  CLIPD2D_CHECK(memory != nullptr);
  const auto result = clipd2d::core::clipboard::ReadAnsiGlobal(memory);
  GlobalFree(memory);
  CLIPD2D_CHECK(result.status.ok());
  CLIPD2D_CHECK_EQ(result.text, std::string("hello"));
}

CLIPD2D_TEST(ClipboardReadAnsiGlobalStopsAtEmbeddedNulBeforeGlobalSize) {
  const char payload[10] = {'h', 't', 'm', 'l', '\0', 'j', 'u', 'n', 'k', '!'};
  HGLOBAL memory = MakeGlobalMemory(payload, sizeof(payload));
  CLIPD2D_CHECK(memory != nullptr);
  const auto result = clipd2d::core::clipboard::ReadAnsiGlobal(memory);
  GlobalFree(memory);
  CLIPD2D_CHECK(result.status.ok());
  CLIPD2D_CHECK_EQ(result.text, std::string("html"));
}

CLIPD2D_TEST(ClipboardReadGlobalTextRejectsNullHandles) {
  CLIPD2D_CHECK(!clipd2d::core::clipboard::ReadGlobalText(nullptr).status.ok());
  CLIPD2D_CHECK(!clipd2d::core::clipboard::ReadAnsiGlobal(nullptr).status.ok());
}

}  // namespace clipd2d::tests
