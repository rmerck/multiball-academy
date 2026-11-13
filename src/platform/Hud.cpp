#include "platform/Hud.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include <string_view>

#include "core/DisplayText.hpp"
#include "core/Format.hpp"
#include "core/Missions.hpp"
#include "core/PanelLayout.hpp"
#include "core/Version.hpp"
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
// The word on an unlit lamp: still readable.
constexpr SDL_Color kLampOff{96, 66, 150, 255};

// The margin and the text sizes the display limits are worked out from
// (core/DisplayText.hpp), so what the rules may say and what is drawn here
// cannot drift apart.
using display::kPad;
constexpr float kLabelSize = display::kSmallestSize;

// What the desktop adds to the game-over message. The two together are held
// to the message window like any message.
constexpr std::string_view kNewGameHint = "   PRESS F2";
constexpr std::string_view kGameOverWithHint = "GAME OVER   PRESS F2";
static_assert(kGameOverWithHint.starts_with(Rules::kGameOverMessage.text()) &&
              kGameOverWithHint.ends_with(kNewGameHint) &&
              kGameOverWithHint.size() == Rules::kGameOverMessage.text().size() + kNewGameHint.size());
static_assert(display::fits(kGameOverWithHint, display::kMaxMessageLength));

// Fixed lines of the course window. Each is held to the width it is drawn in.
constexpr std::string_view kNoCourse = "NO COURSE";
constexpr std::string_view kNoCourseHint = "HIT A COURSE TARGET, LEFT WALL";
constexpr std::string_view kEnrolHint = "SHOOT THE RUNWAY TO ENROL";
static_assert(kNoCourse.size() <= display::kMaxMissionNameLength);
static_assert(kNoCourseHint.size() <=
              display::charsThatFit(display::innerWidth(panel::kMissionWell), kLabelSize));
static_assert(kEnrolHint.size() <=
              display::charsThatFit(display::innerWidth(panel::kMissionWell), kLabelSize));

// The review tool's fullest course window: every line at its limit.
constexpr std::string_view kPreviewCourse = "COURSE NAME AT ITS LIMIT";
constexpr std::string_view kPreviewGoal = "GOAL LINE AT ITS FULL SIZE";
static_assert(kPreviewCourse.size() == display::kMaxMissionNameLength &&
              kPreviewGoal.size() == display::kMaxGoalTextLength);

// The windows of both displays have the same inner widths where the limits
// are worked out from them: what fits the panel fits the strip.
static_assert(display::innerWidth(strip::kMissionWell) ==
              display::innerWidth(panel::kMissionWell));
static_assert(display::innerWidth(strip::kRankWell) == display::kNarrowRankWidth);
static_assert(display::charsThatFit(display::innerWidth(strip::kMessageWell),
                                    display::kMissionSize) >= display::kMaxMessageLength);

float centerX(const screen::Rect& r) noexcept { return r.x + r.w * 0.5F; }

void label(SDL_Renderer* r, const screen::Rect& well, std::string_view text) {
  drawText(r, text, well.x + kPad, well.y + 10.0F, kLabelSize, kDim);
}

void setColor(SDL_Renderer* r, SDL_Color c) {
  SDL_SetRenderDrawColor(r, c.r, c.g, c.b, SDL_ALPHA_OPAQUE);
}

void fill(SDL_Renderer* r, float x, float y, float w, float h) {
  const SDL_FRect rect{x, y, w, h};
  SDL_RenderFillRect(r, &rect);
}

// The largest whole size from start down to the smallest at which the text
// is no wider than width.
float fittedSize(std::string_view text, float width, float start) noexcept {
  float size = start;
  while (size > display::kSmallestSize && textWidth(text, size) > width) {
    size -= 1.0F;
  }
  return size;
}

// What the course window shows, from the game or from the review tool.
struct CourseView {
  MissionControl::Phase phase = MissionControl::Phase::Idle;
  std::string_view name;
  std::string_view goal;
  int remaining = 0;
  int energy = 0;
};

CourseView courseView(const MissionControl& missions, const HudPreview* preview) noexcept {
  CourseView view;
  if (preview != nullptr && preview->course) {
    view = {MissionControl::Phase::Active, kPreviewCourse, kPreviewGoal, 255, 2};
    return view;
  }
  if (const Mission* mission = missions.mission()) {
    view.phase = missions.phase();
    view.name = mission->name;
    if (const MissionStage* stage = missions.stage()) {
      view.goal = goalText(stage->goal);
      view.remaining = missions.remaining();
      view.energy = missions.fuelLights();
    }
  }
  return view;
}

// The line about locks and balls in play. Empty when there is nothing to say.
std::string_view multiballLine(const MultiballView& multiball, std::span<char> buffer) noexcept {
  if (!multiball.known) {
    return {};
  }
  if (multiball.running) {
    return formatText(buffer.data(), buffer.size(), "MULTIBALL %d", multiball.ballsLive);
  }
  if (multiball.lockLit) {
    return "LOCK IS LIT";
  }
  if (multiball.locksHeld > 0) {
    return formatText(buffer.data(), buffer.size(), "LOCKS %d/%d", multiball.locksHeld,
                      kLocksForMultiball);
  }
  return {};
}

// Everything both displays show, gathered once.
struct Content {
  const Rules& rules;
  const HudExtras& extras;
  CourseView course;
  MultiballView multiball;
  std::string_view message;
  bool blinkOn;
};

// The game's name in the title area: the art if there is any, else its
// lettering, the last word on a smaller line of its own.
void drawTitle(SDL_Renderer* r, SDL_Texture* art, const screen::Rect& area) {
  if (art != nullptr) {
    // 480 x 120 units, made smaller if the area is narrower.
    const float scale = std::min({1.0F, area.w / 480.0F, area.h / 120.0F});
    const SDL_FRect dst{centerX(area) - 240.0F * scale, area.y + (area.h - 120.0F * scale) * 0.5F,
                        480.0F * scale, 120.0F * scale};
    SDL_RenderTexture(r, art, nullptr, &dst);
    return;
  }
  const std::string_view title = kGameTitle;
  const std::size_t space = title.rfind(' ');
  const bool twoLines = space != std::string_view::npos;
  const std::string_view first = twoLines ? title.substr(0, space) : title;
  const float size = std::max(fittedSize(first, area.w, 6.0F), 3.0F);
  drawNeonText(r, first, centerX(area), area.y + 18.0F, size, kWhite, kCyan, kPink,
               TextAlign::Center);
  if (twoLines) {
    drawNeonText(r, title.substr(space + 1), centerX(area), area.y + 78.0F, 3.0F, kAmber, kPink,
                 kViolet, TextAlign::Center);
  }
}

// A row of gauge segments, the first `lit` of them in the lit colour.
void drawSegments(SDL_Renderer* r, float x, float y, float width, float height, float gap,
                  int count, int lit, SDL_Color litColor) {
  for (int i = 0; i < count; ++i) {
    // The colour changes at most twice along the gauge: set it only then.
    if (i == 0 || i == lit) {
      setColor(r, i < lit ? litColor : kUnlit);
    }
    fill(r, x + static_cast<float>(i) * (width + gap), y, width, height);
  }
}

void drawRankGauge(SDL_Renderer* r, const screen::Rect& well, float y, int progress) {
  const float gap = 4.0F;
  const float segment = (well.w - 2.0F * kPad - gap * (kProgressPerRank - 1)) /
                        static_cast<float>(kProgressPerRank);
  drawSegments(r, well.x + kPad, y, segment, 8.0F, gap, kProgressPerRank, progress, kAmber);
}

// The energy lights: green, red and blinking when nearly out.
void drawEnergy(SDL_Renderer* r, float x, float y, float width, float height, float gap,
                int energy, bool blinkOn) {
  const bool low = energy <= 2;
  const int lit = low && !blinkOn ? 0 : energy;
  drawSegments(r, x, y, width, height, gap, MissionControl::kFuelLights, lit, low ? kRed : kGreen);
}

void drawMessage(SDL_Renderer* r, const screen::Rect& well, const Content& c, float largest) {
  if (c.message.empty()) {
    return;
  }
  const float size = fittedSize(c.message, well.w - 2.0F * kPad, largest);
  drawText(r, c.message, centerX(well), well.y + well.h * 0.5F - 3.5F * size, size,
           c.rules.gameOver() || c.rules.tilted() ? kRed : kAmber, TextAlign::Center);
}

void drawScore(SDL_Renderer* r, const screen::Rect& well, const Content& c, float labelY,
               float digitsY, float largest) {
  drawText(r, "SCORE", well.x + kPad, well.y + labelY, kLabelSize, kDim);
  ScoreText bestBuffer{};
  std::array<char, 48> bestLine{};
  const std::string_view best = formatScore(c.extras.bestScore, bestBuffer);
  const bool celebrate = c.extras.newHighScore && c.blinkOn;
  drawText(r,
           c.extras.newHighScore
               ? std::string_view("NEW HIGH SCORE")
               : formatText(bestLine.data(), bestLine.size(), "HIGH %.*s",
                            static_cast<int>(best.size()), best.data()),
           well.x + well.w - kPad, well.y + labelY, kLabelSize, celebrate ? kGreen : kDim,
           TextAlign::Right);
  ScoreText scoreBuffer{};
  const std::string_view score = formatScore(c.rules.score(), scoreBuffer);
  // Shrink the digits when the number gets long, so it always fits the well.
  const float size = fittedSize(score, well.w - 2.0F * kPad, largest);
  drawText(r, score, well.x + well.w - kPad, well.y + digitsY, size, kWhite, TextAlign::Right);
}

// The Classic panel, for the desktop and for a touch screen held sideways.
void drawPanel(SDL_Renderer* r, const HudArt& art, const screen::Layout& layout,
               const Content& c) {
  // The wells are laid out for a panel at panel::kLeft, 0. Move them to
  // where this layout has it.
  const float dx = layout.hud.x - panel::kLeft;
  const float dy = layout.hud.y;
  const auto placed = [dx, dy](const panel::Rect& well) noexcept {
    return panel::Rect{well.x + dx, well.y + dy, well.w, well.h};
  };
  const panel::Rect scoreWell = placed(panel::kScoreWell);
  const panel::Rect statusWell = placed(panel::kStatusWell);
  const panel::Rect rankWell = placed(panel::kRankWell);
  const panel::Rect missionWell = placed(panel::kMissionWell);
  const panel::Rect messageWell = placed(panel::kMessageWell);
  const panel::Rect systemsWell = placed(panel::kHighScoreWell);
  const bool desktop = layout.idiom == screen::Idiom::Desktop;

  const SDL_FRect area{layout.hud.x, layout.hud.y, panel::kWidth, panel::kHeight};
  if (art.panel != nullptr) {
    SDL_RenderTexture(r, art.panel, nullptr, &area);
  } else {
    SDL_SetRenderDrawColor(r, 24, 24, 30, SDL_ALPHA_OPAQUE);
    SDL_RenderFillRect(r, &area);
  }

  const Rules& rules = c.rules;
  const float middle = layout.hud.x + panel::kWidth * 0.5F;

  // A touch screen has its pause button in the corner of the title area.
  drawTitle(r, art.title, placed(desktop ? panel::kTitleArea : panel::kTouchTitleArea));

  // Score, with the best score beside the label.
  drawScore(r, scoreWell, c, 10.0F, 44.0F, 6.0F);

  // Ball and multiplier.
  std::array<char, 32> buffer{};
  drawText(r,
           formatText(buffer.data(), buffer.size(), "BALL %d OF %d", rules.ball(),
                      Rules::kBallsPerGame),
           statusWell.x + kPad, statusWell.y + 14.0F, 3.0F, kWhite);
  if (rules.extraBalls() > 0) {
    drawText(r, formatText(buffer.data(), buffer.size(), "EXTRA BALLS %d", rules.extraBalls()),
             statusWell.x + kPad, statusWell.y + 44.0F, kLabelSize, kGreen);
  }
  if (const std::string_view line = multiballLine(c.multiball, buffer); !line.empty()) {
    drawText(r, line, statusWell.x + 190.0F, statusWell.y + 44.0F, kLabelSize,
             c.multiball.running ? kAmber : kCyan);
  }
  drawText(r, formatText(buffer.data(), buffer.size(), "X%d", rules.fieldMultiplier()),
           statusWell.x + statusWell.w - kPad, statusWell.y + 14.0F, 5.0F,
           rules.fieldMultiplier() > 1 ? kAmber : kDim, TextAlign::Right);

  const MissionControl& missions = rules.missions();

  // Rank and progress towards the next one.
  label(r, rankWell, "RANK");
  drawText(r, rankName(missions.rank()), rankWell.x + kPad, rankWell.y + 30.0F,
           display::kRankSize, kCyan);
  drawRankGauge(r, rankWell, rankWell.y + rankWell.h - 18.0F, missions.progress());

  // Course.
  label(r, missionWell, "COURSE");
  const float missionX = missionWell.x + kPad;
  const float missionY = missionWell.y + 34.0F;
  const CourseView& course = c.course;
  if (course.phase != MissionControl::Phase::Idle) {
    drawText(r, course.name, missionX, missionY, display::kMissionSize, kAmber);
    if (course.phase == MissionControl::Phase::Selected) {
      if (c.blinkOn) {
        drawText(r, kEnrolHint, missionX, missionY + 34.0F, kLabelSize, kGreen);
      }
    } else if (!course.goal.empty()) {
      drawText(r, course.goal, missionX, missionY + 34.0F, kLabelSize, kWhite);
      drawText(r, formatText(buffer.data(), buffer.size(), "%d TO GO", course.remaining),
               missionWell.x + missionWell.w - kPad, missionY + 34.0F, kLabelSize, kWhite,
               TextAlign::Right);
      // Energy gauge.
      drawText(r, "ENERGY", missionX, missionY + 62.0F, kLabelSize, kDim);
      drawEnergy(r, missionX + 84.0F, missionY + 60.0F, 28.0F, 16.0F, 6.0F, course.energy,
                 c.blinkOn);
    }
  } else {
    drawText(r, kNoCourse, missionX, missionY, display::kMissionSize, kDim);
    drawText(r, kNoCourseHint, missionX, missionY + 34.0F, kLabelSize, kDim);
  }
  // Lit awards along the bottom of the course window.
  {
    const float y = missionWell.y + missionWell.h - 26.0F;
    const bool jackpot = rules.jackpotLit() || c.multiball.jackpotLit || c.multiball.superJackpotLit;
    drawText(r, "JACKPOT", missionX, y, kLabelSize, jackpot ? kAmber : kDim);
    if (c.multiball.known) {
      // With multiball there is a lock to show, next to the jackpot.
      drawText(r, "LOCK", missionX + 100.0F, y, kLabelSize, c.multiball.lockLit ? kAmber : kDim);
      drawText(r, "OUTLANE BONUS", missionX + 164.0F, y, kLabelSize,
               rules.bonusLaneLit() ? kAmber : kDim);
    } else {
      drawText(r, "OUTLANE BONUS", centerX(missionWell), y, kLabelSize,
               rules.bonusLaneLit() ? kAmber : kDim, TextAlign::Center);
    }
    drawText(r, "BALL SAVE", missionWell.x + missionWell.w - kPad, y, kLabelSize,
             rules.ballSaveActive() ? kGreen : kDim, TextAlign::Right);
  }

  // Systems: the levels of the four ladders, and the bonus so far.
  {
    const panel::Rect& well = systemsWell;
    const float left = well.x + kPad;
    const float right = well.x + well.w - kPad;
    std::array<char, 48> line{};
    drawText(r, formatText(line.data(), line.size(), "METEOR %d", rules.attackBumperLevel() + 1),
             left, well.y + 16.0F, kLabelSize, kWhite);
    drawText(r, formatText(line.data(), line.size(), "TURBO %d/4", rules.boosterLevel()),
             centerX(well), well.y + 16.0F, kLabelSize, kWhite, TextAlign::Center);
    drawText(r, formatText(line.data(), line.size(), "MERIT %d/3", rules.medalLevel()), right,
             well.y + 16.0F, kLabelSize, kWhite, TextAlign::Right);
    const std::string_view quasar =
        formatText(line.data(), line.size(), "QUASAR %d/4", rules.hyperspaceLevel());
    drawText(r, quasar, left, well.y + 42.0F, kLabelSize, kWhite);
    const float room = right - left - textWidth(quasar, kLabelSize) - 2.0F * kPad;
    ScoreText bonusBuffer{};
    const std::string_view bonus = formatScore(rules.bonus(), bonusBuffer);
    std::array<char, 48> bonusLine{};
    std::string_view text =
        formatText(bonusLine.data(), bonusLine.size(), "END OF BALL BONUS %.*s",
                   static_cast<int>(bonus.size()), bonus.data());
    if (textWidth(text, kLabelSize) > room) {
      // The largest bonus there is needs the shorter label.
      text = formatText(bonusLine.data(), bonusLine.size(), "BONUS %.*s",
                        static_cast<int>(bonus.size()), bonus.data());
    }
    drawText(r, text, right, well.y + 42.0F, kLabelSize, kDim, TextAlign::Right);
  }

  drawMessage(r, messageWell, c, 4.0F);

  // Controls. A touch screen has its buttons here instead.
  if (desktop) {
    drawText(r, "Z / FLIP   SPACE LAUNCH   X . UP NUDGE", middle, dy + 882.0F, kLabelSize, kDim,
             TextAlign::Center);
    drawText(r, "F1 ABOUT  F2 NEW  F3 PAUSE  F4 FULL", middle, dy + 904.0F, kLabelSize, kDim,
             TextAlign::Center);
    drawText(r, "M MUTE   N MUSIC", middle, dy + 926.0F, kLabelSize, kDim, TextAlign::Center);
  }
}

// A plain display window, for a strip that has no art yet.
void drawPlainWell(SDL_Renderer* r, const screen::Rect& well, SDL_Color rim) {
  setColor(r, rim);
  fill(r, well.x, well.y, well.w, well.h);
  SDL_SetRenderDrawColor(r, 12, 4, 34, SDL_ALPHA_OPAQUE);
  fill(r, well.x + 2.0F, well.y + 2.0F, well.w - 4.0F, well.h - 4.0F);
}

// A small labelled lamp: the word, bright when lit. Returns the x where the
// next one to its left ends.
float drawLamp(SDL_Renderer* r, std::string_view text, float right, float y, bool lit,
               SDL_Color litColor) {
  drawText(r, text, right, y, kLabelSize, lit ? litColor : kLampOff, TextAlign::Right);
  return right - textWidth(text, kLabelSize) - 8.0F;
}

// The Stack's strip above the table: the same facts in six small windows.
// It has no systems line and no title.
void drawStrip(SDL_Renderer* r, const HudArt& art, const screen::Layout& layout,
               const Content& c) {
  const float dx = layout.hud.x;
  const float dy = layout.hud.y;
  const auto placed = [dx, dy](const screen::Rect& well) noexcept {
    return screen::Rect{well.x + dx, well.y + dy, well.w, well.h};
  };
  const screen::Rect scoreWell = placed(strip::kScoreWell);
  const screen::Rect statusWell = placed(strip::kStatusWell);
  const screen::Rect rankWell = placed(strip::kRankWell);
  const screen::Rect missionWell = placed(strip::kMissionWell);
  const screen::Rect messageWell = placed(strip::kMessageWell);

  const SDL_FRect area{layout.hud.x, layout.hud.y, layout.hud.w, layout.hud.h};
  if (art.strip != nullptr) {
    SDL_RenderTexture(r, art.strip, nullptr, &area);
  } else {
    SDL_SetRenderDrawColor(r, 26, 10, 58, SDL_ALPHA_OPAQUE);
    SDL_RenderFillRect(r, &area);
    drawPlainWell(r, scoreWell, kCyan);
    drawPlainWell(r, statusWell, kPink);
    drawPlainWell(r, rankWell, kAmber);
    drawPlainWell(r, missionWell, kPink);
    drawPlainWell(r, messageWell, kCyan);
  }

  const Rules& rules = c.rules;
  std::array<char, 32> buffer{};

  drawScore(r, scoreWell, c, 8.0F, 32.0F, 6.0F);

  // Ball and multiplier, then extra balls, then locks or balls in play.
  drawText(r,
           formatText(buffer.data(), buffer.size(), "BALL %d OF %d", rules.ball(),
                      Rules::kBallsPerGame),
           statusWell.x + kPad, statusWell.y + 10.0F, kLabelSize, kWhite);
  drawText(r, formatText(buffer.data(), buffer.size(), "X%d", rules.fieldMultiplier()),
           statusWell.x + statusWell.w - kPad, statusWell.y + 8.0F, 3.0F,
           rules.fieldMultiplier() > 1 ? kAmber : kDim, TextAlign::Right);
  if (rules.extraBalls() > 0) {
    drawText(r, formatText(buffer.data(), buffer.size(), "EXTRA BALLS %d", rules.extraBalls()),
             statusWell.x + kPad, statusWell.y + 35.0F, kLabelSize, kGreen);
  }
  if (const std::string_view line = multiballLine(c.multiball, buffer); !line.empty()) {
    drawText(r, line, statusWell.x + kPad, statusWell.y + 60.0F, kLabelSize,
             c.multiball.running ? kAmber : kCyan);
  }

  // Rank and progress towards the next one.
  const MissionControl& missions = rules.missions();
  drawText(r, "RANK", rankWell.x + kPad, rankWell.y + 8.0F, kLabelSize, kDim);
  drawText(r, rankName(missions.rank()), rankWell.x + kPad, rankWell.y + 28.0F,
           display::kMissionSize, kCyan);
  drawRankGauge(r, rankWell, rankWell.y + 58.0F, missions.progress());

  // Course: name, goal and count, and along the bottom the energy and the
  // lit awards as lamps.
  const float missionX = missionWell.x + kPad;
  const float missionRight = missionWell.x + missionWell.w - kPad;
  const float goalY = missionWell.y + 34.0F;
  const float lampY = missionWell.y + 55.0F;
  const CourseView& course = c.course;
  if (course.phase != MissionControl::Phase::Idle) {
    drawText(r, course.name, missionX, missionWell.y + 8.0F, display::kMissionSize, kAmber);
    if (course.phase == MissionControl::Phase::Selected) {
      if (c.blinkOn) {
        drawText(r, kEnrolHint, missionX, goalY, kLabelSize, kGreen);
      }
    } else if (!course.goal.empty()) {
      drawText(r, course.goal, missionX, goalY, kLabelSize, kWhite);
      drawText(r, formatText(buffer.data(), buffer.size(), "%d TO GO", course.remaining),
               missionRight, goalY, kLabelSize, kWhite, TextAlign::Right);
      drawText(r, "ENERGY", missionX, lampY, kLabelSize, kDim);
      drawEnergy(r, missionX + 78.0F, lampY + 1.0F, 10.0F, 12.0F, 2.0F, course.energy, c.blinkOn);
    }
  } else {
    drawText(r, kNoCourse, missionX, missionWell.y + 8.0F, display::kMissionSize, kDim);
    drawText(r, kNoCourseHint, missionX, goalY, kLabelSize, kDim);
  }
  {
    float right = missionRight;
    right = drawLamp(r, "SAVE", right, lampY, rules.ballSaveActive(), kGreen);
    right = drawLamp(r, "OUTLANE", right, lampY, rules.bonusLaneLit(), kAmber);
    if (c.multiball.known) {
      right = drawLamp(r, "LOCK", right, lampY, c.multiball.lockLit, kAmber);
    }
    const bool jackpot = rules.jackpotLit() || c.multiball.jackpotLit || c.multiball.superJackpotLit;
    drawLamp(r, "JACKPOT", right, lampY, jackpot, kAmber);
  }

  drawMessage(r, messageWell, c, display::kMissionSize);
}

}  // namespace

void drawHud(SDL_Renderer* r, const HudArt& art, const Game& game, const screen::Layout& layout,
             const HudExtras& extras) {
  if (r == nullptr) {
    return;
  }
  const Rules& rules = game.rules();
  const HudPreview* const preview = extras.preview;
  Content content{.rules = rules,
                  .extras = extras,
                  .course = courseView(rules.missions(), preview),
                  .multiball = preview != nullptr && preview->multiball
                                   ? *preview->multiball
                                   : multiballView(rules),
                  .message = rules.message(),
                  // Slow blink for things waiting on the player.
                  .blinkOn = (game.stepCount() / 30U) % 2U == 0U};
  if (preview != nullptr && !preview->message.empty()) {
    content.message = preview->message;
  } else if (layout.idiom == screen::Idiom::Desktop && rules.gameOver() &&
             content.message == Rules::kGameOverMessage.text()) {
    // On a desktop a finished game also says which key starts the next one:
    // drawn here, never said by the rules. A touch screen has a button.
    content.message = kGameOverWithHint;
  }
  if (layout.composition == screen::Composition::Stack) {
    drawStrip(r, art, layout, content);
  } else {
    drawPanel(r, art, layout, content);
  }
}

}  // namespace pinball
