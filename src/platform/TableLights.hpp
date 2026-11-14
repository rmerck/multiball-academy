#pragma once

#include <array>
#include <cstddef>

#include "core/Game.hpp"
#include "core/TableLayout.hpp"

// What the table's lights show: which lamps are lit and which bay doors are
// marked. The drawing asks nothing else about lamps, and asks it only here,
// so that when the table's real rules arrive this one function is rewired
// and the renderer is not touched. No SDL: unit tested in core_tests.
namespace pinball {

struct TableLights {
  // In LampId order (table::kLamps).
  std::array<bool, table::kLamps.size()> lamps{};
  // In table::kDropTargets order.
  std::array<bool, table::kDropTargets.size()> doorsMarked{};

  [[nodiscard]] constexpr bool lit(table::LampId id) const noexcept {
    const auto index = static_cast<std::size_t>(id);
    return index < lamps.size() && lamps[index];
  }
  constexpr void light(table::LampId id, bool on) noexcept {
    const auto index = static_cast<std::size_t>(id);
    if (index < lamps.size()) {
      lamps[index] = on;
    }
  }
};

// The bay a locked ball sits in: the nearer of the two.
[[nodiscard]] inline std::size_t lockBayOf(Vec2 position) noexcept {
  const auto distance = [position](Vec2 bay) noexcept {
    const Vec2 d = bay - position;
    return d.x * d.x + d.y * d.y;
  };
  return distance(table::kLockBays[1]) < distance(table::kLockBays[0]) ? 1 : 0;
}

// PROVISIONAL mapping, for the stub rules on main (Rules.hpp says which of
// its parts are provisional). What they can say is shown; every other lamp
// stays unlit, as baked into the table art, until the rules light it:
//   Lock1, Lock2  a ball in that bay (by bay, not by count)
//   Kickback      the kickback is armed
//   ShootAgain    the ball saver is on
//   ArrowHangar   the lock is lit
//   doors         Rules::doorMarked
[[nodiscard]] inline TableLights tableLights(const Game& game) noexcept {
  using table::LampId;
  TableLights lights;
  const Rules& rules = game.rules();
  for (const BallView& ball : game.balls()) {
    if (ball.locked) {
      lights.light(lockBayOf(ball.position) == 0 ? LampId::Lock1 : LampId::Lock2, true);
    }
  }
  lights.light(LampId::Kickback, game.kickbackArmed());
  lights.light(LampId::ShootAgain, rules.ballSaveActive());
  lights.light(LampId::ArrowHangar, rules.lockLit());
  for (std::size_t i = 0; i < lights.doorsMarked.size(); ++i) {
    lights.doorsMarked[i] = rules.doorMarked(i);
  }
  return lights;
}

}  // namespace pinball
