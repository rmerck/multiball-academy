// What happens to the game when the player's attention goes elsewhere: the
// window loses focus or is minimised and, where the system has such a thing,
// the application goes to the background and returns.
#include "platform/App.hpp"

namespace pinball {

void App::handleFocusChange() {
  // Do not let the ball drain while the player is elsewhere. The window's
  // own state decides, not the event, so a restore without focus stays
  // paused. Timed automated runs have no player and never auto-pause.
  const SDL_WindowFlags flags = SDL_GetWindowFlags(window_.get());
  const bool away = (flags & SDL_WINDOW_INPUT_FOCUS) == 0 || (flags & SDL_WINDOW_MINIMIZED) != 0;
  autoPaused_ = away && !options_.frameLimit;
  latched_ = InputState{};
}

}  // namespace pinball
