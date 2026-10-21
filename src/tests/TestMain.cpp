#include "tests/TestSupport.h"

#include <windows.h>

#include <iostream>

namespace clipd2d::tests {

std::vector<TestCase>& TestRegistry() {
  static std::vector<TestCase> registry;
  return registry;
}

void RegisterTest(std::string name, TestFunction function) {
  TestRegistry().push_back(TestCase{std::move(name), std::move(function)});
}

TestRegistrar::TestRegistrar(std::string name, TestFunction function) {
  RegisterTest(std::move(name), std::move(function));
}

std::filesystem::path CreateUniqueTempDirectory(std::wstring_view prefix) {
  const std::filesystem::path base = std::filesystem::temp_directory_path();
  const std::wstring unique =
      std::wstring(prefix) + L"-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
  const std::filesystem::path path = base / unique;
  std::filesystem::create_directories(path);
  return path;
}

void DeleteDirectoryIfExists(const std::filesystem::path& path) {
  std::error_code ec;
  std::filesystem::remove_all(path, ec);
}

}  // namespace clipd2d::tests

int main() {
  int failures = 0;
  for (const auto& test : clipd2d::tests::TestRegistry()) {
    try {
      test.function();
      std::cout << "[PASS] " << test.name << '\n';
    } catch (const std::exception& ex) {
      ++failures;
      std::cerr << "[FAIL] " << test.name << ": " << ex.what() << '\n';
    }
  }
  return failures == 0 ? 0 : 1;
}
