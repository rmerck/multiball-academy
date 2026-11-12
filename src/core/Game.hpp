#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "core/Input.hpp"
#include "core/Physics.hpp"
#include "core/Rules.hpp"
#include "core/Sound.hpp"
#include "core/TableLayout.hpp"
#include "core/Vec2.hpp"

namespace pinball {

// What the renderer needs to draw the ball.
struct BallView {
  Vec2 position;        // Playfield pixels.
  float angle = 0.0F;   // Radians.
  bool visible = true;  // False while a hole holds it out of sight.
  float scale = 1.0F;   // Above 1 while it rides the ramp over the table.
};

// What the renderer needs to draw one flipper.
struct FlipperView {
  Vec2 pivot;          // Playfield pixels.
  float angle = 0.0F;  // Radians, clockwise on screen from +x.
};

// All game state and rules. Has no dependency on SDL so it can be driven and
// verified headless. The platform layer feeds it input and draws its state.
class Game {
 public:
  // The simulation always advances by exactly this much. A fixed step keeps
  // physics deterministic, so the caller cannot choose it.
  static constexpr double kStepSeconds = 1.0 / 120.0;

  Game();

  // Advances the simulation by one fixed step.
  void step(const InputState& input);

  // Derived from the step count so it cannot drift from it.
  double simTime() const noexcept { return static_cast<double>(stepCount_) * kStepSeconds; }
  std::uint64_t stepCount() const noexcept { return stepCount_; }

  BallView ball() const;
  // Index 0 is the left flipper, 1 the right.
  FlipperView flipper(std::size_t index) const;
  // How far the plunger is drawn back, in pixels.
  float plungerPull() const noexcept { return physics_.plungerPull(); }
  bool laneGateClosed() const noexcept { return physics_.laneGateClosed(); }
  // 1 just after a bumper was hit, fading to 0. Drives its light.
  float bumperFlash(std::size_t index) const noexcept {
    return index < bumperFlash_.size() ? bumperFlash_[index] : 0.0F;
  }
  // Ball search: if a ball in play has not moved for this long, the table
  // gives it a shove, as a real machine fires its coils to free a stuck ball.
  static constexpr double kBallSearchSeconds = 4.0;
  // Times the ball search has fired.
  std::uint32_t ballSearchCount() const noexcept { return ballSearchCount_; }

  // How long a hole keeps the ball, in seconds.
  static constexpr double kHoleHoldSeconds = 0.9;

  // Times the ball has rolled over each rollover.
  std::uint32_t rolloverCount(std::size_t index) const noexcept {
    return index < rolloverCounts_.size() ? rolloverCounts_[index] : 0;
  }
  // Times the ball has dropped into each hole.
  std::uint32_t holeCount(std::size_t index) const noexcept {
    return index < holeCounts_.size() ? holeCounts_[index] : 0;
  }
  // Whether a hole is holding the ball right now.
  bool ballHeld() const noexcept { return physics_.ballHeld(); }

  // Nudge: speed given to the ball, in pixels per second, by a shove from
  // the side and from the bottom of the table.
  static constexpr float kNudgeSideSpeed = 130.0F;
  static constexpr float kNudgeUpSpeed = 170.0F;
  // How far the playfield is drawn displaced right now, in pixels. Decays to
  // zero after a nudge.
  Vec2 shake() const noexcept { return shake_; }

  // Sounds started by the most recent step.
  SoundSet sounds() const noexcept { return sounds_; }

  // Score, lights, balls and messages.
  const Rules& rules() const noexcept { return rules_; }
  bool gameOver() const noexcept { return rules_.gameOver(); }
  bool targetLit(std::size_t index) const noexcept { return rules_.targetLit(index); }
  // 1 just after a slingshot fired, fading to 0.
  float slingshotFlash(std::size_t index) const noexcept {
    return index < slingshotFlash_.size() ? slingshotFlash_[index] : 0.0F;
  }
  // Total bumper hits this session.
  std::uint32_t bumperHits() const noexcept { return bumperHits_; }
  // Number of balls lost down the drain so far.
  std::uint32_t drainCount() const noexcept { return drainCount_; }

 private:
  void serveBall();
  void handleEvents();
  void startNewGame();
  void handleNudges(const InputState& input);
  void handleDrain();
  void updateHeldBall();
  void updateBallSearch(const InputState& input);

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
  // The hole holding the ball, and for how many steps so far.
  std::size_t heldBy_ = 0;
  std::uint32_t heldSteps_ = 0;
  bool wasHeld_ = false;
  // Ball search state.
  Vec2 searchAnchor_{};
  std::uint32_t stillSteps_ = 0;
  std::uint32_t ballSearchCount_ = 0;
  Vec2 shake_{};
  SoundSet sounds_ = 0;
};

}  // namespace pinball
