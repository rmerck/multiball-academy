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
  float peakSpeed = 0.0F;  // Fastest upward speed, pixels per second.
  float highest = 1e9F;    // Smallest y reached.
  float leftmost = 1e9F;   // Smallest x reached.
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

    // The briefest tap barely moves the plunger: the ball does not clear
    // the lane and stays in it.
    CHECK(tap.highest > table::kLaneWallTopY);
    CHECK(tap.leftmost > table::kLaneWallX);

    // Anything longer leaves the lane: even a short press sends the ball up
    // the launch curve and through the one-way arch.
    const LaunchResult brief = launch(8, 360);
    CHECK(brief.highest < table::kArchCenter.y - 100.0F);
    CHECK(brief.leftmost < table::kLaneWallX - 40.0F);

    // A full draw sends the ball over the arch into the left half.
    CHECK(full.highest < table::kArchCenter.y - 150.0F);
    CHECK(full.leftmost < table::kFieldCenterX);

    // The ball leaves somewhat faster than the plunger face (it rebounds off
    // it), but not wildly so.
    const float commanded = PhysicsWorld::kPlungerMaxSpeed * PhysicsWorld::kPixelsPerMeter;
    CHECK(full.peakSpeed > commanded);
    CHECK(full.peakSpeed < commanded * 1.45F);
  }

  {  // A tap does not half-launch. The plunger has to be drawn back for
     // kShortestLaunch steps (a twentieth of a second) before it can throw
     // the ball: let go sooner and the ball settles back on it, having gone
     // nowhere. From there on, every hold up to the full draw and past it
     // sends the ball out of the lane and through the arch. There is no
     // hold between the two that leaves the ball part of the way up.
    constexpr int kShortestLaunch = 6;
    int returned = 0;
    int launched = 0;
    for (int hold = 1; hold <= 130; ++hold) {
      PhysicsWorld world;
      world.resetBall(table::kBallStart, {0.0F, 0.0F});
      for (int i = 0; i < 30; ++i) {
        world.step(kStep);
      }
      const Vec2 rest = world.ballPosition();
      world.setPlunger(true);
      for (int i = 0; i < hold; ++i) {
        world.step(kStep);
      }
      world.setPlunger(false);
      bool out = false;
      for (int i = 0; i < 120 * 4 && !out; ++i) {
        world.step(kStep);
        out = world.ballPosition().x < table::kLaneWallX - 40.0F;
      }
      const Vec2 p = world.ballPosition();
      const Vec2 v = world.ballVelocity();
      const bool back = !out && std::fabs(p.y - rest.y) < 1.0F && std::fabs(p.x - rest.x) < 1.0F &&
                        std::fabs(v.x) + std::fabs(v.y) < 5.0F;
      CHECK(out || back);
      CHECK(out == (hold >= kShortestLaunch));
      returned += back ? 1 : 0;
      launched += out ? 1 : 0;
    }
    CHECK(returned == kShortestLaunch - 1 && launched == 131 - kShortestLaunch);
  }

  {  // No launch is an unavoidable drain: for every hold that leaves the lane,
     // an untouched ball does not run round the arch, down the left lane and
     // straight out of the left outlane.
    int straightOut = 0;
    int leftLane = 0;
    for (int hold = 60; hold <= 200; hold += 4) {
      Game game;
      InputState input;
      for (int i = 0; i < 30; ++i) {
        game.step(input);
      }
      input.plunger = true;
      for (int i = 0; i < hold; ++i) {
        game.step(input);
      }
      input.plunger = false;
      bool inPlay = false;
      bool touchedField = false;
      for (int i = 0; i < 120 * 6 && game.drainCount() == 0; ++i) {
        game.step(input);
        const Vec2 p = game.ball().position;
        inPlay = inPlay || game.ballInPlay(0);
        // Anywhere in the middle of the table counts as having entered play.
        touchedField = touchedField || (p.x > 120.0F && p.x < 620.0F && p.y > 200.0F);
      }
      if (inPlay) {
        ++leftLane;
        if (game.drainCount() > 0 && !touchedField) {
          ++straightOut;
        }
      }
    }
    CHECK(leftLane > 10);
    CHECK(straightOut == 0);
  }

  {  // In a game: the ball is served at rest and launches with the plunger;
     // once in play it never comes back into the lane, though nothing shuts
     // behind it; and after it drains a new ball is served.
    Game game;
    InputState input;
    for (int i = 0; i < 60; ++i) {
      game.step(input);
    }
    CHECK(std::fabs(game.ball().position.y - table::kBallStart.y) < 3.0F);
    CHECK(!game.ballInPlay(0));
    CHECK(!game.rules().ballInPlay());
    CHECK(game.drainCount() == 0);

    input.plunger = true;
    for (int i = 0; i < 150; ++i) {
      game.step(input);
    }
    input.plunger = false;
    bool inPlay = false;
    bool reenteredLane = false;
    for (int i = 0; i < 120 * 60 && game.drainCount() == 0; ++i) {
      game.step(input);
      inPlay = inPlay || game.ballInPlay(0);
      if (inPlay && game.drainCount() == 0) {
        CHECK(game.rules().ballInPlay());
        const Vec2 p = game.ball().position;
        if (p.x > table::kLaneWallX + 8.0F && p.y > table::kLaneWallTopY) {
          reenteredLane = true;
        }
      }
    }
    CHECK(inPlay);
    CHECK(!reenteredLane);
    CHECK(game.drainCount() == 1);
    CHECK(!game.ballInPlay(0));
    for (int i = 0; i < 60; ++i) {
      game.step(input);
    }
    CHECK(std::fabs(game.ball().position.x - table::kBallStart.x) < 3.0F);
  }

  {  // The one-way arch: balls fired at the lane from the field stay out of
     // it. From under the arch at every height of it, and up and down the
     // right circuit lane at its wall.
    int entered = 0;
    for (int shot = 0; shot < 60; ++shot) {
      PhysicsWorld world;
      if (shot < 30) {
        const float y = 130.0F + 5.5F * static_cast<float>(shot);
        world.resetBall({500.0F, y}, {2500.0F, shot % 2 == 0 ? 200.0F : -200.0F});
      } else {
        const float y = 310.0F + 7.0F * static_cast<float>(shot - 30);
        world.resetBall({594.0F, y}, {1500.0F, shot % 2 == 0 ? 900.0F : -900.0F});
      }
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
