// The keyboard: the controls, the function keys and typing at the initials
// prompt. A hardware keyboard works the same on every system.
#include "platform/App.hpp"

#include <cstddef>
#include <span>

namespace pinball {

namespace {

// How long the initials prompt ignores keys after it appears.
constexpr Uint64 kEntryGuardMs = 700;

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
  latchKey(key.scancode);
  if (key.key == SDLK_F1) {
    aboutOpen_ = !aboutOpen_;
    latched_ = InputState{};
  } else if (key.key == SDLK_ESCAPE && aboutOpen_) {
    aboutOpen_ = false;
    latched_ = InputState{};
  } else if (key.key == SDLK_F3 && !aboutOpen_) {
    // Not behind the About screen, where the change could not be seen.
    paused_ = !paused_;
    // Nothing pressed during a pause carries into play.
    latched_ = InputState{};
  } else if (key.key == SDLK_F4) {
    toggleFullscreen();
  } else if (key.scancode == SDL_SCANCODE_M) {
    mixer_.setMuted(!mixer_.muted());
  } else if (key.scancode == SDL_SCANCODE_N) {
    musicOn_ = !musicOn_;
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
  int keyCount = 0;
  const bool* const state = SDL_GetKeyboardState(&keyCount);
  const std::span<const bool> keys(
      state, state != nullptr && keyCount > 0 ? static_cast<std::size_t>(keyCount) : 0);
  input_ = InputState{};
  for (const SDL_Scancode scancode : kControlKeys) {
    bool InputState::* const control = controlFor(scancode);
    const auto index = static_cast<std::size_t>(scancode);
    const bool held = index < keys.size() && keys[index];
    // Held now, or pressed and released since the last simulation step.
    input_.*control = held || latched_.*control;
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
