// Development tool: prints the table geometry as JSON on standard output.
// The GIMP art scripts read this, so the art is drawn from exactly the same
// geometry the physics uses. Under "screen" it also prints where everything
// goes on the screen: the game's name for the title art, the windows of the
// strip and the rectangles of the deck, the panel's title and control
// areas, and every preset's layout with its overlays. Takes no input.
#include <concepts>
#include <cstddef>
#include <cstdio>
#include <ranges>
#include <span>

#include "core/OverlayLayout.hpp"
#include "core/PanelLayout.hpp"
#include "core/ScreenLayout.hpp"
#include "core/TableLayout.hpp"
#include "core/Version.hpp"

namespace {

using namespace pinball;  // NOLINT: small tool.

void point(Vec2 p) { std::printf("[%.3f, %.3f]", static_cast<double>(p.x), static_cast<double>(p.y)); }

void capsule(const table::Capsule& c) {
  std::printf("{\"a\": ");
  point(c.a);
  std::printf(", \"b\": ");
  point(c.b);
  std::printf(", \"r\": %.3f}", static_cast<double>(c.radius));
}

void triangle(const table::Triangle& t) {
  std::printf("[");
  for (std::size_t i = 0; i < 3; ++i) {
    point(t.points[i]);
    std::printf(i < 2 ? ", " : "");
  }
  std::printf("]");
}

const char* name(table::TargetId id) {
  switch (id) {
    using enum table::TargetId;
    case NestLeft:
      return "nestLeft";
    case NestRight:
      return "nestRight";
  }
  return "unknown";
}

const char* name(table::RolloverGroup g) {
  switch (g) {
    using enum table::RolloverGroup;
    case TopLane:
      return "rollCall";
    case Outlane:
      return "outlane";
    case Inlane:
      return "inlane";
  }
  return "unknown";
}

const char* name(table::HoleType t) {
  switch (t) {
    using enum table::HoleType;
    case Scoop:
      return "scoop";
    case Hangar:
      return "hangar";
    case RampLeft:
      return "rampLeft";
    case RampRight:
      return "rampRight";
  }
  return "unknown";
}

template <std::ranges::sized_range Container,
          std::invocable<std::ranges::range_reference_t<const Container>> Fn>
void list(const char* key, const Container& items, Fn&& each, bool last = false) {
  std::printf("  \"%s\": [\n", key);
  std::size_t i = 0;
  for (const auto& item : items) {
    std::printf("    ");
    each(item);
    std::printf(++i < std::ranges::size(items) ? ",\n" : "\n");
  }
  std::printf(last ? "  ]\n" : "  ],\n");
}

void rect(const screen::Rect& r) {
  std::printf("[%.3f, %.3f, %.3f, %.3f]", static_cast<double>(r.x), static_cast<double>(r.y),
              static_cast<double>(r.w), static_cast<double>(r.h));
}

// "key": [x, y, w, h], with a comma unless it is the last of its object.
void member(const char* key, const screen::Rect& r, bool last = false) {
  std::printf("\"%s\": ", key);
  rect(r);
  std::printf(last ? "" : ", ");
}

void rects(const char* key, std::span<const screen::Rect> items) {
  std::printf("\"%s\": [", key);
  for (std::size_t i = 0; i < items.size(); ++i) {
    rect(items[i]);
    std::printf(i + 1 < items.size() ? ", " : "");
  }
  std::printf("], ");
}

// The game's name is letters, digits and spaces (the build checks it), but
// nothing is printed into a JSON string unchecked.
void jsonString(const char* text) {
  std::putchar('"');
  for (const char* c = text; *c != '\0'; ++c) {
    if (*c == '"' || *c == '\\') {
      std::putchar('\\');
    }
    std::putchar(static_cast<unsigned char>(*c) < 0x20 ? ' ' : *c);
  }
  std::putchar('"');
}

void presetLayout(const screen::Preset& preset) {
  const screen::Layout l = screen::computeLayout(preset.surface, preset.idiom);
  const screen::Surface& s = preset.surface;
  std::printf("{\"name\": \"%.*s\", \"idiom\": \"%s\", \"composition\": \"%s\",\n      ",
              static_cast<int>(preset.name.size()), preset.name.data(),
              l.idiom == screen::Idiom::Touch ? "touch" : "desktop",
              l.composition == screen::Composition::Stack ? "stack" : "classic");
  std::printf("\"surface\": {\"width\": %.1f, \"height\": %.1f, \"insets\": [%.1f, %.1f, %.1f, "
              "%.1f]},\n      ",
              static_cast<double>(s.width), static_cast<double>(s.height),
              static_cast<double>(s.safeTop), static_cast<double>(s.safeBottom),
              static_cast<double>(s.safeLeft), static_cast<double>(s.safeRight));
  std::printf("\"canvas\": [%.3f, %.3f], \"pointsPerUnit\": %.6f,\n      ",
              static_cast<double>(l.canvasWidth), static_cast<double>(l.canvasHeight),
              static_cast<double>(l.pointsPerUnit));
  member("safe", l.safe);
  member("playfield", l.playfield);
  member("hud", l.hud);
  member("deck", l.deck);
  member("dialog", l.dialog);
  std::printf("\n      ");
  member("pauseButton", l.pauseButton);
  member("nudgeLeftButton", l.nudgeLeftButton);
  member("centreButton", l.centreButton);
  member("nudgeRightButton", l.nudgeRightButton);
  std::printf("\n      ");
  member("flipperLeftZone", l.flipperLeftZone);
  member("flipperRightZone", l.flipperRightZone);
  const screen::MenuLayout menu = screen::menuLayout(l);
  std::printf("\n      \"menu\": {");
  member("box", menu.box);
  member("resume", menu.resume);
  member("newGame", menu.newGame);
  member("sound", menu.sound);
  member("music", menu.music);
  member("about", menu.about, true);
  const screen::AboutLayout about = screen::aboutLayout(l);
  std::printf("},\n      \"about\": {");
  member("box", about.box);
  member("close", about.close);
  rects("tabs", about.tabs);
  member("body", about.body);
  member("privacyLink", about.privacyLink);
  member("previous", about.previous);
  member("next", about.next, true);
  const screen::InitialsLayout initials = screen::initialsLayout(l);
  std::printf("},\n      \"initials\": {");
  member("box", initials.box);
  rects("up", initials.up);
  rects("slot", initials.slot);
  rects("down", initials.down);
  member("ok", initials.ok, true);
  std::printf("}}");
}

void screenLayouts() {
  std::printf("  \"screen\": {\n    \"appName\": ");
  jsonString(kGameTitle);
  std::printf(",\n    \"playfield\": [%.1f, %.1f], \"classic\": [%.1f, %.1f],\n",
              static_cast<double>(screen::kPlayfieldWidth),
              static_cast<double>(screen::kPlayfieldHeight),
              static_cast<double>(screen::kClassicWidth),
              static_cast<double>(screen::kClassicHeight));
  std::printf("    \"stack\": {\"width\": %.1f, \"stripHeight\": %.1f, \"deckMinHeight\": %.1f, "
              "\"deckMaxHeight\": %.1f},\n",
              static_cast<double>(screen::kStackWidth), static_cast<double>(screen::kStripHeight),
              static_cast<double>(screen::kDeckMinHeight),
              static_cast<double>(screen::kDeckMaxHeight));
  std::printf("    \"backdropRgb\": [%d, %d, %d],\n", screen::kBackdropRgb[0],
              screen::kBackdropRgb[1], screen::kBackdropRgb[2]);
  std::printf("    \"minHitUnits\": %.1f, \"minHitPoints\": %.1f, \"minScene\": [%.1f, %.1f],\n",
              static_cast<double>(screen::kMinHitUnits),
              static_cast<double>(screen::kMinHitPoints),
              static_cast<double>(screen::kMinSceneWidth),
              static_cast<double>(screen::kMinSceneHeight));
  // Relative to the strip's origin.
  std::printf("    \"strip\": {");
  member("pauseButton", strip::kPauseButton);
  member("pauseHit", strip::kPauseHit);
  member("scoreWell", strip::kScoreWell);
  member("statusWell", strip::kStatusWell);
  member("rankWell", strip::kRankWell);
  member("missionWell", strip::kMissionWell);
  member("messageWell", strip::kMessageWell, true);
  // Relative to the deck's origin.
  std::printf("},\n    \"deck\": {");
  member("nudgeLeftButton", deck::kNudgeLeftButton);
  member("centreButton", deck::kCentreButton);
  member("nudgeRightButton", deck::kNudgeRightButton);
  member("leftCorner", deck::kLeftCorner);
  member("rightCorner", deck::kRightCorner);
  std::printf("\"padTop\": %.1f", static_cast<double>(deck::kPadTop));
  // In the units of the Classic composition, like the panel's wells.
  std::printf("},\n    \"panelAreas\": {");
  member("title", panel::kTitleArea);
  member("touchTitle", panel::kTouchTitleArea);
  member("controls", panel::kControlsArea);
  member("nudgeLeftButton", panel::kNudgeLeftButton);
  member("centreButton", panel::kCentreButton);
  member("nudgeRightButton", panel::kNudgeRightButton);
  member("pauseButton", panel::kPauseButton);
  member("nudgeLeftHit", panel::kNudgeLeftHit);
  member("centreHit", panel::kCentreHit);
  member("nudgeRightHit", panel::kNudgeRightHit);
  member("pauseHit", panel::kPauseHit);
  member("flipperCorner", panel::kFlipperCorner, true);
  std::printf("},\n    \"presets\": [\n");
  const std::span<const screen::Preset> presets = screen::presets();
  for (std::size_t i = 0; i < presets.size(); ++i) {
    std::printf("     ");
    presetLayout(presets[i]);
    std::printf(i + 1 < presets.size() ? ",\n" : "\n");
  }
  std::printf("    ]\n  },\n");
}

}  // namespace

int main() {
  std::printf("{\n");
  std::printf("  \"width\": %.1f,\n  \"height\": %.1f,\n", static_cast<double>(table::kWidth),
              static_cast<double>(table::kHeight));
  std::printf("  \"ballRadius\": %.1f,\n", static_cast<double>(table::kBallRadius));
  std::printf("  \"fieldCenterX\": %.3f,\n", static_cast<double>(table::kFieldCenterX));
  std::printf("  \"lane\": {\"wallX\": %.1f, \"topY\": %.1f, \"centerX\": %.3f, \"right\": %.1f},\n",
              static_cast<double>(table::kLaneWallX), static_cast<double>(table::kLaneWallTopY),
              static_cast<double>(table::kLaneCenterX), static_cast<double>(table::kWallRight));
  std::printf("  \"plunger\": {\"width\": %.1f, \"height\": %.1f, \"restTopY\": %.1f, "
              "\"travel\": %.1f},\n",
              static_cast<double>(table::kPlungerWidth), static_cast<double>(table::kPlungerHeight),
              static_cast<double>(table::kPlungerRestTopY),
              static_cast<double>(table::kPlungerTravel));
  std::printf("  \"flipper\": {\"length\": %.1f, \"baseRadius\": %.1f, \"tipRadius\": %.1f},\n",
              static_cast<double>(table::kFlipperLength),
              static_cast<double>(table::kFlipperBaseRadius),
              static_cast<double>(table::kFlipperTipRadius));
  std::printf("  \"panel\": {\"left\": %.1f, \"width\": %.1f, \"height\": %.1f, \"wells\": [",
              static_cast<double>(panel::kLeft), static_cast<double>(panel::kWidth),
              static_cast<double>(panel::kHeight));
  {
    bool first = true;
    for (const panel::Rect& w : panel::kWells) {
      std::printf("%s[%.1f, %.1f, %.1f, %.1f]", first ? "" : ", ", static_cast<double>(w.x),
                  static_cast<double>(w.y), static_cast<double>(w.w), static_cast<double>(w.h));
      first = false;
    }
  }
  std::printf("]},\n");
  screenLayouts();
  // Where a ball has left the launch channel, for anyone drawing the lane.
  std::printf("  \"arch\": {\"center\": ");
  point(table::kArchCenter);
  std::printf(", \"radiusX\": %.1f, \"radiusY\": %.1f},\n",
              static_cast<double>(table::kArchRadiusX), static_cast<double>(table::kArchRadiusY));
  std::printf("  \"ramp\": {\"minUpwardSpeed\": %.1f, \"rideSeconds\": %.2f},\n",
              static_cast<double>(table::kRampMinUpwardSpeed), table::kRampRideSeconds);
  std::printf("  \"spinner\": {\"a\": ");
  point(table::kSpinner.a);
  std::printf(", \"b\": ");
  point(table::kSpinner.b);
  std::printf("},\n  \"kickback\": {\"c\": ");
  point(table::kKickbackZone.center);
  std::printf(", \"r\": %.1f},\n", static_cast<double>(table::kKickbackZone.radius));
  std::printf("  \"lampSizes\": {\"arrowLength\": %.1f, \"course\": %.1f, \"status\": %.1f},\n",
              static_cast<double>(table::kArrowLampLength),
              static_cast<double>(table::kCourseLampDiameter),
              static_cast<double>(table::kStatusLampDiameter));

  // The three chains. "innerArch" and the vanes are one-sided and open: the
  // first and last points of each are ghosts, not part of the wall.
  list("shell", table::kShell, [](Vec2 p) { point(p); });
  list("laneIsland", table::kLaneIsland, [](Vec2 p) { point(p); });
  list("innerArch", table::kInnerArch, [](Vec2 p) { point(p); });
  list("slingVaneLeft", table::kSlingVaneLeft, [](Vec2 p) { point(p); });
  list("slingVaneRight", table::kSlingVaneRight, [](Vec2 p) { point(p); });
  list("walls", table::kAllWalls, [](const table::Capsule& c) { capsule(c); });
  list("deadPosts", table::kDeadPosts, [](const table::Capsule& c) { capsule(c); });
  list("slingshots", table::kSlingshots, [](const table::Triangle& t) { triangle(t); });
  list("flippers", table::kFlippers, [](const table::Flipper& f) {
    std::printf("{\"pivot\": ");
    point(f.pivot);
    std::printf(", \"rest\": %.5f, \"active\": %.5f}", static_cast<double>(f.restAngle),
                static_cast<double>(f.activeAngle));
  });
  list("bumpers", table::kBumpers, [](const table::Bumper& b) {
    std::printf("{\"c\": ");
    point(b.shape.center);
    std::printf(", \"r\": %.1f}", static_cast<double>(b.shape.radius));
  });
  list("targets", table::kTargets, [](const table::Target& t) {
    std::printf("{\"shape\": ");
    capsule(t.shape);
    std::printf(", \"id\": \"%s\"}", name(t.id));
  });
  list("bayDoors", table::kDropTargets, [](const table::Capsule& c) { capsule(c); });
  list("lockBays", table::kLockBays, [](Vec2 p) { point(p); });
  list("rollovers", table::kRollovers, [](const table::Rollover& r) {
    const Vec2 half = table::rolloverHalfSize(r);
    std::printf("{\"c\": ");
    point(r.zone.center);
    std::printf(", \"r\": %.1f, \"halfSize\": ", static_cast<double>(r.zone.radius));
    point(half);
    std::printf(", \"group\": \"%s\"}", name(r.group));
  });
  list("holes", table::kHoles, [](const table::Hole& h) {
    std::printf("{\"c\": ");
    point(h.center);
    std::printf(", \"halfSize\": ");
    point(h.halfSize);
    std::printf(", \"type\": \"%s\", \"exit\": ", name(h.type));
    point(h.exitPosition);
    std::printf(", \"exitVelocity\": ");
    point(h.exitVelocity);
    std::printf("}");
  });
  // Sensor lines, in the order of table::SensorId: the two circuit mouths,
  // the top of the arch, the spinner. The kickback's path starts in its zone.
  list("sensors", table::kSensors, [](const table::SensorLine& line) {
    std::printf("{\"a\": ");
    point(line.a);
    std::printf(", \"b\": ");
    point(line.b);
    std::printf("}");
  });
  list("kickbackPath", table::kKickbackPath, [](Vec2 p) { point(p); });
  list("runwayRail", table::kRunwayRail, [](Vec2 p) { point(p); });
  list("skywayRail", table::kSkywayRail, [](Vec2 p) { point(p); });
  // Lamps in LampId order; "kind" is arrow, course or status.
  list(
      "lamps", table::kLamps,
      [](const table::Lamp& lamp) {
        std::printf("{\"id\": %d, \"kind\": \"%s\", \"c\": ", static_cast<int>(lamp.id),
                    table::isArrowLamp(lamp.id)
                        ? "arrow"
                        : (table::isCourseLamp(lamp.id) ? "course" : "status"));
        point(lamp.position);
        std::printf("}");
      },
      true);
  std::printf("}\n");
  return 0;
}
