#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <cstdio>

#include "Check.hpp"
#include "GameTestAccess.hpp"
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

// The scripted soak below, reduced to one number: FNV-1a over the bits of
// the ball's position, the score and the drain count after every one of its
// 72,000 steps. It pins the whole single-ball game: physics, serve, holes,
// rules. It must change only together with a change that is meant to alter
// how the ball moves or scores (table geometry, a physics constant, a rule
// the script reaches, a new Box2D); a refactoring that changes it has
// changed the game.
//
// To regenerate, deliberately and with the reason in the commit message:
// build and run core_tests. On a mismatch it prints the line
//   soak trajectory 0x................, score ...
// and that value goes here. Check the same commit on arm64 (the macOS CI
// jobs run core_tests) before it is merged.
//
// The same on every platform the game is built for (x86-64 with glibc and
// arm64 macOS): the core is compiled without fused multiply-add
// (-ffp-contract=off, as Box2D is), the physics uses only exactly rounded
// arithmetic, and the table's curves are point tables written out in
// TableLayout.hpp, not computed with the C library.
// (Table revision 3, integrated (table/rev3-int): re-recorded once for the
// captive ball (2a) and the drone's rise after its 20 s (2b); see those
// branches. Concept A G0/G1 (table/a-geometry): re-recorded for the new upper
// right (the Flight Deck, its flipper, stand-ups and holes, F-L-A-P-S), as
// 0xdd2e8503b7c67182. Then merged with main's hole cups (#50, main's value
// 0x1cb50251cc53d2f3) and Altitude (#49): re-recorded deliberately for the
// capture at the cup and the pocket funnels of the Mission scoop, Hangar,
// CAT SHOT and deck drain, the deck's set-down moved clear of the raised
// deck bat (532, 236), and the shelf under the resting deck bat. Two runs on
// x86-64 give the same value; arm64 is checked by the macOS CI job. The soak
// plays at FL100, where the Air Pocket and the Prop Wash are idle.
// G2: re-recorded for the frictionless Hangar roof, deck stand-up A
// lowered, the deck set-down (507, 228) moving right,
// the BOGEY drops and the RADAR stand-ups (0xa1fa4e7df9970e85), then merged
// with concept A's rules (G5, feat/a-rules, recorded there as
// 0x09482ffd3c224202 for the new rules the script reaches): re-recorded on
// the merge (0x9671ef8fc1d10394); then the one-shape door bank was
// reverted (it moved B4): 0x72134c8bea581229. Was 0x4213a29dab26e639.)
constexpr std::uint64_t kSoakTrajectory = 0x72134c8bea581229ULL;

void mixInto(std::uint64_t& hash, std::uint64_t value) noexcept {
  for (unsigned byte = 0; byte < 8U; ++byte) {
    hash ^= (value >> (8U * byte)) & 0xFFU;
    hash *= 0x100000001b3ULL;
  }
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
  {  // Layout: every exit is inside the table, below the box that took the
     // ball or at the far end of a rail, and no hole returns the ball
     // straight into another hole's box.
    CHECK(table::kHoles[table::kScoopHole].type == table::HoleType::Subway);
    CHECK(table::kHoles[table::kHangarHole].type == table::HoleType::Hangar);
    CHECK(table::kHoles[table::kRunwayHole].type == table::HoleType::RampLeft);
    CHECK(table::kHoles[table::kSkywayHole].type == table::HoleType::RampRight);
    for (const table::Hole& hole : table::kHoles) {
      CHECK(hole.exitPosition.x > table::kWallLeft + table::kBallRadius);
      CHECK(hole.exitPosition.x < table::kLaneWallX - table::kBallRadius);
      CHECK(hole.exitPosition.y > 60.0F && hole.exitPosition.y < table::kHeight);
      // The Skyway's diverted ride (concept A) is never taken from the
      // field: its box has no size.
      if (hole.type == table::HoleType::DeckRamp) {
        CHECK(hole.halfSize.x == 0.0F && hole.halfSize.y == 0.0F);
        continue;
      }
      CHECK(hole.halfSize.x > 0.0F && hole.halfSize.y >= 9.0F);
      // A hole ignores the ball it has just returned until it is clear, so
      // the exit must be where that works: within the rearm distance of its
      // own box, or well away from it.
      for (const table::Hole& other : table::kHoles) {
        // A hole that kicks the ball back out of its own cup lets it go
        // there: it ignores the ball until it is clear (kHoleRearmDistance).
        if (&other == &hole && table::hasCup(hole) && hole.exitPosition.x == hole.cup.x &&
            hole.exitPosition.y == hole.cup.y) {
          continue;
        }
        const bool inBox = std::fabs(hole.exitPosition.x - other.center.x) <
                               other.halfSize.x + table::kBallRadius &&
                           std::fabs(hole.exitPosition.y - other.center.y) <
                               other.halfSize.y + table::kBallRadius;
        CHECK(!inBox);
      }
      if (table::isRamp(hole.type)) {
        // The rail starts at the mouth and ends at the exit.
        const auto& rail = table::rampRail(hole.type);
        CHECK(distance(rail.front(), hole.center) < 0.01F);
        CHECK(distance(rail.back(), hole.exitPosition) < 0.01F);
      }
    }
    // The two ramps mirror each other.
    CHECK(table::kHoles[table::kRunwayHole].center.x + table::kHoles[table::kSkywayHole].center.x ==
          2.0F * table::kFieldCenterX);
  }

  // The scoop and the hangar are pockets: a ball inside the box is taken
  // whatever its speed, held off the table, and cannot be taken again until
  // it has left. (The hangar is reached here by hand: in play its doors
  // keep the ball out.)
  for (const std::size_t i : {table::kScoopHole, table::kHangarHole}) {
    const table::Hole& hole = table::kHoles[i];
    for (const float speed : {40.0F, 900.0F, 2600.0F}) {
      PhysicsWorld world;
      // (Into the Mission scoop up and to the right, as it takes a ball.)
      const float across = hole.type == table::HoleType::Subway ? 0.3F * speed : 0.0F;
      world.resetBall({hole.center.x - (hole.type == table::HoleType::Subway ? 6.0F : 0.0F),
                       hole.center.y + hole.halfSize.y - 2.0F},
                      {across, -speed});
      const table::ElementRef captured = stepUntil(world, PhysicsEvent::Type::Capture, 20);
      CHECK(captured.kind == table::ElementKind::Hole);
      CHECK(captured.index == i);
      CHECK(world.ballHeld());
      CHECK(world.heldHole() == i);

      // Held: the ball does not move and nothing else happens to it.
      const Vec2 heldAt = world.ballPosition();
      for (int s = 0; s < 60; ++s) {
        world.step(kStep);
        CHECK(world.events().empty());
      }
      CHECK(distance(world.ballPosition(), heldAt) < 0.01F);
      CHECK(world.ballIsSane());

      // Released in place with no speed, it sits in the box but is not
      // swallowed again until it has rolled clear.
      world.releaseBall(hole.center, {0.0F, 0.0F});
      CHECK(!world.ballHeld());
      bool recaptured = false;
      for (int s = 0; s < 8; ++s) {
        world.step(kStep);
        recaptured = recaptured || world.ballHeld();
      }
      CHECK(!recaptured);
    }
  }

  {  // The Briefing room (revision 3) is reached from below along its
     // slanted channel, up and to the right, at any speed that gets there;
     // its subway sets the ball down far below its mouth.
    const table::Hole& scoop = table::kHoles[table::kScoopHole];
    for (const float speed : {900.0F, 1500.0F, 2400.0F}) {
      PhysicsWorld world;
      world.resetBall({410.0F, 520.0F}, {0.28F * speed, -0.96F * speed});
      const table::ElementRef captured = stepUntil(world, PhysicsEvent::Type::Capture, 60);
      CHECK(captured.kind == table::ElementKind::Hole && captured.index == table::kScoopHole);
    }
    CHECK(scoop.exitPosition.y > table::kBriefingLeft.a.y + table::kBallRadius);
  }

  // A ramp takes only a ball moving up the table fast enough, anywhere
  // across its mouth, and not beside it.
  for (const std::size_t ramp : {table::kRunwayHole, table::kSkywayHole}) {
    const table::Hole& hole = table::kHoles[ramp];
    // Starts 50 px below the mouth, aimed to cross it at centre + offset.
    const auto shootAt = [&](float offset, Vec2 velocity) {
      PhysicsWorld world;
      const float drift = 50.0F * velocity.x / velocity.y;
      world.resetBall({hole.center.x + offset + drift, hole.center.y + 50.0F}, velocity);
      return stepUntil(world, PhysicsEvent::Type::Capture, 30);
    };
    const auto taken = [ramp](table::ElementRef e) {
      return e.kind == table::ElementKind::Hole && e.index == ramp;
    };
    const Vec2 shot{0.0F, -1500.0F};
    CHECK(taken(shootAt(0.0F, shot)));
    CHECK(taken(shootAt(hole.halfSize.x - 4.0F, shot)));
    CHECK(taken(shootAt(-(hole.halfSize.x - 4.0F), shot)));
    // Leaning either way: there is no test of direction, only of speed.
    CHECK(taken(shootAt(0.0F, {-300.0F, -1500.0F})));
    CHECK(taken(shootAt(0.0F, {300.0F, -1500.0F})));
    // Just fast enough at the mouth, and just too slow.
    CHECK(taken(shootAt(0.0F, {0.0F, -(table::kRampMinUpwardSpeed + 120.0F)})));
    CHECK(shootAt(0.0F, {0.0F, -(table::kRampMinUpwardSpeed - 20.0F)}).kind ==
          table::ElementKind::None);
    {  // Falling back down through the mouth: not taken.
      PhysicsWorld world;
      world.resetBall({hole.center.x, hole.center.y - 50.0F}, {0.0F, 300.0F});
      const table::ElementRef captured = stepUntil(world, PhysicsEvent::Type::Capture, 40);
      CHECK(captured.kind == table::ElementKind::None);
    }
    {  // The fastest ball the table allows cannot step over the mouth.
      PhysicsWorld world;
      const float fastest = PhysicsWorld::kMaxBallSpeed * PhysicsWorld::kPixelsPerMeter;
      world.resetBall({hole.center.x, hole.center.y + 43.0F}, {0.0F, -fastest});
      CHECK(taken(stepUntil(world, PhysicsEvent::Type::Capture, 10)));
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
    if (enters != 1) {
      std::printf("rollover %zu entered %zu times\n", i, enters);
    }
    CHECK(enters == 1);
  }

  // A rollover spans its lane: a ball that runs down the lane is counted
  // wherever it is across the width, including hard against either wall.
  // The drawn circle is narrower than the return lanes and used to miss a
  // ball hugging the outer wall.
  for (std::size_t i = 0; i < table::kRollovers.size(); ++i) {
    const Vec2 c = table::kRollovers[i].zone.center;
    const Vec2 half = table::rolloverHalfSize(table::kRollovers[i]);
    int passes = 0;
    for (float offset = -half.x; offset <= half.x; offset += 1.0F) {
      PhysicsWorld world;
      // Starts that overlap a wall are pushed into the lane by the solver.
      world.resetBall({c.x + offset, c.y - 30.0F}, {0.0F, 250.0F});
      std::size_t enters = 0;
      bool stayedInLane = true;
      bool through = false;
      for (int s = 0; s < 60 && !through; ++s) {
        world.step(kStep);
        for (const PhysicsEvent& e : world.events()) {
          if (e.type == PhysicsEvent::Type::Enter && e.element.index == i) {
            ++enters;
          }
        }
        const Vec2 p = world.ballPosition();
        stayedInLane = stayedInLane && std::fabs(p.x - c.x) < half.x;
        through = p.y > c.y + half.y + 5.0F;
      }
      if (through && stayedInLane) {
        ++passes;
        CHECK(enters == 1);
      }
    }
    // Most of the lane's width is clear of its walls.
    CHECK(passes >= static_cast<int>(half.x));
  }

  {  // The switches of neighbouring lanes do not overlap.
    for (std::size_t i = 0; i < table::kRollovers.size(); ++i) {
      for (std::size_t j = i + 1; j < table::kRollovers.size(); ++j) {
        const Vec2 a = table::kRollovers[i].zone.center;
        const Vec2 b = table::kRollovers[j].zone.center;
        const Vec2 ha = table::rolloverHalfSize(table::kRollovers[i]);
        const Vec2 hb = table::rolloverHalfSize(table::kRollovers[j]);
        CHECK(std::fabs(a.x - b.x) >= ha.x + hb.x || std::fabs(a.y - b.y) >= ha.y + hb.y);
      }
    }
  }

  {  // The game's hold times and counters.
    const double holdSteps = table::kScoopHoldSeconds / Game::kStepSeconds;
    CHECK(holdSteps > 30.0 && holdSteps < 600.0);
    CHECK(Game::holdSeconds(table::HoleType::Subway) == table::kScoopHoldSeconds);
    CHECK(Game::holdSeconds(table::HoleType::Hangar) == table::kHangarHoldSeconds);
    CHECK(Game::holdSeconds(table::HoleType::RampLeft) == table::kRampRideSeconds);
    CHECK(Game::holdSeconds(table::HoleType::RampRight) == table::kRampRideSeconds);
    CHECK(Game().holeCount(999) == 0);
    CHECK(Game().rolloverCount(999) == 0);
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

  // Every exit is safe with the flippers at rest: the released ball meets a
  // flipper or runs down an inlane before anything else, and it does not
  // leave between the flippers untouched. A little off the exact release
  // each time.
  for (std::size_t h = 0; h < table::kHoles.size(); ++h) {
    const table::Hole& hole = table::kHoles[h];
    // The Flight Deck's set-down is on the deck, above its own flipper
    // (concept A: GameTest's deck block checks it).
    if (hole.type == table::HoleType::DeckRamp) {
      continue;
    }
    for (int j = 0; j < 12; ++j) {
      PhysicsWorld world;
      const float dx = (static_cast<float>(j % 4) - 1.5F) * 0.3F;
      const float dv = (static_cast<float>(j / 4) - 1.0F) * 8.0F;
      world.resetBall({hole.exitPosition.x + dx, hole.exitPosition.y},
                      {hole.exitVelocity.x + dv, hole.exitVelocity.y});
      // As the hole lets it go: it does not take it straight back.
      world.disarmHolesNear(0, hole.exitPosition);
      bool safe = false;
      for (int s = 0; s < 120 * 6 && !safe; ++s) {
        world.step(kStep);
        const Vec2 p = world.ballPosition();
        for (const PhysicsEvent& e : world.events()) {
          safe = safe ||
                 (e.type == PhysicsEvent::Type::Enter &&
                  table::kRollovers[e.element.index].group == table::RolloverGroup::Inlane);
          // Nothing else may come first.
          CHECK(safe);
        }
        for (const table::Flipper& flipper : table::kFlippers) {
          const float along = std::fabs(p.x - flipper.pivot.x);
          safe = safe || (along < table::kFlipperLength && p.y > flipper.pivot.y - 30.0F &&
                          p.y < flipper.pivot.y + 60.0F);
        }
        CHECK(p.y < table::kDrainY);
      }
      CHECK(safe);
    }
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
    // One ball throughout: the Hangar returns the ball and does not lock it.
    pinball::GameTestAccess::rules(game).setMultiballEnabled(false);
    int stillSteps = 0;
    int worstStill = 0;
    Vec2 previous{};
    Vec2 worstAt{};
    std::uint64_t trajectory = 0xcbf29ce484222325ULL;
    for (int i = 0; i < 120 * 600; ++i) {
      InputState input;
      input.plunger = (i % 900) < 100 + (i / 900) % 60;
      input.leftFlipper = (i / 29) % 2 == 0;
      input.rightFlipper = (i / 41) % 2 == 0;
      input.newGame = game.gameOver();
      game.step(input);

      const pinball::BallView ball = game.ball();
      mixInto(trajectory, std::bit_cast<std::uint32_t>(ball.position.x));
      mixInto(trajectory, std::bit_cast<std::uint32_t>(ball.position.y));
      mixInto(trajectory, game.rules().score());
      mixInto(trajectory, game.drainCount());
      // One ball, start to finish.
      if (game.ballCount() != 1 || ball.id != 0) {
        trajectory = 0;
      }
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
    // (Fewer drains than there were before the kickback and the short save
    // after an eject: each keeps a ball in play.)
    CHECK(game.drainCount() >= 12);
    CHECK(game.bumperHits() > 12);

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

    // The single-ball game is exactly the game it was.
    if (trajectory != kSoakTrajectory) {
      std::fprintf(stderr, "soak trajectory 0x%016llx, score %llu\n",
                   static_cast<unsigned long long>(trajectory),
                   static_cast<unsigned long long>(game.rules().score()));
    }
    CHECK(trajectory == kSoakTrajectory);
  }
}
