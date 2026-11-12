#include "platform/Text.hpp"

#include <array>
#include <cstddef>

#include "platform/Font5x7.hpp"

namespace pinball {

namespace {

const std::array<std::uint8_t, 7>& glyphFor(char c) noexcept {
  if (c >= 'a' && c <= 'z') {
    c = static_cast<char>(c - 'a' + 'A');
  }
  if (c < font::kFirstChar || c > font::kLastChar) {
    c = '?';
  }
  return font::kGlyphs[static_cast<std::size_t>(c - font::kFirstChar)];
}

}  // namespace

float textWidth(std::string_view text, float pixelSize) noexcept {
  if (text.empty()) {
    return 0.0F;
  }
  const std::size_t count = text.size() < kMaxTextLength ? text.size() : kMaxTextLength;
  // The last character has no trailing gap.
  return (static_cast<float>(count) * static_cast<float>(font::kAdvance) - 1.0F) * pixelSize;
}

void drawText(SDL_Renderer* renderer, std::string_view text, float x, float y, float pixelSize,
              SDL_Color color, TextAlign align) {
  if (renderer == nullptr || text.empty() || !(pixelSize > 0.0F)) {
    return;
  }
  if (text.size() > kMaxTextLength) {
    text = text.substr(0, kMaxTextLength);
  }
  if (align == TextAlign::Center) {
    x -= textWidth(text, pixelSize) * 0.5F;
  } else if (align == TextAlign::Right) {
    x -= textWidth(text, pixelSize);
  }

  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);

  // Lit pixels are collected and drawn in batches.
  std::array<SDL_FRect, 256> rects{};
  std::size_t used = 0;
  const auto flush = [&] {
    if (used > 0) {
      SDL_RenderFillRects(renderer, rects.data(), static_cast<int>(used));
      used = 0;
    }
  };

  float penX = x;
  for (const char c : text) {
    const std::array<std::uint8_t, 7>& glyph = glyphFor(c);
    for (int row = 0; row < font::kGlyphHeight; ++row) {
      const std::uint8_t bits = glyph[static_cast<std::size_t>(row)];
      // Merge runs of lit pixels in a row into one rectangle.
      int col = 0;
      while (col < font::kGlyphWidth) {
        if ((bits & (0x10U >> static_cast<unsigned>(col))) == 0U) {
          ++col;
          continue;
        }
        const int start = col;
        while (col < font::kGlyphWidth && (bits & (0x10U >> static_cast<unsigned>(col))) != 0U) {
          ++col;
        }
        if (used == rects.size()) {
          flush();
        }
        rects[used++] = {penX + static_cast<float>(start) * pixelSize,
                         y + static_cast<float>(row) * pixelSize,
                         static_cast<float>(col - start) * pixelSize, pixelSize};
      }
    }
    penX += static_cast<float>(font::kAdvance) * pixelSize;
  }
  flush();
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

}  // namespace pinball
