#include "Check.hpp"
#include "Suites.hpp"
#include "core/ScreenLayout.hpp"
#include "core/TouchControls.hpp"

// The touch mapper is not written yet. This pins down the one thing its
// placeholder promises: nothing is ever pressed.
void testTouchControls() {
  using pinball::TouchControls;
  const pinball::screen::Layout layout = pinball::screen::desktopLayout();
  const TouchControls::Context idle{.ballAwaitingLaunch = false, .gameOver = false};
  TouchControls controls;
  controls.handle({.id = 1, .phase = pinball::TouchPhase::Down, .x = 10.0F, .y = 900.0F}, layout,
                  idle);
  const pinball::InputState held = controls.held();
  CHECK(!held.leftFlipper && !held.rightFlipper && !held.plunger && !held.newGame);
  const pinball::InputState pressed = controls.takePressed();
  CHECK(!pressed.nudgeLeft && !pressed.nudgeRight && !pressed.nudgeUp);
  CHECK(!controls.takePauseRequest());
  controls.cancelAll();

  // The centre button's role follows the game.
  using Centre = TouchControls::View::Centre;
  CHECK(controls.view(idle).centreRole == Centre::Bump);
  CHECK(controls.view({.ballAwaitingLaunch = true, .gameOver = false}).centreRole ==
        Centre::Launch);
  CHECK(controls.view({.ballAwaitingLaunch = true, .gameOver = true}).centreRole == Centre::Play);
}
