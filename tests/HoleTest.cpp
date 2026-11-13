#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
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

// The scripted soak below, reduced to one number: FNV-1a over the bits of
// the ball's position, the score and the drain count after every one of its
// 72,000 steps. It pins the whole single-ball game: physics, serve, holes,
// rules. It must change only together with a change that is meant to alter
// how the ball moves or scores (table geometry, a physics constant, a rule
// the script reaches, a new Box2D); a refactoring that changes it has
// changed the game. To regenerate, run core_tests: on a mismatch it prints
// the value it got.
//
// Recorded on x86-64, where g++ 13 and clang 18 give the same value with and
// without optimisation and under the sanitizers. Other processors may round
// differently (a compiler is free to fuse a multiply and an add where the
// processor has an instruction for it), so there the value is printed and
// only checked once it has been recorded here.
#if defined(__x86_64__) || defined(_M_X64)
constexpr std::uint64_t kSoakTrajectory = 0xe84f9acc5335bb8fULL;
constexpr bool kSoakTrajectoryKnown = true;
#else
constexpr std::uint64_t kSoakTrajectory = 0;
constexpr bool kSoakTrajectoryKnown = false;
#endif

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

  {  // The ramp takes only a ball shot firmly up the table and leaning left,
     // into the mouth that faces the right flipper.
    constexpr auto ramp = static_cast<std::size_t>(
        std::ranges::find(table::kHoles, table::HoleType::Ramp, &table::Hole::type) -
        table::kHoles.begin());
    static_assert(ramp < table::kHoles.size());
    const Vec2 entry = table::kHoles[ramp].center;
    // A shot like the right flipper's: 1500 px/s up, leaning 0.4 to the left.
    const Vec2 shot{-600.0F, -1500.0F};
    // Starts 50 px below the mouth, placed so it crosses at entry.x + offset.
    const auto shootAt = [&](float offset, Vec2 velocity) {
      PhysicsWorld world;
      const float drift = 50.0F * velocity.x / velocity.y;
      world.resetBall({entry.x + offset + drift, entry.y + 50.0F}, velocity);
      return stepUntil(world, PhysicsEvent::Type::Capture, 30);
    };
    const auto taken = [](table::ElementRef e) {
      return e.kind == table::ElementKind::Hole && e.index == ramp;
    };
    // Taken anywhere across the mouth, and not beside it (a wormhole lies
    // beyond the left end, so only the ramp is ruled out there).
    CHECK(taken(shootAt(0.0F, shot)));
    CHECK(taken(shootAt(table::kRampMouthReach - 4.0F, shot)));
    CHECK(taken(shootAt(-(table::kRampMouthReach - 4.0F), shot)));
    CHECK(!taken(shootAt(table::kRampMouthReach + 4.0F, shot)));
    CHECK(!taken(shootAt(-(table::kRampMouthReach + 4.0F), shot)));
    // Too slow to climb it, though aimed the same way.
    CHECK(shootAt(0.0F, {-360.0F, -900.0F}).kind == table::ElementKind::None);
    // Fast, but straight up the table: that is the left flipper's shot.
    CHECK(shootAt(0.0F, {-150.0F, -1700.0F}).kind == table::ElementKind::None);
    CHECK(shootAt(0.0F, {300.0F, -1500.0F}).kind == table::ElementKind::None);
    {  // Falling back down through the mouth: not taken.
      PhysicsWorld world;
      world.resetBall({entry.x, entry.y - 50.0F}, {0.0F, 300.0F});
      const table::ElementRef captured = stepUntil(world, PhysicsEvent::Type::Capture, 40);
      CHECK(captured.kind == table::ElementKind::None);
    }
    {  // The fastest ball the table allows cannot step over the mouth.
      PhysicsWorld world;
      const float fastest = PhysicsWorld::kMaxBallSpeed * PhysicsWorld::kPixelsPerMeter;
      world.resetBall({entry.x + 16.0F, entry.y + 43.0F}, {-0.35F * fastest, -0.93F * fastest});
      CHECK(taken(stepUntil(world, PhysicsEvent::Type::Capture, 10)));
    }

    // The shot itself. A ball rolls down an inlane onto the flipper (the
    // natural feed) or is cradled and let go, and the flipper is triggered
    // at every step in turn while the ball is on the bat. The ramp must be
    // makeable over a few consecutive steps from the right flipper: a timing
    // window of two steps (17 ms) at the least, but not most of the bat. Only the width of
    // the window is asserted; which steps it falls on depends on the solver.
    struct Window {
      int longest = 0;  // Most consecutive trigger steps that made the ramp.
      int total = 0;
    };
    const auto sweep = [](std::size_t side, bool cradled, float startOffset = 0.0F) {
      const Vec2 inlane = side == table::kLeftFlipperIndex ? table::kLeftInlane
                                                           : table::mirrored(table::kLeftInlane);
      const int cradleSteps = cradled ? 360 : 0;
      Window window;
      int run = 0;
      for (int trigger = 0; trigger < 240; ++trigger) {
        PhysicsWorld world;
        world.setLaneGateClosed(true);
        world.resetBall({inlane.x + startOffset, inlane.y}, {0.0F, 0.0F});
        const int flipAt = cradleSteps + trigger;
        bool made = false;
        bool lost = false;
        // Follow the ball for a second after the flip: long enough to reach
        // the mouth, too short for a second visit to the flippers.
        for (int s = 0; s < flipAt + 120 && !made && !lost; ++s) {
          const bool held = s < cradleSteps || (s >= flipAt && s < flipAt + 40);
          world.setFlippers(side == table::kLeftFlipperIndex && held,
                            side == table::kRightFlipperIndex && held);
          world.step(kStep);
          for (const PhysicsEvent& e : world.events()) {
            if (e.type == PhysicsEvent::Type::Capture) {
              made = s >= flipAt && e.element.index == ramp;
              lost = !made;
            }
          }
          // Rolled off the end of the bat before the flip.
          lost = lost || (s < flipAt && world.ballPosition().y > table::kHeight);
        }
        run = made ? run + 1 : 0;
        window.longest = std::max(window.longest, run);
        window.total += made ? 1 : 0;
      }
      return window;
    };
    // Where the window falls on the bat, and so how many whole steps fit in
    // it, shifts with the smallest change in the feed: a hundredth of a pixel
    // in the start moves it between two and five steps. One start position
    // would sit on that edge, so several are tried: every one must leave a
    // window, and the best must be three steps or more.
    int bestRolling = 0;
    for (const float offset : {-0.2F, -0.1F, 0.0F, 0.1F, 0.2F}) {
      const Window rolling = sweep(table::kRightFlipperIndex, false, offset);
      CHECK(rolling.longest >= 2);
      CHECK(rolling.longest <= 7);
      bestRolling = std::max(bestRolling, rolling.longest);
    }
    CHECK(bestRolling >= 3);
    const Window cradled = sweep(table::kRightFlipperIndex, true);
    CHECK(cradled.longest >= 3);
    CHECK(cradled.longest <= 9);
    // From these feeds it is the right flipper's shot. The left one drives a
    // ball that was rolling along it or cradled on it straight up through
    // the mouth, which does not take it. (A falling ball struck late in the
    // left flipper's stroke can go in; that is not covered here.)
    CHECK(sweep(table::kLeftFlipperIndex, false).total <= 1);
    CHECK(sweep(table::kLeftFlipperIndex, true).total <= 1);
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

  // A rollover spans its lane: a ball that runs down the lane is counted
  // wherever it is across the width, including hard against either wall.
  // The drawn circle is narrower than the return lanes and used to miss a
  // ball hugging the outer wall.
  for (std::size_t i = 0; i < table::kRollovers.size(); ++i) {
    const Vec2 c = table::kRollovers[i].zone.center;
    int passes = 0;
    for (float offset = -16.0F; offset <= 16.0F; offset += 1.0F) {
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
        stayedInLane = stayedInLane && std::fabs(p.x - c.x) < table::kRolloverReach;
        through = p.y > c.y + table::kRolloverRadius + 5.0F;
      }
      if (through && stayedInLane) {
        ++passes;
        CHECK(enters == 1);
      }
    }
    CHECK(passes >= 15);
  }

  {  // The switches of neighbouring lanes do not overlap.
    for (std::size_t i = 0; i < table::kRollovers.size(); ++i) {
      for (std::size_t j = i + 1; j < table::kRollovers.size(); ++j) {
        const Vec2 a = table::kRollovers[i].zone.center;
        const Vec2 b = table::kRollovers[j].zone.center;
        CHECK(std::fabs(a.x - b.x) > 2.0F * table::kRolloverReach ||
              std::fabs(a.y - b.y) > 2.0F * table::kRolloverRadius);
      }
    }
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

  {  // No pocket between the arch deflector and the first lane post: their
     // surfaces are more than a ball apart, and balls dropped there move on.
    const Vec2 tip = table::kArchDeflector.b;
    const table::Capsule post = table::lanePost(-1.5F);
    CHECK(distance(tip, post.a) - table::kArchDeflector.radius - post.radius >
          2.0F * table::kBallRadius + 4.0F);
    for (const float x : {252.0F, 263.0F, 268.0F, 281.0F}) {
      PhysicsWorld world;
      world.resetBall({x, 90.0F}, {0.0F, 0.0F});
      // It must leave the area. Where it is later does not matter: a bumper
      // may well send it back up, and that differs between platforms.
      bool movedOn = false;
      for (int s = 0; s < 120 * 5 && !movedOn; ++s) {
        world.step(kStep);
        movedOn = world.ballHeld() || world.ballPosition().y > 190.0F;
      }
      CHECK(movedOn);
    }
  }

  {  // The hyperspace chute returns the ball to the left flipper, not down
     // the middle: released untouched, it meets the bat.
    const auto chute = static_cast<std::size_t>(
        std::ranges::find(table::kHoles, table::HoleType::Hyperspace, &table::Hole::type) -
        table::kHoles.begin());
    CHECK(chute < table::kHoles.size());
    PhysicsWorld world;
    // Drop it in, then let the chute return it the way the game does.
    world.resetBall(table::kHoles[chute].center, {0.0F, 0.0F});
    world.step(kStep);
    CHECK(world.ballHeld());
    world.releaseBall(table::kHoles[chute].exitPosition, table::kHoles[chute].exitVelocity);
    bool reachedBat = false;
    for (int s = 0; s < 240 && !reachedBat; ++s) {
      world.step(kStep);
      const Vec2 p = world.ballPosition();
      const Vec2 pivot = table::kLeftFlipper.pivot;
      reachedBat = p.x > pivot.x && p.x < pivot.x + table::kFlipperLength && p.y > pivot.y - 40.0F &&
                   p.y < pivot.y + 60.0F;
    }
    CHECK(reachedBat);
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

    // The single-ball game is exactly the game it was.
    if (!kSoakTrajectoryKnown || trajectory != kSoakTrajectory) {
      std::fprintf(stderr, "soak trajectory 0x%016llx, score %llu%s\n",
                   static_cast<unsigned long long>(trajectory),
                   static_cast<unsigned long long>(game.rules().score()),
                   kSoakTrajectoryKnown ? "" : " (not recorded for this processor)");
    }
    CHECK(!kSoakTrajectoryKnown || trajectory == kSoakTrajectory);
  }
}
