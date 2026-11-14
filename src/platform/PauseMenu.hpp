#pragma once

#include <SDL3/SDL.h>

#include "core/ScreenLayout.hpp"
#include "platform/TouchOverlay.hpp"

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

// Draws the pause display over a dimmed table. A desktop gets the word
// PAUSED over the playfield and the key that resumes. A touch screen gets
// the menu of screen::menuLayout (resume, new game, sound, music, scores and
// about) with everything round the table dimmed as well; after an
// interruption it gets the word and TAP TO RESUME instead, and any tap
// resumes. The menu is drawn on the dialog card with the title and the
// buttons of art (TableRenderer::overlayArt) where there is any.
void drawPauseMenu(SDL_Renderer* renderer, const screen::Layout& layout, const MenuState& state,
                   const OverlayArt& art = {});

}  // namespace pinball
