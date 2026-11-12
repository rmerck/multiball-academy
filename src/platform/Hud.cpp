#include "platform/Hud.hpp"

#include <array>
#include <string_view>

#include "core/Format.hpp"
#include "core/PanelLayout.hpp"
#include "platform/Text.hpp"

namespace pinball {

namespace {

constexpr SDL_Color kAmber{255, 190, 70, 255};
constexpr SDL_Color kCyan{110, 220, 255, 255};
constexpr SDL_Color kWhite{235, 240, 255, 255};
constexpr SDL_Color kDim{120, 135, 170, 255};
constexpr SDL_Color kGreen{120, 255, 150, 255};
constexpr SDL_Color kRed{255, 110, 100, 255};

constexpr float kLabelSize = 2.0F;
constexpr float kPad = 12.0F;

float centerX(const panel::Rect& r) noexcept { return r.x + r.w * 0.5F; }

void label(SDL_Renderer* r, const panel::Rect& well, std::string_view text) {
  drawText(r, text, well.x + kPad, well.y + 10.0F, kLabelSize, kDim);
}

}  // namespace

void drawHud(SDL_Renderer* r, SDL_Texture* panelArt, const Game& game) {
  if (r == nullptr) {
    return;
  }
  const SDL_FRect area{panel::kLeft, 0.0F, panel::kWidth, panel::kHeight};
  if (panelArt != nullptr) {
    SDL_RenderTexture(r, panelArt, nullptr, &area);
  } else {
    SDL_SetRenderDrawColor(r, 24, 24, 30, SDL_ALPHA_OPAQUE);
    SDL_RenderFillRect(r, &area);
  }

  const Rules& rules = game.rules();
  const float middle = panel::kLeft + panel::kWidth * 0.5F;

  // Title.
  drawText(r, "SPACE CADET", middle, 38.0F, 6.0F, kCyan, TextAlign::Center);
  drawText(r, "PINBALL", middle, 98.0F, 3.0F, kAmber, TextAlign::Center);

  // Score.
  label(r, panel::kScoreWell, "SCORE");
  ScoreText scoreBuffer{};
  const std::string_view score = formatScore(rules.score(), scoreBuffer);
  // Shrink the digits when the number gets long, so it always fits the well.
  float size = 6.0F;
  while (size > 2.0F && textWidth(score, size) > panel::kScoreWell.w - 2.0F * kPad) {
    size -= 1.0F;
  }
  drawText(r, score, panel::kScoreWell.x + panel::kScoreWell.w - kPad,
           panel::kScoreWell.y + 44.0F, size, kWhite, TextAlign::Right);

  // Ball and multiplier.
  std::array<char, 32> buffer{};
  drawText(r, formatText(buffer.data(), buffer.size(), "BALL %d OF %d", rules.ball(), Rules::kBallsPerGame),
           panel::kStatusWell.x + kPad, panel::kStatusWell.y + 14.0F, 3.0F, kWhite);
  if (rules.extraBalls() > 0) {
    drawText(r, formatText(buffer.data(), buffer.size(), "EXTRA BALLS %d", rules.extraBalls()), panel::kStatusWell.x + kPad,
             panel::kStatusWell.y + 44.0F, kLabelSize, kGreen);
  }
  drawText(r, formatText(buffer.data(), buffer.size(), "X%d", rules.fieldMultiplier()),
           panel::kStatusWell.x + panel::kStatusWell.w - kPad, panel::kStatusWell.y + 14.0F, 5.0F,
           rules.fieldMultiplier() > 1 ? kAmber : kDim, TextAlign::Right);

  // Awards in progress.
  label(r, panel::kRankWell, "SYSTEMS");
  const float awardsY = panel::kRankWell.y + 34.0F;
  drawText(r, formatText(buffer.data(), buffer.size(), "ATTACK LV %d", rules.attackBumperLevel() + 1),
           panel::kRankWell.x + kPad, awardsY, kLabelSize, kWhite);
  drawText(r, formatText(buffer.data(), buffer.size(), "BOOSTER %d/4", rules.boosterLevel()), panel::kRankWell.x + kPad,
           awardsY + 22.0F, kLabelSize, kWhite);
  drawText(r, formatText(buffer.data(), buffer.size(), "MEDAL %d/3", rules.medalLevel()), centerX(panel::kRankWell) + kPad,
           awardsY, kLabelSize, kWhite);
  drawText(r, formatText(buffer.data(), buffer.size(), "HYPERSPACE %d/4", rules.hyperspaceLevel()),
           centerX(panel::kRankWell) + kPad, awardsY + 22.0F, kLabelSize, kWhite);

  // Lit awards.
  label(r, panel::kMissionWell, "STATUS");
  float line = panel::kMissionWell.y + 38.0F;
  const auto status = [&](std::string_view text, SDL_Color color) {
    drawText(r, text, panel::kMissionWell.x + kPad, line, 3.0F, color);
    line += 30.0F;
  };
  if (rules.ballSaveActive()) {
    status("BALL SAVE ON", kGreen);
  }
  if (rules.jackpotLit()) {
    status("JACKPOT LIT", kAmber);
  }
  if (rules.bonusLaneLit()) {
    status("BONUS LANES LIT", kAmber);
  }
  ScoreText bonusBuffer{};
  std::array<char, 48> bonusLine{};
  const std::string_view bonus = formatScore(rules.bonus(), bonusBuffer);
  status(formatText(bonusLine.data(), bonusLine.size(), "BONUS %.*s", static_cast<int>(bonus.size()), bonus.data()), kWhite);

  // Message.
  const std::string_view message = rules.message();
  if (!message.empty()) {
    float messageSize = 4.0F;
    while (messageSize > 2.0F &&
           textWidth(message, messageSize) > panel::kMessageWell.w - 2.0F * kPad) {
      messageSize -= 1.0F;
    }
    drawText(r, message, centerX(panel::kMessageWell),
             panel::kMessageWell.y + panel::kMessageWell.h * 0.5F - 3.5F * messageSize,
             messageSize, rules.gameOver() ? kRed : kAmber, TextAlign::Center);
  }

  // Controls.
  drawText(r, "Z AND / FLIP     SPACE LAUNCH", middle, 892.0F, kLabelSize, kDim, TextAlign::Center);
  drawText(r, "F2 NEW GAME   F3 PAUSE   F4 FULL SCREEN", middle, 916.0F, kLabelSize, kDim,
           TextAlign::Center);
}

}  // namespace pinball
