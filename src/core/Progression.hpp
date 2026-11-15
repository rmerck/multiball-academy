#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

// Progression: the game climbs through five Altitude levels, FL100 to FL500
// (0 to 4 here), and the table gets busier, brighter and harder as it goes.
// The level is part of the game's state (Rules keeps it; it is saved with a
// game and restored with it) and it moves only on the rules' own events, so
// a replay climbs the same way, step for step.
//
// The table layer never decides what a level means for itself: it asks for
// the level's knobs (progression::knobs) and applies them. One table here
// for every knob, so tuning the climb is a change in one place.
namespace pinball::progression {

inline constexpr int kLevels = 5;
inline constexpr int kTopLevel = kLevels - 1;

[[nodiscard]] constexpr int clampLevel(int level) noexcept {
  return std::clamp(level, 0, kTopLevel);
}

// The flight level shown to the player: 100 at level 0 up to 500.
[[nodiscard]] constexpr int flightLevel(int level) noexcept {
  return (clampLevel(level) + 1) * 100;
}

// What each level asks of the table. Level 0 is the table as tuned for a
// beginner: every scale there is 1 and every extra is 0, so a game that
// never climbs plays exactly as the table's own tuning says.
struct Knobs {
  // Moving devices (a patrolling drone, a turntable, a moving target): their
  // speed as a share of their own.
  float deviceSpeed = 1.0F;
  // Timed windows the player is given (a course's clock, a lit letter's
  // time out, a pop-up's time up): their length as a share of their own.
  double timerScale = 1.0;
  // The ball saver's time as a share of its own.
  double ballSaveScale = 1.0;
  // Extra hits for anything that takes several (an armoured target).
  int extraHits = 0;
  // Guards that stand across a shot after it is used (a diverter, a post).
  bool guardsUp = false;
  // Banks and devices that reset themselves after a while.
  bool autoReset = false;
  // The light show: 0 at a new game, 1 at the top. How bright the table's
  // steady light is and how many lights run at once (core/LightShow).
  float lightEnergy = 0.0F;
};

inline constexpr std::array<Knobs, kLevels> kKnobs{{
    // FL100: the beginner's table.
    {.deviceSpeed = 1.00F,
     .timerScale = 1.00,
     .ballSaveScale = 1.00,
     .extraHits = 0,
     .guardsUp = false,
     .autoReset = false,
     .lightEnergy = 0.00F},
    // FL200: devices a little faster; banks reset.
    {.deviceSpeed = 1.15F,
     .timerScale = 0.95,
     .ballSaveScale = 0.85,
     .extraHits = 1,
     .guardsUp = false,
     .autoReset = true,
     .lightEnergy = 0.25F},
    // FL300: the guards come up.
    {.deviceSpeed = 1.30F,
     .timerScale = 0.90,
     .ballSaveScale = 0.70,
     .extraHits = 1,
     .guardsUp = true,
     .autoReset = true,
     .lightEnergy = 0.50F},
    // FL400.
    {.deviceSpeed = 1.45F,
     .timerScale = 0.85,
     .ballSaveScale = 0.55,
     .extraHits = 2,
     .guardsUp = true,
     .autoReset = true,
     .lightEnergy = 0.75F},
    // FL500: night flying.
    {.deviceSpeed = 1.60F,
     .timerScale = 0.80,
     .ballSaveScale = 0.40,
     .extraHits = 3,
     .guardsUp = true,
     .autoReset = true,
     .lightEnergy = 1.00F},
}};

[[nodiscard]] constexpr const Knobs& knobs(int level) noexcept {
  return kKnobs[static_cast<std::size_t>(clampLevel(level))];
}

// The knobs only ever tighten (or stay) from one level to the next, and
// level 0 is the plain tuning.
[[nodiscard]] constexpr bool knobsClimb() noexcept {
  const Knobs& base = kKnobs[0];
  if (base.deviceSpeed != 1.0F || base.timerScale != 1.0 || base.ballSaveScale != 1.0 ||
      base.extraHits != 0 || base.guardsUp || base.autoReset || base.lightEnergy != 0.0F) {
    return false;
  }
  for (std::size_t i = 1; i < kKnobs.size(); ++i) {
    const Knobs& a = kKnobs[i - 1];
    const Knobs& b = kKnobs[i];
    if (b.deviceSpeed < a.deviceSpeed || b.timerScale > a.timerScale ||
        b.ballSaveScale > a.ballSaveScale || b.extraHits < a.extraHits ||
        (a.guardsUp && !b.guardsUp) || (a.autoReset && !b.autoReset) ||
        b.lightEnergy < a.lightEnergy || b.timerScale <= 0.0 || b.ballSaveScale <= 0.0) {
      return false;
    }
  }
  return kKnobs[kTopLevel].lightEnergy == 1.0F;
}
static_assert(knobsClimb(), "progression::kKnobs must start at the plain tuning and only tighten");

// The level a game has reached. It only climbs within a game; a new game
// starts at its floor (0 unless a test or a bench sets one).
class Altitude {
 public:
  [[nodiscard]] int level() const noexcept { return level_; }
  [[nodiscard]] int floor() const noexcept { return floor_; }
  [[nodiscard]] const Knobs& knobs() const noexcept { return progression::knobs(level_); }
  [[nodiscard]] bool atTop() const noexcept { return level_ >= kTopLevel; }

  // A new game.
  void reset() noexcept { level_ = floor_; }
  // Up by this many levels (never past the top, never down): returns how
  // many it actually rose.
  int climb(int levels = 1) noexcept {
    const int before = level_;
    level_ = clampLevel(level_ + std::max(levels, 0));
    return level_ - before;
  }
  // Never below this, now and at every new game: for measuring each level's
  // table with the bots, and for the tools' stills.
  void setFloor(int level) noexcept {
    floor_ = clampLevel(level);
    level_ = std::max(level_, floor_);
  }
  // For a restored game (the value is checked by the save's own validator).
  void restore(int level) noexcept { level_ = std::max(clampLevel(level), floor_); }

 private:
  int level_ = 0;
  int floor_ = 0;
};

}  // namespace pinball::progression
