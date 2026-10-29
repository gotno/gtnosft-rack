#pragma once
// Minimal test registry for the Rack-free units. Run with tests/run.sh.
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace test {
struct Case {
  const char* name;
  std::function<void()> fn;
};
inline std::vector<Case>& registry() {
  static std::vector<Case> cases;
  return cases;
}
inline int& failures() {
  static int count = 0;
  return count;
}
struct Register {
  Register(const char* name, std::function<void()> fn) {
    registry().push_back({name, fn});
  }
};
}

#define TEST_CAT2(a, b) a##b
#define TEST_CAT(a, b) TEST_CAT2(a, b)
#define TEST(name) \
  static void name(); \
  static test::Register TEST_CAT(register_, name)(#name, name); \
  static void name()
#define CHECK(cond) do { \
    if (!(cond)) { \
      std::printf("  %s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
      ++test::failures(); \
    } \
  } while (0)
#define CHECK_EQ(a, b) do { \
    auto checkA = (a); \
    auto checkB = (b); \
    if (!(checkA == checkB)) { \
      std::printf("  %s:%d: CHECK_EQ(%s, %s) failed: %s != %s\n", __FILE__, \
        __LINE__, #a, #b, std::to_string(checkA).c_str(), \
        std::to_string(checkB).c_str()); \
      ++test::failures(); \
    } \
  } while (0)
