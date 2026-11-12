#pragma once

#include <cstdint>

#include "core/Input.hpp"

namespace pinball {

// All game state and rules. Has no dependency on SDL so it can be driven and
// verified headless. The platform layer feeds it input and draws its state.
class Game {
 public:
  // Advances the simulation by one fixed step of dt seconds.
  void update(double dt, const InputState& input);

  double simTime() const noexcept { return simTime_; }
  std::uint64_t stepCount() const noexcept { return stepCount_; }

 private:
  double simTime_ = 0.0;
  std::uint64_t stepCount_ = 0;
};

}  // namespace pinball
