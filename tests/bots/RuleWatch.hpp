#pragma once

#include <cstddef>
#include <cstdint>

#include "core/Courses.hpp"
#include "core/DisplayText.hpp"
#include "core/Game.hpp"
#include "core/Rules.hpp"

// What must hold on every step of every game, whatever the player and the
// balls do: the ball count, what is left running when a game is over, the
// score, and that everything the display is given fits it.
namespace pinball::bots {

struct RuleWatch {
  std::uint64_t lastScore = 0;
  int multiballWithoutBalls = 0;
  int ballsWithoutMultiball = 0;
  // Tell it before a step that starts a new game: the score starts again.
  void newGame() noexcept { lastScore = 0; }
};

// Returns what is wrong, or nullptr. Call after every step.
[[nodiscard]] inline const char* brokenRule(const Game& game, RuleWatch& watch) {
  const Rules& rules = game.rules();
  const std::size_t queued = game.queuedBalls();
  std::size_t playing = queued;
  for (const BallView& ball : game.balls()) {
    playing += ball.locked ? 0U : 1U;
  }
  if (game.ballCount() + queued > Game::kMaxBalls) {
    return "more than three balls on the table and waiting";
  }
  if (!rules.gameOver() && playing == 0) {
    return "no ball in a game that is not over";
  }
  if (rules.gameOver()) {
    if (rules.multiball() != Multiball::None || rules.courseRunning() ||
        rules.graduationRunning() || rules.ballSaveActive() || rules.lockLit() ||
        rules.superJackpotLit() || rules.overdrive() != 1 || rules.comboLevel() != 0 ||
        rules.kickbackLit() || rules.skillShotOffered()) {
      return "something still running after game over";
    }
    if (game.lockedBalls() != 0 || queued != 0) {
      return "balls locked or waiting after game over";
    }
  }
  if (rules.score() < watch.lastScore) {
    return "the score went down";
  }
  watch.lastScore = rules.score();
  if (rules.score() > points::kMaxScore) {
    return "the score is over its limit";
  }
  if (!rules.message().empty() && !display::fits(rules.message(), display::kMaxMessageLength)) {
    return "a message does not fit the display";
  }
  if (!rules.gameOver() && !display::fits(rules.goalText(), display::kMaxGoalTextLength)) {
    return "a goal line does not fit the display";
  }
  if (!rules.courseName().empty() &&
      !display::fits(rules.courseName(), display::kMaxMissionNameLength)) {
    return "a course name does not fit the display";
  }
  if (rules.rank() >= kRankCount || rules.ball() < 1 || rules.ball() > Rules::kBallsPerGame ||
      rules.overdrive() < 1 || rules.overdrive() > Rules::kMaxOverdrive ||
      rules.comboLevel() < 0 || rules.comboLevel() > Rules::kMaxCombo ||
      rules.bonusMultiplier() < 1 || rules.bonusMultiplier() > Rules::kMaxBonusMultiplier ||
      rules.extraBalls() < 0 || rules.extraBalls() > Rules::kMaxExtraBallsHeld ||
      rules.locksHeld() < 0 || rules.locksHeld() > Rules::kLocksNeeded ||
      rules.turboMeter() < 0 || rules.turboMeter() >= rules.turboGoal() ||
      rules.goalRemaining() < 0 || rules.goalRemaining() > 255 ||
      rules.rampsForBriefing() < 0 || rules.examShotsLeft() < 0) {
    return "a number the display shows is out of its range";
  }
  // A multiball and its balls go together, give or take the moments in
  // which balls are let out of the Hangar or served to the lane.
  constexpr int kSlack = 5 * 120;
  const bool multiball = rules.multiball() != Multiball::None;
  watch.multiballWithoutBalls = multiball && playing <= 1 ? watch.multiballWithoutBalls + 1 : 0;
  watch.ballsWithoutMultiball =
      !multiball && !rules.gameOver() && playing >= 2 ? watch.ballsWithoutMultiball + 1 : 0;
  if (watch.multiballWithoutBalls > kSlack) {
    return "a multiball with one ball";
  }
  if (watch.ballsWithoutMultiball > kSlack) {
    return "several balls and no multiball";
  }
  return nullptr;
}

}  // namespace pinball::bots
