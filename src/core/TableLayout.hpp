#pragma once

#include <array>
#include <cstddef>
#include <vector>

#include "core/Vec2.hpp"

// Geometry of the table in playfield pixels (768 x 960, x right, y down).
// This is the single source of truth for physics, debug drawing and art.
namespace pinball::table {

inline constexpr float kWidth = 768.0F;
inline constexpr float kHeight = 960.0F;

inline constexpr float kBallRadius = 12.0F;

// Outer shell: straight side walls joined by an elliptical arch over the top.
inline constexpr float kWallLeft = 40.0F;
inline constexpr float kWallRight = 728.0F;
inline constexpr float kArchCenterX = (kWallLeft + kWallRight) * 0.5F;
inline constexpr float kArchCenterY = 300.0F;
inline constexpr float kArchRadiusX = (kWallRight - kWallLeft) * 0.5F;
inline constexpr float kArchRadiusY = 260.0F;
// The shell continues below the visible table; a ball past kDrainY is lost.
inline constexpr float kFloorY = 1010.0F;
inline constexpr float kDrainY = kHeight + kBallRadius;

// Launch lane along the right wall.
inline constexpr float kLaneWallX = 676.0F;
inline constexpr float kLaneWallTopY = 290.0F;
inline constexpr float kLaneFloorY = 925.0F;
inline constexpr Vec2 kBallStart{(kLaneWallX + kWallRight) * 0.5F + 2.5F, 880.0F};

// Half thickness of interior walls and guides.
inline constexpr float kWallRadius = 5.0F;

// The main playfield is symmetric about this x: midway between the left wall
// and the near face of the launch lane wall.
inline constexpr float kFieldCenterX = (kWallLeft + kLaneWallX - kWallRadius) * 0.5F;

// A thick line with round ends. Used for interior walls and guides.
struct Capsule {
  Vec2 a;
  Vec2 b;
  float radius;
};

struct Triangle {
  std::array<Vec2, 3> points;
  float cornerRadius;
};

constexpr Vec2 mirrored(Vec2 p) noexcept { return {2.0F * kFieldCenterX - p.x, p.y}; }
constexpr Capsule mirrored(const Capsule& c) noexcept {
  return {mirrored(c.a), mirrored(c.b), c.radius};
}

inline constexpr Capsule kLeftGuideUpper{{90.0F, 640.0F}, {90.0F, 795.0F}, kWallRadius};
inline constexpr Capsule kLeftGuideLower{{90.0F, 795.0F}, {236.0F, 842.0F}, kWallRadius};

// Static interior walls.
inline constexpr std::array<Capsule, 6> kWalls{{
    {{kLaneWallX, kLaneWallTopY}, {kLaneWallX, kFloorY}, kWallRadius},
    {{kLaneWallX, kLaneFloorY}, {kWallRight, kLaneFloorY}, kWallRadius},
    kLeftGuideUpper,
    kLeftGuideLower,
    mirrored(kLeftGuideUpper),
    mirrored(kLeftGuideLower),
}};

inline constexpr Triangle kLeftSlingshot{
    {{{140.0F, 655.0F}, {140.0F, 770.0F}, {222.0F, 800.0F}}}, 3.0F};
inline constexpr Triangle kRightSlingshot{
    {{mirrored(kLeftSlingshot.points[0]), mirrored(kLeftSlingshot.points[1]),
      mirrored(kLeftSlingshot.points[2])}},
    kLeftSlingshot.cornerRadius};
inline constexpr std::array<Triangle, 2> kSlingshots{{kLeftSlingshot, kRightSlingshot}};

// Flippers. Angles are in radians, measured from +x towards +y (clockwise on
// screen, because y points down). The flipper extends from its pivot along
// the direction of its angle.
struct Flipper {
  Vec2 pivot;
  float restAngle;
  float activeAngle;
};

inline constexpr float kPi = 3.14159265358979323846F;
inline constexpr float kFlipperLength = 98.0F;      // Pivot to tip centre.
inline constexpr float kFlipperBaseRadius = 11.0F;
inline constexpr float kFlipperTipRadius = 6.0F;
inline constexpr float kFlipperSwing = kPi / 6.0F;  // 30 degrees either side of level.

inline constexpr Flipper kLeftFlipper{{244.0F, 850.0F}, kFlipperSwing, -kFlipperSwing};
inline constexpr Flipper kRightFlipper{mirrored(kLeftFlipper.pivot), kPi - kFlipperSwing,
                                       kPi + kFlipperSwing};
inline constexpr std::array<Flipper, 2> kFlippers{{kLeftFlipper, kRightFlipper}};
inline constexpr std::size_t kLeftFlipperIndex = 0;
inline constexpr std::size_t kRightFlipperIndex = 1;

// Outer shell as a closed loop, ordered counter-clockwise on screen so the
// one-sided collision normals point into the table.
std::vector<Vec2> outerBoundary();

}  // namespace pinball::table
