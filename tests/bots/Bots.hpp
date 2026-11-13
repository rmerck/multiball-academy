#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "core/TableLayout.hpp"
#include "core/Vec2.hpp"

// The players that test the table (TABLE.md section 5.1). Each is
// deterministic: the same seed and the same balls give the same presses.
// They see only where the balls are and how they move, and the fixed layout;
// they know nothing of the rules. Header only, so that any test or tool can
// drive any game with them.
namespace pinball::bots {

// xorshift64. Not for anything but repeatable test input.
struct Rng {
  std::uint64_t state;
  explicit constexpr Rng(std::uint64_t seed) noexcept
      : state(seed * 0x9E3779B97F4A7C15ULL + 0x1234567ULL) {}
  constexpr std::uint64_t next() noexcept {
    state ^= state << 13U;
    state ^= state >> 7U;
    state ^= state << 17U;
    return state;
  }
  // A whole number uniform in [low, high].
  constexpr int range(int low, int high) noexcept {
    return low + static_cast<int>(next() % static_cast<std::uint64_t>(high - low + 1));
  }
  // Uniform in [0, 1).
  constexpr float unit() noexcept { return static_cast<float>(next() % 1000000ULL) / 1000000.0F; }
};

// One ball as a bot sees it.
struct BallSense {
  Vec2 position;
  Vec2 velocity;  // Pixels per second.
};
// The balls a player could see and reach: on the table and not in a hole.
struct Sense {
  std::array<BallSense, table::kMaxBalls> balls{};
  std::size_t count = 0;
};

struct Flippers {
  bool left = false;
  bool right = false;
};

class Bot {
 public:
  Bot() = default;
  Bot(const Bot&) = default;
  Bot& operator=(const Bot&) = default;
  virtual ~Bot() = default;
  // Called once per simulation step, before the step.
  virtual Flippers act(const Sense& sense) = 0;
};

// P0: never presses a flipper.
class IdleBot final : public Bot {
 public:
  Flippers act(const Sense& /*sense*/) override { return {}; }
};

// P1: each flipper on its own is pressed for 10 to 36 steps (83 to 300 ms),
// then released for 1 to 74: about two presses a second a side.
class RandomBot final : public Bot {
 public:
  explicit RandomBot(std::uint64_t seed) noexcept : rng_(seed) {}
  Flippers act(const Sense& /*sense*/) override {
    for (std::size_t i = 0; i < 2; ++i) {
      if (--timer_[i] <= 0) {
        down_[i] = !down_[i];
        timer_[i] = down_[i] ? rng_.range(10, 36) : rng_.range(1, 74);
      }
    }
    return {down_[0], down_[1]};
  }

 private:
  Rng rng_;
  std::array<int, 2> timer_{};
  std::array<bool, 2> down_{};
};

// Distance from a point to a segment, and how far along the segment (from
// a, in pixels, not clamped) the nearest point of its line lies.
struct SegmentReach {
  float distance = 0.0F;
  float along = 0.0F;
};
inline SegmentReach reachOf(Vec2 p, Vec2 a, Vec2 b) noexcept {
  const Vec2 d = b - a;
  const float lengthSquared = d.x * d.x + d.y * d.y;
  const float length = std::sqrt(lengthSquared);
  const float t = ((p.x - a.x) * d.x + (p.y - a.y) * d.y) / lengthSquared;
  const float clamped = std::clamp(t, 0.0F, 1.0F);
  const Vec2 nearest{a.x + clamped * d.x - p.x, a.y + clamped * d.y - p.y};
  return {std::sqrt(nearest.x * nearest.x + nearest.y * nearest.y), t * length};
}

// The tip of a flipper at rest. The rest angle is 30 degrees below level,
// written out so that no library trigonometry decides a press.
inline Vec2 restingTip(std::size_t flipper) noexcept {
  const float side = flipper == table::kLeftFlipperIndex ? 1.0F : -1.0F;
  const Vec2 pivot = table::kFlippers[flipper].pivot;
  return {pivot.x + side * table::kFlipperLength * 0.8660254F,
          pivot.y + table::kFlipperLength * 0.5F};
}

// P2, the reflex bot: the stand-in for an average player. It does not aim at
// shots. It aims at one moment, the ball's foot on the resting bat reaching
// the middle of the bat, and misses that moment by a timing error drawn once
// per approach, a whole number of steps uniform in [-error, +error]:
// negative is early, positive late. The error is the skill.
class ReflexBot final : public Bot {
 public:
  // The foot, from the pivot, that the bot aims for: mid-bat.
  static constexpr float kAim = 49.0F;
  // A ball is seen within this distance of the resting bat's centre line, on
  // its upper side, with its foot between 0 and kZoneLength from the pivot,
  // unless it is moving up the table faster than kRisingSpeed.
  static constexpr float kNear = 30.0F;
  static constexpr float kZoneLength = 110.0F;
  static constexpr float kRisingSpeed = 200.0F;
  // A ball watched this long without a press is flipped anyway.
  static constexpr int kPatienceSteps = 60;
  // After a press the flipper stays down this long.
  static constexpr int kRestSteps = 10;

  ReflexBot(std::uint64_t seed, int error) noexcept : rng_(seed ^ 0x5151ULL), error_(error) {}

  Flippers act(const Sense& sense) override {
    std::array<bool, 2> out{};
    for (std::size_t i = 0; i < 2; ++i) {
      Side& side = sides_[i];
      if (side.hold > 0) {
        --side.hold;
        out[i] = true;
        if (side.hold == 0) {
          side.rest = kRestSteps;
        }
        continue;
      }
      if (side.rest > 0) {
        --side.rest;
        side.seen = false;
        continue;
      }
      const float along = furthestFoot(sense, i);
      if (along < 0.0F) {
        side.seen = false;
        continue;
      }
      if (!side.seen) {
        side.seen = true;
        side.error = rng_.range(-error_, error_);
        side.late = 0;
        side.watched = 0;
        side.previousAlong = along;
      }
      const float rate = along - side.previousAlong;
      side.previousAlong = along;
      ++side.watched;
      bool press = false;
      if (along >= kAim) {
        // Arrived: now if early or on time, else wait out the lateness.
        press = side.late >= side.error;
        ++side.late;
      } else if (side.error < 0 && rate > 0.2F) {
        // Early by |error| steps, judged from the foot's speed.
        press = (kAim - along) / rate <= static_cast<float>(-side.error);
      }
      if (side.watched > kPatienceSteps) {
        press = true;
      }
      if (press) {
        side.hold = rng_.range(10, 18);
        out[i] = true;
        side.seen = false;
      }
    }
    return {out[0], out[1]};
  }

 private:
  // The foot of the ball furthest along the bat among those in the zone,
  // or -1 if there is none.
  static float furthestFoot(const Sense& sense, std::size_t flipper) noexcept {
    const Vec2 pivot = table::kFlippers[flipper].pivot;
    const Vec2 tip = restingTip(flipper);
    const float mirror = flipper == table::kLeftFlipperIndex ? 1.0F : -1.0F;
    float best = -1.0F;
    for (std::size_t b = 0; b < sense.count; ++b) {
      const Vec2 p = sense.balls[b].position;
      const SegmentReach reach = reachOf(p, pivot, tip);
      // Which side of the bat's line: positive above it.
      const float above =
          ((p.x - pivot.x) * (tip.y - pivot.y) - (p.y - pivot.y) * (tip.x - pivot.x)) * mirror;
      if (above > 0.0F && reach.distance < kNear && reach.along >= 0.0F &&
          reach.along <= kZoneLength && sense.balls[b].velocity.y > -kRisingSpeed) {
        best = std::max(best, reach.along);
      }
    }
    return best;
  }

  struct Side {
    int hold = 0;
    int rest = 0;
    bool seen = false;
    int error = 0;
    int late = 0;
    int watched = 0;
    float previousAlong = 0.0F;
  };
  Rng rng_;
  int error_;
  std::array<Side, 2> sides_{};
};

// P3, scripted aim, for reachability only: one flipper pressed at a given
// step for a given time, counted from when the bot was made. The table of
// (feed, press step) pairs that make each shot is found by search in the
// test itself: this is the hand that carries out one entry of it.
class ScriptedBot final : public Bot {
 public:
  static constexpr int kPressSteps = 30;
  // pressStep below zero never presses.
  ScriptedBot(std::size_t flipper, int pressStep, int pressSteps = kPressSteps) noexcept
      : left_(flipper == table::kLeftFlipperIndex),
        pressStep_(pressStep),
        pressSteps_(pressSteps) {}
  Flippers act(const Sense& /*sense*/) override {
    const bool press = pressStep_ >= 0 && step_ >= pressStep_ && step_ < pressStep_ + pressSteps_;
    ++step_;
    return {left_ && press, !left_ && press};
  }

 private:
  bool left_;
  int pressStep_;
  int pressSteps_;
  int step_ = 0;
};

}  // namespace pinball::bots
