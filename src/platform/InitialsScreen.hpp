#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <cstdint>

#include "core/InitialsEntry.hpp"
#include "core/ScreenLayout.hpp"
#include "generated/ArtPalette.hpp"
#include "platform/TouchOverlay.hpp"

namespace pinball {

// The colours of the large letters: cream faces shading to yellow (the
// letter being set) or aqua, edged in pink or violet. The dialog's art has
// none of them exactly, so the layout check finds the letters by them.
inline constexpr std::array<SDL_Color, 5> kInitialsLettering{{
    {art::kCream.r, art::kCream.g, art::kCream.b, 255},
    {art::kYellow.r, art::kYellow.g, art::kYellow.b, 255},
    {art::kAqua.r, art::kAqua.g, art::kAqua.b, 255},
    {art::kPink.r, art::kPink.g, art::kPink.b, 255},
    {art::kViolet.r, art::kViolet.g, art::kViolet.b, 255},
}};

// Draws the initials prompt shown when a finished game makes the high score
// table, in the box screen::initialsLayout gives for the layout, over a
// dimmed canvas. cursorLit makes the marker under the current letter blink.
// A desktop is told which keys to use; a touch screen gets the controls of
// screen::initialsLayout: an arrow above and below each letter, and OK.
// The prompt is drawn on the dialog card, with buttons of art, where there
// is any (TableRenderer::overlayArt).
void drawInitialsEntry(SDL_Renderer* renderer, const InitialsEntry& entry, std::uint64_t score,
                       bool cursorLit, const screen::Layout& layout, const OverlayArt& art = {});

}  // namespace pinball
