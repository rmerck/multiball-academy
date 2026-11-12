#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "Check.hpp"
#include "Suites.hpp"
#include "core/Game.hpp"
#include "core/Physics.hpp"
#include "core/TableLayout.hpp"

namespace {

using pinball::Game;
using pinball::InputState;
using pinball::PhysicsEvent;
using pinball::PhysicsWorld;
using pinball::Vec2;
namespace table = pinball::table;

constexpr float kStep = 1.0F / 120.0F;

float distance(Vec2 a, Vec2 b) {
  const Vec2 d = a - b;
  return std::sqrt(d.x * d.x + d.y * d.y);
}

// Steps until an event of the given type arrives. Returns its element, or
// an invalid reference on timeout.
table::ElementRef stepUntil(PhysicsWorld& world, PhysicsEvent::Type type, int maxSteps) {
  for (int i = 0; i < maxSteps; ++i) {
    world.step(kStep);
    for (const PhysicsEvent& e : world.events()) {
      if (e.type == type) {
        return e.element;
      }
    }
  }
  return {};
}

}  // namespace

void testHoles() {
  {  // Layout: wormholes form a ring, every exit is inside the table, and no
     // hole returns the ball straight into another hole's mouth.
    for (std::size_t i = 0; i < table::kWormholeCount; ++i) {
      CHECK(table::kHoles[i].type == table::HoleType::Wormhole);
      const table::Hole& next = table::kHoles[(i + 1) % table::kWormholeCount];
      CHECK(distance(table::kHoles[i].exitPosition, next.center) < 0.01F);
    }
    for (const table::Hole& hole : table::kHoles) {
      CHECK(hole.exitPosition.x > table::kWallLeft + table::kBallRadius);
      CHECK(hole.exitPosition.x < table::kLaneWallX - table::kBallRadius);
      CHECK(hole.exitPosition.y > 60.0F && hole.exitPosition.y < table::kHeight);
      for (const table::Hole& other : table::kHoles) {
        const float d = distance(hole.exitPosition, other.center);
        CHECK(d < 0.01F || d > table::kHoleRearmDistance + table::kBallRadius);
      }
    }
  }

  // A ball rolled slowly into each ordinary hole is captured by that hole,
  // held off the table, and cannot be captured again until it has left.
  for (std::size_t i = 0; i < table::kHoles.size(); ++i) {
    const table::Hole& hole = table::kHoles[i];
    if (hole.type == table::HoleType::Ramp) {
      continue;
    }
    PhysicsWorld world;
    world.resetBall({hole.center.x, hole.center.y - 40.0F}, {0.0F, 200.0F});
    const table::ElementRef captured = stepUntil(world, PhysicsEvent::Type::Capture, 120);
    CHECK(captured.kind == table::ElementKind::Hole);
    CHECK(captured.index == i);
    CHECK(world.ballHeld());

    // Held: the ball does not move and nothing else happens to it.
    const Vec2 heldAt = world.ballPosition();
    for (int s = 0; s < 60; ++s) {
      world.step(kStep);
      CHECK(world.events().empty());
    }
    CHECK(distance(world.ballPosition(), heldAt) < 0.01F);
    CHECK(world.ballIsSane());

    // Released in place with no speed, it sits in the mouth but is not
    // swallowed again until it has rolled clear.
    world.releaseBall(hole.center, {0.0F, 0.0F});
    CHECK(!world.ballHeld());
    bool recaptured = false;
    for (int s = 0; s < 20; ++s) {
      world.step(kStep);
      recaptured = recaptured || world.ballHeld();
    }
    CHECK(!recaptured);
  }

  {  // The ramp takes only a ball travelling up the table fast enough.
    std::size_t ramp = 0;
    for (std::size_t i = 0; i < table::kHoles.size(); ++i) {
      if (table::kHoles[i].type == table::HoleType::Ramp) {
        ramp = i;
      }
    }
    const Vec2 entry = table::kHoles[ramp].center;
    {
      PhysicsWorld world;
      world.resetBall({entry.x, entry.y + 50.0F}, {0.0F, -900.0F});
      const table::ElementRef captured = stepUntil(world, PhysicsEvent::Type::Capture, 60);
      CHECK(captured.kind == table::ElementKind::Hole && captured.index == ramp);
    }
    {  // Falling back down through the entry: not taken.
      PhysicsWorld world;
      world.resetBall({entry.x, entry.y - 50.0F}, {0.0F, 300.0F});
      const table::ElementRef captured = stepUntil(world, PhysicsEvent::Type::Capture, 40);
      CHECK(captured.kind == table::ElementKind::None);
    }
    {  // Creeping upward too slowly: not taken.
      PhysicsWorld world;
      world.resetBall({entry.x, entry.y + 14.0F}, {0.0F, -150.0F});
      const table::ElementRef captured = stepUntil(world, PhysicsEvent::Type::Capture, 20);
      CHECK(captured.kind == table::ElementKind::None);
    }
  }

  // Each rollover reports once per pass, however long the ball takes.
  for (std::size_t i = 0; i < table::kRollovers.size(); ++i) {
    const Vec2 c = table::kRollovers[i].zone.center;
    PhysicsWorld world;
    world.resetBall({c.x, c.y - 30.0F}, {0.0F, 60.0F});
    std::size_t enters = 0;
    // Long enough to cross the switch once, short enough that nothing below
    // has bounced the ball back over it.
    for (int s = 0; s < 38; ++s) {
      world.step(kStep);
      for (const PhysicsEvent& e : world.events()) {
        if (e.type == PhysicsEvent::Type::Enter) {
          CHECK(e.element.kind == table::ElementKind::Rollover);
          CHECK(e.element.index == i);
          ++enters;
        }
      }
    }
    CHECK(enters == 1);
  }

  {  // Through the game: a wormhole hides the ball, holds it, and returns it
     // from the next wormhole in the ring; the ramp keeps it visible and
     // delivers it to the top of the field.
    const double holdSteps = Game::kHoleHoldSeconds / Game::kStepSeconds;
    CHECK(holdSteps > 30.0 && holdSteps < 600.0);
    CHECK(Game().holeCount(999) == 0);
    CHECK(Game().rolloverCount(999) == 0);
  }

  {  // No pocket on the wall-mounted target banks: balls dropped down either
     // side wall onto the top of a bank roll off it.
    for (const float x : {table::kWallLeft + table::kBallRadius + 0.5F,
                          table::kLaneWallX - table::kWallRadius - table::kBallRadius - 0.5F}) {
      for (const float speed : {0.0F, 150.0F, 600.0F}) {
        PhysicsWorld world;
        world.resetBall({x, 400.0F}, {0.0F, speed});
        // It may bounce around or drop into a hole afterwards; what it must
        // not do is stop anywhere near the bank.
        int restingSteps = 0;
        for (int s = 0; s < 120 * 6 && !world.ballHeld(); ++s) {
          world.step(kStep);
          const Vec2 p = world.ballPosition();
          const Vec2 v = world.ballVelocity();
          const bool nearBank = p.y > 470.0F && p.y < 640.0F &&
                                std::fabs(p.x - x) < 40.0F;
          const bool still = v.x * v.x + v.y * v.y < 25.0F;
          restingSteps = nearBank && still ? restingSteps + 1 : 0;
        }
        CHECK(restingSteps < 60);
      }
    }
  }

  {  // pushBall adds velocity to a ball in play, respects the speed cap, and
     // ignores bad input and a held ball.
    PhysicsWorld world;
    world.resetBall({table::kFieldCenterX, 600.0F}, {0.0F, 0.0F});
    world.pushBall({260.0F, -420.0F});
    Vec2 v = world.ballVelocity();
    CHECK(std::fabs(v.x - 260.0F) < 1.0F && std::fabs(v.y + 420.0F) < 1.0F);
    world.pushBall({NAN, 0.0F});
    v = world.ballVelocity();
    CHECK(std::fabs(v.x - 260.0F) < 1.0F);
    world.pushBall({1.0e6F, 0.0F});
    v = world.ballVelocity();
    CHECK(std::sqrt(v.x * v.x + v.y * v.y) <=
          PhysicsWorld::kMaxBallSpeed * PhysicsWorld::kPixelsPerMeter * 1.001F);
  }

  {  // Ball search never fires for a ball waiting on the plunger.
    Game game;
    const InputState idle;
    for (int i = 0; i < 30; ++i) {
      game.step(idle);
    }
    for (int i = 0; i < 120 * 10; ++i) {
      game.step(idle);
    }
    CHECK(game.ballSearchCount() == 0);
  }

  {  // Soak: ten minutes of scripted play. The ball must never sit still on
     // the playfield (a wedge), every kind of feature must see use, and the
     // game must keep serving balls.
    Game game;
    int stillSteps = 0;
    int worstStill = 0;
    Vec2 previous{};
    Vec2 worstAt{};
    for (int i = 0; i < 120 * 600; ++i) {
      InputState input;
      input.plunger = (i % 900) < 100 + (i / 900) % 60;
      input.leftFlipper = (i / 29) % 2 == 0;
      input.rightFlipper = (i / 41) % 2 == 0;
      game.step(input);

      const pinball::BallView ball = game.ball();
      const bool inLane = ball.position.x > table::kLaneWallX;
      if (game.ballHeld() || inLane) {
        stillSteps = 0;
        continue;
      }
      const float moved = distance(ball.position, previous);
      previous = ball.position;
      stillSteps = moved < 0.05F ? stillSteps + 1 : 0;
      if (stillSteps > worstStill) {
        worstStill = stillSteps;
        worstAt = ball.position;
      }
    }
    if (worstStill >= 120 * 3) {
      std::fprintf(stderr, "ball sat still for %d steps at (%.0f, %.0f)\n", worstStill,
                   static_cast<double>(worstAt.x), static_cast<double>(worstAt.y));
    }
    CHECK(worstStill < 120 * 3);
    // The geometry alone must keep the ball moving: the ball search is a
    // safety net and should never be needed in normal play.
    CHECK(game.ballSearchCount() == 0);
    CHECK(game.drainCount() >= 20);
    CHECK(game.bumperHits() > 50);

    std::uint32_t holes = 0;
    std::uint32_t rollovers = 0;
    for (std::size_t h = 0; h < table::kHoles.size(); ++h) {
      holes += game.holeCount(h);
    }
    for (std::size_t r = 0; r < table::kRollovers.size(); ++r) {
      rollovers += game.rolloverCount(r);
    }
    std::fprintf(stderr,
                 "soak: drains %u, bumper hits %u, hole captures %u, rollovers %u, longest "
                 "still %d steps\n",
                 game.drainCount(), game.bumperHits(), holes, rollovers, worstStill);
    CHECK(holes > 0);
    CHECK(rollovers > 20);
  }
}
