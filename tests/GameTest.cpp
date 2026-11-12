#include <cmath>
#include <limits>

#include "Check.hpp"
#include "core/Game.hpp"

void testGame() {
  using pinball::Game;
  using pinball::InputState;

  {  // Time and step count advance together.
    Game game;
    const InputState input;
    for (int i = 0; i < 120; ++i) {
      game.update(1.0 / 120.0, input);
    }
    CHECK(game.stepCount() == 120);
    CHECK(std::fabs(game.simTime() - 1.0) < 1e-9);
  }
  {  // Invalid steps are ignored.
    Game game;
    const InputState input;
    game.update(0.0, input);
    game.update(-1.0, input);
    game.update(std::numeric_limits<double>::quiet_NaN(), input);
    game.update(std::numeric_limits<double>::infinity(), input);
    CHECK(game.stepCount() == 0);
    CHECK(game.simTime() == 0.0);
  }
}
