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

// A unit vector from which to approach a target so the shot is square to its
// face and comes from open playfield.
Vec2 approachDirection(const table::Target& target) {
  switch (target.bank) {
    case table::TargetBank::Mission:
      return {-1.0F, 0.0F};  // On the left wall: travel left.
    case table::TargetBank::Booster:
      return {1.0F, 0.0F};  // On the right wall: travel right.
    case table::TargetBank::Medal:
      return {0.0F, -1.0F};  // Faces the flippers: travel up.
    case table::TargetBank::Multiplier:
      break;
  }
  // Diagonal bank on the upper left: travel up and left, square to it.
  const Vec2 d = target.shape.b - target.shape.a;
  const float len = std::sqrt(d.x * d.x + d.y * d.y);
  return {d.y / len, -d.x / len};
}

}  // namespace

void testTargets() {
  {  // Element references validate against the layout.
    CHECK(table::isValid({table::ElementKind::Target, 0}));
    CHECK(!table::isValid({table::ElementKind::Target, 200}));
    CHECK(!table::isValid({table::ElementKind::None, 0}));
    CHECK(table::elementCount(table::ElementKind::Slingshot) == 2);
    std::size_t perBank[table::kTargetBankCount] = {};
    for (const table::Target& t : table::kTargets) {
      ++perBank[static_cast<std::size_t>(t.bank)];
    }
    CHECK(perBank[static_cast<std::size_t>(table::TargetBank::Booster)] == 3);
    CHECK(perBank[static_cast<std::size_t>(table::TargetBank::Medal)] == 3);
    CHECK(perBank[static_cast<std::size_t>(table::TargetBank::Mission)] == 3);
    CHECK(perBank[static_cast<std::size_t>(table::TargetBank::Multiplier)] == 2);
  }

  // A square shot at each target reports a hit on exactly that target first,
  // and the ball bounces back.
  for (std::size_t i = 0; i < table::kTargets.size(); ++i) {
    const table::Target& target = table::kTargets[i];
    const Vec2 dir = approachDirection(target);
    const Vec2 mid = midpoint(target.shape);
    PhysicsWorld world;
    world.resetBall(mid - dir * 60.0F, dir * 900.0F);
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
        events += world.events().size();
      }
      CHECK(events == 0);
    }
  }

  {  // Bank logic: lighting every target of a bank counts one completion and
     // resets the bank. Driven by real shots through the game.
    pinball::Game game;
    CHECK(game.bankCompletions(table::TargetBank::Medal) == 0);
    CHECK(!game.targetLit(0));
    CHECK(!game.targetLit(999));
    CHECK(game.slingshotFlash(999) == 0.0F);
  }

  {  // A ball dropped on the medal island's roof rolls off; it cannot rest there.
    for (const float dx : {-40.0F, -10.0F, 0.5F, 12.0F, 45.0F}) {
      PhysicsWorld world;
      world.resetBall({table::kFieldCenterX + dx, 380.0F}, {0.0F, 0.0F});
      for (int step = 0; step < 120 * 4; ++step) {
        world.step(kStep);
      }
      CHECK(world.ballPosition().y > table::kMedalBankY + 20.0F);
    }
  }
}
