// Scroll / scrollbar-drag benchmark harness (--bench-scroll).
//
// Measures the real render + data path under two scripted scenarios:
//   Phase A — steady whole-range scroll (uniform offset steps)
//   Phase B — scrollbar thumb drag (drives the real OnMouseMove drag path)
// Per frame it records wall/CPU cost, skeleton ("降级") rows in view, and
// whether ComputeRequestRange actually covers the visible rows. Phase B also
// records thumb-follow error and bottom coverage.
//
// Output: outputs/scroll_bench_<tag>.md (tag from --bench-scroll=<tag>).

#include "ui/MainWindowInternal.h"
#include "ui/vlist/RowHeightModel.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace clipd2d::ui {
namespace {

struct FrameSample {
  double wall_ms = 0.0;
  double cpu_ms = 0.0;
  int skeleton_rows = 0;
  int visible_rows = 0;
  bool range_miss = false;
  int range_miss_rows = 0;
};

struct PhaseStats {
  std::vector<double> wall;
  std::vector<double> cpu;
  int frames = 0;
  int skeleton_frames = 0;
  int skeleton_rows_total = 0;
  int visible_rows_total = 0;
  int range_miss_frames = 0;
  int range_miss_rows_max = 0;

  void Add(const FrameSample& s) {
    wall.push_back(s.wall_ms);
    cpu.push_back(s.cpu_ms);
    ++frames;
    visible_rows_total += s.visible_rows;
    if (s.skeleton_rows > 0) {
      ++skeleton_frames;
      skeleton_rows_total += s.skeleton_rows;
    }
    if (s.range_miss) {
      ++range_miss_frames;
      range_miss_rows_max = std::max(range_miss_rows_max, s.range_miss_rows);
    }
  }
};

double Percentile(std::vector<double> values, double p) {
  if (values.empty()) {
    return 0.0;
  }
  std::sort(values.begin(), values.end());
  const double idx = p * static_cast<double>(values.size() - 1);
  const size_t lo = static_cast<size_t>(idx);
  const size_t hi = std::min(values.size() - 1, lo + 1);
  const double frac = idx - static_cast<double>(lo);
  return values[lo] * (1.0 - frac) + values[hi] * frac;
}

double Average(const std::vector<double>& values) {
  if (values.empty()) {
    return 0.0;
  }
  double sum = 0.0;
  for (double v : values) {
    sum += v;
  }
  return sum / static_cast<double>(values.size());
}

double Maximum(const std::vector<double>& values) {
  double m = 0.0;
  for (double v : values) {
    m = std::max(m, v);
  }
  return m;
}

std::string F2(double v) {
  char buf[64];
  snprintf(buf, sizeof(buf), "%.2f", v);
  return buf;
}

std::string F1(double v) {
  char buf[64];
  snprintf(buf, sizeof(buf), "%.1f", v);
  return buf;
}

std::string Percent(int part, int whole) {
  if (whole <= 0) {
    return "0%";
  }
  return F1(100.0 * static_cast<double>(part) / static_cast<double>(whole)) + "%";
}

void PumpMessages(DWORD budget_ms) {
  const ULONGLONG end = GetTickCount64() + budget_ms;
  for (;;) {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) {
        // Re-post so the outer GetMessage loop can observe the quit.
        PostQuitMessage(static_cast<int>(msg.wParam));
        return;
      }
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    const ULONGLONG now = GetTickCount64();
    if (now >= end) {
      return;
    }
    MsgWaitForMultipleObjects(0, nullptr, FALSE, static_cast<DWORD>(end - now), QS_ALLINPUT);
  }
}

std::wstring ResolveReportDir() {
  wchar_t module[MAX_PATH]{};
  GetModuleFileNameW(nullptr, module, MAX_PATH);
  std::wstring path(module);
  const size_t slash = path.find_last_of(L"\\/");
  if (slash != std::wstring::npos) {
    path.resize(slash);
  }
  // build/Release -> repo root
  std::wstring candidate = path + L"\\..\\..\\outputs";
  DWORD attrs = GetFileAttributesW(candidate.c_str());
  if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
    return candidate;
  }
  candidate = L"outputs";
  CreateDirectoryW(candidate.c_str(), nullptr);
  return candidate;
}

bool WriteReportFile(const std::wstring& path, const std::string& utf8) {
  HANDLE file = CreateFileW(
      path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return false;
  }
  DWORD written = 0;
  const BOOL ok = WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
  CloseHandle(file);
  return ok == TRUE;
}

}  // namespace

void MainWindow::MaybeArmScrollBench() {
  const wchar_t* cmd = GetCommandLineW();
  common::WriteLog(
      common::LogLevel::Info,
      "ui.bench",
      std::string("command line: ") + (cmd != nullptr ? "present" : "null") +
          (cmd != nullptr && wcsstr(cmd, L"--bench-scroll") != nullptr ? " flag=yes" : " flag=no"));
  if (cmd == nullptr || wcsstr(cmd, L"--bench-scroll") == nullptr) {
    return;
  }
  SetTimer(hwnd_, kScrollBenchTimerId, 600, nullptr);
  common::WriteLog(common::LogLevel::Info, "ui.bench", "scroll bench armed via --bench-scroll");
}

void MainWindow::RunScrollBenchmark() {
  KillTimer(hwnd_, kScrollBenchTimerId);

  // Report tag: --bench-scroll or --bench-scroll=<tag>
  std::string tag = "report";
  if (const wchar_t* cmd = GetCommandLineW()) {
    if (const wchar_t* eq = wcsstr(cmd, L"--bench-scroll=")) {
      std::wstring t(eq + 15);  // skip "--bench-scroll="
      for (wchar_t& c : t) {
        if (c == L' ' || c == L'"') {
          c = 0;
          break;
        }
      }
      if (!t.empty()) {
        tag.assign(t.begin(), t.end());
      }
    }
  }

  // Keep the panel visible and pinned so auto-hide does not interfere.
  panel_locked_ = true;
  ShowWindow(hwnd_, SW_SHOW);
  SetForegroundWindow(hwnd_);

  // Wait for the first data commit (bounded).
  const ULONGLONG data_deadline = GetTickCount64() + 10000;
  while (GetTickCount64() < data_deadline) {
    PumpMessages(50);
    if (item_count_ > 0 && !page_cache_.empty() && display_generation_ != 0) {
      break;
    }
  }
  PumpMessages(400);  // let async page loads + RebuildViewportHeights settle

  RECT client{};
  GetClientRect(hwnd_, &client);
  PanelLayout layout = ComputeLayout(client.right, client.bottom);
  const int viewport_h = std::max(1, static_cast<int>(layout.list.bottom - layout.list.top));
  const int track_h = std::max(1, static_cast<int>(layout.scrollbar_track.bottom - layout.scrollbar_track.top));
  const int track_top = layout.scrollbar_track.top;

  std::string report;
  report += "# 滚动/拖拽基准报告 (" + tag + ")\n\n";
  report += "生成时间: " + std::to_string(GetTickCount64() / 1000) + "s (tick64)  进程: " +
      std::to_string(GetCurrentProcessId()) + "\n\n";

  if (item_count_ <= 0) {
    report += "**无数据：等待列表加载超时，跳过测量。**\n";
    const std::wstring dir = ResolveReportDir();
    const std::wstring path = dir + L"\\scroll_bench_" + std::wstring(tag.begin(), tag.end()) + L".md";
    WriteReportFile(path, report);
    common::WriteLog(common::LogLevel::Warning, "ui.bench", "scroll bench: no data");
    PostQuitMessage(0);
    return;
  }

  // ---- Height model stats -------------------------------------------------
  const int content_w = std::max(80, static_cast<int>(layout.list.right - layout.list.left) - 2 * kRowPaddingX);
  int measured_min = INT_MAX;
  int measured_max = 0;
  long long measured_sum = 0;
  int measured_count = 0;
  for (int page = 0; page * page_size_ < item_count_; ++page) {
    auto it = page_cache_.find(page);
    if (it == page_cache_.end()) {
      continue;
    }
    for (size_t off = 0; off < it->second.size(); ++off) {
      const bool compact =
          controller_ != nullptr &&
          controller_->current_settings().list_density == core::store::ListDensity::Compact;
      const int h = vlist::RowHeightModel::Measure(it->second[off], content_w, compact);
      measured_min = std::min(measured_min, h);
      measured_max = std::max(measured_max, h);
      measured_sum += h;
      ++measured_count;
    }
  }
  const int total_height = viewport_.TotalHeight();
  const int fixed_estimate = item_count_ * GetItemHeight();

  report += "## 数据与高度模型\n\n";
  report += "- 条目数: " + std::to_string(item_count_) + "\n";
  report += "- vlist TotalHeight: " + std::to_string(total_height) + " px\n";
  report += "- 固定行高估算 (N * GetItemHeight=" + std::to_string(GetItemHeight()) + "): " +
      std::to_string(fixed_estimate) + " px  (偏差 " +
      F1(100.0 * (static_cast<double>(fixed_estimate) - total_height) / std::max(1, total_height)) + "%)\n";
  if (measured_count > 0) {
    report += "- 已加载行高样本 (n=" + std::to_string(measured_count) + "): min " +
        std::to_string(measured_min) + " / avg " +
        F1(static_cast<double>(measured_sum) / measured_count) + " / max " +
        std::to_string(measured_max) + " px\n";
  }
  report += "- 视口高: " + std::to_string(viewport_h) + " px, 滚动条轨道: " + std::to_string(track_h) + " px\n";
  report += "- MaxScrollOffset: " + std::to_string(scroll_controller_->MaxScrollOffset()) + " px\n\n";

  // ---- Shared per-frame measurement --------------------------------------
  const auto measure_frame = [&](FrameSample* out) {
    ULONGLONG cpu0 = 0;
    ULONGLONG cpu1 = 0;
    QueryThreadCycleTime(GetCurrentThread(), &cpu0);
    LARGE_INTEGER t0{};
    LARGE_INTEGER t1{};
    QueryPerformanceCounter(&t0);
    RenderFrame();
    QueryPerformanceCounter(&t1);
    QueryThreadCycleTime(GetCurrentThread(), &cpu1);
    out->wall_ms =
        (t1.QuadPart - t0.QuadPart) * 1000.0 / static_cast<double>(performance_counter_frequency_.QuadPart);
    out->cpu_ms = static_cast<double>(cpu1 - cpu0) / 1000.0;  // kcycles (raw cycles / 1000)

    const float scroll = scroll_controller_->VisualScrollOffset();
    const vlist::VisibleRange vis = viewport_.RangeAt(scroll, viewport_h, 0, 0);
    out->visible_rows = 0;
    out->skeleton_rows = 0;
    for (int i = std::max(0, vis.first); i <= std::min(item_count_ - 1, vis.last); ++i) {
      ++out->visible_rows;
      if (GetSummaryAt(i) == nullptr) {
        ++out->skeleton_rows;
      }
    }
    int first_req = 0;
    int last_req = -1;
    ComputeRequestRange(
        item_count_,
        GetItemHeight(),
        viewport_h,
        scroll_controller_->CurrentScrollOffset(),
        scroll_controller_->GetTargetScrollOffset(),
        scroll_controller_->GetScrollVelocity(),
        &first_req,
        &last_req,
        &viewport_.heights());
    const int uncovered_first = first_req - std::max(0, vis.first);
    const int uncovered_last = std::min(item_count_ - 1, vis.last) - last_req;
    out->range_miss_rows = std::max(0, std::max(uncovered_first, uncovered_last));
    out->range_miss = out->range_miss_rows > 0;
  };

  // ---- Phase A: steady whole-range scroll ---------------------------------
  PhaseStats phase_a;
  {
    const int steps = 400;
    const int max_scroll = scroll_controller_->MaxScrollOffset();
    const int step_px = std::max(1, max_scroll / steps);
    for (int i = 0; i <= steps; ++i) {
      scroll_controller_->SetScrollOffsetTarget(std::min(max_scroll, i * step_px), true);
      FrameSample sample;
      measure_frame(&sample);
      phase_a.Add(sample);
      PumpMessages(16);
    }
  }

  // ---- Phase B: scrollbar thumb drag sweep --------------------------------
  PhaseStats phase_b;
  double follow_err_sum = 0.0;
  double follow_err_max = 0.0;
  int follow_samples = 0;
  double map_err_sum = 0.0;
  double map_err_max = 0.0;
  int bottom_achieved = 0;
  {
    const int thumb_x = (layout.scrollbar_track.left + layout.scrollbar_track.right) / 2;
    const int max_scroll = std::max(1, scroll_controller_->MaxScrollOffset());
    const float list_h = static_cast<float>(viewport_h);
    const float content_h = static_cast<float>(std::max(1, viewport_.TotalHeight()));
    const float track_f = static_cast<float>(track_h);
    const float thumb_h = std::max(44.0f, track_f * (list_h / content_h));
    const float travel = std::max(1.0f, track_f - thumb_h);

    drag_scrollbar_ = true;
    const int steps = 320;
    for (int i = 0; i <= steps; ++i) {
      const int y = track_top + static_cast<int>(std::lround(travel * (static_cast<double>(i) / steps))) +
          static_cast<int>(thumb_h / 2);
      POINT pt{thumb_x, y};
      OnMouseMove(pt);
      FrameSample sample;
      measure_frame(&sample);
      phase_b.Add(sample);

      // Thumb follow error: rendered thumb center (render math) vs pointer.
      const float ratio = scroll_controller_->VisualScrollOffset() /
          std::max(1.0f, content_h - list_h);
      const float thumb_center = static_cast<float>(track_top) + travel * ratio + thumb_h * 0.5f;
      const double follow = std::fabs(static_cast<double>(thumb_center - static_cast<float>(y)));
      follow_err_sum += follow;
      follow_err_max = std::max(follow_err_max, follow);
      ++follow_samples;

      // Mapping error: what the pointer should map to (render geometry) vs
      // what the drag path actually targeted.
      const float correct_ratio =
          std::clamp((static_cast<float>(y) - static_cast<float>(track_top) - thumb_h * 0.5f) / travel, 0.0f, 1.0f);
      const int intended = static_cast<int>(std::lround(correct_ratio * (content_h - list_h)));
      const int achieved = scroll_controller_->GetTargetScrollOffset();
      const double map_err = std::fabs(static_cast<double>(achieved - intended));
      map_err_sum += map_err;
      map_err_max = std::max(map_err_max, map_err);
      if (i == steps) {
        bottom_achieved = achieved;
      }
      PumpMessages(16);
    }
    drag_scrollbar_ = false;
  }

  // ---- Phase C: cold-cache jump exposure ---------------------------------
  // Mimics "hidden cache trim -> reopen -> drag to a far position": drop every
  // cached page except page 0, jump through the real drag path, and count how
  // many frames (at 16ms pacing) until no skeleton rows remain.
  struct ColdJump {
    int frames_to_content = 0;
    int first_frame_skeleton = 0;
  };
  std::vector<ColdJump> cold_jumps;
  {
    auto saved_cache = page_cache_;
    auto saved_ticks = page_access_ticks_;
    for (auto it = page_cache_.begin(); it != page_cache_.end();) {
      if (it->first != 0) {
        page_access_ticks_.erase(it->first);
        it = page_cache_.erase(it);
      } else {
        ++it;
      }
    }
    const int thumb_x = (layout.scrollbar_track.left + layout.scrollbar_track.right) / 2;
    for (int jump = 1; jump <= 8; ++jump) {
      const int y = track_top + (track_h * jump) / 9;
      drag_scrollbar_ = true;
      OnMouseMove(POINT{thumb_x, y});
      drag_scrollbar_ = false;

      ColdJump result;
      FrameSample sample;
      measure_frame(&sample);
      result.first_frame_skeleton = sample.skeleton_rows;
      result.frames_to_content = 1;
      while (sample.skeleton_rows > 0 && result.frames_to_content < 60) {
        PumpMessages(16);
        measure_frame(&sample);
        ++result.frames_to_content;
      }
      cold_jumps.push_back(result);
      PumpMessages(16);
    }
    page_cache_ = std::move(saved_cache);
    page_access_ticks_ = std::move(saved_ticks);
  }

  // ---- Emit report --------------------------------------------------------
  const auto phase_section = [&report](const std::string& title, const PhaseStats& s) {
    report += "## " + title + "\n\n";
    report += "- 帧数: " + std::to_string(s.frames) + "\n";
    report += "- 帧耗时 wall ms: avg " + F2(Average(s.wall)) + " / p50 " + F2(Percentile(s.wall, 0.5)) +
        " / p95 " + F2(Percentile(s.wall, 0.95)) + " / max " + F2(Maximum(s.wall)) + "\n";
    report += "- 帧耗时 cpu kcycles (QueryThreadCycleTime/1000, ~3GHz 下 3000≈1ms): avg " + F2(Average(s.cpu)) + " / p95 " +
        F2(Percentile(s.cpu, 0.95)) + " / max " + F2(Maximum(s.cpu)) + "\n";
    report += "- 骨架行(降级显示)帧占比: " + Percent(s.skeleton_frames, s.frames) + "  (平均受影响行 " +
        (s.skeleton_frames > 0
             ? F2(static_cast<double>(s.skeleton_rows_total) / s.skeleton_frames)
             : std::string("0.00")) +
        " / 平均可见 " +
        (s.frames > 0 ? F1(static_cast<double>(s.visible_rows_total) / s.frames) : std::string("0")) + ")\n";
    report += "- 预取范围未覆盖真实可见行: 帧占比 " + Percent(s.range_miss_frames, s.frames) +
        "  (最大未覆盖 " + std::to_string(s.range_miss_rows_max) + " 行)\n\n";
  };

  phase_section("Phase A — 匀速全范围滚动（400 步等距 offset 跳转）", phase_a);

  phase_section("Phase B — 滚动条拖拽（真实 OnMouseMove 拖拽路径，320 步扫过轨道）", phase_b);
  report += "- 滑块跟随误差 (渲染滑块中心 vs 指针): avg " +
      F1(follow_samples > 0 ? follow_err_sum / follow_samples : 0.0) + " px / max " +
      F1(follow_err_max) + " px\n";
  report += "- 拖拽映射误差 (实际 offset vs 指针几何应有的 offset): avg " +
      F1(follow_samples > 0 ? map_err_sum / follow_samples : 0.0) + " px / max " +
      F1(map_err_max) + " px\n";
  report += "- 拖到底覆盖率: achieved " + std::to_string(bottom_achieved) + " / MaxScrollOffset " +
      std::to_string(scroll_controller_->MaxScrollOffset()) + "  (" +
      Percent(bottom_achieved, std::max(1, scroll_controller_->MaxScrollOffset())) + ")\n\n";

  {
    double frames_sum = 0.0;
    int frames_max = 0;
    double first_skel_sum = 0.0;
    int first_skel_max = 0;
    for (const ColdJump& jump : cold_jumps) {
      frames_sum += jump.frames_to_content;
      frames_max = std::max(frames_max, jump.frames_to_content);
      first_skel_sum += jump.first_frame_skeleton;
      first_skel_max = std::max(first_skel_max, jump.first_frame_skeleton);
    }
    const int n = static_cast<int>(cold_jumps.size());
    report += "## Phase C — 冷缓存跳转（只留第 0 页，模拟隐藏裁剪后重开；真实拖拽路径跳 8 个位置）\n\n";
    report += "- 跳转后骨架持续帧数 (16ms 节奏, 上限 60): avg " +
        F1(n > 0 ? frames_sum / n : 0.0) + " / max " + std::to_string(frames_max) + "\n";
    report += "- 首帧骨架行数: avg " + F1(n > 0 ? first_skel_sum / n : 0.0) + " / max " +
        std::to_string(first_skel_max) + "\n";
    report += "- 跳转次数: " + std::to_string(n) + "\n\n";
  }

  report += "## 口径\n\n";
  report += "- wall = QueryPerformanceCounter 包住 RenderFrame()（含 Present(1,0) 的 vsync 等待）\n";
  report += "- cpu = QueryThreadCycleTime 差值换算为 kcycles（排除 vsync 阻塞的纯渲染 CPU 成本）\n";
  report += "- 骨架行 = 可见行内 GetSummaryAt() == nullptr，即未加载页的“降级显示”占位\n";
  report += "- 预取覆盖 = ComputeRequestRange 的 [first,last] 是否包住 viewport_.RangeAt 的真实可见行\n";
  report += "- Phase B 每步经 OnMouseMove 真实拖拽代码路径（含其行高映射），步间 pump 16ms 消息\n";
  report += "- Phase C 骨架持续帧 = 跳转后到首个无骨架帧为止的帧数（每帧间 pump 16ms，即含异步加载往返）\n";

  const std::wstring dir = ResolveReportDir();
  const std::wstring path = dir + L"\\scroll_bench_" + std::wstring(tag.begin(), tag.end()) + L".md";
  WriteReportFile(path, report);
  common::WriteLog(common::LogLevel::Info, "ui.bench", "scroll bench report written, exiting");
  PostQuitMessage(0);
}

}  // namespace clipd2d::ui
