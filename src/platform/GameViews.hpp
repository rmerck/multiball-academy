#pragma once

#include <array>
#include <cstddef>
#include <span>

#include "core/Game.hpp"
#include "core/TouchControls.hpp"

// What the drawing code asks of the game where the game's own interface is
// still on its way: several balls at once, and the multiball rules. Each
// function uses the call the game is to have (Game::balls,
// Game::ballAwaitingLaunch, the multiball queries of Rules) when the type it
// is given has it, and today's single ball when it does not. They are
// templates so that the choice is made by the compiler for the type in hand;
// no SDL, so they are unit tested with stand-ins for both kinds of game.
namespace pinball {

// More than any table holds; a game that offered more would have the rest
// left out of the picture, never written past the end.
inline constexpr std::size_t kMaxDrawnBalls = 8;

struct DrawnBalls {
  std::array<BallView, kMaxDrawnBalls> items{};
  std::size_t count = 0;

  [[nodiscard]] std::span<const BallView> span() const& noexcept {
    return std::span<const BallView>(items).first(count);
  }
  std::span<const BallView> span() const&& = delete;
  // Adds one more, if there is room.
  void add(const BallView& ball) noexcept {
    if (count < items.size()) {
      items[count++] = ball;
    }
  }
};

// Every ball to draw, as it is alpha of a step after the one before.
template <typename GameType>
[[nodiscard]] DrawnBalls ballsToDraw(const GameType& game, float alpha) {
  DrawnBalls drawn;
  if constexpr (requires { game.balls(alpha); }) {
    for (const BallView& ball : game.balls(alpha)) {
      drawn.add(ball);
    }
  } else {
    drawn.add(game.ball(alpha));
  }
  return drawn;
}

// What the centre button and its label depend on. Until the game can say
// that a ball waits in the launch lane, a game that is not over and has no
// ball in play is taken to have one there.
template <typename GameType>
[[nodiscard]] TouchControls::Context touchContext(const GameType& game) {
  const bool over = game.rules().gameOver();
  if constexpr (requires { game.ballAwaitingLaunch(); }) {
    return {.ballAwaitingLaunch = !over && game.ballAwaitingLaunch(), .gameOver = over};
  } else {
    return {.ballAwaitingLaunch = !over && !game.rules().ballInPlay(), .gameOver = over};
  }
}

// The multiball part of the status display.
struct MultiballView {
  // The rules have multiball. When false the display leaves out its lock
  // and ball-count indicators altogether.
  bool known = false;
  int ballsLive = 1;
  int locksHeld = 0;
  bool lockLit = false;
  bool running = false;
  bool jackpotLit = false;
  bool superJackpotLit = false;
};

inline constexpr int kLocksForMultiball = 2;

template <typename RulesType>
[[nodiscard]] MultiballView multiballView(const RulesType& rules) {
  MultiballView view;
  if constexpr (requires {
                  rules.ballsLive();
                  rules.locksHeld();
                  rules.lockLit();
                  rules.multiball();
                  rules.multiballJackpotLit();
                  rules.superJackpotLit();
                }) {
    view.known = true;
    view.ballsLive = static_cast<int>(rules.ballsLive());
    view.locksHeld = static_cast<int>(rules.locksHeld());
    view.lockLit = rules.lockLit();
    // The first value of the mode is "none".
    view.running = static_cast<int>(rules.multiball()) != 0;
    view.jackpotLit = rules.multiballJackpotLit();
    view.superJackpotLit = rules.superJackpotLit();
  }
  return view;
}

}  // namespace pinball
