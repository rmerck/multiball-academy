#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <numbers>

#include "core/Vec2.hpp"

// Geometry of the table in playfield pixels (768 x 960, x right, y down).
// This is the single source of truth for physics, drawing and art.
//
// Nothing here is computed with a library function: every curve is a table
// of points written out, so every platform has exactly the same outline and
// the ball takes exactly the same path. Do not include <cmath> in this file.
namespace pinball::table {

inline constexpr float kWidth = 768.0F;
inline constexpr float kHeight = 960.0F;

inline constexpr float kBallRadius = 12.0F;
// Most balls on the table at once. The physics world keeps this many ball
// slots; all but one are idle outside a multiball.
inline constexpr std::size_t kMaxBalls = 3;
inline constexpr float kPi = std::numbers::pi_v<float>;

// The shell continues below the visible table; a ball past kDrainY is lost.
inline constexpr float kFloorY = 1010.0F;
inline constexpr float kDrainY = kHeight + kBallRadius;
// Half thickness of interior walls and guides.
inline constexpr float kWallRadius = 5.0F;

// The field between the two circuit walls (x 88 and x 623) is symmetric
// about this x.
inline constexpr float kFieldCenterX = 355.5F;
constexpr Vec2 mirrored(Vec2 p) noexcept { return {2.0F * kFieldCenterX - p.x, p.y}; }

// A thick line with round ends. Used for interior walls and guides.
struct Capsule {
  Vec2 a;
  Vec2 b;
  float radius;
};
constexpr Capsule mirrored(const Capsule& c) noexcept {
  return {mirrored(c.a), mirrored(c.b), c.radius};
}
struct Triangle {
  std::array<Vec2, 3> points;
  float cornerRadius;
};
struct Circle {
  Vec2 center;
  float radius;
};

// --- Outer shell -------------------------------------------------------------
// The extremes of the shell: the left outlane wall and the launch lane's
// outer wall. A ball beyond them has left the table.
inline constexpr float kWallLeft = 40.0F;
inline constexpr float kWallRight = 728.0F;
// The arch the ball rides between the two circuit lanes is the ellipse about
// this point with radii 267.5 by 235 (top at y 65). For reference and for
// the art: the points are in kShell.
inline constexpr Vec2 kArchCenter{kFieldCenterX, 300.0F};
inline constexpr float kArchRadiusX = 267.5F;
inline constexpr float kArchRadiusY = 235.0F;

// How the point tables were made (once, by script, rounded to 0.01; the
// tables are now the definition, and regenerating one is a geometry change
// that moves the recorded trajectory in tests/HoleTest.cpp):
//   kShell 3..18    cubic Bezier from (728, 300), control points (728, 130)
//                   and (607.36, 143.88), to (468.55, 87.02); 16 segments.
//   kShell 18..58   the arch ellipse from 65 to 180 degrees; 40 segments.
//   kInnerArch      (623, 536), (623, 300), the arch ellipse from 0 to 67
//                   degrees in 24 segments; ghosts (671, 584) and the point
//                   at 70 degrees.
//   kLaneIsland     the ellipse with radii 269.5 by 237 from 0 to 28 degrees
//                   (8 segments), back along radii 279.5 by 247, the lane wall.
//   kRimLeftArc     the ellipse with radii 210.5 by 175 from 180 to 116.4
//                   degrees; 10 segments.

// clang-format off: the tables stay as they were generated, four to a line.
// Outer shell, a closed chain wound so that the table is on its solid side:
// floor, launch lane outer wall, the launch curve (leaving the lane straight
// up and meeting the arch tangentially at 65 degrees), the arch over to the
// left circuit wall (x 88), down to y 556, the step out to the left outlane
// pocket and its wall.
inline constexpr std::array<Vec2, 61> kShell{{
    {40.00F, 1010.00F}, {728.00F, 1010.00F}, {728.00F, 300.00F}, {726.61F, 270.22F},
    {722.54F, 244.37F}, {715.95F, 222.09F}, {706.98F, 203.00F}, {695.78F, 186.73F},
    {682.51F, 172.90F}, {667.31F, 161.14F}, {650.33F, 151.08F}, {631.72F, 142.35F},
    {611.64F, 134.57F}, {590.23F, 127.37F}, {567.65F, 120.38F}, {544.04F, 113.22F},
    {519.55F, 105.52F}, {494.34F, 96.91F}, {468.55F, 87.02F}, {456.25F, 82.30F},
    {443.69F, 78.14F}, {430.91F, 74.53F}, {417.95F, 71.49F}, {404.82F, 69.03F}, {391.57F, 67.15F},
    {378.23F, 65.85F}, {364.84F, 65.14F}, {351.41F, 65.03F}, {338.00F, 65.50F}, {324.64F, 66.57F},
    {311.35F, 68.22F}, {298.17F, 70.46F}, {285.14F, 73.27F}, {272.28F, 76.66F}, {259.64F, 80.61F},
    {247.23F, 85.11F}, {235.10F, 90.15F}, {223.27F, 95.72F}, {211.77F, 101.80F},
    {200.64F, 108.39F}, {189.89F, 115.45F}, {179.56F, 122.98F}, {169.68F, 130.96F},
    {160.26F, 139.36F}, {151.34F, 148.16F}, {142.92F, 157.35F}, {135.05F, 166.89F},
    {127.72F, 176.78F}, {120.98F, 186.97F}, {114.82F, 197.44F}, {109.26F, 208.18F},
    {104.33F, 219.14F}, {100.03F, 230.31F}, {96.38F, 241.66F}, {93.37F, 253.15F},
    {91.03F, 264.76F}, {89.35F, 276.46F}, {88.34F, 288.21F}, {88.00F, 300.00F}, {88.00F, 556.00F},
    {40.00F, 600.00F},
}};

// The lane island, a closed chain, solid inside: the right circuit wall and
// the foot of the arch (set 2 units behind the inner arch), back down the
// arch's outer face, the launch lane wall (faces x 681 and 671) and the right
// outlane pocket's roof. The roof starts 20 units higher than the left one.
inline constexpr std::array<Vec2, 23> kLaneIsland{{
    {625.00F, 536.00F}, {625.00F, 300.00F}, {624.50F, 285.53F}, {622.99F, 271.12F},
    {620.49F, 256.81F}, {616.99F, 242.66F}, {612.53F, 228.73F}, {607.10F, 215.07F},
    {600.73F, 201.72F}, {593.45F, 188.74F}, {602.28F, 184.04F}, {609.83F, 197.57F},
    {616.44F, 211.48F}, {622.06F, 225.73F}, {626.70F, 240.25F}, {630.32F, 254.99F},
    {632.92F, 269.90F}, {634.48F, 284.92F}, {635.00F, 300.00F}, {681.00F, 340.00F},
    {681.00F, 1010.00F}, {671.00F, 1010.00F}, {671.00F, 584.00F},
}};

// The inner arch, an open one-sided chain, bottom to top: the right circuit
// wall from (623, 536) to (623, 300), then the arch from 0 to 67 degrees. The
// first and last points are ghosts: they shape the end normals and are not
// collided with. Solid from the field; a ball coming up the launch channel
// behind it passes through. From 28 to 65 degrees it is the launch lane's
// one-way gate: there is no gate body to switch.
inline constexpr std::array<Vec2, 28> kInnerArch{{
    {671.00F, 584.00F}, {623.00F, 536.00F}, {623.00F, 300.00F}, {622.68F, 288.55F},
    {621.73F, 277.14F}, {620.15F, 265.77F}, {617.94F, 254.49F}, {615.10F, 243.31F},
    {611.65F, 232.27F}, {607.59F, 221.39F}, {602.93F, 210.70F}, {597.69F, 200.22F},
    {591.87F, 189.98F}, {585.49F, 179.99F}, {578.56F, 170.29F}, {571.11F, 160.90F},
    {563.14F, 151.84F}, {554.68F, 143.14F}, {545.75F, 134.80F}, {536.36F, 126.86F},
    {526.55F, 119.32F}, {516.33F, 112.22F}, {505.73F, 105.56F}, {494.77F, 99.36F},
    {483.48F, 93.64F}, {471.89F, 88.41F}, {460.02F, 83.68F}, {446.99F, 79.17F},
}};

// The island rim is the inner side of both circuits. Its left arc, from the
// top of the left circuit lane to the first roll call lane, joined by
// capsules; the right arc is its mirror. The ball only ever rides the rim's
// outer side, where capsule joints do not catch it. A curved guide ridden on
// its inner side must be a chain: there a row of capsules kicks the ball at
// every joint.
inline constexpr std::array<Vec2, 11> kRimLeftArc{{
    {145.00F, 300.00F}, {146.30F, 280.61F}, {150.17F, 261.47F}, {156.56F, 242.79F},
    {165.41F, 224.83F}, {176.60F, 207.78F}, {189.99F, 191.87F}, {205.41F, 177.30F},
    {222.69F, 164.23F}, {241.59F, 152.83F}, {261.90F, 143.25F},
}};
// clang-format on

// --- Launch lane -------------------------------------------------------------
inline constexpr float kLaneWallX = 676.0F;  // Centre line of the lane wall.
inline constexpr float kLaneWallTopY = 340.0F;
inline constexpr float kLaneCenterX = (kLaneWallX + kWallRadius + kWallRight) * 0.5F;

// Plunger: a block that closes the bottom of the lane. Pulling it back lowers
// its top face by up to kPlungerTravel.
inline constexpr float kPlungerWidth = 36.0F;
inline constexpr float kPlungerHeight = 60.0F;
inline constexpr float kPlungerRestTopY = 895.0F;
inline constexpr float kPlungerTravel = 40.0F;
// A new ball is placed resting on the plunger.
inline constexpr Vec2 kBallStart{kLaneCenterX, kPlungerRestTopY - kBallRadius};
// A ball has left the launch channel and is in play once it is left of
// kInPlayX (it has ridden the arch past the top lanes), or below the arch's
// centre line and left of the lane island (it came through the one-way arch
// slowly and is in the right circuit lane).
inline constexpr float kInPlayX = 440.0F;
inline constexpr float kInPlayLaneX = 628.0F;
[[nodiscard]] constexpr bool inPlayArea(Vec2 p) noexcept {
  return p.x < kInPlayX || (p.y > kArchCenter.y && p.x < kInPlayLaneX);
}
// Speed given, straight up, to a ball that is launched from the plunger
// without the player (a ball added during play). From 1600 px/s up every
// such ball laps the arch to the left inlane; 1500 leaves some in the roll
// call lanes.
inline constexpr float kAutoLaunchSpeed = 1700.0F;

// --- Straight interior walls ---------------------------------------------------
// Inlane and outlane are divided by a wall whose top (y 622) is above the
// slingshot's top, so a ball in the inlane cannot hop out. Its lower run is
// laid along the line of a resting flipper's top surface and ends inside the
// flipper's base circle, so a ball rolls onto the bat without a bump.
inline constexpr float kDividerX = 83.0F;
inline constexpr float kDividerTopY = 622.0F;
inline constexpr float kDividerKneeY = 738.1F;
inline constexpr Capsule kLeftDividerUpper{
    {kDividerX, kDividerTopY}, {kDividerX, kDividerKneeY}, kWallRadius};
inline constexpr Capsule kLeftDividerLower{
    {kDividerX, kDividerKneeY}, {249.5F, 846.8F}, kWallRadius};
inline constexpr Capsule kRimLeftStraight{{145.0F, 300.0F}, {145.0F, 420.0F}, kWallRadius};
// The foot of each rim leans into the field, widening the circuit's mouth.
inline constexpr Capsule kRimLeftFoot{{145.0F, 420.0F}, {159.0F, 480.0F}, kWallRadius};
// The Hangar: a pocket left of centre with a peaked roof.
inline constexpr Capsule kHangarLeft{{237.0F, 318.0F}, {237.0F, 397.0F}, kWallRadius};
inline constexpr Capsule kHangarRoofLeft{{237.0F, 318.0F}, {279.0F, 296.0F}, kWallRadius};
inline constexpr Capsule kHangarRoofRight{{279.0F, 296.0F}, {321.0F, 318.0F}, kWallRadius};
inline constexpr Capsule kHangarRight{{321.0F, 318.0F}, {321.0F, 400.0F}, kWallRadius};
// The Briefing room: the scoop at dead centre, its mouth flared.
inline constexpr Capsule kScoopFlareLeft{{321.0F, 400.0F}, {313.0F, 428.0F}, kWallRadius};
inline constexpr Capsule kScoopRoofLeft{{321.0F, 318.0F}, {356.0F, 335.0F}, kWallRadius};
inline constexpr Capsule kScoopRoofRight{{356.0F, 335.0F}, {391.0F, 352.0F}, kWallRadius};
inline constexpr Capsule kScoopRight{{391.0F, 352.0F}, {391.0F, 400.0F}, kWallRadius};
inline constexpr Capsule kScoopFlareRight{{391.0F, 400.0F}, {399.0F, 428.0F}, kWallRadius};
// The wall between the Turbine lane and the Skyway's lane.
inline constexpr Capsule kTurbineRail{{473.0F, 335.0F}, {473.0F, 430.0F}, kWallRadius};
// Posts between the three roll call lanes.
inline constexpr Capsule kLanePostLeft{{324.0F, 128.0F}, {324.0F, 168.0F}, kWallRadius};
inline constexpr Capsule kLanePostRight{{387.0F, 128.0F}, {387.0F, 168.0F}, kWallRadius};

// Dead posts are metal, not rubber: a ball that strikes one drops instead of
// springing back at the flippers. The two rim feet, the Turbine lane's rail
// (a ball sliding off the Briefing room's roof strikes it and must drop onto
// the right flipper, not cross to the gap) and the Briefing room's flares.
inline constexpr float kDeadPostRestitution = 0.1F;
inline constexpr std::array<Capsule, 5> kDeadPosts{{
    kRimLeftFoot,
    mirrored(kRimLeftFoot),
    kTurbineRail,
    kScoopFlareLeft,
    kScoopFlareRight,
}};

inline constexpr std::array<Capsule, 15> kWalls{{
    kLeftDividerUpper,
    kLeftDividerLower,
    mirrored(kLeftDividerUpper),
    mirrored(kLeftDividerLower),
    kRimLeftStraight,
    mirrored(kRimLeftStraight),
    kHangarLeft,
    kHangarRoofLeft,
    kHangarRoofRight,
    kHangarRight,
    kScoopRoofLeft,
    kScoopRoofRight,
    kScoopRight,
    kLanePostLeft,
    kLanePostRight,
}};
// Every static capsule wall with the ordinary wall surface: kWalls, then the
// rim arcs, a left part and its mirror in turn. (Dead posts are apart.)
inline constexpr std::size_t kRimArcParts = kRimLeftArc.size() - 1;
inline constexpr std::array<Capsule, kWalls.size() + 2 * kRimArcParts> kAllWalls = [] {
  std::array<Capsule, kWalls.size() + 2 * kRimArcParts> walls{};
  std::size_t n = 0;
  for (const Capsule& wall : kWalls) {
    walls[n++] = wall;
  }
  for (std::size_t i = 0; i < kRimArcParts; ++i) {
    const Capsule part{kRimLeftArc[i], kRimLeftArc[i + 1], kWallRadius};
    walls[n++] = part;
    walls[n++] = mirrored(part);
  }
  return walls;
}();

// --- Slingshots ----------------------------------------------------------------
// The kicker is the long face from point 0 (the top corner) to point 2.
inline constexpr float kSlingTopX = 146.0F;
inline constexpr float kSlingTopY = 668.0F;
inline constexpr Triangle kLeftSlingshot{
    {{{kSlingTopX, kSlingTopY}, {129.0F, 722.0F}, {211.0F, 776.0F}}}, 3.0F};
inline constexpr Triangle kRightSlingshot{
    {{mirrored(kLeftSlingshot.points[0]), mirrored(kLeftSlingshot.points[1]),
      mirrored(kLeftSlingshot.points[2])}},
    kLeftSlingshot.cornerRadius};
inline constexpr std::array<Triangle, 2> kSlingshots{{kLeftSlingshot, kRightSlingshot}};

// Slingshot vanes: a one-sided dead chain above each slingshot, solid from
// the circuit lane only. A ball coming down the lane on its inner side is
// kept off the slingshot's top corner and falls into the inlane; a ball from
// the field passes through it into the lane. Four points: a ghost, the two
// ends of the vane, a ghost.
inline constexpr std::array<Vec2, 4> kSlingVaneLeft{
    {{144.0F, 690.0F}, {146.0F, 664.0F}, {150.0F, 600.0F}, {152.0F, 570.0F}}};
inline constexpr std::array<Vec2, 4> kSlingVaneRight{
    {mirrored(kSlingVaneLeft[3]), mirrored(kSlingVaneLeft[2]), mirrored(kSlingVaneLeft[1]),
     mirrored(kSlingVaneLeft[0])}};

// --- Flippers ------------------------------------------------------------------
// Angles are in radians, measured from +x towards +y (clockwise on screen,
// because y points down). The flipper extends from its pivot along the
// direction of its angle.
struct Flipper {
  Vec2 pivot;
  float restAngle;
  float activeAngle;
};
inline constexpr float kFlipperLength = 98.0F;  // Pivot to tip centre.
inline constexpr float kFlipperBaseRadius = 11.0F;
inline constexpr float kFlipperTipRadius = 6.0F;
inline constexpr float kFlipperSwing = kPi / 6.0F;  // 30 degrees either side of level.
// The gap between the resting tips is 36 units.
inline constexpr Flipper kLeftFlipper{{246.5F, 852.0F}, kFlipperSwing, -kFlipperSwing};
inline constexpr Flipper kRightFlipper{mirrored(kLeftFlipper.pivot), kPi - kFlipperSwing,
                                       kPi + kFlipperSwing};
inline constexpr std::array<Flipper, 2> kFlippers{{kLeftFlipper, kRightFlipper}};
inline constexpr std::size_t kLeftFlipperIndex = 0;
inline constexpr std::size_t kRightFlipperIndex = 1;

// --- Thruster bumpers ------------------------------------------------------------
// Round posts that kick the ball away when touched: a wide triangle in the
// nest inside the rim. Every gap between them and to a wall is closed or
// comfortably wider than the ball.
struct Bumper {
  Circle shape;
};
inline constexpr std::array<Bumper, 3> kBumpers{{
    {{{262.0F, 222.0F}, 24.0F}},
    {{{449.0F, 222.0F}, 24.0F}},
    {{{355.5F, 250.0F}, 24.0F}},
}};

// --- Turbo targets ---------------------------------------------------------------
// Stand-up targets: short rubber-faced bars that register a hit. Two, set
// into the rim's inner side with their ends 6 units inside the rim line, so
// a ball riding the rim's outer side cannot trip them.
enum class TargetId : std::uint8_t { NestLeft, NestRight };
struct Target {
  Capsule shape;
  TargetId id;
};
inline constexpr float kTargetRadius = 5.0F;
inline constexpr Capsule kNestTargetLeft{{175.5F, 218.8F}, {191.0F, 240.0F}, kTargetRadius};
inline constexpr std::array<Target, 2> kTargets{{
    {kNestTargetLeft, TargetId::NestLeft},
    {mirrored(kNestTargetLeft), TargetId::NestRight},
}};

// --- Bay doors -------------------------------------------------------------------
// Three drop targets across the Hangar's mouth. They are one bank: each
// reports its own hits, and they drop and come back up together, so the mouth
// is either closed or wholly open, never open by one door. Standing, they
// and the Hangar's walls leave no way in.
inline constexpr float kDoorY = 392.0F;
inline constexpr std::array<Capsule, 3> kDropTargets{{
    {{247.5F, kDoorY}, {261.0F, kDoorY}, 5.0F},
    {{272.3F, kDoorY}, {285.7F, kDoorY}, 5.0F},
    {{297.0F, kDoorY}, {310.5F, kDoorY}, 5.0F},
}};

// The bank does not come up onto a ball: it waits until no free ball's centre
// is inside this box in front of and behind the door line.
inline constexpr Vec2 kDoorMouthCenter{279.0F, kDoorY};
inline constexpr Vec2 kDoorMouthHalfSize{54.0F, 32.0F};

// --- Rollovers -------------------------------------------------------------------
// Switches in the playfield that register a ball passing over: the three
// roll call lanes at the top and the four return lanes at the bottom.
enum class RolloverGroup : std::uint8_t { TopLane, Inlane, Outlane };
// Which switch it is: the rules light a lamp for each.
enum class RolloverId : std::uint8_t { RollCall1, RollCall2, RollCall3, WingW, WingI, WingN, WingG };
struct Rollover {
  Circle zone;
  RolloverGroup group;
  RolloverId id;
};
inline constexpr float kRolloverRadius = 10.0F;
inline constexpr float kTopLaneY = 150.0F;
inline constexpr float kReturnLaneY = 705.0F;
inline constexpr Vec2 kLeftOutlane{59.0F, kReturnLaneY};
inline constexpr Vec2 kLeftInlane{107.0F, kReturnLaneY};
inline constexpr std::array<Rollover, 7> kRollovers{{
    {{{293.0F, kTopLaneY}, kRolloverRadius}, RolloverGroup::TopLane, RolloverId::RollCall1},
    {{{355.5F, kTopLaneY}, kRolloverRadius}, RolloverGroup::TopLane, RolloverId::RollCall2},
    {{{418.0F, kTopLaneY}, kRolloverRadius}, RolloverGroup::TopLane, RolloverId::RollCall3},
    {{kLeftOutlane, kRolloverRadius}, RolloverGroup::Outlane, RolloverId::WingW},
    {{kLeftInlane, kRolloverRadius}, RolloverGroup::Inlane, RolloverId::WingI},
    {{mirrored(kLeftInlane), kRolloverRadius}, RolloverGroup::Inlane, RolloverId::WingN},
    {{mirrored(kLeftOutlane), kRolloverRadius}, RolloverGroup::Outlane, RolloverId::WingG},
}};
// A rollover is a switch across its lane, a strip and not the drawn circle.
// Its half width is the lane's half width less the ball's edge, so a ball
// hugging the lane's wall still trips it; its half length is the circle's
// radius, except in the roll call lanes, where the strip is longer so that
// a ball rounding the end of the rim into an outer lane is still counted.
inline constexpr float kRolloverReach = 26.0F;
inline constexpr float kOutlaneReach = 19.0F;
inline constexpr float kTopLaneHalfLength = 18.0F;
[[nodiscard]] constexpr Vec2 rolloverHalfSize(const Rollover& rollover) noexcept {
  return {rollover.group == RolloverGroup::Outlane ? kOutlaneReach : kRolloverReach,
          rollover.group == RolloverGroup::TopLane ? kTopLaneHalfLength : rollover.zone.radius};
}

// --- Holes: anything that takes the ball off the field for a moment -----------------
enum class HoleType : std::uint8_t { Scoop, Hangar, RampLeft, RampRight };
struct Hole {
  Vec2 center;
  Vec2 halfSize;  // The ball's centre is taken inside this box.
  HoleType type;
  // Where and how fast the ball comes back, in pixels and pixels per second.
  Vec2 exitPosition;
  Vec2 exitVelocity;
};
[[nodiscard]] constexpr bool isRamp(HoleType type) noexcept {
  return type == HoleType::RampLeft || type == HoleType::RampRight;
}
// A ramp takes only a ball moving up the table at least this fast; a slower
// one rolls on over the mouth into the nest.
inline constexpr float kRampMinUpwardSpeed = 600.0F;
// After returning a ball, a hole ignores it until it has moved this far away.
inline constexpr float kHoleRearmDistance = 46.0F;
inline constexpr std::size_t kScoopHole = 0;
inline constexpr std::size_t kHangarHole = 1;
inline constexpr std::size_t kRunwayHole = 2;
inline constexpr std::size_t kSkywayHole = 3;
inline constexpr std::array<Hole, 4> kHoles{{
    // Briefing room: kicked down to the left flipper from below its flares.
    {{356.0F, 390.0F}, {19.0F, 30.0F}, HoleType::Scoop, {356.0F, 456.0F}, {-95.0F, 200.0F}},
    // Hangar: lobbed from below the door line to the left inlane.
    {{279.0F, 349.0F}, {27.0F, 23.0F}, HoleType::Hangar, {279.0F, 420.0F}, {-211.0F, -60.0F}},
    // Runway and Skyway: set down at the far end of the rail, above an inlane.
    {{192.0F, 425.0F}, {27.0F, 9.0F}, HoleType::RampLeft, {107.0F, 672.0F}, {0.0F, 80.0F}},
    {{519.0F, 425.0F}, {27.0F, 9.0F}, HoleType::RampRight, {604.0F, 672.0F}, {0.0F, 80.0F}},
}};
// Ramp rails: where a ball taken by a ramp is drawn while it rides. It moves
// along the polyline at constant speed over kRampRideSeconds and is set down
// at the last point, the hole's exit.
inline constexpr double kRampRideSeconds = 1.25;
// The speed at which a rail sets the ball down is the hole's exit speed plus
// a whole number of pixels per second drawn from -kRampExitJitter to
// +kRampExitJitter by the game's own seeded generator: 60 to 100 px/s. If it
// were the same to the step every time, so would be the roll down the inlane
// and onto the bat, and a player with steady timing would send every such
// ball to the same place.
inline constexpr int kRampExitJitter = 20;
inline constexpr std::array<Vec2, 9> kRunwayRail{{
    {192.0F, 425.0F},
    {192.0F, 372.0F},
    {170.0F, 318.0F},
    {120.0F, 300.0F},
    {72.0F, 322.0F},
    {64.0F, 380.0F},
    {64.0F, 560.0F},
    {74.0F, 628.0F},
    {107.0F, 672.0F},
}};
inline constexpr std::array<Vec2, 9> kSkywayRail = [] {
  std::array<Vec2, 9> rail{};
  for (std::size_t i = 0; i < rail.size(); ++i) {
    rail[i] = mirrored(kRunwayRail[i]);
  }
  return rail;
}();
[[nodiscard]] constexpr const std::array<Vec2, 9>& rampRail(HoleType type) noexcept {
  return type == HoleType::RampRight ? kSkywayRail : kRunwayRail;
}
// How long the Briefing room and the Hangar keep a ball before returning
// it, in seconds. One ball at a time: a second ball that arrives meanwhile
// is taken too and waits its turn behind the first.
inline constexpr double kScoopHoldSeconds = 0.9;
inline constexpr double kHangarHoldSeconds = 0.9;
[[nodiscard]] constexpr double holdSeconds(HoleType type) noexcept {
  return isRamp(type) ? kRampRideSeconds
                      : (type == HoleType::Hangar ? kHangarHoldSeconds : kScoopHoldSeconds);
}
// The Briefing room does not kick the ball out the same way twice. Its
// exit velocity is the hole's, with a whole number of pixels per second
// added to each part, drawn by the game's own seeded generator: across from
// -kScoopExitJitterLeft to +kScoopExitJitterRight, down from
// -kScoopExitJitterDown to +kScoopExitJitterDown. With one fixed kick the
// ball lands on the same spot of the left flipper every time, and a player
// with steady timing can then repeat one safe round of shots for ever.
inline constexpr int kScoopExitJitterLeft = 15;
inline constexpr int kScoopExitJitterRight = 15;
inline constexpr int kScoopExitJitterDown = 20;
// A ball that drains this soon after the Briefing room or the Hangar
// returned it is owed back to the player: the rules are told so.
inline constexpr double kEjectSaveSeconds = 2.5;

// --- The lock: two balls held in view in the Hangar --------------------------------------
// Where locked balls sit.
inline constexpr std::array<Vec2, 2> kLockBays{{{258.0F, 334.0F}, {300.0F, 334.0F}}};
// Balls leave the Hangar one at a time by its exit, at least this far apart.
inline constexpr double kLockReleaseSpacingSeconds = 0.4;
// Balls leaving close together go different ways: the first is lobbed to the
// left inlane (the hole's exit velocity), the next kicked down to the right
// flipper, the third lobbed left again. A ball follows "close" if it leaves
// within kHangarBurstSeconds of the one before.
inline constexpr Vec2 kHangarEjectRight{160.0F, 150.0F};
inline constexpr double kHangarBurstSeconds = 1.0;

// --- Sensor lines: crossed, never collided with -------------------------------------------
// A crossing is reported once, with the ball, its speed across the line and
// the direction: +1 towards the side that is on the left hand when looking
// from a to b on the screen (y down), -1 the other way. For the two circuit
// mouths and the spinner, drawn left to right, +1 is up the table; for the
// top of the arch, drawn downwards, +1 is to the right (the way a ball that
// went up the left circuit travels).
enum class SensorId : std::uint8_t { CircuitMouthLeft, CircuitMouthRight, ArchTop, Spinner };
struct SensorLine {
  Vec2 a;
  Vec2 b;
  SensorId id;
};
// The spinner: a gate across the Turbine lane that a passing ball sets
// spinning. It is counted, never collided with, and does not slow the ball.
struct Spinner {
  Vec2 a;
  Vec2 b;
};
inline constexpr Spinner kSpinner{{396.0F, 415.0F}, {468.0F, 415.0F}};
inline constexpr std::array<SensorLine, 4> kSensors{{
    {{88.0F, 440.0F}, {145.0F, 440.0F}, SensorId::CircuitMouthLeft},
    {mirrored(Vec2{145.0F, 440.0F}), mirrored(Vec2{88.0F, 440.0F}), SensorId::CircuitMouthRight},
    {{kFieldCenterX, 65.0F}, {kFieldCenterX, 125.0F}, SensorId::ArchTop},
    {kSpinner.a, kSpinner.b, SensorId::Spinner},
}};
inline constexpr std::size_t kSpinnerSensor = 3;
// A ball crossing the spinner at speed v across it (pixels per second) sets
// it spinning v / kSpinSpeedPerSpin times, rounded down, at least once and at
// most kMaxSpinsPerPass. The spins are counted out one every
// kSpinPayoutSteps steps. A ball crossing while spins are still owed adds
// its own to them, up to kMaxSpinsOwed owed at once.
inline constexpr float kSpinSpeedPerSpin = 150.0F;
inline constexpr int kMaxSpinsPerPass = 12;
inline constexpr int kMaxSpinsOwed = 24;
inline constexpr int kSpinPayoutSteps = 6;
[[nodiscard]] constexpr int spinsFor(float speedAcross) noexcept {
  // Written so that a speed that is not a number gives one spin.
  if (!(speedAcross >= 2.0F * kSpinSpeedPerSpin)) {
    return 1;
  }
  if (speedAcross >= static_cast<float>(kMaxSpinsPerPass) * kSpinSpeedPerSpin) {
    return kMaxSpinsPerPass;
  }
  return static_cast<int>(speedAcross / kSpinSpeedPerSpin);
}

// --- Kickback in the left outlane ------------------------------------------------------
// When it is armed, a ball whose centre comes inside the zone is taken and
// the kickback goes out. The ball is not thrown and left to chance: it is
// carried along kKickbackPath, up the outlane and over the top of the
// divider, fast at first (about 950 px/s) and slowing to a stop over
// kKickbackSeconds, and is set down above the left inlane exactly where and
// as the Runway's rail sets its balls down (that hole's exit and exit
// velocity, with the same variation). Whatever speed the ball arrived with,
// it comes back the same way, to the left flipper. The path starts from
// where the ball was taken and joins the table at its second point.
inline constexpr Circle kKickbackZone{{59.0F, 740.0F}, 12.0F};
inline constexpr std::array<Vec2, 6> kKickbackPath{{
    {59.0F, 740.0F},
    {59.0F, 630.0F},
    {68.0F, 604.0F},
    {90.0F, 600.0F},
    {107.0F, 628.0F},
    kHoles[kRunwayHole].exitPosition,
}};
inline constexpr double kKickbackSeconds = 0.5;

// --- Lamps -----------------------------------------------------------------------------
// Positions are centres. Drawn sizes: shot arrows kArrowLampLength long,
// course and Graduation lamps kCourseLampDiameter across, the rest
// kStatusLampDiameter. A shot's arrow is within 60 units of its mouth.
inline constexpr float kArrowLampLength = 28.0F;
inline constexpr float kCourseLampDiameter = 22.0F;
inline constexpr float kStatusLampDiameter = 20.0F;
enum class LampId : std::uint8_t {
  ArrowCircuitLeft,
  ArrowRunway,
  ArrowHangar,
  ArrowBriefing,
  ArrowTurbine,
  ArrowSkyway,
  ArrowCircuitRight,
  Lock1,
  Lock2,
  Course1,
  Course2,
  Course3,
  Course4,
  Course5,
  Course6,
  Graduation,
  ExtraCredit,
  ExtraBall,
  Overdrive,
  TurboCharge,
  Kickback,
  ShootAgain,
  RollCall1,
  RollCall2,
  RollCall3,
  WingW,
  WingI,
  WingN,
  WingG,
  Count,
};
struct Lamp {
  LampId id;
  Vec2 position;
};
inline constexpr std::array<Lamp, 29> kLamps{{
    {LampId::ArrowCircuitLeft, {118.0F, 512.0F}},
    {LampId::ArrowRunway, {204.0F, 470.0F}},
    {LampId::ArrowHangar, {282.0F, 440.0F}},
    {LampId::ArrowBriefing, {356.0F, 474.0F}},
    {LampId::ArrowTurbine, {430.0F, 462.0F}},
    {LampId::ArrowSkyway, {507.0F, 470.0F}},
    {LampId::ArrowCircuitRight, {593.0F, 512.0F}},
    {LampId::Lock1, {262.0F, 416.0F}},
    {LampId::Lock2, {296.0F, 416.0F}},
    {LampId::Course1, {262.0F, 560.0F}},
    {LampId::Course2, {293.0F, 584.0F}},
    {LampId::Course3, {330.0F, 600.0F}},
    {LampId::Course4, {381.0F, 600.0F}},
    {LampId::Course5, {418.0F, 584.0F}},
    {LampId::Course6, {449.0F, 560.0F}},
    {LampId::Graduation, {355.5F, 566.0F}},
    {LampId::ExtraCredit, {334.0F, 498.0F}},
    {LampId::ExtraBall, {378.0F, 498.0F}},
    {LampId::Overdrive, {430.0F, 488.0F}},
    {LampId::TurboCharge, {452.0F, 444.0F}},
    {LampId::Kickback, {59.0F, 672.0F}},
    {LampId::ShootAgain, {355.5F, 930.0F}},
    {LampId::RollCall1, {293.0F, 150.0F}},
    {LampId::RollCall2, {355.5F, 150.0F}},
    {LampId::RollCall3, {418.0F, 150.0F}},
    {LampId::WingW, {59.0F, 705.0F}},
    {LampId::WingI, {107.0F, 705.0F}},
    {LampId::WingN, {604.0F, 705.0F}},
    {LampId::WingG, {652.0F, 705.0F}},
}};
static_assert(kLamps.size() == static_cast<std::size_t>(LampId::Count), "one lamp per id");
[[nodiscard]] constexpr bool isArrowLamp(LampId id) noexcept {
  return id <= LampId::ArrowCircuitRight;
}
[[nodiscard]] constexpr bool isCourseLamp(LampId id) noexcept {
  return id >= LampId::Course1 && id <= LampId::Graduation;
}

// --- Elements --------------------------------------------------------------------------
// Identifies one interactive element of the table. Stored in physics shapes
// and carried by events.
enum class ElementKind : std::uint8_t {
  None,
  Bumper,
  Slingshot,
  Target,
  Rollover,
  Hole,
  DropTarget,
  Sensor,
  Kickback,
};
struct ElementRef {
  ElementKind kind = ElementKind::None;
  std::uint8_t index = 0;

  friend constexpr bool operator==(const ElementRef&, const ElementRef&) noexcept = default;
};

// Number of elements of a kind; 0 for None or an unknown kind.
[[nodiscard]] constexpr std::size_t elementCount(ElementKind kind) noexcept {
  switch (kind) {
    using enum ElementKind;
    case Bumper:
      return kBumpers.size();
    case Slingshot:
      return kSlingshots.size();
    case Target:
      return kTargets.size();
    case Rollover:
      return kRollovers.size();
    case Hole:
      return kHoles.size();
    case DropTarget:
      return kDropTargets.size();
    case Sensor:
      return kSensors.size();
    case Kickback:
      return 1;
    case None:
      break;
  }
  return 0;
}
[[nodiscard]] constexpr bool isValid(ElementRef ref) noexcept {
  return ref.index < elementCount(ref.kind);
}

}  // namespace pinball::table
