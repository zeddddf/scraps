#pragma once

#ifndef SCRAPS_VERSION
#define SCRAPS_VERSION "0.0.0"
#endif

#define SCRAPS_VERSION_WIDE_INDIR(x) L##x
#define SCRAPS_VERSION_W(x) SCRAPS_VERSION_WIDE_INDIR(x)

#include <windows.h>

#include <atomic>
#include <functional>
#include <string>
#include <thread>

namespace clipd2d::app {

// Checks GitHub releases for a newer version at most once per day.
// The check runs on a background thread; the callback fires on that thread and
// must marshal to the UI thread before touching windows.
class UpdateChecker {
 public:
  using UpdateCallback =
      std::function<void(std::wstring new_version, std::wstring release_url)>;

  UpdateChecker() = default;
  ~UpdateChecker();

  UpdateChecker(const UpdateChecker&) = delete;
  UpdateChecker& operator=(const UpdateChecker&) = delete;

  void StartAsync(const std::wstring& cache_dir,
                  std::wstring current_version,
                  UpdateCallback on_update);
  void Shutdown();

 private:
  void Worker(std::wstring cache_dir,
              std::wstring current_version,
              UpdateCallback on_update);

  std::thread worker_;
  std::atomic<bool> stopping_{false};
};

}  // namespace clipd2d::app
