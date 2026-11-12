// Development tool: prints the table geometry as JSON on standard output.
// The GIMP art scripts read this, so the art is drawn from exactly the same
// geometry the physics uses. Takes no input.
#include <cstdio>
#include <vector>

#include "core/TableLayout.hpp"

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

const char* name(table::BumperGroup g) {
  switch (g) {
    case table::BumperGroup::Attack:
      return "attack";
    case table::BumperGroup::Engine:
      return "engine";
    case table::BumperGroup::Satellite:
      return "satellite";
  }
  return "unknown";
}

const char* name(table::TargetBank b) {
  switch (b) {
    case table::TargetBank::Booster:
      return "booster";
    case table::TargetBank::Medal:
      return "medal";
    case table::TargetBank::Mission:
      return "mission";
    case table::TargetBank::Multiplier:
      return "multiplier";
  }
  return "unknown";
}

const char* name(table::RolloverGroup g) {
  switch (g) {
    case table::RolloverGroup::ReEntry:
      return "reentry";
    case table::RolloverGroup::Outlane:
      return "outlane";
    case table::RolloverGroup::Inlane:
      return "inlane";
  }
  return "unknown";
}

const char* name(table::HoleType t) {
  switch (t) {
    case table::HoleType::Wormhole:
      return "wormhole";
    case table::HoleType::BlackHole:
      return "blackhole";
    case table::HoleType::Hyperspace:
      return "hyperspace";
    case table::HoleType::Ramp:
      return "ramp";
  }
  return "unknown";
}

template <typename Container, typename Fn>
void list(const char* key, const Container& items, Fn&& each, bool last = false) {
  std::printf("  \"%s\": [\n", key);
  std::size_t i = 0;
  for (const auto& item : items) {
    std::printf("    ");
    each(item);
    std::printf(++i < items.size() ? ",\n" : "\n");
  }
  std::printf(last ? "  ]\n" : "  ],\n");
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
  std::printf("  \"hole\": {\"radius\": %.1f},\n", static_cast<double>(table::kHoleRadius));
  std::printf("  \"fuelLights\": {\"radius\": %.1f, \"centers\": [",
              static_cast<double>(table::kFuelLightRadius));
  for (std::size_t i = 0; i < table::kFuelLightCount; ++i) {
    point(table::fuelLight(i));
    std::printf(i + 1 < table::kFuelLightCount ? ", " : "");
  }
  std::printf("]},\n");
  std::printf("  \"rampExit\": ");
  point(table::kRampExit);
  std::printf(",\n  \"laneGate\": ");
  capsule(table::kLaneGate);
  std::printf(",\n  \"medalIsland\": ");
  triangle(table::kMedalIsland);
  std::printf(",\n");

  const std::vector<Vec2> shell = table::outerBoundary();
  list("shell", shell, [](Vec2 p) { point(p); });
  list("walls", table::kWalls, [](const table::Capsule& c) { capsule(c); });
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
    std::printf(", \"r\": %.1f, \"group\": \"%s\"}", static_cast<double>(b.shape.radius),
                name(b.group));
  });
  list("targets", table::kTargets, [](const table::Target& t) {
    std::printf("{\"shape\": ");
    capsule(t.shape);
    std::printf(", \"bank\": \"%s\"}", name(t.bank));
  });
  list("rollovers", table::kRollovers, [](const table::Rollover& r) {
    std::printf("{\"c\": ");
    point(r.zone.center);
    std::printf(", \"r\": %.1f, \"group\": \"%s\"}", static_cast<double>(r.zone.radius),
                name(r.group));
  });
  list(
      "holes", table::kHoles,
      [](const table::Hole& h) {
        std::printf("{\"c\": ");
        point(h.center);
        std::printf(", \"type\": \"%s\"}", name(h.type));
      },
      true);
  std::printf("}\n");
  return 0;
}
