#pragma once

#include <cstddef>
#include <cstdint>

#include "core/Input.hpp"
#include "core/Physics.hpp"
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
  // Number of balls lost down the drain so far.
  std::uint32_t drainCount() const noexcept { return drainCount_; }

  // Test access to the simulation.
  PhysicsWorld& physics() noexcept { return physics_; }

 private:
  void serveBall();

  PhysicsWorld physics_;
  std::uint64_t stepCount_ = 0;
  std::uint32_t drainCount_ = 0;
  InputState previousInput_;
};

}  // namespace pinball
