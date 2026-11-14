#include "platform/About.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>

#include "core/Format.hpp"
#include "core/Courses.hpp"
#include "core/OverlayLayout.hpp"
#include "core/Version.hpp"
#include "generated/ArtPalette.hpp"
#include "platform/Notices.hpp"
#include "platform/Text.hpp"
#include "platform/TouchOverlay.hpp"

namespace pinball {

namespace {

// The art's palette (tools/art/palette.py). Body text stays the light
// kCaptionPlain, which the layout check looks for.
constexpr SDL_Color sdl(art::Rgb c) noexcept { return {c.r, c.g, c.b, 255}; }
constexpr SDL_Color kAmber = sdl(art::kYellow);
constexpr SDL_Color kCyan = sdl(art::kAqua);
constexpr SDL_Color kWhite = kCaptionPlain;
constexpr SDL_Color kDim = sdl(art::kOrchid);
constexpr SDL_Color kPink = sdl(art::kPink);
constexpr SDL_Color kViolet = sdl(art::kViolet);
constexpr SDL_Color kTeal = sdl(art::kTeal);
constexpr SDL_Color kGreyed{70, 56, 104, 255};

constexpr std::string_view kCredit =
    "AN ORIGINAL GAME BY RMERCK. ALL ART AND SOUND ARE ORIGINAL.";
// The same in two lines, for the narrow dialog.
constexpr std::string_view kCreditLines =
    "AN ORIGINAL GAME BY RMERCK.\nALL ART AND SOUND ARE ORIGINAL.";
// What the game does with the player's data.
constexpr std::string_view kPrivacy =
    "THIS GAME COLLECTS NOTHING AND SENDS NOTHING. IT HAS NO ACCOUNTS, NO ADS AND NO "
    "ANALYTICS, AND IT NEVER GOES ONLINE. YOUR HIGH SCORES STAY ON THIS DEVICE.";

// The build's address for the privacy policy. Empty if it has none: then
// nothing on the screen refers to one.
std::string_view privacyUrl() noexcept { return kPrivacyUrl; }

// Body text in a layout: its size (the smallest that is large enough on
// that screen), the distance from one line to the next, and how many
// characters fit a width.
struct Body {
  float size;
  float step;

  explicit Body(const screen::Layout& layout) noexcept
      : size(screen::smallestTextSize(layout)), step(size * 10.0F) {}
  [[nodiscard]] std::size_t across(float width) const noexcept {
    const float advance = 6.0F * size;
    return width < advance ? 1 : static_cast<std::size_t>((width + size) / advance);
  }
};

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
float drawParagraph(SDL_Renderer* r, const Body& body, std::string_view text, float x, float y,
                    float width, SDL_Color color) {
  const std::size_t lines =
      wrapLines(text, body.across(width), [&](std::size_t line, std::string_view part) {
        drawText(r, part, x, y + static_cast<float>(line) * body.step, body.size, color,
                 TextAlign::Center);
      });
  return y + static_cast<float>(lines) * body.step;
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
// page, so the text takes its row as well, less a line for the page count.
screen::Rect licenceArea(const screen::Layout& layout) noexcept {
  const screen::AboutLayout about = screen::aboutLayout(layout);
  if (isTouch(layout)) {
    const float bottom = about.privacyLink.y + about.privacyLink.h;
    return {about.body.x, about.body.y, about.body.w,
            bottom - about.body.y - Body(layout).step};
  }
  const screen::Rect& box = about.box;
  return {box.x + 90.0F, box.y + 166.0F, box.w - 180.0F, box.h - 166.0F - 136.0F};
}

// How the licence text is paged in a layout: as few pages as hold it, with
// the lines shared out evenly so the last page is never a line or two.
struct Paging {
  std::size_t pages;
  std::size_t perPage;
};

Paging licencePaging(const screen::Layout& layout) noexcept {
  const screen::Rect area = licenceArea(layout);
  const Body body(layout);
  const std::size_t lines =
      wrapLines(licenceText(), body.across(area.w), [](std::size_t, std::string_view) {});
  const std::size_t most = std::max<std::size_t>(static_cast<std::size_t>(area.h / body.step), 1);
  const std::size_t pages = std::max<std::size_t>((lines + most - 1) / most, 1);
  return {pages, (lines + pages - 1) / pages};
}

// The title, the version, and a rule under them. Returns the y below the
// rule.
float drawHeading(SDL_Renderer* r, const OverlayArt& art, const Body& body, float middle,
                  float y, float width, float largest) {
  // The title lockup in the heading's 64 units, or the name in lettering.
  if (!drawTitleArt(r, art.title, {middle - width * 0.5F, y - 10.0F, width, 60.0F})) {
    const float size = fittedSize(kGameTitle, width, largest);
    drawNeonText(r, kGameTitle, middle, y, size, kAmber, kPink, kViolet, TextAlign::Center);
  }
  y += 64.0F;
  std::array<char, 48> version{};
  drawText(r, formatText(version.data(), version.size(), "VERSION %s", kGameVersion), middle, y,
           body.size, kDim, TextAlign::Center);
  y += 7.0F * body.size + 10.0F;
  drawDialogRule(r, art, middle, y, 300.0F);
  return y + 30.0F;
}

// The high score table from y down, centred on middle.
void drawScoreTable(SDL_Renderer* r, const Body& body, const HighScores& scores, float middle,
                    float y, bool narrow) {
  drawNeonText(r, "HIGH SCORES", middle, y, 3.0F, kWhite, kCyan, kViolet,
               TextAlign::Center);
  y += 38.0F;
  if (scores.size() == 0) {
    drawText(r, "NO GAMES FINISHED YET", middle, y, body.size, kDim, TextAlign::Center);
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

// The About page's facts from y down: the build and the privacy statement,
// and where the policy is if the build knows and showAddress asks for it.
void drawFacts(SDL_Renderer* r, const Body& body, float middle, float y, float width,
               bool showAddress) {
  std::array<char, 48> build{};
  drawText(r, formatText(build.data(), build.size(), "BUILD %s", kBuildNumber), middle, y,
           body.size, kDim, TextAlign::Center);
  y += 7.0F * body.size + 20.0F;
  drawNeonText(r, "YOUR PRIVACY", middle, y, 3.0F, kWhite, kCyan, kViolet,
               TextAlign::Center);
  y += 38.0F;
  y = drawParagraph(r, body, kPrivacy, middle, y, width, kWhite) + 14.0F;
  if (showAddress && !privacyUrl().empty()) {
    drawText(r, "PRIVACY POLICY", middle, y, body.size, kDim, TextAlign::Center);
    drawParagraph(r, body, privacyUrl(), middle, y + body.step, width, kAmber);
  }
}

// One page of the licence text, set from the left like any legal text.
void drawLicences(SDL_Renderer* r, const screen::Layout& layout, std::size_t page) {
  const screen::Rect area = licenceArea(layout);
  const Body body(layout);
  const Paging paging = licencePaging(layout);
  page = std::min(page, paging.pages - 1);
  const std::size_t first = page * paging.perPage;
  wrapLines(licenceText(), body.across(area.w), [&](std::size_t line, std::string_view part) {
    if (line >= first && line < first + paging.perPage) {
      drawText(r, part, area.x, area.y + static_cast<float>(line - first) * body.step, body.size,
               kWhite);
    }
  });
  if (isTouch(layout)) {
    std::array<char, 32> count{};
    drawText(r,
             formatText(count.data(), count.size(), "PAGE %u OF %u",
                        static_cast<unsigned>(page + 1), static_cast<unsigned>(paging.pages)),
             area.x + area.w * 0.5F, area.y + area.h + 4.0F, body.size, kDim, TextAlign::Center);
  }
}

// The dialog as the desktop has always had it: ornaments, the heading, the
// page, and along the foot the credit and the keys.
void drawDesktop(SDL_Renderer* r, const HighScores& scores, const screen::Layout& layout,
                 const AboutState& state, const OverlayArt& art) {
  const Body body(layout);
  const screen::Rect dialog = screen::aboutLayout(layout).box;
  const SDL_FRect box{dialog.x, dialog.y, dialog.w, dialog.h};
  drawDialogCard(r, art, box);

  const float middle = box.x + box.w * 0.5F;
  if (art.dialog == nullptr) {
    // Without the card of art, ornaments drawn in code: zigzags down the
    // margins and, on the first page, a small sunset over a grid.
    drawNeonZigzag(r, box.x + 46.0F, box.y + 120.0F, box.y + box.h - 120.0F, kPink);
    drawNeonZigzag(r, box.x + box.w - 46.0F, box.y + 120.0F, box.y + box.h - 120.0F, kCyan);
    if (state.page == AboutPage::Scores) {
      drawNeonSunset(r, {middle - 300.0F, box.y + box.h - 258.0F, 600.0F, 118.0F});
    }
  }
  const float y = drawHeading(r, art, body, middle, box.y + 36.0F, box.w - 120.0F, 6.0F);

  const std::size_t licencePages = licencePageCount(layout);
  switch (state.page) {
    case AboutPage::Scores:
      drawScoreTable(r, body, scores, middle, y + 122.0F, false);
      break;
    case AboutPage::About:
      drawFacts(r, body, middle, y + 12.0F, box.w - 220.0F, true);
      break;
    case AboutPage::Licences:
      drawLicences(r, layout, state.licencePage);
      break;
  }

  drawDialogRule(r, art, middle, box.y + box.h - 124.0F, 300.0F);
  drawText(r, kCredit, middle, box.y + box.h - 100.0F, 2.0F, kDim, TextAlign::Center);
  std::array<char, 64> pages{};
  drawText(r,
           formatText(pages.data(), pages.size(), "LEFT AND RIGHT ARROWS: PAGE %u OF %u",
                      static_cast<unsigned>(aboutPageNumber(state, licencePages) + 1),
                      static_cast<unsigned>(aboutPageTotal(licencePages))),
           middle, box.y + box.h - 78.0F, 2.0F, kDim, TextAlign::Center);
  drawText(r, "PRESS F1 TO CLOSE", middle, box.y + box.h - 42.0F, 3.0F, kWhite, TextAlign::Center);
}

// A page-turning button; greyed, and without its shadow, when it leads
// nowhere.
void drawTurnButton(SDL_Renderer* r, const ButtonArt& art, const screen::Rect& face,
                    std::string_view arrow, bool enabled) {
  if (enabled) {
    drawCaptionButton(r, face, arrow, kButtonOrchid, false, 5.0F, 3.0F, art);
  } else if (art.face != nullptr) {
    drawCaptionButton(r, face, arrow, kButtonOff, false, 5.0F, 3.0F, art);
  } else {
    drawCaptionButton(r, face, arrow, kGreyed, true, 5.0F, 3.0F, art);
  }
}

// The dialog with the controls a touch screen needs.
void drawTouch(SDL_Renderer* r, const HighScores& scores, const screen::Layout& layout,
               const AboutState& state, const OverlayArt& art) {
  const Body text(layout);
  const screen::AboutLayout about = screen::aboutLayout(layout);
  drawDialogCard(r, art, {about.box.x, about.box.y, about.box.w, about.box.h});

  // The tabs: the page that is up looks held down.
  constexpr std::array<std::string_view, 3> kTabs{"SCORES", "ABOUT", "LICENCES"};
  for (std::size_t i = 0; i < kTabs.size(); ++i) {
    const bool current = static_cast<std::size_t>(state.page) == i;
    drawCaptionButton(r, about.tabs[i], kTabs[i], current ? kAmber : kButtonOrchid, current, 3.0F,
                      3.0F, art.button);
  }

  const screen::Rect& body = about.body;
  const float middle = body.x + body.w * 0.5F;
  switch (state.page) {
    case AboutPage::Scores: {
      const float y = drawHeading(r, art, text, middle, body.y + 8.0F, body.w, 6.0F);
      drawScoreTable(r, text, scores, middle, y, body.w < 700.0F);
      drawParagraph(r, text, textWidth(kCredit, text.size) <= body.w ? kCredit : kCreditLines,
                    middle, about.privacyLink.y + 24.0F, body.w, kDim);
      break;
    }
    case AboutPage::About: {
      const float y = drawHeading(r, art, text, middle, body.y + 8.0F, body.w, 6.0F);
      // The button is the address here; the text of it is for a desktop.
      drawFacts(r, text, middle, y, body.w, false);
      if (privacyButtonShown(layout, state)) {
        drawCaptionButton(r, about.privacyLink, "PRIVACY POLICY", kTeal, false, 3.0F, 3.0F,
                          art.button);
      }
      break;
    }
    case AboutPage::Licences:
      drawLicences(r, layout, state.licencePage);
      break;
  }

  if (pageButtonsShown(layout, state)) {
    const std::size_t pages = licencePageCount(layout);
    const std::size_t page = std::min(state.licencePage, pages - 1);
    drawTurnButton(r, art.button, about.previous, "<", page > 0);
    drawTurnButton(r, art.button, about.next, ">", page + 1 < pages);
  }
  drawCaptionButton(r, about.close, "CLOSE", kPink, false, 4.0F, 3.0F, art.button);
}

}  // namespace

std::string_view licenceText() noexcept { return notices::kText; }

std::size_t licencePageCount(const screen::Layout& layout) noexcept {
  return licencePaging(layout).pages;
}

bool privacyButtonShown(const screen::Layout& layout, const AboutState& state) noexcept {
  return isTouch(layout) && state.page == AboutPage::About && !privacyUrl().empty();
}

bool pageButtonsShown(const screen::Layout& layout, const AboutState& state) noexcept {
  return isTouch(layout) && state.page == AboutPage::Licences && licencePageCount(layout) > 1;
}

void drawAbout(SDL_Renderer* r, const HighScores& scores, const screen::Layout& layout,
               const AboutState& state, const OverlayArt& art) {
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

  if (isTouch(layout)) {
    drawTouch(r, scores, layout, state, art);
  } else {
    drawDesktop(r, scores, layout, state, art);
  }
}

}  // namespace pinball
