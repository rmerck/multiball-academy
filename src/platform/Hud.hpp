#pragma once

#include <SDL3/SDL.h>

#include "core/Game.hpp"

namespace pinball {

// Draws the information panel: score, ball, awards and messages. The panel
// background art is drawn first by the caller's texture.
void drawHud(SDL_Renderer* renderer, SDL_Texture* panelArt, const Game& game);

}  // namespace pinball
