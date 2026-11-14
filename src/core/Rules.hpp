#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "core/DisplayText.hpp"
#include "core/Sound.hpp"
#include "core/TableLayout.hpp"

namespace pinball {

// Point values. Provisional: one flat value for each thing the ball can do
// on the table, so that a game has a score while the table's own rule set
// (courses, multiball, combos, bonus) is still to come.
namespace points {
inline constexpr std::uint64_t kBumper = 1000;
inline constexpr std::uint64_t kSlingshot = 250;
inline constexpr std::uint64_t kTarget = 2500;
inline constexpr std::uint64_t kBayDoor = 2000;
inline constexpr std::uint64_t kRollCallLane = 3000;
inline constexpr std::uint64_t kReturnLane = 1000;
inline constexpr std::uint64_t kBriefingRoom = 5000;
inline constexpr std::uint64_t kHangar = 10000;
inline constexpr std::uint64_t kRamp = 6000;
inline constexpr std::uint64_t kMaxScore = 999'999'999'999ULL;
}  // namespace points

// The rules of the game: scoring, balls and messages. It knows nothing about
// physics. Game tells it what the ball did and asks it what to show.
//
// This is a provisional rule set for the new table: every element scores a
// flat value, a game is three balls, the ball save and the tilt work as they
// always have. Nothing lights, there are no courses and rank is never earned.
class Rules {
 public:
  static constexpr int kBallsPerGame = 3;
  // A ball that drains this soon after entering play is returned for free.
  static constexpr double kBallSaveSeconds = 6.0;
  static constexpr double kMessageSeconds = 3.0;
  // Tilt: each nudge adds one unit of heat, which cools at one unit per
  // kTiltCoolSeconds. Reaching kTiltLimit tilts the table for the rest of
  // the ball. The limit sits just under three so that three nudges inside
  // about a second tilt, while occasional nudges never do.
  static constexpr double kTiltLimit = 2.5;
  static constexpr double kTiltCoolSeconds = 2.5;

  enum class DrainResult : std::uint8_t { BallSaved, NextBall, GameOver };

  explicit Rules(double stepSeconds) noexcept;

  // Begins a new game: ball one, score zero.
  void startGame() noexcept;

  // --- What the ball did. Indices are into the TableLayout arrays; out of
  // --- range values are ignored. Nothing scores once the game is over.
  void onBumper(std::size_t index) noexcept;
  void onSlingshot(std::size_t index) noexcept;
  void onTarget(std::size_t index) noexcept;
  void onDropTarget(std::size_t index) noexcept;
  void onRollover(std::size_t index) noexcept;
  void onHole(std::size_t index) noexcept;
  // The launched ball has left the lane and entered play. Once per ball in
  // play: it arms the ball save.
  void onBallInPlay() noexcept;
  // The ball is back on the plunger without having drained (Game's fail-safe
  // for a lost ball). It is out of play until launched again: nudges stop
  // counting, and a tilt is cleared so that the plunger works. Nothing is
  // scored or forfeited and the ball number stays.
  void onBallReturned() noexcept;
  // The ball went down the drain. Says what happens next.
  DrainResult onDrain() noexcept;
  // The player shoved the table. Returns true if the shove should move the
  // ball: false once tilted or when the game is over.
  bool onNudge() noexcept;
  // Once per simulation step.
  void tick() noexcept;

  // --- What to show.
  std::uint64_t score() const noexcept { return score_; }
  int ball() const noexcept { return ball_; }
  bool gameOver() const noexcept { return gameOver_; }
  // Tilted: the controls are dead and nothing scores until the ball drains.
  bool tilted() const noexcept { return tilted_; }
  // Tilt heat as a fraction of the limit, 0 to 1. Drives a warning light.
  double tiltWarning() const noexcept { return tiltHeat_ / kTiltLimit; }
  bool ballSaveActive() const noexcept { return ballSaveSteps_ > 0; }
  // Between entering play and draining (or being returned to the plunger).
  bool ballInPlay() const noexcept { return ballInPlay_; }
  // Index into the rank names (core/Missions.hpp). Rank is earned by passing
  // courses, which this rule set does not have: always the first.
  std::size_t rank() const noexcept { return 0; }

  // Sounds the rules asked for since the last call. Calling it clears them.
  SoundSet takeSounds() noexcept {
    const SoundSet sounds = sounds_;
    sounds_ = 0;
    return sounds;
  }

  // Text for the message display. Empty when there is nothing to say.
  std::string_view message() const noexcept { return message_; }
  // What it says once the last ball has drained. It names no key: how a new
  // game is started depends on the device, so the platform adds that.
  static constexpr display::Message kGameOverMessage{"GAME OVER"};

 private:
  void award(std::uint64_t points) noexcept;
  // Every message is checked against the display when it is compiled.
  void say(display::Message text) noexcept;
  void cue(Sound sound) noexcept { sounds_ |= soundBit(sound); }
  void resetBall() noexcept;
  std::uint32_t stepsFor(double seconds) const noexcept;

  double stepSeconds_;
  std::uint64_t score_ = 0;
  int ball_ = 1;
  bool gameOver_ = false;
  bool tilted_ = false;
  double tiltHeat_ = 0.0;

  std::uint32_t ballSaveSteps_ = 0;
  // The save is offered once per ball, not after every saved ball.
  bool ballSaveUsed_ = false;
  // Between entering play and draining.
  bool ballInPlay_ = false;
  std::uint32_t messageSteps_ = 0;
  std::string_view message_;
  SoundSet sounds_ = 0;
};

}  // namespace pinball
