#include "core/commands/AppController.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "common/Utf8.h"
#include "core/store/ClipTypes.h"
#include "tests/TestSupport.h"

namespace clipd2d::tests {

namespace {

bool HasActionId(const std::vector<clipd2d::core::actions::ActionDescriptor>& actions, std::string_view id) {
  return std::any_of(actions.begin(), actions.end(), [id](const auto& action) { return action.id == id; });
}

std::string ActionIdsSummary(const std::vector<clipd2d::core::actions::ActionDescriptor>& actions) {
  std::ostringstream stream;
  bool first = true;
  for (const auto& action : actions) {
    if (!first) {
      stream << ", ";
    }
    first = false;
    stream << action.id;
  }
  return stream.str();
}

std::string ActionLabelById(const std::vector<clipd2d::core::actions::ActionDescriptor>& actions, std::string_view id) {
  const auto it = std::find_if(actions.begin(), actions.end(), [id](const auto& action) { return action.id == id; });
  return it == actions.end() ? std::string() : it->label;
}

clipd2d::core::store::ClipRecord MakeClip(clipd2d::core::store::ClipContentType type) {
  clipd2d::core::store::ClipRecord clip;
  clip.summary.content_type = type;
  clip.summary.file_count = 1;
  return clip;
}

}  // namespace

CLIPD2D_TEST(AppControllerResolveActionsAddsOpenTargetForRecognizedText) {
  clipd2d::core::commands::AppController controller;
  auto clip = MakeClip(clipd2d::core::store::ClipContentType::Text);
  clip.plain_text = " https://example.com ";

  const auto actions = controller.ResolveActions(clip);
  CLIPD2D_CHECK(HasActionId(actions, "open-target"));
  CLIPD2D_CHECK(HasActionId(actions, "show-qr"));
  CLIPD2D_CHECK(HasActionId(actions, "powershell-here-string"));
  CLIPD2D_CHECK_EQ(ActionLabelById(actions, "open-target"), std::string("打开网页"));
}

CLIPD2D_TEST(AppControllerResolveActionsAddsOpenDefaultForSingleFile) {
  clipd2d::core::commands::AppController controller;
  const std::filesystem::path temp_dir = CreateUniqueTempDirectory(L"actions-file");
  const std::filesystem::path file_path = temp_dir / L"sample.txt";
  std::ofstream output(file_path);
  output << "sample";
  output.close();

  auto clip = MakeClip(clipd2d::core::store::ClipContentType::Files);
  clip.storage_path = file_path.wstring();

  const auto actions = controller.ResolveActions(clip);
  CLIPD2D_CHECK(HasActionId(actions, "open-default"));
  CLIPD2D_CHECK(HasActionId(actions, "copy-path"));
  CLIPD2D_CHECK(HasActionId(actions, "open-folder"));
  CLIPD2D_CHECK_EQ(ActionLabelById(actions, "open-default"), std::string("打开文件"));

  DeleteDirectoryIfExists(temp_dir);
}

CLIPD2D_TEST(AppControllerResolveActionsSkipsOpenDefaultForMultiFile) {
  clipd2d::core::commands::AppController controller;
  const std::filesystem::path temp_dir = CreateUniqueTempDirectory(L"actions-multi");
  const std::filesystem::path file_path_a = temp_dir / L"a.txt";
  const std::filesystem::path file_path_b = temp_dir / L"b.txt";
  std::ofstream output_a(file_path_a);
  output_a << "sample-a";
  output_a.close();
  std::ofstream output_b(file_path_b);
  output_b << "sample-b";
  output_b.close();

  auto clip = MakeClip(clipd2d::core::store::ClipContentType::Files);
  clip.summary.file_count = 2;
  clip.plain_text =
      clipd2d::common::WideToUtf8(file_path_a.wstring() + L"\n" + file_path_b.wstring()).utf8;
  clip.storage_path = file_path_a.wstring();

  const auto actions = controller.ResolveActions(clip);
  if (!HasActionId(actions, "paste-path")) {
    throw std::runtime_error(std::string("Expected paste-path, got: ") + ActionIdsSummary(actions));
  }
  CLIPD2D_CHECK(!HasActionId(actions, "open-default"));
  CLIPD2D_CHECK(!HasActionId(actions, "copy-path"));
  CLIPD2D_CHECK(HasActionId(actions, "open-folder"));

  DeleteDirectoryIfExists(temp_dir);
}

CLIPD2D_TEST(AppControllerResolveActionsAddsOpenDefaultForImage) {
  clipd2d::core::commands::AppController controller;
  auto clip = MakeClip(clipd2d::core::store::ClipContentType::Image);
  clip.storage_path = L"C:\\temp\\image.png";

  const auto actions = controller.ResolveActions(clip);
  CLIPD2D_CHECK(HasActionId(actions, "open-default"));
  CLIPD2D_CHECK(HasActionId(actions, "open-folder"));
  CLIPD2D_CHECK_EQ(ActionLabelById(actions, "open-default"), std::string("打开图片"));
}

}  // namespace clipd2d::tests
