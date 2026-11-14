// What happens to the game when the player's attention goes elsewhere: the
// window loses focus or is minimised and, where the system has such a thing,
// the application goes to the background and returns.
#include "platform/App.hpp"

#include "platform/Motion.hpp"
#include "platform/ios/IosSystem.hpp"

namespace pinball {

void App::handleFocusChange(Uint32 type) {
  // Do not let the ball drain while the player is elsewhere. The window's
  // own state decides, not the event, so a restore without focus stays
  // paused. Timed automated runs have no player and never auto-pause.
  const SDL_WindowFlags flags = SDL_GetWindowFlags(window_.get());
  const bool away = (flags & SDL_WINDOW_INPUT_FOCUS) == 0 || (flags & SDL_WINDOW_MINIMIZED) != 0;
  if (layout_.idiom == screen::Idiom::Touch) {
    // On a touch screen the game does not start again by itself when the
    // player returns: the hands are not in place yet. And only losing the
    // window stops it: a window that is merely not focused yet when it is
    // first shown must not open the game on its pause menu.
    const bool lost = type == SDL_EVENT_WINDOW_FOCUS_LOST || type == SDL_EVENT_WINDOW_MINIMIZED;
    if (away && lost) {
      interrupt();
    }
    return;
  }
  autoPaused_ = away && !options_.frameLimit;
  latched_ = InputState{};
}

void App::interrupt() {
  // The title and the touch guide stand still already, and stay up.
  if (!options_.frameLimit && !frontOpen_ && !guideOpen_) {
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

void App::leaveForeground() {
  interrupt();
  mixer_.setMusicPlaying(false);
  audioSuspended_ = true;
  if (audio_) {
    // No frames will run to keep it supplied.
    audio_->suspend();
  }
}

void App::handleLifecycle(Uint32 type) {
  switch (type) {
    case SDL_EVENT_WILL_ENTER_BACKGROUND:
      // Also sent when something only covers the game for a moment (the
      // system's control panel, a call, the application switcher), and then
      // followed straight by "did enter foreground".
      leaveForeground();
      // The system may end a suspended application without another word:
      // the game in progress is written now, on this thread, while the
      // application still has the time (a few milliseconds; docs/RESUME.md).
      saveGameInProgress();
      break;
    case SDL_EVENT_DID_ENTER_BACKGROUND:
      // The system usually ends a suspended application without another
      // word, so this is the last moment a score waiting at the initials
      // prompt can be kept: under the letters shown, as when quitting at
      // the prompt. One small file. A game in progress is saved as well
      // (written once: it does not move while away). It also lives on in
      // memory if the application does.
      // Everything "will enter background" does, again: nothing promises
      // that it came first.
      leaveForeground();
      commitInitials();
      saveGameInProgress();
      backgrounded_ = true;
      break;
    case SDL_EVENT_WILL_ENTER_FOREGROUND:
      backgrounded_ = false;
      break;
    case SDL_EVENT_DID_ENTER_FOREGROUND:
      backgrounded_ = false;
      // The game is still in memory: the copy written on the way out goes.
      withdrawSavedGame();
      audioSuspended_ = false;
      if (audio_) {
        audio_->resume();
      } else {
        // No device opened before; there may be one now.
        audioRetryWanted_ = true;
      }
      reduceMotion_ = systemReduceMotion();
      // The time away is not played back as a burst of catch-up steps
      // (a timed run is not paused, so the pause does not see to it).
      (void)clock_.simulated(0.0, true);
      break;
    case SDL_EVENT_TERMINATING:
      // The application is being ended. What finish() would do, now: there
      // may be no later.
      leaveForeground();
      commitInitials();
      saveGameInProgress();
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

// --- The saved game ------------------------------------------------------------------
// One file, there only while the game it holds is not in memory: written on
// the way out, deleted on the way back in, when it is resumed, and when a
// game is begun or ended. So there is never a second copy of a game to go
// back to (docs/RESUME.md).

bool App::keepsSavedGame() const noexcept {
  return options_.keepScores && !options_.frameLimit && !options_.scene;
}

void App::saveGameInProgress() {
  // Behind the title is a game the player left by MAIN MENU, or none: not
  // one to offer again.
  if (!keepsSavedGame() || !game_.inProgress() || frontOpen_) {
    return;
  }
  if (wroteSave_ && savedAtStep_ == game_.stepCount()) {
    return;  // On disk as it stands.
  }
  const Uint64 started = SDL_GetTicksNS();
  const save::Snapshot snapshot = game_.snapshot();
  if (!save::valid(snapshot)) {
    // Not a state this build can resume: nothing is written. (The tests
    // play thousands of games without meeting one.)
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "The game in progress could not be saved");
    return;
  }
  // Whatever was offered to resume is overwritten by this game.
  savedGame_.reset();
  if (!saveStore_.write(snapshot)) {
    return;
  }
  saveOnDisk_ = true;
  wroteSave_ = true;
  savedAtStep_ = game_.stepCount();
  SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "Game saved in %.2f ms",
               static_cast<double>(SDL_GetTicksNS() - started) / 1e6);
}

void App::withdrawSavedGame() {
  if (!wroteSave_) {
    return;  // A save from before this run waits to be resumed.
  }
  if (saveStore_.remove()) {
    saveOnDisk_ = false;
  }
  wroteSave_ = false;
}

void App::followSavedGame() {
  if (!saveOnDisk_ && !savedGame_) {
    return;
  }
  if (game_.gamesBegun() != gamesBegunSeen_ || game_.gameOver()) {
    gamesBegunSeen_ = game_.gamesBegun();
    discardSavedGame();
  }
}

void App::discardSavedGame() {
  savedGame_.reset();
  if (keepsSavedGame() && saveStore_.remove()) {
    saveOnDisk_ = false;
  }
  wroteSave_ = false;
}

bool App::resumeSavedGame() {
  if (!savedGame_) {
    return false;
  }
  // Off the disk before it is on the table: a save is resumed once.
  if (!saveStore_.remove()) {
    return false;
  }
  saveOnDisk_ = false;
  wroteSave_ = false;
  const save::Snapshot snapshot = *savedGame_;
  savedGame_.reset();
  if (!game_.restore(snapshot)) {
    return false;
  }
  gamesBegunSeen_ = game_.gamesBegun();
  // The table holds a game that is not over and has no score to enter.
  entry_.reset();
  wasGameOver_ = false;
  newHighScore_ = false;
  // Paused, as it was left: the player's own pause, with its menu on a
  // touch screen, so that it takes the player to go on.
  paused_ = true;
  interrupted_ = layout_.idiom == screen::Idiom::Touch;
  confirmNewGame_ = false;
  latched_ = InputState{};
  input_ = InputState{};
  touch_.cancelAll();
  overlayTaps_.cancel();
  aboutOpen_ = false;
  // No catch-up for the time spent choosing.
  (void)clock_.simulated(0.0, true);
  redrawsDue_ = kRedrawsAfterEvent;
  return true;
}

}  // namespace pinball
