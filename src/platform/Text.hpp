#pragma once

#include <SDL3/SDL.h>

#include <string_view>

namespace pinball {

enum class TextAlign { Left, Center, Right };

// Width in canvas pixels of text drawn with the given font pixel size.
float textWidth(std::string_view text, float pixelSize) noexcept;

// Draws text in the built-in 5x7 font. x is the left edge, centre or right
// edge according to align; y is the top. Lower case is shown as upper case
// and characters outside the font as '?'. At most kMaxTextLength characters
// are drawn.
void drawText(SDL_Renderer* renderer, std::string_view text, float x, float y, float pixelSize,
              SDL_Color color, TextAlign align = TextAlign::Left);

inline constexpr std::size_t kMaxTextLength = 96;

}  // namespace pinball
