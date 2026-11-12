#include <cmath>
#include <cstdint>

#include "Check.hpp"
#include "Suites.hpp"
#include "core/Game.hpp"
#include "core/Physics.hpp"
#include "core/TableLayout.hpp"

namespace {

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
  if (p.x < table::kWallLeft || p.x > table::kWallRight || p.y > table::kFloorY) {
    return false;
  }
  if (p.y < table::kArchCenterY) {
    const float nx = (p.x - table::kArchCenterX) / table::kArchRadiusX;
    const float ny = (p.y - table::kArchCenterY) / table::kArchRadiusY;
    if (nx * nx + ny * ny > 1.0F) {
      return false;
    }
  }
  return true;
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
    world.resetBall({table::kFieldCenterX, 400.0F}, {0.0F, 0.0F});
    for (int i = 0; i < 30; ++i) {
      world.step(kStep);
    }
    CHECK(world.ballPosition().y > 400.0F);
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
      const Vec2 origin{120.0F + rng.unit() * 500.0F, 150.0F + rng.unit() * 450.0F};
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
    const Vec2 after = world.ballPosition();
    CHECK(before.x == after.x && before.y == after.y);
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
}
