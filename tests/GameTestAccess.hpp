#pragma once

#include <cstddef>

#include "core/Game.hpp"
#include "core/Vec2.hpp"

// The seam Game leaves for tests: puts balls somewhere by hand and reads
// what the game does not show a player.
struct pinball::GameTestAccess {
  static void placeBall(Game& game, Vec2 position) {
    game.physics_.resetBall(position, {0.0F, 0.0F});
  }
  // As if the ball had just left the launch lane.
  static void putInPlay(Game& game, std::size_t ball = 0) {
    if (ball < Game::kMaxBalls) {
      game.balls_[ball].inPlay = true;
    }
    game.enterPlay();
  }
  // The same for any ball, with a velocity.
  static void placeBall(Game& game, std::size_t ball, Vec2 position, Vec2 velocity = {}) {
    game.physics_.resetBall(ball, position, velocity);
  }
  // Puts one more ball on the table. Returns its id, or Game::kMaxBalls.
  static std::size_t addBall(Game& game, Vec2 position, Vec2 velocity = {}) {
    return game.addBall(position, velocity);
  }
  static bool live(const Game& game, std::size_t ball) { return game.physics_.ballActive(ball); }
  static bool held(const Game& game, std::size_t ball) { return game.physics_.ballHeld(ball); }
  static Vec2 velocity(const Game& game, std::size_t ball) {
    return game.physics_.ballVelocity(ball);
  }
  static Vec2 position(const Game& game, std::size_t ball) {
    return game.physics_.ballPosition(ball);
  }
  // Adds to a free ball's velocity, as a nudge does.
  static void pushBall(Game& game, std::size_t ball, Vec2 velocityChange) {
    game.physics_.pushBall(ball, velocityChange);
  }
};
