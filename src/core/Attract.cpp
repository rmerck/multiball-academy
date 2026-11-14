#include "core/Attract.hpp"

#include <algorithm>
#include <cmath>

#include "core/Game.hpp"

namespace pinball {

namespace {

// The tip of a flipper at rest: 30 degrees below level, written out so no
// library trigonometry decides a press.
Vec2 restingTip(std::size_t flipper) noexcept {
  const float side = flipper == table::kLeftFlipperIndex ? 1.0F : -1.0F;
  const Vec2 pivot = table::kFlippers[flipper].pivot;
  return {pivot.x + side * table::kFlipperLength * 0.8660254F,
          pivot.y + table::kFlipperLength * 0.5F};
}

// True if a ball is over the resting bat, within reach of a flip.
bool ballOverBat(const Game& game, std::size_t flipper) noexcept {
  const Vec2 pivot = table::kFlippers[flipper].pivot;
  const Vec2 tip = restingTip(flipper);
  const Vec2 d = tip - pivot;
  const float length = std::sqrt(d.x * d.x + d.y * d.y);
  const float mirror = flipper == table::kLeftFlipperIndex ? 1.0F : -1.0F;
  for (const BallView& ball : game.balls()) {
    if (!ball.visible || ball.locked || ball.raised) {
      continue;
    }
    const Vec2 p = ball.position;
    const float along = ((p.x - pivot.x) * d.x + (p.y - pivot.y) * d.y) / length;
    // Distance from the bat's line, positive on its upper side.
    const float above = ((p.x - pivot.x) * d.y - (p.y - pivot.y) * d.x) * mirror / length;
    if (along >= AttractPilot::kReachFrom && along <= AttractPilot::kReachTo && above > 0.0F &&
        above < AttractPilot::kNear) {
      return true;
    }
  }
  return false;
}

}  // namespace

InputState AttractPilot::next(const Game& game) noexcept {
  InputState input;
  if (game.gameOver()) {
    sides_ = {};
    plunge_ = 0;
    lanePause_ = 0;
    // A press of new game, one step long, once the pause is over.
    if (overSteps_ < kRestartSteps) {
      ++overSteps_;
    } else if (!newGame_) {
      newGame_ = true;
      input.newGame = true;
    }
    return input;
  }
  overSteps_ = 0;
  newGame_ = false;

  for (std::size_t i = 0; i < sides_.size(); ++i) {
    Side& side = sides_[i];
    bool press = false;
    if (side.hold > 0) {
      --side.hold;
      press = true;
      if (side.hold == 0) {
        side.rest = kRestSteps;
      }
    } else if (side.rest > 0) {
      --side.rest;
    } else if (ballOverBat(game, i)) {
      side.hold = kHoldSteps;
      press = true;
    }
    if (i == table::kLeftFlipperIndex) {
      input.leftFlipper = press;
    } else {
      input.rightFlipper = press;
    }
  }

  // A ball waiting in the lane: a moment's pause, then a pull and let go.
  if (plunge_ > 0) {
    --plunge_;
    input.plunger = plunge_ > 0;
  } else if (!game.rules().ballInPlay()) {
    if (lanePause_ < 90) {
      ++lanePause_;
    } else {
      lanePause_ = 0;
      plunge_ = kPulls[pulls_ % kPulls.size()];
      ++pulls_;
      input.plunger = true;
    }
  } else {
    lanePause_ = 0;
  }
  return input;
}

}  // namespace pinball
