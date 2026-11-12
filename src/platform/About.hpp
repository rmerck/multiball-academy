#pragma once

#include <SDL3/SDL.h>

#include "core/HighScores.hpp"

namespace pinball {

// Draws the About screen over the whole canvas: who made the game, where to
// find them, and the high score table.
void drawAbout(SDL_Renderer* renderer, const HighScores& scores);

}  // namespace pinball
