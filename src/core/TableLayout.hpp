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
inline constexpr Capsule kHangarLeft{{237.0F, 318.0F}, {237.0F, 372.0F}, kWallRadius};
inline constexpr Capsule kHangarRoofLeft{{237.0F, 318.0F}, {279.0F, 296.0F}, kWallRadius};
inline constexpr Capsule kHangarRoofRight{{279.0F, 296.0F}, {321.0F, 318.0F}, kWallRadius};
inline constexpr Capsule kHangarRight{{321.0F, 318.0F}, {321.0F, 400.0F}, kWallRadius};
// --- Revision 3 (TABLE_REV3.md): the centre --------------------------------------
// The Afterburner lane: the Turbine's spinner lane, moved to dead centre,
// where every early flip of either flipper sends the ball. Its left wall is
// the Hangar's right wall; its one flare is a dead post (right). Above it
// the ball meets a splitter post that turns it left, past the G-force post,
// or right into the bumpers.
inline constexpr Capsule kLaneRight{{391.0F, 360.0F}, {391.0F, 400.0F}, kWallRadius};
inline constexpr Capsule kLaneFlareRight{{391.0F, 400.0F}, {399.0F, 428.0F}, kWallRadius};
// Over the lane, one slope from the Hangar roof's right end down to the
// Briefing room's rail: over the lane it is a one-way gate (a one-sided
// chain, solid from above), so a ball shot up the lane passes into the nest
// and a ball coming down from the nest rolls off to the right, over the rail,
// to the right flipper, and never back down the lane.
inline constexpr std::array<Vec2, 4> kLaneGate{
    {{300.0F, 313.3F}, {321.0F, 318.0F}, {394.0F, 334.4F}, {420.0F, 340.2F}}};
// The Briefing room, off centre: a channel leaning about 11 degrees right,
// along the left flipper's firm shot; a shot from the right flipper (up and
// to the left) meets its left wall. Its right wall is the old Turbine rail,
// whose lower end is the stand-up I.
// Its roof continues the lane's one-way gate down to the rail.
// (Its top face is flush with the gate's line, 5 below the gate's points.)
inline constexpr Capsule kBriefingLeft{{400.0F, 436.0F}, {418.0F, 346.0F}, kWallRadius};
inline constexpr Capsule kBriefingRoof{{394.0F, 339.4F}, {473.0F, 357.0F}, kWallRadius};
inline constexpr Capsule kTurbineRail{{473.0F, 357.0F}, {473.0F, 400.0F}, kWallRadius};
inline constexpr Capsule kRailEnd{{473.0F, 400.0F}, {473.0F, 430.0F}, kWallRadius};

// Posts between the three roll call lanes.
inline constexpr Capsule kLanePostLeft{{324.0F, 128.0F}, {324.0F, 168.0F}, kWallRadius};
inline constexpr Capsule kLanePostRight{{387.0F, 128.0F}, {387.0F, 168.0F}, kWallRadius};

// Dead posts are metal, not rubber: a ball that strikes one drops instead of
// springing back at the flippers. Revision 3 keeps two: the Afterburner
// lane's right flare and the Briefing room's rail. The rest became stand-ups.
inline constexpr float kDeadPostRestitution = 0.1F;
inline constexpr std::array<Capsule, 2> kDeadPosts{{
    kLaneFlareRight,
    kTurbineRail,
}};

// T-A-X-I: stand-up switches where revision 2 had silent dead posts. They
// keep the dead posts' restitution (drain fairness, B4) but score.
// X sits on the Hangar's left wall, low: the lane's left flare was in the
// path of every early flip from the right flipper and dropped those balls
// down the middle (B2), so the lane has no left flare.
inline constexpr Capsule kStandupX{{237.0F, 372.0F}, {237.0F, 397.0F}, kWallRadius};
// Concept A: the Flight Deck's A-C-E stand-ups (indices 4 to 6): A on the
// deck's left wall, C and E set 8 inside the rim arc, all facing the deck
// flipper. They keep the stand-ups' dead bounce.
inline constexpr std::size_t kTaxiStandups = 4;
inline constexpr std::size_t kDeckStandupA = 4;
// Concept A G2: RADAR, three dead stand-ups (lesson 6) on CONCEPT.md's line
// mirrored, (548, 492) to (538, 552), struck from the left flipper.
[[nodiscard]] constexpr Capsule bogeyTarget(std::size_t i) noexcept {
  const float t0 = static_cast<float>(i) / 3.0F + 0.03F;
  const float t1 = static_cast<float>(i + 1) / 3.0F - 0.03F;
  return {{163.0F + 10.0F * t0, 492.0F + 60.0F * t0}, {163.0F + 10.0F * t1, 492.0F + 60.0F * t1}, 5.0F};
}
// BOGEY itself cannot stand on CONCEPT.md's line: the Hangar's exit lobs
// its ball down that line to the left inlane. It stands lower and to the
// right of the lob, (200, 562) to (210, 622), facing the right flipper.
[[nodiscard]] constexpr Capsule bogeyDrop(std::size_t i) noexcept {
  const float t0 = static_cast<float>(i) / 3.0F + 0.03F;
  const float t1 = static_cast<float>(i + 1) / 3.0F - 0.03F;
  return {{200.0F + 10.0F * t0, 562.0F + 60.0F * t0}, {200.0F + 10.0F * t1, 562.0F + 60.0F * t1}, 5.0F};
}
inline constexpr std::array<Capsule, 3> kBogeys{{bogeyDrop(0), bogeyDrop(1), bogeyDrop(2)}};
// A struck BOGEY target drops; the bank comes up again kBogeyResetSeconds
// after the third is down, and (from the Altitude's autoReset level) any
// target down kBogeyAutoResetSeconds rises on its own. Each rises only when
// no ball is over it.
inline constexpr double kBogeyResetSeconds = 3.0;
inline constexpr double kBogeyAutoResetSeconds = 30.0;
inline constexpr std::size_t kRadarStandup = 7;  // RADAR are kStandups 7, 8, 9.
inline constexpr std::array<Capsule, 10> kStandups{{
    kRimLeftFoot,
    mirrored(kRimLeftFoot),
    kStandupX,
    kRailEnd,
    {{482.0F, 236.0F}, {482.0F, 252.0F}, 4.0F},  // A (G2: 8 lower, the CAT SHOT window 3+ on every feed)
    {{518.5F, 200.5F}, {528.3F, 213.0F}, 4.0F},
    {{538.4F, 228.3F}, {547.2F, 246.3F}, 4.0F},
    mirrored(bogeyTarget(0)),
    mirrored(bogeyTarget(1)),
    mirrored(bogeyTarget(2)),
}};
// --- The captive ball (G-force test, revision 3) --------------------------------
// A ball of its own in a short channel between the nest and the left rim,
// where phase 1's G-force post stood. It is held to the channel's centre
// line by a prismatic joint: it slides up the channel when the playfield
// ball strikes its exposed lower face, and gravity and a light spring bring
// it back to rest at the mouth. Reaching the far end closes the switch
// there (the G-meter). It is not one of kMaxBalls and never leaves the
// channel. Nothing here is computed: the axis is the 3-4-5 direction, so
// every point below is exact in decimal.
//
// The channel's walls stop the playfield ball, which is wider (24) than the
// channel inside them (23); the captive ball meets nothing but the
// playfield balls (its joint holds it in the channel), so at rest it is in
// no contact at all.
inline constexpr float kCaptiveBallRadius = 11.0F;
// Up the channel, towards the switch: a unit vector, up and to the left.
inline constexpr Vec2 kCaptiveAxis{-0.8F, -0.6F};
// Square to the axis, up and to the right (the upper wall's side).
inline constexpr Vec2 kCaptiveNormal{0.6F, -0.8F};
// The captive ball's centre at rest, at the channel's mouth: half of it
// stands out of the channel, to be struck.
inline constexpr Vec2 kCaptiveHome{188.0F, 281.0F};
// How far up the channel its centre can go (the stop under the switch).
inline constexpr float kCaptiveTravel = 24.0F;
// The switch closes when the ball is this far up the channel, and is ready
// again once it has come back down to kCaptiveSwitchRearm.
inline constexpr float kCaptiveSwitchAt = 22.0F;
inline constexpr float kCaptiveSwitchRearm = 8.0F;
// The spring that, with gravity, brings the ball back down. It sets how
// hard a hit has to be to close the switch: a ball shot up at it from 70
// below, on a weak Runway shot's line, needs about 550 px/s (some 420 when
// it arrives); softer strikes lift it and let it fall back (ElementTest
// measures it).
inline constexpr float kCaptiveSpringHertz = 2.65F;
// Walls 16.5 either side of the centre line (23 apart inside), from the
// mouth (level with the ball at rest) 36 up the channel, past the stop: the
// lower one ends in the rim, the upper one 4 short of it. With the ball at
// rest the mouth is closed, and the opening between it and the Hangar's
// roof is clear (45 wide).
inline constexpr Capsule kCaptiveWallUpper{{197.9F, 267.8F}, {169.1F, 246.2F}, kWallRadius};
inline constexpr Capsule kCaptiveWallLower{{178.1F, 294.2F}, {149.3F, 272.6F}, kWallRadius};
// The switch behind the stop (drawn: the stop is the joint's limit), square
// to the axis, 37.5 up the channel.
inline constexpr Capsule kCaptiveSwitch{{162.2F, 252.9F}, {153.8F, 264.1F}, 3.0F};
// Its two element indices (ElementKind::CaptiveBall): struck by a ball, and
// the switch closed.
inline constexpr std::uint8_t kCaptiveBallStruck = 0;
inline constexpr std::uint8_t kCaptiveBallSwitch = 1;
// Where its centre is, a translation up the channel from home.
[[nodiscard]] constexpr Vec2 captivePosition(float translation) noexcept {
  return {kCaptiveHome.x + kCaptiveAxis.x * translation,
          kCaptiveHome.y + kCaptiveAxis.y * translation};
}
// A point's distance up the channel from home, and to the side of its
// centre line (towards kCaptiveNormal). The physics and the saved game's
// checks both use these, so they agree to the bit.
[[nodiscard]] constexpr float captiveTranslationAt(Vec2 p) noexcept {
  return (p.x - kCaptiveHome.x) * kCaptiveAxis.x + (p.y - kCaptiveHome.y) * kCaptiveAxis.y;
}
[[nodiscard]] constexpr float captiveOffsetAt(Vec2 p) noexcept {
  return (p.x - kCaptiveHome.x) * kCaptiveNormal.x + (p.y - kCaptiveHome.y) * kCaptiveNormal.y;
}
// How far a captive ball may be found off its line or past its ends before
// it counts as out of its channel (the joint is soft by a fraction of this).
inline constexpr float kCaptiveSlack = 3.0F;

// --- Concept A: the Flight Deck (an enclosed mini-playfield) ---------------------
// Inside the right rim arc, where revision 3 had its right bumper. The rim
// arc is its roof and right side; a straight left wall (x 476) and a floor
// (y 312) close it. Its only ways in are the Skyway's diverter (a ride that
// sets the ball down inside) and its only ways out are two holes: the deck
// drain (a subway to the left inlane) and the CAT SHOT (a ride over the nest
// into the Hangar). The left wall stands 28 clear of the right bumper, so a
// ball always passes between them; the floor stands 13 above a ball rolling
// off the Briefing room's roof, so the nest still empties to the right.
inline constexpr Capsule kDeckLeftWall{{476.0F, 157.0F}, {476.0F, 312.0F}, kWallRadius};
inline constexpr Capsule kDeckFloor{{476.0F, 312.0F}, {566.0F, 312.0F}, kWallRadius};
// Under the deck bat at rest (150 degrees; it only swings up from there) the
// bat, its pivot and the floor closed a pocket that a ball pushed out of the
// bat's body could be left in (D2: at rest at (549.5, 296.5)). A shelf on
// the floor fills it; the resting bat clears it by 1.4 units.
inline constexpr Capsule kDeckBatShelf{{530.0F, 307.0F}, {560.0F, 307.0F}, kWallRadius};
// The box a ball's centre is inside while it is on the deck.
inline constexpr Vec2 kDeckCenter{521.0F, 232.0F};
inline constexpr Vec2 kDeckHalfSize{45.0F, 80.0F};
[[nodiscard]] constexpr bool onDeck(Vec2 p) noexcept {
  return p.x > kDeckCenter.x - kDeckHalfSize.x && p.x < kDeckCenter.x + kDeckHalfSize.x &&
         p.y > kDeckCenter.y - kDeckHalfSize.y && p.y < kDeckCenter.y + kDeckHalfSize.y;
}
// Where the Skyway's diverter sets a ball down on the deck: over the deck
// flipper, moving right onto the bat, which it rolls down towards its tip.
// 10 clear of every solid on the deck and of the bat raised or at rest (G2:
// (532, 252) was inside the raised bat, (532, 236) inside the E stand-up).
inline constexpr Vec2 kDeckSetDown{507.0F, 228.0F};
// A ball may stay this long on the deck; then the deck's bat stays down
// until it has left (no stall under a held button).
inline constexpr double kDeckVisitSeconds = 10.0;
inline constexpr Vec2 kDeckSetDownVelocity{100.0F, 0.0F};

inline constexpr std::array<Capsule, 20> kWalls{{
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
    kLaneRight,
    kBriefingLeft,
    kBriefingRoof,
    kLanePostLeft,
    kLanePostRight,
    kCaptiveWallUpper,
    kCaptiveWallLower,
    kDeckLeftWall,
    kDeckFloor,
    kDeckBatShelf,
}};
// Every static capsule wall with the ordinary wall surface: kWalls, then the
// rim arcs, a left part and its mirror in turn. (Dead posts are apart.)
inline constexpr std::size_t kRimArcParts = kRimLeftArc.size() - 1;
// The Hangar roof's two walls meet in a rounded apex. With friction a ball that
// crept over it could stop a little off the top and stay (G1, P2 E=3 at FL100:
// at rest at (277.7, 279.0), freed by the ball search). Frictionless, a ball
// on that convex corner always has a sideways pull and rolls off: no shelf.
inline constexpr std::array<Capsule, 2> kSlickWalls{{kHangarRoofLeft, kHangarRoofRight}};
[[nodiscard]] constexpr bool isSlickWall(const Capsule& c) noexcept {
  for (const Capsule& s : kSlickWalls) {
    if (s.a.x == c.a.x && s.a.y == c.a.y && s.b.x == c.b.x && s.b.y == c.b.y) {
      return true;
    }
  }
  return false;
}
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
inline constexpr float kFlipperLength = 98.0F;  // The lower flippers: pivot to tip centre.
inline constexpr float kFlipperBaseRadius = 11.0F;
inline constexpr float kFlipperTipRadius = 6.0F;
struct Flipper {
  Vec2 pivot;
  float restAngle;
  float activeAngle;
  float length = kFlipperLength;  // Pivot to tip centre.
  float baseRadius = kFlipperBaseRadius;
  float tipRadius = kFlipperTipRadius;
};
inline constexpr float kFlipperSwing = kPi / 6.0F;  // 30 degrees either side of level.
// The gap between the resting tips is 36 units.
inline constexpr Flipper kLeftFlipper{{246.5F, 852.0F}, kFlipperSwing, -kFlipperSwing};
inline constexpr Flipper kRightFlipper{mirrored(kLeftFlipper.pivot), kPi - kFlipperSwing,
                                       kPi + kFlipperSwing};
// Concept A: the deck flipper, a small right-hand bat inside the Flight Deck,
// driven by the right button. Its base closes the gap to the rim; at rest
// its tip lies on the deck floor, 28 clear of the deck's left wall: the deck
// drain. (Revision 3's upper flipper in the right circuit's lane was cut:
// a third flipper is fair only with its own feed and exit, TABLE_LESSONS 7.)
inline constexpr Flipper kDeckFlipper{{556.0F, 278.0F}, kPi - kFlipperSwing, kPi + kFlipperSwing,
                                      48.0F, 8.0F, 5.0F};
inline constexpr std::array<Flipper, 3> kFlippers{{kLeftFlipper, kRightFlipper, kDeckFlipper}};
inline constexpr std::size_t kLeftFlipperIndex = 0;
inline constexpr std::size_t kRightFlipperIndex = 1;
inline constexpr std::size_t kDeckFlipperIndex = 2;

// --- Thruster bumpers ------------------------------------------------------------
// Round posts that kick the ball away when touched: a wide triangle in the
// nest inside the rim. Every gap between them and to a wall is closed or
// comfortably wider than the ball.
struct Bumper {
  Circle shape;
};
// Concept A: the right one moves in from (449, 222), which is now inside the
// Flight Deck, to (423, 214), and is smaller (20): more than a ball's width
// and 8 to spare (32) clear of the centre bumper (32.5), of the right lane
// post's foot (33.4) and of the right turbo target (32.6), and 28 clear of
// the deck's left wall, so no pocket forms between any of them.
inline constexpr std::array<Bumper, 3> kBumpers{{
    {{{262.0F, 222.0F}, 24.0F}},
    {{{423.0F, 214.0F}, 20.0F}},
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
// Concept A: the right one stands on the outer (nest) face of the Flight
// Deck's left wall, below the right bumper (32 clear of it).
inline constexpr Capsule kNestTargetRight{{468.0F, 250.0F}, {468.0F, 276.0F}, kTargetRadius};
inline constexpr std::array<Target, 2> kTargets{{
    {kNestTargetLeft, TargetId::NestLeft},
    {kNestTargetRight, TargetId::NestRight},
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

// The door a ball at x strikes: the nearest door's (the bank is one shape).
[[nodiscard]] constexpr std::size_t doorAt(float x) noexcept {
  std::size_t best = 0;
  float bestDistance = 1.0e9F;
  for (std::size_t i = 0; i < kDropTargets.size(); ++i) {
    const float mid = 0.5F * (kDropTargets[i].a.x + kDropTargets[i].b.x);
    const float d = x > mid ? x - mid : mid - x;
    if (d < bestDistance) {
      bestDistance = d;
      best = i;
    }
  }
  return best;
}
// The bank does not come up onto a ball: it waits until no free ball's centre
// is inside this box in front of and behind the door line.
inline constexpr Vec2 kDoorMouthCenter{279.0F, kDoorY};
inline constexpr Vec2 kDoorMouthHalfSize{54.0F, 32.0F};

// --- Rollovers -------------------------------------------------------------------
// Switches in the playfield that register a ball passing over: the three
// roll call lanes at the top and the four return lanes at the bottom.
// Flaps (concept A, Rev A.2): five flush rollovers in an arc across the
// insert field, F-L-A-P-S. Flush: they never touch the ball.
enum class RolloverGroup : std::uint8_t { TopLane, Inlane, Outlane, Flaps };
// Which switch it is: the rules light a lamp for each.
enum class RolloverId : std::uint8_t {
  RollCall1,
  RollCall2,
  RollCall3,
  WingW,
  WingI,
  WingN,
  WingG,
  FlapF,
  FlapL,
  FlapA,
  FlapP,
  FlapS,
};
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
inline constexpr float kFlapsY = 616.0F;
inline constexpr std::array<Rollover, 12> kRollovers{{
    {{{293.0F, kTopLaneY}, kRolloverRadius}, RolloverGroup::TopLane, RolloverId::RollCall1},
    {{{355.5F, kTopLaneY}, kRolloverRadius}, RolloverGroup::TopLane, RolloverId::RollCall2},
    {{{418.0F, kTopLaneY}, kRolloverRadius}, RolloverGroup::TopLane, RolloverId::RollCall3},
    {{kLeftOutlane, kRolloverRadius}, RolloverGroup::Outlane, RolloverId::WingW},
    {{kLeftInlane, kRolloverRadius}, RolloverGroup::Inlane, RolloverId::WingI},
    {{mirrored(kLeftInlane), kRolloverRadius}, RolloverGroup::Inlane, RolloverId::WingN},
    {{mirrored(kLeftOutlane), kRolloverRadius}, RolloverGroup::Outlane, RolloverId::WingG},
    {{{291.0F, kFlapsY + 10.0F}, kRolloverRadius}, RolloverGroup::Flaps, RolloverId::FlapF},
    {{{323.0F, kFlapsY + 2.0F}, kRolloverRadius}, RolloverGroup::Flaps, RolloverId::FlapL},
    {{{355.5F, kFlapsY}, kRolloverRadius}, RolloverGroup::Flaps, RolloverId::FlapA},
    {{{388.0F, kFlapsY + 2.0F}, kRolloverRadius}, RolloverGroup::Flaps, RolloverId::FlapP},
    {{{420.0F, kFlapsY + 10.0F}, kRolloverRadius}, RolloverGroup::Flaps, RolloverId::FlapS},
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
  if (rollover.group == RolloverGroup::Flaps) {
    return {rollover.zone.radius, rollover.zone.radius};
  }
  return {rollover.group == RolloverGroup::Outlane ? kOutlaneReach : kRolloverReach,
          rollover.group == RolloverGroup::TopLane ? kTopLaneHalfLength : rollover.zone.radius};
}

// --- Holes: anything that takes the ball off the field for a moment -----------------
// Subway (revision 3, the Briefing room): the ball drops into the room and
// is carried under the playfield, out of sight but shown by a glow moving
// along kBriefingSubway, to a set-down far from the room. (Revision 2's
// Scoop, a hole that kicked the ball back out of its own mouth, is gone.)
// Concept A adds: DeckRamp, the Skyway's ride diverted to the Flight Deck
// (never taken from the field: the Skyway's capture is switched to it);
// CatShot, the deck's hole whose ride ends in the Hangar; DeckDrain, the
// deck's drain, a subway to the left inlane.
enum class HoleType : std::uint8_t { Subway, Hangar, RampLeft, RampRight, DeckRamp, CatShot, DeckDrain };
// Two ways a hole takes the ball. A ramp takes it across its mouth: the box
// is the mouth, and a ball inside it is taken (cupRadius 0). A hole with a
// cup (a scoop, a saucer, a lock pocket, a subway's entry) takes it only at
// the cup: the box is the pocket, a ball inside it rolls on under the
// physics, drawn towards the cup by the pocket's shallow funnel
// (kHoleFunnelPull), and is taken on the step its centre comes within
// cupRadius of the cup's centre (anywhere along its path that step, so a
// fast ball cannot skip over it). Then, all in fixed steps:
//   pull-in  (kHoleSinkSeconds): drawn moving from where it was taken onto
//            the cup's centre while it shrinks and darkens (sink 0 -> 1);
//   down     out of sight, for the rest of its hold;
//   rise     (kHoleRiseSeconds, the last part of the hold): drawn at
//            exitPosition coming up, growing and brightening (sink 1 -> 0);
//   let go   under the physics from exitPosition with exitVelocity.
// For a ball kicked back out of its own cup exitPosition is the cup; for a
// subway it is where the ball comes up. Nothing here names a particular
// table: every hole with a cup works this way.
struct Hole {
  Vec2 center;
  Vec2 halfSize;  // The mouth (a ramp) or the pocket (a hole with a cup).
  HoleType type;
  // Where and how fast the ball comes back, in pixels and pixels per second.
  Vec2 exitPosition;
  Vec2 exitVelocity;
  // The cup, and how near its centre the ball's centre must come to be
  // taken. 0: no cup (a ramp).
  Vec2 cup;
  float cupRadius;
};
[[nodiscard]] constexpr bool hasCup(const Hole& hole) noexcept { return hole.cupRadius > 0.0F; }
[[nodiscard]] constexpr bool isRamp(HoleType type) noexcept {
  return type == HoleType::RampLeft || type == HoleType::RampRight || type == HoleType::DeckRamp;
}
// A ramp takes only a ball moving up the table at least this fast; a slower
// one rolls on over the mouth into the nest.
inline constexpr float kRampMinUpwardSpeed = 600.0F;
// The Briefing room (revision 3) takes only a ball going up the table and
// to the right, at least this much across for each unit up: the left
// flipper's firm shot (about 0.25 to 0.30) and not the right flipper's
// (about 0.13 to 0.16), which the subway's kick-out feeds. Without it a
// player with perfect timing could cradle on the right flipper and loop the
// room for ever; a ball turned away rises to the roof and falls back out.
inline constexpr float kScoopMinRightward = 0.20F;
// After returning a ball, a hole ignores it until it has moved this far away.
inline constexpr float kHoleRearmDistance = 46.0F;
// The cups (before build 3 the ball was taken anywhere in the pocket's box,
// i.e. at its mouth, and was seen to vanish there). How near the cup's
// centre the ball's centre must come: a ball radius, so that the ball is
// over the cup when it is taken.
inline constexpr float kHoleCupRadius = kBallRadius;
// Concept A: the Mission scoop's cup (the Briefing room's hole as the art
// draws it), the Hangar's, the CAT SHOT's and the deck drain's.
inline constexpr Vec2 kScoopCup{441.0F, 390.0F};
inline constexpr Vec2 kHangarCup{279.0F, 349.0F};
inline constexpr Vec2 kCatCup{495.0F, 196.0F};
inline constexpr Vec2 kDeckDrainCup{495.0F, 296.0F};
// The pocket's funnel, for a hole with a cup: inside the pocket a ball the
// hole will take is pulled towards the cup by kHoleFunnelPull per second
// squared for each pixel it is away, and its speed is let go at
// kHoleFunnelDamping per second, like a dished floor: a ball that misses
// the cup on the way in, or comes back off the roof, rolls to it instead of
// out of the mouth.
inline constexpr float kHoleFunnelPull = 150.0F;
inline constexpr float kHoleFunnelDamping = 10.0F;
inline constexpr std::size_t kScoopHole = 0;
inline constexpr std::size_t kHangarHole = 1;
inline constexpr std::size_t kRunwayHole = 2;
inline constexpr std::size_t kSkywayHole = 3;
inline constexpr std::size_t kDeckRampHole = 4;
inline constexpr std::size_t kCatHole = 5;
inline constexpr std::size_t kDeckDrainHole = 6;
inline constexpr std::array<Hole, 7> kHoles{{
    // Briefing room (revision 3; concept A's Mission scoop): off centre;
    // taken at its cup and returned by its subway, set down above the right
    // inlane like the Skyway's rail, to the right flipper: a repeat needs a
    // transfer.
    {{441.0F, 385.0F}, {15.0F, 25.0F}, HoleType::Subway, {604.0F, 640.0F}, {0.0F, 80.0F}, kScoopCup,
     kHoleCupRadius},
    // Hangar: taken at its cup between the bays; it comes up again below
    // the door line, like a subway (the door bank may stand between the cup
    // and the field), and is lobbed to the left inlane.
    {{279.0F, 349.0F}, {27.0F, 23.0F}, HoleType::Hangar, {279.0F, 420.0F}, {-211.0F, -60.0F},
     kHangarCup, kHoleCupRadius},
    // Runway and Skyway: set down at the far end of the rail, above an inlane.
    {{192.0F, 425.0F}, {27.0F, 9.0F}, HoleType::RampLeft, {107.0F, 672.0F}, {0.0F, 80.0F}, {}, 0.0F},
    {{519.0F, 425.0F}, {27.0F, 9.0F}, HoleType::RampRight, {604.0F, 672.0F}, {0.0F, 80.0F}, {},
     0.0F},
    // Concept A. The Skyway diverted: a box of no size and no cup (the field
    // never takes a ball into it), set down on the Flight Deck.
    {{519.0F, 425.0F}, {0.0F, 0.0F}, HoleType::DeckRamp, kDeckSetDown, kDeckSetDownVelocity, {}, 0.0F},
    // CAT SHOT: the deck's top left corner, between its wall and C, taken at
    // its cup. Its ride ends in the Hangar, which locks or returns the ball
    // as if it had been shot in (exit: the Hangar's).
    {{495.0F, 196.0F}, {14.0F, 16.0F}, HoleType::CatShot, {279.0F, 420.0F}, {-211.0F, -60.0F},
     kCatCup, kHoleCupRadius},
    // Deck drain: past the deck flipper's tip, on the floor, taken at its
    // cup; a subway sets the ball down where the Runway's rail does, above
    // the left inlane.
    {{495.0F, 292.0F}, {14.0F, 12.0F}, HoleType::DeckDrain, {107.0F, 672.0F}, {0.0F, 80.0F},
     kDeckDrainCup, kHoleCupRadius},
}};
// Ramp rails: where a ball taken by a ramp is drawn while it rides. It moves
// along the polyline at constant speed over kRampRideSeconds and is set down
// at the last point, the hole's exit.
inline constexpr double kRampRideSeconds = 0.85;
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
// The Skyway's branch to the Flight Deck (concept A): up the Skyway, then
// over the deck to its set-down.
inline constexpr std::array<Vec2, 9> kDeckRail{{
    {519.0F, 425.0F},
    {519.0F, 372.0F},
    {524.0F, 340.0F},
    {534.0F, 312.0F},
    {548.0F, 290.0F},
    {552.0F, 262.0F},
    {540.0F, 242.0F},
    {522.0F, 231.0F},
    kDeckSetDown,
}};
[[nodiscard]] constexpr const std::array<Vec2, 9>& rampRail(HoleType type) noexcept {
  return type == HoleType::RampRight ? kSkywayRail
                                     : (type == HoleType::DeckRamp ? kDeckRail : kRunwayRail);
}
// The CAT SHOT's ride: out of the deck's corner, over the top lanes and
// down onto the Hangar's roof into the Hangar (drawn above the field).
inline constexpr double kCatRideSeconds = 0.9;
inline constexpr std::array<Vec2, 9> kCatRail{{
    {495.0F, 196.0F},
    {482.0F, 140.0F},
    {440.0F, 108.0F},
    {380.0F, 96.0F},
    {324.0F, 100.0F},
    {290.0F, 128.0F},
    {280.0F, 180.0F},
    {279.0F, 260.0F},
    {279.0F, 349.0F},
}};
// The deck drain's subway: under the field from the drain to the left
// inlane's set-down; the ball waits kDeckDrainWaitSeconds in the drain, then
// rides for kDeckDrainRideSeconds (the glow).
inline constexpr double kDeckDrainWaitSeconds = 0.3;
inline constexpr double kDeckDrainRideSeconds = 0.6;
inline constexpr std::array<Vec2, 6> kDeckSubway{{
    {495.0F, 296.0F},
    {470.0F, 380.0F},
    {360.0F, 470.0F},
    {200.0F, 560.0F},
    {120.0F, 620.0F},
    {107.0F, 672.0F},
}};
// How long the Briefing room and the Hangar keep a ball before returning
// it, in seconds. One ball at a time: a second ball that arrives meanwhile
// is taken too and waits its turn behind the first.
inline constexpr double kScoopHoldSeconds = 0.9;
inline constexpr double kHangarHoldSeconds = 0.9;
[[nodiscard]] constexpr double holdSeconds(HoleType type) noexcept {
  if (type == HoleType::CatShot) {
    return kCatRideSeconds;
  }
  if (type == HoleType::DeckDrain) {
    return kDeckDrainWaitSeconds + kDeckDrainRideSeconds;
  }
  return isRamp(type) ? kRampRideSeconds
                      : (type == HoleType::Hangar ? kHangarHoldSeconds : kScoopHoldSeconds);
}
// The Briefing room's subway (revision 3). The room keeps the ball for
// kScoopHoldSeconds in all, as phase 1 measured it: it waits in the room for
// the first kScoopHoldSeconds - kSubwayRideSeconds, then rides under the
// playfield along kBriefingSubway, from below the room's hole to the set-down
// above the right inlane (its exit), over kSubwayRideSeconds. The ride is
// for the eye only (a glow under the playfield: the rule "never hide the ball
// without showing its return"); the game holds the ball the whole time and
// sets it down at the hole's exit when the hold is over. A ball kept waiting
// for its exit to clear is shown waiting under the set-down.
inline constexpr double kSubwayRideSeconds = 0.5;
inline constexpr std::array<Vec2, 6> kBriefingSubway{{
    {441.0F, 390.0F},  // Under the room's hole (kScoopDropPoint).
    {468.0F, 436.0F},
    {520.0F, 500.0F},
    {572.0F, 560.0F},
    {604.0F, 600.0F},
    {604.0F, 640.0F},  // The set-down: the hole's exit.
}};
// The Briefing room does not kick the ball out the same way twice. Its
// exit velocity is the hole's, with a whole number of pixels per second
// added to each part, drawn by the game's own seeded generator: across from
// -kScoopExitJitterLeft to +kScoopExitJitterRight, down from
// -kScoopExitJitterDown to +kScoopExitJitterDown. With one fixed kick the
// ball lands on the same spot of the left flipper every time, and a player
// with steady timing can then repeat one safe round of shots for ever
// (left inlane, Briefing room, right flipper, slingshots, Runway, and
// round again: the reflex player's balls that ran to the 20 minute cap).
// (Revision 3: the subway sets the ball down straight, like the rails.)
inline constexpr int kScoopExitJitterLeft = 0;
inline constexpr int kScoopExitJitterRight = 0;
inline constexpr int kScoopExitJitterDown = 20;
// Where a ball is seen to drop: the cup (for the Briefing room, its hole as
// the art draws it).
[[nodiscard]] constexpr Vec2 holeDropPoint(const Hole& hole) noexcept {
  return hasCup(hole) ? hole.cup : hole.center;
}
// Where a ball is seen to come up when a hole with a cup returns it, and
// where it is let go: the hole's exit (the subway's set-down, the Hangar's
// exit below the door line).
[[nodiscard]] constexpr Vec2 holeRisePoint(const Hole& hole) noexcept { return hole.exitPosition; }
inline constexpr Vec2 kScoopDropPoint = kScoopCup;
static_assert(kBriefingSubway.front().x == kScoopDropPoint.x &&
              kBriefingSubway.front().y == kScoopDropPoint.y);
static_assert(kBriefingSubway.back().x == kHoles[kScoopHole].exitPosition.x &&
              kBriefingSubway.back().y == kHoles[kScoopHole].exitPosition.y);
static_assert(kDeckSubway.front().x == kDeckDrainCup.x && kDeckSubway.front().y == kDeckDrainCup.y);
static_assert(kSubwayRideSeconds < kScoopHoldSeconds);
// The pull-in: from where it was taken onto the cup's centre, shrinking and
// darkening, then out of sight. A locked ball is drawn rolling from the cup
// to its bay over kLockRollSeconds instead, in view.
inline constexpr double kHoleSinkSeconds = 0.125;
// The rise: coming up at the exit, growing and brightening, before the
// physics has the ball again. The last part of the hold.
inline constexpr double kHoleRiseSeconds = 0.125;
// A locked ball rolls from the cup to its bay over this long; let go, it
// rolls from its bay back to the cup over the same time, dropping in as it
// gets there, and then comes up at the exit like any other.
inline constexpr double kLockRollSeconds = 0.15;
static_assert(kHoleSinkSeconds >= 0.08 && kHoleSinkSeconds <= 0.15);
static_assert(kHoleSinkSeconds + kHoleRiseSeconds <= kScoopHoldSeconds &&
              kHoleSinkSeconds + kHoleRiseSeconds <= kHangarHoldSeconds &&
              kHoleSinkSeconds + kHoleRiseSeconds <= kDeckDrainWaitSeconds + kDeckDrainRideSeconds);
// How much smaller and darker a ball is drawn when it is all the way down.
inline constexpr float kHoleSinkShrink = 0.5F;
inline constexpr float kHoleSinkDarken = 0.7F;
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
// A spinner: a blade across a lane that a passing ball sets spinning. It is
// counted, never collided with, and does not slow the ball. Each lies on a
// sensor line (kSensors[sensor]) and keeps its own turns and its own phase
// (Game::spinnerPhase). bothWays: a ball going down the lane turns it too;
// otherwise only one going up (+1 across its sensor) does.
struct Spinner {
  Vec2 a;
  Vec2 b;
  std::size_t sensor = 3;  // kSpinnerSensor
  bool bothWays = true;
};
// The Afterburner lane's spinner (revision 3: the centre lane).
inline constexpr Spinner kSpinner{{326.0F, 404.0F}, {386.0F, 404.0F}};
inline constexpr std::array<SensorLine, 4> kSensors{{
    {{88.0F, 440.0F}, {145.0F, 440.0F}, SensorId::CircuitMouthLeft},
    {mirrored(Vec2{145.0F, 440.0F}), mirrored(Vec2{88.0F, 440.0F}), SensorId::CircuitMouthRight},
    {{kFieldCenterX, 65.0F}, {kFieldCenterX, 125.0F}, SensorId::ArchTop},
    {kSpinner.a, kSpinner.b, SensorId::Spinner},
}};
inline constexpr std::size_t kSpinnerSensor = 3;
// Revision 3: three spinners. 0 the Afterburner lane's (both ways), 1 and 2
// across the left and right circuit mouths on the mouths' sensors, 57 long.
// The circuit spinners turn for a ball going up the circuit only: the made
// circuit's return comes down the other mouth, and phase 1 measured the table
// with the circuits paying on the way in (TABLE_REV3_PHASE1.md 1); whether
// they should turn both ways is the rules' call (phase 3).
inline constexpr std::size_t kLaneSpinner = 0;
inline constexpr std::size_t kCircuitSpinnerLeft = 1;
inline constexpr std::size_t kCircuitSpinnerRight = 2;
inline constexpr std::array<Spinner, 3> kSpinners{{
    kSpinner,
    {kSensors[0].a, kSensors[0].b, 0, false},
    {kSensors[1].a, kSensors[1].b, 1, false},
}};
// The spinner on a sensor line, or kSpinners.size() if it has none.
[[nodiscard]] constexpr std::size_t spinnerOnSensor(std::size_t sensor) noexcept {
  for (std::size_t i = 0; i < kSpinners.size(); ++i) {
    if (kSpinners[i].sensor == sensor) {
      return i;
    }
  }
  return kSpinners.size();
}
static_assert(spinnerOnSensor(kSpinnerSensor) == kLaneSpinner);
static_assert(kSensors[kSpinners[kCircuitSpinnerLeft].sensor].id == SensorId::CircuitMouthLeft);
static_assert(kSensors[kSpinners[kCircuitSpinnerRight].sensor].id == SensorId::CircuitMouthRight);
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
// kKickbackSeconds, and is set down above the left inlane where the Runway's
// rail sets its balls down, moving down at kKickbackExitVelocity (with the
// rails' variation, kRampExitJitter). Whatever speed the ball arrived with,
// it comes back the same way, to the left flipper. The path starts from
// where the ball was taken and joins the table at its second point.
// The set-down speed is the one the reflex player loses fewest balls from
// (measured from 0 to 300 px/s: about 1 percent lost within three seconds
// at 150, about 2 percent at the rails' 80).
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
inline constexpr Vec2 kKickbackExitVelocity{0.0F, 150.0F};

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
  // Revision 3 (TABLE_REV3.md 3.10): ladders and letters, at the inserts
  // the art bakes for them (tools/art/palette.py PLANNED_LAMPS).
  DroneArmour1,  // Bottom; one lamp a hit, 5 knocks it down.
  DroneArmour2,
  DroneArmour3,
  DroneArmour4,
  DroneArmour5,
  GForce1,  // 1G (bottom) to 5G, beside the G-force post.
  GForce2,
  GForce3,
  GForce4,
  GForce5,
  TaxiT,  // One beside each stand-up (kStandups order: see taxiLamp).
  TaxiA,
  TaxiX,
  TaxiI,
  Rank1,  // The rank ladder, bottom to top.
  Rank2,
  Rank3,
  Rank4,
  Rank5,
  Rank6,
  Rank7,
  Combo2X,
  Combo3X,
  Combo4X,
  Combo5X,
  Bonus2X,
  Bonus3X,
  Bonus4X,
  Bonus5X,
  // Concept A (G6): the art's proposed lamps (tools/art/table_a lamp_plan.json,
  // adopted at G1), then the Flight Deck's letters. Each shot's progress
  // ladder: three pips below its arrow, the first nearest the arrow.
  LadderCircuitLeft1,
  LadderCircuitLeft2,
  LadderCircuitLeft3,
  LadderRunway1,
  LadderRunway2,
  LadderRunway3,
  LadderHangar1,
  LadderHangar2,
  LadderHangar3,
  LadderBriefing1,
  LadderBriefing2,
  LadderBriefing3,
  LadderTurbine1,
  LadderTurbine2,
  LadderTurbine3,
  LadderSkyway1,
  LadderSkyway2,
  LadderSkyway3,
  LadderCircuitRight1,
  LadderCircuitRight2,
  LadderCircuitRight3,
  AltitudeFL100,  // The altimeter, bottom (FL100) to top (FL500).
  AltitudeFL200,
  AltitudeFL300,
  AltitudeFL400,
  AltitudeFL500,
  FlapsF,  // One under each F-L-A-P-S rollover (kRollovers order).
  FlapsL,
  FlapsA,
  FlapsP,
  FlapsS,
  Bogey1,  // Beside the BOGEY drops and the RADAR stand-ups (G2 builds them).
  Bogey2,
  Bogey3,
  Radar1,
  Radar2,
  Radar3,
  SafetyPost,
  Multiball,
  DeckLit,  // The Skyway's diverter sends the next ball to the deck.
  DeckA,    // On the field side of the deck's A-C-E stand-ups.
  DeckC,
  DeckE,
  CatShot,  // Below the CAT SHOT cup.
  Count,
};
struct Lamp {
  LampId id;
  Vec2 position;
};
inline constexpr std::array<Lamp, 102> kLamps{{
    {LampId::ArrowCircuitLeft, {118.0F, 512.0F}},
    {LampId::ArrowRunway, {204.0F, 470.0F}},
    {LampId::ArrowHangar, {282.0F, 440.0F}},
    {LampId::ArrowBriefing, {432.0F, 468.0F}},
    {LampId::ArrowTurbine, {356.0F, 458.0F}},
    {LampId::ArrowSkyway, {507.0F, 470.0F}},
    {LampId::ArrowCircuitRight, {593.0F, 512.0F}},
    {LampId::Lock1, {262.0F, 416.0F}},
    {LampId::Lock2, {296.0F, 416.0F}},
    // Concept A G1: the centre insert field now holds the pylon (355.5, 548),
    // F-L-A-P-S (y 616-626), the Air Pocket (355.5, 652) and the Prop Wash
    // (355.5, 712). Graduation and courses 3-4 moved up clear of the pylon
    // and L/P, the combo and bonus ladders down clear of F/S, and the rank
    // chevrons out of the centre to x 500 (the mirror of the Altitude
    // ladder's x 211). No lamp overlaps a device or another lamp's group.
    {LampId::Course1, {262.0F, 560.0F}},
    {LampId::Course2, {293.0F, 584.0F}},
    {LampId::Course3, {327.0F, 596.0F}},
    {LampId::Course4, {384.0F, 596.0F}},
    {LampId::Course5, {418.0F, 584.0F}},
    {LampId::Course6, {449.0F, 560.0F}},
    {LampId::Graduation, {355.5F, 572.0F}},
    {LampId::ExtraCredit, {334.0F, 498.0F}},
    {LampId::ExtraBall, {378.0F, 498.0F}},
    {LampId::Overdrive, {402.0F, 520.0F}},
    {LampId::TurboCharge, {356.0F, 520.0F}},
    {LampId::Kickback, {59.0F, 662.0F}},
    {LampId::ShootAgain, {355.5F, 930.0F}},
    {LampId::RollCall1, {293.0F, 150.0F}},
    {LampId::RollCall2, {355.5F, 150.0F}},
    {LampId::RollCall3, {418.0F, 150.0F}},
    {LampId::WingW, {59.0F, 705.0F}},
    {LampId::WingI, {107.0F, 705.0F}},
    {LampId::WingN, {604.0F, 705.0F}},
    {LampId::WingG, {652.0F, 705.0F}},
    {LampId::DroneArmour1, {356.0F, 386.0F}},
    {LampId::DroneArmour2, {356.0F, 373.5F}},
    {LampId::DroneArmour3, {356.0F, 361.0F}},
    {LampId::DroneArmour4, {356.0F, 348.5F}},
    {LampId::DroneArmour5, {356.0F, 336.0F}},
    {LampId::GForce1, {212.0F, 370.0F}},
    {LampId::GForce2, {212.0F, 356.0F}},
    {LampId::GForce3, {212.0F, 342.0F}},
    {LampId::GForce4, {212.0F, 328.0F}},
    {LampId::GForce5, {212.0F, 314.0F}},
    {LampId::TaxiT, {176.0F, 462.0F}},
    {LampId::TaxiA, {535.0F, 462.0F}},
    {LampId::TaxiX, {221.0F, 392.0F}},
    {LampId::TaxiI, {487.0F, 450.0F}},
    {LampId::Rank1, {500.0F, 662.0F}},
    {LampId::Rank2, {500.0F, 650.0F}},
    {LampId::Rank3, {500.0F, 638.0F}},
    {LampId::Rank4, {500.0F, 626.0F}},
    {LampId::Rank5, {500.0F, 614.0F}},
    {LampId::Rank6, {500.0F, 602.0F}},
    {LampId::Rank7, {500.0F, 590.0F}},
    {LampId::Combo2X, {262.0F, 664.0F}},
    {LampId::Combo3X, {278.0F, 658.0F}},
    {LampId::Combo4X, {294.0F, 652.0F}},
    {LampId::Combo5X, {310.0F, 646.0F}},
    {LampId::Bonus2X, {449.0F, 664.0F}},
    {LampId::Bonus3X, {433.0F, 658.0F}},
    {LampId::Bonus4X, {417.0F, 652.0F}},
    {LampId::Bonus5X, {401.0F, 646.0F}},
    // Concept A (G6). Where the art placed them (lamp_plan.json), except the
    // Afterburner's pips, which G1 moved to clear TurboCharge.
    {LampId::LadderCircuitLeft1, {118.0F, 535.0F}},
    {LampId::LadderCircuitLeft2, {118.0F, 548.0F}},
    {LampId::LadderCircuitLeft3, {118.0F, 561.0F}},
    {LampId::LadderRunway1, {209.9F, 492.2F}},
    {LampId::LadderRunway2, {213.3F, 504.8F}},
    {LampId::LadderRunway3, {216.6F, 517.3F}},
    {LampId::LadderHangar1, {283.4F, 463.0F}},
    {LampId::LadderHangar2, {284.2F, 475.9F}},
    {LampId::LadderHangar3, {285.1F, 488.9F}},
    {LampId::LadderBriefing1, {429.0F, 490.8F}},
    {LampId::LadderBriefing2, {427.3F, 503.7F}},
    {LampId::LadderBriefing3, {425.6F, 516.6F}},
    {LampId::LadderTurbine1, {356.0F, 476.0F}},
    {LampId::LadderTurbine2, {356.0F, 486.0F}},
    {LampId::LadderTurbine3, {356.0F, 496.0F}},
    {LampId::LadderSkyway1, {501.1F, 492.2F}},
    {LampId::LadderSkyway2, {497.7F, 504.8F}},
    {LampId::LadderSkyway3, {494.4F, 517.3F}},
    {LampId::LadderCircuitRight1, {593.0F, 535.0F}},
    {LampId::LadderCircuitRight2, {593.0F, 548.0F}},
    {LampId::LadderCircuitRight3, {593.0F, 561.0F}},
    {LampId::AltitudeFL100, {211.0F, 676.0F}},
    {LampId::AltitudeFL200, {211.0F, 658.0F}},
    {LampId::AltitudeFL300, {211.0F, 640.0F}},
    {LampId::AltitudeFL400, {211.0F, 622.0F}},
    {LampId::AltitudeFL500, {211.0F, 604.0F}},
    {LampId::FlapsF, {291.0F, 626.0F}},
    {LampId::FlapsL, {323.0F, 618.0F}},
    {LampId::FlapsA, {355.5F, 616.0F}},
    {LampId::FlapsP, {388.0F, 618.0F}},
    {LampId::FlapsS, {420.0F, 626.0F}},
    {LampId::Bogey1, {188.0F, 505.0F}},
    {LampId::Bogey2, {190.0F, 525.0F}},
    {LampId::Bogey3, {192.0F, 545.0F}},
    {LampId::Radar1, {523.0F, 505.0F}},
    {LampId::Radar2, {521.0F, 525.0F}},
    {LampId::Radar3, {519.0F, 545.0F}},
    {LampId::SafetyPost, {355.5F, 904.0F}},
    {LampId::Multiball, {312.0F, 530.0F}},
    {LampId::DeckLit, {540.0F, 498.0F}},
    {LampId::DeckA, {493.0F, 236.0F}},
    {LampId::DeckC, {514.7F, 213.6F}},
    {LampId::DeckE, {532.9F, 242.1F}},
    {LampId::CatShot, {497.0F, 220.0F}},
}};
static_assert(kLamps.size() == static_cast<std::size_t>(LampId::Count), "one lamp per id");
[[nodiscard]] constexpr bool isArrowLamp(LampId id) noexcept {
  return id <= LampId::ArrowCircuitRight;
}
[[nodiscard]] constexpr bool isCourseLamp(LampId id) noexcept {
  return id >= LampId::Course1 && id <= LampId::Graduation;
}
// Revision 3's ladders and letters (drawn as the art's small inserts).
[[nodiscard]] constexpr bool isLadderLamp(LampId id) noexcept { return id >= LampId::DroneArmour1; }
// The lamp beside a stand-up (kStandups order).
[[nodiscard]] constexpr LampId taxiLamp(std::size_t standup) noexcept {
  return static_cast<LampId>(static_cast<std::size_t>(LampId::TaxiT) +
                             (standup < kTaxiStandups ? standup : 0));
}
// Concept A's lamp groups (G6). Each shot's arrow (LampId order, the first
// seven lamps) has a ladder of three pips; k is 0 (nearest the arrow) to 2.
inline constexpr std::size_t kShotLadderPips = 3;
[[nodiscard]] constexpr LampId ladderLamp(std::size_t arrow, std::size_t k) noexcept {
  return static_cast<LampId>(static_cast<std::size_t>(LampId::LadderCircuitLeft1) +
                             (arrow < 7 ? arrow : 0) * kShotLadderPips +
                             (k < kShotLadderPips ? k : 0));
}
[[nodiscard]] constexpr bool isPipLamp(LampId id) noexcept {
  return id >= LampId::LadderCircuitLeft1 && id <= LampId::LadderCircuitRight3;
}
// The altimeter's rung for a level, 0 (FL100) to 4 (FL500).
[[nodiscard]] constexpr LampId altitudeLamp(std::size_t level) noexcept {
  return static_cast<LampId>(static_cast<std::size_t>(LampId::AltitudeFL100) +
                             (level < 5 ? level : 4));
}
// F-L-A-P-S letter 0 to 4.
[[nodiscard]] constexpr LampId flapsLamp(std::size_t letter) noexcept {
  return static_cast<LampId>(static_cast<std::size_t>(LampId::FlapsF) + (letter < 5 ? letter : 0));
}
// A deck stand-up's letter (kStandups index kDeckStandupA to kDeckStandupA + 2).
[[nodiscard]] constexpr LampId deckLetterLamp(std::size_t standup) noexcept {
  return static_cast<LampId>(static_cast<std::size_t>(LampId::DeckA) +
                             (standup >= kDeckStandupA && standup < kDeckStandupA + 3
                                  ? standup - kDeckStandupA
                                  : 0));
}

// --- Flasher domes (revision 3, TABLE_REV3.md 3.10) ------------------------------------
// Each stands on a solid where no ball rolls; the art bakes its base unlit
// (tools/art/palette.py FLASHER_DOMES, same positions); the light show
// fires them. (No Tower dome: revision 3 has no upper flipper.)
enum class FlasherId : std::uint8_t {
  Hangar,
  Briefing,
  Afterburner,
  Runway,
  Skyway,
  Nest,
  SlingLeft,
  SlingRight,
  // Concept A (G1): on the top of the deck's left wall, and on the top of
  // the left rim straight (the Left Loop's arch).
  Deck,
  LeftArch,
  Count,
};
inline constexpr std::array<Vec2, 10> kFlasherDomes{{
    {279.0F, 296.0F},
    {473.0F, 357.0F},
    {391.0F, 360.0F},
    {145.0F, 392.0F},
    {566.0F, 392.0F},
    {324.0F, 168.0F},
    {150.0F, 692.0F},
    {561.0F, 692.0F},
    {476.0F, 165.0F},
    {145.0F, 300.0F},
}};
static_assert(kFlasherDomes.size() == static_cast<std::size_t>(FlasherId::Count));

// --- The training drone (revision 3) ---------------------------------------------------
// A moving bash target over the Afterburner lane's exit: a hexagon, a corner
// pointing down the table so that its lower faces throw a ball aside. It
// patrols from kDroneLeftX to kDroneRightX and back over kDronePatrolSeconds,
// then dives out of play for kDroneDiveSeconds (the lane is open), and comes
// up again only when no ball is where it rises. Its clock is the physics'
// own step count: the same on every platform.
inline constexpr float kDroneRadius = 15.0F;  // Centre to corner.
// Phase 1 moved it from the lane's mouth (y 452) to the top of the lane,
// above the one-way gate: at the mouth its rebounds came back down the middle
// (B2b 12.7%, against 8.7% here); up here a ball it turns back lands on the
// gate and rolls off to the right flipper.
inline constexpr float kDroneY = 293.0F;
inline constexpr float kDroneLeftX = 332.0F;
inline constexpr float kDroneRightX = 380.0F;
inline constexpr double kDronePatrolSeconds = 4.0;
inline constexpr double kDroneDiveSeconds = 4.0;
// Knocked down by the rules (DRONE DOWN): out of play this long, then up
// again as soon as its place is clear.
inline constexpr double kDroneDownSeconds = 20.0;
// For the eye only (Game::drone()): a hit throws the drone back kDroneRecoil
// units, away from the ball, and it comes back, over kDroneRecoilSeconds
// (out over the first half, back over the second). The body itself keeps to
// its path: a kinematic body thrown 10 units up would squeeze a ball against
// the centre bumper 4 units above it, and a ball that has hit it is already
// on its way. It is drawn sinking into the table over kDroneSinkSeconds when
// it dives or is knocked down, and coming up over kDroneRiseSeconds when it
// rises (it is solid from the moment it rises, as a drop target is).
inline constexpr float kDroneRecoil = 10.0F;
inline constexpr double kDroneRecoilSeconds = 0.2;
inline constexpr double kDroneSinkSeconds = 0.35;
inline constexpr double kDroneRiseSeconds = 0.15;
// Hits that knock it down (the rules count them: Rules::onDrone).
inline constexpr int kDroneArmour = 5;

// --- Concept A, Rev A.2: the centre devices --------------------------------------------
// Altitude (the game's progression, FL100 to FL500) is 0 to 4: the rules
// own it; the devices below read it. Every one of them sits off the eight
// shot lines and, idle, does nothing to the ball.
// Air Pocket: a magnet under the insert field. It pulses: once every
// progression::Knobs::airPocketPeriod seconds it is on for kAirPocketOnSeconds, and
// any free ball whose centre is within kAirPocketRadius and which is not
// rising fast (a ball just flipped is never robbed) is drawn to its centre
// and held; at the end of the pulse each held ball is let go with a kick of
// kAirPocketKickSpeed up the table to the left or right (never down at the
// flippers), the side and angle drawn from the step count. Off at FL100 and
// FL200 (period 0). Its clock is the game's step count. It also rests for
// kAirPocketFlipQuietSeconds after any flipper button is pressed (Rev A.2:
// "never within 1.5 s of a flipper hit"): no pull and no kick in that time.
inline constexpr Vec2 kAirPocketCenter{355.5F, 652.0F};
inline constexpr float kAirPocketRadius = 26.0F;
inline constexpr double kAirPocketOnSeconds = 0.35;
inline constexpr float kAirPocketPull = 9.0F;  // Per second: velocity = pull x offset.
inline constexpr float kAirPocketRisingLimit = -250.0F;  // px/s; faster upward is not taken.
inline constexpr float kAirPocketKickSpeed = 420.0F;
inline constexpr double kAirPocketFlipQuietSeconds = 1.5;

// Prop Wash: a flat disc turning in the field. A ball rolling on it is
// dragged towards the disc's own surface speed, at most kPropWashGrip px/s
// per second (friction, not a kick: a fast shot across it bends a little, a
// slow ball is spun off). Still at FL100; it turns faster with altitude.
inline constexpr Circle kPropWash{{355.5F, 712.0F}, 38.0F};
inline constexpr float kPropWashGrip = 700.0F;
// Its spin (radians per second, clockwise on screen) is the Altitude's:
// progression::Knobs::propWashSpin.

// Pylon: a pop-up target in the centre of the insert field. Flush (not in
// play) unless the rules raise it (sorties and multiballs); it rises only
// when no free ball is over it. Dead material: a hit drops the ball.
inline constexpr Circle kPylon{{355.5F, 548.0F}, 10.0F};

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
  Drone,        // Revision 3: the training drone.
  Standup,      // Revision 3: T-A-X-I (0 to 3).
  CaptiveBall,  // Revision 3: the captive ball (0) struck, its switch (1) closed.
  Pylon,        // Concept A: the pop-up pylon.
  Bogey,        // Concept A G2: the BOGEY drop targets (0 to 2).
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
    case Drone:
    case Pylon:
      return 1;
    case Standup:
      return kStandups.size();
    case Bogey:
      return kBogeys.size();
    case CaptiveBall:
      return 2;
    case None:
      break;
  }
  return 0;
}
[[nodiscard]] constexpr bool isValid(ElementRef ref) noexcept {
  return ref.index < elementCount(ref.kind);
}

}  // namespace pinball::table
