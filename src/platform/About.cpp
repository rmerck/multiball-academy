#include "platform/About.hpp"

#include <array>
#include <cstddef>
#include <string_view>

#include "core/Format.hpp"
#include "core/Missions.hpp"
#include "core/Version.hpp"
#include "platform/Canvas.hpp"
#include "platform/Text.hpp"

namespace pinball {

namespace {

constexpr SDL_Color kAmber{255, 190, 70, 255};
constexpr SDL_Color kCyan{110, 220, 255, 255};
constexpr SDL_Color kWhite{235, 240, 255, 255};
constexpr SDL_Color kDim{130, 145, 180, 255};

// Where the source lives. The screen names no person.
constexpr std::string_view kSource = "GITHUB.COM/RMERCK";

}  // namespace

void drawAbout(SDL_Renderer* r, const HighScores& scores) {
  if (r == nullptr) {
    return;
  }
  // Dim everything behind.
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(r, 0, 0, 12, 215);
  const SDL_FRect whole{0.0F, 0.0F, static_cast<float>(kCanvasWidth),
                        static_cast<float>(kCanvasHeight)};
  SDL_RenderFillRect(r, &whole);

  // The dialog: a bordered box in the middle of the canvas.
  const SDL_FRect box{190.0F, 90.0F, 900.0F, 780.0F};
  SDL_SetRenderDrawColor(r, 110, 220, 255, 255);
  SDL_RenderFillRect(r, &box);
  const SDL_FRect inner{box.x + 4.0F, box.y + 4.0F, box.w - 8.0F, box.h - 8.0F};
  SDL_SetRenderDrawColor(r, 10, 14, 34, 255);
  SDL_RenderFillRect(r, &inner);
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

  const float middle = box.x + box.w * 0.5F;
  float y = box.y + 36.0F;

  drawText(r, "SPACE CADET PINBALL", middle, y, 6.0F, kCyan, TextAlign::Center);
  y += 64.0F;
  std::array<char, 32> version{};
  drawText(r, formatText(version.data(), version.size(), "VERSION %s", kGameVersion), middle, y,
           2.0F, kDim, TextAlign::Center);
  y += 54.0F;

  drawText(r, "SOURCE", middle, y, 2.0F, kDim, TextAlign::Center);
  y += 26.0F;
  drawText(r, kSource, middle, y, 3.0F, kAmber, TextAlign::Center);
  y += 96.0F;

  drawText(r, "HIGH SCORES", middle, y, 3.0F, kCyan, TextAlign::Center);
  y += 38.0F;
  if (scores.size() == 0) {
    drawText(r, "NO GAMES FINISHED YET", middle, y, 2.0F, kDim, TextAlign::Center);
    y += 28.0F;
  }
  for (std::size_t i = 0; i < scores.size(); ++i) {
    const HighScore entry = scores.at(i);
    ScoreText scoreBuffer{};
    const std::string_view score = formatScore(entry.score, scoreBuffer);
    std::array<char, 8> place{};
    drawText(r, formatText(place.data(), place.size(), "%u.", static_cast<unsigned>(i + 1)),
             middle - 330.0F, y, 3.0F, kDim);
    drawText(r, std::string_view(entry.initials.data(), entry.initials.size()), middle - 285.0F,
             y, 3.0F, kCyan);
    drawText(r, score, middle + 60.0F, y, 3.0F, kWhite, TextAlign::Right);
    drawText(r, rankName(entry.rank), middle + 100.0F, y, 3.0F, kAmber);
    y += 36.0F;
  }

  drawText(r, "AN INDEPENDENT FAN PROJECT. NOT AFFILIATED WITH MICROSOFT,", middle,
           box.y + box.h - 100.0F, 2.0F, kDim, TextAlign::Center);
  drawText(r, "MAXIS OR CINEMATRONICS. ALL ART AND SOUND ARE ORIGINAL.", middle,
           box.y + box.h - 78.0F, 2.0F, kDim, TextAlign::Center);
  drawText(r, "PRESS F1 TO CLOSE", middle, box.y + box.h - 42.0F, 3.0F, kWhite, TextAlign::Center);
}

}  // namespace pinball
