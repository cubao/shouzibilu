// 轻量测试框架：静态注册 + assert 异常。
// 不引入 gtest 等外部依赖。
#pragma once

#include <cstdio>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace tfw {

struct TestCase {
  std::string name;
  std::function<void()> fn;
};

inline std::vector<TestCase>& Registry() {
  static std::vector<TestCase> r;
  return r;
}

struct Registrar {
  Registrar(std::string name, std::function<void()> fn) {
    Registry().push_back({std::move(name), std::move(fn)});
  }
};

inline int RunAll() {
  int failed = 0;
  for (const auto& t : Registry()) {
    try {
      t.fn();
      std::printf("PASS  %s\n", t.name.c_str());
    } catch (const std::exception& e) {
      std::printf("FAIL  %s\n      %s\n", t.name.c_str(), e.what());
      ++failed;
    }
  }
  std::printf("----\n%s: %zu tests, %d failed\n", failed ? "FAILED" : "OK",
              Registry().size(), failed);
  return failed ? 1 : 0;
}

}  // namespace tfw

#define TEST(suite, name)                                                  \
  static void test_##suite##_##name();                                     \
  static ::tfw::Registrar reg_##suite##_##name(#suite "." #name,           \
                                               test_##suite##_##name);     \
  static void test_##suite##_##name()

#define ASSERT(cond)                                                       \
  do {                                                                     \
    if (!(cond)) {                                                         \
      throw std::runtime_error(std::string(__FILE__) + ":" +               \
                               std::to_string(__LINE__) +                  \
                               " assert failed: " #cond);                  \
    }                                                                      \
  } while (0)

#define ASSERT_EQ(a, b)                                                    \
  do {                                                                     \
    auto va = (a);                                                         \
    auto vb = (b);                                                         \
    if (!(va == vb)) {                                                     \
      throw std::runtime_error(std::string(__FILE__) + ":" +               \
                               std::to_string(__LINE__) +                  \
                               " assert failed: " #a " == " #b);           \
    }                                                                      \
  } while (0)
