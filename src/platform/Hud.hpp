#pragma once

#include <SDL3/SDL.h>

#include <cstdint>

#include "core/Game.hpp"

namespace pinball {

// Things the panel shows that are not part of the game's own state.
struct HudExtras {
  std::uint64_t bestScore = 0;
  // The game that just ended made the high score table.
  bool newHighScore = false;
};

// Draws the information panel: score, ball, awards and messages. The panel
// background art is drawn first by the caller's texture.
void drawHud(SDL_Renderer* renderer, SDL_Texture* panelArt, const Game& game,
             const HudExtras& extras);

}  // namespace pinball
