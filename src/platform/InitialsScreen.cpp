#include "platform/InitialsScreen.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "core/Format.hpp"
#include "core/OverlayLayout.hpp"
#include "generated/ArtPalette.hpp"
#include "platform/Text.hpp"
#include "platform/TouchOverlay.hpp"

namespace pinball {

namespace {

// The art's palette (tools/art/palette.py).
constexpr SDL_Color sdl(art::Rgb c) noexcept { return {c.r, c.g, c.b, 255}; }
constexpr SDL_Color kYellow = sdl(art::kYellow);
constexpr SDL_Color kAqua = sdl(art::kAqua);
constexpr SDL_Color kCream = sdl(art::kCream);
constexpr SDL_Color kDim = sdl(art::kOrchid);
constexpr SDL_Color kPink = sdl(art::kPink);
constexpr SDL_Color kTeal = sdl(art::kTeal);
constexpr SDL_Color kViolet = sdl(art::kViolet);

constexpr float kLetterSize = 14.0F;    // Canvas pixels per font pixel.
constexpr float kLetterPitch = 120.0F;  // Distance between letter centres.

// An arrow button: a plain face with a stepped triangle on it, pointing up
// or down.
void drawArrowButton(SDL_Renderer* r, const ButtonArt& art, const screen::Rect& face, bool up) {
  drawButtonFace(r, art, face, kViolet, false);
  const SDL_Color ink = captionColor(art);
  constexpr int kRows = 6;
  constexpr float kRowHeight = 6.0F;
  const float middle = face.x + face.w * 0.5F;
  const float top = face.y + (face.h - kRows * kRowHeight) * 0.5F;
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
  SDL_SetRenderDrawColor(r, ink.r, ink.g, ink.b, SDL_ALPHA_OPAQUE);
  for (int row = 0; row < kRows; ++row) {
    // The narrow end is at the top of an up arrow, the bottom of a down one.
    const int rank = up ? row : kRows - 1 - row;
    const float half = 6.0F + static_cast<float>(rank) * 6.0F;
    const SDL_FRect bar{middle - half, top + static_cast<float>(row) * kRowHeight, half * 2.0F,
                        kRowHeight};
    SDL_RenderFillRect(r, &bar);
  }
}

}  // namespace

void drawInitialsEntry(SDL_Renderer* r, const InitialsEntry& entry, std::uint64_t score,
                       bool cursorLit, const screen::Layout& layout, const OverlayArt& art) {
  if (r == nullptr) {
    return;
  }
  // Dim everything behind.
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(r, screen::kBackdropRgb[0], screen::kBackdropRgb[1],
                         screen::kBackdropRgb[2], 205);
  const SDL_FRect whole{0.0F, 0.0F, layout.canvasWidth, layout.canvasHeight};
  SDL_RenderFillRect(r, &whole);
  // Only the dimming needs blending; the dialog's own fills are opaque.
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

  const screen::InitialsLayout prompt = screen::initialsLayout(layout);
  const bool touch = layout.idiom == screen::Idiom::Touch;
  const screen::Rect& dialog = prompt.box;
  const SDL_FRect box{dialog.x, dialog.y, dialog.w, dialog.h};
  drawDialogCard(r, art, box);
  if (art.dialog == nullptr) {
    drawNeonZigzag(r, box.x + 40.0F, box.y + 110.0F, box.y + box.h - 110.0F, kPink);
    drawNeonZigzag(r, box.x + box.w - 40.0F, box.y + 110.0F, box.y + box.h - 110.0F, kAqua);
  }

  const float middle = box.x + box.w * 0.5F;
  drawNeonText(r, "NEW HIGH SCORE", middle, box.y + 36.0F, 5.0F, kYellow, kPink, kViolet,
               TextAlign::Center);
  ScoreText scoreBuffer{};
  drawText(r, formatScore(score, scoreBuffer), middle, box.y + 96.0F, 4.0F, kCream,
           TextAlign::Center);
  if (touch) {
    // Short enough for the box at either size.
    const float size = screen::smallestTextSize(layout);
    drawText(r, "ARROWS CHANGE A LETTER", middle, box.y + 157.0F - 3.5F * size, size,
             kDim, TextAlign::Center);
  } else {
    drawText(r, "ENTER YOUR INITIALS", middle, box.y + 150.0F, 2.0F, kDim, TextAlign::Center);
  }

  drawDialogRule(r, art, middle, box.y + 174.0F, 200.0F);

  const Initials& letters = entry.letters();
  // A touch screen has an arrow above and below each letter, which moves
  // the letters down their slots.
  const float lettersTop = touch ? prompt.slot[0].y + 16.0F : box.y + 196.0F;
  for (std::size_t i = 0; i < letters.size(); ++i) {
    const float centre = middle + (static_cast<float>(i) - 1.0F) * kLetterPitch;
    const bool current = i == entry.cursor();
    if (current) {
      drawNeonText(r, std::string_view(&letters[i], 1), centre, lettersTop, kLetterSize, kCream,
                   kYellow, kPink, TextAlign::Center);
    } else {
      drawNeonText(r, std::string_view(&letters[i], 1), centre, lettersTop, kLetterSize, kCream,
                   kAqua, kViolet, TextAlign::Center);
    }
    if (current && cursorLit) {
      const SDL_FRect marker{centre - 40.0F, lettersTop + 7.0F * kLetterSize + 12.0F, 80.0F, 8.0F};
      SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
      SDL_SetRenderDrawColor(r, kYellow.r, kYellow.g, kYellow.b, 255);
      SDL_RenderFillRect(r, &marker);
    }
  }

  if (touch) {
    for (std::size_t i = 0; i < prompt.up.size(); ++i) {
      drawArrowButton(r, art.button, prompt.up[i], true);
      drawArrowButton(r, art.button, prompt.down[i], false);
    }
    drawCaptionButton(r, prompt.ok, "OK", kTeal, false, 4.0F, 3.0F, art.button);
    return;
  }
  drawText(r, "TYPE A LETTER, OR UP AND DOWN TO CHANGE IT", middle, box.y + box.h - 84.0F, 2.0F,
           kDim, TextAlign::Center);
  drawText(r, "ENTER TO CONFIRM   BACKSPACE TO GO BACK   ESC TO ACCEPT", middle, box.y + box.h - 56.0F, 2.0F,
           kDim, TextAlign::Center);
}

}  // namespace pinball
