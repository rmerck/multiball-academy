#include <algorithm>
#include <cmath>

#include "Check.hpp"
#include "Suites.hpp"
#include "core/Game.hpp"
#include "core/Physics.hpp"
#include "core/TableLayout.hpp"

namespace {

using pinball::Game;
using pinball::InputState;
using pinball::PhysicsWorld;
using pinball::Vec2;
namespace table = pinball::table;

constexpr float kStep = 1.0F / 120.0F;

struct LaunchResult {
  float peakSpeed = 0.0F;   // Fastest upward speed, pixels per second.
  float highest = 1e9F;     // Smallest y reached.
  float leftmost = 1e9F;    // Smallest x reached.
};

// Holds the plunger for holdSteps, releases, and follows the ball.
LaunchResult launch(int holdSteps, int followSteps) {
  PhysicsWorld world;
  world.resetBall(table::kBallStart, {0.0F, 0.0F});
  for (int i = 0; i < 30; ++i) {
    world.step(kStep);
  }
  world.setPlunger(true);
  for (int i = 0; i < holdSteps; ++i) {
    world.step(kStep);
  }
  world.setPlunger(false);
  LaunchResult result;
  for (int i = 0; i < followSteps; ++i) {
    world.step(kStep);
    result.peakSpeed = std::max(result.peakSpeed, -world.ballVelocity().y);
    result.highest = std::min(result.highest, world.ballPosition().y);
    result.leftmost = std::min(result.leftmost, world.ballPosition().x);
  }
  return result;
}

}  // namespace

void testPlunger() {
  {  // The plunger draws back while held, up to its travel, and the ball
     // follows it down.
    PhysicsWorld world;
    world.resetBall(table::kBallStart, {0.0F, 0.0F});
    world.setPlunger(true);
    float previous = 0.0F;
    for (int i = 0; i < 240; ++i) {
      world.step(kStep);
      CHECK(world.plungerPull() >= previous);
      CHECK(world.plungerPull() <= table::kPlungerTravel);
      previous = world.plungerPull();
    }
    CHECK(std::fabs(world.plungerPull() - table::kPlungerTravel) < 0.01F);
    CHECK(world.ballPosition().y > table::kBallStart.y + table::kPlungerTravel - 4.0F);

    // Released, it returns exactly to rest.
    world.setPlunger(false);
    for (int i = 0; i < 60; ++i) {
      world.step(kStep);
    }
    CHECK(world.plungerPull() == 0.0F);
  }

  {  // Launch strength grows with how long the plunger is held.
    const LaunchResult tap = launch(2, 360);
    const LaunchResult half = launch(60, 360);
    const LaunchResult full = launch(150, 360);
    CHECK(tap.peakSpeed < half.peakSpeed);
    CHECK(half.peakSpeed < full.peakSpeed);

    // A tap does not clear the lane; the ball stays in it.
    CHECK(tap.highest > table::kLaneWallTopY);
    CHECK(tap.leftmost > table::kLaneWallX);

    // A full draw sends the ball over the arch into the left half.
    CHECK(full.highest < table::kArchCenterY - 150.0F);
    CHECK(full.leftmost < table::kFieldCenterX);

    // And lands near the commanded speed, without wild overshoot.
    const float commanded = PhysicsWorld::kPlungerMaxSpeed * PhysicsWorld::kPixelsPerMeter;
    CHECK(full.peakSpeed > commanded * 0.8F);
    CHECK(full.peakSpeed < commanded * 1.3F);
  }

  {  // In a game: the ball is served at rest, launches with the plunger, the
     // gate shuts behind it, and after it drains a new ball is served with
     // the gate open again.
    Game game;
    InputState input;
    for (int i = 0; i < 60; ++i) {
      game.step(input);
    }
    CHECK(std::fabs(game.ball().position.y - table::kBallStart.y) < 3.0F);
    CHECK(!game.laneGateClosed());
    CHECK(game.drainCount() == 0);

    input.plunger = true;
    for (int i = 0; i < 150; ++i) {
      game.step(input);
    }
    input.plunger = false;
    bool gateClosed = false;
    bool reenteredLane = false;
    for (int i = 0; i < 120 * 60 && game.drainCount() == 0; ++i) {
      game.step(input);
      gateClosed = gateClosed || game.laneGateClosed();
      if (gateClosed && game.drainCount() == 0) {
        const Vec2 p = game.ball().position;
        if (p.x > table::kLaneWallX + 8.0F && p.y > table::kLaneWallTopY) {
          reenteredLane = true;
        }
      }
    }
    CHECK(gateClosed);
    CHECK(!reenteredLane);
    CHECK(game.drainCount() == 1);
    CHECK(!game.laneGateClosed());
    for (int i = 0; i < 60; ++i) {
      game.step(input);
    }
    CHECK(std::fabs(game.ball().position.x - table::kBallStart.x) < 3.0F);
  }

  {  // With the gate closed, balls fired at the lane mouth stay out of it.
    int entered = 0;
    for (int shot = 0; shot < 40; ++shot) {
      PhysicsWorld world;
      world.setLaneGateClosed(true);
      const float y = 150.0F + 3.0F * static_cast<float>(shot);
      world.resetBall({560.0F, y}, {2500.0F, 200.0F});
      for (int i = 0; i < 360; ++i) {
        world.step(kStep);
        const Vec2 p = world.ballPosition();
        if (p.x > table::kLaneWallX + 8.0F && p.y > table::kLaneWallTopY) {
          ++entered;
          break;
        }
      }
    }
    CHECK(entered == 0);
  }
}
