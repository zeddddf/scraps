#include "tests/TestSupport.h"

#include <cstdint>
#include <filesystem>

#include "common/Paths.h"
#include "core/commands/AppController.h"
#include "core/commands/RefreshRequest.h"
#include "core/store/ClipTypes.h"

namespace {

using clipd2d::common::AppPaths;
using clipd2d::common::EnsureDirectory;
using clipd2d::core::commands::AppController;
using clipd2d::core::commands::RefreshRequest;

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

}  // namespace

namespace clipd2d::tests {

// 约束：本文件禁止真实 sleep / 线程时序断言。所有断言都发生在同步调用返回
// 或 Shutdown join 完成之后，因此结果与线程调度无关（零 flaky）。

CLIPD2D_TEST(AppControllerInitializeTwiceIsIdempotent) {
  const auto temp_dir = clipd2d::tests::CreateUniqueTempDirectory(L"appctl-init-twice");
  auto cleanup = [&]() { clipd2d::tests::DeleteDirectoryIfExists(temp_dir); };
  const AppPaths paths = MakeTempAppPaths(temp_dir);

  AppController controller;
  CLIPD2D_CHECK(controller.Initialize(paths).ok());

  // 第二次 Initialize 必须直接返回 Ok：不得在 joinable 的线程对象上重复启动
  // （若真启动会 std::terminate，后续 Shutdown 的干净 join 即为间接行为断言）。
  CLIPD2D_CHECK(controller.Initialize(paths).ok());

  // 初始化后的同步查询路径可用，且库为空。
  std::int64_t count = -1;
  CLIPD2D_CHECK(controller.CountClips(clipd2d::core::store::ClipQuery{}, &count).ok());
  CLIPD2D_CHECK_EQ(count, 0);

  controller.Shutdown();
  cleanup();
}

CLIPD2D_TEST(AppControllerShutdownIsIdempotent) {
  const auto temp_dir = clipd2d::tests::CreateUniqueTempDirectory(L"appctl-shutdown-twice");
  auto cleanup = [&]() { clipd2d::tests::DeleteDirectoryIfExists(temp_dir); };
  const AppPaths paths = MakeTempAppPaths(temp_dir);

  AppController controller;
  CLIPD2D_CHECK(controller.Initialize(paths).ok());

  // 连续两次 Shutdown：第一次 join 全部 worker，第二次应为无操作、不得崩溃。
  controller.Shutdown();
  controller.Shutdown();
  cleanup();
}

CLIPD2D_TEST(AppControllerDispatcherSilencedAfterShutdown) {
  const auto temp_dir = clipd2d::tests::CreateUniqueTempDirectory(L"appctl-dispatcher");
  auto cleanup = [&]() { clipd2d::tests::DeleteDirectoryIfExists(temp_dir); };
  const AppPaths paths = MakeTempAppPaths(temp_dir);

  AppController controller;
  CLIPD2D_CHECK(controller.Initialize(paths).ok());

  int dispatch_count = 0;
  controller.SetRefreshDispatcher([&dispatch_count](const RefreshRequest&) {
    ++dispatch_count;
  });

  // 运行中的通知路径（RunStartupMaintenance → NotifyRefreshFromWorker）恰好触发一次，
  // 且为同步调用，返回即断言、不等待任何线程。
  CLIPD2D_CHECK(controller.RunStartupMaintenance().ok());
  CLIPD2D_CHECK_EQ(dispatch_count, 1);

  controller.Shutdown();

  // Shutdown 后：维护路径在 shutting_down_ 处短路，dispatcher 不再被调用。
  CLIPD2D_CHECK(controller.RunStartupMaintenance().ok());
  CLIPD2D_CHECK_EQ(dispatch_count, 1);

  // Shutdown 后：剪贴板事件路径同样直接返回，不入队、不通知。
  bool inserted = true;
  CLIPD2D_CHECK(controller.OnClipboardChanged(nullptr, 0, &inserted).ok());
  CLIPD2D_CHECK(!inserted);
  CLIPD2D_CHECK_EQ(dispatch_count, 1);

  cleanup();
}

// current_settings() 走的是 settings_ 的只读快照缓存（独立小锁），不再持业务锁。
// 缓存必须有刷新点，否则 SaveSettings 之后仍会读到旧值——这个测试就是那个守卫：
// 任何漏掉 PublishSettingsCacheLocked() 的改动都会在这里失败。
CLIPD2D_TEST(AppControllerCurrentSettingsReflectsSave) {
  const auto temp_dir = clipd2d::tests::CreateUniqueTempDirectory(L"appctl-settings-cache");
  auto cleanup = [&]() { clipd2d::tests::DeleteDirectoryIfExists(temp_dir); };
  const AppPaths paths = MakeTempAppPaths(temp_dir);

  AppController controller;
  CLIPD2D_CHECK(controller.Initialize(paths).ok());

  const clipd2d::core::store::AppSettings defaults = controller.current_settings();
  clipd2d::core::store::AppSettings changed = defaults;
  changed.list_density = clipd2d::core::store::ListDensity::Compact;
  changed.history_limit = defaults.history_limit + 37;
  changed.monitoring_enabled = !defaults.monitoring_enabled;
  CLIPD2D_CHECK(controller.SaveSettings(changed).ok());

  // 快照路径立即可见（不经过 mutex_，与保存顺序无关地一致）。
  const clipd2d::core::store::AppSettings after_save = controller.current_settings();
  CLIPD2D_CHECK(after_save.list_density == clipd2d::core::store::ListDensity::Compact);
  CLIPD2D_CHECK_EQ(after_save.history_limit, changed.history_limit);
  CLIPD2D_CHECK_EQ(after_save.monitoring_enabled, changed.monitoring_enabled);

  controller.Shutdown();
  cleanup();
}

}  // namespace clipd2d::tests
