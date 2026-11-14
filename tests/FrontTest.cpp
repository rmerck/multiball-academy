// The front screens without SDL: the settings file, the menus' navigation,
// their rectangles on every layout, the touch guide's, the haptic rate
// limit, the review gate and attract mode's hands.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "Check.hpp"
#include "Suites.hpp"
#include "bots/Bots.hpp"
#include "core/Attract.hpp"
#include "core/Feedback.hpp"
#include "core/FrontLayout.hpp"
#include "core/FrontMenu.hpp"
#include "core/Game.hpp"
#include "core/HowToText.hpp"
#include "core/OverlayLayout.hpp"
#include "core/ScreenLayout.hpp"
#include "core/Settings.hpp"
#include "core/TextWrap.hpp"
#include "core/TouchControls.hpp"

namespace {

using namespace pinball;
using screen::Layout;
using screen::Rect;

// --- Settings -------------------------------------------------------------

void testSettingsRoundTrip() {
  Settings defaults;
  CHECK(defaults.sound && defaults.music && defaults.haptics && !defaults.touchGuideSeen);
  CHECK(defaults.volume == 8 && defaults.gamesFinished == 0 && defaults.reviewAskedVersion.empty());
  Settings parsed;
  CHECK(parsed.parse(defaults.serialize()));
  CHECK(parsed == defaults);

  Settings changed;
  changed.sound = false;
  changed.volume = 0;
  changed.music = false;
  changed.haptics = false;
  changed.touchGuideSeen = true;
  changed.gamesFinished = 12;
  changed.reviewAskedVersion = "2.0.0";
  const std::string text = changed.serialize();
  CHECK(text ==
        "PINBALL-SETTINGS 1\nsound 0\nvolume 0\nmusic 0\nhaptics 0\nguide 1\ngames 12\n"
        "asked 2.0.0\n");
  CHECK(parsed.parse(text) && parsed == changed);
  CHECK(text.size() <= Settings::kMaxTextBytes);

  // The largest values still fit and come back.
  Settings largest = changed;
  largest.volume = Settings::kMaxVolume;
  largest.gamesFinished = Settings::kMaxGames;
  largest.reviewAskedVersion = std::string(Settings::kMaxVersionLength, '9');
  CHECK(largest.serialize().size() <= Settings::kMaxTextBytes);
  CHECK(parsed.parse(largest.serialize()) && parsed == largest);
  // Out of range on the way out is held to the range.
  Settings over;
  over.volume = 200;
  over.gamesFinished = 4'000'000'000U;
  over.reviewAskedVersion = "not a version";
  CHECK(parsed.parse(over.serialize()));
  CHECK(parsed.volume == Settings::kMaxVolume && parsed.gamesFinished == Settings::kMaxGames);
  CHECK(parsed.reviewAskedVersion.empty());

  // Keys in any order, and missing ones keep their defaults.
  CHECK(parsed.parse("PINBALL-SETTINGS 1\nmusic 0\nsound 0\n"));
  CHECK(!parsed.sound && !parsed.music && parsed.volume == 8 && parsed.haptics);
  CHECK(parsed.parse("PINBALL-SETTINGS 1\n"));
  CHECK(parsed == Settings{});
  // A key from a newer version is passed over: the rest still counts.
  CHECK(parsed.parse("PINBALL-SETTINGS 1\ngames 7\ncolour-scheme neon\nasked 2.0.0\n"));
  CHECK(parsed.gamesFinished == 7 && parsed.reviewAskedVersion == "2.0.0");
  // But not a malformed one.
  CHECK(!parsed.parse("PINBALL-SETTINGS 1\nColour neon\n"));
  CHECK(!parsed.parse("PINBALL-SETTINGS 1\ncolour neo n\n"));
}

void testSettingsRejectsGarbage() {
  const std::array<std::string_view, 30> bad{{
      "",
      "PINBALL-SETTINGS 1",                    // No newline.
      "PINBALL-SETTINGS 2\n",                  // Another format.
      "pinball-settings 1\n",
      "PINBALL-SETTINGS 1\nsound 1",           // Last line unended.
      "PINBALL-SETTINGS 1\nsound 2\n",
      "PINBALL-SETTINGS 1\nsound  1\n",
      "PINBALL-SETTINGS 1\nsound\n",
      "PINBALL-SETTINGS 1\nsound 1 \n",
      "PINBALL-SETTINGS 1\nsound 1\nsound 1\n",  // Twice.
      "PINBALL-SETTINGS 1\nvolume 11\n",
      "PINBALL-SETTINGS 1\nvolume -1\n",
      "PINBALL-SETTINGS 1\nvolume 08\n",
      "PINBALL-SETTINGS 1\nvolume +8\n",
      "PINBALL-SETTINGS 1\nvolume 8x\n",
      "PINBALL-SETTINGS 1\ngames 1000000\n",
      "PINBALL-SETTINGS 1\ngames 99999999999999999999\n",
      "PINBALL-SETTINGS 1\nasked \n",
      "PINBALL-SETTINGS 1\nasked 2.0.0b\n",
      "PINBALL-SETTINGS 1\nasked 1234567890123456\n",
      "PINBALL-SETTINGS 1\nasked ../x\n",
      "PINBALL-SETTINGS 1\ncolour\n",         // Unknown key, no value.
      "PINBALL-SETTINGS 1\n\n",
      "PINBALL-SETTINGS 1\r\nsound 1\r\n",
      "PINBALL-SETTINGS 1\nSOUND 1\n",
      "\nPINBALL-SETTINGS 1\n",
      "PINBALL-SETTINGS 1\nguide yes\n",
      "PINBALL-SETTINGS 1\nhaptics 1\nPINBALL-SETTINGS 1\n",
      "PINBALL-SETTINGS 1\nmusic 0\0\n",
      "PINBALL-SETTINGS 1\n sound 1\n",
  }};
  for (const std::string_view text : bad) {
    Settings s;
    s.sound = false;
    s.volume = 3;
    CHECK(!s.parse(text));
    // A text that is refused leaves the defaults, never half of it.
    CHECK(s == Settings{});
  }
  // Too long, even if it were valid line by line.
  std::string longText = "PINBALL-SETTINGS 1\n";
  while (longText.size() <= Settings::kMaxTextBytes) {
    longText += "x";
  }
  Settings s;
  CHECK(!s.parse(longText));

  // Bounded fuzz: random bytes, random edits of a good file. Never accepted
  // outside the ranges, never crashes, always back to the defaults when refused.
  bots::Rng rng(42);
  const std::string good = Settings{}.serialize();
  for (int round = 0; round < 20000; ++round) {
    std::string text;
    if (round % 2 == 0) {
      const int length = rng.range(0, 300);
      for (int i = 0; i < length; ++i) {
        text.push_back(static_cast<char>(rng.range(0, 255)));
      }
    } else {
      text = good;
      const int edits = rng.range(1, 4);
      for (int i = 0; i < edits; ++i) {
        const auto at = static_cast<std::size_t>(rng.range(0, static_cast<int>(text.size()) - 1));
        switch (rng.range(0, 2)) {
          case 0:
            text[at] = static_cast<char>(rng.range(32, 126));
            break;
          case 1:
            text.erase(at, 1);
            break;
          default:
            text.insert(at, 1, static_cast<char>(rng.range(0, 127)));
            break;
        }
        if (text.empty()) {
          break;
        }
      }
    }
    Settings fuzzed;
    if (fuzzed.parse(text)) {
      CHECK(fuzzed.volume <= Settings::kMaxVolume);
      CHECK(fuzzed.gamesFinished <= Settings::kMaxGames);
      CHECK(fuzzed.reviewAskedVersion.empty() || isSettingsVersion(fuzzed.reviewAskedVersion));
      // What was accepted comes back the same.
      Settings again;
      CHECK(again.parse(fuzzed.serialize()) && again == fuzzed);
    } else {
      CHECK(fuzzed == Settings{});
    }
  }
}

// --- Navigation -------------------------------------------------------------

void testTitleNavigation() {
  FrontMenu menu;
  CHECK(menu.state().page == FrontPage::Title);
  CHECK(menu.state().titleFocus == TitleItem::Play);
  CHECK(!menu.shown(TitleItem::Continue));
  // Down through every item, wrapping past the hidden Continue.
  const std::array<TitleItem, 6> order{TitleItem::Scores, TitleItem::HowTo, TitleItem::Settings,
                                       TitleItem::About, TitleItem::Play, TitleItem::Scores};
  for (const TitleItem item : order) {
    CHECK(menu.command(FrontCommand::Down) == FrontAction::Moved);
    CHECK(menu.state().titleFocus == item);
  }
  CHECK(menu.command(FrontCommand::Up) == FrontAction::Moved);
  CHECK(menu.state().titleFocus == TitleItem::Play);
  CHECK(menu.command(FrontCommand::Up) == FrontAction::Moved);
  CHECK(menu.state().titleFocus == TitleItem::About);
  CHECK(menu.command(FrontCommand::Select) == FrontAction::OpenAbout);
  CHECK(menu.state().page == FrontPage::Title);
  // Back, left and right do nothing on the title.
  CHECK(menu.command(FrontCommand::Back) == FrontAction::None);
  CHECK(menu.command(FrontCommand::Left) == FrontAction::None);

  // Each page opens, and back returns focus to its item.
  for (const TitleItem item : {TitleItem::Scores, TitleItem::HowTo, TitleItem::Settings}) {
    menu.reset();
    CHECK(menu.tap({FrontTarget::Kind::Item, static_cast<std::uint8_t>(item)}) ==
          FrontAction::Opened);
    CHECK(menu.state().page != FrontPage::Title);
    CHECK(menu.command(FrontCommand::Back) == FrontAction::Closed);
    CHECK(menu.state().page == FrontPage::Title && menu.state().titleFocus == item);
  }
  CHECK(menu.tap({FrontTarget::Kind::Item, static_cast<std::uint8_t>(TitleItem::Play)}) ==
        FrontAction::Play);
  // A tap on the hidden Continue, or out of range, does nothing.
  CHECK(menu.tap({FrontTarget::Kind::Item, 0}) == FrontAction::None);
  CHECK(menu.tap({FrontTarget::Kind::Item, 99}) == FrontAction::None);
  CHECK(menu.tap({FrontTarget::Kind::Back, 0}) == FrontAction::None);

  // With a saved game, Continue is first and has the focus.
  FrontMenu resumable(FrontConfig{.continueShown = true});
  CHECK(resumable.state().titleFocus == TitleItem::Continue);
  CHECK(resumable.command(FrontCommand::Select) == FrontAction::Continue);
  CHECK(resumable.command(FrontCommand::Up) == FrontAction::Moved);
  CHECK(resumable.state().titleFocus == TitleItem::About);
  // Hidden again: the focus leaves it.
  resumable.configure({});
  resumable.reset();
  CHECK(resumable.state().titleFocus == TitleItem::Play);
}

void testHowToAndScoresNavigation() {
  FrontMenu menu(FrontConfig{.guideShown = true});
  CHECK(menu.tap({FrontTarget::Kind::Item, static_cast<std::uint8_t>(TitleItem::HowTo)}) ==
        FrontAction::Opened);
  CHECK(menu.state().howToPage == 0);
  CHECK(menu.command(FrontCommand::Left) == FrontAction::None);
  for (std::size_t i = 1; i < kHowToPageCount; ++i) {
    CHECK(menu.command(FrontCommand::Right) == FrontAction::Moved);
    CHECK(menu.state().howToPage == i);
  }
  CHECK(menu.command(FrontCommand::Right) == FrontAction::None);
  CHECK(menu.tap({FrontTarget::Kind::Previous, 0}) == FrontAction::Moved);
  CHECK(menu.state().howToPage == kHowToPageCount - 2);
  // The guide opens from here and goes back here.
  CHECK(menu.tap({FrontTarget::Kind::GuideButton, 0}) == FrontAction::ShowGuide);
  CHECK(menu.state().page == FrontPage::Guide);
  CHECK(menu.tap({FrontTarget::Kind::Anywhere, 0}) == FrontAction::Opened);
  CHECK(menu.state().page == FrontPage::HowTo);
  CHECK(menu.command(FrontCommand::Select) == FrontAction::Closed);
  // Opened again it starts at the first page.
  CHECK(menu.command(FrontCommand::Select) == FrontAction::Opened);
  CHECK(menu.state().howToPage == 0);
  // No guide where there is no touch screen.
  FrontMenu keyboard;
  (void)keyboard.tap({FrontTarget::Kind::Item, static_cast<std::uint8_t>(TitleItem::HowTo)});
  CHECK(keyboard.tap({FrontTarget::Kind::GuideButton, 0}) == FrontAction::None);
  CHECK(keyboard.state().page == FrontPage::HowTo);

  FrontMenu scores;
  (void)scores.tap({FrontTarget::Kind::Item, static_cast<std::uint8_t>(TitleItem::Scores)});
  CHECK(scores.state().page == FrontPage::Scores);
  CHECK(scores.command(FrontCommand::Up) == FrontAction::None);
  CHECK(scores.command(FrontCommand::Select) == FrontAction::Closed);
}

void testSettingsNavigation() {
  FrontMenu menu(FrontConfig{.hapticsShown = false});
  (void)menu.tap({FrontTarget::Kind::Item, static_cast<std::uint8_t>(TitleItem::Settings)});
  CHECK(menu.state().page == FrontPage::Settings);
  CHECK(menu.state().settingsFocus == SettingsRow::Sound);
  CHECK(menu.command(FrontCommand::Select) == FrontAction::ToggleSound);
  CHECK(menu.command(FrontCommand::Left) == FrontAction::None);
  CHECK(menu.command(FrontCommand::Down) == FrontAction::Moved);
  CHECK(menu.state().settingsFocus == SettingsRow::Volume);
  CHECK(menu.command(FrontCommand::Left) == FrontAction::VolumeDown);
  CHECK(menu.command(FrontCommand::Right) == FrontAction::VolumeUp);
  CHECK(menu.command(FrontCommand::Down) == FrontAction::Moved);
  CHECK(menu.state().settingsFocus == SettingsRow::Music);
  CHECK(menu.command(FrontCommand::Select) == FrontAction::ToggleMusic);
  // Haptics hidden: skipped.
  CHECK(menu.command(FrontCommand::Down) == FrontAction::Moved);
  CHECK(menu.state().settingsFocus == SettingsRow::Reset);
  CHECK(menu.tap({FrontTarget::Kind::Row, static_cast<std::uint8_t>(SettingsRow::Haptics)}) ==
        FrontAction::None);
  // Reset asks first; moving away forgets the question.
  CHECK(menu.command(FrontCommand::Select) == FrontAction::AskReset);
  CHECK(menu.state().confirmReset);
  CHECK(menu.command(FrontCommand::Up) == FrontAction::Moved);
  CHECK(!menu.state().confirmReset);
  CHECK(menu.command(FrontCommand::Down) == FrontAction::Moved);
  CHECK(menu.command(FrontCommand::Select) == FrontAction::AskReset);
  CHECK(menu.command(FrontCommand::Select) == FrontAction::ResetScores);
  CHECK(!menu.state().confirmReset);
  // By touch the same, and another control in between forgets it.
  const FrontTarget reset{FrontTarget::Kind::Row, static_cast<std::uint8_t>(SettingsRow::Reset)};
  CHECK(menu.tap(reset) == FrontAction::AskReset);
  CHECK(menu.tap({FrontTarget::Kind::VolumeUp, 0}) == FrontAction::VolumeUp);
  CHECK(menu.tap(reset) == FrontAction::AskReset);
  CHECK(menu.tap(reset) == FrontAction::ResetScores);
  // Back leaves without resetting.
  CHECK(menu.tap(reset) == FrontAction::AskReset);
  CHECK(menu.command(FrontCommand::Back) == FrontAction::Closed);
  CHECK(!menu.state().confirmReset && menu.state().page == FrontPage::Title);
  (void)menu.command(FrontCommand::Select);
  CHECK(menu.state().page == FrontPage::Settings && menu.state().settingsFocus == SettingsRow::Sound);
  CHECK(menu.tap({FrontTarget::Kind::Row, static_cast<std::uint8_t>(SettingsRow::Back)}) ==
        FrontAction::Closed);

  FrontMenu haptic(FrontConfig{.hapticsShown = true});
  (void)haptic.tap({FrontTarget::Kind::Item, static_cast<std::uint8_t>(TitleItem::Settings)});
  for (int i = 0; i < 3; ++i) {
    (void)haptic.command(FrontCommand::Down);
  }
  CHECK(haptic.state().settingsFocus == SettingsRow::Haptics);
  CHECK(haptic.command(FrontCommand::Select) == FrontAction::ToggleHaptics);
}

// --- Rectangles -------------------------------------------------------------

bool wellFormed(const Rect& r) noexcept {
  return std::isfinite(r.x) && std::isfinite(r.y) && r.w > 0.0F && r.h > 0.0F;
}
bool inside(const Rect& inner, const Rect& outer, float slack = 0.01F) noexcept {
  return inner.x >= outer.x - slack && inner.y >= outer.y - slack &&
         inner.x + inner.w <= outer.x + outer.w + slack &&
         inner.y + inner.h <= outer.y + outer.h + slack;
}
bool overlap(const Rect& a, const Rect& b) noexcept {
  return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}
bool noneOverlap(const std::vector<Rect>& rects) noexcept {
  for (std::size_t i = 0; i < rects.size(); ++i) {
    for (std::size_t j = i + 1; j < rects.size(); ++j) {
      if (overlap(rects[i], rects[j])) {
        return false;
      }
    }
  }
  return true;
}
// A control a finger must find: 44 points both ways.
bool bigEnough(const Rect& r, const Layout& l) noexcept {
  return std::min(r.w, r.h) >= screen::kMinHitUnits &&
         (l.idiom == screen::Idiom::Desktop ||
          std::min(r.w, r.h) * l.pointsPerUnit >= screen::kMinHitPoints - 0.001F);
}
// The text fits its box at the size the screens draw it.
bool textFits(std::string_view text, const Rect& box, float size) {
  const std::size_t lines = lineCount(text, charsAcross(box.w, size));
  return static_cast<float>(lines) * kLinePitch * size <= box.h + 0.01F;
}

bool textWidthFits(std::string_view text, float width, float size) {
  return static_cast<float>(text.size()) * kGlyphAdvance * size - size <= width;
}

void checkFront(const Layout& l, const char* name) {
  const Rect canvas{0.0F, 0.0F, l.canvasWidth, l.canvasHeight};
  const Rect area = screen::compositionArea(l);
  const bool touch = l.idiom == screen::Idiom::Touch;
  const float size = screen::smallestTextSize(l);
  CHECK(inside(area, canvas));
  int before = pinball_test::failures();
  // Attract mode's table: whole, in its own shape, in the safe area, and
  // touching two of its sides.
  const Rect attract = screen::attractArea(l);
  CHECK(wellFormed(attract) && inside(attract, l.safe));
  CHECK(std::fabs(attract.w * screen::kPlayfieldHeight - attract.h * screen::kPlayfieldWidth) <
        0.5F);
  CHECK(std::fabs(attract.w - l.safe.w) < 0.01F || std::fabs(attract.h - l.safe.h) < 0.01F);
  for (const bool continueShown : {false, true}) {
    const FrontConfig config{.continueShown = continueShown, .hapticsShown = touch,
                             .guideShown = touch};
    const screen::TitleLayout title = screen::titleLayout(l, config);
    std::vector<Rect> parts{title.logo, title.marquee, title.footer};
    for (std::size_t i = 0; i < title.items.size(); ++i) {
      if (i == 0 && !continueShown) {
        CHECK(title.items[i].w == 0.0F);
        continue;
      }
      // A finger finds it (44 points), at no less than the floor in units.
      const Rect& item = title.items[i];
      CHECK(std::min(item.w, item.h) >= screen::kTitleItemMinUnits);
      CHECK(l.idiom == screen::Idiom::Desktop ||
            std::min(item.w, item.h) * l.pointsPerUnit >= screen::kMinHitPoints - 0.001F);
      parts.push_back(title.items[i]);
    }
    for (const Rect& r : parts) {
      CHECK(wellFormed(r) && inside(r, l.safe));
    }
    // The menu is over the table.
    for (std::size_t i = 1; i < title.items.size(); ++i) {
      CHECK(inside(title.items[i], attract));
    }
    CHECK(noneOverlap(parts));
    // Every item is found where it is, and nothing between them.
    FrontMenu menu(config);
    for (std::size_t i = continueShown ? 0 : 1; i < title.items.size(); ++i) {
      const Rect& r = title.items[i];
      const FrontTarget t = screen::frontTarget(l, config, menu.state(), r.x + r.w * 0.5F,
                                                r.y + r.h * 0.5F);
      CHECK(t.kind == FrontTarget::Kind::Item && t.index == i);
    }
    CHECK(screen::frontTarget(l, config, menu.state(), title.logo.x + 1.0F, title.logo.y + 1.0F)
              .kind == FrontTarget::Kind::None);

    const screen::SettingsLayout settings = screen::settingsLayout(l, config);
    std::vector<Rect> controls{settings.page.back, settings.volumeDown, settings.volumeUp};
    for (std::size_t i = 0; i < settings.rows.size(); ++i) {
      const auto row = static_cast<SettingsRow>(i);
      if (row == SettingsRow::Haptics && !config.hapticsShown) {
        CHECK(settings.rows[i].w == 0.0F);
        continue;
      }
      CHECK(bigEnough(settings.rows[i], l));
      CHECK(inside(settings.rows[i], settings.page.box));
      if (row != SettingsRow::Volume && row != SettingsRow::Back) {
        controls.push_back(settings.rows[i]);
      }
    }
    CHECK(noneOverlap(controls));
    CHECK(inside(settings.volumeDown, settings.rows[1]) && inside(settings.volumeUp, settings.rows[1]));
    CHECK(inside(settings.rows[4], settings.page.body));
  }
  for (const FrontPage page : {FrontPage::Scores, FrontPage::HowTo, FrontPage::Settings}) {
    for (const bool guide : {false, true}) {
      const screen::PageLayout p = screen::pageLayout(l, page, guide);
      CHECK(inside(p.box, area) && inside(p.box, l.safe));
      CHECK(wellFormed(p.body) && inside(p.body, p.box) && inside(p.heading, p.box));
      std::vector<Rect> parts{p.heading, p.body, p.back};
      CHECK(bigEnough(p.back, l) && inside(p.back, p.box));
      if (page == FrontPage::HowTo) {
        for (const Rect& r : {p.previous, p.next}) {
          CHECK(bigEnough(r, l) && inside(r, p.box));
          parts.push_back(r);
        }
        if (guide) {
          CHECK(bigEnough(p.guide, l) && inside(p.guide, p.box));
          parts.push_back(p.guide);
        }
        // Every page of text fits the body, the first beside the guide button.
        for (const howto::Page& text : howto::kPages) {
          // Under the page's own title, a line of size + 1.
          const Rect below{p.body.x, p.body.y + (size + 1.0F) * 12.0F, p.body.w,
                           p.body.h - (size + 1.0F) * 12.0F};
          CHECK(textFits(howto::text(text, touch), below, size));
          CHECK(textFits(howto::text(text, !touch), below, size));
          CHECK(screen::kMinTextPoints <= size * screen::kFontRows * l.pointsPerUnit + 0.001F);
        }
      }
      CHECK(noneOverlap(parts));
    }
  }
  // The guide: on touch screens, every label in the safe area and none on
  // another, and each label's text fits it.
  if (touch) {
    const screen::GuideLayout g = screen::guideLayout(l);
    const std::vector<Rect> labels{g.card, g.leftLabel, g.rightLabel, g.centreLabel,
                                   g.nudgeLeftLabel, g.nudgeRightLabel, g.pauseLabel};
    for (const Rect& r : labels) {
      CHECK(wellFormed(r) && inside(r, l.safe));
    }
    CHECK(noneOverlap(labels));
    CHECK(inside(g.leftLabel, l.flipperLeftZone) && inside(g.rightLabel, l.flipperRightZone));
    CHECK(textFits(howto::kGuideLeft, g.leftLabel, size));
    CHECK(textFits(howto::kGuideRight, g.rightLabel, size));
    CHECK(textFits(howto::kGuideCentre, g.centreLabel, size));
    CHECK(textFits(howto::kGuideNudge, g.nudgeLeftLabel, size));
    CHECK(textFits(howto::kGuideNudge, g.nudgeRightLabel, size));
    CHECK(textFits(howto::kGuidePause, g.pauseLabel, size));
    // The card: its title at twice the size, then the line that says how to go on.
    CHECK(textWidthFits(howto::kGuideTitle, g.card.w - 40.0F, 2.0F * size));
    CHECK(textWidthFits(howto::kGuideBack, g.card.w - 40.0F, size));
  }
  if (pinball_test::failures() != before) {
    std::fprintf(stderr, "  front layout failures on %s\n", name);
  }
}

void testFrontLayouts() {
  for (const screen::Preset& preset : screen::presets()) {
    checkFront(screen::computeLayout(preset.surface, preset.idiom), std::string(preset.name).c_str());
  }
  // Any window from the smallest scene up.
  for (const float w : {352.0F, 402.0F, 600.0F, 744.0F, 1024.0F, 1366.0F, 2048.0F}) {
    for (const float h : {602.0F, 700.0F, 874.0F, 1024.0F, 1366.0F, 2048.0F}) {
      checkFront(screen::computeLayout({w, h, 0.0F, 0.0F, 0.0F, 0.0F}, screen::Idiom::Touch),
                 "sweep");
      checkFront(screen::computeLayout({w + 124.0F, h + 21.0F, 0.0F, 21.0F, 62.0F, 62.0F},
                                       screen::Idiom::Touch),
                 "sweep with insets");
    }
  }
}

// The pause menu's new row is reached by touch.
void testPauseMainMenu() {
  for (const screen::Preset& preset : screen::presets()) {
    const Layout l = screen::computeLayout(preset.surface, preset.idiom);
    const screen::MenuLayout menu = screen::menuLayout(l);
    const Rect& r = menu.mainMenu;
    CHECK(menuTarget(menu, r.x + r.w * 0.5F, r.y + r.h * 0.5F).kind ==
          OverlayTarget::Kind::MenuMainMenu);
  }
}

// --- Haptics and the review -----------------------------------------------

void testHapticGate() {
  CHECK(!hapticFor(0));
  CHECK(hapticFor(soundBit(Sound::Flipper)) == Haptic::Flipper);
  CHECK(hapticFor(soundBit(Sound::Bumper) | soundBit(Sound::Flipper)) == Haptic::Hit);
  CHECK(hapticFor(soundBit(Sound::Slingshot)) == Haptic::Hit);
  CHECK(hapticFor(soundBit(Sound::Drain) | soundBit(Sound::Bumper)) == Haptic::Drain);
  CHECK(hapticFor(soundBit(Sound::Jackpot) | soundBit(Sound::Drain)) == Haptic::Jackpot);
  CHECK(hapticFor(soundBit(Sound::SuperJackpot)) == Haptic::Jackpot);
  CHECK(hapticFor(soundBit(Sound::MultiballStart)) == Haptic::Jackpot);
  CHECK(!hapticFor(soundBit(Sound::Target) | soundBit(Sound::Rollover)));

  // A bumper storm: a hit every frame for ten seconds gives no more than the
  // burst limit in each second.
  HapticGate storm;
  std::vector<std::uint64_t> played;
  for (std::uint64_t t = 0; t < 10'000; t += 16) {
    if (storm.take(soundBit(Sound::Bumper), t)) {
      played.push_back(t);
    }
  }
  CHECK(!played.empty());
  for (std::size_t i = 1; i < played.size(); ++i) {
    CHECK(played[i] - played[i - 1] >= HapticGate::kHitMs);
  }
  for (std::uint64_t start = 0; start < 10'000; start += 100) {
    const auto inWindow = std::ranges::count_if(
        played, [&](std::uint64_t t) { return t >= start && t < start + 1000; });
    CHECK(inWindow <= HapticGate::kBurstHits + 1);
  }
  CHECK(played.size() <= 10 * (HapticGate::kBurstHits + 1));

  // A drain right after a hit still comes, after the least gap; a heavier
  // kind wins in the same frame.
  HapticGate gate;
  CHECK(gate.take(soundBit(Sound::Bumper), 1000) == Haptic::Hit);
  CHECK(!gate.take(soundBit(Sound::Drain), 1010));
  CHECK(gate.take(soundBit(Sound::Drain), 1000 + HapticGate::kAnyMs) == Haptic::Drain);
  CHECK(gate.take(soundBit(Sound::Jackpot) | soundBit(Sound::Bumper), 2000) == Haptic::Jackpot);
  // The jackpot is not due again, but a hit in the same frame is.
  CHECK(gate.take(soundBit(Sound::Jackpot) | soundBit(Sound::Bumper), 2100) == Haptic::Hit);
  // Flippers, quickly.
  CHECK(gate.take(soundBit(Sound::Flipper), 3000) == Haptic::Flipper);
  CHECK(!gate.take(soundBit(Sound::Flipper), 3030));
  CHECK(gate.take(soundBit(Sound::Flipper), 3000 + HapticGate::kFlipperMs) == Haptic::Flipper);
  // A clock that runs back counts as no time.
  CHECK(!gate.take(soundBit(Sound::Flipper), 10));
}

void testReviewGate() {
  Settings s;
  CHECK(!reviewDue(s, "2.0.0", true));
  s.gamesFinished = kReviewMinGames - 1;
  CHECK(!reviewDue(s, "2.0.0", true));
  s.gamesFinished = kReviewMinGames;
  CHECK(reviewDue(s, "2.0.0", true));
  // Never unless a game ended in this run (so never at launch, never mid-game).
  CHECK(!reviewDue(s, "2.0.0", false));
  s.reviewAskedVersion = "2.0.0";
  CHECK(!reviewDue(s, "2.0.0", true));
  // A new version may ask once more.
  CHECK(reviewDue(s, "2.1.0", true));
  CHECK(!reviewDue(s, "", true));
  CHECK(!reviewDue(s, "2.1.0 beta", true));
}

// --- Attract mode -----------------------------------------------------------

std::uint64_t attractRun(int steps, std::uint64_t& score, int& games) {
  Game game;
  AttractPilot pilot;
  std::uint64_t presses = 0;
  bool wasOver = false;
  games = 0;
  score = 0;
  for (int i = 0; i < steps; ++i) {
    const InputState input = pilot.next(game);
    presses = presses * 31U + (input.leftFlipper ? 1U : 0U) + (input.rightFlipper ? 2U : 0U) +
              (input.plunger ? 4U : 0U) + (input.newGame ? 8U : 0U);
    game.step(input);
    score = std::max(score, game.rules().score());
    if (game.gameOver() && !wasOver) {
      ++games;
    }
    wasOver = game.gameOver();
  }
  return presses;
}

void testAttractPilot() {
  // Plays: launches balls, flips them and scores; a finished game starts
  // again; and the same steps give the same presses.
  std::uint64_t score = 0;
  int games = 0;
  const std::uint64_t first = attractRun(120 * 600, score, games);
  CHECK(score > 0);
  CHECK(games >= 1);
  std::uint64_t again = 0;
  int gamesAgain = 0;
  CHECK(attractRun(120 * 600, again, gamesAgain) == first);
  CHECK(again == score && gamesAgain == games);
}

}  // namespace

void testFront() {
  testSettingsRoundTrip();
  testSettingsRejectsGarbage();
  testTitleNavigation();
  testHowToAndScoresNavigation();
  testSettingsNavigation();
  testFrontLayouts();
  testPauseMainMenu();
  testHapticGate();
  testReviewGate();
  testAttractPilot();
}
