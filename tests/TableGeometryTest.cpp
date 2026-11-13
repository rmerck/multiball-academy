#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <span>
#include <vector>

#include "Check.hpp"
#include "Suites.hpp"
#include "core/TableLayout.hpp"
#include "core/Vec2.hpp"

// Static checks on the table's layout: TABLE.md criteria F1 (sizes) and F2
// (lamps), the passages, the windings of the chains, and that the layout is
// what the header says it is. Nothing here runs the physics.
namespace {

using pinball::Vec2;
namespace table = pinball::table;

float length(Vec2 v) { return std::sqrt(v.x * v.x + v.y * v.y); }
float distance(Vec2 a, Vec2 b) { return length(a - b); }
// Mirror images are worked out in float: equal to within rounding.
bool near(float a, float b) { return std::fabs(a - b) < 1.0e-3F; }
Vec2 midpoint(const table::Capsule& c) { return (c.a + c.b) * 0.5F; }

// Twice the signed area of the triangle a, b, c: its sign says on which
// side of the line from a to b the point c lies.
constexpr float side(Vec2 a, Vec2 b, Vec2 c) noexcept {
  return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

constexpr float signedArea(std::span<const Vec2> loop) noexcept {
  float twice = 0.0F;
  for (std::size_t i = 0; i < loop.size(); ++i) {
    const Vec2 a = loop[i];
    const Vec2 b = loop[(i + 1) % loop.size()];
    twice += a.x * b.y - b.x * a.y;
  }
  return twice * 0.5F;
}

// The whole layout is made at compile time: no table of points is filled in
// by a function of the C library when the game starts. (A compiler that
// would evaluate such a call in a constant expression is not what this
// rests on: tests/CheckNoLibm.cmake reads the header for them as well.)
static_assert(table::kShell.size() == 61 && table::kLaneIsland.size() == 23 &&
              table::kInnerArch.size() == 28 && table::kRimLeftArc.size() == 11);
static_assert(table::kAllWalls.size() == table::kWalls.size() + 20);
static_assert(table::kAllWalls[table::kWalls.size() + 1].a.x ==
              2.0F * table::kFieldCenterX - table::kRimLeftArc[0].x);
static_assert(table::kSkywayRail[8].x == 604.0F && table::kSkywayRail[0].x == 519.0F);
static_assert(table::kLamps.size() == 29);
// The shell and the island are wound opposite ways: the table is inside the
// one and outside the other.
static_assert(signedArea(table::kShell) * signedArea(table::kLaneIsland) < 0.0F);

// Where each major shot's mouth is, for the arrow lamps (TABLE.md section 2).
struct Mouth {
  table::LampId lamp;
  Vec2 centre;
};
constexpr std::array<Mouth, 7> kMouths{{
    {table::LampId::ArrowCircuitLeft, {121.0F, 480.0F}},
    {table::LampId::ArrowRunway, {table::kHoles[table::kRunwayHole].center.x, 425.0F}},
    {table::LampId::ArrowHangar, {279.0F, table::kDoorY}},
    {table::LampId::ArrowBriefing, {356.0F, 428.0F}},
    {table::LampId::ArrowTurbine, {432.0F, table::kSpinner.a.y}},
    {table::LampId::ArrowSkyway, {table::kHoles[table::kSkywayHole].center.x, 425.0F}},
    {table::LampId::ArrowCircuitRight, {590.0F, 480.0F}},
}};

// --- The gap test (TABLE.md 3.2: "every gap not meant as a passage is
// closed, or at least 43") -----------------------------------------------------
// A ball is 24 across. An opening between two solids that is wider than the
// ball but narrower than 43 is where a ball can lodge or two can arch, so
// each one on the table is either a lane, listed below with the reason it is
// there, or a defect. The scan walks the free space on a 2 unit grid. A point
// is in an opening when the nearest solid and the nearest solid on the other
// side of it (its direction more than 120 degrees from the first) are equally
// far away to within the grid; the opening's width there is the sum.
struct Solid {
  Vec2 a;
  Vec2 b;
  float radius;
};
struct Reach {
  float gap = 1.0e9F;
  Vec2 toward{};
};
Reach reach(Vec2 p, const Solid& s) {
  const Vec2 run = s.b - s.a;
  const float runSquared = run.x * run.x + run.y * run.y;
  const float along =
      runSquared > 0.0F
          ? std::clamp(((p.x - s.a.x) * run.x + (p.y - s.a.y) * run.y) / runSquared, 0.0F, 1.0F)
          : 0.0F;
  const Vec2 to = s.a + run * along - p;
  const float far = length(to);
  return {far - s.radius, far > 0.0F ? to * (1.0F / far) : Vec2{}};
}
bool inside(Vec2 p, std::span<const Vec2> loop) {
  bool in = false;
  for (std::size_t i = 0, j = loop.size() - 1; i < loop.size(); j = i++) {
    if ((loop[i].y > p.y) != (loop[j].y > p.y) &&
        p.x < (loop[j].x - loop[i].x) * (p.y - loop[i].y) / (loop[j].y - loop[i].y) + loop[i].x) {
      in = !in;
    }
  }
  return in;
}
std::vector<Solid> solids(bool doorsUp) {
  std::vector<Solid> all;
  const auto chain = [&all](std::span<const Vec2> points, bool closed) {
    for (std::size_t i = 0; i + 1 < points.size(); ++i) {
      all.push_back({points[i], points[i + 1], 0.0F});
    }
    if (closed) {
      all.push_back({points.back(), points.front(), 0.0F});
    }
  };
  chain(table::kShell, true);
  chain(table::kLaneIsland, true);
  // The inner arch without its ghost points.
  chain(std::span<const Vec2>{table::kInnerArch}.subspan(1, table::kInnerArch.size() - 2), false);
  for (const table::Capsule& c : table::kAllWalls) {
    all.push_back({c.a, c.b, c.radius});
  }
  for (const table::Capsule& c : table::kDeadPosts) {
    all.push_back({c.a, c.b, c.radius});
  }
  for (const table::Target& t : table::kTargets) {
    all.push_back({t.shape.a, t.shape.b, t.shape.radius});
  }
  if (doorsUp) {
    for (const table::Capsule& c : table::kDropTargets) {
      all.push_back({c.a, c.b, c.radius});
    }
  }
  for (const table::Bumper& b : table::kBumpers) {
    all.push_back({b.shape.center, b.shape.center, b.shape.radius});
  }
  for (const table::Triangle& sling : table::kSlingshots) {
    for (std::size_t e = 0; e < 3; ++e) {
      all.push_back({sling.points[e], sling.points[(e + 1) % 3], sling.cornerRadius});
    }
  }
  return all;
}

// The openings in the forbidden band that are meant. `narrowest` pins each:
// the scan fails if one of them closes up further than it is today.
struct Opening {
  const char* name;
  const char* reason;
  Vec2 low;
  Vec2 high;
  float narrowest;
};
constexpr Opening mirroredOpening(const Opening& o) {
  const float twice = 2.0F * table::kFieldCenterX;
  return {o.name, o.reason, {twice - o.high.x, o.low.y}, {twice - o.low.x, o.high.y}, o.narrowest};
}
constexpr Opening kOutlane{"outlane",
                           "A lane: 38 wide, the least a lane may be (TABLE.md 3.2).",
                           {40.0F, 570.0F},
                           {80.0F, 760.0F},
                           38.0F};
constexpr Opening kInlane{
    "inlane and its guide",
    "A lane: 38 wide at the slingshot's back corner. Below that it is a guide, 30.2 wide, "
    "between the divider's lower run and the slingshot's underside: one ball wide on purpose, "
    "so that a ball is fed to the flipper along one line and two cannot pass or arch in it.",
    {90.0F, 690.0F},
    {240.0F, 800.0F},
    30.0F};
constexpr Opening kNestSide{
    "nest side exit",
    "A passage, 40.8 wide: between an outer bumper and the rim. A ball that touches the "
    "bumper is kicked, so nothing rests here.",
    {210.0F, 150.0F},
    {260.0F, 200.0F},
    40.5F};
constexpr Opening kTargetNotch{
    "turbo target notch",
    "Not a passage: the corner between a turbo target and the rim it stands out from. It "
    "opens down the table, so a ball that enters it rolls out again.",
    {150.0F, 230.0F},
    {190.0F, 270.0F},
    24.0F};
constexpr std::array kOpenings{
    kOutlane,
    mirroredOpening(kOutlane),
    kInlane,
    mirroredOpening(kInlane),
    kNestSide,
    mirroredOpening(kNestSide),
    kTargetNotch,
    mirroredOpening(kTargetNotch),
    Opening{"launch channel's end",
            "Behind the one-way arch, where it closes on the shell. The arch is not solid "
            "from this side: a ball here passes through it into the field.",
            {540.0F, 100.0F},
            {610.0F, 170.0F},
            24.0F},
    Opening{"Briefing room roof corner",
            "Not a passage: the corner under the Briefing room's sloping roof, inside the "
            "scoop. It opens down the table, onto the scoop's capture box.",
            {325.0F, 325.0F},
            {365.0F, 365.0F},
            24.0F},
    Opening{"under the bay doors",
            "Not a passage: the corner between the raised bay doors and the Briefing room's "
            "left flare. It opens down the table. (Gone when the doors are down.)",
            {270.0F, 395.0F},
            {320.0F, 430.0F},
            24.0F},
};

void checkGaps() {
  constexpr float kBall = 2.0F * table::kBallRadius;
  constexpr float kClear = 43.0F;
  constexpr float kStep = 2.0F;
  std::array<float, kOpenings.size()> narrowest{};
  narrowest.fill(1.0e9F);
  int unlisted = 0;
  for (const bool doorsUp : {true, false}) {
    const std::vector<Solid> all = solids(doorsUp);
    for (float y = 0.0F; y < table::kHeight; y += kStep) {
      for (float x = 0.0F; x < table::kWidth; x += kStep) {
        const Vec2 p{x, y};
        if (!inside(p, table::kShell) || inside(p, table::kLaneIsland) ||
            inside(p, table::kLeftSlingshot.points) || inside(p, table::kRightSlingshot.points)) {
          continue;
        }
        Reach first;
        for (const Solid& s : all) {
          const Reach r = reach(p, s);
          first = r.gap < first.gap ? r : first;
        }
        if (first.gap <= 0.0F || first.gap >= kClear * 0.5F) {
          continue;
        }
        Reach second;
        for (const Solid& s : all) {
          const Reach r = reach(p, s);
          if (r.toward.x * first.toward.x + r.toward.y * first.toward.y < -0.5F &&
              r.gap < second.gap) {
            second = r;
          }
        }
        const float width = first.gap + second.gap;
        if (second.gap - first.gap > kStep + 0.05F || width <= kBall || width >= kClear) {
          continue;
        }
        bool listed = false;
        for (std::size_t i = 0; i < kOpenings.size(); ++i) {
          const Opening& o = kOpenings[i];
          if (x >= o.low.x && x <= o.high.x && y >= o.low.y && y <= o.high.y) {
            narrowest[i] = std::min(narrowest[i], width);
            listed = true;
          }
        }
        if (!listed && unlisted++ < 10) {
          std::fprintf(stderr, "gap %.1f wide at (%.0f, %.0f) is not a listed opening\n",
                       static_cast<double>(width), static_cast<double>(x), static_cast<double>(y));
        }
      }
    }
  }
  CHECK(unlisted == 0);
  for (std::size_t i = 0; i < kOpenings.size(); ++i) {
    // Each listed opening is there (the list has nothing stale in it) and
    // is no narrower than recorded.
    const bool holds = narrowest[i] < kClear && narrowest[i] >= kOpenings[i].narrowest;
    if (!holds) {
      std::fprintf(stderr, "opening '%s' (%s): narrowest %.1f, recorded %.1f\n", kOpenings[i].name,
                   kOpenings[i].reason, static_cast<double>(narrowest[i]),
                   static_cast<double>(kOpenings[i].narrowest));
    }
    CHECK(holds);
  }
}

}  // namespace

void testTableGeometry() {
  checkGaps();

  {  // F1: nothing a player must see or hit is smaller than 20 units, except
     // the stand-up targets, which are at least 10 across and 24 along.
    constexpr float kSmallest = 20.0F;
    for (const table::Bumper& bumper : table::kBumpers) {
      CHECK(2.0F * bumper.shape.radius >= kSmallest);
    }
    for (const table::Rollover& rollover : table::kRollovers) {
      const Vec2 half = table::rolloverHalfSize(rollover);
      CHECK(2.0F * half.x >= kSmallest && 2.0F * half.y >= kSmallest);
      CHECK(2.0F * rollover.zone.radius >= kSmallest);
    }
    for (const std::size_t pocket : {table::kScoopHole, table::kHangarHole}) {
      const Vec2 half = table::kHoles[pocket].halfSize;
      CHECK(2.0F * half.x >= kSmallest && 2.0F * half.y >= kSmallest);
    }
    CHECK(table::kArrowLampLength >= kSmallest && table::kCourseLampDiameter >= kSmallest &&
          table::kStatusLampDiameter >= kSmallest);
    for (const table::Target& target : table::kTargets) {
      CHECK(2.0F * target.shape.radius >= 10.0F);
      CHECK(distance(target.shape.a, target.shape.b) >= 24.0F);
    }
    // The bay doors: three must fit the Hangar's 74-unit mouth, which makes
    // each 23.5 long, half a unit under the 24 asked of a stand-up target.
    // A known miss of the design (TABLE.md 6.8, open question 4): held here
    // to what it is, so that it cannot get worse unnoticed.
    for (const table::Capsule& door : table::kDropTargets) {
      const float along = distance(door.a, door.b) + 2.0F * door.radius;
      CHECK(2.0F * door.radius >= 10.0F);
      CHECK(along >= 23.4F && along < 24.0F);
    }
  }

  {  // F2: every major shot has its arrow lamp within 60 units of its mouth,
     // below it on the line of the shot.
    for (const Mouth& mouth : kMouths) {
      const table::Lamp& lamp = table::kLamps[static_cast<std::size_t>(mouth.lamp)];
      CHECK(lamp.id == mouth.lamp && table::isArrowLamp(lamp.id));
      CHECK(distance(lamp.position, mouth.centre) <= 60.0F);
      CHECK(lamp.position.y > mouth.centre.y);
    }
  }

  {  // The lamps: one per id, in id order, on the visible playfield, and the
     // course lamps far enough apart to be told from each other.
    for (std::size_t i = 0; i < table::kLamps.size(); ++i) {
      const table::Lamp& lamp = table::kLamps[i];
      CHECK(static_cast<std::size_t>(lamp.id) == i);
      CHECK(lamp.position.x > table::kWallLeft && lamp.position.x < table::kLaneWallX);
      CHECK(lamp.position.y > 60.0F && lamp.position.y < table::kHeight);
      for (std::size_t j = i + 1; j < table::kLamps.size(); ++j) {
        const table::Lamp& other = table::kLamps[j];
        if (table::isCourseLamp(lamp.id) && table::isCourseLamp(other.id)) {
          CHECK(distance(lamp.position, other.position) >= 34.0F);
        }
        // No two lamps share a place, except a lane's lamp and its switch.
        CHECK(distance(lamp.position, other.position) >= table::kStatusLampDiameter);
      }
    }
    CHECK(table::isArrowLamp(table::LampId::ArrowCircuitRight) &&
          !table::isArrowLamp(table::LampId::Lock1));
    CHECK(table::isCourseLamp(table::LampId::Graduation) &&
          !table::isCourseLamp(table::LampId::ExtraCredit));
  }

  {  // The field is symmetric about its centre line: flippers, slingshots,
     // bumpers, targets, return lanes, ramps, rails, vanes.
    const float twice = 2.0F * table::kFieldCenterX;
    CHECK(near(table::kLeftFlipper.pivot.x + table::kRightFlipper.pivot.x, twice));
    CHECK(table::kLeftFlipper.pivot.y == table::kRightFlipper.pivot.y);
    for (std::size_t i = 0; i < 3; ++i) {
      CHECK(near(table::kLeftSlingshot.points[i].x + table::kRightSlingshot.points[i].x, twice));
      CHECK(table::kLeftSlingshot.points[i].y == table::kRightSlingshot.points[i].y);
    }
    CHECK(near(table::kBumpers[0].shape.center.x + table::kBumpers[1].shape.center.x, twice));
    CHECK(table::kBumpers[2].shape.center.x == table::kFieldCenterX);
    CHECK(table::kRollovers[1].zone.center.x == table::kFieldCenterX);
    CHECK(near(table::kRollovers[0].zone.center.x + table::kRollovers[2].zone.center.x, twice));
    CHECK(near(table::kRollovers[3].zone.center.x + table::kRollovers[6].zone.center.x, twice));
    CHECK(near(table::kRollovers[4].zone.center.x + table::kRollovers[5].zone.center.x, twice));
    for (std::size_t i = 0; i < table::kRunwayRail.size(); ++i) {
      CHECK(near(table::kRunwayRail[i].x + table::kSkywayRail[i].x, twice));
      CHECK(table::kRunwayRail[i].y == table::kSkywayRail[i].y);
    }
    for (std::size_t i = 0; i < table::kSlingVaneLeft.size(); ++i) {
      const Vec2 mirror = table::kSlingVaneRight[table::kSlingVaneRight.size() - 1 - i];
      CHECK(near(table::kSlingVaneLeft[i].x + mirror.x, twice) &&
            table::kSlingVaneLeft[i].y == mirror.y);
    }
    // The walls that come in mirrored pairs: the two runs of each divider,
    // the rims' straights, the lane posts and every part of the rim arcs.
    // (The Hangar and the Briefing room in the middle are not symmetric.)
    int mirrored = 0;
    for (const table::Capsule& wall : table::kAllWalls) {
      for (const table::Capsule& other : table::kAllWalls) {
        mirrored += near(wall.a.x + other.a.x, twice) && wall.a.y == other.a.y &&
                            near(wall.b.x + other.b.x, twice) && wall.b.y == other.b.y &&
                            wall.a.x < table::kFieldCenterX
                        ? 1
                        : 0;
      }
    }
    CHECK(mirrored == 2 + 1 + 1 + static_cast<int>(table::kRimArcParts));
  }

  {  // The passages. Flipper gap 36; outlanes 38 wide; inlanes 38 wide at the
     // slingshot's back corner (the guide under the slingshot is 30.2: see
     // kInlane above); roll call lanes 52; the circuit lanes 52 between wall
     // and rim.
    const float tipReach = table::kFlipperLength * 0.8660254F;
    const float gap = (table::kRightFlipper.pivot.x - tipReach) -
                      (table::kLeftFlipper.pivot.x + tipReach) - 2.0F * table::kFlipperTipRadius;
    CHECK(gap > 35.5F && gap < 37.0F);
    const float dividerOuter = table::kDividerX - table::kWallRadius;
    const float dividerInner = table::kDividerX + table::kWallRadius;
    CHECK(dividerOuter - table::kWallLeft == 38.0F);  // Outlane.
    // The inlane, at the slingshot's back corner where it is narrowest.
    CHECK(table::kLeftSlingshot.points[1].x - table::kLeftSlingshot.cornerRadius - dividerInner ==
          38.0F);
    // The divider's top is above the slingshot's top: no hopping out.
    CHECK(table::kDividerTopY < table::kSlingTopY);
    const float postLeft = table::kLanePostLeft.a.x;
    const float postRight = table::kLanePostRight.a.x;
    CHECK(postRight - postLeft - 2.0F * table::kWallRadius >= 52.0F);
    CHECK(postLeft - table::kRimLeftArc.back().x - 2.0F * table::kWallRadius >= 52.0F);
    constexpr float kLeftCircuitWall = 88.0F;
    CHECK(table::kRimLeftStraight.a.x - table::kWallRadius - kLeftCircuitWall == 52.0F);
    // Every lane and passage is wider than the ball with room to spare.
    CHECK(38.0F > 2.0F * table::kBallRadius + 12.0F);
    // The outlane's switch reaches across the outlane, the inlane's across
    // the inlane, less the ball's edge.
    CHECK(table::kOutlaneReach == 38.0F * 0.5F);
    CHECK(table::kLeftOutlane.x == (table::kWallLeft + dividerOuter) * 0.5F);
  }

  {  // Bumpers: every gap between two of them, and from each to the roofs
     // and the lane posts, is wider than the ball with room to spare.
    const float ball = 2.0F * table::kBallRadius;
    for (std::size_t i = 0; i < table::kBumpers.size(); ++i) {
      const table::Circle& a = table::kBumpers[i].shape;
      for (std::size_t j = i + 1; j < table::kBumpers.size(); ++j) {
        const table::Circle& b = table::kBumpers[j].shape;
        CHECK(distance(a.center, b.center) - a.radius - b.radius > ball + 8.0F);
      }
      for (const Vec2 peak : {table::kHangarRoofLeft.b, table::kScoopRoofLeft.a,
                              table::kLanePostLeft.b, table::kLanePostRight.b}) {
        CHECK(distance(a.center, peak) - a.radius - table::kWallRadius > ball + 8.0F);
      }
    }
  }

  {  // The chains are wound so that the ball's side is the solid one. The
     // inner arch runs the same way round as the shell's arch above it, so
     // both face the field; each vane faces its circuit lane.
    const Vec2 field = table::kArchCenter;
    constexpr std::size_t kShellArchFirst = 18;
    constexpr std::size_t kShellArchLast = 58;
    const float shellTurn =
        side(table::kShell[kShellArchFirst], table::kShell[kShellArchFirst + 1], field);
    for (std::size_t i = kShellArchFirst; i < kShellArchLast; ++i) {
      CHECK(side(table::kShell[i], table::kShell[i + 1], field) * shellTurn > 0.0F);
    }
    // The ghosts aside: points 1 to size - 2.
    for (std::size_t i = 1; i + 2 < table::kInnerArch.size(); ++i) {
      CHECK(side(table::kInnerArch[i], table::kInnerArch[i + 1], field) * shellTurn > 0.0F);
    }
    const Vec2 leftLane{110.0F, 630.0F};
    CHECK(side(table::kSlingVaneLeft[1], table::kSlingVaneLeft[2], leftLane) * shellTurn > 0.0F);
    CHECK(side(table::kSlingVaneRight[1], table::kSlingVaneRight[2], table::mirrored(leftLane)) *
              shellTurn >
          0.0F);
    // The shell's right wall, going up, has the field on that same side.
    CHECK(side(table::kShell[1], table::kShell[2], field) * shellTurn > 0.0F);
    // The inner arch starts on the right circuit wall and ends short of
    // the top, under the shell.
    CHECK(table::kInnerArch[1].x == 623.0F && table::kInnerArch[2].x == 623.0F);
    CHECK(table::kInnerArch[table::kInnerArch.size() - 2].y >
          table::kArchCenter.y - table::kArchRadiusY);
    // And lies inside the shell's arch all the way: the launch channel is
    // between the two.
    for (std::size_t i = 3; i + 1 < table::kInnerArch.size(); ++i) {
      const Vec2 p = table::kInnerArch[i];
      const float ex = (p.x - field.x) / table::kArchRadiusX;
      const float ey = (p.y - field.y) / table::kArchRadiusY;
      CHECK(ex * ex + ey * ey < 1.01F);
    }
  }

  {  // Where a ball is in play, and where it is not yet.
    CHECK(!table::inPlayArea(table::kBallStart));
    CHECK(!table::inPlayArea({table::kLaneCenterX, 400.0F}));
    CHECK(!table::inPlayArea({600.0F, 150.0F}));  // Riding the launch curve.
    CHECK(table::inPlayArea({300.0F, 100.0F}));   // Past the top of the arch.
    CHECK(table::inPlayArea({600.0F, 400.0F}));   // The right circuit lane.
    CHECK(table::inPlayArea({table::kFieldCenterX, 600.0F}));
    // The plunger's ball rests in the middle of the lane.
    CHECK(table::kBallStart.x > table::kLaneWallX + table::kWallRadius + table::kBallRadius);
    CHECK(table::kBallStart.x < table::kWallRight - table::kBallRadius);
  }

  {  // The dead posts are the two rim feet, the Turbine rail and the two
     // flares of the Briefing room, and none of them is also a plain wall.
    CHECK(table::kDeadPosts.size() == 5);
    for (const table::Capsule& post : table::kDeadPosts) {
      for (const table::Capsule& wall : table::kAllWalls) {
        CHECK(distance(midpoint(post), midpoint(wall)) > 1.0F);
      }
    }
    CHECK(table::kDeadPostRestitution < 0.2F);
  }

  {  // Element references.
    using table::ElementKind;
    CHECK(table::elementCount(ElementKind::Bumper) == 3);
    CHECK(table::elementCount(ElementKind::Rollover) == 7);
    CHECK(table::elementCount(ElementKind::Hole) == 4);
    CHECK(table::elementCount(ElementKind::DropTarget) == 3);
    CHECK(table::elementCount(ElementKind::None) == 0);
    CHECK(table::isRamp(table::HoleType::RampLeft) && table::isRamp(table::HoleType::RampRight));
    CHECK(!table::isRamp(table::HoleType::Scoop) && !table::isRamp(table::HoleType::Hangar));
  }
}
