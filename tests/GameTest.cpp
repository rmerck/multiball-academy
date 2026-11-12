#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

#include "Check.hpp"
#include "Suites.hpp"
#include "core/Game.hpp"
#include "core/TableLayout.hpp"

namespace {

using pinball::BallView;
using pinball::Game;
using pinball::InputState;
using pinball::Vec2;
namespace table = pinball::table;

float distance(Vec2 a, Vec2 b) {
  const Vec2 d = a - b;
  return std::sqrt(d.x * d.x + d.y * d.y);
}

bool same(const BallView& a, const BallView& b) {
  return a.position.x == b.position.x && a.position.y == b.position.y && a.angle == b.angle &&
         a.visible == b.visible && a.scale == b.scale;
}

// A simple player: launches the ball, and raises a flipper for a moment
// whenever the ball comes down to it. How early it reacts and how long it
// holds change every minute or so, so that play cannot settle into a loop
// whatever the solver does on a given platform.
InputState play(const Game& game, std::uint64_t step) {
  InputState input;
  const Vec2 p = game.ball().position;
  input.plunger = !game.laneGateClosed() && (step % 240U) < 150U;
  const float reactY = 780.0F + 10.0F * static_cast<float>((step / 4800U) % 5U);
  const auto within = [p, reactY](const table::Flipper& flipper, float direction) {
    const float along = (p.x - flipper.pivot.x) * direction;
    return along > -14.0F && along < 104.0F && p.y > reactY && p.y < 905.0F;
  };
  // Let go regularly so the bat comes down for the next ball.
  const std::uint64_t period = 20U + 4U * ((step / 7200U) % 4U);
  const bool pulse = (step % period) < period * 3U / 5U;
  input.leftFlipper = pulse && within(table::kLeftFlipper, 1.0F);
  input.rightFlipper = pulse && within(table::kRightFlipper, -1.0F);
  input.newGame = game.gameOver();
  return input;
}

}  // namespace

void testGame() {
  {  // Time is derived from the step count and does not drift.
    Game game;
    const InputState input;
    for (int i = 0; i < 120 * 600; ++i) {
      game.step(input);
    }
    CHECK(game.stepCount() == 120U * 600U);
    CHECK(std::fabs(game.simTime() - 600.0) < 1e-9);
  }

  {  // Drawing between steps: before the first step there is one state only.
    const Game game;
    CHECK(same(game.ball(0.0F), game.ball()));
    CHECK(same(game.ball(0.5F), game.ball()));
    CHECK(game.flipper(0, 0.0F).angle == game.flipper(0).angle);
    CHECK(game.flipper(1, 0.3F).angle == game.flipper(1).angle);
  }

  {  // Alpha 0 is the state one step ago, 1 the latest, and between them the
     // ball and the flippers lie on the line from one to the other.
    Game game;
    InputState input;
    input.plunger = true;
    for (int i = 0; i < 120; ++i) {
      game.step(input);
    }
    input.plunger = false;
    input.leftFlipper = true;
    int moving = 0;
    for (int i = 0; i < 40; ++i) {
      const BallView before = game.ball();
      const float flipperBefore = game.flipper(0).angle;
      game.step(input);
      const BallView now = game.ball();
      CHECK(same(game.ball(1.0F), now));
      CHECK(same(game.ball(0.0F), before));
      const BallView half = game.ball(0.5F);
      CHECK(distance(half.position, (before.position + now.position) * 0.5F) < 0.01F);
      const BallView quarter = game.ball(0.25F);
      CHECK(std::fabs(distance(quarter.position, before.position) -
                      0.25F * distance(now.position, before.position)) < 0.01F);
      CHECK(game.flipper(0, 0.0F).angle == flipperBefore);
      CHECK(game.flipper(0, 1.0F).angle == game.flipper(0).angle);
      CHECK(std::fabs(game.flipper(0, 0.5F).angle -
                      0.5F * (flipperBefore + game.flipper(0).angle)) < 1.0e-5F);
      // The idle flipper does not move, and the pivot is never blended.
      CHECK(game.flipper(1, 0.5F).angle == game.flipper(1).angle);
      CHECK(game.flipper(0, 0.5F).pivot.x == table::kLeftFlipper.pivot.x);
      // Out of range and bad values: clamped, and not-a-number is the latest.
      CHECK(same(game.ball(-3.0F), before));
      CHECK(same(game.ball(7.0F), now));
      CHECK(same(game.ball(std::numeric_limits<float>::quiet_NaN()), now));
      CHECK(game.flipper(0, std::numeric_limits<float>::quiet_NaN()).angle ==
            game.flipper(0).angle);
      CHECK(game.flipper(0, -1.0F).angle == flipperBefore);
      CHECK(game.flipper(9, 0.5F).angle == 0.0F);
      moving += distance(now.position, before.position) > 1.0F ? 1 : 0;
      moving += game.flipper(0).angle != flipperBefore ? 1 : 0;
    }
    // The launch and the flipper stroke were really under way.
    CHECK(moving > 30);
  }

  {  // The spinning ball's angle is blended the short way round its wrap.
    Game game;
    for (std::uint64_t i = 0; i < 120U * 60U; ++i) {
      const float before = game.ball().angle;
      game.step(play(game, i));
      const BallView now = game.ball();
      if (!now.visible || !game.ball(0.0F).visible) {
        continue;
      }
      const float half = game.ball(0.5F).angle;
      const float turn = std::remainder(now.angle - before, 2.0F * table::kPi);
      if (game.ball(0.0F).angle == before) {
        CHECK(std::fabs(std::remainder(half - before, 2.0F * table::kPi) - 0.5F * turn) < 1.0e-3F);
      }
    }
  }

  {  // Through a whole game with a player at the flippers: the picture never
     // shows the ball part-way along a jump (a serve, a hole's return), and
     // a ball on the ramp is carried in sight from the mouth to the exit.
    const auto ramp = static_cast<std::size_t>(
        std::ranges::find(table::kHoles, table::HoleType::Ramp, &table::Hole::type) -
        table::kHoles.begin());
    CHECK(ramp < table::kHoles.size());
    const table::Hole& rampHole = table::kHoles[ramp];
    // The furthest a ball can travel in a step, plus the first step of a
    // ramp ride and some slack.
    constexpr float kLongestMove = 3000.0F / 120.0F + 8.0F;

    Game game;
    std::uint32_t drains = 0;
    std::uint32_t captures = 0;
    std::uint32_t rampRides = 0;
    int hiddenSteps = 0;
    int rideSteps = 0;
    float rideTop = 0.0F;
    float rideLastY = 0.0F;
    bool riding = false;
    for (std::uint64_t i = 0; i < 120U * 600U && (rampRides < 2 || drains < 3); ++i) {
      const std::uint32_t rampBefore = game.holeCount(ramp);
      game.step(play(game, i));
      const BallView from = game.ball(0.0F);
      const BallView now = game.ball();
      if (from.visible && now.visible) {
        CHECK(distance(from.position, now.position) < kLongestMove);
      }
      CHECK(same(game.ball(1.0F), now));

      if (game.drainCount() != drains) {
        // Served this step: drawn on the plunger whatever the alpha.
        drains = game.drainCount();
        CHECK(same(game.ball(0.0F), now));
        CHECK(distance(now.position, table::kBallStart) < 2.0F);
      }
      hiddenSteps += now.visible ? 0 : 1;

      if (game.holeCount(ramp) != rampBefore) {
        // Taken somewhere across the mouth.
        CHECK(game.ballHeld());
        CHECK(std::fabs(now.position.x - rampHole.center.x) < table::kRampMouthReach + 2.0F);
        CHECK(std::fabs(now.position.y - rampHole.center.y) < table::kHoleCaptureRadius + 6.0F);
        riding = true;
        rideSteps = 0;
        rideTop = 1.0F;
        rideLastY = now.position.y;
      } else if (riding && game.ballHeld()) {
        // Always in sight, always moving up the table towards the exit, and
        // never outside the span between the mouth and the exit.
        CHECK(now.visible);
        CHECK(now.position.y < rideLastY);
        CHECK(now.position.y > rampHole.exitPosition.y - 0.01F);
        CHECK(now.position.x > rampHole.center.x - table::kRampMouthReach - 2.0F);
        CHECK(now.position.x < rampHole.center.x + table::kRampMouthReach + 2.0F);
        CHECK(now.scale >= 1.0F && now.scale <= 1.351F);
        rideLastY = now.position.y;
        rideTop = std::max(rideTop, now.scale);
        ++rideSteps;
      } else if (riding) {
        // Let go at the exit, full size, and not drawn back along the ride.
        riding = false;
        ++rampRides;
        CHECK(distance(now.position, rampHole.exitPosition) < 2.0F);
        CHECK(now.scale == 1.0F && now.visible);
        CHECK(same(game.ball(0.0F), now));
        CHECK(distance(Vec2{rampHole.exitPosition.x, rideLastY}, rampHole.exitPosition) < 8.0F);
        CHECK(rideTop > 1.3F);
        CHECK(rideSteps > 60);
      }
      captures = 0;
      for (std::size_t h = 0; h < table::kHoles.size(); ++h) {
        captures += game.holeCount(h);
      }
    }
    CHECK(drains >= 2);
    CHECK(rampRides >= 1);
    // Other holes were visited too, and they hide the ball.
    CHECK(captures == rampRides || hiddenSteps > 0);
  }
}
