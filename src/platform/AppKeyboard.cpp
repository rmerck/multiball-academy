// The keyboard: the controls, the function keys and typing at the initials
// prompt. A hardware keyboard works the same on every system.
#include "platform/App.hpp"

#include <cstddef>
#include <span>

#include "platform/About.hpp"

namespace pinball {

namespace {

// Maps a physical key to the control it drives, or nullptr. Scancodes are
// key positions, so the controls sit under the same fingers on every
// keyboard layout. Positions match the original defaults.
bool InputState::* controlFor(SDL_Scancode scancode) noexcept {
  switch (scancode) {
    case SDL_SCANCODE_Z:
      return &InputState::leftFlipper;
    case SDL_SCANCODE_SLASH:
      return &InputState::rightFlipper;
    case SDL_SCANCODE_SPACE:
      return &InputState::plunger;
    case SDL_SCANCODE_X:
      return &InputState::nudgeLeft;
    case SDL_SCANCODE_PERIOD:
      return &InputState::nudgeRight;
    case SDL_SCANCODE_UP:
      return &InputState::nudgeUp;
    case SDL_SCANCODE_F2:
      return &InputState::newGame;
    default:
      return nullptr;
  }
}

constexpr SDL_Scancode kControlKeys[] = {
    SDL_SCANCODE_Z, SDL_SCANCODE_SLASH, SDL_SCANCODE_SPACE, SDL_SCANCODE_X,
    SDL_SCANCODE_PERIOD, SDL_SCANCODE_UP, SDL_SCANCODE_F2};

}  // namespace

void App::handleKeyDown(const SDL_KeyboardEvent& key) {
  if (key.repeat) {
    return;
  }
  if (entry_ && !aboutOpen_) {
    handleEntryKey(key.key, key.mod);
    return;
  }
  if (frontOpen_ && !aboutOpen_) {
    handleFrontKey(key);
    return;
  }
  if (guideOpen_) {
    // Any key goes on from the touch guide, except a shortcut.
    if ((key.mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL | SDL_KMOD_ALT)) == 0) {
      dismissGuide();
    }
    return;
  }
  latchKey(key.scancode);
  if (key.key == SDLK_F1) {
    aboutOpen_ = !aboutOpen_;
    aboutPage_ = AboutState{};
    latched_ = InputState{};
  } else if (aboutOpen_ && (key.key == SDLK_LEFT || key.key == SDLK_RIGHT)) {
    // The arrows turn the pages: scores, about, then each licence page.
    aboutPage_ = turnAboutPage(aboutPage_, key.key == SDLK_RIGHT, licencePageCount(layout_));
  } else if (key.key == SDLK_ESCAPE && aboutOpen_) {
    aboutOpen_ = false;
    latched_ = InputState{};
  } else if (key.key == SDLK_ESCAPE && paused_) {
    // The main menu, from the pause. A game in progress is only left on a
    // second press.
    if (!confirmMainMenu_ && !game_.gameOver()) {
      confirmMainMenu_ = true;
      confirmNewGame_ = false;
    } else {
      goToMainMenu();
    }
  } else if (key.key == SDLK_F3 && !aboutOpen_) {
    // Not behind the About screen, where the change could not be seen.
    paused_ = !paused_;
    // Nothing pressed during a pause carries into play.
    latched_ = InputState{};
    if (!paused_) {
      // The touch screen's pause menu is gone with it.
      interrupted_ = false;
      confirmNewGame_ = false;
    }
    confirmMainMenu_ = false;
  } else if (key.key == SDLK_F4) {
    toggleFullscreen();
  } else if (key.scancode == SDL_SCANCODE_M) {
    setSound(mixer_.muted());
  } else if (key.scancode == SDL_SCANCODE_N) {
    setMusic(!musicOn_);
  }
}

void App::latchKey(SDL_Scancode scancode) {
  if (halted()) {
    return;
  }
  if (bool InputState::* const control = controlFor(scancode)) {
    latched_.*control = true;
  }
}

void App::readInput() {
  if (halted()) {
    input_ = InputState{};
    return;
  }
  // Touches that began since the last step join the latched presses.
  const InputState touched = touch_.takePressed();
  const InputState touching = touch_.held();
  int keyCount = 0;
  const bool* const state = SDL_GetKeyboardState(&keyCount);
  const std::span<const bool> keys(
      state, state != nullptr && keyCount > 0 ? static_cast<std::size_t>(keyCount) : 0);
  input_ = InputState{};
  for (const SDL_Scancode scancode : kControlKeys) {
    bool InputState::* const control = controlFor(scancode);
    const auto index = static_cast<std::size_t>(scancode);
    const bool held = index < keys.size() && keys[index];
    latched_.*control = latched_.*control || touched.*control;
    // Held now, by a key or a finger, or pressed and released since the
    // last simulation step.
    input_.*control = held || touching.*control || latched_.*control;
  }
}

void App::handleEntryKey(SDL_Keycode key, SDL_Keymod modifiers) {
  if (!entry_) {
    return;
  }
  // Shortcuts such as Cmd+H are not typing.
  if ((modifiers & (SDL_KMOD_GUI | SDL_KMOD_CTRL | SDL_KMOD_ALT)) != 0) {
    return;
  }
  // Not typing either, so it does not wait for the guard below.
  if (key == SDLK_F4) {
    toggleFullscreen();
    return;
  }
  // The prompt appears the moment the last ball drains, while fingers are
  // still on the flipper keys. Presses in the first moments are play, not
  // typing.
  if (SDL_GetTicks() - entryOpenedMs_ < kEntryGuardMs) {
    return;
  }
  // Letters are matched by what the key types, so they follow the layout.
  if (key >= SDLK_A && key <= SDLK_Z) {
    entry_->type(static_cast<char>('A' + static_cast<int>(key - SDLK_A)));
    return;
  }
  switch (key) {
    case SDLK_UP:
      entry_->next();
      break;
    case SDLK_DOWN:
      entry_->previous();
      break;
    case SDLK_LEFT:
    case SDLK_BACKSPACE:
      entry_->left();
      break;
    case SDLK_RIGHT:
      entry_->right();
      break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
      // Not Space: it is the plunger key, and too easy to hit by habit.
      if (entry_->confirm()) {
        commitInitials();
      }
      break;
    case SDLK_ESCAPE:
      // Accept the letters as they stand.
      commitInitials();
      break;
    default:
      break;
  }
}

}  // namespace pinball
