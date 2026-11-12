#pragma once

#include <cstdio>

// Minimal test support: CHECK records a failure and keeps going.
namespace pinball_test {

inline int& failures() {
  static int count = 0;
  return count;
}

// How many checks ran. A run that checked nothing has proved nothing.
inline long& checks() {
  static long count = 0;
  return count;
}

inline void check(bool condition, const char* expr, const char* file, int line) {
  ++checks();
  if (!condition) {
    std::fprintf(stderr, "FAIL %s:%d: %s\n", file, line, expr);
    ++failures();
  }
}

}  // namespace pinball_test

#define CHECK(expr) ::pinball_test::check((expr), #expr, __FILE__, __LINE__)
