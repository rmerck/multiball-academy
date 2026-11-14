#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "core/Input.hpp"
#include "core/TableLayout.hpp"
#include "core/Vec2.hpp"

namespace pinball {

class Game;

// The hands of attract mode: plays a game of its own behind the title
// screen, as a cabinet does when nobody is at it. It sees only where the
// balls are and the fixed layout, as a player would: a ball coming down
// onto a resting flipper is flipped, a ball waiting in the lane is
// plunged, a finished game is started again after a pause. Deterministic:
// the same game and the same steps give the same presses. It drives a Game
// that is not the player's, which keeps no score anywhere.
class AttractPilot {
 public:
  // How long a finished attract game stands before the next starts.
  static constexpr int kRestartSteps = 360;
  // Steps of plunger pull, round through these for variety.
  static constexpr std::array<int, 4> kPulls{110, 96, 124, 104};
  // A ball is flipped when it comes this close above a resting bat, its
  // centre between kReachFrom and kReachTo along it from the pivot.
  static constexpr float kNear = 30.0F;
  static constexpr float kReachFrom = 30.0F;
  static constexpr float kReachTo = 104.0F;
  static constexpr int kHoldSteps = 14;
  static constexpr int kRestSteps = 10;

  // The input for the game's next step.
  [[nodiscard]] InputState next(const Game& game) noexcept;

 private:
  struct Side {
    int hold = 0;
    int rest = 0;
  };
  std::array<Side, 2> sides_{};
  int plunge_ = 0;       // Steps of pull still to go.
  int lanePause_ = 0;    // Steps before the next pull starts.
  int overSteps_ = 0;    // Steps since the game ended.
  std::size_t pulls_ = 0;
  bool newGame_ = false;
};

}  // namespace pinball
