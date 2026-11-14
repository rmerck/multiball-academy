#include "platform/Hud.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "core/Courses.hpp"
#include "core/DisplayText.hpp"
#include "core/Format.hpp"
#include "core/PanelLayout.hpp"
#include "core/Version.hpp"
#include "generated/ArtPalette.hpp"
#include "platform/TableLights.hpp"
#include "platform/Text.hpp"

namespace pinball {

namespace {

// The art's palette (tools/art/palette.py) on the dark glass of the
// windows: yellow digits, cream text, orchid labels (kHudLettering).
constexpr SDL_Color sdl(art::Rgb c) noexcept { return {c.r, c.g, c.b, 255}; }
constexpr SDL_Color kAmber = kHudLettering[0];
constexpr SDL_Color kCyan = kHudLettering[1];
constexpr SDL_Color kWhite = kHudLettering[2];
constexpr SDL_Color kDim = kHudLettering[3];
constexpr SDL_Color kGreen = kHudLettering[4];
constexpr SDL_Color kPink = kHudLettering[5];
// A word or a letter whose lamp is out: still readable.
constexpr SDL_Color kLampOff = kHudLettering[6];
constexpr SDL_Color kViolet = sdl(art::kViolet);
// Icons: an unlit pip, door or meter is the glass's highlight colour.
constexpr SDL_Color kUnlit = sdl(art::kWellHi);
constexpr SDL_Color kGlass = sdl(art::kWell);
constexpr SDL_Color kOrange = sdl(art::kOrange);
constexpr SDL_Color kSteel = sdl(art::kChMid);
constexpr SDL_Color kChrome = sdl(art::kChHi);
constexpr SDL_Color kOrchid = sdl(art::kOrchid);
constexpr SDL_Color kMeterGreen = sdl(art::kGreen);

using display::kPad;
// Every line of the display is at least this size (8 points at the
// smallest scene); the limits in core/DisplayText.hpp are worked out at it.
constexpr float kSmall = display::kSmallestSize;
static_assert(kSmall == screen::kHudTextSize);
constexpr float kRowHeight = kSmall * screen::kFontRows;  // 17.5 units.

// What the desktop adds to the game-over message, and what a touch screen
// adds: its centre button says PLAY then. Each pair is held to the message
// window like any message.
constexpr std::string_view kGameOverDesktop = "GAME OVER   PRESS F2";
constexpr std::string_view kGameOverTouch = "GAME OVER   TAP PLAY";
static_assert(kGameOverDesktop.starts_with(Rules::kGameOverMessage.text()) &&
              kGameOverTouch.starts_with(Rules::kGameOverMessage.text()));
static_assert(display::fits(kGameOverDesktop, display::kMaxMessageLength) &&
              display::fits(kGameOverTouch, display::kMaxMessageLength));

// Fixed lines of the course window, each held to the width it is drawn in.
constexpr std::string_view kNoCourse = "NO COURSE";
static_assert(display::fits(kNoCourse, display::kMaxMissionNameLength));

// The review tool's fullest course window: every line at its limit.
constexpr std::string_view kPreviewCourse = "COURSE NAME AT THE EDGE";
constexpr std::string_view kPreviewGoal = "GOAL LINE AT ITS FULL SIZE";
static_assert(kPreviewCourse.size() == display::kMaxMissionNameLength &&
              kPreviewGoal.size() == display::kMaxGoalTextLength);

// The fullest rows of the status windows, drawn as one part at the left
// edge and one at the right: at the smallest size the two keep at least
// two font pixels apart (checked here, so that a narrower window is a build
// error, not an overlap).
constexpr float widthAtSmallest(std::string_view text) noexcept {
  return (static_cast<float>(text.size() * display::kAdvance) - 1.0F) * kSmall;
}
constexpr bool rowFits(std::string_view left, std::string_view right,
                       const screen::Rect& well) noexcept {
  return widthAtSmallest(left) + 2.0F * kSmall + widthAtSmallest(right) <=
         display::innerWidth(well);
}
static_assert(rowFits("BALL 3 OF 3", "+1", strip::kStatusWell));
static_assert(rowFits("3X >>>>>", "SAVE", strip::kStatusWell));
static_assert(rowFits("WING", "KICKBACK", strip::kStatusWell));
static_assert(rowFits("BALL 3 OF 3  EXTRA BALL", "SAVE", panel::kStatusWell));
static_assert(rowFits("3X >>>>>    WING", "KICKBACK", panel::kStatusWell));
static_assert(widthAtSmallest("HIGH 999,999,999,999") <= display::innerWidth(strip::kScoreWell));
static_assert(widthAtSmallest("SUPER JACKPOT 1,500,000") <=
              display::innerWidth(strip::kMessageWell));

float centerX(const screen::Rect& r) noexcept { return r.x + r.w * 0.5F; }

void fill(SDL_Renderer* r, SDL_Color c, float x, float y, float w, float h) {
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
  SDL_SetRenderDrawColor(r, c.r, c.g, c.b, SDL_ALPHA_OPAQUE);
  const SDL_FRect rect{x, y, w, h};
  SDL_RenderFillRect(r, &rect);
}

// A filled round lamp, drawn as one row of the disc per unit.
void disc(SDL_Renderer* r, SDL_Color c, float cx, float cy, float radius) {
  std::array<SDL_FRect, 48> rows{};
  const int count = std::min(static_cast<int>(std::ceil(radius * 2.0F)), static_cast<int>(rows.size()));
  for (int i = 0; i < count; ++i) {
    const float dy = -radius + static_cast<float>(i) + 0.5F;
    const float half = std::sqrt(std::max(0.0F, radius * radius - dy * dy));
    rows[static_cast<std::size_t>(i)] = {cx - half, cy - radius + static_cast<float>(i), 2.0F * half,
                                         1.0F};
  }
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
  SDL_SetRenderDrawColor(r, c.r, c.g, c.b, SDL_ALPHA_OPAQUE);
  SDL_RenderFillRects(r, rows.data(), count);
}

// A diamond, the Graduation lamp's shape.
void diamond(SDL_Renderer* r, SDL_Color c, float cx, float cy, float half) {
  std::array<SDL_FRect, 48> rows{};
  const int count = std::min(static_cast<int>(std::ceil(half * 2.0F)), static_cast<int>(rows.size()));
  for (int i = 0; i < count; ++i) {
    const float dy = -half + static_cast<float>(i) + 0.5F;
    const float w = half - std::fabs(dy);
    rows[static_cast<std::size_t>(i)] = {cx - w, cy - half + static_cast<float>(i), 2.0F * w, 1.0F};
  }
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
  SDL_SetRenderDrawColor(r, c.r, c.g, c.b, SDL_ALPHA_OPAQUE);
  SDL_RenderFillRects(r, rows.data(), count);
}

// The largest size from start down to the smallest, in half steps, at which
// the text is no wider than width.
float fittedSize(std::string_view text, float width, float start) noexcept {
  float size = start;
  while (size > kSmall && textWidth(text, size) > width) {
    size -= 0.5F;
  }
  return std::max(size, kSmall);
}

// Text whose top is placed so that its foot is at bottom.
void textAbove(SDL_Renderer* r, std::string_view text, float x, float bottom, float size,
               SDL_Color c, TextAlign align = TextAlign::Left) {
  drawText(r, text, x, bottom - size * screen::kFontRows, size, c, align);
}

// --- What the display shows, gathered once a frame ---------------------------

struct CourseLine {
  // A course or Graduation Day is running.
  bool running = false;
  std::string_view name;
  std::string_view goal;
  // Of the goal: how many in all, and how many made. 0 in all: no count.
  int total = 0;
  int made = 0;
  // Whole seconds left on its clock; negative for none.
  int seconds = -1;
  // TURBINE THEORY: what passing pays now; else 0.
  std::uint64_t hurryUp = 0;
};

CourseLine courseLine(const Rules& rules, const HudPreview* preview) noexcept {
  CourseLine line;
  if (preview != nullptr && preview->course) {
    line.running = true;
    line.name = kPreviewCourse;
    line.goal = kPreviewGoal;
    line.total = 12;
    line.made = 5;
    line.seconds = 35;
    return line;
  }
  if (rules.gameOver()) {
    return line;
  }
  line.goal = rules.goalText();
  if (rules.graduationRunning()) {
    line.running = true;
    line.name = rules.courseName();
    const int left = rules.goalRemaining();
    if (left > 0) {
      line.total = static_cast<int>(kShotCount);
      line.made = line.total - left;
    }
    return line;
  }
  if (rules.courseRunning()) {
    line.running = true;
    line.name = rules.courseName();
    const Course& course = kCourses[std::min(rules.course(), kCourseCount - 1)];
    // HANGAR SAFETY's second part is one shot: the Hangar.
    line.total = line.goal == kGoalEnterHangar ? 1 : course.goal;
    line.made = std::clamp(line.total - rules.goalRemaining(), 0, line.total);
    line.seconds = static_cast<int>(std::ceil(rules.courseSecondsLeft()));
    line.hurryUp = rules.hurryUpValue();
    return line;
  }
  // Nothing running: the course the Briefing room would start, or Graduation
  // Day, and what lights it.
  if (rules.graduationLit()) {
    line.name = kGraduationName;
  } else if (rules.coursesPassed() < static_cast<int>(kCourseCount)) {
    line.name = kCourses[std::min(rules.courseOffered(), kCourseCount - 1)].name;
  } else {
    line.name = kNoCourse;
  }
  const int ramps = rules.rampsForBriefing();
  if (ramps > 0) {
    line.total = ramps;  // Shown as the ramps still to make.
  }
  return line;
}

struct Status {
  int ball = 1;
  int extraBalls = 0;
  int overdrive = 1;
  bool overdriveLit = false;
  int combo = 0;
  bool save = false;
  bool saveEnding = false;
  std::array<bool, 4> wing{};
  bool kickback = false;
  std::size_t rank = 0;
  // Bay doors marked and whether the bank is down; bays taken; lock lit.
  std::array<bool, table::kDropTargets.size()> doors{};
  bool doorsDown = false;
  std::array<bool, Rules::kLocksNeeded> bays{};
  bool lockLit = false;
  double turbo = 0.0;
  bool turboLamp = false;
  bool multiball = false;
  bool jackpotLit = false;
  bool superLit = false;
  std::uint64_t jackpot = 0;  // What the next jackpot pays, overdrive included.
  // Six course lamps and Graduation Day's, as the table shows them.
  std::array<bool, kCourseCount> courses{};
  bool graduation = false;
};

Status statusOf(const Game& game) noexcept {
  const Rules& rules = game.rules();
  const TableLights lights = tableLights(game);
  Status s;
  s.ball = rules.ball();
  s.extraBalls = rules.extraBalls();
  s.overdrive = rules.overdrive();
  s.overdriveLit = rules.overdriveLit();
  s.combo = rules.comboLevel();
  s.save = rules.ballSaveActive();
  s.saveEnding = s.save && rules.ballSaveSecondsLeft() < Rules::kBallSaveWarningSeconds &&
                 rules.graceSecondsLeft() <= 0.0;
  using table::LampId;
  s.wing = {lights.lit(LampId::WingW), lights.lit(LampId::WingI), lights.lit(LampId::WingN),
            lights.lit(LampId::WingG)};
  s.kickback = rules.kickbackLamp();
  s.rank = rules.rank();
  for (std::size_t i = 0; i < s.doors.size(); ++i) {
    s.doors[i] = lights.doorsMarked[i];
  }
  s.doorsDown = !game.doorBankUp();
  for (std::size_t bay = 0; bay < s.bays.size(); ++bay) {
    s.bays[bay] = game.bayTaken(bay);
  }
  s.lockLit = rules.lockLit();
  s.turbo = std::clamp(rules.turboFraction(), 0.0, 1.0);
  s.turboLamp = lights.lit(LampId::TurboCharge);
  s.multiball = rules.multiball() != Multiball::None;
  s.jackpotLit = rules.multiballJackpotLit();
  s.superLit = rules.superJackpotLit();
  const std::uint64_t base = s.superLit ? points::kSuperJackpot : rules.jackpotValue();
  s.jackpot = base * static_cast<std::uint64_t>(std::max(rules.overdrive(), 1));
  for (std::size_t i = 0; i < kCourseCount; ++i) {
    s.courses[i] = lights.lit(static_cast<LampId>(static_cast<std::size_t>(LampId::Course1) + i));
  }
  s.graduation = lights.lit(LampId::Graduation);
  return s;
}

// Everything both displays show.
struct Content {
  const Rules& rules;
  const HudExtras& extras;
  Status status;
  CourseLine course;
  std::string_view message;
  bool blinkOn;
  bool fastBlinkOn;
};

// --- Pieces both displays draw ---------------------------------------------------

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
  const float size = std::max(std::floor(fittedSize(first, area.w, 6.0F)), 3.0F);
  drawNeonText(r, first, centerX(area), area.y + 18.0F, size, kWhite, kCyan, kPink,
               TextAlign::Center);
  if (twoLines) {
    drawNeonText(r, title.substr(space + 1), centerX(area), area.y + 78.0F, 3.0F, kAmber, kPink,
                 kViolet, TextAlign::Center);
  }
}

// The message, as large as fits from largest down. Without one: in a
// multiball, what the next jackpot pays.
void drawMessage(SDL_Renderer* r, const screen::Rect& well, const Content& c, float largest,
                 bool jackpotWhenIdle) {
  std::array<char, 40> buffer{};
  std::string_view text = c.message;
  SDL_Color colour = c.rules.gameOver() || c.rules.tilted() ? kPink : kAmber;
  if (text.empty() && jackpotWhenIdle && c.status.multiball &&
      (c.status.jackpotLit || c.status.superLit)) {
    ScoreText value{};
    const std::string_view number = formatScore(c.status.jackpot, value);
    text = formatText(buffer.data(), buffer.size(), "%s %.*s",
                      c.status.superLit ? "SUPER JACKPOT" : "JACKPOT",
                      static_cast<int>(number.size()), number.data());
    colour = c.blinkOn ? kAmber : kWhite;
  }
  if (text.empty()) {
    return;
  }
  const float size = fittedSize(text, well.w - 2.0F * kPad, largest);
  drawText(r, text, centerX(well), well.y + (well.h - size * screen::kFontRows) * 0.5F, size,
           colour, TextAlign::Center);
}

// The best score so far, or the news that this game beat it.
std::string_view bestLine(const Content& c, std::span<char> buffer, std::string_view label) {
  if (c.extras.newHighScore) {
    return "NEW HIGH SCORE";
  }
  ScoreText best{};
  const std::string_view number = formatScore(c.extras.bestScore, best);
  return formatText(buffer.data(), buffer.size(), "%.*s %.*s", static_cast<int>(label.size()),
                    label.data(), static_cast<int>(number.size()), number.data());
}

// The score's digits, right-aligned on bottom, as large as fits.
void drawDigits(SDL_Renderer* r, const screen::Rect& well, const Content& c, float bottom,
                float largest) {
  ScoreText buffer{};
  const std::string_view score = formatScore(c.rules.score(), buffer);
  const float size = fittedSize(score, well.w - 2.0F * kPad, largest);
  textAbove(r, score, well.x + well.w - kPad, bottom, size, kAmber, TextAlign::Right);
}

// A pip for each rank, those reached lit. Right edge at right.
void drawRankPips(SDL_Renderer* r, const Content& c, float right, float cy, float radius,
                  float spacing) {
  for (std::size_t i = 0; i < kRankCount; ++i) {
    const float cx = right - radius - spacing * static_cast<float>(kRankCount - 1 - i);
    disc(r, i <= c.status.rank ? kAmber : kUnlit, cx, cy, radius);
  }
}

// W-I-N-G, each letter as bright as its lamp on the table.
float drawWing(SDL_Renderer* r, const Content& c, float x, float y, float size) {
  constexpr std::string_view kLetters = "WING";
  for (std::size_t i = 0; i < kLetters.size(); ++i) {
    drawText(r, kLetters.substr(i, 1), x + static_cast<float>(i) * 6.0F * size, y, size,
             c.status.wing[i] ? kAmber : kLampOff);
  }
  return textWidth(kLetters, size);
}

// The multiplier ("2X", blinking while overdrive is lit and not started)
// and the combo chain as one chevron a link.
void drawMultiplier(SDL_Renderer* r, const Content& c, float x, float y, float size) {
  const Status& s = c.status;
  std::array<char, 8> buffer{};
  if (s.overdrive > 1 || (s.overdriveLit && c.blinkOn)) {
    drawText(r, formatText(buffer.data(), buffer.size(), "%dX", std::max(s.overdrive, 2)), x, y,
             size, s.overdrive > 1 ? kPink : kLampOff);
  }
  if (s.combo > 1) {
    constexpr std::string_view kChevrons = ">>>>>";
    const std::string_view chain = kChevrons.substr(0, static_cast<std::size_t>(std::min(s.combo, 5)));
    drawText(r, chain, x + textWidth("3X ", size) + size, y, size, kAmber);
  }
}

// Ball saver: SAVE, blinking fast for its last seconds.
void drawSave(SDL_Renderer* r, const Content& c, float right, float y, float size) {
  if (c.status.save && (!c.status.saveEnding || c.fastBlinkOn)) {
    drawText(r, "SAVE", right, y, size, kGreen, TextAlign::Right);
  }
}

// The bay doors, three small doors (marked: orange; standing and not:
// steel; dropped: lying flat), then the two lock bays (a ball in each that
// holds one; ringed while the lock is lit).
void drawLockIcons(SDL_Renderer* r, const Content& c, float x, float y, float doorW, float doorH,
                   float gap, float bayRadius) {
  const Status& s = c.status;
  for (std::size_t i = 0; i < s.doors.size(); ++i) {
    const float dx = x + static_cast<float>(i) * (doorW + gap);
    const SDL_Color face = s.doors[i] ? kOrange : kSteel;
    if (s.doorsDown) {
      fill(r, face, dx, y + doorH - 4.0F, doorW, 4.0F);
    } else {
      fill(r, face, dx, y, doorW, doorH);
      fill(r, kGlass, dx + 3.0F, y + 3.0F, doorW - 6.0F, 2.0F);
    }
  }
  float cx = x + 3.0F * (doorW + gap) + gap + bayRadius;
  const float cy = y + doorH * 0.5F;
  for (const bool taken : s.bays) {
    if (taken) {
      disc(r, kChrome, cx, cy, bayRadius);
    } else {
      disc(r, s.lockLit && c.blinkOn ? kOrange : kUnlit, cx, cy, bayRadius);
      disc(r, kGlass, cx, cy, bayRadius - 3.0F);
    }
    cx += 2.0F * bayRadius + gap;
  }
}

// The turbo meter: green to the share filled, the rest unlit; its lamp's
// blink shows at the tip.
void drawTurbo(SDL_Renderer* r, const Content& c, float x, float y, float w, float h) {
  fill(r, kUnlit, x, y, w, h);
  const float filled = std::round(w * static_cast<float>(c.status.turbo));
  if (filled > 0.0F) {
    fill(r, kMeterGreen, x, y, filled, h);
    if (c.status.turboLamp) {
      fill(r, kChrome, x + filled - std::min(4.0F, filled), y, std::min(4.0F, filled), h);
    }
  }
}

// The course window's progress: one segment for each step of a short goal,
// a bar for a long one.
void drawProgress(SDL_Renderer* r, const CourseLine& line, float x, float y, float w, float h) {
  if (line.total <= 0) {
    return;
  }
  const int total = line.total;
  const int made = std::clamp(line.made, 0, total);
  if (total <= 12) {
    const float gap = 4.0F;
    const float seg = (w - gap * static_cast<float>(total - 1)) / static_cast<float>(total);
    for (int i = 0; i < total; ++i) {
      fill(r, i < made ? kOrchid : kUnlit, x + static_cast<float>(i) * (seg + gap), y, seg, h);
    }
    return;
  }
  fill(r, kUnlit, x, y, w, h);
  fill(r, kOrchid, x, y, std::round(w * static_cast<float>(made) / static_cast<float>(total)), h);
}

// The six course lamps and the Graduation diamond, as the table's.
void drawCourseLamps(SDL_Renderer* r, const Content& c, float x, float cy, float spacing,
                     float radius) {
  for (std::size_t i = 0; i < kCourseCount; ++i) {
    const float cx = x + radius + spacing * static_cast<float>(i);
    disc(r, c.status.courses[i] ? kOrchid : kUnlit, cx, cy, radius);
  }
  const float cx = x + radius + spacing * static_cast<float>(kCourseCount);
  diamond(r, c.status.graduation ? kAmber : kUnlit, cx, cy, radius + 1.0F);
  if (!c.status.graduation) {
    diamond(r, kGlass, cx, cy, radius - 2.0F);
  }
}

// The course window's two lines: name and clock, goal and count.
void drawCourseLines(SDL_Renderer* r, const screen::Rect& well, const Content& c, float nameY,
                     float goalY, float nameSize) {
  const CourseLine& line = c.course;
  const float left = well.x + kPad;
  const float right = well.x + well.w - kPad;
  const float inner = well.w - 2.0F * kPad;
  std::array<char, 24> buffer{};
  if (!line.name.empty()) {
    float room = inner;
    if (line.seconds >= 0) {
      const std::string_view clock =
          formatText(buffer.data(), buffer.size(), "%d", std::clamp(line.seconds, 0, 99));
      drawText(r, clock, right, nameY, nameSize, line.seconds <= 5 && !c.blinkOn ? kPink : kWhite,
               TextAlign::Right);
      room -= textWidth("99 ", nameSize);
    }
    drawText(r, line.name, left, nameY, fittedSize(line.name, room, nameSize),
             line.running ? kOrchid : kLampOff);
  }
  if (line.goal.empty()) {
    return;
  }
  drawText(r, line.goal, left, goalY, kSmall, line.running ? kWhite : kDim);
  // Beside the goal: what passing pays (the hurry-up), or the count, where
  // there is room for it.
  std::string_view count;
  ScoreText value{};
  if (line.hurryUp > 0) {
    count = formatScore(line.hurryUp, value);
  } else if (line.total > 0) {
    count = line.running ? formatText(buffer.data(), buffer.size(), "%d/%d", line.made, line.total)
                         : formatText(buffer.data(), buffer.size(), "%d", line.total);
  }
  if (!count.empty() &&
      textWidth(line.goal, kSmall) + textWidth(" ", kSmall) + textWidth(count, kSmall) <= inner) {
    drawText(r, count, right, goalY, kSmall, line.running ? kAmber : kDim, TextAlign::Right);
  }
}

// --- The Classic panel, for the desktop and a touch screen held sideways ------

void drawPanel(SDL_Renderer* r, const HudArt& art, const screen::Layout& layout,
               const Content& c) {
  // The wells are laid out for a panel at panel::kLeft, 0. Move them to
  // where this layout has it.
  const float dx = layout.hud.x - panel::kLeft;
  const float dy = layout.hud.y;
  const auto placed = [dx, dy](const panel::Rect& well) noexcept {
    return panel::Rect{well.x + dx, well.y + dy, well.w, well.h};
  };
  const panel::Rect score = placed(panel::kScoreWell);
  const panel::Rect status = placed(panel::kStatusWell);
  const panel::Rect rank = placed(panel::kRankWell);
  const panel::Rect course = placed(panel::kCourseWell);
  const panel::Rect meter = placed(panel::kMeterWell);
  const panel::Rect message = placed(panel::kMessageWell);
  const panel::Rect high = placed(panel::kHighScoreWell);
  const bool desktop = layout.idiom == screen::Idiom::Desktop;

  const SDL_FRect area{layout.hud.x, layout.hud.y, panel::kWidth, panel::kHeight};
  if (art.panel != nullptr) {
    SDL_RenderTexture(r, art.panel, nullptr, &area);
  } else {
    fill(r, {24, 24, 30, 255}, area.x, area.y, area.w, area.h);
    for (const panel::Rect& well : panel::kWells) {
      const panel::Rect w = placed(well);
      fill(r, kUnlit, w.x, w.y, w.w, w.h);
      fill(r, kGlass, w.x + 2.0F, w.y + 2.0F, w.w - 4.0F, w.h - 4.0F);
    }
  }
  // A touch screen has its pause button in the corner of the title area.
  drawTitle(r, art.title, placed(desktop ? panel::kTitleArea : panel::kTouchTitleArea));

  const float left = score.x + kPad;
  const float right = score.x + score.w - kPad;
  const Status& s = c.status;
  std::array<char, 40> buffer{};

  // Score.
  drawText(r, "SCORE", left, score.y + 12.0F, kSmall, kDim);
  drawDigits(r, score, c, score.y + 88.0F, 6.0F);

  // Status: ball, extra ball, saver; multiplier and combo, W-I-N-G, kickback.
  {
    const float y1 = status.y + 8.0F;
    const float y2 = status.y + status.h - 8.0F - kRowHeight;
    drawText(r,
             formatText(buffer.data(), buffer.size(), "BALL %d OF %d", s.ball,
                        Rules::kBallsPerGame),
             left, y1, kSmall, kWhite);
    if (s.extraBalls > 0) {
      drawText(r, "EXTRA BALL", left + textWidth("BALL 3 OF 3  ", kSmall), y1, kSmall, kAmber);
    }
    drawSave(r, c, right, y1, kSmall);
    drawMultiplier(r, c, left, y2, kSmall);
    const float wingX = left + textWidth("3X >>>>>    ", kSmall);
    drawWing(r, c, wingX, y2, kSmall);
    drawText(r, "KICKBACK", right, y2, kSmall, s.kickback ? kGreen : kLampOff, TextAlign::Right);
  }

  // Rank: its name, and a pip for each rank.
  drawText(r, "RANK", left, rank.y + 10.0F, kSmall, kDim);
  textAbove(r, rankName(s.rank), left, rank.y + rank.h - 14.0F, 4.0F, kCyan);
  drawRankPips(r, c, right, rank.y + rank.h - 28.0F, 9.0F, 26.0F);

  // Course: name and clock, goal and count, progress, the course lamps.
  drawCourseLines(r, course, c, course.y + 12.0F, course.y + 44.0F, 3.0F);
  drawProgress(r, c.course, left, course.y + 72.0F, right - left, 10.0F);
  drawCourseLamps(r, c, left + 8.0F, course.y + 120.0F, (right - left - 40.0F) / 6.0F, 12.0F);

  // Lock and turbo on the left, the jackpot on the right.
  {
    const float row1 = meter.y + 10.0F;
    const float row2 = meter.y + meter.h - 10.0F - kRowHeight;
    drawText(r, "LOCK", left, row1, kSmall, s.lockLit ? kOrange : kDim);
    drawLockIcons(r, c, left + 72.0F, row1 - 2.0F, 16.0F, 22.0F, 6.0F, 10.0F);
    drawText(r, "TURBO", left, row2, kSmall, kDim);
    drawTurbo(r, c, left + 92.0F, row2 + 1.0F, 120.0F, kRowHeight - 2.0F);
    const bool lit = s.multiball && (s.jackpotLit || s.superLit);
    drawText(r, s.superLit ? "SUPER JACKPOT" : "JACKPOT", right, row1, kSmall,
             lit ? kAmber : kLampOff, TextAlign::Right);
    ScoreText value{};
    textAbove(r, formatScore(s.jackpot, value), right, meter.y + meter.h - 10.0F, 4.0F,
              lit ? kAmber : kLampOff, TextAlign::Right);
  }

  drawMessage(r, message, c, 4.0F, false);

  // The best score.
  {
    const std::string_view line = bestLine(c, buffer, "HIGH SCORE");
    const bool news = c.extras.newHighScore;
    drawText(r, line, centerX(high), high.y + (high.h - 3.0F * screen::kFontRows) * 0.5F,
             fittedSize(line, high.w - 2.0F * kPad, 3.0F), news && c.blinkOn ? kGreen : kCyan,
             TextAlign::Center);
  }

  // Controls. A touch screen has its buttons here instead.
  if (desktop) {
    const float middle = layout.hud.x + panel::kWidth * 0.5F;
    if (art.panel != nullptr) {
      // The art has the touch buttons' sockets here. The keys are listed on
      // a window like the others instead: one of the art's windows, with
      // its chrome lip, stretched over the area and a little wider than
      // the windows above, so the longest line clears the lip. The status
      // window is the source: nothing of the art's ornament lies by it.
      // Two pixels to the unit.
      constexpr float kLip = 6.0F;
      constexpr float kCorner = 28.0F;  // Pixels: the lip and the glass's round corner.
      constexpr float kMargin = 10.0F;  // From the panel's sides to the lip.
      const panel::Rect& source = panel::kStatusWell;
      const SDL_FRect src{(source.x - kLip - panel::kLeft) * 2.0F, (source.y - kLip) * 2.0F,
                          (source.w + 2.0F * kLip) * 2.0F, (source.h + 2.0F * kLip) * 2.0F};
      const SDL_FRect dst{layout.hud.x + kMargin, dy + 874.0F, panel::kWidth - 2.0F * kMargin,
                          panel::kHeight - 876.0F};
      SDL_RenderTexture9Grid(r, art.panel, &src, kCorner, kCorner, kCorner, kCorner, 0.5F, &dst);
    }
    constexpr float kKeys = screen::kDesktopKeyTextSize;
    drawText(r, "Z / FLIP   SPACE LAUNCH   X . UP NUDGE", middle, dy + 882.0F, kKeys, kDim,
             TextAlign::Center);
    drawText(r, "F1 ABOUT  F2 NEW  F3 PAUSE  F4 FULL", middle, dy + 904.0F, kKeys, kDim,
             TextAlign::Center);
    drawText(r, "M MUTE   N MUSIC", middle, dy + 926.0F, kKeys, kDim, TextAlign::Center);
  }
}

// --- The Stack's strip, above the table on a phone -------------------------------

void drawStrip(SDL_Renderer* r, const HudArt& art, const screen::Layout& layout,
               const Content& c) {
  const float dx = layout.hud.x;
  const float dy = layout.hud.y;
  const auto placed = [dx, dy](const screen::Rect& well) noexcept {
    return screen::Rect{well.x + dx, well.y + dy, well.w, well.h};
  };
  const screen::Rect score = placed(strip::kScoreWell);
  const screen::Rect status = placed(strip::kStatusWell);
  const screen::Rect rank = placed(strip::kRankWell);
  const screen::Rect course = placed(strip::kCourseWell);
  const screen::Rect meter = placed(strip::kMeterWell);
  const screen::Rect message = placed(strip::kMessageWell);
  const screen::Rect lamps = placed(strip::kLampWell);

  const SDL_FRect area{layout.hud.x, layout.hud.y, layout.hud.w, layout.hud.h};
  if (art.strip != nullptr) {
    SDL_RenderTexture(r, art.strip, nullptr, &area);
  } else {
    fill(r, {26, 10, 58, 255}, area.x, area.y, area.w, area.h);
    for (const screen::Rect& well : strip::kWells) {
      const screen::Rect w = placed(well);
      fill(r, kUnlit, w.x, w.y, w.w, w.h);
      fill(r, kGlass, w.x + 2.0F, w.y + 2.0F, w.w - 4.0F, w.h - 4.0F);
    }
  }

  const Status& s = c.status;
  std::array<char, 40> buffer{};

  // Score: the best score small above it.
  {
    if (c.extras.bestScore > 0 || c.extras.newHighScore) {
      const std::string_view best = bestLine(c, buffer, "HIGH");
      drawText(r, best, score.x + kPad, score.y + 8.0F, kSmall,
               c.extras.newHighScore && c.blinkOn ? kGreen : kDim);
    }
    drawDigits(r, score, c, score.y + score.h - 9.0F, 5.0F);
  }

  // Status, three rows: ball and extra balls; multiplier, combo and saver;
  // W-I-N-G and the kickback.
  {
    const float left = status.x + kPad;
    const float right = status.x + status.w - kPad;
    const float step = (status.h - 3.0F * kRowHeight) / 4.0F;
    const float y1 = status.y + step;
    const float y2 = y1 + kRowHeight + step;
    const float y3 = y2 + kRowHeight + step;
    drawText(r,
             formatText(buffer.data(), buffer.size(), "BALL %d OF %d", s.ball,
                        Rules::kBallsPerGame),
             left, y1, kSmall, kWhite);
    if (s.extraBalls > 0) {
      drawText(r, formatText(buffer.data(), buffer.size(), "+%d", s.extraBalls), right, y1, kSmall,
               kAmber, TextAlign::Right);
    }
    drawMultiplier(r, c, left, y2, kSmall);
    drawSave(r, c, right, y2, kSmall);
    drawWing(r, c, left, y3, kSmall);
    drawText(r, "KICKBACK", right, y3, kSmall, s.kickback ? kGreen : kLampOff, TextAlign::Right);
  }

  // Rank: its name, a pip for each rank under it.
  drawText(r, rankName(s.rank), rank.x + kPad, rank.y + 9.0F,
           fittedSize(rankName(s.rank), rank.w - 2.0F * kPad, 3.0F), kCyan);
  drawRankPips(r, c, rank.x + rank.w - kPad - 1.0F, rank.y + rank.h - 17.0F, 7.0F, 20.0F);

  // Course: name and clock, goal and count, progress along the foot.
  drawCourseLines(r, course, c, course.y + 7.0F, course.y + 30.0F, kSmall);
  drawProgress(r, c.course, course.x + kPad, course.y + course.h - 15.0F, course.w - 2.0F * kPad,
               7.0F);

  // Meters, icons only: bay doors and lock bays above, turbo below.
  drawLockIcons(r, c, meter.x + kPad - 2.0F, meter.y + 10.0F, 14.0F, 22.0F, 5.0F, 8.5F);
  drawTurbo(r, c, meter.x + kPad - 2.0F, meter.y + 44.0F, meter.w - 2.0F * kPad + 4.0F, 14.0F);

  drawMessage(r, message, c, 3.0F, true);

  // The course lamps, as on the table.
  drawCourseLamps(r, c, lamps.x + kPad, lamps.y + lamps.h * 0.5F, 26.0F, 8.0F);
}

}  // namespace

void drawHud(SDL_Renderer* r, const HudArt& art, const Game& game, const screen::Layout& layout,
             const HudExtras& extras) {
  if (r == nullptr) {
    return;
  }
  const Rules& rules = game.rules();
  const HudPreview* const preview = extras.preview;
  const std::uint64_t step = game.stepCount();
  Content content{.rules = rules,
                  .extras = extras,
                  .status = statusOf(game),
                  .course = courseLine(rules, preview),
                  .message = rules.message(),
                  .blinkOn = lampLit(LampState::Blink, step),
                  .fastBlinkOn = lampLit(LampState::FastBlink, step)};
  if (preview != nullptr && !preview->message.empty()) {
    content.message = preview->message;
  } else if (rules.gameOver() && content.message == Rules::kGameOverMessage.text()) {
    // A finished game also says how the next one starts: drawn here, never
    // said by the rules, because it depends on the device.
    content.message =
        layout.idiom == screen::Idiom::Desktop ? kGameOverDesktop : kGameOverTouch;
  }
  if (layout.composition == screen::Composition::Stack) {
    drawStrip(r, art, layout, content);
  } else {
    drawPanel(r, art, layout, content);
  }
}

}  // namespace pinball
