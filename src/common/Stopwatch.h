#pragma once

// 高精度计时（QPC），微秒精度。
//
// 为什么不能用 GetTickCount64 给热路径计时：
// 它读的是系统时钟 tick，默认分辨率 15.625ms。一次真实耗时 0.4ms 的调用，
// 只要恰好跨过 tick 边界就会读出 15 或 16ms；连续测量时读数只会在
// 0 / 16 / 31 / 47 / 62 ... 这些离散值上跳。曾据此产生两个错误结论：
//   1) "Present 阻塞 16ms"：其实是 16ms 量化格里的任意小数，可能仅 1ms；
//   2) "725 条慢消息"：无法区分真慢与假慢，探针等于失效。
// 凡是判断"是否超过 X 毫秒预算"的代码，必须用本工具，否则阈值判断不可信。
//
// 用法：
//   common::Stopwatch sw;
//   DoWork();
//   if (sw.ElapsedUs() >= 16000) { ... }

#include <windows.h>

#include <cstdint>
#include <string>

namespace clipd2d::common {

class Stopwatch {
 public:
  Stopwatch() noexcept {
    LARGE_INTEGER frequency{};
    if (QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0) {
      frequency_ = frequency.QuadPart;
    }
    QueryPerformanceCounter(&start_);
  }

  void Restart() noexcept { QueryPerformanceCounter(&start_); }

  // 自构造（或上次 Restart）以来经过的微秒数（向下截断到整数微秒）。
  // 亚微秒的区间会返回 0——判断"是否超预算"只需微秒级，不必为此引入浮点。
  std::int64_t ElapsedUs() const noexcept {
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    const std::int64_t delta = now.QuadPart - start_.QuadPart;
    // QPC 频率通常是 10MHz 量级，先乘后除会溢出，这里按微秒取整。
    return delta * 1000000 / frequency_;
  }

  double ElapsedMs() const noexcept {
    return static_cast<double>(ElapsedUs()) / 1000.0;
  }

 private:
  std::int64_t frequency_ = 1;
  LARGE_INTEGER start_{};
};

// 给日志用的毫秒字符串，保留一位小数：量化假象消失后，"0.4ms" 与 "14.2ms"
// 必须能区分开，取整会把它们都写成 0 和 14，看不出量级差。
inline std::string FormatMs(std::int64_t microseconds) {
  const std::int64_t tenths = (microseconds + 50) / 100;  // 四舍五入到 0.1ms
  return std::to_string(tenths / 10) + "." + std::to_string(tenths % 10);
}

}  // namespace clipd2d::common
