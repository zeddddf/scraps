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
#include "core/clipboard/ClipboardFormat.h"
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

// 测试辅助：清空剪贴板并消化一次会话增量，返回清空后的 sequence。
DWORD ClearClipboardAndReadSequence() {
  if (OpenClipboard(nullptr)) {
    EmptyClipboard();
    CloseClipboard();
  }
  return GetClipboardSequenceNumber();
}

// Bug A 会话数探测点的测试侧计数器（回调必须是无捕获函数指针）。
int g_clipboard_session_count = 0;
void CountClipboardSession() {
  ++g_clipboard_session_count;
}

// Bug A 回归锁：图片写回必须收敛为单次剪贴板会话。
// 直接测量写回期间 ClipboardWriter 打开的会话数；若写回跨多个
// Open/Empty/Close 会话，探测计数会大于 1。
// 注意：不能用 sequence 精确增量做断言——系统放置 CF_DIB 时会自动
// 合成派生格式，每次合成都使 sequence 额外 +1（增量不确定）。
CLIPD2D_TEST(ClipboardWriterImageWritebackFromPngUsesSingleClipboardSession) {
  HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(hr)) {
    throw std::runtime_error("CoInitializeEx failed");
  }
  const auto temp_dir = CreateUniqueTempDirectory(L"clipboard-session-test");
  auto cleanup = [&]() {
    DeleteDirectoryIfExists(temp_dir);
    CoUninitialize();
  };
  const AppPaths paths = MakeTempAppPaths(temp_dir);

  std::wstring output_path;
  auto status = clipd2d::core::qr::GenerateUrlQrPng(
      paths,
      L"https://example.com/single-session",
      &output_path);
  if (!status.ok()) {
    cleanup();
    throw std::runtime_error("GenerateUrlQrPng failed: " + status.message);
  }

  ClearClipboardAndReadSequence();

  g_clipboard_session_count = 0;
  core::clipboard::SetClipboardSessionProbe(&CountClipboardSession);

  core::store::ClipRecord clip{};
  clip.summary.content_type = core::store::ClipContentType::Image;
  clip.storage_path = output_path;
  status = core::clipboard::WriteClipToClipboard(clip, {}, false);
  core::clipboard::SetClipboardSessionProbe(nullptr);
  if (!status.ok()) {
    cleanup();
    throw std::runtime_error("WriteClipToClipboard failed: " + status.message);
  }

  CLIPD2D_CHECK_EQ(g_clipboard_session_count, 1);

  // 组合写入后 payload 齐全：派生格式 + 标准格式 + 文件列表全部就位。
  CLIPD2D_CHECK(HasClipboardFormat(CF_DIB));
  CLIPD2D_CHECK(HasClipboardFormat(CF_DIBV5));
  CLIPD2D_CHECK(HasClipboardFormat(CF_BITMAP));
  CLIPD2D_CHECK(HasClipboardFormat(CF_HDROP));
  CLIPD2D_CHECK(HasClipboardFormat(clipd2d::core::clipboard::GetPngClipboardFormat()));

  cleanup();
}

// Bug A 回归锁：无 storage_path 的图片条目走 SetClipboardImageClip，
// 格式 payload 全部在内存预置，写回同样只含一次会话。
CLIPD2D_TEST(ClipboardWriterImageClipFromFormatsUsesSingleClipboardSession) {
  HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  if (FAILED(hr)) {
    throw std::runtime_error("CoInitializeEx failed");
  }
  const auto temp_dir = CreateUniqueTempDirectory(L"clipboard-formats-test");
  auto cleanup = [&]() {
    DeleteDirectoryIfExists(temp_dir);
    CoUninitialize();
  };

  ClearClipboardAndReadSequence();

  // 1x1 32bpp DIB（BITMAPINFOHEADER + 4 字节像素）与 DIBV5（BITMAPV5HEADER + 4 字节像素）。
  std::vector<std::uint8_t> dib(sizeof(BITMAPINFOHEADER) + 4, 0);
  auto* dib_header = reinterpret_cast<BITMAPINFOHEADER*>(dib.data());
  dib_header->biSize = sizeof(BITMAPINFOHEADER);
  dib_header->biWidth = 1;
  dib_header->biHeight = -1;
  dib_header->biPlanes = 1;
  dib_header->biBitCount = 32;
  dib_header->biCompression = BI_RGB;

  std::vector<std::uint8_t> dibv5(sizeof(BITMAPV5HEADER) + 4, 0);
  auto* dibv5_header = reinterpret_cast<BITMAPV5HEADER*>(dibv5.data());
  dibv5_header->bV5Size = sizeof(BITMAPV5HEADER);
  dibv5_header->bV5Width = 1;
  dibv5_header->bV5Height = -1;
  dibv5_header->bV5Planes = 1;
  dibv5_header->bV5BitCount = 32;
  dibv5_header->bV5Compression = BI_BITFIELDS;
  dibv5_header->bV5RedMask = 0x00FF0000;
  dibv5_header->bV5GreenMask = 0x0000FF00;
  dibv5_header->bV5BlueMask = 0x000000FF;
  dibv5_header->bV5AlphaMask = 0xFF000000;

  core::store::ClipRecord clip{};
  clip.summary.content_type = core::store::ClipContentType::Image;
  clip.formats.push_back({"CF_DIB", core::store::ClipFormatPayloadKind::Binary, {}, dib, {}, 0});
  clip.formats.push_back({"CF_DIBV5", core::store::ClipFormatPayloadKind::Binary, {}, dibv5, {}, 1});

  g_clipboard_session_count = 0;
  core::clipboard::SetClipboardSessionProbe(&CountClipboardSession);
  auto status = core::clipboard::WriteClipToClipboard(clip, {}, false);
  core::clipboard::SetClipboardSessionProbe(nullptr);
  if (!status.ok()) {
    cleanup();
    throw std::runtime_error("WriteClipToClipboard failed: " + status.message);
  }

  CLIPD2D_CHECK_EQ(g_clipboard_session_count, 1);

  CLIPD2D_CHECK(HasClipboardFormat(CF_DIB));
  CLIPD2D_CHECK(HasClipboardFormat(CF_DIBV5));

  // 回读 CF_DIB payload，确认写回的字节与输入一致（头 + 1 像素）。
  CLIPD2D_CHECK(OpenClipboard(nullptr));
  HGLOBAL dib_data = static_cast<HGLOBAL>(GetClipboardData(CF_DIB));
  CLIPD2D_CHECK(dib_data != nullptr);
  if (dib_data != nullptr) {
    CLIPD2D_CHECK_EQ(GlobalSize(dib_data), dib.size());
    const void* locked = GlobalLock(dib_data);
    CLIPD2D_CHECK(locked != nullptr);
    if (locked != nullptr) {
      CLIPD2D_CHECK(std::memcmp(locked, dib.data(), dib.size()) == 0);
      GlobalUnlock(dib_data);
    }
  }
  CloseClipboard();

  cleanup();
}

}  // namespace clipd2d::tests
