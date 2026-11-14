#include "platform/FrontScreens.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

#include "core/Courses.hpp"
#include "core/Format.hpp"
#include "core/FrontLayout.hpp"
#include "core/HowToText.hpp"
#include "core/TextWrap.hpp"
#include "core/Version.hpp"
#include "generated/ArtPalette.hpp"
#include "platform/Font5x7.hpp"
#include "platform/Text.hpp"

namespace pinball {

namespace {

constexpr SDL_Color sdl(art::Rgb c, Uint8 a = 255) noexcept { return {c.r, c.g, c.b, a}; }
constexpr SDL_Color kYellow = sdl(art::kYellow);
constexpr SDL_Color kOrange = sdl(art::kOrange);
constexpr SDL_Color kTeal = sdl(art::kTeal);
constexpr SDL_Color kPink = sdl(art::kPink);
constexpr SDL_Color kViolet = sdl(art::kViolet);
constexpr SDL_Color kOrchid = sdl(art::kOrchid);
constexpr SDL_Color kAqua = sdl(art::kAqua);
constexpr SDL_Color kCream = sdl(art::kCream);
constexpr SDL_Color kInk = sdl(art::kInk);
// The dot display: amber on near black (ART_DIRECTION 3, AMBER).
constexpr SDL_Color kDotLit{255, 154, 31, 255};
constexpr SDL_Color kDotUnlit{58, 26, 6, 255};
constexpr SDL_Color kDotGlass{20, 8, 2, 255};
// Body text: the light caption colour, which the layout check looks for.
constexpr SDL_Color kBody = kCaptionPlain;

constexpr double clamp01(double v) noexcept { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

SDL_FRect frect(const screen::Rect& r) noexcept { return {r.x, r.y, r.w, r.h}; }

void fillRect(SDL_Renderer* r, const screen::Rect& rect, SDL_Color c) {
  SDL_SetRenderDrawBlendMode(r, c.a == 255 ? SDL_BLENDMODE_NONE : SDL_BLENDMODE_BLEND);
  SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a);
  const SDL_FRect f = frect(rect);
  SDL_RenderFillRect(r, &f);
  SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_NONE);
}

void outlineRect(SDL_Renderer* r, const screen::Rect& rect, float width, SDL_Color c) {
  fillRect(r, {rect.x, rect.y, rect.w, width}, c);
  fillRect(r, {rect.x, rect.y + rect.h - width, rect.w, width}, c);
  fillRect(r, {rect.x, rect.y + width, width, rect.h - 2.0F * width}, c);
  fillRect(r, {rect.x + rect.w - width, rect.y + width, width, rect.h - 2.0F * width}, c);
}

// The keyboard's focus: a yellow ring just outside the control.
void drawFocus(SDL_Renderer* r, const screen::Rect& face) {
  outlineRect(r, {face.x - 9.0F, face.y - 9.0F, face.w + 18.0F, face.h + 18.0F}, 5.0F, kYellow);
}

// --- The title's logo -------------------------------------------------------

// The title lockup's texture in pixels, and its drawn shape (4:1).
constexpr float kTitlePixelsW = 960.0F;
constexpr float kTitlePixelsH = 240.0F;

// Part of the lockup: the texture's columns [u0, u1) and rows [v0, v1) as
// fractions, drawn where they fall in dst.
void drawTitlePart(SDL_Renderer* r, SDL_Texture* title, const screen::Rect& dst, float u0,
                   float u1, float v0, float v1) {
  u0 = std::clamp(u0, 0.0F, 1.0F);
  u1 = std::clamp(u1, 0.0F, 1.0F);
  v0 = std::clamp(v0, 0.0F, 1.0F);
  v1 = std::clamp(v1, 0.0F, 1.0F);
  if (u1 <= u0 || v1 <= v0) {
    return;
  }
  const SDL_FRect src{u0 * kTitlePixelsW, v0 * kTitlePixelsH, (u1 - u0) * kTitlePixelsW,
                      (v1 - v0) * kTitlePixelsH};
  const SDL_FRect to{dst.x + u0 * dst.w, dst.y + v0 * dst.h, (u1 - u0) * dst.w,
                     (v1 - v0) * dst.h};
  SDL_RenderTexture(r, title, &src, &to);
}

// The same part again as light: added over what is there, in the colour
// and strength given.
void lightTitlePart(SDL_Renderer* r, SDL_Texture* title, const screen::Rect& dst, float u0,
                    float u1, float v0, float v1, SDL_Color color, float strength) {
  if (strength <= 0.0F) {
    return;
  }
  SDL_SetTextureBlendMode(title, SDL_BLENDMODE_ADD);
  SDL_SetTextureColorMod(title, color.r, color.g, color.b);
  SDL_SetTextureAlphaModFloat(title, std::min(strength, 1.0F));
  drawTitlePart(r, title, dst, u0, u1, v0, v1);
  SDL_SetTextureAlphaModFloat(title, 1.0F);
  SDL_SetTextureColorMod(title, 255, 255, 255);
  SDL_SetTextureBlendMode(title, SDL_BLENDMODE_BLEND);
}

// The logo builds itself: it scans in from the top behind a bright line,
// flashes once, and from then on a light runs across it every few seconds.
void drawLogo(SDL_Renderer* r, const screen::Rect& logo, const FrontView& view,
              const OverlayArt& art) {
  const double t = view.reduceMotion ? kTitleIntroSeconds : view.introSeconds;
  if (art.title == nullptr) {
    // No art: the name in lettering, revealed the same way by rows.
    if (t < 0.15) {
      return;
    }
    const float size = std::min(logo.w / (6.0F * 9.0F), logo.h / 22.0F);
    const float middle = logo.x + logo.w * 0.5F;
    drawNeonText(r, "MULTIBALL", middle, logo.y, size, kYellow, kOrange, kPink,
                 TextAlign::Center);
    drawNeonText(r, "ACADEMY", middle, logo.y + size * 11.0F, size * 0.8F, kCream, kAqua,
                 kViolet, TextAlign::Center);
    return;
  }
  constexpr double kScanStart = 0.15;
  constexpr double kScanTime = 0.75;
  constexpr double kFlashTime = 0.35;
  const double scan = clamp01((t - kScanStart) / kScanTime);
  if (scan <= 0.0) {
    return;
  }
  const auto edge = static_cast<float>(scan);
  drawTitlePart(r, art.title, logo, 0.0F, 1.0F, 0.0F, edge);
  if (scan < 1.0) {
    // The rows just reached glow, and a line of light leads them.
    lightTitlePart(r, art.title, logo, 0.0F, 1.0F, edge - 0.08F, edge, kCream, 0.9F);
    fillRect(r, {logo.x, logo.y + logo.h * edge - 1.5F, logo.w, 3.0F}, sdl(art::kCream, 200));
    return;
  }
  const double flash = clamp01((t - kScanStart - kScanTime) / kFlashTime);
  if (flash < 1.0) {
    lightTitlePart(r, art.title, logo, 0.0F, 1.0F, 0.0F, 1.0F, kCream,
                   static_cast<float>(0.85 * (1.0 - flash)));
  }
  if (view.reduceMotion || t < kTitleIntroSeconds) {
    return;
  }
  // The light across: 0.7 s of every 5.
  constexpr double kSheenEvery = 5.0;
  constexpr double kSheenTime = 0.7;
  const double phase = std::fmod(std::max(view.seconds, 0.0), kSheenEvery) / kSheenTime;
  if (phase < 1.0) {
    const auto centre = static_cast<float>(-0.1 + 1.2 * phase);
    lightTitlePart(r, art.title, logo, centre - 0.05F, centre + 0.05F, 0.0F, 1.0F, kCream, 0.7F);
  }
}

// --- The dot display --------------------------------------------------------

constexpr float kDotPitch = 8.0F;
constexpr int kDotRows = 8;

struct Marquee {
  std::array<std::string, 8> messages;
  std::size_t count = 0;
  void add(std::string text) {
    if (count < messages.size()) {
      messages[count++] = std::move(text);
    }
  }
};

Marquee marqueeFor(const HighScores* scores, bool keyboard) {
  Marquee m;
  m.add(keyboard ? "PRESS ENTER" : "PRESS PLAY");
  m.add("MULTIBALL");
  m.add("ACADEMY");
  if (scores != nullptr && scores->size() > 0) {
    m.add("HIGH SCORES");
    for (std::size_t i = 0; i < scores->size() && m.count < m.messages.size(); ++i) {
      const HighScore entry = scores->at(i);
      ScoreText buffer{};
      std::string line(entry.initials.data(), entry.initials.size());
      line += ' ';
      line += formatScore(entry.score, buffer);
      m.add(std::move(line));
    }
  }
  return m;
}

// Lit dots of the text in a grid columns wide: lit(col, row).
template <typename Fn>
void forEachLitDot(std::string_view text, int startColumn, int columns, Fn&& lit) {
  for (std::size_t i = 0; i < text.size(); ++i) {
    char c = text[i];
    if (c < font::kFirstChar || c > font::kLastChar) {
      c = '?';
    }
    const auto& glyph = font::kGlyphs[static_cast<std::size_t>(c - font::kFirstChar)];
    const int x0 = startColumn + static_cast<int>(i) * font::kAdvance;
    for (int row = 0; row < font::kGlyphHeight; ++row) {
      for (int col = 0; col < font::kGlyphWidth; ++col) {
        const int x = x0 + col;
        if (x >= 0 && x < columns && ((static_cast<unsigned>(glyph[static_cast<std::size_t>(row)]) >> static_cast<unsigned>(4 - col)) & 1U) != 0) {
          lit(x, row);
        }
      }
    }
  }
}

void drawDot(SDL_Renderer* r, const OverlayArt& art, float x, float y, SDL_Color c) {
  if (art.dot != nullptr) {
    SDL_SetTextureColorMod(art.dot, c.r, c.g, c.b);
    const SDL_FRect to{x, y, kDotPitch, kDotPitch};
    SDL_RenderTexture(r, art.dot, nullptr, &to);
  } else {
    SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255);
    const SDL_FRect to{x + 1.5F, y + 1.5F, kDotPitch - 3.0F, kDotPitch - 3.0F};
    SDL_RenderFillRect(r, &to);
  }
}

void drawMarquee(SDL_Renderer* r, const screen::Rect& box, const FrontView& view,
                 const OverlayArt& art) {
  const double t = view.reduceMotion ? kTitleIntroSeconds : view.introSeconds;
  // The glass and its chrome-like lip.
  fillRect(r, {box.x - 6.0F, box.y - 6.0F, box.w + 12.0F, box.h + 12.0F}, sdl(art::kChLo));
  fillRect(r, {box.x - 4.0F, box.y - 4.0F, box.w + 8.0F, 2.0F}, sdl(art::kChHi));
  fillRect(r, box, kDotGlass);
  const int columns = static_cast<int>(box.w / kDotPitch);
  const float left = box.x + (box.w - static_cast<float>(columns) * kDotPitch) * 0.5F;
  // While the logo builds, the rows of unlit dots come on one by one.
  const int rowsOn = t >= 1.0 ? kDotRows : static_cast<int>(clamp01((t - 0.2) / 0.8) * kDotRows);
  std::array<std::array<bool, 128>, kDotRows> lit{};
  if (t >= kTitleIntroSeconds - 0.2) {
    const Marquee m = marqueeFor(view.scores, view.keyboard);
    // Each message slides in and holds, or runs across if it is too long.
    constexpr double kSlide = 0.45;
    constexpr double kHold = 1.8;
    constexpr double kRun = 40.0;  // Columns a second.
    double clock = std::max(view.seconds, 0.0);
    // The messages' lengths in time, then where the clock falls among them.
    double total = 0.0;
    std::array<double, 8> lengths{};
    for (std::size_t i = 0; i < m.count; ++i) {
      const auto width = static_cast<int>(m.messages[i].size()) * font::kAdvance - 1;
      lengths[i] = width <= columns || view.reduceMotion ? kSlide + kHold
                                                         : (width + columns) / kRun;
      total += lengths[i];
    }
    clock = std::fmod(clock, total);
    std::size_t index = 0;
    while (index + 1 < m.count && clock >= lengths[index]) {
      clock -= lengths[index];
      ++index;
    }
    const std::string& text = m.messages[index];
    const int width = static_cast<int>(text.size()) * font::kAdvance - 1;
    int start = (columns - width) / 2;
    if (width > columns && !view.reduceMotion) {
      start = columns - static_cast<int>(clock * kRun);
    } else if (!view.reduceMotion && clock < kSlide) {
      start += static_cast<int>((1.0 - clock / kSlide) * static_cast<double>(columns - start));
    }
    forEachLitDot(text, start, std::min(columns, 128), [&lit](int x, int row) {
      lit[static_cast<std::size_t>(row)][static_cast<std::size_t>(x)] = true;
    });
  }
  for (int row = 0; row < rowsOn; ++row) {
    for (int col = 0; col < columns && col < 128; ++col) {
      const bool on = lit[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)];
      drawDot(r, art, left + static_cast<float>(col) * kDotPitch,
              box.y + static_cast<float>(row) * kDotPitch + 2.0F, on ? kDotLit : kDotUnlit);
    }
  }
  if (art.dot != nullptr) {
    SDL_SetTextureColorMod(art.dot, 255, 255, 255);
  }
}

// --- Buttons and cards -------------------------------------------------------

bool isPressed(const FrontView& view, FrontTarget target) noexcept {
  return view.pressed.kind != FrontTarget::Kind::None && view.pressed == target;
}

void button(SDL_Renderer* r, const OverlayArt& art, const screen::Rect& face,
            std::string_view caption, SDL_Color tint, bool pressed, bool focused) {
  if (face.w <= 0.0F || face.h <= 0.0F) {
    return;
  }
  drawCaptionButton(r, face, caption, tint, pressed, 4.0F, 3.0F, art.button);
  if (focused) {
    drawFocus(r, face);
  }
}

void card(SDL_Renderer* r, const OverlayArt& art, const screen::PageLayout& page,
          std::string_view heading) {
  drawDialogCard(r, art, frect(page.box));
  const float middle = page.heading.x + page.heading.w * 0.5F;
  drawNeonText(r, heading, middle, page.heading.y + 8.0F, 6.0F, kYellow, kPink, kViolet,
               TextAlign::Center);
  drawDialogRule(r, art, middle, page.heading.y + page.heading.h + 6.0F, 260.0F);
}

void backButton(SDL_Renderer* r, const OverlayArt& art, const screen::PageLayout& page,
                const FrontView& view, bool focused) {
  button(r, art, page.back, "BACK", kPink, isPressed(view, {FrontTarget::Kind::Back, 0}),
         focused && view.keyboard);
}

// --- The pages ----------------------------------------------------------------

std::string_view captionOf(TitleItem item) noexcept {
  switch (item) {
    case TitleItem::Continue:
      return "CONTINUE";
    case TitleItem::Play:
      return "PLAY";
    case TitleItem::Scores:
      return "HIGH SCORES";
    case TitleItem::HowTo:
      return "HOW TO PLAY";
    case TitleItem::Settings:
      return "SETTINGS";
    case TitleItem::About:
      return "ABOUT";
  }
  return "";
}

void drawTitle(SDL_Renderer* r, const screen::Layout& layout, const FrontView& view,
               const OverlayArt& art) {
  const screen::TitleLayout title = screen::titleLayout(layout, view.config);
  drawLogo(r, title.logo, view, art);
  drawMarquee(r, title.marquee, view, art);
  const double t = view.reduceMotion ? kTitleIntroSeconds : view.introSeconds;
  std::size_t shown = 0;
  for (std::size_t i = 0; i < kTitleItemCount; ++i) {
    const auto item = static_cast<TitleItem>(i);
    if (item == TitleItem::Continue && !view.config.continueShown) {
      continue;
    }
    // One after another as the intro ends.
    if (t < 1.0 + 0.07 * static_cast<double>(shown++)) {
      continue;
    }
    const SDL_Color tint = item == TitleItem::Play || item == TitleItem::Continue ? kTeal
                           : item == TitleItem::About                           ? kPink
                                                                                : kButtonOrchid;
    button(r, art, title.items[i], captionOf(item), tint,
           isPressed(view, {FrontTarget::Kind::Item, static_cast<std::uint8_t>(i)}),
           view.keyboard && view.state.titleFocus == item);
  }
  if (t >= kTitleIntroSeconds) {
    const float size = screen::smallestTextSize(layout);
    const float middle = title.footer.x + title.footer.w * 0.5F;
    if (view.keyboard) {
      drawText(r, "ARROWS AND ENTER TO CHOOSE.  F2 TO PLAY AT ONCE.", middle, title.footer.y,
               size, kOrchid, TextAlign::Center);
    } else {
      std::array<char, 48> version{};
      drawText(r, formatText(version.data(), version.size(), "VERSION %s", kGameVersion), middle,
               title.footer.y, size, kOrchid, TextAlign::Center);
    }
  }
}

void drawScores(SDL_Renderer* r, const screen::Layout& layout, const FrontView& view,
                const OverlayArt& art) {
  const screen::PageLayout page = screen::pageLayout(layout, FrontPage::Scores);
  card(r, art, page, "HIGH SCORES");
  const screen::Rect& body = page.body;
  const float middle = body.x + body.w * 0.5F;
  const float small = screen::smallestTextSize(layout);
  if (view.scores == nullptr || view.scores->size() == 0) {
    drawText(r, "NO GAMES FINISHED YET", middle, body.y + 60.0F, std::max(small, 3.0F), kBody,
             TextAlign::Center);
  } else {
    // Place, initials, score and the rank reached: four columns.
    const float size = body.w >= 800.0F ? 4.0F : 3.0F;
    const float rowStep = size * 22.0F;
    const float place = body.x + 8.0F;
    const float initials = place + 4.0F * 6.0F * size;
    const float scoreRight = initials + 3.0F * 6.0F * size + 17.0F * 6.0F * size;
    const float rank = scoreRight + 2.0F * 6.0F * size;
    float y = body.y + 24.0F;
    for (std::size_t i = 0; i < view.scores->size(); ++i) {
      const HighScore entry = view.scores->at(i);
      std::array<char, 8> number{};
      drawText(r, formatText(number.data(), number.size(), "%u.", static_cast<unsigned>(i + 1)),
               place, y, size, kOrchid);
      drawText(r, std::string_view(entry.initials.data(), entry.initials.size()), initials, y,
               size, kPink);
      ScoreText buffer{};
      drawText(r, formatScore(entry.score, buffer), scoreRight, y, size, kBody, TextAlign::Right);
      const float rankSize = rank + textWidth(rankName(entry.rank), size) <= body.x + body.w
                                 ? size
                                 : std::max(size - 1.0F, small);
      drawText(r, rankName(entry.rank), rank, y + (size - rankSize) * 3.5F, rankSize, kYellow);
      y += rowStep;
    }
  }
  backButton(r, art, page, view, true);
}

void drawHowTo(SDL_Renderer* r, const screen::Layout& layout, const FrontView& view,
               const OverlayArt& art) {
  const std::size_t number = std::min(view.state.howToPage, kHowToPageCount - 1);
  const bool guide = view.config.guideShown && number == 0;
  const screen::PageLayout page = screen::pageLayout(layout, FrontPage::HowTo, guide);
  const howto::Page& text = howto::kPages[number];
  card(r, art, page, "HOW TO PLAY");
  const screen::Rect& body = page.body;
  // The largest size, up to 4, at which every page fits (the same size on
  // every page); the smallest of the layout always does (tests/FrontTest).
  float size = screen::smallestTextSize(layout);
  for (float larger = 4.0F; larger > size; larger -= 1.0F) {
    const float textHeight = body.h - (larger + 1.0F) * 12.0F;
    const bool fits = std::ranges::all_of(howto::kPages, [&](const howto::Page& p) {
      return static_cast<float>(lineCount(howto::text(p, !view.keyboard),
                                          charsAcross(body.w, larger))) *
                 kLinePitch * larger <=
             textHeight;
    });
    if (fits) {
      size = larger;
      break;
    }
  }
  // The page's own title, then its text set from the left.
  drawNeonText(r, text.title, body.x, body.y, size + 1.0F, kCream, kAqua, kViolet);
  const screen::Rect area{body.x, body.y + (size + 1.0F) * 12.0F, body.w,
                          body.h - (size + 1.0F) * 12.0F};
  wrapText(howto::text(text, !view.keyboard), charsAcross(area.w, size),
           [&](std::size_t line, std::string_view part) {
             drawText(r, part, area.x, area.y + static_cast<float>(line) * kLinePitch * size, size,
                      kBody);
           });
  std::array<char, 16> count{};
  drawText(r,
           formatText(count.data(), count.size(), "%u / %u", static_cast<unsigned>(number + 1),
                      static_cast<unsigned>(kHowToPageCount)),
           body.x + body.w, body.y, size, kOrchid, TextAlign::Right);
  const bool first = number == 0;
  const bool last = number + 1 >= kHowToPageCount;
  drawCaptionButton(r, page.previous, "<", first ? kButtonOff : kButtonOrchid,
                    isPressed(view, {FrontTarget::Kind::Previous, 0}), 5.0F, 3.0F, art.button);
  drawCaptionButton(r, page.next, ">", last ? kButtonOff : kButtonOrchid,
                    isPressed(view, {FrontTarget::Kind::Next, 0}), 5.0F, 3.0F, art.button);
  if (guide) {
    button(r, art, page.guide, "SHOW TOUCH GUIDE", kTeal,
           isPressed(view, {FrontTarget::Kind::GuideButton, 0}), false);
  }
  backButton(r, art, page, view, true);
}

void drawSettings(SDL_Renderer* r, const screen::Layout& layout, const FrontView& view,
                  const OverlayArt& art) {
  const screen::SettingsLayout s = screen::settingsLayout(layout, view.config);
  card(r, art, s.page, "SETTINGS");
  const Settings settings = view.settings != nullptr ? *view.settings : Settings{};
  const auto rowRect = [&s](SettingsRow row) { return s.rows[static_cast<std::size_t>(row)]; };
  const auto focused = [&view](SettingsRow row) {
    return view.keyboard && view.state.settingsFocus == row;
  };
  const auto pressedRow = [&view](SettingsRow row) {
    return isPressed(view, {FrontTarget::Kind::Row, static_cast<std::uint8_t>(row)});
  };
  const auto toggle = [&](SettingsRow row, std::string_view on, std::string_view off, bool value) {
    button(r, art, rowRect(row), value ? on : off, value ? kButtonOrchid : kButtonOff,
           pressedRow(row), focused(row));
  };
  toggle(SettingsRow::Sound, "SOUND: ON", "SOUND: OFF", settings.sound);
  // Volume: a key either side of a row of lamps, one for each step.
  const screen::Rect volume = rowRect(SettingsRow::Volume);
  drawCaptionButton(r, s.volumeDown, "-", settings.volume == 0 ? kButtonOff : kButtonOrchid,
                    isPressed(view, {FrontTarget::Kind::VolumeDown, 0}), 5.0F, 3.0F, art.button);
  drawCaptionButton(r, s.volumeUp, "+",
                    settings.volume >= Settings::kMaxVolume ? kButtonOff : kButtonOrchid,
                    isPressed(view, {FrontTarget::Kind::VolumeUp, 0}), 5.0F, 3.0F, art.button);
  {
    const float left = s.volumeDown.x + s.volumeDown.w + 16.0F;
    const float right = s.volumeUp.x - 16.0F;
    const float size = 3.0F;
    std::array<char, 16> label{};
    drawText(r,
             formatText(label.data(), label.size(), "VOLUME %u",
                        static_cast<unsigned>(settings.volume)),
             (left + right) * 0.5F, volume.y + 8.0F, size, kBody, TextAlign::Center);
    const float step = (right - left) / static_cast<float>(Settings::kMaxVolume);
    for (int i = 0; i < Settings::kMaxVolume; ++i) {
      const bool on = i < settings.volume;
      fillRect(r,
               {left + static_cast<float>(i) * step + 3.0F, volume.y + 44.0F, step - 6.0F,
                volume.h - 56.0F},
               on ? (settings.sound ? kYellow : kOrchid) : sdl(art::kInk));
    }
    if (focused(SettingsRow::Volume)) {
      drawFocus(r, volume);
    }
  }
  toggle(SettingsRow::Music, "MUSIC: ON", "MUSIC: OFF", settings.music);
  if (view.config.hapticsShown) {
    toggle(SettingsRow::Haptics, "HAPTICS: ON", "HAPTICS: OFF", settings.haptics);
  }
  if (view.state.confirmReset) {
    button(r, art, rowRect(SettingsRow::Reset),
           view.keyboard ? "PRESS AGAIN TO RESET" : "TAP AGAIN TO RESET", kOrange, true,
           focused(SettingsRow::Reset));
  } else {
    button(r, art, rowRect(SettingsRow::Reset), "RESET HIGH SCORES", kPink,
           pressedRow(SettingsRow::Reset), focused(SettingsRow::Reset));
  }
  backButton(r, art, s.page, view, focused(SettingsRow::Back));
}

// A label of the touch guide: an ink card with its words.
void guideLabel(SDL_Renderer* r, const screen::Rect& box, std::string_view text, float size,
                SDL_Color edge) {
  fillRect(r, box, sdl(art::kInk, 225));
  outlineRect(r, box, 3.0F, edge);
  const std::size_t across = charsAcross(box.w - 16.0F, size);
  const std::size_t lines = lineCount(text, across);
  const float top = box.y + (box.h - static_cast<float>(lines) * kLinePitch * size + 3.0F * size) * 0.5F;
  const float middle = box.x + box.w * 0.5F;
  wrapText(text, across, [&](std::size_t line, std::string_view part) {
    drawText(r, part, middle, top + static_cast<float>(line) * kLinePitch * size, size, kBody,
             TextAlign::Center);
  });
}

void tintArea(SDL_Renderer* r, const screen::Rect& area, SDL_Color c) {
  fillRect(r, area, {c.r, c.g, c.b, 46});
}

}  // namespace

std::size_t marqueeMessageCount(const HighScores* scores) noexcept {
  return marqueeFor(scores, false).count;
}

std::string_view titleItemLabel(TitleItem item) noexcept { return captionOf(item); }

void drawFront(SDL_Renderer* r, const screen::Layout& layout, const FrontView& view,
               const OverlayArt& art) {
  if (r == nullptr) {
    return;
  }
  switch (view.state.page) {
    case FrontPage::Title:
      drawTitle(r, layout, view, art);
      break;
    case FrontPage::Scores:
      drawScores(r, layout, view, art);
      break;
    case FrontPage::HowTo:
      drawHowTo(r, layout, view, art);
      break;
    case FrontPage::Settings:
      drawSettings(r, layout, view, art);
      break;
    case FrontPage::Guide:
      drawTouchGuide(r, layout, false, art);
      break;
  }
}

void drawTouchGuide(SDL_Renderer* r, const screen::Layout& layout, bool firstGame,
                    const OverlayArt& art) {
  if (r == nullptr || layout.idiom != screen::Idiom::Touch) {
    return;
  }
  // A veil over everything, then each area lit in its colour.
  fillRect(r, {0.0F, 0.0F, layout.canvasWidth, layout.canvasHeight}, sdl(art::kNight, 120));
  tintArea(r, layout.flipperLeftZone, kPink);
  tintArea(r, layout.flipperRightZone, kTeal);
  for (const screen::Rect& button :
       {layout.centreButton, layout.nudgeLeftButton, layout.nudgeRightButton, layout.pauseButton}) {
    outlineRect(r, button, 4.0F, kYellow);
  }
  // The line between the two halves.
  const float split = layout.flipperRightZone.x;
  fillRect(r, {split - 2.0F, layout.flipperRightZone.y, 4.0F, layout.flipperRightZone.h},
           sdl(art::kCream, 160));
  const screen::GuideLayout g = screen::guideLayout(layout);
  const float size = screen::smallestTextSize(layout);
  guideLabel(r, g.leftLabel, howto::kGuideLeft, size, kPink);
  guideLabel(r, g.rightLabel, howto::kGuideRight, size, kTeal);
  guideLabel(r, g.centreLabel, howto::kGuideCentre, size, kYellow);
  guideLabel(r, g.nudgeLeftLabel, howto::kGuideNudge, size, kYellow);
  guideLabel(r, g.nudgeRightLabel, howto::kGuideNudge, size, kYellow);
  guideLabel(r, g.pauseLabel, howto::kGuidePause, size, kYellow);
  drawDialogCard(r, art, frect(g.card));
  const float middle = g.card.x + g.card.w * 0.5F;
  drawNeonText(r, howto::kGuideTitle, middle, g.card.y + 50.0F, 2.0F * size, kYellow, kPink,
               kViolet, TextAlign::Center);
  drawText(r, firstGame ? howto::kGuideStart : howto::kGuideBack, middle,
           g.card.y + g.card.h - 60.0F - 7.0F * size, size, kBody, TextAlign::Center);
  (void)kInk;
}

}  // namespace pinball
