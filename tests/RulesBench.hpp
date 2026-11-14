#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "core/Rules.hpp"
#include "core/TableLayout.hpp"

// The rules on a bench of their own: a stand-in for the table that does
// what Game does with the rules' answers (doors, balls asked for, locks let
// go) and nothing else, so that every rule can be tested an event at a time.
namespace pinball_test {

namespace table = pinball::table;

inline constexpr double kRuleStep = 1.0 / 120.0;

struct RulesBench {
  pinball::Rules rules{kRuleStep};
  // One ball, free on the playfield.
  pinball::TableStatus balls{.live = 1, .free = 1, .queued = 0, .locked = 0};
  bool doorsUp = true;
  int launched = 0;  // Balls asked for and put on the table.
  int refused = 0;   // Asked for with three already there.
  int dropped = 0;
  int raised = 0;
  // Whether a locked ball gets its served ball (Game: only with no other).
  bool announce = true;

  RulesBench() {
    apply();
    rules.onBallInPlay();
  }

  [[nodiscard]] int total() const noexcept { return balls.live + balls.queued + balls.locked; }

  // What Game does every step with what the rules ask.
  void apply() {
    switch (rules.takeDoorCommand()) {
      case pinball::Rules::DoorCommand::Drop:
        if (doorsUp) {
          doorsUp = false;
          ++dropped;
          rules.onDoorBank(false);
        }
        break;
      case pinball::Rules::DoorCommand::Raise:
        if (!doorsUp) {
          doorsUp = true;
          ++raised;
          rules.onDoorBank(true);
        }
        break;
      case pinball::Rules::DoorCommand::None:
        break;
    }
    if (rules.takeLockRelease()) {
      releaseLocks();
    }
    for (int n = rules.takeBallRequests(); n > 0; --n) {
      if (total() < 3) {
        ++balls.live;
        ++balls.free;
        ++launched;
      } else {
        ++refused;
      }
    }
  }
  void releaseLocks() {
    while (balls.locked > 0) {
      --balls.locked;
      ++balls.live;
      ++balls.free;
      rules.onLockReleased(static_cast<std::size_t>(balls.locked));
    }
  }
  void step(int steps = 1) {
    for (int i = 0; i < steps; ++i) {
      rules.setTable(balls);
      rules.tick();
      apply();
    }
  }
  void wait(double seconds) { step(static_cast<int>(seconds / kRuleStep) + 2); }

  // --- Things a ball does ----------------------------------------------------
  void bumper(int times = 1) {
    for (int i = 0; i < times; ++i) {
      rules.onBumper(static_cast<std::size_t>(i) % table::kBumpers.size());
    }
  }
  void spin(int times = 1) {
    for (int i = 0; i < times; ++i) {
      rules.onSpin();
    }
  }
  void turbine() { rules.onSensor(table::kSpinnerSensor, 1, 800.0F, 0); }
  void rollCall(std::size_t lane, std::size_t ball = 0) { rules.onRollover(lane, ball); }
  // The letters W, I, N, G: 0 to 3.
  void wing(std::size_t letter) { rules.onRollover(3 + letter); }
  void runway() { rules.onHole(table::kRunwayHole); }
  void skyway() { rules.onHole(table::kSkywayHole); }
  void circuit(bool right, std::size_t ball = 0) {
    rules.onSensor(right ? 1 : 0, 1, 1200.0F, ball);
    rules.onSensor(2, right ? -1 : 1, 900.0F, ball);
  }
  void scoop() {
    rules.onHole(table::kScoopHole);
    step();
    rules.onEject(table::kScoopHole);
  }
  void door(std::size_t index) { rules.onDropTarget(index); }
  // All three doors, and the step in which the bank drops.
  void doors() {
    for (std::size_t i = 0; i < table::kDropTargets.size(); ++i) {
      door(i);
    }
    step();
  }
  // A ball into the Hangar, with what Game does about the answer.
  pinball::Rules::HoleResult hangar() {
    const pinball::Rules::HoleResult result = rules.onHole(table::kHangarHole);
    switch (result) {
      case pinball::Rules::HoleResult::Lock: {
        --balls.live;
        ++balls.locked;
        const bool served = balls.live == 0 && balls.queued == 0;
        if (served) {
          ++balls.live;
        }
        balls.free = std::min(balls.free, balls.live);
        rules.onBallLocked(static_cast<std::size_t>(balls.locked - 1), served);
        if (served) {
          // And the player plunges it.
          balls.free = balls.live;
          rules.onBallInPlay();
        }
        break;
      }
      case pinball::Rules::HoleResult::StartMultiball:
        step();
        rules.onEject(table::kHangarHole);
        releaseLocks();
        break;
      case pinball::Rules::HoleResult::Eject:
        step();
        rules.onEject(table::kHangarHole);
        break;
    }
    return result;
  }

  // The last ball drains: Game puts one on the plunger and the player
  // plunges it.
  pinball::Rules::DrainResult drain(pinball::DrainInfo info = {}) {
    const pinball::Rules::DrainResult result = rules.onDrain(info);
    balls.live = 1;
    balls.free = 1;
    balls.queued = 0;
    if (rules.gameOver()) {
      balls.locked = 0;  // The bays are emptied.
    } else {
      rules.onBallInPlay();
    }
    rules.setTable(balls);
    return result;
  }
  // The same with the saver run out first.
  pinball::Rules::DrainResult drainForGood() {
    rules.onSlingshot(0);
    wait(pinball::Rules::kBallSaveSeconds);
    return drain();
  }
  // One of several balls drains. Returns whether it came back.
  bool lose(pinball::DrainInfo info = {}) {
    const bool owed = rules.onBallLost(info);
    --balls.live;
    balls.free = std::min(balls.free, balls.live);
    if (owed) {
      // Served and launched again.
      ++balls.live;
      ++balls.free;
    }
    return owed;
  }

  [[nodiscard]] std::string_view message() const noexcept { return rules.message(); }
  [[nodiscard]] std::uint64_t score() const noexcept { return rules.score(); }
};

}  // namespace pinball_test
