#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>

#include "Check.hpp"
#include "GameTestAccess.hpp"
#include "RulesBench.hpp"
#include "Suites.hpp"
#include "bots/Bots.hpp"
#include "bots/RuleWatch.hpp"
#include "bots/TableHarness.hpp"
#include "core/Game.hpp"
#include "core/Rules.hpp"
#include "core/TableLayout.hpp"

// The rules under random play. Two runs, both the same every time:
//   1. the rules alone, fed random events through the bench's stand-in for
//      the table, far faster and deeper into the game than play gets;
//   2. the whole Game with a bot at the flippers and a hand that also puts
//      balls into holes and lanes, closes switches, shoves the table and
//      presses the start button at random.
// After every step the invariants of bots/RuleWatch.hpp must hold, and with
// them: the rules never ask for a ball past the three-ball limit.
namespace {

using pinball::Game;
using pinball::Multiball;
using pinball::Rules;
using pinball::Vec2;
using pinball::bots::Rng;
using pinball_test::RulesBench;
namespace display = pinball::display;
namespace points = pinball::points;
namespace table = pinball::table;
using Seam = pinball::GameTestAccess;

// What must hold for the rules on the bench after a step.
const char* brokenOnBench(const RulesBench& bench, std::uint64_t& lastScore) {
  const Rules& rules = bench.rules;
  if (bench.total() > 3) {
    return "more than three balls";
  }
  if (rules.score() < lastScore || rules.score() > points::kMaxScore) {
    return "the score went down or over its limit";
  }
  lastScore = rules.score();
  if (!rules.message().empty() && !display::fits(rules.message(), display::kMaxMessageLength)) {
    return "a message does not fit";
  }
  if (rules.gameOver()) {
    if (rules.multiball() != Multiball::None || rules.courseRunning() || rules.lockLit() ||
        rules.ballSaveActive() || rules.locksHeld() != 0 || rules.graduationRunning()) {
      return "something running after game over";
    }
    return nullptr;
  }
  if (!display::fits(rules.goalText(), display::kMaxGoalTextLength)) {
    return "a goal line does not fit";
  }
  if (rules.ballsLive() != bench.balls.live + bench.balls.queued) {
    return "the rules miscount the balls in play";
  }
  if (rules.locksHeld() != bench.balls.locked) {
    return "the rules miscount the locked balls";
  }
  if ((rules.multiball() != Multiball::None) != (bench.balls.live + bench.balls.queued >= 2)) {
    return "multiball and the ball count disagree";
  }
  if ((rules.lockLit() || rules.superJackpotLit()) && bench.doorsUp) {
    return "the Hangar is lit behind standing doors";
  }
  if (rules.turboMeter() >= rules.turboGoal() || rules.overdrive() < 1 || rules.overdrive() > 3 ||
      rules.comboLevel() > Rules::kMaxCombo || rules.extraBalls() > Rules::kMaxExtraBallsHeld ||
      rules.bonusMultiplier() > Rules::kMaxBonusMultiplier || rules.rank() >= pinball::kRankCount) {
    return "a number out of range";
  }
  return nullptr;
}

void fuzzRulesAlone(std::uint64_t seed, int operations) {
  RulesBench bench;
  Rng rng(seed);
  std::uint64_t lastScore = 0;
  for (int n = 0; n < operations; ++n) {
    const int roll = rng.range(0, 99);
    Rules& rules = bench.rules;
    if (roll < 14) {
      bench.bumper(rng.range(1, 12));
    } else if (roll < 26) {
      bench.spin(rng.range(1, 24));
    } else if (roll < 30) {
      rules.onTarget(static_cast<std::size_t>(rng.range(0, 1)));
    } else if (roll < 38) {
      if (bench.doorsUp) {  // A door that is down cannot be hit.
        bench.door(static_cast<std::size_t>(rng.range(0, 2)));
      }
    } else if (roll < 46) {
      rules.onRollover(static_cast<std::size_t>(rng.range(0, 6)),
                       static_cast<std::size_t>(rng.range(0, 2)));
    } else if (roll < 54) {
      rng.range(0, 1) == 0 ? bench.runway() : bench.skyway();
    } else if (roll < 60) {
      bench.circuit(rng.range(0, 1) == 1, static_cast<std::size_t>(rng.range(0, 2)));
    } else if (roll < 64) {
      bench.turbine();
    } else if (roll < 71) {
      bench.scoop();
    } else if (roll < 79) {
      if (!bench.doorsUp) {  // Nor the Hangar entered past a standing one.
        bench.hangar();
      }
    } else if (roll < 81) {
      if (rules.kickbackLit()) {
        rules.onKickback();
      }
    } else if (roll < 84) {
      rules.onFlipper(rng.range(0, 1) == 1);
    } else if (roll < 86) {
      rules.onNudge();
      if (rng.range(0, 3) == 0) {
        rules.onNudge();
        rules.onNudge();
      }
    } else if (roll < 91) {
      // A ball drains: one of several, or the last.
      const pinball::DrainInfo info{.afterEject = rng.range(0, 5) == 0};
      if (bench.balls.live + bench.balls.queued > 1) {
        bench.lose(info);
      } else {
        bench.drain(info);
      }
    } else if (roll < 92) {
      rules.onSlingshot(static_cast<std::size_t>(rng.range(0, 1)));
    } else if (roll < 93 && rules.gameOver()) {
      bench = RulesBench{};
      lastScore = 0;
    } else {
      // Time passes, sometimes with every ball in a hold.
      const int free = bench.balls.free;
      if (rng.range(0, 4) == 0) {
        bench.balls.free = 0;
        bench.allHeld = rng.range(0, 1) == 0;
      }
      bench.step(rng.range(1, 600));
      bench.balls.free = free;
      bench.allHeld = false;
    }
    // Two steps: a ball asked for is served in the first, and the rules
    // hear it was in the second (as with Game).
    bench.step(2);
    const char* broken = brokenOnBench(bench, lastScore);
    if (broken != nullptr) {
      std::fprintf(stderr, "rules fuzz, seed %llu, operation %d: %s\n",
                   static_cast<unsigned long long>(seed), n, broken);
      CHECK(broken == nullptr);
      return;
    }
  }
  // The table refuses what it has no room for, and the rules live with it.
  CHECK(bench.total() <= 3);
}

// Places where a ball does something when it is put there moving up.
struct Drop {
  Vec2 position;
  Vec2 velocity;
};
constexpr Drop kDrops[] = {
    {{356.0F, 500.0F}, {0.0F, -900.0F}},                   // Into the Briefing room.
    {{279.0F, 440.0F}, {0.0F, -900.0F}},                   // At the Hangar or its doors.
    {{192.0F, 470.0F}, {0.0F, -1100.0F}},                  // Up the Runway.
    {{519.0F, 470.0F}, {0.0F, -1100.0F}},                  // Up the Skyway.
    {{432.0F, 470.0F}, {0.0F, -1200.0F}},                  // Up the Turbine lane.
    {{114.0F, 470.0F}, {0.0F, -1600.0F}},                  // Round the left circuit.
    {{355.5F, 880.0F}, {0.0F, 300.0F}},                    // Down the middle.
    {{59.0F, 680.0F}, {0.0F, 200.0F}},                     // Down the left outlane.
    {{652.0F, 680.0F}, {0.0F, 200.0F}},                    // Down the right outlane.
    {{355.5F, 130.0F}, {0.0F, 100.0F}},                    // Into a roll call lane.
};

// How far the game runs got, all together.
struct Reached {
  int multiballSteps = 0;
  int gamesOver = 0;
  int locks = 0;
};

void fuzzGame(std::uint64_t seed, int steps, bool reflex, Reached& reached) {
  pinball::bots::TableHarness harness;
  harness.setSaver(true);
  Game& game = harness.game();
  std::unique_ptr<pinball::bots::Bot> bot;
  if (reflex) {
    bot = std::make_unique<pinball::bots::ReflexBot>(seed, 5);
  } else {
    bot = std::make_unique<pinball::bots::RandomBot>(seed);
  }
  Rng rng(seed ^ 0x5EEDULL);
  pinball::bots::RuleWatch watch;
  int pull = 0;
  int waited = 0;
  int tilts = 0;
  int multiballSteps = 0;
  int gamesOver = 0;
  for (int n = 0; n < steps; ++n) {
    harness.act(*bot);
    // The plunger, whenever a ball waits for it.
    waited = game.ballAwaitingLaunch() ? waited + 1 : 0;
    if (pull == 0 && waited > 30) {
      pull = rng.range(60, 125);
    }
    harness.plunger = pull > 1;
    pull = pull > 0 ? pull - 1 : 0;

    const int roll = rng.range(0, 9999);
    Rules& rules = Seam::rules(game);
    if (roll < 40) {
      // The hand: a free ball in play is put somewhere that matters.
      const std::size_t slot = static_cast<std::size_t>(rng.range(0, 2));
      if (harness.live(slot) && !harness.held(slot) && game.ballInPlay(slot) &&
          !game.ballLocked(slot)) {
        const Drop& drop = kDrops[static_cast<std::size_t>(rng.range(0, 9))];
        harness.put(slot, drop.position, drop.velocity, true);
      }
    } else if (roll < 70) {
      // Switches closed with no ball near them, in bursts.
      switch (rng.range(0, 6)) {
        case 0:
          for (int i = rng.range(1, 40); i > 0; --i) {
            rules.onSpin();
          }
          break;
        case 1:
          for (int i = rng.range(1, 12); i > 0; --i) {
            rules.onBumper(0);
          }
          break;
        case 2:
          if (game.doorBankUp()) {
            for (std::size_t door = 0; door < table::kDropTargets.size(); ++door) {
              rules.onDropTarget(door);
            }
          }
          break;
        case 3:
          rules.onHole(table::kScoopHole);
          break;
        case 4:
          for (int i = rng.range(1, 3); i > 0; --i) {
            rules.onHole(rng.range(0, 1) == 0 ? table::kRunwayHole : table::kSkywayHole);
          }
          break;
        case 5:
          rules.onSensor(0, 1, 1500.0F, 0);
          rules.onSensor(2, 1, 900.0F, 0);
          break;
        default:
          rules.onTarget(0);
          rules.onTarget(1);
          break;
      }
    } else if (roll < 76) {
      harness.nudge = rng.range(1, 3);
    } else if (roll < 78) {
      tilts = 5;  // Three shoves in a row tilt the table.
    } else if (roll == 9999) {
      harness.restart = true;
    }
    if (tilts > 0 && n % 2 == 0) {
      harness.nudge = 1 + tilts % 3;
      --tilts;
    }
    if (harness.restart || game.gameOver()) {
      watch.newGame();
    }
    harness.advance();
    const char* broken = pinball::bots::brokenRule(game, watch);
    if (broken != nullptr) {
      std::fprintf(stderr, "game fuzz, seed %llu, step %d: %s\n",
                   static_cast<unsigned long long>(seed), n, broken);
      CHECK(broken == nullptr);
      return;
    }
    multiballSteps += game.rules().multiball() != Multiball::None ? 1 : 0;
    gamesOver += game.gameOver() ? 1 : 0;
    if (harness.events.size() > 4096) {
      harness.events.clear();
    }
  }
  // (The bench's own count of lost balls is no use here: a ball the hand
  // moves, or a game started over, looks lost to it.)
  CHECK(harness.hangarEntriesDoorsUp == 0);
  reached.multiballSteps += multiballSteps;
  reached.gamesOver += gamesOver;
  reached.locks += static_cast<int>(harness.locks);
}

}  // namespace

void testRuleFuzz() {
  for (std::uint64_t seed = 1; seed <= 24; ++seed) {
    fuzzRulesAlone(seed, 6000);
  }
  Reached reached;
  fuzzGame(11, 36000, true, reached);
  fuzzGame(12, 36000, false, reached);
  fuzzGame(13, 24000, true, reached);
  // The runs got somewhere: multiballs were played, balls locked, games ended.
  CHECK(reached.multiballSteps > 1000 && reached.gamesOver > 0 && reached.locks > 0);
}
