#include <cstdint>
#include <limits>
#include <string_view>

#include "Check.hpp"
#include "Suites.hpp"
#include "core/Format.hpp"

void testFormat() {
  using pinball::formatScore;
  pinball::ScoreText buffer{};
  CHECK(formatScore(0, buffer) == std::string_view("0"));
  CHECK(formatScore(7, buffer) == std::string_view("7"));
  CHECK(formatScore(999, buffer) == std::string_view("999"));
  CHECK(formatScore(1000, buffer) == std::string_view("1,000"));
  CHECK(formatScore(12345, buffer) == std::string_view("12,345"));
  CHECK(formatScore(100000, buffer) == std::string_view("100,000"));
  CHECK(formatScore(1234567, buffer) == std::string_view("1,234,567"));
  CHECK(formatScore(999999999999ULL, buffer) == std::string_view("999,999,999,999"));
  {  // formatText truncates and terminates; it never overruns.
    char small[8];
    CHECK(pinball::formatText(small, sizeof small, "BALL %d", 3) == std::string_view("BALL 3"));
    const std::string_view cut = pinball::formatText(small, sizeof small, "%s", "ABCDEFGHIJKLMNOP");
    CHECK(cut == std::string_view("ABCDEFG"));
    CHECK(small[7] == '\0');
    CHECK(pinball::formatText(small, 0, "%d", 1).empty());
    CHECK(pinball::formatText(nullptr, 8, "%d", 1).empty());
  }
  // The largest possible value still fits the buffer.
  CHECK(formatScore(std::numeric_limits<std::uint64_t>::max(), buffer) ==
        std::string_view("18,446,744,073,709,551,615"));
}
