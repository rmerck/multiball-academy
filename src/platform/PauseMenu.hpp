#pragma once

#include <SDL3/SDL.h>

#include "core/ScreenLayout.hpp"

namespace pinball {

// What the pause display shows.
struct MenuState {
  bool muted;
  bool musicOn;
  // NEW GAME was chosen once and waits for a second tap.
  bool confirmNewGame;
  // The game stopped by itself (the window lost focus, the application went
  // to the background) and was not paused by the player.
  bool afterInterruption;
};

// Draws the pause display over a dimmed table.
//
// Only the desktop's exists so far, whatever the idiom: the word PAUSED over
// the playfield and how to resume. The menu of buttons a touch screen needs
// (screen::menuLayout) is not drawn yet.
void drawPauseMenu(SDL_Renderer* renderer, const screen::Layout& layout, const MenuState& state);

}  // namespace pinball
