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
    // 60 degrees at 13.5 rad/s is about 78 ms, or 10 steps.
    CHECK(stepsToTop >= 9 && stepsToTop <= 11);
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
    CHECK(stepsToRest >= stepsToTop);
    CHECK(stepsToRest <= 13);
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

  {  // A ball does not tunnel through a swinging flipper either. Sweep where
     // it lands, how fast it falls, and when the flipper fires. A ball that
     // tunnelled ends up underneath the bat, between its ends.
    int tunnelled = 0;
    int runs = 0;
    float fastest = 0.0F;
    for (const std::size_t index : {left, right}) {
      const table::Flipper& f = table::kFlippers[index];
      for (int along = 2; along <= 9; ++along) {
        for (int fall = 0; fall < 5; ++fall) {
          for (int delay = 0; delay < 12; ++delay) {
            ++runs;
            PhysicsWorld world;
            const float t = static_cast<float>(along) / 10.0F;
            const Vec2 onBat{f.pivot.x + std::cos(f.restAngle) * table::kFlipperLength * t,
                             f.pivot.y + std::sin(f.restAngle) * table::kFlipperLength * t};
            world.resetBall({onBat.x, onBat.y - 90.0F},
                            {0.0F, 200.0F + 500.0F * static_cast<float>(fall)});
            bool under = false;
            for (int step = 0; step < 50; ++step) {
              const bool fire = step >= delay && step < delay + 20;
              world.setFlippers(fire && index == left, fire && index == right);
              world.step(kStep);

              const Vec2 v = world.ballVelocity();
              fastest = std::max(fastest, std::sqrt(v.x * v.x + v.y * v.y));

              // Ball position in the flipper's current frame.
              const float angle = world.flipperAngle(index);
              const Vec2 rel = world.ballPosition() - f.pivot;
              const float alongBat = rel.x * std::cos(angle) + rel.y * std::sin(angle);
              const float across = -rel.x * std::sin(angle) + rel.y * std::cos(angle);
              // "Below" is +across for the left flipper and -across for the
              // right, whose frame is rotated half a turn.
              const float below = index == left ? across : -across;
              if (alongBat > table::kFlipperLength * 0.15F &&
                  alongBat < table::kFlipperLength * 0.85F && below > 4.0F && below < 22.0F) {
                under = true;
              }
            }
            tunnelled += under ? 1 : 0;
          }
        }
      }
    }
    CHECK(runs == 960);
    CHECK(tunnelled == 0);
    // The speed cap holds even for balls struck by a moving flipper.
    CHECK(fastest <= PhysicsWorld::kMaxBallSpeed * PhysicsWorld::kPixelsPerMeter * 1.001F);
  }

  {  // A ball rolling down the inlane feeds onto the resting flipper smoothly:
     // it stays close to the bat rather than hopping off the pivot. Swept
     // over start positions, so over arrival speeds.
    const table::Capsule& guide = table::kLeftDividerLower;
    const table::Flipper& f = table::kLeftFlipper;
    const Vec2 run = guide.b - guide.a;
    const float runLength = std::sqrt(run.x * run.x + run.y * run.y);
    const Vec2 up{run.y / runLength, -run.x / runLength};
    constexpr int kStarts = 44;
    float worstGap = 0.0F;
    int noticeable = 0;
    for (int start = 0; start < kStarts; ++start) {
      PhysicsWorld world;
      // Resting on the guide's sloped surface, not dropped onto it.
      const float t = 0.02F + 0.85F * static_cast<float>(start) / static_cast<float>(kStarts);
      world.resetBall(guide.a + run * t + up * (guide.radius + table::kBallRadius + 0.2F),
                      {0.0F, 0.0F});
      float gap = 0.0F;
      for (int step = 0; step < 300; ++step) {
        world.step(kStep);
        const Vec2 rel = world.ballPosition() - f.pivot;
        const float alongBat = rel.x * std::cos(f.restAngle) + rel.y * std::sin(f.restAngle);
        const float above = rel.x * std::sin(f.restAngle) - rel.y * std::cos(f.restAngle);
        if (alongBat > 5.0F && alongBat < table::kFlipperLength * 0.8F) {
          // Bat half-thickness tapers from base to tip.
          const float half = table::kFlipperBaseRadius +
                             (table::kFlipperTipRadius - table::kFlipperBaseRadius) * alongBat /
                                 table::kFlipperLength;
          gap = std::max(gap, above - half - table::kBallRadius);
        }
      }
      worstGap = std::max(worstGap, gap);
      noticeable += gap > 3.5F ? 1 : 0;
    }
    // A few pixels of float is invisible; a hop that clears the bat is not.
    CHECK(worstGap < 7.0F);
    CHECK(noticeable <= kStarts / 10);
  }
}
