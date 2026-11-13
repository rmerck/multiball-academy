#include <cstddef>
#include <cstdint>
#include <string_view>

#include "Check.hpp"
#include "Suites.hpp"
#include "core/Game.hpp"
#include "core/Rules.hpp"
#include "core/TableLayout.hpp"

namespace {

using pinball::Rules;
namespace points = pinball::points;
namespace table = pinball::table;

constexpr double kStep = 1.0 / 120.0;

std::size_t firstBumper(table::BumperGroup group) {
  for (std::size_t i = 0; i < table::kBumpers.size(); ++i) {
    if (table::kBumpers[i].group == group) {
      return i;
    }
  }
  return 0;
}

std::size_t firstHole(table::HoleType type) {
  for (std::size_t i = 0; i < table::kHoles.size(); ++i) {
    if (table::kHoles[i].type == type) {
      return i;
    }
  }
  return 0;
}

std::size_t firstRollover(table::RolloverGroup group) {
  for (std::size_t i = 0; i < table::kRollovers.size(); ++i) {
    if (table::kRollovers[i].group == group) {
      return i;
    }
  }
  return 0;
}

void hitBank(Rules& rules, table::TargetBank bank) {
  for (std::size_t i = 0; i < table::kTargets.size(); ++i) {
    if (table::kTargets[i].bank == bank) {
      rules.onTarget(i);
    }
  }
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
    CHECK(rules.fieldMultiplier() == 1);
    CHECK(rules.extraBalls() == 0);
    CHECK(!rules.message().empty());
  }

  {  // Basic awards.
    Rules rules(kStep);
    rules.onBumper(firstBumper(table::BumperGroup::Attack));
    CHECK(rules.score() == points::kAttackBumper[0]);
    rules.onBumper(firstBumper(table::BumperGroup::Engine));
    rules.onBumper(firstBumper(table::BumperGroup::Satellite));
    rules.onSlingshot(0);
    CHECK(rules.score() == points::kAttackBumper[0] + points::kEngineBumper +
                               points::kSatelliteBumper + points::kSlingshot);
    CHECK(rules.bonus() == 3 * points::kBonusPerHit);
  }

  {  // Out-of-range indices are ignored everywhere.
    Rules rules(kStep);
    rules.onBumper(999);
    rules.onSlingshot(999);
    rules.onTarget(999);
    rules.onRollover(999);
    rules.onHole(999);
    CHECK(rules.score() == 0);
    CHECK(!rules.targetLit(999));
    CHECK(!rules.laneLit(999));
  }

  {  // Re-entry lanes: each lights, all three upgrade the attack bumpers up
     // to three times, and flipper presses rotate the lit lanes.
    Rules rules(kStep);
    const std::size_t lane = firstRollover(table::RolloverGroup::ReEntry);
    rules.onRollover(lane);
    CHECK(rules.laneLit(0) && !rules.laneLit(1) && !rules.laneLit(2));
    rules.onFlipper(false);
    CHECK(!rules.laneLit(0) && rules.laneLit(1) && !rules.laneLit(2));
    rules.onFlipper(false);
    rules.onFlipper(false);
    CHECK(rules.laneLit(0));
    rules.onFlipper(true);
    CHECK(rules.laneLit(2) && !rules.laneLit(0));
    rules.onFlipper(false);

    rules.onRollover(lane + 1);
    const std::uint64_t before = rules.score();
    rules.onRollover(lane + 2);
    CHECK(rules.score() == before + points::kReEntryLane + points::kReEntryComplete);
    CHECK(!rules.laneLit(0) && !rules.laneLit(1) && !rules.laneLit(2));
    CHECK(rules.attackBumperLevel() == 1);

    for (int round = 0; round < 5; ++round) {
      for (std::size_t i = 0; i < 3; ++i) {
        rules.onRollover(lane + i);
      }
    }
    CHECK(rules.attackBumperLevel() == 3);
    const std::uint64_t s = rules.score();
    rules.onBumper(firstBumper(table::BumperGroup::Attack));
    CHECK(rules.score() == s + points::kAttackBumper[3]);
  }

  {  // Targets light individually; a full bank pays, resets and advances.
    Rules rules(kStep);
    std::size_t medal = 0;
    for (std::size_t i = 0; i < table::kTargets.size(); ++i) {
      if (table::kTargets[i].bank == table::TargetBank::Medal) {
        medal = i;
        break;
      }
    }
    rules.onTarget(medal);
    CHECK(rules.targetLit(medal));
    rules.onTarget(medal);  // Hitting a lit target scores but changes nothing else.
    CHECK(rules.targetLit(medal));
    CHECK(rules.medalLevel() == 0);
    rules.onTarget(medal + 1);
    rules.onTarget(medal + 2);
    CHECK(!rules.targetLit(medal));
    CHECK(rules.medalLevel() == 1);
    CHECK(rules.score() == 4 * points::kTarget + points::kMedalBank[0]);

    hitBank(rules, table::TargetBank::Medal);
    CHECK(rules.medalLevel() == 2);
    CHECK(rules.extraBalls() == 0);
    hitBank(rules, table::TargetBank::Medal);
    CHECK(rules.extraBalls() == 1);
    CHECK(rules.medalLevel() == 0);
  }

  {  // Field multiplier: steps 1, 2, 3, 5, 10, stays at 10, applies to
     // awards, and resets on the next ball.
    Rules rules(kStep);
    const int expected[] = {2, 3, 5, 10, 10, 10};
    for (const int want : expected) {
      hitBank(rules, table::TargetBank::Multiplier);
      CHECK(rules.fieldMultiplier() == want);
    }
    const std::uint64_t before = rules.score();
    rules.onSlingshot(0);
    CHECK(rules.score() == before + 10 * points::kSlingshot);
    CHECK(drain(rules) == Rules::DrainResult::NextBall);
    CHECK(rules.fieldMultiplier() == 1);
  }

  {  // Booster bank: online, jackpot lit, bonus lanes lit, extra ball. The
     // jackpot is collected by the second hyperspace shot.
    Rules rules(kStep);
    hitBank(rules, table::TargetBank::Booster);
    CHECK(rules.boosterLevel() == 1 && !rules.jackpotLit());
    hitBank(rules, table::TargetBank::Booster);
    CHECK(rules.jackpotLit());
    hitBank(rules, table::TargetBank::Booster);
    CHECK(rules.bonusLaneLit());

    const std::size_t outlane = firstRollover(table::RolloverGroup::Outlane);
    std::uint64_t before = rules.score();
    rules.onRollover(outlane);
    CHECK(rules.score() == before + points::kBonusLane);
    CHECK(!rules.bonusLaneLit());
    before = rules.score();
    rules.onRollover(outlane);
    CHECK(rules.score() == before);  // Unlit outlane pays nothing.

    hitBank(rules, table::TargetBank::Booster);
    CHECK(rules.extraBalls() == 1);

    const std::size_t hyper = firstHole(table::HoleType::Hyperspace);
    before = rules.score();
    rules.onHole(hyper);
    CHECK(rules.score() == before + points::kHyperspace[0]);
    wait(rules, Rules::kReflexSeconds);
    before = rules.score();
    rules.onHole(hyper);
    CHECK(rules.score() == before + points::kJackpot);
    CHECK(!rules.jackpotLit());
  }

  {  // Hyperspace ladder without a jackpot: three bonuses then an extra ball.
    Rules rules(kStep);
    const std::size_t hyper = firstHole(table::HoleType::Hyperspace);
    std::uint64_t total = 0;
    for (int i = 0; i < 3; ++i) {
      rules.onHole(hyper);
      total += points::kHyperspace[i];
      wait(rules, Rules::kReflexSeconds);
    }
    CHECK(rules.score() == total);
    rules.onHole(hyper);
    CHECK(rules.extraBalls() == 1);
    CHECK(rules.hyperspaceLevel() == 0);
  }

  {  // Reflex shot: the same shot again inside the window pays extra; after
     // the window it does not.
    Rules rules(kStep);
    const std::size_t ramp = firstHole(table::HoleType::Ramp);
    rules.onHole(ramp);
    CHECK(rules.score() == points::kRamp);
    wait(rules, 1.0);
    rules.onHole(ramp);
    CHECK(rules.score() == 2 * points::kRamp + points::kReflexShot);
    wait(rules, Rules::kReflexSeconds);
    rules.onHole(ramp);
    CHECK(rules.score() == 3 * points::kRamp + points::kReflexShot);
  }

  {  // Other holes.
    Rules rules(kStep);
    rules.onHole(firstHole(table::HoleType::Wormhole));
    rules.onHole(firstHole(table::HoleType::BlackHole));
    CHECK(rules.score() == points::kWormhole + points::kBlackHole);
  }

  {  // Ball save: a drain soon after entering play costs nothing.
    Rules rules(kStep);
    rules.onBallInPlay();
    CHECK(rules.ballSaveActive());
    CHECK(rules.onDrain() == Rules::DrainResult::BallSaved);
    CHECK(rules.ball() == 1);
    // One save per ball: the returned ball is not saved a second time.
    rules.onBallInPlay();
    CHECK(!rules.ballSaveActive());
    CHECK(rules.onDrain() == Rules::DrainResult::NextBall);
    // The next ball gets its own.
    rules.onBallInPlay();
    wait(rules, Rules::kBallSaveSeconds);
    CHECK(!rules.ballSaveActive());
  }

  {  // Three balls, bonus paid at each drain, then game over; nothing scores
     // after that until a new game.
    Rules rules(kStep);
    rules.onBumper(0);
    rules.onBumper(0);
    const std::uint64_t before = rules.score();
    CHECK(drain(rules) == Rules::DrainResult::NextBall);
    CHECK(rules.score() == before + 2 * points::kBonusPerHit);
    CHECK(rules.bonus() == 0);
    CHECK(rules.ball() == 2);
    CHECK(drain(rules) == Rules::DrainResult::NextBall);
    CHECK(rules.ball() == 3);
    CHECK(drain(rules) == Rules::DrainResult::GameOver);
    CHECK(rules.gameOver());
    CHECK(rules.ball() == 3);

    const std::uint64_t final = rules.score();
    rules.onBumper(0);
    rules.onTarget(0);
    rules.onRollover(0);
    rules.onHole(0);
    rules.onSlingshot(0);
    rules.onFlipper(true);
    rules.onBallInPlay();
    CHECK(rules.score() == final);
    CHECK(rules.onDrain() == Rules::DrainResult::GameOver);
    wait(rules, 10.0);
    CHECK(!rules.message().empty());  // The game-over message stays up.

    rules.startGame();
    CHECK(!rules.gameOver() && rules.score() == 0 && rules.ball() == 1);
  }

  {  // An extra ball replays the same ball number.
    Rules rules(kStep);
    for (int i = 0; i < 3; ++i) {
      hitBank(rules, table::TargetBank::Medal);
    }
    CHECK(rules.extraBalls() == 1);
    CHECK(drain(rules) == Rules::DrainResult::ShootAgain);
    CHECK(rules.ball() == 1 && rules.extraBalls() == 0);
    CHECK(drain(rules) == Rules::DrainResult::NextBall);
  }

  {  // Extra balls are capped.
    Rules rules(kStep);
    for (int i = 0; i < 60; ++i) {
      hitBank(rules, table::TargetBank::Medal);
    }
    CHECK(rules.extraBalls() == Rules::kMaxExtraBalls);
  }

  {  // The score saturates; it never wraps.
    Rules rules(kStep);
    for (int i = 0; i < 6; ++i) {
      hitBank(rules, table::TargetBank::Multiplier);
    }
    hitBank(rules, table::TargetBank::Booster);
    hitBank(rules, table::TargetBank::Booster);
    const std::size_t hyper = firstHole(table::HoleType::Hyperspace);
    for (int i = 0; i < 4000000; ++i) {
      rules.onHole(hyper);
    }
    CHECK(rules.score() == points::kMaxScore);
    rules.onBumper(0);
    CHECK(rules.score() == points::kMaxScore);
  }

  {  // Messages expire.
    Rules rules(kStep);
    rules.onHole(firstHole(table::HoleType::BlackHole));
    CHECK(rules.message() == std::string_view("BLACK HOLE"));
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
      rules.onHole(firstHole(table::HoleType::BlackHole));
      CHECK(rules.message() == std::string_view("BLACK HOLE"));
      if (step >= 1.0) {
        rules.tick();
        CHECK(rules.message().empty());
        CHECK(!rules.ballSaveActive());
      }
    }
  }

  {  // At the 10x cap the multiplier bank still pays, but does not claim to
     // have raised anything.
    Rules rules(kStep);
    for (int i = 0; i < 3; ++i) {
      hitBank(rules, table::TargetBank::Multiplier);
      CHECK(rules.message() == std::string_view("FIELD MULTIPLIER RAISED"));
    }
    hitBank(rules, table::TargetBank::Multiplier);
    CHECK(rules.fieldMultiplier() == 10);
    CHECK(rules.message() == std::string_view("FIELD MULTIPLIER RAISED"));
    const std::uint64_t before = rules.score();
    hitBank(rules, table::TargetBank::Multiplier);
    CHECK(rules.fieldMultiplier() == 10);
    CHECK(rules.message() == std::string_view("FIELD MULTIPLIER AT MAXIMUM"));
    std::uint64_t targets = 0;
    for (const table::Target& target : table::kTargets) {
      targets += target.bank == table::TargetBank::Multiplier ? 1U : 0U;
    }
    CHECK(rules.score() == before + 10 * (targets * points::kTarget + points::kMultiplierBank));
  }

  {  // The extra-ball rung of the hyperspace ladder with no room for another
     // ball pays the top bonus and says so, instead of doing nothing.
    Rules rules(kStep);
    for (int i = 0; i < 60; ++i) {
      hitBank(rules, table::TargetBank::Medal);
    }
    CHECK(rules.extraBalls() == Rules::kMaxExtraBalls);
    const std::size_t hyper = firstHole(table::HoleType::Hyperspace);
    for (int i = 0; i < 3; ++i) {
      rules.onHole(hyper);
      wait(rules, Rules::kReflexSeconds);
    }
    CHECK(rules.hyperspaceLevel() == 3);
    CHECK(rules.message().empty());
    const std::uint64_t before = rules.score();
    rules.takeSounds();
    rules.onHole(hyper);
    CHECK(rules.extraBalls() == Rules::kMaxExtraBalls);
    CHECK(rules.hyperspaceLevel() == 0);
    CHECK(rules.score() == before + points::kHyperspace[2]);
    CHECK(rules.message() == std::string_view("HYPERSPACE BONUS"));
    CHECK((rules.takeSounds() & pinball::soundBit(pinball::Sound::ExtraBall)) == 0U);
    // With room, the same rung is the extra ball and pays no points.
    Rules roomy(kStep);
    for (int i = 0; i < 3; ++i) {
      roomy.onHole(hyper);
      wait(roomy, Rules::kReflexSeconds);
    }
    const std::uint64_t roomyBefore = roomy.score();
    roomy.onHole(hyper);
    CHECK(roomy.extraBalls() == 1);
    CHECK(roomy.score() == roomyBefore);
    CHECK(roomy.message() == std::string_view("EXTRA BALL"));
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
    const std::uint64_t bonus = rules.bonus();
    rules.takeSounds();
    rules.onBallReturned();
    CHECK(!rules.tilted());
    CHECK(rules.tiltWarning() == 0.0);
    CHECK(rules.message() == std::string_view("RE-DEPLOY"));
    CHECK(rules.score() == score && rules.bonus() == bonus && bonus > 0);
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

    // Fuel stops burning while the returned ball waits.
    Rules fuel(kStep);
    fuel.onBallInPlay();
    for (std::size_t i = 0; i < table::kTargets.size(); ++i) {
      if (table::kTargets[i].bank == table::TargetBank::Mission) {
        fuel.onTarget(i);
        break;
      }
    }
    fuel.onHole(firstHole(table::HoleType::Ramp));
    CHECK(fuel.missions().phase() == pinball::MissionControl::Phase::Active);
    fuel.onBallReturned();
    wait(fuel, pinball::MissionControl::kFuelSeconds + 5.0);
    CHECK(fuel.missions().phase() == pinball::MissionControl::Phase::Active);
    CHECK(fuel.missions().fuelLights() == pinball::MissionControl::kFuelLights);

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
    for (; steps < 120 * 1200 && !game.gameOver(); ++steps) {
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
