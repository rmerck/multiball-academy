#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

#include "Check.hpp"
#include "Suites.hpp"
#include "core/OverlayLayout.hpp"
#include "core/ScreenLayout.hpp"
#include "core/TouchControls.hpp"

namespace {

using pinball::CanvasPoint;
using pinball::InputState;
using pinball::OverlayTaps;
using pinball::OverlayTarget;
using pinball::TouchControls;
using pinball::TouchPhase;
using pinball::screen::Layout;
using pinball::screen::Rect;
using Centre = TouchControls::View::Centre;
using Kind = OverlayTarget::Kind;

constexpr TouchControls::Context kPlaying{.ballAwaitingLaunch = false, .gameOver = false};
constexpr TouchControls::Context kAwaiting{.ballAwaitingLaunch = true, .gameOver = false};
constexpr TouchControls::Context kOver{.ballAwaitingLaunch = false, .gameOver = true};

Layout presetLayout(std::string_view name) {
  const pinball::screen::Preset* preset = pinball::screen::findPreset(name);
  CHECK(preset != nullptr);
  return preset != nullptr ? pinball::screen::computeLayout(preset->surface, preset->idiom)
                           : pinball::screen::desktopLayout();
}

CanvasPoint centreOf(const Rect& r) { return {r.x + r.w * 0.5F, r.y + r.h * 0.5F}; }

bool onAButton(const Layout& layout, CanvasPoint p) {
  using pinball::screen::contains;
  return contains(layout.pauseButton, p.x, p.y) || contains(layout.nudgeLeftButton, p.x, p.y) ||
         contains(layout.centreButton, p.x, p.y) || contains(layout.nudgeRightButton, p.x, p.y);
}

// A point of a flipper zone that no button covers: its bottom corner on the
// outer side. The tests are written against the layout's rectangles, not
// their numbers, so this is checked and not assumed.
CanvasPoint flipperPoint(const Layout& layout, bool left) {
  const Rect& zone = left ? layout.flipperLeftZone : layout.flipperRightZone;
  const CanvasPoint p{left ? zone.x + 4.0F : zone.x + zone.w - 4.0F, zone.y + zone.h - 4.0F};
  CHECK(pinball::screen::contains(zone, p.x, p.y));
  CHECK(!onAButton(layout, p));
  return p;
}

void send(TouchControls& controls, const Layout& layout, std::uint64_t id, TouchPhase phase,
          CanvasPoint p, const TouchControls::Context& context = kPlaying) {
  controls.handle({.id = id, .phase = phase, .x = p.x, .y = p.y}, layout, context);
}

bool nothingHeld(const InputState& s) {
  return !s.leftFlipper && !s.rightFlipper && !s.plunger && !s.nudgeLeft && !s.nudgeRight &&
         !s.nudgeUp && !s.newGame;
}

void testFlipperZones(const Layout& layout) {
  const CanvasPoint left = flipperPoint(layout, true);
  const CanvasPoint right = flipperPoint(layout, false);

  // A finger holds the flipper of the side it went down on.
  TouchControls controls;
  send(controls, layout, 7, TouchPhase::Down, left);
  CHECK(controls.held().leftFlipper && !controls.held().rightFlipper);
  CHECK(controls.view(kPlaying).left && !controls.view(kPlaying).right);
  // The press is latched once, for a simulation step that has yet to run.
  CHECK(controls.takePressed().leftFlipper);
  CHECK(nothingHeld(controls.takePressed()));

  // Sliding across the centre line does not change flippers.
  send(controls, layout, 7, TouchPhase::Move, right);
  CHECK(controls.held().leftFlipper && !controls.held().rightFlipper);
  CHECK(nothingHeld(controls.takePressed()));

  // Both at once, each finger its own.
  send(controls, layout, 8, TouchPhase::Down, right);
  CHECK(controls.held().leftFlipper && controls.held().rightFlipper);
  CHECK(controls.takePressed().rightFlipper);
  // The first finger lifts on the right half: it is the left flipper that
  // drops.
  send(controls, layout, 7, TouchPhase::Up, right);
  CHECK(!controls.held().leftFlipper && controls.held().rightFlipper);
  send(controls, layout, 8, TouchPhase::Up, right);
  CHECK(nothingHeld(controls.held()));

  // Two fingers on one side: the flipper stays up until both have lifted.
  send(controls, layout, 1, TouchPhase::Down, left);
  send(controls, layout, 2, TouchPhase::Down, left);
  send(controls, layout, 1, TouchPhase::Up, left);
  CHECK(controls.held().leftFlipper);
  send(controls, layout, 2, TouchPhase::Up, left);
  CHECK(!controls.held().leftFlipper);

  // A press and release between two steps is still seen as a press.
  send(controls, layout, 3, TouchPhase::Down, right);
  send(controls, layout, 3, TouchPhase::Up, right);
  CHECK(nothingHeld(controls.held()));
  CHECK(controls.takePressed().rightFlipper);

  // The zones reach the edges of the canvas, outside the safe area too.
  const Rect& zone = layout.flipperLeftZone;
  send(controls, layout, 4, TouchPhase::Down, {0.0F, zone.y + zone.h - 0.5F});
  CHECK(controls.held().leftFlipper);
  send(controls, layout, 4, TouchPhase::Up, {0.0F, zone.y + zone.h - 0.5F});
  CHECK(controls.takePressed().leftFlipper);
  // Off the canvas altogether there is nothing.
  send(controls, layout, 5, TouchPhase::Down, {-5.0F, zone.y + zone.h + 50.0F});
  CHECK(nothingHeld(controls.held()));
  CHECK(nothingHeld(controls.takePressed()) && !controls.takePauseRequest());
}

void testCentreButton(const Layout& layout) {
  const CanvasPoint centre = centreOf(layout.centreButton);
  CHECK(pinball::screen::contains(layout.centreButton, centre.x, centre.y));

  // With a ball waiting: hold to draw the plunger, let go to fire.
  TouchControls controls;
  CHECK(controls.view(kAwaiting).centreRole == Centre::Launch);
  send(controls, layout, 1, TouchPhase::Down, centre, kAwaiting);
  CHECK(controls.held().plunger && !controls.held().nudgeUp && !controls.held().newGame);
  CHECK(controls.view(kAwaiting).centre);
  // Never a flipper as well, though the button lies in a flipper zone.
  CHECK(!controls.held().leftFlipper && !controls.held().rightFlipper);
  // What the button does was settled when the finger went down: the label
  // and the control stay Launch while it is held, whatever the game does.
  send(controls, layout, 1, TouchPhase::Move, centre, kPlaying);
  CHECK(controls.held().plunger);
  CHECK(controls.view(kPlaying).centreRole == Centre::Launch);
  send(controls, layout, 1, TouchPhase::Up, centre, kPlaying);
  CHECK(!controls.held().plunger);
  CHECK(controls.takePressed().plunger);
  CHECK(controls.view(kPlaying).centreRole == Centre::Bump);

  // During play it bumps the table upward: once for each touch.
  send(controls, layout, 2, TouchPhase::Down, centre, kPlaying);
  CHECK(controls.held().nudgeUp && !controls.held().plunger);
  InputState pressed = controls.takePressed();
  CHECK(pressed.nudgeUp && !pressed.plunger && !pressed.newGame);
  // Still Bump for this finger if a ball arrives in the lane meanwhile, so
  // lifting it cannot fire the plunger.
  send(controls, layout, 2, TouchPhase::Move, centre, kAwaiting);
  CHECK(!controls.held().plunger);
  CHECK(controls.view(kAwaiting).centreRole == Centre::Bump);
  send(controls, layout, 2, TouchPhase::Up, centre, kAwaiting);
  CHECK(nothingHeld(controls.held()));
  CHECK(nothingHeld(controls.takePressed()));

  // After the game is over it starts a new one.
  CHECK(controls.view(kOver).centreRole == Centre::Play);
  CHECK(controls.view({.ballAwaitingLaunch = true, .gameOver = true}).centreRole == Centre::Play);
  send(controls, layout, 3, TouchPhase::Down, centre, kOver);
  CHECK(controls.held().newGame && !controls.held().plunger && !controls.held().nudgeUp);
  CHECK(controls.takePressed().newGame);
  send(controls, layout, 3, TouchPhase::Up, centre, kAwaiting);
  CHECK(nothingHeld(controls.held()));
}

void testNudgeAndPause(const Layout& layout) {
  const CanvasPoint nudgeLeft = centreOf(layout.nudgeLeftButton);
  const CanvasPoint nudgeRight = centreOf(layout.nudgeRightButton);
  const CanvasPoint pause = centreOf(layout.pauseButton);
  const CanvasPoint flipper = flipperPoint(layout, true);

  TouchControls controls;
  send(controls, layout, 1, TouchPhase::Down, nudgeLeft);
  CHECK(controls.held().nudgeLeft && !controls.held().nudgeRight);
  CHECK(!controls.held().leftFlipper && !controls.held().rightFlipper);
  CHECK(controls.view(kPlaying).nudgeLeft);
  InputState pressed = controls.takePressed();
  CHECK(pressed.nudgeLeft && !pressed.leftFlipper);
  // Sliding off a button onto the flipper pad does not flip.
  send(controls, layout, 1, TouchPhase::Move, flipper);
  CHECK(controls.held().nudgeLeft && !controls.held().leftFlipper);
  send(controls, layout, 1, TouchPhase::Up, flipper);
  CHECK(nothingHeld(controls.held()));

  send(controls, layout, 2, TouchPhase::Down, nudgeRight);
  CHECK(controls.held().nudgeRight && !controls.held().nudgeLeft);
  CHECK(!controls.held().rightFlipper);
  CHECK(controls.takePressed().nudgeRight);
  send(controls, layout, 2, TouchPhase::Up, nudgeRight);

  // Pause: asked for when the finger lifts inside the button, once.
  send(controls, layout, 3, TouchPhase::Down, pause);
  CHECK(controls.view(kPlaying).pause);
  CHECK(!controls.takePauseRequest());
  CHECK(nothingHeld(controls.held()));
  send(controls, layout, 3, TouchPhase::Up, pause);
  CHECK(controls.takePauseRequest());
  CHECK(!controls.takePauseRequest());
  CHECK(!controls.view(kPlaying).pause);
  CHECK(nothingHeld(controls.takePressed()));

  // A finger that leaves the button before lifting has not paused.
  send(controls, layout, 4, TouchPhase::Down, pause);
  send(controls, layout, 4, TouchPhase::Move, flipper);
  CHECK(nothingHeld(controls.held()));
  send(controls, layout, 4, TouchPhase::Up, flipper);
  CHECK(!controls.takePauseRequest());

  // Nor has one that slid onto it from a flipper pad.
  send(controls, layout, 5, TouchPhase::Down, flipper);
  send(controls, layout, 5, TouchPhase::Move, pause);
  CHECK(controls.held().leftFlipper);
  send(controls, layout, 5, TouchPhase::Up, pause);
  CHECK(!controls.takePauseRequest());
  CHECK(nothingHeld(controls.held()));

  // A touch the system took away pauses nothing.
  send(controls, layout, 6, TouchPhase::Down, pause);
  send(controls, layout, 6, TouchPhase::Cancel, pause);
  CHECK(!controls.takePauseRequest());
  CHECK(!controls.view(kPlaying).pause);
}

void testCancelling(const Layout& layout) {
  const CanvasPoint left = flipperPoint(layout, true);
  const CanvasPoint right = flipperPoint(layout, false);
  const CanvasPoint centre = centreOf(layout.centreButton);
  const CanvasPoint pause = centreOf(layout.pauseButton);

  // One cancelled finger lets go of its own control only.
  TouchControls controls;
  send(controls, layout, 1, TouchPhase::Down, left);
  send(controls, layout, 2, TouchPhase::Down, right);
  send(controls, layout, 1, TouchPhase::Cancel, left);
  CHECK(!controls.held().leftFlipper && controls.held().rightFlipper);

  // cancelAll lets go of everything, and the fingers that were down do
  // nothing more: not when they move, and not when they lift.
  send(controls, layout, 3, TouchPhase::Down, centre, kAwaiting);
  send(controls, layout, 4, TouchPhase::Down, pause);
  CHECK(controls.held().plunger);
  controls.cancelAll();
  CHECK(nothingHeld(controls.held()));
  const TouchControls::View view = controls.view(kAwaiting);
  CHECK(!view.left && !view.right && !view.centre && !view.pause);
  send(controls, layout, 2, TouchPhase::Move, left);
  send(controls, layout, 3, TouchPhase::Move, centre, kAwaiting);
  CHECK(nothingHeld(controls.held()));
  send(controls, layout, 4, TouchPhase::Up, pause);
  CHECK(!controls.takePauseRequest());
  send(controls, layout, 2, TouchPhase::Up, right);
  send(controls, layout, 3, TouchPhase::Up, centre, kAwaiting);
  CHECK(nothingHeld(controls.held()));
  // A press from before the cancel does not reach the game after it.
  CHECK(nothingHeld(controls.takePressed()));
  // Nor does a pause that was asked for and not yet taken.
  send(controls, layout, 4, TouchPhase::Down, pause);
  send(controls, layout, 4, TouchPhase::Up, pause);
  controls.cancelAll();
  CHECK(!controls.takePauseRequest());
  // The same finger, down again, works as usual.
  send(controls, layout, 2, TouchPhase::Down, right);
  CHECK(controls.held().rightFlipper);
  send(controls, layout, 2, TouchPhase::Up, right);

  // An event for a finger never seen is ignored.
  send(controls, layout, 99, TouchPhase::Move, left);
  send(controls, layout, 99, TouchPhase::Up, pause);
  send(controls, layout, 99, TouchPhase::Cancel, left);
  CHECK(nothingHeld(controls.held()));
  CHECK(!controls.takePauseRequest());

  // A second Down for a finger that never lifted replaces the first.
  send(controls, layout, 5, TouchPhase::Down, left);
  send(controls, layout, 5, TouchPhase::Down, right);
  CHECK(!controls.held().leftFlipper && controls.held().rightFlipper);
  send(controls, layout, 5, TouchPhase::Up, right);
  CHECK(nothingHeld(controls.held()));
}

void testTooManyFingers(const Layout& layout) {
  const CanvasPoint left = flipperPoint(layout, true);
  const CanvasPoint right = flipperPoint(layout, false);
  TouchControls controls;
  for (std::uint64_t id = 1; id <= TouchControls::kMaxTouches; ++id) {
    send(controls, layout, id, TouchPhase::Down, left);
  }
  CHECK(controls.held().leftFlipper);
  // One more than can be followed: ignored, now and when it moves or lifts.
  const std::uint64_t extra = TouchControls::kMaxTouches + 1;
  send(controls, layout, extra, TouchPhase::Down, right);
  CHECK(!controls.held().rightFlipper);
  CHECK(!controls.takePressed().rightFlipper);
  send(controls, layout, extra, TouchPhase::Move, right);
  send(controls, layout, extra, TouchPhase::Up, right);
  CHECK(controls.held().leftFlipper && !controls.held().rightFlipper);
  // Many more, with ids of every size.
  for (std::uint64_t id = 100; id < 200; ++id) {
    send(controls, layout, id * 0x0101010101010101ULL, TouchPhase::Down, right);
  }
  send(controls, layout, std::numeric_limits<std::uint64_t>::max(), TouchPhase::Down, right);
  send(controls, layout, 0, TouchPhase::Down, right);
  CHECK(!controls.held().rightFlipper);
  // Once a finger lifts there is room for another.
  send(controls, layout, 1, TouchPhase::Up, left);
  send(controls, layout, 0, TouchPhase::Down, right);
  CHECK(controls.held().leftFlipper && controls.held().rightFlipper);
  for (std::uint64_t id = 0; id <= TouchControls::kMaxTouches; ++id) {
    send(controls, layout, id, TouchPhase::Up, left);
  }
  CHECK(nothingHeld(controls.held()));
}

void testMargins() {
  // The Stack on a phone: bands above and below the safe area.
  const Layout phone = presetLayout("phone");
  CHECK(phone.composition == pinball::screen::Composition::Stack);
  CHECK(phone.safe.y > 0.0F && phone.safe.y + phone.safe.h < phone.canvasHeight);
  TouchControls controls;
  // Under the home indicator, below the safe area: still the flipper.
  const float bottom = phone.canvasHeight - 1.0F;
  CHECK(bottom > phone.safe.y + phone.safe.h);
  send(controls, phone, 1, TouchPhase::Down, {phone.canvasWidth * 0.25F, bottom});
  send(controls, phone, 2, TouchPhase::Down, {phone.canvasWidth * 0.75F, bottom});
  CHECK(controls.held().leftFlipper && controls.held().rightFlipper);
  controls.cancelAll();
  // In the band beside the notch, above the status strip: nothing.
  send(controls, phone, 3, TouchPhase::Down, {phone.canvasWidth * 0.25F, 1.0F});
  send(controls, phone, 4, TouchPhase::Down, {phone.canvasWidth * 0.75F, phone.safe.y * 0.5F});
  CHECK(nothingHeld(controls.held()));
  CHECK(nothingHeld(controls.takePressed()));
  // On the status strip itself, beside the pause button: nothing.
  send(controls, phone, 5, TouchPhase::Down,
       {phone.hud.x + phone.hud.w - 8.0F, phone.hud.y + phone.hud.h * 0.5F});
  CHECK(nothingHeld(controls.held()));
  send(controls, phone, 3, TouchPhase::Up, {phone.canvasWidth * 0.25F, 1.0F});
  send(controls, phone, 4, TouchPhase::Up, centreOf(phone.pauseButton));
  CHECK(!controls.takePauseRequest());

  // A tablet in portrait has gutters beside the column: flipper zone too.
  const Layout pad = presetLayout("pad-portrait");
  CHECK(pad.playfield.x > 8.0F);
  send(controls, pad, 6, TouchPhase::Down, {2.0F, pad.playfield.y + 400.0F});
  send(controls, pad, 7, TouchPhase::Down, {pad.canvasWidth - 2.0F, pad.playfield.y + 400.0F});
  CHECK(controls.held().leftFlipper && controls.held().rightFlipper);
}

void testLayoutChange() {
  // The window turns from portrait to landscape with fingers down. The
  // application lets go of everything; what was a flipper touch must not
  // become something else under the new rectangles.
  const Layout portrait = presetLayout("pad-portrait");
  const Layout landscape = presetLayout("pad-landscape");
  CHECK(portrait.composition != landscape.composition);
  TouchControls controls;
  const CanvasPoint left = flipperPoint(portrait, true);
  send(controls, portrait, 1, TouchPhase::Down, left);
  send(controls, portrait, 2, TouchPhase::Down, centreOf(portrait.centreButton), kAwaiting);
  send(controls, portrait, 3, TouchPhase::Down, centreOf(portrait.pauseButton));
  CHECK(controls.held().leftFlipper && controls.held().plunger);
  controls.cancelAll();
  CHECK(nothingHeld(controls.held()));
  // The old fingers move and lift over the new layout's controls.
  send(controls, landscape, 1, TouchPhase::Move, centreOf(landscape.centreButton), kAwaiting);
  send(controls, landscape, 2, TouchPhase::Move, flipperPoint(landscape, false), kAwaiting);
  send(controls, landscape, 3, TouchPhase::Up, centreOf(landscape.pauseButton));
  CHECK(nothingHeld(controls.held()));
  CHECK(nothingHeld(controls.takePressed()));
  CHECK(!controls.takePauseRequest());
  // A new touch is placed by the new layout.
  send(controls, landscape, 4, TouchPhase::Down, flipperPoint(landscape, false));
  CHECK(controls.held().rightFlipper && !controls.held().leftFlipper);

  // Without the cancel, a finger keeps the control it was given even when
  // the rectangles move under it: only the pause button looks at where the
  // finger lifts.
  TouchControls kept;
  send(kept, portrait, 1, TouchPhase::Down, left);
  send(kept, landscape, 1, TouchPhase::Move, flipperPoint(landscape, false));
  CHECK(kept.held().leftFlipper && !kept.held().rightFlipper);
}

void testDesktopHasNoTouchControls() {
  const Layout layout = pinball::screen::desktopLayout();
  TouchControls controls;
  std::uint64_t id = 0;
  for (float y = 0.0F; y <= 960.0F; y += 96.0F) {
    for (float x = 0.0F; x <= 1280.0F; x += 64.0F) {
      send(controls, layout, ++id % 7, TouchPhase::Down, {x, y}, kAwaiting);
      CHECK(nothingHeld(controls.held()));
      send(controls, layout, id % 7, TouchPhase::Up, {x, y}, kAwaiting);
    }
  }
  CHECK(nothingHeld(controls.takePressed()));
  CHECK(!controls.takePauseRequest());
  const TouchControls::View view = controls.view(kPlaying);
  CHECK(!view.left && !view.right && !view.centre && !view.nudgeLeft && !view.nudgeRight &&
        !view.pause);
  CHECK(view.centreRole == Centre::Bump);
}

void testCanvasPoint() {
  const Layout phone = presetLayout("phone");
  const CanvasPoint middle = pinball::canvasPoint(phone, 0.5F, 0.5F);
  CHECK(std::fabs(middle.x - phone.canvasWidth * 0.5F) < 0.01F);
  CHECK(std::fabs(middle.y - phone.canvasHeight * 0.5F) < 0.01F);
  const CanvasPoint origin = pinball::canvasPoint(phone, 0.0F, 0.0F);
  CHECK(origin.x == 0.0F && origin.y == 0.0F);
  const CanvasPoint corner = pinball::canvasPoint(phone, 1.0F, 1.0F);
  CHECK(corner.x == phone.canvasWidth && corner.y == phone.canvasHeight);
  // The far corner of the window is the far corner of the flipper zone.
  const CanvasPoint last = pinball::canvasPoint(phone, 0.999F, 0.999F);
  CHECK(pinball::screen::contains(phone.flipperRightZone, last.x, last.y));
  // A position that is not a number is on nothing.
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();
  for (const CanvasPoint bad : {pinball::canvasPoint(phone, nan, 0.5F),
                                pinball::canvasPoint(phone, 0.5F, nan),
                                pinball::canvasPoint(phone, inf, 0.5F),
                                pinball::canvasPoint(phone, 0.5F, -inf)}) {
    CHECK(std::isfinite(bad.x) && std::isfinite(bad.y));
    CHECK(!pinball::screen::contains(phone.flipperLeftZone, bad.x, bad.y));
    CHECK(!pinball::screen::contains(phone.flipperRightZone, bad.x, bad.y));
    CHECK(!onAButton(phone, bad));
    TouchControls controls;
    send(controls, phone, 1, TouchPhase::Down, bad);
    CHECK(nothingHeld(controls.held()));
  }
  // Fed raw, a position that is not a number is on nothing either.
  TouchControls controls;
  send(controls, phone, 1, TouchPhase::Down, {nan, nan});
  send(controls, phone, 2, TouchPhase::Down, {inf, 100.0F});
  CHECK(nothingHeld(controls.held()));
}

void testSystemCorners() {
  // The bottom corners of a tablet window are the system's resize handle:
  // 32 points in from either side and up from the bottom, in canvas units.
  const Layout l = pinball::screen::computeLayout({1024.0F, 1366.0F, 24.0F, 20.0F, 0.0F, 0.0F},
                                                  pinball::screen::Idiom::Touch);
  const float reach = pinball::kSystemCornerPoints / l.pointsPerUnit;
  CHECK(pinball::inSystemCorner(l, {l.canvasWidth - 1.0F, l.canvasHeight - 1.0F}));
  CHECK(pinball::inSystemCorner(l, {1.0F, l.canvasHeight - 1.0F}));
  CHECK(pinball::inSystemCorner(l, {reach - 0.5F, l.canvasHeight - reach + 0.5F}));
  CHECK(!pinball::inSystemCorner(l, {reach + 1.0F, l.canvasHeight - 1.0F}));
  CHECK(!pinball::inSystemCorner(l, {1.0F, l.canvasHeight - reach - 1.0F}));
  CHECK(!pinball::inSystemCorner(l, {l.canvasWidth * 0.5F, l.canvasHeight - 1.0F}));
  // The top corners and anything off the canvas are not.
  CHECK(!pinball::inSystemCorner(l, {1.0F, 1.0F}));
  CHECK(!pinball::inSystemCorner(l, {-1.0F, -1.0F}));
  CHECK(!pinball::inSystemCorner(l, {l.canvasWidth + 1.0F, l.canvasHeight - 1.0F}));
  // No control a player needs is only reachable there: each button keeps
  // most of its area outside the corners on every preset.
  for (const pinball::screen::Preset& preset : pinball::screen::presets()) {
    const Layout p = pinball::screen::computeLayout(preset.surface, preset.idiom);
    for (const pinball::screen::Rect& r :
         {p.pauseButton, p.nudgeLeftButton, p.centreButton, p.nudgeRightButton}) {
      CHECK(!pinball::inSystemCorner(p, {r.x + r.w * 0.5F, r.y + r.h * 0.5F}));
    }
  }
}

void testOverlayTargets(const Layout& layout) {
  using pinball::screen::Rect;
  const auto at = [](const Rect& r) { return centreOf(r); };

  const pinball::screen::MenuLayout menu = pinball::screen::menuLayout(layout);
  const auto menuAt = [&menu](CanvasPoint p) { return pinball::menuTarget(menu, p.x, p.y).kind; };
  CHECK(menuAt(at(menu.resume)) == Kind::MenuResume);
  CHECK(menuAt(at(menu.newGame)) == Kind::MenuNewGame);
  CHECK(menuAt(at(menu.sound)) == Kind::MenuSound);
  CHECK(menuAt(at(menu.music)) == Kind::MenuMusic);
  CHECK(menuAt(at(menu.about)) == Kind::MenuAbout);
  CHECK(menuAt({menu.box.x - 1.0F, menu.box.y + 10.0F}) == Kind::Outside);
  CHECK(menuAt({menu.box.x + 10.0F, menu.box.y + menu.box.h + 1.0F}) == Kind::Outside);
  // Inside the box and on no button: the box's own corner.
  CHECK(menuAt({menu.box.x + 0.5F, menu.box.y + 0.5F}) == Kind::None);

  const pinball::screen::AboutLayout about = pinball::screen::aboutLayout(layout);
  const auto aboutAt = [&about](CanvasPoint p) { return pinball::aboutTarget(about, p.x, p.y); };
  CHECK(aboutAt(at(about.close)).kind == Kind::AboutClose);
  CHECK(aboutAt(at(about.privacyLink)).kind == Kind::AboutPrivacy);
  CHECK(aboutAt(at(about.previous)).kind == Kind::AboutPrevious);
  CHECK(aboutAt(at(about.next)).kind == Kind::AboutNext);
  for (std::size_t i = 0; i < about.tabs.size(); ++i) {
    const OverlayTarget tab = aboutAt(at(about.tabs[i]));
    CHECK(tab.kind == Kind::AboutTab && tab.index == i);
  }
  CHECK(aboutAt({about.box.x - 2.0F, about.box.y - 2.0F}).kind == Kind::Outside);
  CHECK(aboutAt({about.box.x + 0.5F, about.box.y + 0.5F}).kind == Kind::None);

  const pinball::screen::InitialsLayout initials = pinball::screen::initialsLayout(layout);
  const auto initialsAt = [&initials](CanvasPoint p) {
    return pinball::initialsTarget(initials, p.x, p.y);
  };
  for (std::size_t i = 0; i < initials.slot.size(); ++i) {
    const OverlayTarget up = initialsAt(at(initials.up[i]));
    const OverlayTarget slot = initialsAt(at(initials.slot[i]));
    const OverlayTarget down = initialsAt(at(initials.down[i]));
    CHECK(up.kind == Kind::InitialUp && up.index == i);
    CHECK(slot.kind == Kind::InitialSlot && slot.index == i);
    CHECK(down.kind == Kind::InitialDown && down.index == i);
  }
  CHECK(initialsAt(at(initials.ok)).kind == Kind::InitialsOk);
  // Outside the prompt's box is not a way out of it: it must be answered.
  CHECK(initialsAt({initials.box.x - 2.0F, initials.box.y - 2.0F}).kind == Kind::Outside);
  CHECK(initialsAt({initials.box.x + 0.5F, initials.box.y + 0.5F}).kind == Kind::None);
}

void testOverlayTargetsOnDesktop() {
  // The desktop's prompt has no arrows and no OK button: their rectangles
  // are empty and nothing lands on them.
  const Layout layout = pinball::screen::desktopLayout();
  const pinball::screen::InitialsLayout initials = pinball::screen::initialsLayout(layout);
  for (float y = initials.box.y; y < initials.box.y + initials.box.h; y += 7.0F) {
    for (float x = initials.box.x; x < initials.box.x + initials.box.w; x += 7.0F) {
      const Kind kind = pinball::initialsTarget(initials, x, y).kind;
      CHECK(kind == Kind::None || kind == Kind::InitialSlot);
    }
  }
}

void testOverlayTaps() {
  constexpr OverlayTarget kResume{Kind::MenuResume, 0};
  constexpr OverlayTarget kSound{Kind::MenuSound, 0};
  constexpr OverlayTarget kNone{Kind::None, 0};
  constexpr OverlayTarget kOutside{Kind::Outside, 0};
  constexpr OverlayTarget kUp1{Kind::InitialUp, 1};
  constexpr OverlayTarget kUp2{Kind::InitialUp, 2};
  constexpr OverlayTarget kDown0{Kind::InitialDown, 0};

  // A tap: down and up on the same button. Nothing happens until the lift.
  OverlayTaps taps;
  CHECK(taps.handle(TouchPhase::Down, 1, kResume, 1000) == kNone);
  CHECK(taps.pressed() == kResume);
  CHECK(taps.handle(TouchPhase::Move, 1, kResume, 1010) == kNone);
  CHECK(taps.repeat(5000) == kNone);
  CHECK(taps.handle(TouchPhase::Up, 1, kResume, 5020) == kResume);
  CHECK(taps.pressed() == kNone);
  // Only once.
  CHECK(taps.handle(TouchPhase::Up, 1, kResume, 5030) == kNone);

  // Sliding off cancels; sliding back before lifting restores.
  CHECK(taps.handle(TouchPhase::Down, 2, kResume, 0) == kNone);
  CHECK(taps.handle(TouchPhase::Move, 2, kSound, 10) == kNone);
  CHECK(taps.pressed() == kNone);
  CHECK(taps.handle(TouchPhase::Up, 2, kSound, 20) == kNone);
  CHECK(taps.handle(TouchPhase::Down, 2, kResume, 30) == kNone);
  CHECK(taps.handle(TouchPhase::Move, 2, kNone, 40) == kNone);
  CHECK(taps.handle(TouchPhase::Move, 2, kResume, 50) == kNone);
  CHECK(taps.pressed() == kResume);
  CHECK(taps.handle(TouchPhase::Up, 2, kResume, 60) == kResume);

  // A tap outside the box is a tap too; a tap on the box's background is
  // not.
  CHECK(taps.handle(TouchPhase::Down, 3, kOutside, 0) == kNone);
  CHECK(taps.pressed() == kNone);
  CHECK(taps.handle(TouchPhase::Up, 3, kOutside, 10) == kOutside);
  CHECK(taps.handle(TouchPhase::Down, 3, kNone, 20) == kNone);
  CHECK(taps.handle(TouchPhase::Up, 3, kNone, 30) == kNone);
  // From outside onto a button is neither.
  CHECK(taps.handle(TouchPhase::Down, 3, kOutside, 40) == kNone);
  CHECK(taps.handle(TouchPhase::Up, 3, kResume, 50) == kNone);

  // The first finger owns the overlay; a second is ignored until it lifts.
  CHECK(taps.handle(TouchPhase::Down, 4, kResume, 0) == kNone);
  CHECK(taps.handle(TouchPhase::Down, 5, kSound, 10) == kNone);
  CHECK(taps.handle(TouchPhase::Up, 5, kSound, 20) == kNone);
  CHECK(taps.pressed() == kResume);
  CHECK(taps.handle(TouchPhase::Up, 4, kResume, 30) == kResume);
  // A finger that was ignored is not adopted when the owner lifts.
  CHECK(taps.handle(TouchPhase::Down, 4, kResume, 40) == kNone);
  CHECK(taps.handle(TouchPhase::Down, 5, kSound, 50) == kNone);
  CHECK(taps.handle(TouchPhase::Up, 4, kResume, 60) == kResume);
  CHECK(taps.handle(TouchPhase::Up, 5, kSound, 70) == kNone);

  // A cancelled touch, or cancel(), acts on nothing.
  CHECK(taps.handle(TouchPhase::Down, 6, kResume, 0) == kNone);
  CHECK(taps.handle(TouchPhase::Cancel, 6, kResume, 10) == kNone);
  CHECK(taps.handle(TouchPhase::Up, 6, kResume, 20) == kNone);
  CHECK(taps.handle(TouchPhase::Down, 6, kResume, 30) == kNone);
  taps.cancel();
  CHECK(taps.pressed() == kNone);
  CHECK(taps.handle(TouchPhase::Up, 6, kResume, 40) == kNone);

  // A letter arrow acts the moment it is touched, and not again on the lift.
  CHECK(taps.handle(TouchPhase::Down, 7, kUp1, 1000) == kUp1);
  CHECK(taps.pressed() == kUp1);
  // Held, it repeats after 400 ms and then eight times a second.
  CHECK(taps.repeat(1000 + OverlayTaps::kRepeatDelayMs - 1) == kNone);
  CHECK(taps.repeat(1000 + OverlayTaps::kRepeatDelayMs) == kUp1);
  CHECK(taps.repeat(1000 + OverlayTaps::kRepeatDelayMs) == kNone);
  CHECK(taps.repeat(1000 + OverlayTaps::kRepeatDelayMs + OverlayTaps::kRepeatPeriodMs - 1) ==
        kNone);
  CHECK(taps.repeat(1000 + OverlayTaps::kRepeatDelayMs + OverlayTaps::kRepeatPeriodMs) == kUp1);
  // After a long gap (the application stood still) one repeat, not a burst.
  CHECK(taps.repeat(60'000) == kUp1);
  CHECK(taps.repeat(60'000) == kNone);
  CHECK(taps.repeat(60'000 + OverlayTaps::kRepeatPeriodMs - 1) == kNone);
  CHECK(taps.repeat(60'000 + OverlayTaps::kRepeatPeriodMs) == kUp1);
  // Sliding off stops the repeats; sliding back does not act by itself but
  // repeating starts again after the delay.
  CHECK(taps.handle(TouchPhase::Move, 7, kUp2, 61'000) == kNone);
  CHECK(taps.pressed() == kNone);
  CHECK(taps.repeat(70'000) == kNone);
  CHECK(taps.handle(TouchPhase::Move, 7, kUp1, 70'000) == kNone);
  CHECK(taps.repeat(70'000 + OverlayTaps::kRepeatDelayMs - 1) == kNone);
  CHECK(taps.repeat(70'000 + OverlayTaps::kRepeatDelayMs) == kUp1);
  CHECK(taps.handle(TouchPhase::Up, 7, kUp1, 71'000) == kNone);
  CHECK(taps.repeat(90'000) == kNone);

  // The down arrows the same; a second finger cannot work another arrow
  // meanwhile.
  CHECK(taps.handle(TouchPhase::Down, 8, kDown0, 0) == kDown0);
  CHECK(taps.handle(TouchPhase::Down, 9, kUp2, 10) == kNone);
  CHECK(taps.repeat(OverlayTaps::kRepeatDelayMs) == kDown0);
  taps.cancel();
  CHECK(taps.repeat(10'000) == kNone);

  // A clock that runs backwards (it never should) repeats nothing early.
  CHECK(taps.handle(TouchPhase::Down, 10, kUp1, 5000) == kUp1);
  CHECK(taps.repeat(0) == kNone);
  CHECK(taps.repeat(std::numeric_limits<std::uint64_t>::max()) == kUp1);
  CHECK(taps.repeat(std::numeric_limits<std::uint64_t>::max()) == kNone);
  // A press at the end of time does not wrap round into an early repeat.
  taps.cancel();
  CHECK(taps.handle(TouchPhase::Down, 11, kUp1, std::numeric_limits<std::uint64_t>::max() - 5) ==
        kUp1);
  CHECK(taps.repeat(std::numeric_limits<std::uint64_t>::max()) == kNone);
  CHECK(taps.repeat(10) == kNone);
}

}  // namespace

void testTouchControls() {
  testSystemCorners();
  // Every touch preset, so both compositions and all sizes of deck.
  for (const pinball::screen::Preset& preset : pinball::screen::presets()) {
    if (preset.idiom != pinball::screen::Idiom::Touch) {
      continue;
    }
    const Layout layout = pinball::screen::computeLayout(preset.surface, preset.idiom);
    testFlipperZones(layout);
    testCentreButton(layout);
    testNudgeAndPause(layout);
    testCancelling(layout);
    testTooManyFingers(layout);
    testOverlayTargets(layout);
  }
  testMargins();
  testLayoutChange();
  testDesktopHasNoTouchControls();
  testCanvasPoint();
  testOverlayTargetsOnDesktop();
  testOverlayTaps();
}
