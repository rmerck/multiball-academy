#pragma once

#include <SDL3/SDL.h>

#include <string_view>

#include "core/ScreenLayout.hpp"

// The on-screen controls of the touch idiom: the buttons and the flipper
// pads, drawn from the rectangles in screen::Layout. They are drawn by
// TableRenderer::drawTouchOverlay (platform/TableRenderer.hpp), which owns
// the textures. The plain button the dialogs share is declared here.
namespace pinball {

// A button face drawn without art: a chunky bordered slab in the given
// colour with a drop shadow, pushed in and filled when pressed.
void drawPlainButton(SDL_Renderer* renderer, const screen::Rect& face, SDL_Color tint,
                     bool pressed);

// drawPlainButton with a caption centred on it, at the largest whole text
// size from `largest` down to `smallest` that fits the face. Every caption
// there is fits at 3.
void drawCaptionButton(SDL_Renderer* renderer, const screen::Rect& face, std::string_view caption,
                       SDL_Color tint, bool pressed, float largest = 4.0F,
                       float smallest = 3.0F);

// The same dimming the paused table gets, over everything on the canvas but
// the table: the status display, the deck and the backdrop.
void dimAround(SDL_Renderer* renderer, const screen::Layout& layout);

}  // namespace pinball
