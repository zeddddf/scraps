#include "tests/TestSupport.h"

#include <set>

#include "common/Stopwatch.h"

namespace clipd2d::tests {

namespace {

// 一段耗时几十微秒的确定性工作，用来验证计时器能分辨亚毫秒区间。
// 循环次数要足够大：ElapsedUs() 截断到整数微秒，若工作本身不到 1µs 就恒为 0。
void DoTinyWork(int iterations) {
  volatile std::uint64_t accumulator = 0;
  for (int i = 0; i < iterations; ++i) {
    accumulator += static_cast<std::uint64_t>(i);
  }
}

}  // namespace

CLIPD2D_TEST(StopwatchMeasuresSubTickIntervals) {
  // 回归防线：这些探针原本用 GetTickCount64 计时，默认 15.625ms 时钟粒度会把
  // 0.05ms 的调用记成 16ms（跨 tick 即进位），导致「725 条 16ms 慢消息」这种
  // 全是假阳性的结论。若有人把实现改回 GetTickCount64，本测试会失败。
  std::set<std::int64_t> samples;
  for (int sample = 0; sample < 44; ++sample) {
    // 每轮做不同量的工作：固定循环在 QPC 下重复性极高（实测 50 次读数完全
    // 相同），只有改变工作量才能验证计时器真的跟随耗时。步长 2 万次约合
    // 20~30µs，44 轮总量仍远小于一个时钟 tick，不会触发下面的量化断言。
    const int iterations = 50000 + sample * 20000;
    common::Stopwatch stopwatch;
    DoTinyWork(iterations);
    const std::int64_t elapsed_us = stopwatch.ElapsedUs();
    CLIPD2D_CHECK(elapsed_us > 0);
    // 这点工作量远不到一个 tick：一旦出现 >=15600us，说明又退回了量化时钟。
    CLIPD2D_CHECK(elapsed_us < 15600);
    samples.insert(elapsed_us);
  }
  // 关键断言在上面：量化时钟只能给出 0 或 >=15625us，能落在 (0, 15600) 内即证明
  // 测的是真实亚毫秒值。不同工作量还应对应明显不同的读数。
  CLIPD2D_CHECK(samples.size() >= 5);
}

CLIPD2D_TEST(StopwatchRestartResetsBaseline) {
  common::Stopwatch stopwatch;
  Sleep(2);
  const std::int64_t before = stopwatch.ElapsedUs();
  stopwatch.Restart();
  const std::int64_t after = stopwatch.ElapsedUs();
  CLIPD2D_CHECK(before > 0);
  CLIPD2D_CHECK(after < before);
}

CLIPD2D_TEST(FormatMsKeepsOneDecimal) {
  CLIPD2D_CHECK_EQ(common::FormatMs(0), std::string("0.0"));
  CLIPD2D_CHECK_EQ(common::FormatMs(40), std::string("0.0"));
  CLIPD2D_CHECK_EQ(common::FormatMs(60), std::string("0.1"));
  CLIPD2D_CHECK_EQ(common::FormatMs(400), std::string("0.4"));
  CLIPD2D_CHECK_EQ(common::FormatMs(12345), std::string("12.3"));
  CLIPD2D_CHECK_EQ(common::FormatMs(16000), std::string("16.0"));
  CLIPD2D_CHECK_EQ(common::FormatMs(16099), std::string("16.1"));
  // 关键点：0.4ms 与 14.2ms 必须能被区分。旧探针把两者都写成 0/14 甚至都写成 16。
  CLIPD2D_CHECK(common::FormatMs(400) != common::FormatMs(14200));
}

}  // namespace clipd2d::tests
