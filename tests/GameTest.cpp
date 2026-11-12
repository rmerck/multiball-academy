#include <cmath>

#include "Check.hpp"
#include "Suites.hpp"
#include "core/Game.hpp"

void testGame() {
  using pinball::Game;
  using pinball::InputState;

  {  // Time is derived from the step count and does not drift.
    Game game;
    const InputState input;
    for (int i = 0; i < 120 * 3600; ++i) {
      game.step(input);
    }
    CHECK(game.stepCount() == 120U * 3600U);
    CHECK(std::fabs(game.simTime() - 3600.0) < 1e-9);
  }
}
