#pragma once

#include <algorithm>
#include <cstddef>
#include <string_view>

// Text broken into lines for the display's font, shared by the drawing and
// the layout tests so that what is checked to fit is what is drawn.
namespace pinball {

// Font pixels from one character to the next, gap included (platform/Font5x7).
inline constexpr float kGlyphAdvance = 6.0F;
// Line pitch, in font pixels: seven rows and three of space.
inline constexpr float kLinePitch = 10.0F;

// How many characters of text drawn at size fit across width. At least 1.
[[nodiscard]] constexpr std::size_t charsAcross(float width, float size) noexcept {
  const float advance = kGlyphAdvance * size;
  return width < advance ? 1 : static_cast<std::size_t>((width + size) / advance);
}

// Breaks text into lines of at most maxChars and hands each to fn with its
// number. A line feed ends a paragraph; an empty paragraph is an empty line.
// Lines break at spaces; a word longer than a line is cut. Returns the
// number of lines.
template <typename Fn>
std::size_t wrapText(std::string_view text, std::size_t maxChars, Fn&& fn) {
  maxChars = std::max<std::size_t>(maxChars, 1);
  std::size_t line = 0;
  while (true) {
    const std::size_t feed = text.find('\n');
    std::string_view paragraph = text.substr(0, feed);
    do {
      std::size_t take = paragraph.size();
      std::size_t skip = 0;
      if (take > maxChars) {
        const std::size_t space = paragraph.rfind(' ', maxChars);
        take = space == std::string_view::npos || space == 0 ? maxChars : space;
        skip = paragraph[take] == ' ' ? 1 : 0;
      }
      fn(line, paragraph.substr(0, take));
      ++line;
      paragraph.remove_prefix(take + skip);
    } while (!paragraph.empty());
    if (feed == std::string_view::npos) {
      return line;
    }
    text.remove_prefix(feed + 1);
  }
}

// The number of lines text takes, wrapped to maxChars.
[[nodiscard]] inline std::size_t lineCount(std::string_view text, std::size_t maxChars) {
  return wrapText(text, maxChars, [](std::size_t, std::string_view) {});
}

}  // namespace pinball
