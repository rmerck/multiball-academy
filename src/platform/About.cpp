#include "platform/About.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>

#include "core/Format.hpp"
#include "core/Missions.hpp"
#include "core/OverlayLayout.hpp"
#include "core/Version.hpp"
#include "platform/Notices.hpp"
#include "platform/Text.hpp"
#include "platform/TouchOverlay.hpp"

namespace pinball {

namespace {

// Neon palette shared with the panel: laser yellow, cyan, hot pink.
constexpr SDL_Color kAmber{255, 225, 70, 255};
constexpr SDL_Color kCyan{70, 235, 255, 255};
constexpr SDL_Color kWhite{248, 240, 255, 255};
constexpr SDL_Color kDim{176, 150, 226, 255};
constexpr SDL_Color kPink{255, 60, 170, 255};
constexpr SDL_Color kViolet{150, 90, 255, 255};

// Where the source lives. The screen names no person.
constexpr std::string_view kSource = "GITHUB.COM/RMERCK";
constexpr std::string_view kCredit =
    "AN ORIGINAL GAME BY RMERCK. ALL ART AND SOUND ARE ORIGINAL.";
// The same in two lines, for the narrow dialog.
constexpr std::string_view kCreditLines =
    "AN ORIGINAL GAME BY RMERCK.\nALL ART AND SOUND ARE ORIGINAL.";
// What the game does with the player's data.
constexpr std::string_view kPrivacy =
    "THIS GAME COLLECTS NOTHING AND SENDS NOTHING. IT HAS NO ACCOUNTS, NO ADS AND NO "
    "ANALYTICS, AND IT NEVER GOES ONLINE. YOUR HIGH SCORES STAY ON THIS DEVICE.";
constexpr std::string_view kPolicyNote =
    "THE PRIVACY POLICY IS IN THE PROJECT'S DOCS FOLDER AT THE ADDRESS ABOVE.";

// Body text: the smallest size, and the distance from one line to the next.
constexpr float kBodySize = 2.0F;
constexpr float kLineStep = 20.0F;
constexpr float kAdvance = 6.0F * kBodySize;

// Characters of body text that fit a width.
std::size_t charsAcross(float width) noexcept {
  return width < kAdvance ? 1 : static_cast<std::size_t>((width + kBodySize) / kAdvance);
}

// Breaks text into lines of at most maxChars and hands each to fn with its
// number. A line feed ends a paragraph; an empty paragraph is an empty line.
// Lines break at spaces, and a word longer than a line is cut. Returns the
// number of lines.
template <typename Fn>
std::size_t wrapLines(std::string_view text, std::size_t maxChars, Fn&& fn) {
  maxChars = std::max<std::size_t>(maxChars, 1);
  std::size_t line = 0;
  while (true) {
    const std::size_t feed = text.find('\n');
    std::string_view paragraph = text.substr(0, feed);
    do {
      std::size_t take = paragraph.size();
      std::size_t skip = 0;
      if (take > maxChars) {
        // The last space that leaves the line short enough, if there is one.
        const std::size_t space = paragraph.rfind(' ', maxChars);
        take = space == std::string_view::npos || space == 0 ? maxChars : space;
        skip = paragraph[take] == ' ' ? 1 : 0;
      }
      fn(line, paragraph.substr(0, take));
      ++line;
      paragraph.remove_prefix(take + skip);
    } while (!paragraph.empty());
    if (feed == std::string_view::npos) {
      return line;
    }
    text.remove_prefix(feed + 1);
  }
}

// Draws wrapped body text centred on x from y down. Returns the y below it.
float drawParagraph(SDL_Renderer* r, std::string_view text, float x, float y, float width,
                    SDL_Color color) {
  const std::size_t lines =
      wrapLines(text, charsAcross(width), [&](std::size_t line, std::string_view part) {
        drawText(r, part, x, y + static_cast<float>(line) * kLineStep, kBodySize, color,
                 TextAlign::Center);
      });
  return y + static_cast<float>(lines) * kLineStep;
}

float fittedSize(std::string_view text, float width, float start) noexcept {
  float size = start;
  while (size > 2.0F && textWidth(text, size) > width) {
    size -= 1.0F;
  }
  return size;
}

bool isTouch(const screen::Layout& layout) noexcept {
  return layout.idiom == screen::Idiom::Touch;
}

// Where the licence text goes. A touch screen has no privacy button on that
// page, so the text takes its row as well.
screen::Rect licenceArea(const screen::Layout& layout) noexcept {
  const screen::AboutLayout about = screen::aboutLayout(layout);
  if (isTouch(layout)) {
    const float bottom = about.privacyLink.y + about.privacyLink.h;
    // The last line is the page count.
    return {about.body.x, about.body.y, about.body.w, bottom - about.body.y - kLineStep};
  }
  const screen::Rect& box = about.box;
  return {box.x + 90.0F, box.y + 166.0F, box.w - 180.0F, box.h - 166.0F - 136.0F};
}

std::size_t linesPerPage(const screen::Rect& area) noexcept {
  return std::max<std::size_t>(static_cast<std::size_t>(area.h / kLineStep), 1);
}

// The title, the version and build, and a rule under them. Returns the y
// below the rule.
float drawHeading(SDL_Renderer* r, float middle, float y, float width, float largest) {
  const float size = fittedSize(kGameTitle, width, largest);
  drawNeonText(r, kGameTitle, middle, y, size, kAmber, kPink, {120, 30, 200, 255},
               TextAlign::Center);
  y += 64.0F;
  std::array<char, 48> version{};
  drawText(r, formatText(version.data(), version.size(), "VERSION %s", kGameVersion), middle, y,
           2.0F, kDim, TextAlign::Center);
  drawNeonRule(r, middle, y + 24.0F, 300.0F);
  return y + 54.0F;
}

// The high score table from y down, centred on middle.
void drawScoreTable(SDL_Renderer* r, const HighScores& scores, float middle, float y,
                    bool narrow) {
  drawNeonText(r, "HIGH SCORES", middle, y, 3.0F, kWhite, kCyan, {200, 40, 150, 255},
               TextAlign::Center);
  y += 38.0F;
  if (scores.size() == 0) {
    drawText(r, "NO GAMES FINISHED YET", middle, y, 2.0F, kDim, TextAlign::Center);
    return;
  }
  // Place, initials, score and rank. The narrow dialog draws them closer.
  const float place = middle - (narrow ? 318.0F : 330.0F);
  const float scoreRight = middle + (narrow ? 58.0F : 60.0F);
  const float rank = middle + (narrow ? 84.0F : 100.0F);
  for (std::size_t i = 0; i < scores.size(); ++i) {
    const HighScore entry = scores.at(i);
    ScoreText scoreBuffer{};
    const std::string_view score = formatScore(entry.score, scoreBuffer);
    std::array<char, 8> number{};
    drawText(r, formatText(number.data(), number.size(), "%u.", static_cast<unsigned>(i + 1)),
             place, y, 3.0F, kDim);
    drawText(r, std::string_view(entry.initials.data(), entry.initials.size()), place + 45.0F, y,
             3.0F, kPink);
    drawText(r, score, scoreRight, y, 3.0F, kWhite, TextAlign::Right);
    drawText(r, rankName(entry.rank), rank, y, 3.0F, kAmber);
    y += 36.0F;
  }
}

// The About page's facts from y down: build, source, and the privacy
// statement. Returns the y below them.
float drawFacts(SDL_Renderer* r, float middle, float y, float width) {
  std::array<char, 48> build{};
  drawText(r, formatText(build.data(), build.size(), "BUILD %s", kBuildNumber), middle, y, 2.0F,
           kDim, TextAlign::Center);
  y += 34.0F;
  drawText(r, "SOURCE AND SUPPORT", middle, y, 2.0F, kDim, TextAlign::Center);
  y += 26.0F;
  drawText(r, kSource, middle, y, 3.0F, kAmber, TextAlign::Center);
  y += 48.0F;
  drawNeonText(r, "YOUR PRIVACY", middle, y, 3.0F, kWhite, kCyan, {200, 40, 150, 255},
               TextAlign::Center);
  y += 38.0F;
  y = drawParagraph(r, kPrivacy, middle, y, width, kWhite);
  return y + 14.0F;
}

// One page of the licence text, and under it which page it is.
void drawLicences(SDL_Renderer* r, const screen::Layout& layout, std::size_t page) {
  const screen::Rect area = licenceArea(layout);
  const std::size_t perPage = linesPerPage(area);
  const std::size_t pages = licencePageCount(layout);
  page = std::min(page, pages - 1);
  const std::size_t first = page * perPage;
  const float middle = area.x + area.w * 0.5F;
  wrapLines(licenceText(), charsAcross(area.w), [&](std::size_t line, std::string_view part) {
    if (line >= first && line < first + perPage) {
      drawText(r, part, middle, area.y + static_cast<float>(line - first) * kLineStep, kBodySize,
               kWhite, TextAlign::Center);
    }
  });
  if (isTouch(layout)) {
    std::array<char, 32> count{};
    drawText(r,
             formatText(count.data(), count.size(), "PAGE %u OF %u",
                        static_cast<unsigned>(page + 1), static_cast<unsigned>(pages)),
             middle, area.y + area.h + 4.0F, kBodySize, kDim, TextAlign::Center);
  }
}

// The dialog as the desktop has always had it: ornaments, the heading, the
// page, and along the foot the credit and the keys.
void drawDesktop(SDL_Renderer* r, const HighScores& scores, const screen::Layout& layout,
                 const AboutState& state) {
  const screen::Rect dialog = screen::aboutLayout(layout).box;
  const SDL_FRect box{dialog.x, dialog.y, dialog.w, dialog.h};
  drawNeonFrame(r, box);

  const float middle = box.x + box.w * 0.5F;
  // Ornaments in the table's style: zigzags down the margins and, on the
  // first page, a small sunset over a grid between the scores and the foot.
  drawNeonZigzag(r, box.x + 46.0F, box.y + 120.0F, box.y + box.h - 120.0F, kPink);
  drawNeonZigzag(r, box.x + box.w - 46.0F, box.y + 120.0F, box.y + box.h - 120.0F, kCyan);
  if (state.page == AboutPage::Scores) {
    drawNeonSunset(r, {middle - 300.0F, box.y + box.h - 258.0F, 600.0F, 118.0F});
  }
  float y = drawHeading(r, middle, box.y + 36.0F, box.w - 120.0F, 6.0F);

  const std::size_t licencePages = licencePageCount(layout);
  std::size_t number = 1;
  switch (state.page) {
    case AboutPage::Scores:
      drawText(r, "SOURCE", middle, y, 2.0F, kDim, TextAlign::Center);
      y += 26.0F;
      drawText(r, kSource, middle, y, 3.0F, kAmber, TextAlign::Center);
      y += 96.0F;
      drawScoreTable(r, scores, middle, y, false);
      break;
    case AboutPage::About:
      number = 2;
      y = drawFacts(r, middle, y + 12.0F, box.w - 220.0F);
      drawParagraph(r, kPolicyNote, middle, y, box.w - 220.0F, kDim);
      break;
    case AboutPage::Licences:
      number = 3 + std::min(state.licencePage, licencePages - 1);
      drawLicences(r, layout, state.licencePage);
      break;
  }

  drawNeonRule(r, middle, box.y + box.h - 124.0F, 300.0F);
  drawText(r, kCredit, middle, box.y + box.h - 100.0F, 2.0F, kDim, TextAlign::Center);
  std::array<char, 64> pages{};
  drawText(r,
           formatText(pages.data(), pages.size(), "LEFT AND RIGHT ARROWS: PAGE %u OF %u",
                      static_cast<unsigned>(number), static_cast<unsigned>(2 + licencePages)),
           middle, box.y + box.h - 78.0F, 2.0F, kDim, TextAlign::Center);
  drawText(r, "PRESS F1 TO CLOSE", middle, box.y + box.h - 42.0F, 3.0F, kWhite, TextAlign::Center);
}

// The dialog with the controls a touch screen needs.
void drawTouch(SDL_Renderer* r, const HighScores& scores, const screen::Layout& layout,
               const AboutState& state) {
  const screen::AboutLayout about = screen::aboutLayout(layout);
  drawNeonFrame(r, {about.box.x, about.box.y, about.box.w, about.box.h});

  // The tabs: the page that is up looks held down.
  constexpr std::array<std::string_view, 3> kTabs{"SCORES", "ABOUT", "LICENCES"};
  for (std::size_t i = 0; i < kTabs.size(); ++i) {
    const bool current = static_cast<std::size_t>(state.page) == i;
    drawCaptionButton(r, about.tabs[i], kTabs[i], current ? kAmber : kViolet, current, 3.0F);
  }

  const screen::Rect& body = about.body;
  const float middle = body.x + body.w * 0.5F;
  switch (state.page) {
    case AboutPage::Scores: {
      const float y = drawHeading(r, middle, body.y + 8.0F, body.w, 6.0F);
      drawScoreTable(r, scores, middle, y + 8.0F, body.w < 700.0F);
      drawParagraph(r, textWidth(kCredit, kBodySize) <= body.w ? kCredit : kCreditLines, middle,
                    about.privacyLink.y + 30.0F, body.w, kDim);
      break;
    }
    case AboutPage::About: {
      const float y = drawHeading(r, middle, body.y + 8.0F, body.w, 6.0F);
      drawFacts(r, middle, y, body.w);
      if (privacyButtonShown(layout, state)) {
        drawCaptionButton(r, about.privacyLink, "PRIVACY POLICY", kCyan, false, 3.0F);
      } else {
        drawParagraph(r, kPolicyNote, middle, about.privacyLink.y + 30.0F, body.w, kDim);
      }
      break;
    }
    case AboutPage::Licences:
      drawLicences(r, layout, state.licencePage);
      break;
  }

  if (pageButtonsShown(layout, state)) {
    drawCaptionButton(r, about.previous, "<", kViolet, false, 5.0F);
    drawCaptionButton(r, about.next, ">", kViolet, false, 5.0F);
  }
  drawCaptionButton(r, about.close, "CLOSE", kPink, false, 4.0F);
}

}  // namespace

std::string_view licenceText() noexcept { return notices::kText; }

std::size_t licencePageCount(const screen::Layout& layout) noexcept {
  const screen::Rect area = licenceArea(layout);
  const std::size_t lines =
      wrapLines(licenceText(), charsAcross(area.w), [](std::size_t, std::string_view) {});
  const std::size_t perPage = linesPerPage(area);
  return std::max<std::size_t>((lines + perPage - 1) / perPage, 1);
}

bool privacyButtonShown(const screen::Layout& layout, const AboutState& state) noexcept {
  return isTouch(layout) && state.page == AboutPage::About && kPrivacyUrl[0] != '\0';
}

bool pageButtonsShown(const screen::Layout& layout, const AboutState& state) noexcept {
  return isTouch(layout) && state.page == AboutPage::Licences && licencePageCount(layout) > 1;
}

void drawAbout(SDL_Renderer* r, const HighScores& scores, const screen::Layout& layout,
               const AboutState& state) {
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

  if (isTouch(layout)) {
    drawTouch(r, scores, layout, state);
  } else {
    drawDesktop(r, scores, layout, state);
  }
}

}  // namespace pinball
