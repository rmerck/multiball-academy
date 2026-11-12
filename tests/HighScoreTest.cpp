#include <cstdint>
#include <string>
#include <string_view>

#include "Check.hpp"
#include "Suites.hpp"
#include "core/HighScores.hpp"
#include "core/Rules.hpp"

void testHighScores() {
  using pinball::HighScore;
  using pinball::HighScores;
  const std::string header = "SPACE-CADET-PINBALL-SCORES 1\n";

  {  // Empty table.
    HighScores scores;
    CHECK(scores.size() == 0 && scores.best() == 0);
    CHECK(!scores.qualifies(0));
    CHECK(scores.qualifies(1));
    CHECK(scores.at(0).score == 0 && scores.at(99).score == 0);
    CHECK(scores.serialize() == header);
    CHECK(scores.submit({0, 0}) == HighScores::kCapacity);
  }

  {  // Entries stay sorted, the table holds five, ties keep the older game
     // ahead, and out-of-range values are clamped.
    HighScores scores;
    CHECK(scores.submit({500, 1}) == 0);
    CHECK(scores.submit({900, 2}) == 0);
    CHECK(scores.submit({700, 0}) == 1);
    CHECK(scores.submit({700, 3}) == 2);
    CHECK(scores.at(1).rank == 0 && scores.at(2).rank == 3);
    CHECK(scores.submit({100, 0}) == 4);
    CHECK(scores.size() == 5);
    CHECK(!scores.qualifies(100));
    CHECK(scores.submit({100, 0}) == HighScores::kCapacity);
    CHECK(scores.submit({50, 0}) == HighScores::kCapacity);
    CHECK(scores.submit({800, 200}) == 1);
    CHECK(scores.at(1).rank == pinball::kRankCount - 1);
    CHECK(scores.size() == 5);
    CHECK(scores.at(4).score == 500);
    CHECK(scores.submit({UINT64_MAX, 0}) == 0);
    CHECK(scores.best() == pinball::points::kMaxScore);
    for (std::size_t i = 1; i < scores.size(); ++i) {
      CHECK(scores.at(i - 1).score >= scores.at(i).score);
    }

    // Round trip.
    HighScores copy;
    CHECK(copy.parse(scores.serialize()));
    CHECK(copy.size() == scores.size());
    for (std::size_t i = 0; i < scores.size(); ++i) {
      CHECK(copy.at(i).score == scores.at(i).score && copy.at(i).rank == scores.at(i).rank);
    }
    CHECK(copy.serialize() == scores.serialize());
    CHECK(scores.serialize().size() <= HighScores::kMaxTextBytes);
  }

  {  // Valid text.
    HighScores scores;
    CHECK(scores.parse(header));
    CHECK(scores.size() == 0);
    CHECK(scores.parse(header + "1000 3\n1000 0\n5 8\n"));
    CHECK(scores.size() == 3 && scores.best() == 1000 && scores.at(2).rank == 8);
  }

  {  // Anything malformed is rejected whole and leaves the table empty.
    const std::string bad[] = {
        "",
        "\n",
        "SPACE-CADET-PINBALL-SCORES 2\n",
        "space-cadet-pinball-scores 1\n",
        header.substr(0, header.size() - 1),          // No final newline.
        header + "100 1",                             // No final newline.
        header + "100\n",                             // Missing rank.
        header + "100 1 1\n",                         // Extra field.
        header + " 100 1\n",                          // Leading space.
        header + "100  1\n",                          // Double space.
        header + "100 1 \n",                          // Trailing space.
        header + "-100 1\n",
        header + "+100 1\n",
        header + "0100 1\n",                          // Leading zero.
        header + "100 01\n",
        header + "1e3 1\n",
        header + "0x10 1\n",
        header + "100 9\n",                           // Rank out of range.
        header + "0 1\n",                             // Zero score.
        header + "1000000000000 1\n",                 // Above the score cap.
        header + "99999999999999999999999 1\n",       // Overflows 64 bits.
        header + "100 1\n200 1\n",                    // Out of order.
        header + "6 0\n5 0\n4 0\n3 0\n2 0\n1 0\n",    // Too many entries.
        header + "100 1\r\n",                         // Carriage return.
        header + "\n",                                // Blank line.
        header + std::string("100 1\0\n", 7),         // Embedded NUL.
        header + std::string(600, '9') + " 1\n",      // Too long.
        "garbage\n100 1\n",
    };
    for (const std::string& text : bad) {
      HighScores scores;
      scores.submit({42, 1});
      CHECK(!scores.parse(text));
      CHECK(scores.size() == 0);
    }
  }
}
