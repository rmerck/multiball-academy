#include "platform/Hud.hpp"

#include <array>
#include <string_view>

#include "core/Format.hpp"
#include "core/Missions.hpp"
#include "core/PanelLayout.hpp"
#include "platform/Text.hpp"

namespace pinball {

namespace {

// Neon palette: laser yellow, cyan, mint and hot pink on dark glass.
constexpr SDL_Color kAmber{255, 225, 70, 255};
constexpr SDL_Color kCyan{70, 235, 255, 255};
constexpr SDL_Color kWhite{248, 240, 255, 255};
constexpr SDL_Color kDim{170, 142, 224, 255};
constexpr SDL_Color kGreen{90, 255, 180, 255};
constexpr SDL_Color kRed{255, 70, 150, 255};
constexpr SDL_Color kPink{255, 60, 170, 255};
constexpr SDL_Color kViolet{120, 30, 200, 255};
// An unlit segment of a gauge.
constexpr SDL_Color kUnlit{58, 30, 92, 255};

constexpr float kLabelSize = 2.0F;
constexpr float kPad = 12.0F;

float centerX(const panel::Rect& r) noexcept { return r.x + r.w * 0.5F; }

void label(SDL_Renderer* r, const panel::Rect& well, std::string_view text) {
  drawText(r, text, well.x + kPad, well.y + 10.0F, kLabelSize, kDim);
}

}  // namespace

void drawHud(SDL_Renderer* r, SDL_Texture* panelArt, const Game& game, const HudExtras& extras) {
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
  drawNeonText(r, "SPACE CADET", middle, 38.0F, 6.0F, kWhite, kCyan, kPink, TextAlign::Center);
  drawNeonText(r, "PINBALL", middle, 98.0F, 3.0F, kAmber, kPink, kViolet, TextAlign::Center);

  // Score, with the best score beside the label.
  label(r, panel::kScoreWell, "SCORE");
  {
    ScoreText bestBuffer{};
    std::array<char, 48> bestLine{};
    const std::string_view best = formatScore(extras.bestScore, bestBuffer);
    const bool celebrate = extras.newHighScore && (game.stepCount() / 30U) % 2U == 0U;
    drawText(r,
             extras.newHighScore
                 ? std::string_view("NEW HIGH SCORE")
                 : formatText(bestLine.data(), bestLine.size(), "HIGH %.*s",
                              static_cast<int>(best.size()), best.data()),
             panel::kScoreWell.x + panel::kScoreWell.w - kPad, panel::kScoreWell.y + 10.0F,
             kLabelSize, celebrate ? kGreen : kDim, TextAlign::Right);
  }
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

  const MissionControl& missions = rules.missions();
  // Slow blink for things waiting on the player.
  const bool blinkOn = (game.stepCount() / 30U) % 2U == 0U;

  // Rank and progress towards the next one.
  label(r, panel::kRankWell, "RANK");
  drawText(r, rankName(missions.rank()), panel::kRankWell.x + kPad, panel::kRankWell.y + 30.0F,
           4.0F, kCyan);
  {
    const float gap = 4.0F;
    const float segment = (panel::kRankWell.w - 2.0F * kPad - gap * (kProgressPerRank - 1)) /
                          static_cast<float>(kProgressPerRank);
    for (int i = 0; i < kProgressPerRank; ++i) {
      const SDL_FRect box{panel::kRankWell.x + kPad + static_cast<float>(i) * (segment + gap),
                          panel::kRankWell.y + panel::kRankWell.h - 18.0F, segment, 8.0F};
      // The colour changes at most twice along the gauge: set it only then.
      if (i == 0 || i == missions.progress()) {
        const SDL_Color c = i < missions.progress() ? kAmber : kUnlit;
        SDL_SetRenderDrawColor(r, c.r, c.g, c.b, SDL_ALPHA_OPAQUE);
      }
      SDL_RenderFillRect(r, &box);
    }
  }

  // Mission.
  label(r, panel::kMissionWell, "MISSION");
  const float missionX = panel::kMissionWell.x + kPad;
  const float missionY = panel::kMissionWell.y + 34.0F;
  if (const Mission* mission = missions.mission()) {
    drawText(r, mission->name, missionX, missionY, 3.0F, kAmber);
    if (missions.phase() == MissionControl::Phase::Selected) {
      if (blinkOn) {
        drawText(r, "SHOOT THE LAUNCH RAMP TO ACCEPT", missionX, missionY + 34.0F, kLabelSize,
                 kGreen);
      }
    } else if (const MissionStage* stage = missions.stage()) {
      drawText(r, goalText(stage->goal), missionX, missionY + 34.0F, kLabelSize, kWhite);
      drawText(r, formatText(buffer.data(), buffer.size(), "%d TO GO", missions.remaining()),
               panel::kMissionWell.x + panel::kMissionWell.w - kPad, missionY + 34.0F, kLabelSize,
               kWhite, TextAlign::Right);
      // Fuel gauge.
      drawText(r, "FUEL", missionX, missionY + 62.0F, kLabelSize, kDim);
      for (int i = 0; i < MissionControl::kFuelLights; ++i) {
        const SDL_FRect box{missionX + 60.0F + static_cast<float>(i) * 34.0F, missionY + 60.0F,
                            28.0F, 16.0F};
        const bool lit = i < missions.fuelLights();
        const bool low = missions.fuelLights() <= 2;
        if (lit && !(low && !blinkOn)) {
          const SDL_Color c = low ? kRed : kGreen;
          SDL_SetRenderDrawColor(r, c.r, c.g, c.b, SDL_ALPHA_OPAQUE);
        } else {
          SDL_SetRenderDrawColor(r, kUnlit.r, kUnlit.g, kUnlit.b, SDL_ALPHA_OPAQUE);
        }
        SDL_RenderFillRect(r, &box);
      }
    }
  } else {
    drawText(r, "NO MISSION", missionX, missionY, 3.0F, kDim);
    drawText(r, "HIT A MISSION TARGET, LEFT WALL", missionX, missionY + 34.0F, kLabelSize,
             kDim);
  }
  // Lit awards along the bottom of the mission window.
  {
    const float y = panel::kMissionWell.y + panel::kMissionWell.h - 26.0F;
    drawText(r, "JACKPOT", missionX, y, kLabelSize, rules.jackpotLit() ? kAmber : kDim);
    drawText(r, "BONUS LANES", centerX(panel::kMissionWell), y, kLabelSize,
             rules.bonusLaneLit() ? kAmber : kDim, TextAlign::Center);
    drawText(r, "BALL SAVE", panel::kMissionWell.x + panel::kMissionWell.w - kPad, y, kLabelSize,
             rules.ballSaveActive() ? kGreen : kDim, TextAlign::Right);
  }

  // Systems.
  {
    const panel::Rect& well = panel::kHighScoreWell;
    std::array<char, 64> line{};
    drawText(r,
             formatText(line.data(), line.size(), "ATK %d  BOOST %d/4  MEDAL %d/3  HYP %d/4",
                        rules.attackBumperLevel() + 1, rules.boosterLevel(), rules.medalLevel(),
                        rules.hyperspaceLevel()),
             centerX(well), well.y + 16.0F, kLabelSize, kWhite, TextAlign::Center);
    ScoreText bonusBuffer{};
    const std::string_view bonus = formatScore(rules.bonus(), bonusBuffer);
    drawText(r,
             formatText(line.data(), line.size(), "END OF BALL BONUS %.*s",
                        static_cast<int>(bonus.size()), bonus.data()),
             centerX(well), well.y + 42.0F, kLabelSize, kDim, TextAlign::Center);
  }

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
             messageSize, rules.gameOver() || rules.tilted() ? kRed : kAmber,
             TextAlign::Center);
  }

  // Controls.
  drawText(r, "Z / FLIP   SPACE LAUNCH   X . UP NUDGE", middle, 882.0F, kLabelSize, kDim,
           TextAlign::Center);
  drawText(r, "F1 ABOUT  F2 NEW  F3 PAUSE  F4 FULL", middle, 904.0F, kLabelSize, kDim,
           TextAlign::Center);
  drawText(r, "M MUTE   N MUSIC", middle, 926.0F, kLabelSize, kDim, TextAlign::Center);
}

}  // namespace pinball
