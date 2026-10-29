#include "test.hpp"

int main(int argc, char** argv) {
  std::string filter = argc > 1 ? argv[1] : "";
  int run = 0;
  for (auto& c : test::registry()) {
    if (!filter.empty() && std::string(c.name).find(filter) == std::string::npos)
      continue;
    int before = test::failures();
    c.fn();
    ++run;
    std::printf("%s %s\n", test::failures() == before ? "PASS" : "FAIL", c.name);
  }
  std::printf("%d tests, %d failed checks\n", run, test::failures());
  return test::failures() ? 1 : 0;
}
