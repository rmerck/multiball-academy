#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "core/Input.hpp"
#include "core/Physics.hpp"
#include "core/Rules.hpp"
#include "core/Sound.hpp"
#include "core/TableLayout.hpp"
#include "core/Vec2.hpp"

namespace pinball {

// What the renderer needs to draw a ball.
struct BallView {
  Vec2 position;        // Playfield pixels.
  float angle = 0.0F;   // Radians.
  bool visible = true;  // False while a hole holds it out of sight.
  float scale = 1.0F;   // Above 1 while it rides the ramp over the table.
  // Which ball: its slot, 0 to kMaxBalls - 1. A ball keeps its id for as
  // long as it is on the table.
  std::uint8_t id = 0;
};

// The balls on the table, lowest id first. Fixed capacity and returned by
// value, so there is no allocation. Loop over it directly:
//   for (const BallView& ball : game.balls(alpha))
struct BallViews {
  std::array<BallView, table::kMaxBalls> items{};
  std::size_t count = 0;

  // A view of the balls present. It points into this object, so it is not
  // offered on a temporary: keep the BallViews in a variable first.
  [[nodiscard]] std::span<const BallView> span() const& noexcept {
    return std::span<const BallView>(items).first(count);
  }
  std::span<const BallView> span() const&& = delete;
  [[nodiscard]] auto begin() const noexcept { return span().begin(); }
  [[nodiscard]] auto end() const noexcept { return span().end(); }
  [[nodiscard]] std::size_t size() const noexcept { return count; }
  [[nodiscard]] bool empty() const noexcept { return count == 0; }
};

// What the renderer needs to draw one flipper.
struct FlipperView {
  Vec2 pivot;          // Playfield pixels.
  float angle = 0.0F;  // Radians, clockwise on screen from +x.
};

struct GameTestAccess;

// All game state and rules. Has no dependency on SDL so it can be driven and
// verified headless. The platform layer feeds it input and draws its state.
class Game {
 public:
  // The simulation always advances by exactly this much. A fixed step keeps
  // physics deterministic, so the caller cannot choose it.
  static constexpr double kStepSeconds = 1.0 / 120.0;
  // Most balls on the table at once.
  static constexpr std::size_t kMaxBalls = table::kMaxBalls;

  Game();

  // Advances the simulation by one fixed step.
  void step(const InputState& input);

  // Derived from the step count so it cannot drift from it.
  [[nodiscard]] double simTime() const noexcept {
    return static_cast<double>(stepCount_) * kStepSeconds;
  }
  [[nodiscard]] std::uint64_t stepCount() const noexcept { return stepCount_; }

  // The primary ball: the one with the lowest id. There is always one.
  [[nodiscard]] BallView ball() const;
  // Every ball on the table, including any in a hole or in the launch
  // lane, as ball(alpha) would give each of them.
  [[nodiscard]] BallViews balls(float alpha = 1.0F) const;
  // How many balls are on the table: 1 to kMaxBalls.
  [[nodiscard]] std::size_t ballCount() const noexcept { return physics_.activeBallCount(); }
  // Index 0 is the left flipper, 1 the right.
  [[nodiscard]] FlipperView flipper(std::size_t index) const;

  // For drawing between steps. alpha is the fraction of a step that has
  // passed since the latest one (FixedTimestep::alpha()): 0 gives the state
  // one step ago, 1 the latest state, and anything else lies between them.
  // Values outside [0, 1] are clamped; a value that is not a number counts
  // as 1. A ball that was moved by hand during the latest step (served,
  // returned by a hole) is given at its new place for every alpha, so it is
  // never drawn part-way along the jump.
  [[nodiscard]] BallView ball(float alpha) const;
  [[nodiscard]] FlipperView flipper(std::size_t index, float alpha) const;
  // How far the plunger is drawn back, in pixels.
  [[nodiscard]] float plungerPull() const noexcept { return physics_.plungerPull(); }
  [[nodiscard]] bool laneGateClosed() const noexcept { return physics_.laneGateClosed(); }
  // 1 just after a bumper was hit, fading to 0. Drives its light.
  [[nodiscard]] float bumperFlash(std::size_t index) const noexcept {
    return index < bumperFlash_.size() ? bumperFlash_[index] : 0.0F;
  }
  // Ball search: if a ball in play has not moved for this long, the table
  // gives it a shove, as a real machine fires its coils to free a stuck ball.
  static constexpr double kBallSearchSeconds = 4.0;
  // Times the ball search has fired.
  [[nodiscard]] std::uint32_t ballSearchCount() const noexcept { return ballSearchCount_; }

  // How long a hole keeps the ball, in seconds.
  static constexpr double kHoleHoldSeconds = 0.9;
  // With several balls a hole returns its ball only when no other ball is
  // within this distance of the exit, in pixels, and this long after the
  // last ball left by the same exit. A single ball never waits for either.
  static constexpr float kHoleExitClearance = 30.0F;
  static constexpr double kHoleReleaseSpacingSeconds = 0.3;

  // Times the ball has rolled over each rollover.
  [[nodiscard]] std::uint32_t rolloverCount(std::size_t index) const noexcept {
    return index < rolloverCounts_.size() ? rolloverCounts_[index] : 0;
  }
  // Times the ball has dropped into each hole.
  [[nodiscard]] std::uint32_t holeCount(std::size_t index) const noexcept {
    return index < holeCounts_.size() ? holeCounts_[index] : 0;
  }
  // Whether a hole is holding the primary ball right now.
  [[nodiscard]] bool ballHeld() const noexcept { return physics_.ballHeld(primaryBall()); }

  // Nudge: speed given to every ball, in pixels per second, by a shove from
  // the side and from the bottom of the table.
  static constexpr float kNudgeSideSpeed = 130.0F;
  static constexpr float kNudgeUpSpeed = 170.0F;
  // How far the playfield is drawn displaced right now, in pixels. Decays to
  // zero after a nudge.
  [[nodiscard]] Vec2 shake() const noexcept { return shake_; }

  // Sounds started by the most recent step.
  [[nodiscard]] SoundSet sounds() const noexcept { return sounds_; }

  // Score, lights, balls and messages.
  [[nodiscard]] const Rules& rules() const noexcept { return rules_; }
  [[nodiscard]] bool gameOver() const noexcept { return rules_.gameOver(); }
  [[nodiscard]] bool targetLit(std::size_t index) const noexcept { return rules_.targetLit(index); }
  // 1 just after a slingshot fired, fading to 0.
  [[nodiscard]] float slingshotFlash(std::size_t index) const noexcept {
    return index < slingshotFlash_.size() ? slingshotFlash_[index] : 0.0F;
  }
  // Total bumper hits this session.
  [[nodiscard]] std::uint32_t bumperHits() const noexcept { return bumperHits_; }
  // Number of balls lost down the drain so far, every ball counted.
  [[nodiscard]] std::uint32_t drainCount() const noexcept { return drainCount_; }

 private:
  // The tests force states that play cannot reach (a ball lost outside the
  // table) through this. It is defined only in the tests.
  friend struct GameTestAccess;

  // Leaves exactly one ball on the table, on the plunger, with the gate open.
  void serveBall();
  void snapshotForDrawing();
  void handleEvents();
  // Tells the rules, once, that the served ball is in play.
  void enterPlay();
  void startNewGame();
  void handleNudges(const InputState& input);
  void handleDrain();
  // Deals with balls that have drained or been lost outside the table.
  void handleLostBalls();
  // Takes one of several balls off the table for good.
  void removeBall(std::size_t slot);
  // Puts another ball on the table, in the lowest free slot. Returns the
  // slot, or kMaxBalls if every slot is in use or the input is not finite.
  std::size_t addBall(Vec2 position, Vec2 velocity);
  void updateLaneGate();
  void updateHeldBall(std::size_t slot);
  // Whether a ball held by this hole may leave now.
  [[nodiscard]] bool exitIsClear(std::size_t slot, std::size_t hole) const;
  // The flags say which flippers are actually raised: the keys count only
  // while the controls are live.
  void updateBallSearch(std::size_t slot, bool leftRaised, bool rightRaised);
  // The lowest live slot.
  [[nodiscard]] std::size_t primaryBall() const noexcept;
  [[nodiscard]] BallView ballView(std::size_t slot) const;
  [[nodiscard]] BallView ballView(std::size_t slot, float alpha) const;

  PhysicsWorld physics_;
  Rules rules_{kStepSeconds};
  std::uint64_t stepCount_ = 0;
  std::uint32_t drainCount_ = 0;
  InputState previousInput_;
  std::array<float, table::kBumpers.size()> bumperFlash_{};
  std::uint32_t bumperHits_ = 0;
  std::array<float, table::kSlingshots.size()> slingshotFlash_{};
  std::array<std::uint32_t, table::kRollovers.size()> rolloverCounts_{};
  std::array<std::uint32_t, table::kHoles.size()> holeCounts_{};
  // What the game keeps for each ball slot, beside the physics state.
  struct BallState {
    // The hole holding the ball, and for how many steps so far.
    std::size_t heldBy = 0;
    std::uint32_t heldSteps = 0;
    bool wasHeld = false;
    // Ball search state.
    Vec2 searchAnchor{};
    std::uint32_t stillSteps = 0;
    // State one step ago, for drawing between steps.
    BallView previous{};
    // The ball was placed somewhere during the latest step.
    bool placed = false;
    // Seen clear of the launch lane since it was put on the table.
    bool inPlay = false;
  };
  std::array<BallState, kMaxBalls> balls_{};
  // For each hole, one more than the step at which it last returned a ball;
  // 0 if it never has.
  std::array<std::uint64_t, table::kHoles.size()> lastReleaseStep_{};
  std::uint32_t ballSearchCount_ = 0;
  Vec2 shake_{};
  SoundSet sounds_ = 0;
  // Flipper angles one step ago, for drawing between steps.
  std::array<float, table::kFlippers.size()> previousFlipperAngles_{};
};

}  // namespace pinball
