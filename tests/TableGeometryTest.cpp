#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <span>

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

}  // namespace

void testTableGeometry() {
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

  {  // The passages. Flipper gap 36; inlanes and outlanes 38 wide; roll call
     // lanes 52; the circuit lanes 52 between wall and rim.
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
