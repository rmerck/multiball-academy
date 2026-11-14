#include <cstddef>
#include <cstdint>
#include <string_view>

#include "Check.hpp"
#include "Suites.hpp"
#include "core/Game.hpp"
#include "core/Rules.hpp"
#include "core/TableLayout.hpp"

// The provisional rule set of the new table: a flat value for everything the
// ball can do, three balls, the ball save and the tilt. The table's own
// rules (courses, multiball, combos, bonus) replace it and these tests.
namespace {

using pinball::Rules;
namespace points = pinball::points;
namespace table = pinball::table;

constexpr double kStep = 1.0 / 120.0;

std::size_t firstRollover(table::RolloverGroup group) {
  for (std::size_t i = 0; i < table::kRollovers.size(); ++i) {
    if (table::kRollovers[i].group == group) {
      return i;
    }
  }
  return 0;
}

void wait(Rules& rules, double seconds) {
  for (int i = 0; i < static_cast<int>(seconds / kStep) + 2; ++i) {
    rules.tick();
  }
}

// Drains with no ball save in the way.
Rules::DrainResult drain(Rules& rules) {
  wait(rules, Rules::kBallSaveSeconds);
  return rules.onDrain();
}

}  // namespace

void testRules() {
  {  // A new game.
    Rules rules(kStep);
    CHECK(rules.score() == 0);
    CHECK(rules.ball() == 1);
    CHECK(!rules.gameOver());
    CHECK(rules.rank() == 0);
    CHECK(!rules.ballInPlay() && !rules.ballSaveActive() && !rules.tilted());
    CHECK(rules.message() == std::string_view("LAUNCH THE BALL"));
  }

  {  // Every element pays its value, each time, and nothing else.
    Rules rules(kStep);
    std::uint64_t expected = 0;
    for (std::size_t i = 0; i < table::kBumpers.size(); ++i) {
      rules.onBumper(i);
      expected += points::kBumper;
      CHECK(rules.score() == expected);
    }
    for (std::size_t i = 0; i < table::kSlingshots.size(); ++i) {
      rules.onSlingshot(i);
      expected += points::kSlingshot;
    }
    for (std::size_t i = 0; i < table::kTargets.size(); ++i) {
      rules.onTarget(i);
      expected += points::kTarget;
    }
    for (std::size_t i = 0; i < table::kDropTargets.size(); ++i) {
      rules.onDropTarget(i);
      expected += points::kBayDoor;
    }
    CHECK(rules.score() == expected);
    rules.onRollover(firstRollover(table::RolloverGroup::TopLane));
    expected += points::kRollCallLane;
    rules.onRollover(firstRollover(table::RolloverGroup::Inlane));
    rules.onRollover(firstRollover(table::RolloverGroup::Outlane));
    expected += 2 * points::kReturnLane;
    CHECK(rules.score() == expected);
    rules.onHole(table::kScoopHole);
    CHECK(rules.message() == std::string_view("BRIEFING"));
    rules.onHole(table::kHangarHole);
    CHECK(rules.message() == std::string_view("HANGAR"));
    rules.onHole(table::kRunwayHole);
    CHECK(rules.message() == std::string_view("RUNWAY"));
    rules.onHole(table::kSkywayHole);
    CHECK(rules.message() == std::string_view("SKYWAY"));
    expected += points::kBriefingRoom + points::kHangar + 2 * points::kRamp;
    CHECK(rules.score() == expected);
    // A second time pays the same again.
    rules.onBumper(0);
    rules.onHole(table::kRunwayHole);
    CHECK(rules.score() == expected + points::kBumper + points::kRamp);
  }

  {  // Out-of-range indices are ignored everywhere.
    Rules rules(kStep);
    rules.onBumper(999);
    rules.onSlingshot(999);
    rules.onTarget(999);
    rules.onDropTarget(999);
    rules.onRollover(999);
    rules.onHole(999);
    CHECK(rules.score() == 0);
    CHECK(rules.message() == std::string_view("LAUNCH THE BALL"));
  }

  {  // Ball save: a drain soon after entering play costs nothing.
    Rules rules(kStep);
    rules.onBallInPlay();
    CHECK(rules.ballSaveActive() && rules.ballInPlay());
    CHECK(rules.onDrain() == Rules::DrainResult::BallSaved);
    CHECK(rules.message() == std::string_view("BALL SAVED"));
    CHECK((rules.takeSounds() & pinball::soundBit(pinball::Sound::BallSaved)) != 0U);
    CHECK(rules.ball() == 1 && !rules.ballInPlay());
    // One save per ball: the returned ball is not saved a second time.
    rules.onBallInPlay();
    CHECK(!rules.ballSaveActive());
    CHECK(rules.onDrain() == Rules::DrainResult::NextBall);
    CHECK(rules.message() == std::string_view("NEXT BALL"));
    // The next ball gets its own.
    rules.onBallInPlay();
    CHECK(rules.ballSaveActive());
    wait(rules, Rules::kBallSaveSeconds);
    CHECK(!rules.ballSaveActive());
  }

  {  // Three balls, then game over; nothing scores after that until a new
     // game.
    Rules rules(kStep);
    rules.onBumper(0);
    rules.onBumper(0);
    const std::uint64_t before = rules.score();
    CHECK(drain(rules) == Rules::DrainResult::NextBall);
    CHECK(rules.score() == before);
    CHECK(rules.ball() == 2);
    CHECK(drain(rules) == Rules::DrainResult::NextBall);
    CHECK(rules.ball() == 3);
    rules.takeSounds();
    CHECK(drain(rules) == Rules::DrainResult::GameOver);
    CHECK(rules.gameOver());
    CHECK(rules.ball() == 3);
    CHECK(rules.message() == Rules::kGameOverMessage.text());
    CHECK((rules.takeSounds() & pinball::soundBit(pinball::Sound::GameOver)) != 0U);

    const std::uint64_t final = rules.score();
    rules.onBumper(0);
    rules.onTarget(0);
    rules.onDropTarget(0);
    rules.onRollover(0);
    rules.onHole(0);
    rules.onSlingshot(0);
    rules.onBallInPlay();
    CHECK(rules.score() == final);
    CHECK(!rules.ballInPlay() && !rules.ballSaveActive());
    CHECK(!rules.onNudge());
    CHECK(rules.onDrain() == Rules::DrainResult::GameOver);
    wait(rules, 10.0);
    CHECK(rules.message() == Rules::kGameOverMessage.text());  // It stays up.

    rules.startGame();
    CHECK(!rules.gameOver() && rules.score() == 0 && rules.ball() == 1);
  }

  {  // The score saturates; it never wraps.
    // The largest award there is, as often as it takes to pass the limit.
    Rules full(kStep);
    const std::uint64_t needed = points::kMaxScore / points::kHangar + 10;
    for (std::uint64_t i = 0; i < needed; ++i) {
      full.onHole(table::kHangarHole);
    }
    CHECK(needed * points::kHangar > points::kMaxScore);
    CHECK(full.score() == points::kMaxScore);
    full.onBumper(0);
    CHECK(full.score() == points::kMaxScore);
  }

  {  // Messages expire.
    Rules rules(kStep);
    rules.onHole(table::kScoopHole);
    CHECK(rules.message() == std::string_view("BRIEFING"));
    wait(rules, Rules::kMessageSeconds);
    CHECK(rules.message().empty());
  }

  {  // A bad step length falls back to a usable one.
    Rules rules(0.0);
    rules.onBallInPlay();
    CHECK(rules.ballSaveActive());
  }

  {  // Extreme step lengths still give every timer a sane, non-zero length:
     // a message always expires and a ball save is always offered.
    for (const double step : {1e-300, 1e-12, 100.0, 1e300}) {
      Rules rules(step);
      rules.onBallInPlay();
      CHECK(rules.ballSaveActive());
      rules.onHole(table::kScoopHole);
      CHECK(rules.message() == std::string_view("BRIEFING"));
      if (step >= 1.0) {
        rules.tick();
        CHECK(rules.message().empty());
        CHECK(!rules.ballSaveActive());
      }
    }
  }

  {  // A ball put back on the plunger without draining is out of play: a
     // tilt is cleared so the plunger works, nudges no longer count, and
     // nothing is scored, forfeited or used up.
    Rules rules(kStep);
    rules.onBallInPlay();
    rules.onBumper(0);
    rules.onNudge();
    rules.onNudge();
    rules.onNudge();
    CHECK(rules.tilted());
    wait(rules, Rules::kMessageSeconds);
    CHECK(rules.message() == std::string_view("TILT"));
    const std::uint64_t score = rules.score();
    rules.takeSounds();
    rules.onBallReturned();
    CHECK(!rules.tilted());
    CHECK(rules.tiltWarning() == 0.0);
    CHECK(rules.message() == std::string_view("BALL SAVED"));
    CHECK(rules.score() == score && score > 0);
    CHECK(rules.ball() == 1 && !rules.gameOver());
    CHECK(rules.takeSounds() == 0U);
    // On the plunger: shoves do nothing and cannot tilt.
    for (int i = 0; i < 5; ++i) {
      CHECK(!rules.onNudge());
    }
    CHECK(!rules.tilted());
    wait(rules, Rules::kMessageSeconds);
    CHECK(rules.message().empty());
    // Launched again, play carries on as before.
    rules.onBallInPlay();
    CHECK(rules.onNudge());
    rules.onBumper(0);
    CHECK(rules.score() > score);
    CHECK(drain(rules) == Rules::DrainResult::NextBall);

    // After the game it does nothing.
    Rules over(kStep);
    for (int i = 0; i < Rules::kBallsPerGame; ++i) {
      over.onDrain();
    }
    CHECK(over.gameOver());
    over.onBallReturned();
    CHECK(over.message() == std::string_view("GAME OVER"));
  }

  {  // Through the game: a full game ends after three balls and F2 restarts.
    pinball::Game game;
    int steps = 0;
    for (; steps < 120 * 3600 && !game.gameOver(); ++steps) {
      pinball::InputState input;
      input.plunger = (steps % 900) < 150;
      input.leftFlipper = (steps / 29) % 2 == 0;
      input.rightFlipper = (steps / 41) % 2 == 0;
      game.step(input);
    }
    CHECK(game.gameOver());
    CHECK(game.rules().score() > 0);
    CHECK(game.rules().ball() == Rules::kBallsPerGame);

    // With the game over the controls are dead: the ball stays on the plunger.
    const std::uint32_t drains = game.drainCount();
    for (int i = 0; i < 120 * 5; ++i) {
      pinball::InputState input;
      input.plunger = (i % 300) < 150;
      input.leftFlipper = true;
      game.step(input);
    }
    CHECK(game.drainCount() == drains);
    CHECK(game.ball().position.x > table::kLaneWallX);

    pinball::InputState press;
    press.newGame = true;
    game.step(press);
    CHECK(!game.gameOver());
    CHECK(game.rules().score() == 0);
    // Holding F2 does not restart again and again.
    game.step(press);
    game.step(pinball::InputState{});
    CHECK(game.rules().ball() == 1);
  }
}
