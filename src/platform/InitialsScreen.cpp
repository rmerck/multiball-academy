#include "platform/InitialsScreen.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "core/Format.hpp"
#include "core/OverlayLayout.hpp"
#include "platform/Text.hpp"

namespace pinball {

namespace {

// Neon palette shared with the panel: laser yellow, cyan, hot pink.
constexpr SDL_Color kAmber{255, 225, 70, 255};
constexpr SDL_Color kCyan{70, 235, 255, 255};
constexpr SDL_Color kWhite{248, 240, 255, 255};
constexpr SDL_Color kDim{176, 150, 226, 255};
constexpr SDL_Color kPink{255, 60, 170, 255};

constexpr float kLetterSize = 14.0F;    // Canvas pixels per font pixel.
constexpr float kLetterPitch = 120.0F;  // Distance between letter centres.

}  // namespace

void drawInitialsEntry(SDL_Renderer* r, const InitialsEntry& entry, std::uint64_t score,
                       bool cursorLit, const screen::Layout& layout) {
  if (r == nullptr) {
    return;
  }
  // Dim everything behind.
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(r, 10, 0, 26, 205);
  const SDL_FRect whole{0.0F, 0.0F, layout.canvasWidth, layout.canvasHeight};
  SDL_RenderFillRect(r, &whole);
  // Only the dimming needs blending; the dialog's own fills are opaque.
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

  const screen::Rect dialog = screen::initialsLayout(layout).box;
  const SDL_FRect box{dialog.x, dialog.y, dialog.w, dialog.h};
  drawNeonFrame(r, box);
  drawNeonZigzag(r, box.x + 40.0F, box.y + 110.0F, box.y + box.h - 110.0F, kPink);
  drawNeonZigzag(r, box.x + box.w - 40.0F, box.y + 110.0F, box.y + box.h - 110.0F, kCyan);

  const float middle = box.x + box.w * 0.5F;
  drawNeonText(r, "NEW HIGH SCORE", middle, box.y + 36.0F, 5.0F, kAmber, kPink,
               {120, 30, 200, 255}, TextAlign::Center);
  ScoreText scoreBuffer{};
  drawText(r, formatScore(score, scoreBuffer), middle, box.y + 96.0F, 4.0F, kWhite,
           TextAlign::Center);
  drawText(r, "ENTER YOUR INITIALS", middle, box.y + 150.0F, 2.0F, kDim, TextAlign::Center);

  drawNeonRule(r, middle, box.y + 174.0F, 200.0F);

  const Initials& letters = entry.letters();
  const float lettersTop = box.y + 196.0F;
  for (std::size_t i = 0; i < letters.size(); ++i) {
    const float centre = middle + (static_cast<float>(i) - 1.0F) * kLetterPitch;
    const bool current = i == entry.cursor();
    if (current) {
      drawNeonText(r, std::string_view(&letters[i], 1), centre, lettersTop, kLetterSize, kWhite,
                   kAmber, kPink, TextAlign::Center);
    } else {
      drawNeonText(r, std::string_view(&letters[i], 1), centre, lettersTop, kLetterSize, kWhite,
                   kCyan, {60, 40, 160, 255}, TextAlign::Center);
    }
    if (current && cursorLit) {
      const SDL_FRect marker{centre - 40.0F, lettersTop + 7.0F * kLetterSize + 12.0F, 80.0F, 8.0F};
      SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
      SDL_SetRenderDrawColor(r, kAmber.r, kAmber.g, kAmber.b, 255);
      SDL_RenderFillRect(r, &marker);
    }
  }

  drawText(r, "TYPE A LETTER, OR UP AND DOWN TO CHANGE IT", middle, box.y + box.h - 84.0F, 2.0F,
           kDim, TextAlign::Center);
  drawText(r, "ENTER TO CONFIRM   BACKSPACE TO GO BACK   ESC TO ACCEPT", middle, box.y + box.h - 56.0F, 2.0F,
           kDim, TextAlign::Center);
}

}  // namespace pinball
