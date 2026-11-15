#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include "core/Settings.hpp"
#include "core/Sound.hpp"

// What the game gives the player besides pictures and sound: a touch of the
// device's haptics for what happens on the table, and, now and then, the
// system's request for a store review. No SDL and no system calls: the
// decisions only, so they are unit tested.
namespace pinball {

// The kinds of haptic the game plays, lightest first.
enum class Haptic : std::uint8_t {
  Flipper,  // A flipper goes up: the lightest tap.
  Hit,      // A thruster bumper or a slingshot.
  Drain,    // A ball is lost: a firm knock.
  Jackpot,  // A jackpot, super jackpot or multiball: the success pattern.
};

// Turns the sounds of a frame into at most one haptic, so that the hand
// feels the same events the ear hears, and keeps a bumper storm from
// buzzing on and on. Each kind has its own least interval, and all of them
// together keep a least gap; a heavier kind that is due wins over a
// lighter one in the same frame.
class HapticGate {
 public:
  // Least time from one haptic of a kind to the next of the same kind, ms.
  static constexpr std::uint64_t kFlipperMs = 60;
  static constexpr std::uint64_t kHitMs = 140;
  static constexpr std::uint64_t kDrainMs = 400;
  static constexpr std::uint64_t kJackpotMs = 600;
  // Least time between any two.
  static constexpr std::uint64_t kAnyMs = 40;
  // In a run of hits, no more than this many in kBurstWindowMs.
  static constexpr int kBurstHits = 4;
  static constexpr std::uint64_t kBurstWindowMs = 1000;

  // The haptic to play for a frame that made these sounds at nowMs, if
  // any. Times must not run backwards; one that does counts as no time.
  [[nodiscard]] std::optional<Haptic> take(SoundSet sounds, std::uint64_t nowMs) noexcept;

 private:
  [[nodiscard]] bool due(Haptic kind, std::uint64_t nowMs) const noexcept;

  std::uint64_t last_[4]{};
  bool played_[4]{};
  std::uint64_t lastAny_ = 0;
  bool playedAny_ = false;
  // When the current burst of hits began, and how many it has had.
  std::uint64_t burstStart_ = 0;
  int burstHits_ = 0;
};

// The kind of haptic a set of sounds calls for, ignoring rate, or none.
[[nodiscard]] std::optional<Haptic> hapticFor(SoundSet sounds) noexcept;

// One event of a haptic as the device plays it, in Core Haptics' terms: a
// transient tap, or a continuous buzz of a duration. Times in seconds from
// the start of the haptic; intensity and sharpness from 0 to 1.
struct HapticPulse {
  float atSeconds = 0.0F;
  float intensity = 0.0F;
  float sharpness = 0.0F;
  // 0 for a transient.
  float durationSeconds = 0.0F;

  [[nodiscard]] constexpr bool continuous() const noexcept { return durationSeconds > 0.0F; }
};

// What each kind feels like. Strong enough to be felt through a phone held
// in two hands over the sound of the game: a flipper is a firm tick, a hit
// a sharper knock, a drain a heavy buzz of a quarter second, a jackpot a
// rising triple tap with a buzz under it.
[[nodiscard]] std::span<const HapticPulse> hapticPattern(Haptic kind) noexcept;

// The TEST HAPTICS button of the settings page: every kind once, lightest
// first, kGapMs apart, so the player can feel each on the device. Ticked
// once a frame; a press while one is running starts it again.
class HapticTest {
 public:
  static constexpr std::uint64_t kGapMs = 700;
  static constexpr std::array<Haptic, 4> kOrder = {Haptic::Flipper, Haptic::Hit, Haptic::Drain,
                                                   Haptic::Jackpot};

  void start(std::uint64_t nowMs) noexcept;
  [[nodiscard]] bool running() const noexcept { return next_ < kOrder.size(); }
  // The kind to play now, if one is due. At most one a call.
  [[nodiscard]] std::optional<Haptic> take(std::uint64_t nowMs) noexcept;

 private:
  std::uint64_t startMs_ = 0;
  std::size_t next_ = kOrder.size();
};

// When to ask for a store review: only once the player has finished
// kReviewMinGames games, at most once for each version of the game, and
// never during a game: the caller asks only from the menu, after a game in
// this run has ended. The system decides whether anything is shown.
inline constexpr std::uint32_t kReviewMinGames = 5;
[[nodiscard]] constexpr bool reviewDue(const Settings& settings, std::string_view version,
                                       bool gameEndedThisRun) noexcept {
  return gameEndedThisRun && settings.gamesFinished >= kReviewMinGames &&
         isSettingsVersion(version) && settings.reviewAskedVersion != version;
}

}  // namespace pinball
