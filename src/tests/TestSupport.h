#pragma once

#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace clipd2d::tests {

using TestFunction = std::function<void()>;

struct TestCase {
  std::string name;
  TestFunction function;
};

std::vector<TestCase>& TestRegistry();
void RegisterTest(std::string name, TestFunction function);
std::filesystem::path CreateUniqueTempDirectory(std::wstring_view prefix);
void DeleteDirectoryIfExists(const std::filesystem::path& path);

struct TestRegistrar {
  TestRegistrar(std::string name, TestFunction function);
};

}  // namespace clipd2d::tests

#define CLIPD2D_TEST(name)                                                                                  \
  void name();                                                                                              \
  static ::clipd2d::tests::TestRegistrar name##_registrar(#name, &name);                                   \
  void name()

#define CLIPD2D_CHECK(condition)                                                                            \
  do {                                                                                                      \
    if (!(condition)) {                                                                                     \
      throw std::runtime_error(std::string("Check failed: ") + #condition);                                \
    }                                                                                                       \
  } while (false)

#define CLIPD2D_CHECK_EQ(lhs, rhs)                                                                          \
  do {                                                                                                      \
    const auto& clipd2d_test_lhs = (lhs);                                                                  \
    const auto& clipd2d_test_rhs = (rhs);                                                                  \
    if (!(clipd2d_test_lhs == clipd2d_test_rhs)) {                                                         \
      throw std::runtime_error(std::string("Check failed: ") + #lhs + " == " + #rhs);                     \
    }                                                                                                       \
  } while (false)
