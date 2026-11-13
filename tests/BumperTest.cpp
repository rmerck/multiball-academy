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

using pinball::PhysicsEvent;
using pinball::PhysicsWorld;
using pinball::Vec2;
namespace table = pinball::table;

constexpr float kStep = 1.0F / 120.0F;

float length(Vec2 v) { return std::sqrt(v.x * v.x + v.y * v.y); }

}  // namespace

void testBumpers() {
  {  // Layout rule: no pockets. Every pair of bumpers is either far enough
     // apart for the ball to pass with room to spare, or would be touching.
    const float ball = 2.0F * table::kBallRadius;
    for (std::size_t i = 0; i < table::kBumpers.size(); ++i) {
      const table::Circle& a = table::kBumpers[i].shape;
      for (std::size_t j = i + 1; j < table::kBumpers.size(); ++j) {
        const table::Circle& b = table::kBumpers[j].shape;
        const float gap = length(a.center - b.center) - a.radius - b.radius;
        CHECK(gap > ball + 8.0F);
      }
    }
  }

  // A ball sent into each bumper from its left is kicked back out, heading
  // away from the bumper, and that bumper reports exactly one hit for the
  // whole touch, however long the ball takes to leave.
  for (std::size_t i = 0; i < table::kBumpers.size(); ++i) {
    const table::Circle& c = table::kBumpers[i].shape;
    PhysicsWorld world;
    const Vec2 start{c.center.x - c.radius - table::kBallRadius - 30.0F, c.center.y + 6.0F};
    // Fast enough that gravity does not carry it under the bumper first.
    world.resetBall(start, {600.0F, 0.0F});
    std::size_t hitsOnThis = 0;
    bool measured = false;
    float speedAfter = 0.0F;
    Vec2 away{};
    Vec2 velocityAfter{};
    for (int step = 0; step < 40; ++step) {
      world.step(kStep);
      for (const PhysicsEvent& e : world.events()) {
        CHECK(e.speed >= 0.0F);
        // The kicked ball may go on to strike something else; only this
        // bumper's own reports are counted.
        if (e.element.kind == table::ElementKind::Bumper && e.element.index == i) {
          ++hitsOnThis;
        }
      }
      if (hitsOnThis > 0 && !measured) {
        measured = true;
        velocityAfter = world.ballVelocity();
        speedAfter = length(velocityAfter);
        away = world.ballPosition() - c.center;
      }
    }
    CHECK(hitsOnThis == 1);
    const float kick = PhysicsWorld::kBumperKickSpeed * PhysicsWorld::kPixelsPerMeter;
    CHECK(speedAfter > kick * 0.9F);
    CHECK(speedAfter < kick * 2.5F);
    CHECK(velocityAfter.x * away.x + velocityAfter.y * away.y > 0.0F);
  }

  {  // A near miss is not a hit: a ball passing a pixel and a half clear of
     // a bumper is neither kicked nor reported.
    const table::Circle& c = table::kBumpers[1].shape;
    PhysicsWorld world;
    const float x = c.center.x + c.radius + table::kBallRadius + 1.5F;
    world.resetBall({x, c.center.y - 40.0F}, {0.0F, 300.0F});
    std::size_t events = 0;
    for (int step = 0; step < 30; ++step) {
      world.step(kStep);
      events += world.events().size();
    }
    CHECK(events == 0);
    CHECK(std::fabs(world.ballPosition().x - x) < 1.0F);
  }

  {  // The centre bumper sits under the middle roll call lane, between its
     // posts: a ball dropped dead centre onto it cannot be held bouncing
     // there. It gets away.
    PhysicsWorld world;
    world.resetBall({table::kFieldCenterX, table::kTopLaneY}, {0.0F, 0.0F});
    bool escaped = false;
    for (int step = 0; step < 120 * 10 && !escaped; ++step) {
      world.step(kStep);
      escaped = std::fabs(world.ballPosition().x - table::kFieldCenterX) > 60.0F ||
                world.ballPosition().y > 300.0F;
    }
    CHECK(escaped);
  }

  {  // Two bumpers cannot hold a ball between them. Dropped into the gap
     // of each pair, from every place across it that is clear of both and
     // at rest or already falling, the ball is out of the nest, or well
     // away from where it was put, within ten seconds.
    int starts = 0;
    int held = 0;
    for (std::size_t i = 0; i < table::kBumpers.size(); ++i) {
      for (std::size_t j = i + 1; j < table::kBumpers.size(); ++j) {
        const table::Circle& a = table::kBumpers[i].shape;
        const table::Circle& b = table::kBumpers[j].shape;
        const Vec2 middle = (a.center + b.center) * 0.5F;
        for (float dx = -40.0F; dx <= 40.0F; dx += 4.0F) {
          for (const float speed : {0.0F, 300.0F}) {
            const Vec2 start = middle + Vec2{dx, -30.0F};
            const float clear = table::kBallRadius + 1.0F;
            if (length(start - a.center) < a.radius + clear ||
                length(start - b.center) < b.radius + clear) {
              continue;
            }
            ++starts;
            PhysicsWorld world;
            world.resetBall(start, {0.0F, speed});
            bool escaped = false;
            for (int step = 0; step < 120 * 10 && !escaped; ++step) {
              world.step(kStep);
              escaped = std::fabs(world.ballPosition().x - start.x) > 80.0F ||
                        world.ballPosition().y > 300.0F;
            }
            held += escaped ? 0 : 1;
          }
        }
      }
    }
    std::printf("two bumpers: %d starts, %d held\n", starts, held);
    CHECK(starts >= 30);
    CHECK(held == 0);
  }

  {  // A rejected step leaves no stale events.
    const table::Circle& c = table::kBumpers[0].shape;
    PhysicsWorld world;
    world.resetBall({c.center.x - c.radius - table::kBallRadius - 6.0F, c.center.y},
                    {900.0F, 0.0F});
    bool sawHit = false;
    for (int step = 0; step < 20 && !sawHit; ++step) {
      world.step(kStep);
      sawHit = !world.events().empty();
    }
    CHECK(sawHit);
    world.step(-1.0F);
    CHECK(world.events().empty());
  }

  {  // Events are cleared every step and never exceed the cap.
    PhysicsWorld world;
    const table::Circle& c = table::kBumpers[0].shape;
    world.resetBall({c.center.x, c.center.y - c.radius - table::kBallRadius - 10.0F},
                    {0.0F, 400.0F});
    bool sawEvent = false;
    for (int step = 0; step < 240; ++step) {
      world.step(kStep);
      CHECK(world.events().size() <= PhysicsWorld::kMaxEventsPerStep);
      if (sawEvent && world.events().empty()) {
        break;
      }
      sawEvent = sawEvent || !world.events().empty();
    }
    CHECK(sawEvent);
    world.step(kStep);
    world.step(kStep);
    CHECK(world.events().size() <= 1);
  }

  {  // Soak: across long scripted games the ball stays sane, bumpers get hit,
     // and their lights flash and fade within range.
    pinball::Game game;
    float maxFlash = 0.0F;
    for (int i = 0; i < 120 * 300; ++i) {
      pinball::InputState input;
      input.plunger = (i % 1200) < 150;
      input.leftFlipper = (i / 31) % 2 == 0;
      input.rightFlipper = (i / 43) % 2 == 0;
      input.newGame = game.gameOver();
      game.step(input);
      for (std::size_t b = 0; b < table::kBumpers.size(); ++b) {
        const float flash = game.bumperFlash(b);
        CHECK(flash >= 0.0F && flash <= 1.0F);
        maxFlash = std::max(maxFlash, flash);
      }
    }
    CHECK(game.bumperHits() > 0);
    CHECK(maxFlash == 1.0F);
    CHECK(game.bumperFlash(999) == 0.0F);
  }
}
