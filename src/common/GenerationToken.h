#pragma once

#include <atomic>
#include <cstdint>

namespace clipd2d::common {

// 统一的「代次」抽象：旧结果不覆盖新状态的唯一机制。
// 语义：Next() 签发新代次；IsStale(issued) 判断签发后是否有更新代次出现。
// 替换此前各自为政的 generation/epoch 计数器（见 refactoring-plan §3.1 P-4）。
//
// 并发说明：内部用 atomic，跨线程签发/读取安全；「UI 线程专用」（无并发）
// 的场景同样适用——统一 atomic 的开销可忽略，换来全项目单一代次语义。
// 注意：本类只表达「单调递增 + 丢弃旧值」；需要写入外部值的水位型成员
// （如设备代次镜像）不属于本抽象，勿强行套用。
class GenerationToken {
 public:
  // 原子递增并返回新值（首值为 1）。
  std::uint64_t Next() noexcept { return value_.fetch_add(1) + 1; }
  // 当前代次值（0 表示尚未签发过）。
  std::uint64_t current() const noexcept { return value_.load(); }
  // issued 签发后是否出现过更新代次（true = issued 已过期，应丢弃）。
  bool IsStale(std::uint64_t issued) const noexcept { return issued != value_.load(); }

 private:
  std::atomic<std::uint64_t> value_{0};
};

}  // namespace clipd2d::common
