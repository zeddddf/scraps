#pragma once

#include <windows.h>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>

namespace clipd2d::app {

class ScreenshotHostManager {
 public:
  ScreenshotHostManager() = default;
  ~ScreenshotHostManager();

  // Disable copy/assignment
  ScreenshotHostManager(const ScreenshotHostManager&) = delete;
  ScreenshotHostManager& operator=(const ScreenshotHostManager&) = delete;

  void Initialize(const std::wstring& root_dir, bool prewarm_enabled = true);
  void TriggerCapture();
  void Shutdown();

 private:
  bool StartProcess();
  void StopProcess();
  void PipeListenerLoop();
  void SendIpcCommand(const std::string& command_json);

  std::wstring root_dir_;
  std::wstring pipe_name_;
  std::wstring screenshot_exe_path_;
  bool prewarm_enabled_ = true;
  
  std::atomic<bool> running_{false};
  std::mutex mutex_;

  // Child process handles
  HANDLE child_process_ = nullptr;
  HANDLE child_thread_ = nullptr;

  // Pipe server handle
  HANDLE pipe_handle_ = INVALID_HANDLE_VALUE;

  std::thread listener_thread_;
};

}  // namespace clipd2d::app
