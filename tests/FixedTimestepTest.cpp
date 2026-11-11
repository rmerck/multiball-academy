#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

#include "core/FixedTimestep.hpp"

namespace {

int failures = 0;

void check(bool condition, const char* expr, int line) {
  if (!condition) {
    std::fprintf(stderr, "FAIL line %d: %s\n", line, expr);
    ++failures;
  }
}

#define CHECK(expr) check((expr), #expr, __LINE__)

}  // namespace

int main() {
  using pinball::FixedTimestep;
  const double step = 1.0 / 120.0;

  {  // Exact multiples produce exact step counts.
    FixedTimestep ts(step, 8);
    CHECK(ts.advance(step * 2.0 + 1e-9) == 2);
    CHECK(ts.alpha() >= 0.0 && ts.alpha() < 1.0);
  }
  {  // Sub-step frames accumulate.
    FixedTimestep ts(step, 8);
    CHECK(ts.advance(step * 0.6) == 0);
    CHECK(ts.advance(step * 0.6) == 1);
    CHECK(std::fabs(ts.alpha() - 0.2) < 1e-6);
  }
  {  // Long frames are capped and the backlog is dropped.
    FixedTimestep ts(step, 8);
    CHECK(ts.advance(10.0) == 8);
    CHECK(ts.alpha() < 1.0);
    CHECK(ts.advance(0.0) == 0);
  }
  {  // Hostile input never produces steps or corrupts state.
    FixedTimestep ts(step, 8);
    CHECK(ts.advance(-1.0) == 0);
    CHECK(ts.advance(std::numeric_limits<double>::quiet_NaN()) == 0);
    CHECK(ts.advance(std::numeric_limits<double>::infinity()) == 0);
    CHECK(ts.alpha() == 0.0);
    CHECK(ts.advance(step + 1e-9) == 1);
  }
  {  // Invalid construction falls back to safe defaults.
    FixedTimestep ts(0.0, 0);
    CHECK(ts.step() > 0.0);
    CHECK(ts.maxSteps() == 1);
    FixedTimestep nan(std::numeric_limits<double>::quiet_NaN(), -3);
    CHECK(nan.step() > 0.0);
    CHECK(nan.maxSteps() == 1);
  }

  if (failures == 0) {
    std::puts("core_tests: all passed");
    return EXIT_SUCCESS;
  }
  return EXIT_FAILURE;
}
