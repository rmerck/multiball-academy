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

// Neon palette shared with the panel: laser yellow, cyan, hot pink.
constexpr SDL_Color kAmber{255, 225, 70, 255};
constexpr SDL_Color kCyan{70, 235, 255, 255};
constexpr SDL_Color kWhite{248, 240, 255, 255};
constexpr SDL_Color kDim{176, 150, 226, 255};
constexpr SDL_Color kPink{255, 60, 170, 255};

// Where the source lives. The screen names no person.
constexpr std::string_view kSource = "GITHUB.COM/RMERCK";

}  // namespace

void drawAbout(SDL_Renderer* r, const HighScores& scores) {
  if (r == nullptr) {
    return;
  }
  // Dim everything behind.
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(r, 10, 0, 26, 205);
  const SDL_FRect whole{0.0F, 0.0F, static_cast<float>(kCanvasWidth),
                        static_cast<float>(kCanvasHeight)};
  SDL_RenderFillRect(r, &whole);
  // Only the dimming needs blending; the dialog's own fills are opaque.
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);

  // The dialog: a bordered box in the middle of the canvas.
  const SDL_FRect box{190.0F, 90.0F, 900.0F, 780.0F};
  drawNeonFrame(r, box);

  const float middle = box.x + box.w * 0.5F;
  // Ornaments in the table's style: zigzags down the margins and a small
  // sunset over a grid between the scores and the closing note.
  drawNeonZigzag(r, box.x + 46.0F, box.y + 120.0F, box.y + box.h - 120.0F, kPink);
  drawNeonZigzag(r, box.x + box.w - 46.0F, box.y + 120.0F, box.y + box.h - 120.0F, kCyan);
  drawNeonSunset(r, {middle - 300.0F, box.y + box.h - 258.0F, 600.0F, 118.0F});
  float y = box.y + 36.0F;

  drawNeonText(r, "SPACE CADET PINBALL", middle, y, 6.0F, kAmber, kPink, {120, 30, 200, 255},
               TextAlign::Center);
  y += 64.0F;
  std::array<char, 32> version{};
  drawText(r, formatText(version.data(), version.size(), "VERSION %s", kGameVersion), middle, y,
           2.0F, kDim, TextAlign::Center);
  drawNeonRule(r, middle, y + 24.0F, 300.0F);
  y += 54.0F;

  drawText(r, "SOURCE", middle, y, 2.0F, kDim, TextAlign::Center);
  y += 26.0F;
  drawText(r, kSource, middle, y, 3.0F, kAmber, TextAlign::Center);
  y += 96.0F;

  drawNeonText(r, "HIGH SCORES", middle, y, 3.0F, kWhite, kCyan, {200, 40, 150, 255},
               TextAlign::Center);
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
             y, 3.0F, kPink);
    drawText(r, score, middle + 60.0F, y, 3.0F, kWhite, TextAlign::Right);
    drawText(r, rankName(entry.rank), middle + 100.0F, y, 3.0F, kAmber);
    y += 36.0F;
  }

  drawNeonRule(r, middle, box.y + box.h - 124.0F, 300.0F);
  drawText(r, "AN INDEPENDENT FAN PROJECT. NOT AFFILIATED WITH MICROSOFT,", middle,
           box.y + box.h - 100.0F, 2.0F, kDim, TextAlign::Center);
  drawText(r, "MAXIS OR CINEMATRONICS. ALL ART AND SOUND ARE ORIGINAL.", middle,
           box.y + box.h - 78.0F, 2.0F, kDim, TextAlign::Center);
  drawText(r, "PRESS F1 TO CLOSE", middle, box.y + box.h - 42.0F, 3.0F, kWhite, TextAlign::Center);
}

}  // namespace pinball
