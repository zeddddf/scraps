#include "common/StartupTrace.h"

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_set>

#include "common/Log.h"

namespace clipd2d::common {

namespace {

struct StartupTraceState {
  std::mutex mutex;
  std::chrono::steady_clock::time_point start{};
  std::chrono::steady_clock::time_point last{};
  bool active = false;
  std::unordered_set<std::string> emitted;
};

StartupTraceState g_startup_trace_state;

}  // namespace

void ResetStartupTrace() {
  const auto now = std::chrono::steady_clock::now();
  std::scoped_lock lock(g_startup_trace_state.mutex);
  g_startup_trace_state.start = now;
  g_startup_trace_state.last = now;
  g_startup_trace_state.active = true;
  g_startup_trace_state.emitted.clear();
}

void MarkStartupPhase(std::string_view phase) {
  const auto now = std::chrono::steady_clock::now();
  std::scoped_lock lock(g_startup_trace_state.mutex);
  if (!g_startup_trace_state.active) {
    g_startup_trace_state.start = now;
    g_startup_trace_state.last = now;
    g_startup_trace_state.active = true;
  }

  const std::string key(phase);
  if (!g_startup_trace_state.emitted.insert(key).second) {
    return;
  }

  const auto total_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - g_startup_trace_state.start).count();
  const auto delta_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - g_startup_trace_state.last).count();
  g_startup_trace_state.last = now;
  common::WriteLog(
      common::LogLevel::Info,
      "app.startup",
      key + " total_ms=" + std::to_string(total_ms) + " delta_ms=" + std::to_string(delta_ms));
}

}  // namespace clipd2d::common
