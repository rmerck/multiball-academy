#pragma once

#include <SDL3/SDL.h>

#include <cstddef>
#include <string_view>

namespace pinball {

enum class TextAlign { Left, Center, Right };

// Width in canvas pixels of text drawn with the given font pixel size.
[[nodiscard]] float textWidth(std::string_view text, float pixelSize) noexcept;

// Draws text in the built-in 5x7 font. x is the left edge, centre or right
// edge according to align; y is the top. Lower case is shown as upper case
// and characters outside the font as '?'. At most kMaxTextLength characters
// are drawn.
void drawText(SDL_Renderer* renderer, std::string_view text, float x, float y, float pixelSize,
              SDL_Color color, TextAlign align = TextAlign::Left);

// Display lettering for titles: a dark drop shadow, an offset edge in a
// second colour, and a face shaded from top to bottom down the seven rows of
// the font, like chrome arcade lettering. Arguments as for drawText.
void drawNeonText(SDL_Renderer* renderer, std::string_view text, float x, float y,
                  float pixelSize, SDL_Color top, SDL_Color bottom, SDL_Color edge,
                  TextAlign align = TextAlign::Left);

// The frame shared by the overlay dialogs: a glowing double neon border round
// a dark, faintly rastered face.
void drawNeonFrame(SDL_Renderer* renderer, const SDL_FRect& box);

// A decorative rule for the dialogs: three stacked bars centred on x that
// shorten and shift from laser yellow to hot pink, like the slats of the sun.
void drawNeonRule(SDL_Renderer* renderer, float centerX, float y, float halfWidth);

// A small neon sunset for the dialogs, drawn inside area and clipped to it:
// a slatted sun sinking behind a dark skyline, over a perspective grid.
void drawNeonSunset(SDL_Renderer* renderer, const SDL_FRect& area);

// A vertical zigzag of small squares between top and bottom, centred on x:
// the margin ornament from the panel.
void drawNeonZigzag(SDL_Renderer* renderer, float x, float top, float bottom, SDL_Color color);

inline constexpr std::size_t kMaxTextLength = 96;

}  // namespace pinball
