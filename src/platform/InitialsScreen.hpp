#pragma once

#include <SDL3/SDL.h>

#include <cstdint>

#include "core/InitialsEntry.hpp"

namespace pinball {

// Draws the initials prompt shown when a finished game makes the high score
// table. cursorLit makes the marker under the current letter blink.
void drawInitialsEntry(SDL_Renderer* renderer, const InitialsEntry& entry, std::uint64_t score,
                       bool cursorLit);

}  // namespace pinball
