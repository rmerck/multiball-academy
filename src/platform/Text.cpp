#include "platform/Text.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>

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
  const std::size_t count = std::min(text.size(), kMaxTextLength);
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

  // Opaque text needs no blending: the pixels come out the same, and a
  // software renderer fills them several times faster without it.
  SDL_SetRenderDrawBlendMode(renderer, color.a == SDL_ALPHA_OPAQUE ? SDL_BLENDMODE_NONE
                                                                   : SDL_BLENDMODE_BLEND);
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
  // The hot pink tube with a white-hot core. Opaque fills need no blending.
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
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
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 34);
  for (float y = faceY; y < faceY + faceH - 1.0F; y += 4.0F) {
    fillRect(renderer, faceX, y, faceW, 1.0F);
  }
  // The inner cyan line, broken at the corners by yellow studs.
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
  const float inset = 9.0F;
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

void drawNeonRule(SDL_Renderer* renderer, float centerX, float y, float halfWidth) {
  if (renderer == nullptr) {
    return;
  }
  constexpr SDL_Color kBars[] = {{255, 225, 70, 255}, {255, 140, 110, 255}, {255, 60, 170, 255}};
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
  float half = halfWidth;
  for (const SDL_Color& c : kBars) {
    SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
    fillRect(renderer, centerX - half, y, half * 2.0F, 2.0F);
    y += 4.0F;
    half *= 0.72F;
  }
}

void drawNeonSunset(SDL_Renderer* renderer, const SDL_FRect& area) {
  if (renderer == nullptr || !(area.w > 0.0F) || !(area.h > 0.0F)) {
    return;
  }
  const SDL_Rect clip{static_cast<int>(std::lround(area.x)), static_cast<int>(std::lround(area.y)),
                      static_cast<int>(std::lround(area.w)), static_cast<int>(std::lround(area.h))};
  SDL_SetRenderClipRect(renderer, &clip);
  // Every fill here is opaque.
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
  const float horizon = area.y + area.h * 0.62F;
  const float centre = area.x + area.w * 0.5F;
  const float bottom = area.y + area.h;
  const float skyHeight = horizon - area.y;

  // Sky: indigo shading to magenta at the horizon.
  constexpr int kSkyBands = 16;
  for (int i = 0; i < kSkyBands; ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(kSkyBands - 1);
    SDL_SetRenderDrawColor(renderer, blend(14, 150, t * t), blend(4, 18, t), blend(44, 128, t),
                           255);
    fillRect(renderer, area.x, area.y + skyHeight * static_cast<float>(i) / kSkyBands, area.w,
             skyHeight / kSkyBands + 1.0F);
  }
  // A few fixed stars, as fractions of the sky.
  constexpr std::array<std::array<float, 2>, 16> kStars{{{0.04F, 0.20F}, {0.09F, 0.55F},
      {0.15F, 0.12F}, {0.21F, 0.42F}, {0.27F, 0.24F}, {0.33F, 0.08F}, {0.38F, 0.36F},
      {0.63F, 0.10F}, {0.68F, 0.40F}, {0.73F, 0.22F}, {0.79F, 0.50F}, {0.84F, 0.14F},
      {0.89F, 0.34F}, {0.94F, 0.58F}, {0.97F, 0.18F}, {0.57F, 0.26F}}};
  SDL_SetRenderDrawColor(renderer, 255, 240, 255, 255);
  for (const std::array<float, 2>& star : kStars) {
    fillRect(renderer, area.x + area.w * star[0], area.y + skyHeight * star[1], 2.0F, 2.0F);
  }

  // The sun, in rows: laser yellow at the crown, hot pink at the horizon,
  // cut by slats that widen as it sinks.
  const float radius = skyHeight * 0.88F;
  float slatTop = horizon - radius * 0.52F;
  float slatGap = 2.0F;
  for (float y = horizon - radius; y < horizon; y += 1.0F) {
    while (y >= slatTop + slatGap) {
      slatTop += slatGap + 8.0F;
      slatGap += 1.5F;
    }
    if (y >= slatTop) {
      continue;
    }
    const float dy = horizon - y;
    const float half = std::sqrt(radius * radius - dy * dy);
    const float t = (y - (horizon - radius)) / radius;
    SDL_SetRenderDrawColor(renderer, 255, blend(225, 60, t), blend(70, 170, t), 255);
    fillRect(renderer, centre - half, y, half * 2.0F, 1.0F);
  }

  // A dark skyline along the horizon, lowest in front of the sun.
  constexpr std::array<Uint8, 30> kSkyline{16, 10, 21, 14, 9,  18, 12, 7, 15, 11, 5,  8, 6, 4, 5,
                                           4,  6,  5,  9,  12, 8,  17, 11, 7, 20, 9,  16, 10, 22, 12};
  const float lot = area.w / static_cast<float>(kSkyline.size());
  for (std::size_t i = 0; i < kSkyline.size(); ++i) {
    const float height = static_cast<float>(kSkyline[i]);
    const float left = area.x + lot * static_cast<float>(i);
    SDL_SetRenderDrawColor(renderer, 10, 2, 26, 255);
    fillRect(renderer, left, horizon - height, lot + 0.5F, height);
    if (i % 3 == 0 && height > 8.0F) {
      SDL_SetRenderDrawColor(renderer, 255, 225, 70, 255);
      fillRect(renderer, left + 4.0F, horizon - height + 4.0F, 2.0F, 2.0F);
      fillRect(renderer, left + 10.0F, horizon - height + 9.0F, 2.0F, 2.0F);
    }
  }

  // The floor: a grid running to the horizon, magenta far away, cyan close.
  SDL_SetRenderDrawColor(renderer, 14, 3, 38, 255);
  fillRect(renderer, area.x, horizon, area.w, bottom - horizon);
  SDL_SetRenderDrawColor(renderer, 190, 50, 210, 255);
  for (int i = -14; i <= 14; ++i) {
    const float fi = static_cast<float>(i);
    SDL_RenderLine(renderer, centre + fi * 5.0F, horizon, centre + fi * area.w / 9.0F, bottom);
  }
  constexpr int kRows = 7;
  for (int k = 1; k <= kRows; ++k) {
    const float t = static_cast<float>(k) / static_cast<float>(kRows);
    SDL_SetRenderDrawColor(renderer, blend(255, 70, t), blend(60, 235, t), blend(220, 255, t), 255);
    fillRect(renderer, area.x, horizon + (bottom - horizon) * t * t - 1.0F, area.w,
             1.0F + t);
  }
  SDL_SetRenderDrawColor(renderer, 215, 248, 255, 255);
  fillRect(renderer, area.x, horizon - 1.0F, area.w, 2.0F);
  SDL_SetRenderClipRect(renderer, nullptr);
}

void drawNeonZigzag(SDL_Renderer* renderer, float x, float top, float bottom, SDL_Color color) {
  if (renderer == nullptr) {
    return;
  }
  SDL_SetRenderDrawBlendMode(renderer, color.a == SDL_ALPHA_OPAQUE ? SDL_BLENDMODE_NONE
                                                                   : SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
  constexpr float kDot = 3.0F;
  int step = 0;
  for (float y = top; y + kDot <= bottom; y += kDot) {
    const int phase = step % 8;
    const int offset = phase < 4 ? phase : 8 - phase;  // 0 1 2 3 4 3 2 1
    fillRect(renderer, x + static_cast<float>(offset - 2) * kDot - kDot * 0.5F, y, kDot, kDot);
    ++step;
  }
  SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

}  // namespace pinball
