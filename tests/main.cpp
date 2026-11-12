#include <cstdio>
#include <cstdlib>

#include "Check.hpp"

void testArgs();
void testAssetName();
void testFixedTimestep();
void testGame();

int main() {
  testArgs();
  testAssetName();
  testFixedTimestep();
  testGame();

  if (pinball_test::failures() == 0) {
    std::puts("core_tests: all passed");
    return EXIT_SUCCESS;
  }
  std::fprintf(stderr, "core_tests: %d failure(s)\n", pinball_test::failures());
  return EXIT_FAILURE;
}
