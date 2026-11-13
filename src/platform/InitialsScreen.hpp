#pragma once

#include <SDL3/SDL.h>

#include <cstdint>

#include "core/InitialsEntry.hpp"
#include "core/ScreenLayout.hpp"

namespace pinball {

// Draws the initials prompt shown when a finished game makes the high score
// table, in the box screen::initialsLayout gives for the layout, over a
// dimmed canvas. cursorLit makes the marker under the current letter blink.
void drawInitialsEntry(SDL_Renderer* renderer, const InitialsEntry& entry, std::uint64_t score,
                       bool cursorLit, const screen::Layout& layout);

}  // namespace pinball
