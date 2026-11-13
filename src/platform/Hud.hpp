#pragma once

#include <SDL3/SDL.h>

#include <cstdint>

#include "core/Game.hpp"
#include "core/ScreenLayout.hpp"

namespace pinball {

// Things the panel shows that are not part of the game's own state.
struct HudExtras {
  std::uint64_t bestScore = 0;
  // The game that just ended made the high score table.
  bool newHighScore = false;
};

// Draws the information panel at layout.hud: score, ball, awards and
// messages. The panel art, if given, is drawn under them. The keyboard hints
// are shown to the desktop idiom only.
//
// Only the Classic panel exists so far; a Stack layout gets the same panel
// at the strip's origin.
void drawHud(SDL_Renderer* renderer, SDL_Texture* panelArt, const Game& game,
             const screen::Layout& layout, const HudExtras& extras);

}  // namespace pinball
