#include <algorithm>
#include <cmath>

#include "Check.hpp"
#include "Suites.hpp"
#include "core/Physics.hpp"
#include "core/TableLayout.hpp"

namespace {

using pinball::PhysicsWorld;
using pinball::Vec2;
namespace table = pinball::table;

constexpr float kStep = 1.0F / 120.0F;
constexpr float kTolerance = 1e-4F;

bool withinLimits(float angle, const table::Flipper& f) {
  const float lo = std::min(f.restAngle, f.activeAngle) - kTolerance;
  const float hi = std::max(f.restAngle, f.activeAngle) + kTolerance;
  return angle >= lo && angle <= hi;
}

}  // namespace

void testFlippers() {
  const std::size_t left = table::kLeftFlipperIndex;
  const std::size_t right = table::kRightFlipperIndex;

  {  // Flippers start at rest and the layout is mirrored.
    PhysicsWorld world;
    CHECK(std::fabs(world.flipperAngle(left) - table::kLeftFlipper.restAngle) < kTolerance);
    CHECK(std::fabs(world.flipperAngle(right) - table::kRightFlipper.restAngle) < kTolerance);
    CHECK(std::fabs((table::kLeftFlipper.pivot.x + table::kRightFlipper.pivot.x) * 0.5F -
                    table::kFieldCenterX) < 0.01F);
    CHECK(world.flipperAngle(99) == 0.0F);
  }

  {  // Holding raises a flipper to its limit quickly, without overshoot, and
     // each button moves only its own flipper.
    PhysicsWorld world;
    world.resetBall({table::kFieldCenterX, 200.0F}, {0.0F, 0.0F});
    world.setFlippers(true, false);
    int stepsToTop = 0;
    for (int i = 0; i < 60; ++i) {
      world.step(kStep);
      CHECK(withinLimits(world.flipperAngle(left), table::kLeftFlipper));
      if (stepsToTop == 0 &&
          std::fabs(world.flipperAngle(left) - table::kLeftFlipper.activeAngle) < kTolerance) {
        stepsToTop = i + 1;
      }
    }
    // 60 degrees at 22 rad/s is about 48 ms, or 6 steps.
    CHECK(stepsToTop >= 5 && stepsToTop <= 7);
    CHECK(std::fabs(world.flipperAngle(right) - table::kRightFlipper.restAngle) < kTolerance);

    // Releasing lets it fall back, more slowly, and stop exactly at rest.
    world.setFlippers(false, false);
    int stepsToRest = 0;
    for (int i = 0; i < 60; ++i) {
      world.step(kStep);
      CHECK(withinLimits(world.flipperAngle(left), table::kLeftFlipper));
      if (stepsToRest == 0 &&
          std::fabs(world.flipperAngle(left) - table::kLeftFlipper.restAngle) < kTolerance) {
        stepsToRest = i + 1;
      }
    }
    CHECK(stepsToRest > stepsToTop);
    CHECK(stepsToRest <= 12);
  }

  {  // Rapid alternation never leaves the limits.
    PhysicsWorld world;
    world.resetBall({table::kFieldCenterX, 200.0F}, {0.0F, 0.0F});
    for (int i = 0; i < 2000; ++i) {
      world.setFlippers((i / 3) % 2 == 0, (i / 5) % 2 == 0);
      world.step(kStep);
      CHECK(withinLimits(world.flipperAngle(left), table::kLeftFlipper));
      CHECK(withinLimits(world.flipperAngle(right), table::kRightFlipper));
    }
  }

  // A ball falling onto the middle of each flipper is thrown back up the
  // table when the flipper fires as it lands.
  for (const std::size_t index : {left, right}) {
    const table::Flipper& f = table::kFlippers[index];
    PhysicsWorld world;
    const Vec2 mid{f.pivot.x + std::cos(f.restAngle) * table::kFlipperLength * 0.6F,
                   f.pivot.y + std::sin(f.restAngle) * table::kFlipperLength * 0.6F};
    world.resetBall({mid.x, mid.y - 120.0F}, {0.0F, 300.0F});
    float fastestUp = 0.0F;
    float highest = mid.y;
    bool fired = false;
    for (int i = 0; i < 240; ++i) {
      if (!fired && world.ballPosition().y > mid.y - 40.0F) {
        fired = true;
      }
      world.setFlippers(fired && index == left, fired && index == right);
      world.step(kStep);
      fastestUp = std::min(fastestUp, world.ballVelocity().y);
      highest = std::min(highest, world.ballPosition().y);
    }
    CHECK(fired);
    CHECK(fastestUp < -600.0F);
    CHECK(highest < f.pivot.y - 300.0F);
  }

  {  // A fast ball does not tunnel through a resting flipper.
    const table::Flipper& f = table::kLeftFlipper;
    const Vec2 mid{f.pivot.x + std::cos(f.restAngle) * table::kFlipperLength * 0.5F,
                   f.pivot.y + std::sin(f.restAngle) * table::kFlipperLength * 0.5F};
    PhysicsWorld world;
    world.resetBall({mid.x, mid.y - 200.0F}, {0.0F, 3900.0F});
    bool passedThrough = false;
    for (int i = 0; i < 12; ++i) {
      world.step(kStep);
      const Vec2 p = world.ballPosition();
      if (std::fabs(p.x - mid.x) < 10.0F && p.y > mid.y + table::kBallRadius) {
        passedThrough = true;
      }
    }
    CHECK(!passedThrough);
  }
}
