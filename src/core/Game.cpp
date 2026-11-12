#include "core/Game.hpp"

#include <cmath>

namespace pinball {

void Game::update(double dt, const InputState& /*input*/) {
  if (!std::isfinite(dt) || dt <= 0.0) {
    return;
  }
  simTime_ += dt;
  ++stepCount_;
}

}  // namespace pinball
