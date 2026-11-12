#include "core/Game.hpp"

namespace pinball {

void Game::step(const InputState& input) {
  ++stepCount_;
  previousInput_ = input;
}

}  // namespace pinball
