#pragma once

namespace pinball {

// Player intent for one simulation step, independent of any input device.
struct InputState {
  bool leftFlipper = false;
  bool rightFlipper = false;
  bool plunger = false;
  // Edge-triggered: true only for the step in which the key went down.
  bool nudgeLeft = false;
  bool nudgeRight = false;
  bool nudgeUp = false;
  bool newGame = false;
};

}  // namespace pinball
