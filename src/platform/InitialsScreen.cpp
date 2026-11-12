#include "platform/InitialsScreen.hpp"

#include <cstddef>
#include <string_view>

#include "core/Format.hpp"
#include "platform/Canvas.hpp"
#include "platform/Text.hpp"

namespace pinball {

namespace {

constexpr SDL_Color kAmber{255, 190, 70, 255};
constexpr SDL_Color kCyan{110, 220, 255, 255};
constexpr SDL_Color kWhite{235, 240, 255, 255};
constexpr SDL_Color kDim{130, 145, 180, 255};

constexpr float kLetterSize = 14.0F;    // Canvas pixels per font pixel.
constexpr float kLetterPitch = 120.0F;  // Distance between letter centres.

}  // namespace

void drawInitialsEntry(SDL_Renderer* r, const InitialsEntry& entry, std::uint64_t score,
                       bool cursorLit) {
  if (r == nullptr) {
    return;
  }
  // Dim everything behind.
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(r, 0, 0, 12, 215);
  const SDL_FRect whole{0.0F, 0.0F, static_cast<float>(kCanvasWidth),
                        static_cast<float>(kCanvasHeight)};
  SDL_RenderFillRect(r, &whole);

  const SDL_FRect box{290.0F, 250.0F, 700.0F, 460.0F};
  SDL_SetRenderDrawColor(r, kCyan.r, kCyan.g, kCyan.b, 255);
  SDL_RenderFillRect(r, &box);
  const SDL_FRect inner{box.x + 4.0F, box.y + 4.0F, box.w - 8.0F, box.h - 8.0F};
  SDL_SetRenderDrawColor(r, 10, 14, 34, 255);
  SDL_RenderFillRect(r, &inner);
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

  const float middle = box.x + box.w * 0.5F;
  drawText(r, "NEW HIGH SCORE", middle, box.y + 36.0F, 5.0F, kCyan, TextAlign::Center);
  ScoreText scoreBuffer{};
  drawText(r, formatScore(score, scoreBuffer), middle, box.y + 96.0F, 4.0F, kWhite,
           TextAlign::Center);
  drawText(r, "ENTER YOUR INITIALS", middle, box.y + 150.0F, 2.0F, kDim, TextAlign::Center);

  const Initials& letters = entry.letters();
  const float lettersTop = box.y + 196.0F;
  for (std::size_t i = 0; i < letters.size(); ++i) {
    const float centre = middle + (static_cast<float>(i) - 1.0F) * kLetterPitch;
    const bool current = i == entry.cursor();
    drawText(r, std::string_view(&letters[i], 1), centre, lettersTop, kLetterSize,
             current ? kAmber : kWhite, TextAlign::Center);
    if (current && cursorLit) {
      const SDL_FRect marker{centre - 40.0F, lettersTop + 7.0F * kLetterSize + 12.0F, 80.0F, 8.0F};
      SDL_SetRenderDrawColor(r, kAmber.r, kAmber.g, kAmber.b, 255);
      SDL_RenderFillRect(r, &marker);
    }
  }

  drawText(r, "TYPE A LETTER, OR UP AND DOWN TO CHANGE IT", middle, box.y + box.h - 84.0F, 2.0F,
           kDim, TextAlign::Center);
  drawText(r, "ENTER TO CONFIRM   BACKSPACE TO GO BACK", middle, box.y + box.h - 56.0F, 2.0F,
           kDim, TextAlign::Center);
}

}  // namespace pinball
