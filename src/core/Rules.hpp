#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "core/Missions.hpp"
#include "core/Sound.hpp"
#include "core/TableLayout.hpp"

namespace pinball {

// Point values. Everything a table feature awards is multiplied by the field
// multiplier, except the end-of-ball bonus which says so itself.
namespace points {
inline constexpr std::uint64_t kAttackBumper[4] = {500, 1000, 1500, 2000};
inline constexpr std::uint64_t kEngineBumper = 1500;
inline constexpr std::uint64_t kSatelliteBumper = 1000;
inline constexpr std::uint64_t kSlingshot = 500;
inline constexpr std::uint64_t kTarget = 750;
inline constexpr std::uint64_t kReEntryLane = 5000;
inline constexpr std::uint64_t kReEntryComplete = 10000;
inline constexpr std::uint64_t kInlane = 5000;
inline constexpr std::uint64_t kBonusLane = 20000;
inline constexpr std::uint64_t kWormhole = 2000;
inline constexpr std::uint64_t kBlackHole = 20000;
inline constexpr std::uint64_t kHyperspace[3] = {10000, 20000, 30000};
inline constexpr std::uint64_t kRamp = 5000;
inline constexpr std::uint64_t kReflexShot = 25000;
inline constexpr std::uint64_t kBoosterBank = 5000;
inline constexpr std::uint64_t kMedalBank[3] = {10000, 50000, 100000};
inline constexpr std::uint64_t kMultiplierBank = 5000;
inline constexpr std::uint64_t kJackpot = 500000;
inline constexpr std::uint64_t kBonusPerHit = 250;
inline constexpr std::uint64_t kMaxScore = 999'999'999'999ULL;
}  // namespace points

// The rules of the game: scoring, lights, balls and awards. It knows nothing
// about physics. Game tells it what the ball did and asks it what to show.
class Rules {
 public:
  static constexpr int kBallsPerGame = 3;
  static constexpr int kMaxExtraBalls = 5;
  static constexpr std::array<int, 5> kFieldMultipliers{1, 2, 3, 5, 10};
  // A ball that drains this soon after entering play is returned for free.
  static constexpr double kBallSaveSeconds = 6.0;
  // Repeating the ramp or the hyperspace chute within this window is a reflex shot.
  static constexpr double kReflexSeconds = 5.0;
  static constexpr double kMessageSeconds = 3.0;
  // Tilt: each nudge adds one unit of heat, which cools at one unit per
  // kTiltCoolSeconds. Reaching kTiltLimit tilts the table for the rest of
  // the ball. The limit sits just under three so that three nudges inside
  // about a second tilt, while occasional nudges never do.
  static constexpr double kTiltLimit = 2.5;
  static constexpr double kTiltCoolSeconds = 2.5;

  enum class DrainResult : std::uint8_t { BallSaved, ShootAgain, NextBall, GameOver };

  explicit Rules(double stepSeconds) noexcept;

  // Begins a new game: ball one, score zero, everything unlit.
  void startGame() noexcept;

  // --- What the ball did. Indices are into the TableLayout arrays; out of
  // --- range values are ignored. Nothing scores once the game is over.
  void onBumper(std::size_t index) noexcept;
  void onSlingshot(std::size_t index) noexcept;
  void onTarget(std::size_t index) noexcept;
  void onRollover(std::size_t index) noexcept;
  void onHole(std::size_t index) noexcept;
  // The launched ball has left the lane and entered play.
  void onBallInPlay() noexcept;
  // The ball went down the drain. Says what happens next.
  DrainResult onDrain() noexcept;
  // A flipper button went down: lit re-entry lanes rotate that way.
  void onFlipper(bool left) noexcept;
  // The player shoved the table. Returns true if the shove should move the
  // ball: false once tilted or when the game is over.
  bool onNudge() noexcept;
  // Once per simulation step.
  void tick() noexcept;

  // --- What to show.
  std::uint64_t score() const noexcept { return score_; }
  int ball() const noexcept { return ball_; }
  int extraBalls() const noexcept { return extraBalls_; }
  bool gameOver() const noexcept { return gameOver_; }
  // Tilted: the controls are dead and nothing scores until the ball drains.
  bool tilted() const noexcept { return tilted_; }
  // Tilt heat as a fraction of the limit, 0 to 1. Drives a warning light.
  double tiltWarning() const noexcept { return tiltHeat_ / kTiltLimit; }
  int fieldMultiplier() const noexcept { return kFieldMultipliers[multiplierLevel_]; }
  std::uint64_t bonus() const noexcept { return bonus_; }
  bool ballSaveActive() const noexcept { return ballSaveSteps_ > 0; }
  bool jackpotLit() const noexcept { return jackpotLit_; }
  bool bonusLaneLit() const noexcept { return bonusLaneLit_; }
  int attackBumperLevel() const noexcept { return attackLevel_; }
  int boosterLevel() const noexcept { return boosterLevel_; }
  int medalLevel() const noexcept { return medalLevel_; }
  int hyperspaceLevel() const noexcept { return hyperspaceLevel_; }
  bool targetLit(std::size_t index) const noexcept {
    return index < targetLit_.size() && targetLit_[index];
  }
  // Re-entry lane lights, left to right.
  bool laneLit(std::size_t index) const noexcept {
    return index < laneLit_.size() && laneLit_[index];
  }
  // Rank, mission and fuel.
  const MissionControl& missions() const noexcept { return missions_; }

  // Sounds the rules asked for since the last call. Calling it clears them.
  SoundSet takeSounds() noexcept {
    const SoundSet sounds = sounds_;
    sounds_ = 0;
    return sounds;
  }

  // Text for the message display. Empty when there is nothing to say.
  std::string_view message() const noexcept { return message_; }

 private:
  void award(std::uint64_t basePoints) noexcept;
  void addScore(std::uint64_t amount) noexcept;
  void addBonus() noexcept;
  void say(std::string_view text) noexcept;
  void cue(Sound sound) noexcept { sounds_ |= soundBit(sound); }
  // A mission announcement. It is not replaced by lesser messages from the
  // same event (the bank award that the completing hit also earned, say).
  void sayMission(std::string_view text) noexcept;
  void awardExtraBall() noexcept;
  void completeBank(table::TargetBank bank) noexcept;
  void resetBall() noexcept;
  void report(GoalSet goals) noexcept;
  void handleMission(const MissionControl::Outcome& outcome) noexcept;
  std::uint32_t stepsFor(double seconds) const noexcept;

  double stepSeconds_;
  MissionControl missions_;
  std::uint64_t score_ = 0;
  std::uint64_t bonus_ = 0;
  int ball_ = 1;
  int extraBalls_ = 0;
  bool gameOver_ = false;
  bool tilted_ = false;
  double tiltHeat_ = 0.0;
  std::size_t multiplierLevel_ = 0;

  std::array<bool, table::kTargets.size()> targetLit_{};
  std::array<bool, 3> laneLit_{};
  int attackLevel_ = 0;
  int boosterLevel_ = 0;
  int medalLevel_ = 0;
  int hyperspaceLevel_ = 0;
  bool jackpotLit_ = false;
  bool bonusLaneLit_ = false;

  std::uint32_t ballSaveSteps_ = 0;
  // The save is offered once per ball, not after every saved ball.
  bool ballSaveUsed_ = false;
  // Between entering play and draining. Mission fuel only burns then.
  bool ballInPlay_ = false;
  bool missionMessageThisStep_ = false;
  std::uint32_t rampReflexSteps_ = 0;
  std::uint32_t hyperspaceReflexSteps_ = 0;
  std::uint32_t messageSteps_ = 0;
  std::string_view message_;
  SoundSet sounds_ = 0;
};

}  // namespace pinball
