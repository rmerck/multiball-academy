#pragma once

namespace pinball {

// Player intent, independent of any input device. Every field is a level:
// true for as long as the control is held. Game derives press edges itself,
// one step at a time, so a press is never repeated on a frame with several
// steps. The platform layer holds a level true until at least one step has
// seen it, so a press shorter than a frame is not lost either.
struct InputState {
  bool leftFlipper = false;
  bool rightFlipper = false;
  bool plunger = false;
  bool nudgeLeft = false;
  bool nudgeRight = false;
  bool nudgeUp = false;
  bool newGame = false;
};

}  // namespace pinball
