#include <cstddef>
#include <string_view>

#include "Check.hpp"
#include "Suites.hpp"
#include "core/DisplayText.hpp"
#include "core/Missions.hpp"
#include "core/Rules.hpp"
#include "platform/Font5x7.hpp"

// What is left of the mission tests: the rank ladder and the arithmetic of
// the display limits. The missions went with the old table's features; the
// tests of courses come with the new table's rule set.
namespace {

using pinball::Rules;
namespace display = pinball::display;

constexpr double kStep = 1.0 / 120.0;

}  // namespace

void testMissions() {
  {  // The display limits. They are worked out from the panel windows and
     // the font, and every rank and message is held to them when the game
     // is compiled: this pins the arithmetic behind that.
    static_assert(display::kAdvance == pinball::font::kAdvance &&
                      display::kFirstChar == pinball::font::kFirstChar &&
                      display::kLastChar == pinball::font::kLastChar,
                  "the limits assume the font the display draws with");
    // 440 pixels inside a window: a line of n characters is 6n - 1 font
    // pixels wide.
    static_assert(display::charsThatFit(240.0F, 3.0F) == 13);  // 77 * 3 = 231, and 14 need 249.
    static_assert(display::kMaxRankNameLength == 13);
    static_assert(display::charsThatFit(440.0F, 4.0F) == 18);  // 107 * 4 = 428, and 19 need 452.
    static_assert(display::charsThatFit(440.0F, 3.0F) == 24);  // 143 * 3 = 429, and 25 need 447.
    static_assert(display::charsThatFit(440.0F, 2.0F) == 36);  // 215 * 2 = 430, and 37 need 442.
    static_assert(display::charsThatFit(10.0F, 2.0F) == 1);
    static_assert(display::charsThatFit(9.0F, 2.0F) == 0);
    static_assert(display::charsThatFit(0.0F, 2.0F) == 0);
    static_assert(display::charsThatFit(440.0F, 0.0F) == 0);
    static_assert(display::fits("COURSE PASSED  RANK UP", display::kMaxMessageLength));
    static_assert(display::fits("FIELD TRIP: 101", 15) && !display::fits("FIELD TRIP: 101", 14));
    static_assert(!display::fits("", 10));
    static_assert(!display::fits(" PAD", 10) && !display::fits("PAD ", 10));
    // Lower case, and anything else the font has no glyph for, is refused.
    static_assert(!display::fits("Rookie", 10) && !display::fits("A\tB", 10) &&
                  !display::fits("A~B", 10) && !display::fits("\xC9", 10));
    // What the rules say comes through unchanged, and fits.
    Rules rules(kStep);
    CHECK(display::fits(rules.message(), display::kMaxMessageLength));
    CHECK(rules.message() == display::Message("LAUNCH THE BALL").text());
  }

  {  // The rank ladder: every name fits wherever a rank is shown, no two are
     // the same, and an index past the end is the top rank.
    for (std::size_t rank = 0; rank < pinball::kRankCount; ++rank) {
      CHECK(display::fits(pinball::rankName(rank), display::kMaxRankNameLength));
      for (std::size_t other = rank + 1; other < pinball::kRankCount; ++other) {
        CHECK(pinball::rankName(rank) != pinball::rankName(other));
      }
    }
    CHECK(pinball::rankName(999) == pinball::rankName(pinball::kRankCount - 1));
    // The provisional rules never promote: a game is played at the first.
    Rules rules(kStep);
    CHECK(pinball::rankName(rules.rank()) == pinball::rankName(0));
  }
}
