#pragma once

#include <cstdint>

#include "core/Input.hpp"

namespace pinball {

// All game state and rules. Has no dependency on SDL so it can be driven and
// verified headless. The platform layer feeds it input and draws its state.
class Game {
 public:
  // The simulation always advances by exactly this much. A fixed step keeps
  // physics deterministic, so the caller cannot choose it.
  static constexpr double kStepSeconds = 1.0 / 120.0;

  // Advances the simulation by one fixed step.
  void step(const InputState& input);

  // Derived from the step count so it cannot drift from it.
  double simTime() const noexcept { return static_cast<double>(stepCount_) * kStepSeconds; }
  std::uint64_t stepCount() const noexcept { return stepCount_; }

 private:
  std::uint64_t stepCount_ = 0;
  InputState previousInput_;
};

}  // namespace pinball
