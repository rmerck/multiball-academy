#include <cstdint>
#include <string>
#include <string_view>

#include "Check.hpp"
#include "Suites.hpp"
#include "core/HighScores.hpp"
#include "core/Missions.hpp"
#include "core/InitialsEntry.hpp"
#include "core/Rules.hpp"

void testHighScores() {
  using pinball::HighScore;
  using pinball::HighScores;
  const std::string header = "PINBALL-SCORES 3\n";

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

  {  // Limits: the largest score and the largest text are accepted, one more is not.
    const std::string top = std::to_string(pinball::points::kMaxScore);
    const std::string over = std::to_string(pinball::points::kMaxScore + 1);
    HighScores scores;
    CHECK(scores.parse(header + top + " 0 ---\n"));
    CHECK(scores.best() == pinball::points::kMaxScore);
    CHECK(!scores.parse(header + over + " 0 ---\n"));
    CHECK(!scores.parse(std::string(HighScores::kMaxTextBytes + 1, ' ')));

    // A full table of top scores turns away another, however large.
    HighScores full;
    for (std::size_t i = 0; i < HighScores::kCapacity; ++i) {
      CHECK(full.submit({pinball::points::kMaxScore, 0}) == i);
    }
    CHECK(full.submit({UINT64_MAX, 0}) == HighScores::kCapacity);
    CHECK(full.size() == HighScores::kCapacity);
  }

  {  // Initials are kept, cleaned on entry, and survive a round trip.
    HighScores scores;
    CHECK(scores.submit({300, 2, {'R', 'P', 'M'}}) == 0);
    CHECK(scores.submit({200, 1, {'a', '1', 'Z'}}) == 1);
    CHECK(scores.submit({100, 0}) == 2);
    CHECK(scores.at(0).initials == (pinball::Initials{'R', 'P', 'M'}));
    CHECK(scores.at(1).initials == (pinball::Initials{'-', '-', 'Z'}));
    CHECK(scores.at(2).initials == pinball::kNoInitials);
    CHECK(scores.serialize() == header + "300 2 RPM\n200 1 --Z\n100 0 ---\n");
    HighScores copy;
    CHECK(copy.parse(scores.serialize()));
    CHECK(copy.at(0).initials == scores.at(0).initials);
    CHECK(copy.serialize() == scores.serialize());
  }

  {  // A full table of the largest entries round trips and fits the size cap.
    HighScores full;
    for (std::size_t i = 0; i < HighScores::kCapacity; ++i) {
      full.submit({pinball::points::kMaxScore, 8, {'Z', 'Z', 'Z'}});
    }
    const std::string text = full.serialize();
    CHECK(text.size() <= HighScores::kMaxTextBytes);
    HighScores copy;
    CHECK(copy.parse(text) && copy.size() == HighScores::kCapacity);
    // Bytes that are not initials are cleaned on the way in.
    HighScores odd;
    odd.submit({10, 0, {'\0', static_cast<char>(0xC3), ' '}});
    CHECK(odd.at(0).initials == pinball::kNoInitials);
  }

  {  // Entering initials.
    static_assert(pinball::InitialsEntry().letters()[0] == 'A', "usable in constant expressions");
    pinball::InitialsEntry entry;
    CHECK(entry.letters() == (pinball::Initials{'A', 'A', 'A'}) && entry.cursor() == 0);
    entry.previous();
    CHECK(entry.letters()[0] == 'Z');
    entry.next();
    entry.next();
    CHECK(entry.letters()[0] == 'B');
    entry.left();
    CHECK(entry.cursor() == 0);
    entry.type('q');  // Ignored: the caller passes capitals.
    entry.type('1');
    CHECK(entry.cursor() == 0 && entry.letters()[0] == 'B');
    entry.type('R');
    CHECK(entry.cursor() == 1 && entry.letters()[0] == 'R');
    CHECK(!entry.confirm());
    CHECK(entry.cursor() == 2);
    entry.type('M');
    entry.type('K');  // Stays on the last letter.
    CHECK(entry.cursor() == 2 && entry.letters() == (pinball::Initials{'R', 'A', 'K'}));
    entry.right();
    CHECK(entry.cursor() == 2);
    CHECK(entry.confirm());
    entry.left();
    entry.left();
    entry.left();
    CHECK(entry.cursor() == 0 && !entry.confirm());

    // Starting letters are cleaned.
    const pinball::InitialsEntry cleaned(pinball::Initials{'x', '-', 'Q'});
    CHECK(cleaned.letters() == (pinball::Initials{'A', 'A', 'Q'}));
  }

  {  // Valid text.
    HighScores scores;
    CHECK(scores.parse(header));
    CHECK(scores.size() == 0);
    CHECK(scores.parse(header + "1000 3 ABC\n1000 0 ---\n5 8 A-Z\n"));
    CHECK(scores.size() == 3 && scores.best() == 1000 && scores.at(2).rank == 8);
  }

  {  // Anything malformed is rejected whole and leaves the table empty.
    const std::string bad[] = {
        "",
        "\n",
        "PINBALL-SCORES 4\n",                         // A format from the future.
        // What the game wrote under its first name is not read.
        "SPACE-CADET-PINBALL-SCORES 2\n",
        "SPACE-CADET-PINBALL-SCORES 2\n100 1 ABC\n",
        "SPACE-CADET-PINBALL-SCORES 1\n",
        "SPACE-CADET-PINBALL-SCORES 1\n100 1\n",
        "pinball-scores 3\n",                         // Lower case.
        " PINBALL-SCORES 3\n",
        "PINBALL-SCORES 3 \n",
        "PINBALL-SCORES  3\n",
        "PINBALL-SCORES 3\r\n",
        header + "100 1\n",                           // Initials missing.
        header + "100 1 \n",
        header + "100 1 AB\n",                        // Too few.
        header + "100 1 ABCD\n",                      // Too many.
        header + "100 1 abc\n",                       // Lower case.
        header + "100 1 A C\n",                       // Space inside.
        header + "100 1 A1C\n",                       // Digit.
        header + "100 1 AB\xC3\n",                    // Not ASCII.
        header + "100 1 ABC \n",                      // Trailing space.
        header + "100 1 ABC DEF\n",                   // Extra field.
        header + "100  1 ABC\n",                      // Double space.
        header + std::string("100 1 A\0C\n", 10),     // Embedded NUL.
        header + "6 0 AAA\n5 0 AAA\n4 0 AAA\n3 0 AAA\n2 0 AAA\n1 0 AAA\n",  // Too many.
        header + "100 1 AAA\n200 1 AAA\n",            // Out of order.
        header + "100 1 ABC\r\n",                     // Carriage return.
        header + "100\t1\tABC\n",                     // Tabs.
        header + " 1 ABC\n",                          // Empty score.
        header + "100  ABC\n",                        // Empty rank.
        header + "200 1 ABC\n" + header + "100 1 ABC\n",  // Second header.
        header.substr(0, header.size() - 1),          // No final newline.
        header + "100 1 ABC",                         // No final newline.
        header + "100\n",                             // Missing rank.
        header + " 100 1 ABC\n",                      // Leading space.
        header + "-100 1 ABC\n",
        header + "+100 1 ABC\n",
        header + "0100 1 ABC\n",                      // Leading zero.
        header + "100 01 ABC\n",
        header + "1e3 1 ABC\n",
        header + "0x10 1 ABC\n",
        header + "100 9 ABC\n",                       // Rank out of range.
        header + "0 1 ABC\n",                         // Zero score.
        header + "1000000000000 1 ABC\n",             // Above the score cap.
        header + "99999999999999999999999 1 ABC\n",   // Overflows 64 bits.
        header + "\n",                                // Blank line.
        header + "100 1 ABC\n\n",                     // Blank line at the end.
        header + std::string("100 1 ABC\0\n", 11),    // Embedded NUL.
        header + std::string(600, '9') + " 1 ABC\n",  // Too long.
        "garbage\n100 1 ABC\n",
    };
    for (const std::string& text : bad) {
      HighScores scores;
      scores.submit({42, 1});
      CHECK(!scores.parse(text));
      CHECK(scores.size() == 0);
    }
  }
}
