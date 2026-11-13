// What happens to the game when the player's attention goes elsewhere: the
// window loses focus or is minimised and, where the system has such a thing,
// the application goes to the background and returns.
#include "platform/App.hpp"

#include "platform/ios/IosSystem.hpp"

namespace pinball {

void App::handleFocusChange() {
  // Do not let the ball drain while the player is elsewhere. The window's
  // own state decides, not the event, so a restore without focus stays
  // paused. Timed automated runs have no player and never auto-pause.
  const SDL_WindowFlags flags = SDL_GetWindowFlags(window_.get());
  const bool away = (flags & SDL_WINDOW_INPUT_FOCUS) == 0 || (flags & SDL_WINDOW_MINIMIZED) != 0;
  if (layout_.idiom == screen::Idiom::Touch) {
    // On a touch screen the game does not start again by itself when the
    // player returns: the hands are not in place yet.
    if (away) {
      interrupt();
    }
    return;
  }
  autoPaused_ = away && !options_.frameLimit;
  latched_ = InputState{};
}

void App::interrupt() {
  if (!options_.frameLimit) {
    if (layout_.idiom == screen::Idiom::Touch) {
      // The player's own pause, with its menu, so that it takes the player
      // to end it.
      paused_ = true;
      interrupted_ = true;
      confirmNewGame_ = false;
    } else {
      autoPaused_ = true;
    }
  }
  latched_ = InputState{};
  input_ = InputState{};
  touch_.cancelAll();
  overlayTaps_.cancel();
}

void App::handleLifecycle(Uint32 type) {
  switch (type) {
    case SDL_EVENT_WILL_ENTER_BACKGROUND:
      // Also sent when something only covers the game for a moment (the
      // system's control panel, a call, the application switcher), and then
      // followed straight by "did enter foreground".
      interrupt();
      mixer_.setMusicPlaying(false);
      if (audio_) {
        // No frames will run to keep it supplied.
        audio_->suspend();
      }
      break;
    case SDL_EVENT_DID_ENTER_BACKGROUND:
      // The system usually ends a suspended application without another
      // word, so this is the last moment a score waiting at the initials
      // prompt can be kept: under the letters shown, as when quitting at
      // the prompt. One small file. A game in progress is not saved; it
      // lives on in memory if the application does.
      commitInitials();
      backgrounded_ = true;
      break;
    case SDL_EVENT_WILL_ENTER_FOREGROUND:
      backgrounded_ = false;
      break;
    case SDL_EVENT_DID_ENTER_FOREGROUND:
      backgrounded_ = false;
      if (audio_) {
        audio_->resume();
      } else {
        // No device opened before; there may be one now.
        audioRetryWanted_ = true;
      }
      reduceMotion_ = ios::reduceMotionEnabled();
      // The time away is not played back as a burst of catch-up steps
      // (a timed run is not paused, so the pause does not see to it).
      (void)clock_.simulated(0.0, true);
      break;
    case SDL_EVENT_TERMINATING:
      // The application is being ended. What finish() would do, now: there
      // may be no later.
      interrupt();
      commitInitials();
      if (audio_) {
        audio_->suspend();
      }
      running_ = false;
      break;
    case SDL_EVENT_LOW_MEMORY:
      // Nothing to give back: there are no caches, and the bytes of the
      // assets were freed once their textures were made.
      SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "The system is short of memory");
      break;
    default:
      break;
  }
}

}  // namespace pinball
