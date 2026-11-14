#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>

#include "Check.hpp"
#include "Suites.hpp"
#include "core/Game.hpp"
#include "core/Physics.hpp"
#include "core/TableLayout.hpp"

namespace {

using pinball::PhysicsEvent;
using pinball::PhysicsWorld;
using pinball::Vec2;
namespace table = pinball::table;

constexpr float kStep = 1.0F / 120.0F;

// Small deterministic generator so the test needs no global random state.
struct Lcg {
  std::uint32_t state;
  float unit() {
    state = state * 1664525U + 1013904223U;
    return static_cast<float>(state >> 8U) / static_cast<float>(1U << 24U);
  }
};

// True while the ball centre is inside the shell. A ball that tunnelled
// through a wall has its centre outside, and keeps going.
bool insideShell(Vec2 p) {
  if (p.x < table::kWallLeft || p.x > table::kWallRight || p.y > table::kFloorY || p.y < 40.0F) {
    return false;
  }
  // Left of the arch's top the shell is the arch itself; right of it the
  // launch curve bulges out to the lane's wall.
  if (p.y < table::kArchCenter.y && p.x <= table::kArchCenter.x) {
    const float nx = (p.x - table::kArchCenter.x) / table::kArchRadiusX;
    const float ny = (p.y - table::kArchCenter.y) / table::kArchRadiusY;
    if (nx * nx + ny * ny > 1.0F) {
      return false;
    }
  }
  // Beyond the left circuit wall, above the outlane pocket, is outside.
  constexpr float kLeftCircuitWall = 88.0F;
  constexpr float kPocketTop = 556.0F;
  return !(p.x < kLeftCircuitWall && p.y < kPocketTop);
}

float length(Vec2 v) { return std::sqrt(v.x * v.x + v.y * v.y); }

constexpr float kSpeedCap = PhysicsWorld::kMaxBallSpeed * PhysicsWorld::kPixelsPerMeter;
// An open stretch of the middle of the field: nothing to hit for 110 px
// either side or 30 px above and below.
constexpr Vec2 kOpenField{table::kFieldCenterX, 510.0F};

// How far two balls' discs overlap, in pixels. Negative when apart.
float overlap(const PhysicsWorld& world, std::size_t a, std::size_t b) {
  return 2.0F * table::kBallRadius - length(world.ballPosition(a) - world.ballPosition(b));
}

bool ballIsFiniteAndCapped(const PhysicsWorld& world, std::size_t ball) {
  const Vec2 p = world.ballPosition(ball);
  const Vec2 v = world.ballVelocity(ball);
  return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(v.x) && std::isfinite(v.y) &&
         length(v) <= kSpeedCap * 1.001F;
}

std::size_t countEvents(const PhysicsWorld& world, PhysicsEvent::Type type, std::size_t ball) {
  std::size_t count = 0;
  for (const PhysicsEvent& event : world.events()) {
    count += event.type == type && event.ball == ball ? 1U : 0U;
  }
  return count;
}

// The world keeps table::kMaxBalls ball slots. One is in use in a normal
// game; these tests drive the others directly.
void testSeveralBalls() {
  static_assert(table::kMaxBalls == 3);

  {  // Only the first slot is in use to begin with. The others take no part:
     // they report nothing and every call on them is ignored.
    PhysicsWorld world;
    CHECK(world.activeBallCount() == 1);
    CHECK(world.ballActive(0) && !world.ballActive(1) && !world.ballActive(2));
    world.resetBall(1, {300.0F, 300.0F}, {50.0F, 0.0F});
    world.pushBall(2, {100.0F, 0.0F});
    world.releaseBall(1, {300.0F, 300.0F}, {0.0F, 0.0F});
    world.deactivateBall(2);
    world.resetBall(0, kOpenField, {0.0F, 0.0F});
    bool onlyTheFirst = true;
    for (int i = 0; i < 600; ++i) {
      world.step(kStep);
      for (const PhysicsEvent& event : world.events()) {
        onlyTheFirst = onlyTheFirst && event.ball == 0 && event.type != PhysicsEvent::Type::BallHit;
      }
    }
    CHECK(onlyTheFirst);
    for (const std::size_t slot : {std::size_t{1}, std::size_t{2}}) {
      CHECK(!world.ballActive(slot) && !world.ballHeld(slot));
      CHECK(world.ballPosition(slot).x == 0.0F && world.ballPosition(slot).y == 0.0F);
      CHECK(world.ballVelocity(slot).x == 0.0F && world.ballVelocity(slot).y == 0.0F);
      CHECK(world.ballAngle(slot) == 0.0F);
      CHECK(world.ballIsSane(slot));
    }
    CHECK(world.activeBallCount() == 1);
    // The forms without a slot are the first slot.
    CHECK(world.ballPosition().x == world.ballPosition(0).x);
    CHECK(world.ballPosition().y == world.ballPosition(0).y);
    CHECK(world.ballVelocity().y == world.ballVelocity(0).y);
    CHECK(world.ballAngle() == world.ballAngle(0));
    CHECK(world.ballHeld() == world.ballHeld(0) && world.heldHole() == world.heldHole(0));
    CHECK(world.ballIsSane() == world.ballIsSane(0));
  }

  {  // A slot that does not exist is ignored everywhere.
    PhysicsWorld world;
    for (const std::size_t slot : {table::kMaxBalls, std::numeric_limits<std::size_t>::max()}) {
      CHECK(!world.activateBall(slot, kOpenField, {0.0F, 0.0F}));
      world.deactivateBall(slot);
      world.resetBall(slot, kOpenField, {0.0F, 0.0F});
      world.pushBall(slot, {10.0F, 0.0F});
      world.releaseBall(slot, kOpenField, {0.0F, 0.0F});
      CHECK(!world.ballActive(slot) && !world.ballHeld(slot));
      CHECK(world.heldHole(slot) == 0);
      CHECK(world.ballPosition(slot).x == 0.0F && world.ballVelocity(slot).x == 0.0F);
      CHECK(world.ballAngle(slot) == 0.0F);
      CHECK(world.ballIsSane(slot));
    }
    world.step(kStep);
    CHECK(world.activeBallCount() == 1 && world.ballIsSane());
  }

  {  // Activating and deactivating, round and round.
    PhysicsWorld world;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    CHECK(!world.activateBall(1, {nan, 300.0F}, {0.0F, 0.0F}));
    CHECK(!world.activateBall(1, {300.0F, 300.0F}, {inf, 0.0F}));
    CHECK(!world.ballActive(1));
    CHECK(!world.activateBall(0, kOpenField, {0.0F, 0.0F}));  // Already active.

    CHECK(world.activateBall(1, kOpenField, {120.0F, -60.0F}));
    CHECK(world.ballActive(1) && world.activeBallCount() == 2);
    CHECK(world.ballPosition(1).x == kOpenField.x && world.ballPosition(1).y == kOpenField.y);
    CHECK(std::fabs(world.ballVelocity(1).x - 120.0F) < 0.01F);
    CHECK(!world.activateBall(1, {200.0F, 500.0F}, {0.0F, 0.0F}));
    CHECK(world.ballPosition(1).x == kOpenField.x);
    // The velocity is held to the cap from the start.
    CHECK(world.activateBall(2, {300.0F, 620.0F}, {3.0e38F, 0.0F}));
    CHECK(length(world.ballVelocity(2)) <= kSpeedCap * 1.001F);
    const float before = world.ballPosition(1).y;
    for (int i = 0; i < 5; ++i) {
      world.step(kStep);
    }
    CHECK(world.ballPosition(1).y != before);  // It moves: it is in the world.

    world.deactivateBall(1);
    CHECK(!world.ballActive(1) && world.activeBallCount() == 2);
    world.deactivateBall(1);
    for (int i = 0; i < 5; ++i) {
      world.step(kStep);
    }
    CHECK(world.activateBall(1, kOpenField, {0.0F, 0.0F}));
    CHECK(world.ballPosition(1).x == kOpenField.x && world.ballPosition(1).y == kOpenField.y);
    CHECK(world.ballVelocity(1).x == 0.0F && world.ballVelocity(1).y == 0.0F);

    // The first slot can be emptied and filled like any other.
    world.deactivateBall(0);
    CHECK(!world.ballActive(0) && world.activeBallCount() == 2);
    world.step(kStep);
    CHECK(world.activateBall(0, table::kBallStart, {0.0F, 0.0F}));
    CHECK(world.activeBallCount() == 3);

    // A held ball can be deactivated, and comes back free and unheld.
    world.resetBall(2, table::kHoles[table::kScoopHole].center, {0.0F, 0.0F});
    world.step(kStep);
    CHECK(world.ballHeld(2) && world.heldHole(2) == table::kScoopHole);
    world.deactivateBall(2);
    CHECK(!world.ballHeld(2) && !world.ballActive(2));
    CHECK(world.activateBall(2, kOpenField + Vec2{60.0F, 0.0F}, {0.0F, 0.0F}));
    CHECK(!world.ballHeld(2));
    for (int i = 0; i < 120; ++i) {
      world.step(kStep);
    }
    CHECK(world.ballIsSane(0) && world.ballIsSane(1) && world.ballIsSane(2));
  }

  {  // Head on at twice the top speed, from 200 different starts. Box2D
     // sweeps a fast ball against the table but not against another ball, so
     // this is what keeps a pair from passing through each other: the most
     // they close in one slice is about half a ball.
    float worstOverlap = 0.0F;
    int worstOverlapSteps = 0;
    bool neverPassed = true;
    bool allSane = true;
    int touches = 0;
    int bounced = 0;
    for (int k = 0; k < 200; ++k) {
      PhysicsWorld world;
      const float gap = 80.0F + 0.31F * static_cast<float>(k);
      const float aside = 1.5F * static_cast<float>(k % 5 - 2);
      world.resetBall(0, kOpenField + Vec2{-gap * 0.5F, 0.0F}, {3000.0F, 0.0F});
      CHECK(world.activateBall(1, kOpenField + Vec2{gap * 0.5F, aside}, {-3000.0F, 0.0F}));
      int hitStep = -1;
      int overlapSteps = 0;
      for (int i = 0; i < 12 && (hitStep < 0 || i < hitStep + 3); ++i) {
        world.step(kStep);
        for (const PhysicsEvent& event : world.events()) {
          if (event.type == PhysicsEvent::Type::BallHit) {
            ++touches;
            hitStep = hitStep < 0 ? i : hitStep;
            CHECK(event.ball == 0 && event.otherBall == 1);
            CHECK(event.element.kind == table::ElementKind::None);
            CHECK(std::isfinite(event.speed) && event.speed > 0.0F);
          } else {
            CHECK(false);  // Nothing else is in reach.
          }
        }
        neverPassed = neverPassed && world.ballPosition(0).x < world.ballPosition(1).x;
        allSane = allSane && ballIsFiniteAndCapped(world, 0) && ballIsFiniteAndCapped(world, 1);
        const float deep = overlap(world, 0, 1);
        worstOverlap = std::max(worstOverlap, deep);
        overlapSteps = deep > 2.0F ? overlapSteps + 1 : 0;
        worstOverlapSteps = std::max(worstOverlapSteps, overlapSteps);
      }
      bounced += world.ballVelocity(0).x < 0.0F && world.ballVelocity(1).x > 0.0F ? 1 : 0;
    }
    std::fprintf(stderr,
                 "ball on ball head on: deepest overlap %.2f px, longest over 2 px %d steps\n",
                 static_cast<double>(worstOverlap), worstOverlapSteps);
    CHECK(neverPassed);
    CHECK(allSane);
    CHECK(touches == 200);  // One event for each touch.
    CHECK(bounced == 200);
    // Seen at the end of a step, so after the solver has had its say.
    CHECK(worstOverlap <= 2.0F);
  }

  {  // Two balls are livelier against each other than against the table.
    PhysicsWorld world;
    world.resetBall(0, kOpenField + Vec2{-40.0F, 0.0F}, {1000.0F, 0.0F});
    CHECK(world.activateBall(1, kOpenField + Vec2{40.0F, 0.0F}, {-1000.0F, 0.0F}));
    bool touched = false;
    for (int i = 0; i < 10 && !touched; ++i) {
      world.step(kStep);
      touched = countEvents(world, PhysicsEvent::Type::BallHit, 0) > 0;
    }
    CHECK(touched);
    world.step(kStep);
    const float parting = world.ballVelocity(1).x - world.ballVelocity(0).x;
    CHECK(parting > 2000.0F * (PhysicsWorld::kBallBallRestitution - 0.1F));
    CHECK(parting < 2000.0F * (PhysicsWorld::kBallBallRestitution + 0.05F));
  }

  {  // A glancing hit keeps the pair's momentum (the balls weigh the same,
     // so that is the sum of their velocities), give or take 5 %.
    PhysicsWorld world;
    world.resetBall(0, kOpenField + Vec2{-60.0F, 0.0F}, {1500.0F, 0.0F});
    CHECK(world.activateBall(1, kOpenField + Vec2{0.0F, 14.0F}, {0.0F, 0.0F}));
    Vec2 before{};
    bool touched = false;
    int steps = 0;
    for (; steps < 12 && !touched; ++steps) {
      before = world.ballVelocity(0) + world.ballVelocity(1);
      world.step(kStep);
      touched = countEvents(world, PhysicsEvent::Type::BallHit, 0) > 0;
    }
    CHECK(touched);
    world.step(kStep);
    const Vec2 after = world.ballVelocity(0) + world.ballVelocity(1);
    // Gravity adds to both balls over the two steps in between.
    const float fall = 2.0F * 2.0F * PhysicsWorld::kGravity * PhysicsWorld::kPixelsPerMeter * kStep;
    CHECK(length(after - before - Vec2{0.0F, fall}) < 0.05F * 1500.0F);
    // And it was a real deflection: the struck ball moves, both ways.
    CHECK(world.ballVelocity(1).x > 300.0F && world.ballVelocity(1).y > 100.0F);
    CHECK(world.ballVelocity(0).y < -100.0F);
  }

  {  // The speed cap holds for every ball, also through a pile-up.
    PhysicsWorld world;
    world.resetBall(0, kOpenField + Vec2{-70.0F, 0.0F}, {0.0F, 0.0F});
    CHECK(world.activateBall(1, kOpenField, {0.0F, 0.0F}));
    CHECK(world.activateBall(2, kOpenField + Vec2{70.0F, 0.0F}, {0.0F, 0.0F}));
    world.pushBall(0, {3.0e38F, 0.0F});
    world.pushBall(1, {0.0F, -3.0e38F});
    world.pushBall(2, {-3.0e38F, 3.0e38F});
    bool capped = true;
    for (std::size_t ball = 0; ball < table::kMaxBalls; ++ball) {
      capped =
          capped && std::fabs(length(world.ballVelocity(ball)) - kSpeedCap) < kSpeedCap * 0.01F;
    }
    CHECK(capped);
    for (int i = 0; i < 600; ++i) {
      world.step(kStep);
      for (std::size_t ball = 0; ball < table::kMaxBalls; ++ball) {
        capped = capped && (!world.ballActive(ball) || ballIsFiniteAndCapped(world, ball));
      }
    }
    CHECK(capped);
  }

  {  // Each ball has its own bumper latch: two balls on one bumper at the
     // same moment are both kicked, once each.
    PhysicsWorld world;
    constexpr std::size_t kThruster = 1;  // The right-hand thruster, clear on both sides.
    const table::Circle& bumper = table::kBumpers[kThruster].shape;
    const float reach = bumper.radius + table::kBallRadius + 0.5F;
    world.resetBall(0, bumper.center + Vec2{-reach, 0.0F}, {0.0F, 0.0F});
    CHECK(world.activateBall(1, bumper.center + Vec2{reach, 0.0F}, {0.0F, 0.0F}));
    std::array<int, 2> kicks{};
    for (int i = 0; i < 4; ++i) {
      world.step(kStep);
      for (const PhysicsEvent& event : world.events()) {
        if (event.type == PhysicsEvent::Type::Hit &&
            event.element.kind == table::ElementKind::Bumper && event.element.index == kThruster &&
            event.ball < kicks.size()) {
          ++kicks[event.ball];
        }
      }
    }
    CHECK(kicks[0] == 1 && kicks[1] == 1);
    CHECK(world.ballVelocity(0).x < -100.0F && world.ballVelocity(1).x > 100.0F);
  }

  {  // And its own rollover latch: two balls in one lane count twice.
    PhysicsWorld world;
    const Vec2 lane = table::kRollovers[1].zone.center;
    world.resetBall(0, lane + Vec2{-12.5F, 0.0F}, {0.0F, 0.0F});
    CHECK(world.activateBall(1, lane + Vec2{12.5F, 0.0F}, {0.0F, 0.0F}));
    world.step(kStep);
    CHECK(countEvents(world, PhysicsEvent::Type::Enter, 0) == 1);
    CHECK(countEvents(world, PhysicsEvent::Type::Enter, 1) == 1);
    world.step(kStep);
    CHECK(countEvents(world, PhysicsEvent::Type::Enter, 0) == 0);
    CHECK(countEvents(world, PhysicsEvent::Type::Enter, 1) == 0);
  }

  {  // A hole that has just returned one ball ignores that ball until it is
     // clear, and takes any other at once.
    PhysicsWorld world;
    constexpr std::size_t kChute = table::kScoopHole;
    const table::Hole& hole = table::kHoles[kChute];
    world.resetBall(0, hole.center, {0.0F, 0.0F});
    world.step(kStep);
    CHECK(world.ballHeld(0) && world.heldHole(0) == kChute);
    CHECK(countEvents(world, PhysicsEvent::Type::Capture, 0) == 1);
    // Let go inside the box, moving out of it.
    world.releaseBall(0, hole.center, {0.0F, 300.0F});
    // The first ball is on its way out: still inside the box, and inside
    // the distance at which the hole rearms for it.
    bool firstTakenAgain = false;
    for (int i = 0; i < 30 && length(world.ballPosition(0) - hole.center) < 26.0F; ++i) {
      world.step(kStep);
      firstTakenAgain = firstTakenAgain || world.ballHeld(0);
    }
    CHECK(length(world.ballPosition(0) - hole.center) < table::kHoleRearmDistance);
    CHECK(world.activateBall(1, hole.center + Vec2{0.0F, -14.0F}, {0.0F, 0.0F}));
    world.step(kStep);
    CHECK(world.ballHeld(1) && world.heldHole(1) == kChute);
    CHECK(countEvents(world, PhysicsEvent::Type::Capture, 1) == 1);
    CHECK(!firstTakenAgain && !world.ballHeld(0));
    // Both can be held at once, by the same hole or by different ones.
    world.resetBall(0, table::kHoles[table::kHangarHole].center, {0.0F, 0.0F});
    world.step(kStep);
    CHECK(world.ballHeld(0) && world.heldHole(0) == table::kHangarHole && world.ballHeld(1));
    // Releasing one leaves the other where it is.
    world.releaseBall(1, kOpenField, {0.0F, 0.0F});
    CHECK(!world.ballHeld(1) && world.ballHeld(0));
    world.pushBall(0, {500.0F, 0.0F});  // Held: ignored.
    world.step(kStep);
    CHECK(world.ballHeld(0) && world.ballVelocity(0).x == 0.0F);
  }

  {  // The inner arch is one-sided: a ball coming up the launch channel
     // behind it passes out onto the field, whichever slot it is in and
     // whatever else is on the table, and no ball gets back in from the
     // field. There is no gate to open or shut.
    for (const std::size_t slot : {std::size_t{0}, std::size_t{2}}) {
      PhysicsWorld world;
      world.resetBall(0, kOpenField, {0.0F, 0.0F});
      if (slot != 0) {
        CHECK(world.activateBall(slot, table::kBallStart, {0.0F, 0.0F}));
      }
      world.resetBall(slot, table::kBallStart, {0.0F, -1500.0F});
      bool out = false;
      for (int i = 0; i < 240 && !out; ++i) {
        world.step(kStep);
        out = table::inPlayArea(world.ballPosition(slot));
      }
      CHECK(out);
      // Straight back at the arch where it came through.
      world.resetBall(slot, {540.0F, 160.0F}, {900.0F, -300.0F});
      bool inLane = false;
      for (int i = 0; i < 90; ++i) {
        world.step(kStep);
        const Vec2 p = world.ballPosition(slot);
        inLane =
            inLane || (p.x > table::kLaneWallX + table::kBallRadius && p.y > table::kLaneWallTopY);
      }
      CHECK(!inLane);
    }
  }

  {  // Three balls piled on a raised flipper for ten seconds stay sane.
    PhysicsWorld world;
    world.setFlippers(true, false);
    const Vec2 pivot = table::kLeftFlipper.pivot;
    world.resetBall(0, pivot + Vec2{30.0F, -70.0F}, {0.0F, 0.0F});
    CHECK(world.activateBall(1, pivot + Vec2{55.0F, -100.0F}, {0.0F, 0.0F}));
    CHECK(world.activateBall(2, pivot + Vec2{20.0F, -130.0F}, {0.0F, 0.0F}));
    bool sane = true;
    float deepest = 0.0F;
    for (int i = 0; i < 120 * 10; ++i) {
      world.step(kStep);
      for (std::size_t ball = 0; ball < table::kMaxBalls; ++ball) {
        sane = sane && world.ballIsSane(ball) && ballIsFiniteAndCapped(world, ball);
      }
      deepest =
          std::max({deepest, overlap(world, 0, 1), overlap(world, 0, 2), overlap(world, 1, 2)});
    }
    CHECK(sane);
    CHECK(deepest <= 2.0F);
  }

  {  // The event list has room for a full step of every ball from the start,
     // so no step allocates, and each ball has its own limit.
    PhysicsWorld world;
    CHECK(world.events().capacity() >=
          PhysicsWorld::kMaxEventsPerStep * table::kMaxBalls + PhysicsWorld::kMaxBallHitsPerStep);
    const std::size_t capacity = world.events().capacity();
    world.resetBall(0, {300.0F, 300.0F}, {400.0F, -200.0F});
    CHECK(world.activateBall(1, {420.0F, 420.0F}, {-300.0F, 100.0F}));
    CHECK(world.activateBall(2, {250.0F, 500.0F}, {100.0F, -700.0F}));
    bool withinLimit = true;
    std::size_t events = 0;
    for (int i = 0; i < 1200; ++i) {
      world.setFlippers((i / 31) % 2 == 0, (i / 43) % 2 == 0);
      world.step(kStep);
      events += world.events().size();
      for (std::size_t ball = 0; ball < table::kMaxBalls; ++ball) {
        std::size_t count = 0;
        for (const PhysicsEvent& event : world.events()) {
          count += event.ball == ball ? 1U : 0U;
          withinLimit =
              withinLimit && event.ball < table::kMaxBalls && event.otherBall < table::kMaxBalls;
        }
        withinLimit = withinLimit &&
                      count <= PhysicsWorld::kMaxEventsPerStep + PhysicsWorld::kMaxBallHitsPerStep;
      }
    }
    CHECK(withinLimit);
    CHECK(events > 0);
    CHECK(world.events().capacity() == capacity);
  }

  {  // The limits of the event list. Each ball has its own allowance: one
     // that fills its own leaves the others theirs.
    using pinball::PhysicsEventList;
    const auto hit = [](std::size_t ball) {
      return PhysicsEvent{PhysicsEvent::Type::Hit,
                          {table::ElementKind::Bumper, 0},
                          100.0F,
                          static_cast<std::uint8_t>(ball),
                          0};
    };
    PhysicsEventList list;
    const std::size_t capacity = list.events().capacity();
    CHECK(capacity >=
          PhysicsEventList::kMaxPerBall * table::kMaxBalls + PhysicsEventList::kMaxBallHits);
    for (std::size_t i = 0; i < PhysicsEventList::kMaxPerBall; ++i) {
      CHECK(list.push(hit(0)));
    }
    CHECK(!list.push(hit(0)));
    CHECK(list.events().size() == PhysicsEventList::kMaxPerBall);
    for (const std::size_t ball : {std::size_t{1}, std::size_t{2}}) {
      for (std::size_t i = 0; i < PhysicsEventList::kMaxPerBall; ++i) {
        CHECK(list.push(hit(ball)));
      }
      CHECK(!list.push(hit(ball)));
    }
    CHECK(list.events().size() == PhysicsEventList::kMaxPerBall * table::kMaxBalls);

    // Touches between balls are counted apart, so they still fit, and a
    // flood of them does not use up what a ball needs for a capture.
    const PhysicsEvent touch{PhysicsEvent::Type::BallHit, {}, 50.0F, 0, 1};
    CHECK(list.push(touch));
    list.clear();
    CHECK(list.events().empty());
    for (std::size_t i = 0; i < PhysicsEventList::kMaxBallHits; ++i) {
      CHECK(list.push(touch));
    }
    CHECK(!list.push(touch));
    CHECK(!list.push({PhysicsEvent::Type::BallHit, {}, 50.0F, 1, 2}));  // One allowance for all.
    const PhysicsEvent capture{
        PhysicsEvent::Type::Capture, {table::ElementKind::Hole, 3}, 10.0F, 0, 0};
    CHECK(list.push(capture));
    CHECK(list.events().back().type == PhysicsEvent::Type::Capture);
    for (std::size_t i = 1; i < PhysicsEventList::kMaxPerBall; ++i) {
      CHECK(list.push(hit(0)));
    }
    CHECK(!list.push(capture));
    CHECK(list.events().size() == PhysicsEventList::kMaxBallHits + PhysicsEventList::kMaxPerBall);
    // In the order they came.
    CHECK(list.events().front().type == PhysicsEvent::Type::BallHit);
    // A slot that does not exist is not recorded.
    list.clear();
    CHECK(!list.push(hit(table::kMaxBalls)));
    CHECK(!list.push({PhysicsEvent::Type::BallHit, {}, 50.0F, 0, 200}));
    CHECK(list.events().empty());
    CHECK(list.events().capacity() == capacity);  // Never grew.
  }

  {  // A slot's latches belong to the ball in it. The next ball to use the
     // slot starts clean: on a rollover it counts, at a bumper it is kicked,
     // and a hole that had let the last ball go takes it.
    PhysicsWorld world;
    const Vec2 lane = table::kRollovers[1].zone.center;
    CHECK(world.activateBall(1, lane, {0.0F, 0.0F}));
    world.step(kStep);
    CHECK(countEvents(world, PhysicsEvent::Type::Enter, 1) == 1);
    world.deactivateBall(1);
    CHECK(world.activateBall(1, lane, {0.0F, 0.0F}));
    world.step(kStep);
    CHECK(countEvents(world, PhysicsEvent::Type::Enter, 1) == 1);
    world.deactivateBall(1);

    const table::Circle& bumper = table::kBumpers[1].shape;
    const Vec2 touching = bumper.center + Vec2{bumper.radius + table::kBallRadius + 0.5F, 0.0F};
    for (int round = 0; round < 2; ++round) {
      CHECK(world.activateBall(1, touching, {0.0F, 0.0F}));
      world.step(kStep);
      CHECK(countEvents(world, PhysicsEvent::Type::Hit, 1) == 1);
      world.deactivateBall(1);
    }

    const table::Hole& chute = table::kHoles[table::kScoopHole];
    CHECK(world.activateBall(2, chute.center, {0.0F, 0.0F}));
    world.step(kStep);
    CHECK(world.ballHeld(2));
    world.releaseBall(2, chute.center, {0.0F, 0.0F});  // The hole now ignores it.
    world.deactivateBall(2);
    CHECK(world.activateBall(2, chute.center, {0.0F, 0.0F}));
    world.step(kStep);
    CHECK(world.ballHeld(2) && countEvents(world, PhysicsEvent::Type::Capture, 2) == 1);
  }

  {  // The speed cap is enforced for every slot after each slice. A ball at
     // top speed that clips a bumper is kicked sideways on top of it. Box2D
     // takes the excess off in its next slice, so it shows only when the
     // kick comes in the last slice of a step: hence every start within one
     // step's travel is tried.
    for (std::size_t slot = 0; slot < table::kMaxBalls; ++slot) {
      const table::Circle& bumper = table::kBumpers[1].shape;
      int kicks = 0;
      bool capped = true;
      for (float aside = 34.0F; aside <= 36.5F; aside += 0.5F) {
        for (float back = 40.0F; back < 66.0F; back += 0.5F) {
          PhysicsWorld world;
          const Vec2 from = bumper.center + Vec2{-aside, back};
          if (slot == 0) {
            world.resetBall(0, from, {0.0F, -kSpeedCap});
          } else {
            CHECK(world.activateBall(slot, from, {0.0F, -kSpeedCap}));
          }
          for (int i = 0; i < 4; ++i) {
            world.step(kStep);
            kicks += static_cast<int>(countEvents(world, PhysicsEvent::Type::Hit, slot));
            capped = capped && ballIsFiniteAndCapped(world, slot);
          }
        }
      }
      CHECK(kicks >= 100);
      CHECK(capped);
    }
  }

  {  // A ball in a hole is off the table: a ball passing close by is not
     // closing on anything, and moves exactly as if the hole were empty.
     // A ramp's mouth is in the open, and a ball falling back through it
     // is not taken.
    const table::Hole& hole = table::kHoles[table::kSkywayHole];
    const Vec2 from = hole.center + Vec2{-8.0F, -110.0F};
    PhysicsWorld occupied;
    PhysicsWorld empty;
    occupied.resetBall(0, hole.center + Vec2{0.0F, 4.0F}, {0.0F, -900.0F});
    empty.deactivateBall(0);
    occupied.step(kStep);
    empty.step(kStep);
    CHECK(occupied.ballHeld(0));
    const Vec2 heldAt = occupied.ballPosition(0);
    CHECK(occupied.activateBall(1, from, {0.0F, 2500.0F}));
    CHECK(empty.activateBall(1, from, {0.0F, 2500.0F}));
    bool identical = true;
    float nearest = 1.0e9F;
    for (int i = 0; i < 8; ++i) {
      occupied.step(kStep);
      empty.step(kStep);
      const Vec2 a = occupied.ballPosition(1);
      const Vec2 b = empty.ballPosition(1);
      identical = identical && a.x == b.x && a.y == b.y && occupied.events().empty();
      nearest = std::min(nearest, length(a - heldAt));
    }
    CHECK(identical);
    CHECK(nearest < 2.0F * table::kBallRadius && !occupied.ballHeld(1));
    CHECK(occupied.ballPosition(1).y > hole.center.y);  // It went past.
  }

  {  // Three balls loose on the table: the same world twice gives the same
     // answer, bit for bit.
    PhysicsWorld a;
    PhysicsWorld b;
    for (PhysicsWorld* world : {&a, &b}) {
      world->resetBall(0, {300.0F, 520.0F}, {400.0F, -200.0F});
      CHECK(world->activateBall(1, {420.0F, 560.0F}, {-300.0F, 100.0F}));
      CHECK(world->activateBall(2, {250.0F, 600.0F}, {100.0F, -700.0F}));
    }
    bool identical = true;
    for (int i = 0; i < 120 * 20 && identical; ++i) {
      a.setFlippers((i / 31) % 2 == 0, (i / 43) % 2 == 0);
      b.setFlippers((i / 31) % 2 == 0, (i / 43) % 2 == 0);
      a.step(kStep);
      b.step(kStep);
      for (std::size_t ball = 0; ball < table::kMaxBalls; ++ball) {
        const Vec2 pa = a.ballPosition(ball);
        const Vec2 pb = b.ballPosition(ball);
        identical =
            identical && pa.x == pb.x && pa.y == pb.y && a.ballHeld(ball) == b.ballHeld(ball);
      }
    }
    CHECK(identical);
  }
}

}  // namespace

void testPhysics() {
  {  // A ball at rest on the plunger stays there.
    PhysicsWorld world;
    const Vec2 start = table::kBallStart;
    world.resetBall(start, {0.0F, 0.0F});
    for (int i = 0; i < 120 * 5; ++i) {
      world.step(kStep);
    }
    const Vec2 p = world.ballPosition();
    CHECK(std::fabs(p.x - start.x) < 2.0F);
    CHECK(std::fabs(p.y - start.y) < 3.0F);
    CHECK(world.ballIsSane());
  }

  {  // Gravity pulls down the screen (positive y).
    PhysicsWorld world;
    world.resetBall({table::kFieldCenterX, 600.0F}, {0.0F, 0.0F});
    for (int i = 0; i < 30; ++i) {
      world.step(kStep);
    }
    CHECK(world.ballPosition().y > 600.0F);
    CHECK(world.ballVelocity().y > 0.0F);
  }

  {  // No tunnelling: balls fired in every direction at the speed cap, and
     // above it, never leave the shell.
    Lcg rng{12345U};
    int escapes = 0;
    for (int shot = 0; shot < 300; ++shot) {
      PhysicsWorld world;
      const float angle = rng.unit() * 6.2831853F;
      const float speed = 1000.0F + rng.unit() * 9000.0F;  // Up to 100 m/s requested.
      // Anywhere on the field below the arch, clear of the circuit walls.
      const Vec2 origin{120.0F + rng.unit() * 470.0F, 310.0F + rng.unit() * 290.0F};
      world.resetBall(origin, {std::cos(angle) * speed, std::sin(angle) * speed});
      for (int i = 0; i < 240; ++i) {
        world.step(kStep);
        if (!insideShell(world.ballPosition()) || !world.ballIsSane()) {
          ++escapes;
          break;
        }
      }
    }
    CHECK(escapes == 0);
  }

  {  // The speed cap holds.
    PhysicsWorld world;
    world.resetBall({table::kFieldCenterX, 400.0F}, {90000.0F, -90000.0F});
    world.step(kStep);
    const Vec2 v = world.ballVelocity();
    const float cap = PhysicsWorld::kMaxBallSpeed * PhysicsWorld::kPixelsPerMeter;
    CHECK(std::sqrt(v.x * v.x + v.y * v.y) <= cap * 1.01F);
  }

  {  // Hostile input is ignored.
    PhysicsWorld world;
    const Vec2 before = world.ballPosition();
    world.resetBall({NAN, 100.0F}, {0.0F, 0.0F});
    world.resetBall({100.0F, 100.0F}, {INFINITY, 0.0F});
    world.step(NAN);
    world.step(-1.0F);
    world.step(0.0F);
    world.step(PhysicsWorld::kMaxStepSeconds * 1.01F);
    world.step(3.0e38F);
    // Too short to divide by: Box2D's 1/dt overflows for these.
    world.step(1.0e-38F);
    world.step(1.0e-45F);
    world.step(PhysicsWorld::kMinStepSeconds * 0.99F);
    const Vec2 after = world.ballPosition();
    CHECK(before.x == after.x && before.y == after.y);
    // The world is still usable afterwards.
    world.setFlippers(true, true);
    for (int i = 0; i < 30; ++i) {
      world.step(kStep);
    }
    CHECK(world.ballIsSane());
    CHECK(std::isfinite(world.flipperAngle(0)) && std::isfinite(world.flipperAngle(1)));
  }

  {  // A finite but enormous velocity is cut to the speed cap and keeps its
     // direction. Squaring it overflows, which once stopped the ball dead.
    const float cap = PhysicsWorld::kMaxBallSpeed * PhysicsWorld::kPixelsPerMeter;
    const float huge = 3.0e38F;
    PhysicsWorld world;
    world.resetBall({table::kFieldCenterX, 600.0F}, {0.0F, 0.0F});
    world.pushBall({huge, 0.0F});
    Vec2 v = world.ballVelocity();
    CHECK(std::fabs(v.x - cap) < cap * 0.01F && std::fabs(v.y) < 1.0F);

    world.resetBall({table::kFieldCenterX, 600.0F}, {-huge, huge});
    v = world.ballVelocity();
    CHECK(std::isfinite(v.x) && std::isfinite(v.y));
    CHECK(v.x < 0.0F && v.y > 0.0F);
    CHECK(std::fabs(std::sqrt(v.x * v.x + v.y * v.y) - cap) < cap * 0.01F);
    world.step(kStep);
    CHECK(world.ballIsSane());
  }

  {  // The simulation is deterministic: the same scripted input gives
     // bit-identical trajectories through a launch and a minute of flipping.
    pinball::Game a;
    pinball::Game b;
    bool identical = true;
    for (int i = 0; i < 120 * 60 && identical; ++i) {
      pinball::InputState input;
      input.plunger = (i % 2400) < 150;
      input.leftFlipper = (i / 37) % 3 == 0;
      input.rightFlipper = (i / 53) % 3 == 1;
      input.newGame = a.gameOver();
      a.step(input);
      b.step(input);
      const Vec2 pa = a.ball().position;
      const Vec2 pb = b.ball().position;
      identical = pa.x == pb.x && pa.y == pb.y && a.drainCount() == b.drainCount();
    }
    CHECK(identical);
    CHECK(a.drainCount() >= 1);
  }

  testSeveralBalls();
}
