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

namespace {

// Draws font rows [rowBegin, rowEnd) of the text in one colour.
void drawRows(SDL_Renderer* renderer, std::string_view text, float x, float y, float pixelSize,
              SDL_Color color, TextAlign align, int rowBegin, int rowEnd) {
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
    for (int row = rowBegin; row < rowEnd; ++row) {
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

Uint8 blend(Uint8 a, Uint8 b, float t) noexcept {
  const float from = static_cast<float>(a);
  return static_cast<Uint8>(from + (static_cast<float>(b) - from) * t + 0.5F);
}

void fillRect(SDL_Renderer* r, float x, float y, float w, float h) {
  const SDL_FRect rect{x, y, w, h};
  SDL_RenderFillRect(r, &rect);
}

}  // namespace

void drawText(SDL_Renderer* renderer, std::string_view text, float x, float y, float pixelSize,
              SDL_Color color, TextAlign align) {
  drawRows(renderer, text, x, y, pixelSize, color, align, 0, font::kGlyphHeight);
}

void drawNeonText(SDL_Renderer* renderer, std::string_view text, float x, float y,
                  float pixelSize, SDL_Color top, SDL_Color bottom, SDL_Color edge,
                  TextAlign align) {
  const float offset = pixelSize * 0.5F;
  // Shadow, then the coloured edge, then the shaded face.
  drawText(renderer, text, x + offset * 2.0F, y + offset * 2.0F, pixelSize, {8, 0, 24, 235}, align);
  drawText(renderer, text, x + offset, y + offset, pixelSize, edge, align);
  for (int row = 0; row < font::kGlyphHeight; ++row) {
    const float t = static_cast<float>(row) / static_cast<float>(font::kGlyphHeight - 1);
    const SDL_Color c{blend(top.r, bottom.r, t), blend(top.g, bottom.g, t),
                      blend(top.b, bottom.b, t), 255};
    drawRows(renderer, text, x, y, pixelSize, c, align, row, row + 1);
  }
}

void drawNeonFrame(SDL_Renderer* renderer, const SDL_FRect& box) {
  if (renderer == nullptr) {
    return;
  }
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  // Bloom: stacked translucent frames that fade outwards.
  for (int i = 8; i >= 1; --i) {
    const float grow = static_cast<float>(i) * 3.0F;
    SDL_SetRenderDrawColor(renderer, 255, 40, 170, static_cast<Uint8>(30 - i * 3));
    fillRect(renderer, box.x - grow, box.y - grow, box.w + grow * 2.0F, box.h + grow * 2.0F);
  }
  // The hot pink tube with a white-hot core.
  SDL_SetRenderDrawColor(renderer, 255, 50, 170, 255);
  fillRect(renderer, box.x, box.y, box.w, box.h);
  SDL_SetRenderDrawColor(renderer, 255, 215, 240, 255);
  fillRect(renderer, box.x + 1.5F, box.y + 1.5F, box.w - 3.0F, box.h - 3.0F);
  SDL_SetRenderDrawColor(renderer, 255, 50, 170, 255);
  fillRect(renderer, box.x + 2.5F, box.y + 2.5F, box.w - 5.0F, box.h - 5.0F);
  // The face: deep indigo shading to violet at the foot.
  constexpr int kBands = 24;
  const float faceX = box.x + 4.0F;
  const float faceY = box.y + 4.0F;
  const float faceW = box.w - 8.0F;
  const float faceH = box.h - 8.0F;
  for (int i = 0; i < kBands; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(kBands - 1);
    SDL_SetRenderDrawColor(renderer, blend(9, 40, t * t), blend(3, 6, t), blend(30, 66, t * t), 255);
    // Bands overlap by a pixel so no seam shows; the last one stops at the edge.
    const float top = faceH * static_cast<float>(i) / static_cast<float>(kBands);
    const float bandH = i + 1 < kBands ? faceH / static_cast<float>(kBands) + 1.0F : faceH - top;
    fillRect(renderer, faceX, faceY + top, faceW, bandH);
  }
  // A faint raster.
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 34);
  for (float y = faceY; y < faceY + faceH - 1.0F; y += 4.0F) {
    fillRect(renderer, faceX, y, faceW, 1.0F);
  }
  // The inner cyan line, broken at the corners by yellow studs.
  const float inset = 12.0F;
  const float lx = box.x + inset;
  const float ly = box.y + inset;
  const float lw = box.w - inset * 2.0F;
  const float lh = box.h - inset * 2.0F;
  SDL_SetRenderDrawColor(renderer, 70, 235, 255, 255);
  fillRect(renderer, lx + 14.0F, ly, lw - 28.0F, 2.0F);
  fillRect(renderer, lx + 14.0F, ly + lh - 2.0F, lw - 28.0F, 2.0F);
  fillRect(renderer, lx, ly + 14.0F, 2.0F, lh - 28.0F);
  fillRect(renderer, lx + lw - 2.0F, ly + 14.0F, 2.0F, lh - 28.0F);
  SDL_SetRenderDrawColor(renderer, 255, 225, 70, 255);
  fillRect(renderer, lx, ly, 6.0F, 6.0F);
  fillRect(renderer, lx + lw - 6.0F, ly, 6.0F, 6.0F);
  fillRect(renderer, lx, ly + lh - 6.0F, 6.0F, 6.0F);
  fillRect(renderer, lx + lw - 6.0F, ly + lh - 6.0F, 6.0F, 6.0F);
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

}  // namespace pinball
