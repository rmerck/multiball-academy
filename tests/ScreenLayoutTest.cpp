#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

#include "Check.hpp"
#include "Suites.hpp"
#include "core/OverlayLayout.hpp"
#include "core/PanelLayout.hpp"
#include "core/ScreenLayout.hpp"
#include "core/SurfaceSpec.hpp"
#include "core/TableLayout.hpp"
#include "platform/GameViews.hpp"

namespace {

using namespace pinball::screen;

// Slack for sums of products of floats that are equal on paper.
constexpr float kEps = 0.01F;

bool same(const Rect& a, const Rect& b) noexcept {
  return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

bool near(float a, float b, float slack = kEps) noexcept { return std::fabs(a - b) <= slack; }

bool isEmpty(const Rect& r) noexcept { return r.w == 0.0F && r.h == 0.0F; }

bool wellFormed(const Rect& r) noexcept {
  return std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.w) && std::isfinite(r.h) &&
         r.w >= 0.0F && r.h >= 0.0F;
}

bool inside(const Rect& inner, const Rect& outer, float slack = kEps) noexcept {
  return inner.x >= outer.x - slack && inner.y >= outer.y - slack &&
         inner.x + inner.w <= outer.x + outer.w + slack &&
         inner.y + inner.h <= outer.y + outer.h + slack;
}

bool overlap(const Rect& a, const Rect& b) noexcept {
  if (isEmpty(a) || isEmpty(b)) {
    return false;
  }
  return a.x < b.x + b.w - kEps && b.x < a.x + a.w - kEps && a.y < b.y + b.h - kEps &&
         b.y < a.y + a.h - kEps;
}

template <std::size_t N>
bool noneOverlap(const std::array<Rect, N>& rects) noexcept {
  for (std::size_t i = 0; i < N; ++i) {
    for (std::size_t j = i + 1; j < N; ++j) {
      if (overlap(rects[i], rects[j])) {
        return false;
      }
    }
  }
  return true;
}

bool wholeUnits(float value) noexcept { return value == std::floor(value); }

Layout touch(float width, float height, float top = 0.0F, float bottom = 0.0F, float left = 0.0F,
             float right = 0.0F) noexcept {
  return computeLayout({width, height, top, bottom, left, right}, Idiom::Touch);
}

// What must hold for every touch layout, whatever the window.
void checkTouchLayout(const Layout& l) {
  const Rect canvas{0.0F, 0.0F, l.canvasWidth, l.canvasHeight};
  CHECK(l.idiom == Idiom::Touch);
  CHECK(std::isfinite(l.canvasWidth) && l.canvasWidth > 0.0F);
  CHECK(std::isfinite(l.canvasHeight) && l.canvasHeight > 0.0F);
  CHECK(std::isfinite(l.pointsPerUnit) && l.pointsPerUnit > 0.0F);

  // No rectangle has a negative size or lies off the canvas.
  const std::array all{l.safe,
                       l.playfield,
                       l.hud,
                       l.deck,
                       l.dialog,
                       l.pauseButton,
                       l.nudgeLeftButton,
                       l.centreButton,
                       l.nudgeRightButton,
                       l.flipperLeftZone,
                       l.flipperRightZone};
  for (const Rect& r : all) {
    CHECK(wellFormed(r));
    CHECK(inside(r, canvas));
  }

  // The playfield is the table, 1:1, at whole units.
  CHECK(l.playfield.w == pinball::table::kWidth && l.playfield.h == pinball::table::kHeight);
  CHECK(wholeUnits(l.playfield.x) && wholeUnits(l.playfield.y));
  // The composition fits the safe area. A block is snapped to whole units,
  // which on a tight fit can cost a part of one unit.
  CHECK(l.safe.w > 0.0F && l.safe.h > 0.0F);
  CHECK(inside(l.playfield, l.safe, 1.0F));
  CHECK(inside(l.hud, l.safe, 1.0F));
  CHECK(inside(l.dialog, l.safe, 1.0F));

  // The blocks and the controls keep clear of each other.
  CHECK(noneOverlap(std::array{l.playfield, l.hud, l.deck}));
  const std::array buttons{l.pauseButton, l.nudgeLeftButton, l.centreButton, l.nudgeRightButton};
  CHECK(noneOverlap(buttons));
  for (const Rect& button : buttons) {
    CHECK(!isEmpty(button));
    CHECK(inside(button, l.safe, 1.0F));
    CHECK(!overlap(button, l.playfield));
    // Large enough to find with a finger, in either composition.
    CHECK(button.w >= kMinHitUnits && button.h >= kMinHitUnits);
  }

  // The flipper zones lie in the window, side by side, and reach both edges.
  CHECK(!isEmpty(l.flipperLeftZone) && !isEmpty(l.flipperRightZone));
  CHECK(!overlap(l.flipperLeftZone, l.flipperRightZone));
  CHECK(l.flipperLeftZone.x == 0.0F);
  CHECK(near(l.flipperLeftZone.x + l.flipperLeftZone.w, l.flipperRightZone.x));
  CHECK(near(l.flipperRightZone.x + l.flipperRightZone.w, l.canvasWidth));
  CHECK(near(l.flipperLeftZone.y + l.flipperLeftZone.h, l.canvasHeight));
  CHECK(l.flipperLeftZone.y == l.flipperRightZone.y);
  // Both flippers' halves of the table are in their zones.
  CHECK(contains(l.flipperLeftZone, l.playfield.x + 100.0F, l.playfield.y + 900.0F));
  CHECK(contains(l.flipperRightZone, l.playfield.x + 660.0F, l.playfield.y + 900.0F));

  if (l.composition == Composition::Stack) {
    // Strip, table and deck in one column, top to bottom, touching.
    CHECK(l.hud.w == kStackWidth && l.hud.h == kStripHeight);
    CHECK(l.hud.x == l.playfield.x && l.deck.x == l.playfield.x && l.deck.w == kStackWidth);
    CHECK(l.hud.y + l.hud.h == l.playfield.y);
    CHECK(l.playfield.y + l.playfield.h == l.deck.y);
    CHECK(l.deck.h >= kDeckMinHeight && l.deck.h <= kDeckMaxHeight);
    CHECK(l.deck.y + l.deck.h <= l.safe.y + l.safe.h + 1.0F);
    CHECK(inside(l.pauseButton, l.hud));
    CHECK(inside(l.nudgeLeftButton, l.deck) && inside(l.centreButton, l.deck) &&
          inside(l.nudgeRightButton, l.deck));
    // The corners beside the row are the thumbs': in the deck, clear of
    // every button.
    for (const Rect& corner : {pinball::deck::kLeftCorner, pinball::deck::kRightCorner}) {
      const Rect placed{l.deck.x + corner.x, l.deck.y + corner.y, corner.w, corner.h};
      CHECK(inside(placed, l.deck));
      for (const Rect& button : buttons) {
        CHECK(!overlap(placed, button));
      }
    }
    // The zones start under the strip, and the row is symmetric about the
    // line between them.
    CHECK(l.flipperLeftZone.y == l.playfield.y);
    CHECK(inside(l.nudgeLeftButton, l.flipperLeftZone));
    CHECK(inside(l.nudgeRightButton, l.flipperRightZone));
    CHECK(near(l.flipperRightZone.x - (l.nudgeLeftButton.x + l.nudgeLeftButton.w),
               l.nudgeRightButton.x - l.flipperRightZone.x));
    CHECK(inside(l.dialog, l.playfield));
  } else {
    // Table left, panel right, touching. The controls are on the panel.
    CHECK(isEmpty(l.deck));
    CHECK(l.hud.x == l.playfield.x + l.playfield.w && l.hud.y == l.playfield.y);
    CHECK(l.hud.w == pinball::panel::kWidth && l.hud.h == pinball::panel::kHeight);
    for (const Rect& button : buttons) {
      CHECK(inside(button, l.hud));
    }
    CHECK(l.flipperLeftZone.y == 0.0F);
  }
}

// The overlays' rectangles: in their box, the box on the canvas, no control
// on top of another.
void checkOverlays(const Layout& l) {
  const Rect canvas{0.0F, 0.0F, l.canvasWidth, l.canvasHeight};

  const MenuLayout menu = menuLayout(l);
  const std::array menuButtons{menu.resume, menu.newGame, menu.sound, menu.music, menu.about};
  CHECK(inside(menu.box, canvas) && inside(menu.box, l.dialog));
  CHECK(noneOverlap(menuButtons));
  for (const Rect& button : menuButtons) {
    CHECK(wellFormed(button) && inside(button, menu.box));
    CHECK(button.w >= kMinHitUnits && button.h >= kMinHitUnits);
  }

  const AboutLayout about = aboutLayout(l);
  const std::array aboutParts{about.close, about.tabs[0],     about.tabs[1],  about.tabs[2],
                              about.body,  about.privacyLink, about.previous, about.next};
  CHECK(same(about.box, l.dialog));
  CHECK(noneOverlap(aboutParts));
  for (const Rect& part : aboutParts) {
    CHECK(wellFormed(part) && inside(part, about.box));
    CHECK(part.w >= kMinHitUnits && part.h >= kMinHitUnits);
  }

  const InitialsLayout initials = initialsLayout(l);
  CHECK(inside(initials.box, canvas) && inside(initials.box, l.dialog));
  const std::array initialsParts{
      initials.up[0],   initials.up[1],   initials.up[2],   initials.slot[0], initials.slot[1],
      initials.slot[2], initials.down[0], initials.down[1], initials.down[2], initials.ok};
  CHECK(noneOverlap(initialsParts));
  for (const Rect& part : initialsParts) {
    CHECK(wellFormed(part));
    CHECK(isEmpty(part) || inside(part, initials.box));
  }
  for (std::size_t i = 0; i < initials.slot.size(); ++i) {
    CHECK(!isEmpty(initials.slot[i]));
    // A touch screen has the arrows and the OK button, a desktop does not.
    CHECK(isEmpty(initials.up[i]) == (l.idiom == Idiom::Desktop));
    CHECK(isEmpty(initials.down[i]) == (l.idiom == Idiom::Desktop));
  }
  CHECK(isEmpty(initials.ok) == (l.idiom == Idiom::Desktop));
}

void testDesktop() {
  // The numbers the game was drawn with before there was a layout: a
  // 1280 x 960 canvas, the table at the origin, the panel beside it, the
  // About box and the initials prompt where they have always been.
  const Layout l = desktopLayout();
  CHECK(l.composition == Composition::Classic && l.idiom == Idiom::Desktop);
  CHECK(l.canvasWidth == 1280.0F && l.canvasHeight == 960.0F);
  CHECK(l.pointsPerUnit == 1.0F);
  CHECK(same(l.safe, {0.0F, 0.0F, 1280.0F, 960.0F}));
  CHECK(same(l.playfield, {0.0F, 0.0F, 768.0F, 960.0F}));
  CHECK(same(l.playfield, {0.0F, 0.0F, pinball::table::kWidth, pinball::table::kHeight}));
  CHECK(same(l.hud, {768.0F, 0.0F, 512.0F, 960.0F}));
  CHECK(
      same(l.hud, {pinball::panel::kLeft, 0.0F, pinball::panel::kWidth, pinball::panel::kHeight}));
  CHECK(same(l.dialog, {190.0F, 90.0F, 900.0F, 780.0F}));
  CHECK(same(aboutLayout(l).box, {190.0F, 90.0F, 900.0F, 780.0F}));
  CHECK(same(initialsLayout(l).box, {290.0F, 250.0F, 700.0F, 460.0F}));
  // No touch controls.
  for (const Rect& r : {l.deck, l.pauseButton, l.nudgeLeftButton, l.centreButton,
                        l.nudgeRightButton, l.flipperLeftZone, l.flipperRightZone}) {
    CHECK(isEmpty(r));
  }
  checkOverlays(l);

  // The desktop idiom does not look at the window at all.
  for (const Surface& s : {Surface{1280.0F, 960.0F, 0.0F, 0.0F, 0.0F, 0.0F},
                           Surface{393.0F, 852.0F, 59.0F, 34.0F, 0.0F, 0.0F},
                           Surface{0.0F, -1.0F, 5.0F, 5.0F, 5.0F, 5.0F}}) {
    const Layout other = computeLayout(s, Idiom::Desktop);
    CHECK(other.composition == l.composition && other.idiom == l.idiom);
    CHECK(other.canvasWidth == l.canvasWidth && other.canvasHeight == l.canvasHeight);
    CHECK(same(other.playfield, l.playfield) && same(other.hud, l.hud));
    CHECK(same(other.dialog, l.dialog) && isEmpty(other.centreButton));
  }

  // The panel's touch controls stay on the panel, clear of the wells.
  namespace panel = pinball::panel;
  const Rect panelArea{panel::kLeft, 0.0F, panel::kWidth, panel::kHeight};
  for (const Rect& r : {panel::kTitleArea, panel::kControlsArea, panel::kNudgeLeftButton,
                        panel::kCentreButton, panel::kNudgeRightButton, panel::kPauseButton}) {
    CHECK(inside(r, panelArea, 0.0F));
    for (const Rect& well : panel::kWells) {
      CHECK(!overlap(r, well));
    }
  }
  CHECK(inside(panel::kNudgeLeftButton, panel::kControlsArea, 0.0F));
  CHECK(inside(panel::kNudgeRightButton, panel::kControlsArea, 0.0F));
  // What a touch may land in is at least the smallest size, holds what is
  // drawn, stays on the panel, and no two of them overlap.
  const std::array hits{panel::kNudgeLeftHit, panel::kCentreHit, panel::kNudgeRightHit,
                        panel::kPauseHit};
  const std::array drawn{panel::kNudgeLeftButton, panel::kCentreButton, panel::kNudgeRightButton,
                         panel::kPauseButton};
  CHECK(noneOverlap(hits));
  for (std::size_t i = 0; i < hits.size(); ++i) {
    CHECK(hits[i].w >= kMinHitUnits && hits[i].h >= kMinHitUnits);
    CHECK(inside(drawn[i], hits[i], 0.0F) && inside(hits[i], panelArea, 0.0F));
    CHECK(!overlap(hits[i], panel::kFlipperCorner));
  }
  CHECK(!overlap(panel::kTouchTitleArea, panel::kPauseButton));
  CHECK(inside(panel::kTouchTitleArea, panel::kTitleArea, 0.0F));
  CHECK(inside(panel::kFlipperCorner, panel::kControlsArea, 0.0F));
}

struct Expected {
  std::string_view name;
  Composition composition;
  float unitsPerPoint;
  float canvasWidth;
  float canvasHeight;
  float deck;  // 0 for Classic.
};

void testPresets() {
  // The worked sizes of the design, to its three figures.
  constexpr std::array<Expected, 11> kExpected{{
      {"phone-short", Composition::Stack, 2.048F, 768.0F, 1366.0F, 133.0F},
      {"phone", Composition::Stack, 1.954F, 768.0F, 1665.0F, 291.0F},
      {"phone-pro", Composition::Stack, 1.910F, 768.0F, 1670.0F, 294.0F},
      {"pad-portrait-11", Composition::Stack, 1.141F, 951.5F, 1362.0F, 120.0F},
      {"phone-max", Composition::Stack, 1.745F, 768.0F, 1669.0F, 309.0F},
      {"duo-outer", Composition::Stack, 1.935F, 902.0F, 1312.0F, 120.0F},
      {"duo-inner", Composition::Stack, 1.380F, 923.0F, 1312.0F, 120.0F},
      {"pad-portrait", Composition::Stack, 0.992F, 1016.0F, 1356.0F, 120.0F},
      // The deck is what the safe area leaves under the table: the 24 and
      // 20 point insets come off the 1551 units of the window.
      {"pad-split", Composition::Stack, 1.515F, 768.0F, 1551.0F, 292.0F},
      {"pad-landscape", Composition::Classic, 0.980F, 1338.0F, 1003.0F, 0.0F},
      {"pad-landscape-11", Composition::Classic, 1.215F, 1451.0F, 1013.0F, 0.0F},
  }};
  CHECK(presets().size() == kExpected.size() + 1);
  for (const Expected& want : kExpected) {
    const Preset* const preset = findPreset(want.name);
    CHECK(preset != nullptr);
    if (preset == nullptr) {
      continue;
    }
    CHECK(preset->idiom == Idiom::Touch);
    const Layout l = computeLayout(preset->surface, preset->idiom);
    CHECK(l.composition == want.composition);
    CHECK(near(1.0F / l.pointsPerUnit, want.unitsPerPoint, 0.0006F));
    CHECK(near(l.canvasWidth, want.canvasWidth, 0.6F));
    CHECK(near(l.canvasHeight, want.canvasHeight, 0.6F));
    // Snapping to whole units may move the deck's top by a part of one.
    CHECK(near(l.deck.h, want.deck, 1.5F));
    checkTouchLayout(l);
    checkOverlays(l);
    // Every preset is a window the game should be given, so every control
    // is at least 44 points across, and so are the thumbs' corners.
    CHECK(meetsMinimumScene(preset->surface));
    for (const Rect& button :
         {l.pauseButton, l.nudgeLeftButton, l.centreButton, l.nudgeRightButton}) {
      CHECK(std::min(button.w, button.h) * l.pointsPerUnit >= kMinHitPoints);
    }
    if (l.composition == Composition::Stack) {
      const float cornerHeight = std::min(pinball::deck::kLeftCorner.h, l.deck.h - 20.0F);
      CHECK(std::min(pinball::deck::kLeftCorner.w, cornerHeight) * l.pointsPerUnit >=
            kMinHitPoints);
    }
  }

  const Preset* const desktop = findPreset("desktop");
  CHECK(desktop != nullptr && desktop->idiom == Idiom::Desktop);
  if (desktop != nullptr) {
    CHECK(same(computeLayout(desktop->surface, desktop->idiom).hud, desktopLayout().hud));
  }
  CHECK(findPreset("") == nullptr);
  CHECK(findPreset("Phone") == nullptr);
  CHECK(findPreset("phone-") == nullptr);
  // Names are unique.
  for (const Preset& a : presets()) {
    CHECK(findPreset(a.name) == &a);
  }
}

void testStackGeometry() {
  {  // A 9:19.5 phone with a notch and a home indicator: limited by width.
    const Layout l = touch(393.0F, 852.0F, 59.0F, 34.0F);
    CHECK(l.composition == Composition::Stack);
    CHECK(near(l.canvasWidth, 768.0F) && near(l.pointsPerUnit, 393.0F / 768.0F, 1e-5F));
    CHECK(near(l.safe.y, 59.0F * 768.0F / 393.0F) && near(l.safe.w, 768.0F));
    // The column is the whole width; the strip starts under the notch.
    CHECK(l.playfield.x == 0.0F);
    CHECK(l.hud.y >= l.safe.y && l.hud.y < l.safe.y + 1.0F);
    // The deck reaches the bottom of the safe area and stops short of the
    // home-indicator band, which is still flipper zone.
    CHECK(near(l.deck.y + l.deck.h, l.safe.y + l.safe.h));
    CHECK(l.deck.y + l.deck.h < l.canvasHeight - 60.0F);
    CHECK(near(l.flipperLeftZone.w, 384.0F) && near(l.flipperRightZone.w, 384.0F));
    // The rows of the strip and the deck, where the design puts them.
    CHECK(same(l.pauseButton, {l.hud.x + 8.0F, l.hud.y, 104.0F, 100.0F}));
    CHECK(same(l.nudgeLeftButton, {132.0F, l.deck.y + 12.0F, 96.0F, 96.0F}));
    CHECK(same(l.centreButton, {244.0F, l.deck.y + 12.0F, 280.0F, 96.0F}));
    CHECK(same(l.nudgeRightButton, {540.0F, l.deck.y + 12.0F, 96.0F, 96.0F}));
    CHECK(same(l.dialog, {24.0F, l.playfield.y + 40.0F, 720.0F, 880.0F}));
  }
  {  // A 3:4 tablet upright: limited by height, so the deck is at its
     // smallest and the spare width is two equal gutters.
    const Layout l = touch(768.0F, 1024.0F);
    CHECK(l.composition == Composition::Stack);
    CHECK(near(l.canvasHeight, 1312.0F) && near(l.canvasWidth, 984.0F));
    CHECK(l.hud.y == 0.0F && near(l.deck.h, kDeckMinHeight));
    CHECK(near(l.playfield.x, (984.0F - 768.0F) * 0.5F, 0.5F));
    CHECK(near(l.flipperLeftZone.w, l.canvasWidth * 0.5F, 0.5F));
    checkTouchLayout(l);
    // With insets the same holds inside the safe area.
    const Layout inset = touch(1024.0F, 1366.0F, 24.0F, 20.0F);
    CHECK(inset.composition == Composition::Stack);
    CHECK(near(inset.deck.h, kDeckMinHeight, 1.0F) && inset.playfield.x == 124.0F);
    CHECK(near(inset.safe.h, 1312.0F));
  }
  {  // A very tall window: the deck stops at its cap and the rest goes
     // above the strip.
    const Layout l = touch(300.0F, 900.0F);
    CHECK(l.composition == Composition::Stack);
    CHECK(near(l.deck.h, kDeckMaxHeight, 1.0F));
    CHECK(l.hud.y > 600.0F);
    CHECK(near(l.deck.y + l.deck.h, l.canvasHeight, 1.0F));
    CHECK(l.flipperLeftZone.y == l.playfield.y);
    checkTouchLayout(l);
  }
  {  // Insets on one side only: the column is centred in the safe area,
     // and the zones still reach the edges of the window.
    const Layout l = touch(900.0F, 1000.0F, 0.0F, 0.0F, 120.0F, 0.0F);
    CHECK(l.composition == Composition::Stack);
    CHECK(near(l.playfield.x + 384.0F, l.safe.x + l.safe.w * 0.5F, 0.5F));
    CHECK(l.flipperLeftZone.w > l.flipperRightZone.w);
    checkTouchLayout(l);
  }
}

void testChoice() {
  // Square-ish foldables: just narrower than the crossover (aspect 0.976)
  // is Stack, just wider is Classic, and a square is Classic.
  CHECK(touch(700.0F, 720.0F).composition == Composition::Stack);
  CHECK(touch(720.0F, 700.0F).composition == Composition::Classic);
  CHECK(touch(800.0F, 800.0F).composition == Composition::Classic);
  CHECK(touch(669.0F, 951.0F).composition == Composition::Stack);
  // Exactly at the crossover both fit equally well, and Stack is chosen.
  CHECK(touch(1280.0F, 1312.0F).composition == Composition::Stack);
  CHECK(touch(1281.0F, 1312.0F).composition == Composition::Classic);
  // The safe area decides, not the window.
  CHECK(touch(1000.0F, 1000.0F).composition == Composition::Classic);
  CHECK(touch(1000.0F, 1000.0F, 0.0F, 0.0F, 30.0F, 30.0F).composition == Composition::Stack);

  {  // Classic on a touch screen: centred in the safe area, with margins.
    const Layout l = touch(1366.0F, 1024.0F, 24.0F, 20.0F);
    CHECK(l.composition == Composition::Classic);
    CHECK(near(l.playfield.x, (l.canvasWidth - 1280.0F) * 0.5F, 0.5F));
    CHECK(l.playfield.y >= l.safe.y - 1.0F && l.playfield.y < l.safe.y + 1.0F);
    namespace panel = pinball::panel;
    CHECK(same(l.pauseButton, {l.playfield.x + panel::kPauseHit.x,
                               l.playfield.y + panel::kPauseHit.y, 96.0F, 96.0F}));
    CHECK(same(l.centreButton, {l.playfield.x + 896.0F, l.playfield.y + 860.0F, 132.0F, 96.0F}));
    CHECK(same(l.dialog, {l.playfield.x + 190.0F, l.playfield.y + 90.0F, 900.0F, 780.0F}));
    // Split down the middle of the composition; the corner right of the
    // button row is the right thumb's.
    CHECK(near(l.flipperRightZone.x, l.playfield.x + 640.0F));
    CHECK(contains(l.flipperRightZone, l.playfield.x + 1200.0F, l.playfield.y + 900.0F));
    CHECK(!contains(l.nudgeRightButton, l.playfield.x + 1200.0F, l.playfield.y + 900.0F));
  }
  {  // A wide, low window: Classic, limited by height.
    const Layout l = touch(2000.0F, 500.0F);
    CHECK(l.composition == Composition::Classic && near(l.canvasHeight, 960.0F));
    CHECK(l.playfield.y == 0.0F && l.playfield.x > 1000.0F);
    checkTouchLayout(l);
  }
}

void testAnyWindow() {
  // A sweep of aspect ratios and sizes, with and without insets.
  constexpr std::array kWidths{120.0F, 320.0F,  393.0F,  500.0F,  700.0F, 744.0F,
                               834.0F, 1024.0F, 1366.0F, 2048.0F, 4096.0F};
  constexpr std::array kAspects{0.30F, 0.40F, 0.46F, 0.5625F, 0.70F, 0.75F, 0.90F, 0.97F, 0.976F,
                                0.98F, 1.0F,  1.2F,  1.3334F, 1.6F,  1.78F, 2.2F,  3.0F};
  constexpr std::array<std::array<float, 4>, 5> kInsets{{{0.0F, 0.0F, 0.0F, 0.0F},
                                                         {59.0F, 34.0F, 0.0F, 0.0F},
                                                         {24.0F, 20.0F, 0.0F, 0.0F},
                                                         {0.0F, 21.0F, 59.0F, 59.0F},
                                                         {7.3F, 0.9F, 13.7F, 2.1F}}};
  for (const float width : kWidths) {
    for (const float aspect : kAspects) {
      for (const auto& inset : kInsets) {
        const float height = width / aspect;
        const Layout l = touch(width, height, inset[0], inset[1], inset[2], inset[3]);
        checkTouchLayout(l);
        // The canvas is the window, scaled alike both ways. (A side under
        // the smallest believed counts as that.)
        const float believed = std::max(height, kMinSurfacePoints);
        CHECK(near(l.canvasWidth * l.pointsPerUnit, width, width * 1e-4F));
        CHECK(near(l.canvasHeight * l.pointsPerUnit, believed, believed * 1e-4F));
        // The composition is as large as the safe area allows: it touches
        // two opposite sides of it.
        const bool stacked = l.composition == Composition::Stack;
        const float contentWidth = stacked ? kStackWidth : kClassicWidth;
        const float contentHeight = stacked ? kStackMinHeight : kClassicHeight;
        CHECK(near(l.safe.w, contentWidth, 0.05F) || near(l.safe.h, contentHeight, 0.05F));
        CHECK(l.safe.w >= contentWidth - 0.05F && l.safe.h >= contentHeight - 0.05F);
      }
    }
  }
  checkOverlays(touch(393.0F, 852.0F, 59.0F, 34.0F));
  checkOverlays(touch(1366.0F, 1024.0F, 24.0F, 20.0F));
}

void testDegenerate() {
  // Windows too small to play in, and values no window should report: still
  // a layout with every rectangle in place, and never a number that is not
  // one.
  constexpr float kNan = std::numeric_limits<float>::quiet_NaN();
  constexpr float kInf = std::numeric_limits<float>::infinity();
  const std::initializer_list<Surface> surfaces{
      {1.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F},
      {1.0F, 3.0F, 0.0F, 0.0F, 0.0F, 0.0F},
      {10.0F, 10.0F, 0.0F, 0.0F, 0.0F, 0.0F},
      {40.0F, 90.0F, 5.0F, 3.0F, 0.0F, 0.0F},
      {64.0F, 48.0F, 1.0F, 1.0F, 1.0F, 1.0F},
      {0.0F, 0.0F, 0.0F, 0.0F, 0.0F, 0.0F},
      {-320.0F, -480.0F, 0.0F, 0.0F, 0.0F, 0.0F},
      {kNan, kNan, kNan, kNan, kNan, kNan},
      {kInf, kInf, kInf, kInf, kInf, kInf},
      {393.0F, 852.0F, -59.0F, -34.0F, -1.0F, kNan},
      // Insets larger than the window.
      {393.0F, 852.0F, 900.0F, 900.0F, 400.0F, 400.0F},
      {393.0F, 852.0F, 852.0F, 0.0F, 0.0F, 393.0F},
      {100.0F, 100.0F, kInf, 0.0F, 0.0F, kInf},
  };
  for (const Surface& s : surfaces) {
    checkTouchLayout(computeLayout(s, Idiom::Touch));
  }
  // Sizes far beyond any screen are held to the largest believed, so the
  // canvas stays small enough for whole units to be exact.
  constexpr float kHuge = 3.0e38F;
  for (const Surface& s : {Surface{kHuge, kHuge, 0.0F, 0.0F, 0.0F, 0.0F},
                           Surface{kHuge, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F},
                           Surface{1.0F, kHuge, kHuge, kHuge, kHuge, kHuge},
                           Surface{kHuge, kHuge, kHuge, kHuge, kHuge, kHuge},
                           Surface{kMaxSurfacePoints, kMinSurfacePoints, 0.0F, 48.0F, 0.0F, 0.0F}}) {
    const Layout l = computeLayout(s, Idiom::Touch);
    checkTouchLayout(l);
    CHECK(l.canvasWidth <= kMaxCanvasUnits && l.canvasHeight <= kMaxCanvasUnits);
    CHECK(wholeUnits(l.playfield.x + 1.0F) && wholeUnits(l.deck.y + 1.0F));
  }
  CHECK(near(computeLayout({kHuge, kHuge, 0.0F, 0.0F, 0.0F, 0.0F}, Idiom::Touch).pointsPerUnit,
             kMaxSurfacePoints / kClassicWidth, 0.01F));
  // A window under the smallest believed counts as that size.
  CHECK(same(touch(1.0F, 1.0F).playfield, touch(64.0F, 64.0F).playfield));
  CHECK(touch(10.0F, 3.0F).canvasWidth == touch(64.0F, 64.0F).canvasWidth);
  // Negative insets count as none.
  const Layout plain = touch(393.0F, 852.0F);
  const Layout negative = touch(393.0F, 852.0F, -59.0F, -34.0F, -1.0F, -1.0F);
  CHECK(same(plain.safe, negative.safe) && same(plain.deck, negative.deck));
  // Insets that would leave nothing leave a quarter of each side.
  const Layout swallowed = touch(400.0F, 800.0F, 900.0F, 900.0F, 400.0F, 400.0F);
  CHECK(near(swallowed.safe.w * swallowed.pointsPerUnit, 100.0F, 0.01F));
  CHECK(near(swallowed.safe.h * swallowed.pointsPerUnit, 200.0F, 0.01F));
}

// From the smallest scene up, whatever the shape and the insets, everything
// a finger must find is at least 44 points across.
void testMinimumHitSize() {
  constexpr std::array kWidths{352.0F, 353.0F, 375.0F, 402.0F, 440.0F,  507.0F, 600.0F,
                               669.0F, 744.0F, 834.0F, 1024.0F, 1366.0F, 2048.0F, 4096.0F};
  constexpr std::array kHeights{602.0F, 603.0F,  640.0F,  667.0F,  700.0F,  852.0F, 956.0F,
                                1024.0F, 1194.0F, 1366.0F, 2048.0F, 2732.0F, 4096.0F};
  constexpr std::array<std::array<float, 4>, 4> kInsets{{{0.0F, 0.0F, 0.0F, 0.0F},
                                                         {62.0F, 34.0F, 0.0F, 0.0F},
                                                         {24.0F, 20.0F, 0.0F, 0.0F},
                                                         {0.0F, 21.0F, 62.0F, 62.0F}}};
  const auto bigEnough = [](const Rect& r, const Layout& l) noexcept {
    return std::min(r.w, r.h) * l.pointsPerUnit >= kMinHitPoints - 0.001F;
  };
  for (const float safeWidth : kWidths) {
    for (const float safeHeight : kHeights) {
      for (const auto& inset : kInsets) {
        // The sizes above are of the safe area; the window is that and its insets.
        const Surface surface{safeWidth + inset[2] + inset[3], safeHeight + inset[0] + inset[1],
                              inset[0], inset[1], inset[2], inset[3]};
        CHECK(meetsMinimumScene(surface));
        const Layout l = computeLayout(surface, Idiom::Touch);
        CHECK(kMinHitUnits * l.pointsPerUnit >= kMinHitPoints - 0.001F);
        for (const Rect& button :
             {l.pauseButton, l.nudgeLeftButton, l.centreButton, l.nudgeRightButton}) {
          CHECK(bigEnough(button, l));
        }
        const MenuLayout menu = menuLayout(l);
        for (const Rect& r : {menu.resume, menu.newGame, menu.sound, menu.music, menu.about}) {
          CHECK(bigEnough(r, l));
        }
        const AboutLayout about = aboutLayout(l);
        for (const Rect& r : {about.close, about.tabs[0], about.tabs[1], about.tabs[2],
                              about.privacyLink, about.previous, about.next}) {
          CHECK(bigEnough(r, l));
        }
        const InitialsLayout initials = initialsLayout(l);
        for (std::size_t i = 0; i < initials.slot.size(); ++i) {
          CHECK(bigEnough(initials.up[i], l) && bigEnough(initials.slot[i], l) &&
                bigEnough(initials.down[i], l));
        }
        CHECK(bigEnough(initials.ok, l));
      }
    }
  }
  // Just under the smallest scene on either side is not one, and the limit
  // is no larger than it needs to be: a square of the smallest width is too
  // small for either composition.
  CHECK(!meetsMinimumScene({351.0F, 602.0F, 0.0F, 0.0F, 0.0F, 0.0F}));
  CHECK(!meetsMinimumScene({352.0F, 601.0F, 0.0F, 0.0F, 0.0F, 0.0F}));
  CHECK(!meetsMinimumScene({402.0F, 650.0F, 62.0F, 34.0F, 0.0F, 0.0F}));
  CHECK(kMinHitUnits * touch(352.0F, 352.0F).pointsPerUnit < kMinHitPoints);
}

// A block that exactly fills its range never ends past it by more than the
// slack of the arithmetic, and never starts more than a unit before it.
void testExactFit() {
  for (float points = 300.0F; points < 1400.0F; points += 0.37F) {
    for (const float top : {0.0F, 20.0F, 47.0F, 59.0F, 62.0F}) {
      // Width-limited and height-limited in turn.
      for (const Layout& l : {touch(points, points * 4.0F, top, 34.0F),
                              touch(points * 3.0F, points, top, 21.0F),
                              touch(points, points * 1312.0F / 768.0F + top, top, 0.0F)}) {
        const Rect& top_block = l.composition == Composition::Stack ? l.hud : l.playfield;
        const float bottom = l.composition == Composition::Stack ? l.deck.y + l.deck.h
                                                                  : l.playfield.y + 960.0F;
        CHECK(top_block.y > l.safe.y - 1.0F);
        CHECK(bottom <= l.safe.y + l.safe.h + kEps);
        CHECK(l.playfield.x > l.safe.x - 1.0F);
        const float right = l.composition == Composition::Stack ? l.playfield.x + 768.0F
                                                                 : l.hud.x + l.hud.w;
        CHECK(right <= l.safe.x + l.safe.w + kEps);
      }
    }
  }
}

void testSurfaceSpec() {
  const auto parse = [](std::string_view text) { return parseSurfaceSpec(text, presets()); };
  {
    const auto spec = parse("phone-pro");
    CHECK(spec && spec->idiom == Idiom::Touch && spec->scale == 0);
    CHECK(spec && spec->surface.width == 402.0F && spec->surface.safeTop == 62.0F);
  }
  CHECK(parse("desktop") && parse("desktop")->idiom == Idiom::Desktop);
  {
    const auto spec = parse("402x874@3+62,34,0,0");
    CHECK(spec && spec->scale == 3 && spec->idiom == Idiom::Touch);
    CHECK(spec && spec->surface.width == 402.0F && spec->surface.height == 874.0F);
    CHECK(spec && spec->surface.safeTop == 62.0F && spec->surface.safeBottom == 34.0F &&
          spec->surface.safeLeft == 0.0F && spec->surface.safeRight == 0.0F);
  }
  {
    const auto spec = parse("pad-split@2+0,21,59,58");
    CHECK(spec && spec->scale == 2 && spec->surface.width == 507.0F);
    CHECK(spec && spec->surface.safeTop == 0.0F && spec->surface.safeBottom == 21.0F &&
          spec->surface.safeLeft == 59.0F && spec->surface.safeRight == 58.0F);
  }
  CHECK(parse("100x100") && parse("4096x4096") && parse("0820x1180"));
  CHECK(parse("820x1180@1") && parse("820x1180@4") && parse("820x1180+0,0,0,0"));
  // Insets that leave a quarter of the side are the most accepted.
  CHECK(parse("400x800+300,300,150,150") && !parse("400x800+301,300,0,0") &&
        !parse("400x800+0,0,151,150"));
  for (const std::string_view bad :
       {"", " ", "phone ", " phone", "Phone", "phone-", "99x800", "800x99", "4097x800",
        "800x4097", "800", "800x", "x800", "800X600", "800x600x2", "-800x600", "+800x600",
        "8 00x600", "800.0x600", "0x320x480", "800x600@", "800x600@0", "800x600@5",
        "800x600@2@2", "800x600@-1", "800x600@2.0", "800x600+", "800x600+1,2,3", "800x600+1,2,3,4,5",
        "800x600+1,2,3,", "800x600+,1,2,3", "800x600+1,,2,3", "800x600+1, 2,3,4",
        "800x600+-1,2,3,4", "800x600+1000,0,0,0", "800x600+1,2,3,4+1,2,3,4", "800x600+1,2,3,4@2",
        "phone@", "phone+", "99999x99999", "18446744073709551617x800",
        "800x600\n", "800x600;rm"}) {
    CHECK(!parse(bad));
  }
  CHECK(!parse(std::string_view("800x600\0", 8)));
  CHECK(!parse(std::string(65, '1')));
  // With no presets given, only sizes are understood.
  CHECK(!parseSurfaceSpec("phone", {}) && parseSurfaceSpec("320x480", {}));
  // Whatever is accepted is a window computeLayout takes as it is.
  for (const std::string_view good : {"100x4096+999,999,0,0", "4096x100@4", "352x602"}) {
    const auto spec = parse(good);
    CHECK(spec.has_value());
    if (spec) {
      const Layout l = computeLayout(spec->surface, spec->idiom);
      checkTouchLayout(l);
      CHECK(near(l.safe.y * l.pointsPerUnit, spec->surface.safeTop, 0.01F));
    }
  }
}

// Stand-ins for a game with one ball and for one with several, to hold the
// drawing code's adapters (platform/GameViews.hpp) to both.
struct OneBallRules {
  bool over = false;
  bool inPlay = false;
  bool gameOver() const noexcept { return over; }
  bool ballInPlay() const noexcept { return inPlay; }
};
struct OneBallGame {
  OneBallRules state;
  const OneBallRules& rules() const noexcept { return state; }
  pinball::BallView ball(float alpha) const {
    return {.position = {alpha, 2.0F}};
  }
};
struct ManyBalls {
  std::array<pinball::BallView, 12> items{};
  std::size_t count = 0;
  auto begin() const noexcept { return items.begin(); }
  auto end() const noexcept { return items.begin() + static_cast<std::ptrdiff_t>(count); }
};
enum class FakeMode : std::uint8_t { None, Turbo, Academy };
struct ManyBallRules : OneBallRules {
  int ballsLive() const noexcept { return 3; }
  int locksHeld() const noexcept { return 1; }
  bool lockLit() const noexcept { return true; }
  FakeMode multiball() const noexcept { return mode; }
  bool multiballJackpotLit() const noexcept { return true; }
  bool superJackpotLit() const noexcept { return false; }
  FakeMode mode = FakeMode::Academy;
};
struct ManyBallGame {
  ManyBallRules state;
  std::size_t balls_ = 3;
  bool waiting = false;
  const ManyBallRules& rules() const noexcept { return state; }
  pinball::BallView ball(float) const { return {.position = {-1.0F, -1.0F}}; }
  ManyBalls balls(float alpha) const {
    ManyBalls out;
    out.count = balls_;
    for (std::size_t i = 0; i < out.count; ++i) {
      out.items[i].position = {static_cast<float>(i), alpha};
    }
    return out;
  }
  bool ballAwaitingLaunch() const noexcept { return waiting; }
};

void testGameViews() {
  using namespace pinball;
  {  // One ball: the game's single ball, and guesses from the rules.
    OneBallGame game;
    const DrawnBalls drawn = ballsToDraw(game, 0.25F);
    CHECK(drawn.count == 1 && drawn.span().size() == 1 && drawn.items[0].position.x == 0.25F);
    CHECK(touchContext(game).ballAwaitingLaunch && !touchContext(game).gameOver);
    game.state.inPlay = true;
    CHECK(!touchContext(game).ballAwaitingLaunch);
    game.state.over = true;
    game.state.inPlay = false;
    CHECK(touchContext(game).gameOver && !touchContext(game).ballAwaitingLaunch);
    const MultiballView view = multiballView(game.rules());
    CHECK(!view.known && !view.running && view.ballsLive == 1 && view.locksHeld == 0);
  }
  {  // Several: every ball the game gives, in its order, and its own word
     // on the launch lane.
    ManyBallGame game;
    const DrawnBalls drawn = ballsToDraw(game, 0.5F);
    CHECK(drawn.count == 3 && drawn.items[2].position.x == 2.0F &&
          drawn.items[0].position.y == 0.5F);
    CHECK(!touchContext(game).ballAwaitingLaunch);
    game.waiting = true;
    CHECK(touchContext(game).ballAwaitingLaunch);
    // More than can be drawn: the rest are left out.
    game.balls_ = 12;
    DrawnBalls many = ballsToDraw(game, 1.0F);
    CHECK(many.count == kMaxDrawnBalls && many.span().size() == kMaxDrawnBalls);
    many.add({});
    CHECK(many.count == kMaxDrawnBalls);
    const MultiballView view = multiballView(game.rules());
    CHECK(view.known && view.running && view.ballsLive == 3 && view.locksHeld == 1 &&
          view.lockLit && view.jackpotLit && !view.superJackpotLit);
    game.state.mode = FakeMode::None;
    CHECK(!multiballView(game.rules()).running);
  }
  {  // The real game, whichever kind it is today.
    const Game game;
    const DrawnBalls drawn = ballsToDraw(game, 1.0F);
    CHECK(drawn.count >= 1 && drawn.items[0].position.x == game.ball(1.0F).position.x);
    CHECK(touchContext(game).ballAwaitingLaunch && !touchContext(game).gameOver);
  }
}

void testContains() {
  constexpr Rect r{10.0F, 20.0F, 30.0F, 40.0F};
  static_assert(contains(r, 10.0F, 20.0F));
  static_assert(contains(r, 39.9F, 59.9F));
  static_assert(!contains(r, 40.0F, 30.0F));
  static_assert(!contains(r, 20.0F, 60.0F));
  static_assert(!contains(r, 9.9F, 30.0F));
  static_assert(!contains(Rect{5.0F, 5.0F, 0.0F, 0.0F}, 5.0F, 5.0F));
  CHECK(contains(r, 25.0F, 40.0F));
  CHECK(!contains(r, std::numeric_limits<float>::quiet_NaN(), 40.0F));
  // Adjoining rectangles never both hold a point.
  constexpr Rect left{0.0F, 0.0F, 384.0F, 100.0F};
  constexpr Rect right{384.0F, 0.0F, 384.0F, 100.0F};
  CHECK(!contains(left, 384.0F, 50.0F) && contains(right, 384.0F, 50.0F));
}

}  // namespace

void testScreenLayout() {
  testDesktop();
  testPresets();
  testStackGeometry();
  testChoice();
  testAnyWindow();
  testDegenerate();
  testMinimumHitSize();
  testExactFit();
  testSurfaceSpec();
  testGameViews();
  testContains();
}
