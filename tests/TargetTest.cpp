#include <algorithm>
#include <cmath>
#include <cstddef>

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

Vec2 midpoint(const table::Capsule& c) { return (c.a + c.b) * 0.5F; }

// A unit vector square to a turbo target's face, pointing at it from the
// nest (the targets are set into the rim, facing in).
Vec2 approachDirection(const table::Target& target) {
  const Vec2 d = target.shape.b - target.shape.a;
  const float len = std::sqrt(d.x * d.x + d.y * d.y);
  const Vec2 normal{d.y / len, -d.x / len};
  const Vec2 fromNest = midpoint(target.shape) - Vec2{table::kFieldCenterX, 230.0F};
  return normal.x * fromNest.x + normal.y * fromNest.y > 0.0F ? normal : normal * -1.0F;
}

}  // namespace

void testTargets() {
  {  // Element references validate against the layout.
    CHECK(table::isValid({table::ElementKind::Target, 0}));
    CHECK(!table::isValid({table::ElementKind::Target, 200}));
    CHECK(!table::isValid({table::ElementKind::None, 0}));
    CHECK(table::isValid({table::ElementKind::DropTarget, 2}));
    CHECK(!table::isValid({table::ElementKind::DropTarget, 3}));
    CHECK(table::elementCount(table::ElementKind::Slingshot) == 2);
    CHECK(table::elementCount(table::ElementKind::Target) == 2);
    CHECK(table::elementCount(table::ElementKind::DropTarget) == 3);
    // The two turbo targets mirror each other.
    CHECK(table::kTargets[0].id == table::TargetId::NestLeft);
    CHECK(table::kTargets[1].id == table::TargetId::NestRight);
    CHECK(midpoint(table::kTargets[0].shape).x + midpoint(table::kTargets[1].shape).x ==
          2.0F * table::kFieldCenterX);
  }

  // A square shot at each turbo target reports a hit on exactly that target
  // first, and the ball bounces back.
  for (std::size_t i = 0; i < table::kTargets.size(); ++i) {
    const table::Target& target = table::kTargets[i];
    const Vec2 dir = approachDirection(target);
    const Vec2 mid = midpoint(target.shape);
    PhysicsWorld world;
    world.resetBall(mid - dir * 50.0F, dir * 900.0F);
    bool hit = false;
    for (int step = 0; step < 60 && !hit; ++step) {
      world.step(kStep);
      for (const PhysicsEvent& e : world.events()) {
        if (!hit) {
          CHECK(e.element.kind == table::ElementKind::Target);
          CHECK(e.element.index == i);
        }
        hit = true;
      }
    }
    CHECK(hit);
    const Vec2 v = world.ballVelocity();
    CHECK(v.x * dir.x + v.y * dir.y < 0.0F);
  }

  // The bay doors stand across the Hangar's mouth. A ball shot up at each
  // reports a hit on that door, bounces back, and does not get into the
  // Hangar behind it. (The third door is half behind the Briefing room's
  // flare for a ball coming straight up: it is shot from the left, as the
  // right flipper's shots come.)
  for (std::size_t i = 0; i < table::kDropTargets.size(); ++i) {
    const Vec2 mid = midpoint(table::kDropTargets[i]);
    const Vec2 from{mid.x - (i + 1 == table::kDropTargets.size() ? 18.0F : 0.0F), mid.y + 60.0F};
    const Vec2 aim = mid - from;
    const float reach = std::sqrt(aim.x * aim.x + aim.y * aim.y);
    PhysicsWorld world;
    world.resetBall(from, aim * (900.0F / reach));
    bool hit = false;
    for (int step = 0; step < 60; ++step) {
      world.step(kStep);
      for (const PhysicsEvent& e : world.events()) {
        if (!hit) {
          CHECK(e.element.kind == table::ElementKind::DropTarget);
          CHECK(e.element.index == i);
        }
        hit = true;
      }
      CHECK(!world.ballHeld());
      CHECK(world.ballPosition().y > table::kDoorY);
    }
    CHECK(hit);
  }
  {  // Nor between two doors, nor past the end of the bank: the mouth is shut.
    for (int k = 0; k <= 40; ++k) {
      const float x = table::kHangarLeft.a.x + 6.0F +
                      static_cast<float>(k) *
                          (table::kHangarRight.a.x - table::kHangarLeft.a.x - 12.0F) / 40.0F;
      PhysicsWorld world;
      world.resetBall({x, table::kDoorY + 70.0F}, {0.0F, -1400.0F});
      for (int step = 0; step < 90; ++step) {
        world.step(kStep);
        CHECK(!world.ballHeld());
        CHECK(world.ballPosition().y > table::kDoorY);
      }
    }
  }

  // Each slingshot kicks a ball that lands on the middle of its face, away
  // from the face, and reports it. A touch on its back edge does not.
  for (std::size_t i = 0; i < table::kSlingshots.size(); ++i) {
    const table::Triangle& sling = table::kSlingshots[i];
    const Vec2 top = sling.points[0];
    const Vec2 back = sling.points[1];
    const Vec2 bottom = sling.points[2];
    const Vec2 faceMid = (top + bottom) * 0.5F;
    const Vec2 face = bottom - top;
    const float len = std::sqrt(face.x * face.x + face.y * face.y);
    Vec2 normal{face.y / len, -face.x / len};
    if ((back.x - top.x) * normal.x + (back.y - top.y) * normal.y > 0.0F) {
      normal = normal * -1.0F;
    }
    {
      PhysicsWorld world;
      world.resetBall(faceMid + normal * 50.0F, normal * -500.0F);
      bool kicked = false;
      for (int step = 0; step < 60 && !kicked; ++step) {
        world.step(kStep);
        for (const PhysicsEvent& e : world.events()) {
          CHECK(e.element.kind == table::ElementKind::Slingshot);
          CHECK(e.element.index == i);
          kicked = true;
        }
      }
      CHECK(kicked);
      const Vec2 v = world.ballVelocity();
      const float outward = v.x * normal.x + v.y * normal.y;
      CHECK(outward > PhysicsWorld::kSlingshotKickSpeed * PhysicsWorld::kPixelsPerMeter * 0.8F);
    }
    {  // Back edge (the vertical side facing the inlane): no kick, no event.
      const Vec2 backMid = (top + back) * 0.5F;
      const float side = backMid.x < table::kFieldCenterX ? -1.0F : 1.0F;
      PhysicsWorld world;
      world.resetBall({backMid.x + side * 30.0F, backMid.y}, {-side * 300.0F, 0.0F});
      std::size_t events = 0;
      for (int step = 0; step < 30; ++step) {
        world.step(kStep);
        for (const PhysicsEvent& e : world.events()) {
          events += e.element.kind == table::ElementKind::Slingshot ? 1U : 0U;
        }
      }
      CHECK(events == 0);
    }
  }

  {  // Accessors tolerate out-of-range indices.
    pinball::Game game;
    CHECK(game.slingshotFlash(999) == 0.0F);
  }

  {  // A dead post takes the bounce out of a ball; an ordinary wall gives a
     // good part of it back. Both struck square at the same speed.
    const auto reboundFrom = [](const table::Capsule& wall, float side) {
      const Vec2 mid = midpoint(wall);
      PhysicsWorld world;
      world.resetBall({mid.x + side * 40.0F, mid.y}, {-side * 600.0F, 0.0F});
      float rebound = 0.0F;
      for (int step = 0; step < 12; ++step) {
        world.step(kStep);
        rebound = std::max(rebound, world.ballVelocity().x * side);
      }
      return rebound;
    };
    const float dead = reboundFrom(table::kTurbineRail, 1.0F);
    const float lively = reboundFrom(table::kHangarLeft, -1.0F);
    CHECK(dead < 600.0F * table::kDeadPostRestitution * 1.5F);
    CHECK(lively > 600.0F * 0.35F);
    CHECK(std::ranges::find_if(table::kDeadPosts, [](const table::Capsule& post) {
            return post.a.x == table::kTurbineRail.a.x && post.a.y == table::kTurbineRail.a.y;
          }) != table::kDeadPosts.end());
  }

  {  // A slingshot vane is a wall from the circuit lane and nothing from the
     // field: a ball in the lane cannot reach the slingshot's top through
     // it, a ball from the field passes into the lane.
    for (int side = 0; side < 2; ++side) {
      const auto& vane = side == 0 ? table::kSlingVaneLeft : table::kSlingVaneRight;
      const Vec2 mid = (vane[1] + vane[2]) * 0.5F;
      const float out = side == 0 ? -1.0F : 1.0F;  // From the vane towards the lane.
      {
        PhysicsWorld world;
        world.resetBall({mid.x + out * 30.0F, mid.y}, {-out * 400.0F, 0.0F});
        for (int step = 0; step < 20; ++step) {
          world.step(kStep);
          CHECK((world.ballPosition().x - mid.x) * out > table::kBallRadius - 4.0F);
        }
      }
      {
        PhysicsWorld world;
        world.resetBall({mid.x - out * 30.0F, mid.y}, {out * 400.0F, 0.0F});
        bool through = false;
        for (int step = 0; step < 20; ++step) {
          world.step(kStep);
          through = through || (world.ballPosition().x - mid.x) * out > table::kBallRadius;
        }
        CHECK(through);
      }
    }
  }

  {  // A ball dropped on the roofs of the Hangar and the Briefing room rolls
     // off into a lane beside them; it cannot rest there.
    for (float x = table::kHangarLeft.a.x - 6.0F; x <= table::kScoopRight.a.x + 6.0F; x += 7.5F) {
      PhysicsWorld world;
      world.resetBall({x, 270.0F}, {0.0F, 0.0F});
      for (int step = 0; step < 120 * 5; ++step) {
        world.step(kStep);
      }
      CHECK(!world.ballHeld());
      CHECK(world.ballPosition().y > 440.0F);
    }
  }
}
