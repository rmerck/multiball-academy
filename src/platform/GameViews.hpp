#pragma once

#include <array>
#include <cstddef>
#include <span>

#include "core/Game.hpp"
#include "core/TouchControls.hpp"

// What the drawing code asks of the game. The balls come straight from
// Game::balls. Two things the rules cannot say yet are asked through
// templates that use the call when the type in hand has it and a stand-in
// until then: whether a ball waits in the launch lane, and the locks and
// lit jackpots of multiball. No SDL, so they are unit tested with stand-ins
// for both kinds of game.
namespace pinball {

// The game's balls and a few more: the review tool adds some to look at a
// crowded table.
inline constexpr std::size_t kMaxDrawnBalls = Game::kMaxBalls + 4;

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

// Every ball to draw, as it is alpha of a step after the one before, and
// after them the extra ones.
[[nodiscard]] inline DrawnBalls ballsToDraw(const Game& game, float alpha,
                                            std::span<const BallView> extra = {}) {
  DrawnBalls drawn;
  for (const BallView& ball : game.balls(alpha)) {
    drawn.add(ball);
  }
  for (const BallView& ball : extra) {
    drawn.add(ball);
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

// For a game whose rules do not say how many locks start a multiball
// (Rules::locksNeeded): the number the design gives. Used for nothing but
// the "LOCKS n/m" line.
inline constexpr int kPlaceholderLocksNeeded = 2;

// The multiball part of the status display.
struct MultiballView {
  // The rules have locks. When false the display leaves out its lock lamp
  // and lock count altogether.
  bool locksKnown = false;
  // Balls in play: more than one is a multiball. From the rules when they
  // count them (a ball locked in the Hangar is on the table and not in
  // play), otherwise the balls on the table.
  int ballsLive = 1;
  int locksHeld = 0;
  int locksNeeded = kPlaceholderLocksNeeded;
  bool lockLit = false;
  bool jackpotLit = false;
  bool superJackpotLit = false;

  [[nodiscard]] bool running() const noexcept { return ballsLive > 1; }
};

template <typename GameType>
[[nodiscard]] MultiballView multiballView(const GameType& game) {
  MultiballView view;
  view.ballsLive = static_cast<int>(game.ballCount());
  const auto& rules = game.rules();
  if constexpr (requires { rules.ballsLive(); }) {
    view.ballsLive = static_cast<int>(rules.ballsLive());
  }
  if constexpr (requires {
                  rules.locksHeld();
                  rules.lockLit();
                }) {
    view.locksKnown = true;
    view.locksHeld = static_cast<int>(rules.locksHeld());
    view.lockLit = rules.lockLit();
  }
  if constexpr (requires { rules.locksNeeded(); }) {
    view.locksNeeded = static_cast<int>(rules.locksNeeded());
  }
  if constexpr (requires { rules.multiballJackpotLit(); }) {
    view.jackpotLit = rules.multiballJackpotLit();
  }
  if constexpr (requires { rules.superJackpotLit(); }) {
    view.superJackpotLit = rules.superJackpotLit();
  }
  return view;
}

}  // namespace pinball
