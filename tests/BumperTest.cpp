#include <algorithm>
#include <cmath>
#include <cstdint>

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
      CHECK(a.center.x - a.radius - table::kWallLeft > ball + 8.0F);
      CHECK(table::kLaneWallX - table::kWallRadius - (a.center.x + a.radius) > ball + 8.0F);
      for (std::size_t j = i + 1; j < table::kBumpers.size(); ++j) {
        const table::Circle& b = table::kBumpers[j].shape;
        const float gap = length(a.center - b.center) - a.radius - b.radius;
        CHECK(gap > ball + 8.0F);
      }
    }
  }

  // A ball sent into each bumper from its left is kicked back out,
  // heading away from the bumper, and exactly that bumper reports a hit.
  for (std::size_t i = 0; i < table::kBumpers.size(); ++i) {
    const table::Circle& c = table::kBumpers[i].shape;
    PhysicsWorld world;
    const Vec2 start{c.center.x - c.radius - table::kBallRadius - 30.0F, c.center.y + 6.0F};
    // Fast enough that gravity does not carry it under the bumper first.
    world.resetBall(start, {600.0F, 0.0F});
    bool hit = false;
    std::size_t hitCount = 0;
    float speedAfter = 0.0F;
    Vec2 away{};
    for (int step = 0; step < 120 && !hit; ++step) {
      world.step(kStep);
      for (const PhysicsEvent& e : world.events()) {
        CHECK(e.type == PhysicsEvent::Type::Hit);
        CHECK(e.element.kind == table::ElementKind::Bumper);
        CHECK(e.element.index == i);
        CHECK(e.speed >= 0.0F);
        hit = true;
        ++hitCount;
      }
      if (hit) {
        speedAfter = length(world.ballVelocity());
        away = world.ballPosition() - c.center;
      }
    }
    CHECK(hit);
    CHECK(hitCount == 1);
    const float kick = PhysicsWorld::kBumperKickSpeed * PhysicsWorld::kPixelsPerMeter;
    CHECK(speedAfter > kick * 0.9F);
    CHECK(speedAfter < kick * 2.5F);
    // Moving away from the bumper centre.
    const Vec2 v = world.ballVelocity();
    CHECK(v.x * away.x + v.y * away.y > 0.0F);
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
