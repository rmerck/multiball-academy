#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "core/Input.hpp"
#include "core/Physics.hpp"
#include "core/TableLayout.hpp"
#include "core/Vec2.hpp"

namespace pinball {

// What the renderer needs to draw the ball.
struct BallView {
  Vec2 position;  // Playfield pixels.
  float angle = 0.0F;  // Radians.
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
  // Whether a stand-up target is currently lit.
  bool targetLit(std::size_t index) const noexcept {
    return index < targetLit_.size() && targetLit_[index];
  }
  // How many times every target in a bank has been lit.
  std::uint32_t bankCompletions(table::TargetBank bank) const noexcept {
    return bankCompletions_[static_cast<std::size_t>(bank)];
  }
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
  void lightTarget(std::size_t index);

  PhysicsWorld physics_;
  std::uint64_t stepCount_ = 0;
  std::uint32_t drainCount_ = 0;
  InputState previousInput_;
  std::array<float, table::kBumpers.size()> bumperFlash_{};
  std::uint32_t bumperHits_ = 0;
  std::array<float, table::kSlingshots.size()> slingshotFlash_{};
  std::array<bool, table::kTargets.size()> targetLit_{};
  std::array<std::uint32_t, table::kTargetBankCount> bankCompletions_{};
};

}  // namespace pinball
