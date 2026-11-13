#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string_view>

#include "Check.hpp"
#include "Suites.hpp"
#include "core/Game.hpp"
#include "core/Rules.hpp"
#include "core/Sound.hpp"
#include "core/TableLayout.hpp"

// The seam Game leaves for tests: puts the ball somewhere by hand.
struct pinball::GameTestAccess {
  static void placeBall(Game& game, Vec2 position) {
    game.physics_.resetBall(position, {0.0F, 0.0F});
  }
  // As if the ball had just left the launch lane.
  static void putInPlay(Game& game) {
    game.physics_.setLaneGateClosed(true);
    game.rules_.onBallInPlay();
  }
  // The same for any ball, with a velocity.
  static void placeBall(Game& game, std::size_t ball, Vec2 position, Vec2 velocity = {}) {
    game.physics_.resetBall(ball, position, velocity);
  }
  // Puts one more ball on the table. Returns its id, or Game::kMaxBalls.
  static std::size_t addBall(Game& game, Vec2 position, Vec2 velocity = {}) {
    return game.addBall(position, velocity);
  }
  static bool live(const Game& game, std::size_t ball) { return game.physics_.ballActive(ball); }
  static bool held(const Game& game, std::size_t ball) { return game.physics_.ballHeld(ball); }
  static Vec2 velocity(const Game& game, std::size_t ball) {
    return game.physics_.ballVelocity(ball);
  }
  static bool passesGate(const Game& game, std::size_t ball) {
    return game.physics_.ballPassesGate(ball);
  }
};

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

using Seam = pinball::GameTestAccess;

// An open stretch of the middle of the field, clear of everything.
constexpr Vec2 kOpenField{table::kFieldCenterX, 510.0F};
constexpr float kSpeedCap =
    pinball::PhysicsWorld::kMaxBallSpeed * pinball::PhysicsWorld::kPixelsPerMeter;

std::size_t holeOfType(table::HoleType type) {
  return static_cast<std::size_t>(std::ranges::find(table::kHoles, type, &table::Hole::type) -
                                  table::kHoles.begin());
}

// The view of the ball with this id. If it is not on the table the result
// is hidden and carries the id kNoBall.
constexpr std::uint8_t kNoBall = 255;
BallView find(const pinball::BallViews& views, std::size_t id) {
  for (const BallView& view : views) {
    if (view.id == id) {
      return view;
    }
  }
  BallView missing;
  missing.visible = false;
  missing.id = kNoBall;
  return missing;
}

// What must hold after every step however many balls there are: one to
// three of them, in id order, each with a finite state inside the speed cap.
bool ballsAreSound(const Game& game) {
  const pinball::BallViews views = game.balls();
  bool sound = views.count >= 1 && views.count <= Game::kMaxBalls &&
               views.count == game.ballCount() && views.span().size() == views.count;
  for (std::size_t i = 0; i < views.count; ++i) {
    const BallView& view = views.items[i];
    const Vec2 v = Seam::velocity(game, view.id);
    sound = sound && view.id < Game::kMaxBalls && Seam::live(game, view.id) &&
            (i == 0 || views.items[i - 1].id < view.id) && std::isfinite(view.position.x) &&
            std::isfinite(view.position.y) && std::isfinite(view.angle) &&
            std::isfinite(view.scale) && std::isfinite(v.x) && std::isfinite(v.y) &&
            std::sqrt(v.x * v.x + v.y * v.y) <= kSpeedCap * 1.001F;
  }
  return sound && same(game.ball(), views.items[0]) && game.ball().id == views.items[0].id;
}

// The deepest any two balls on the table overlap, in pixels. A ball in a
// hole is off the table.
float deepestOverlap(const Game& game) {
  const pinball::BallViews views = game.balls();
  float deepest = 0.0F;
  for (std::size_t a = 0; a < views.count; ++a) {
    for (std::size_t b = a + 1; b < views.count; ++b) {
      if (Seam::held(game, views.items[a].id) || Seam::held(game, views.items[b].id)) {
        continue;
      }
      deepest = std::max(deepest, 2.0F * table::kBallRadius -
                                      distance(views.items[a].position, views.items[b].position));
    }
  }
  return deepest;
}

// The soak script of the hole tests: launches and flips on a fixed rhythm.
InputState soakInput(const Game& game, int step) {
  InputState input;
  input.plunger = (step % 900) < 100 + (step / 900) % 60;
  input.leftFlipper = (step / 29) % 2 == 0;
  input.rightFlipper = (step / 41) % 2 == 0;
  input.newGame = game.gameOver();
  return input;
}

// Brings the table up to three balls, dropping the new ones into the middle
// of the field wherever there is room. Returns how many were added.
int fillTable(Game& game) {
  constexpr Vec2 kSpots[] = {{kOpenField.x - 60.0F, kOpenField.y},
                             {kOpenField.x + 60.0F, kOpenField.y},
                             kOpenField,
                             {kOpenField.x - 120.0F, kOpenField.y},
                             {kOpenField.x + 120.0F, kOpenField.y}};
  int added = 0;
  for (const Vec2 spot : kSpots) {
    if (game.ballCount() >= Game::kMaxBalls) {
      break;
    }
    bool room = true;
    for (const BallView& view : game.balls()) {
      room = room && distance(view.position, spot) > 4.0F * table::kBallRadius;
    }
    if (room && Seam::addBall(game, spot, {spot.x < kOpenField.x ? -90.0F : 90.0F, -200.0F}) <
                    Game::kMaxBalls) {
      ++added;
    }
  }
  return added;
}

// The game with more than one ball on the table. No rule puts a second ball
// there yet, so the tests add them through the seam.
void testSeveralBalls() {
  {  // One ball to begin with; more appear in id order and the primary ball
     // is the lowest of them.
    Game game;
    CHECK(Game::kMaxBalls == table::kMaxBalls && Game::kMaxBalls == 3);
    CHECK(game.ballCount() == 1);
    pinball::BallViews views = game.balls();
    CHECK(views.count == 1 && views.size() == 1 && !views.empty());
    CHECK(views.items[0].id == 0 && same(views.items[0], game.ball()));
    CHECK(views.end() - views.begin() == 1);
    CHECK(ballsAreSound(game));

    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(Seam::addBall(game, {nan, 300.0F}) == Game::kMaxBalls);
    CHECK(game.ballCount() == 1);
    CHECK(Seam::addBall(game, kOpenField) == 1);
    CHECK(Seam::addBall(game, kOpenField + Vec2{80.0F, 0.0F}) == 2);
    CHECK(Seam::addBall(game, kOpenField + Vec2{-80.0F, 0.0F}) == Game::kMaxBalls);  // Full.
    CHECK(game.ballCount() == 3);
    views = game.balls();
    CHECK(views.count == 3);
    CHECK(views.items[0].id == 0 && views.items[1].id == 1 && views.items[2].id == 2);
    CHECK(distance(views.items[1].position, kOpenField) < 0.01F);
    CHECK(distance(views.items[0].position, table::kBallStart) < 2.0F);
    CHECK(game.ball().id == 0 && same(game.ball(), views.items[0]));
    std::size_t seen = 0;
    for (const BallView& view : views) {
      CHECK(view.id == seen && view.visible && view.scale == 1.0F);
      ++seen;
    }
    CHECK(seen == 3);
    CHECK(ballsAreSound(game));

    // A new game clears the table down to one ball on the plunger.
    InputState input;
    game.step(input);
    input.newGame = true;
    game.step(input);
    CHECK(game.ballCount() == 1 && game.balls().count == 1);
    CHECK(game.ball().id == 0 && distance(game.ball().position, table::kBallStart) < 2.0F);
    CHECK(!game.laneGateClosed());
    CHECK(ballsAreSound(game));
  }

  {  // Each ball is drawn between its own last two places, and one that has
     // just been put on the table is drawn where it is.
    Game game;
    const InputState input;
    Seam::placeBall(game, kOpenField + Vec2{-70.0F, 0.0F});
    Seam::putInPlay(game);
    game.step(input);
    CHECK(Seam::addBall(game, kOpenField + Vec2{70.0F, 0.0F}, {200.0F, -300.0F}) == 1);
    // Not drawn on a line from anywhere else, at any alpha, before or after
    // the first step it takes part in.
    CHECK(same(find(game.balls(0.0F), 1), find(game.balls(), 1)));
    CHECK(distance(find(game.balls(0.3F), 1).position, kOpenField + Vec2{70.0F, 0.0F}) < 0.01F);
    for (int i = 0; i < 40; ++i) {
      const pinball::BallViews before = game.balls();
      game.step(input);
      const pinball::BallViews now = game.balls();
      const pinball::BallViews half = game.balls(0.5F);
      const pinball::BallViews start = game.balls(0.0F);
      CHECK(now.count == 2 && half.count == 2 && start.count == 2);
      for (std::size_t b = 0; b < 2; ++b) {
        CHECK(half.items[b].id == b && start.items[b].id == b);
        CHECK(same(game.balls(1.0F).items[b], now.items[b]));
        CHECK(same(start.items[b], before.items[b]));
        CHECK(distance(half.items[b].position,
                       (before.items[b].position + now.items[b].position) * 0.5F) < 0.01F);
        CHECK(distance(now.items[b].position, before.items[b].position) > 0.01F);
      }
      // ball(alpha) is the primary ball's entry.
      CHECK(same(game.ball(0.5F), half.items[0]));
    }
  }

  {  // A nudge shoves every ball on the table.
    Game game;
    InputState input;
    Seam::placeBall(game, kOpenField + Vec2{-70.0F, 0.0F});
    Seam::putInPlay(game);
    CHECK(Seam::addBall(game, kOpenField + Vec2{70.0F, 0.0F}) == 1);
    CHECK(Seam::addBall(game, kOpenField) == 2);
    game.step(input);
    std::array<float, 3> before{};
    for (std::size_t b = 0; b < 3; ++b) {
      before[b] = Seam::velocity(game, b).y;
    }
    input.nudgeUp = true;
    game.step(input);
    for (std::size_t b = 0; b < 3; ++b) {
      CHECK(Seam::velocity(game, b).y < before[b] - 0.8F * Game::kNudgeUpSpeed);
    }
  }

  {  // Losing one ball of several costs nothing but that ball. The last one
     // down the drain ends the ball exactly as a single ball does.
    Game game;
    Game alone;  // The same game with one ball throughout.
    const InputState input;
    for (Game* each : {&game, &alone}) {
      Seam::placeBall(*each, kOpenField + Vec2{-70.0F, 0.0F});
      Seam::putInPlay(*each);
    }
    CHECK(Seam::addBall(game, kOpenField + Vec2{70.0F, 0.0F}) == 1);
    // Past the six seconds in which the rules would give the ball back.
    for (int i = 0; i < 120 * 7; ++i) {
      for (Game* each : {&game, &alone}) {
        Seam::placeBall(*each, 0, kOpenField + Vec2{-70.0F, 0.0F});
        each->step(input);
      }
      Seam::placeBall(game, 1, kOpenField + Vec2{70.0F, 0.0F});
    }
    CHECK(!game.rules().ballSaveActive());
    const std::uint64_t score = game.rules().score();

    // The second ball drains.
    Seam::placeBall(game, 1, {table::kFieldCenterX, table::kDrainY + 5.0F});
    game.step(input);
    alone.step(input);
    CHECK(game.ballCount() == 1 && game.drainCount() == 1);
    CHECK(game.ball().id == 0 && distance(game.ball().position, table::kBallStart) > 100.0F);
    CHECK(game.laneGateClosed() && game.rules().ballInPlay());
    CHECK(game.rules().ball() == 1 && game.rules().score() == score);
    CHECK(std::string_view(game.rules().message()) == std::string_view(alone.rules().message()));

    // Another joins, and this time the first one drains: the primary ball
    // is then the one that is left.
    CHECK(Seam::addBall(game, kOpenField + Vec2{70.0F, 0.0F}) == 1);
    Seam::placeBall(game, 0, {table::kFieldCenterX, table::kDrainY + 5.0F});
    game.step(input);
    alone.step(input);
    CHECK(game.ballCount() == 1 && game.drainCount() == 2);
    CHECK(game.ball().id == 1 && game.balls().items[0].id == 1);
    CHECK(distance(game.ball().position, kOpenField + Vec2{70.0F, 0.0F}) < 5.0F);
    CHECK(game.ballHeld() == Seam::held(game, 1));
    CHECK(game.rules().ball() == 1 && game.rules().ballInPlay() && game.laneGateClosed());
    CHECK(ballsAreSound(game));

    // The last ball drains: the end of the ball, as ever.
    Seam::placeBall(game, 1, {table::kFieldCenterX, table::kDrainY + 5.0F});
    Seam::placeBall(alone, 0, {table::kFieldCenterX, table::kDrainY + 5.0F});
    game.step(input);
    alone.step(input);
    CHECK(game.ballCount() == 1 && game.drainCount() == 3 && alone.drainCount() == 1);
    CHECK(game.ball().id == 0 && distance(game.ball().position, table::kBallStart) < 2.0F);
    CHECK(same(game.ball(0.0F), game.ball()));  // Served: not drawn along the jump.
    CHECK(!game.laneGateClosed() && !Seam::passesGate(game, 0));
    CHECK(game.rules().ball() == alone.rules().ball() && game.rules().ball() == 2);
    CHECK(game.rules().ballInPlay() == alone.rules().ballInPlay());
    CHECK(game.rules().score() == alone.rules().score());
    CHECK(std::string_view(game.rules().message()) == std::string_view(alone.rules().message()));

    // Two down the drain in the same step: the first is simply lost, the
    // second ends the ball. One ball is served, not two.
    Seam::placeBall(game, kOpenField);
    Seam::putInPlay(game);
    CHECK(Seam::addBall(game, kOpenField + Vec2{70.0F, 0.0F}) == 1);
    game.step(input);
    Seam::placeBall(game, 0, {table::kFieldCenterX - 20.0F, table::kDrainY + 5.0F});
    Seam::placeBall(game, 1, {table::kFieldCenterX + 20.0F, table::kDrainY + 5.0F});
    game.step(input);
    CHECK(game.drainCount() == 5 && game.ballCount() == 1);
    CHECK(game.ball().id == 0 && distance(game.ball().position, table::kBallStart) < 2.0F);
    CHECK(!game.laneGateClosed());
    CHECK(ballsAreSound(game));
  }

  {  // Fail safe with several balls: the one lost outside the table is taken
     // away and the others play on. Nothing is served and nothing counted.
    Game game;
    const InputState input;
    Seam::placeBall(game, kOpenField + Vec2{-70.0F, 0.0F});
    Seam::putInPlay(game);
    CHECK(Seam::addBall(game, kOpenField) == 1);
    CHECK(Seam::addBall(game, kOpenField + Vec2{70.0F, 0.0F}) == 2);
    game.step(input);
    Seam::placeBall(game, 1, {-500.0F, 300.0F});
    game.step(input);
    CHECK(game.ballCount() == 2 && game.drainCount() == 0);
    const pinball::BallViews views = game.balls();
    CHECK(views.count == 2 && views.items[0].id == 0 && views.items[1].id == 2);
    CHECK(find(views, 1).id == kNoBall);
    CHECK(distance(views.items[0].position, kOpenField + Vec2{-70.0F, 0.0F}) < 10.0F);
    CHECK(game.laneGateClosed() && game.rules().ballInPlay() && game.rules().ball() == 1);
    // The freed slot is the next one used.
    CHECK(Seam::addBall(game, kOpenField) == 1);
    CHECK(ballsAreSound(game));
  }

  {  // A single ball is never given the pass through the lane gate.
    Game game;
    bool passed = false;
    for (int i = 0; i < 120 * 30; ++i) {
      game.step(soakInput(game, i));
      passed = passed || Seam::passesGate(game, 0);
    }
    CHECK(!passed);
    CHECK(game.laneGateClosed() || game.drainCount() > 0);
  }

  {  // With a ball already in play the gate is shut, and a ball waiting on
     // the plunger is let through it: it can still be launched, and once it
     // is out the gate is solid for it too.
    Game game;
    InputState input;
    game.step(input);
    CHECK(Seam::addBall(game, kOpenField) == 1);
    game.step(input);
    CHECK(game.laneGateClosed() && game.rules().ballInPlay());
    CHECK(Seam::passesGate(game, 0) && !Seam::passesGate(game, 1));
    input.plunger = true;
    for (int i = 0; i < 150; ++i) {
      game.step(input);
    }
    input.plunger = false;
    bool out = false;
    for (int i = 0; i < 360 && !out; ++i) {
      game.step(input);
      out = Seam::live(game, 0) && game.balls().items[0].id == 0 &&
            game.balls().items[0].position.x < table::kLaneGateCloseX;
    }
    CHECK(out);
    CHECK(game.laneGateClosed());
    CHECK(!Seam::passesGate(game, 0) && !Seam::passesGate(game, 1));
  }

  {  // Three balls into one hole. Each is scored as it arrives and held its
     // own 0.9 s; they leave one at a time, 0.3 s apart, and of two that are
     // both waiting the lower id goes first.
    Game game;
    const InputState input;
    const std::size_t hole = holeOfType(table::HoleType::BlackHole);
    const table::Hole& blackHole = table::kHoles[hole];
    Seam::placeBall(game, kOpenField);
    Seam::putInPlay(game);
    CHECK(Seam::addBall(game, kOpenField + Vec2{70.0F, 0.0F}) == 1);
    CHECK(Seam::addBall(game, kOpenField + Vec2{-70.0F, 0.0F}) == 2);
    // Arrivals: ball 2 first, then 1, then 0, five steps apart.
    std::array<int, 3> captured{-1, -1, -1};
    std::array<int, 3> released{-1, -1, -1};
    std::uint64_t score = game.rules().score();
    bool sound = true;
    float deepest = 0.0F;
    for (int i = 0; i < 400; ++i) {
      for (std::size_t b = 0; b < 3; ++b) {
        if (i == 5 * static_cast<int>(2 - b)) {
          Seam::placeBall(game, b, blackHole.center);
        } else if (captured[b] < 0) {
          // Kept out of the way until its turn.
          Seam::placeBall(game, b, kOpenField + Vec2{70.0F * (static_cast<float>(b) - 1.0F), 0.0F});
        }
      }
      game.step(input);
      for (std::size_t b = 0; b < 3; ++b) {
        const bool held = Seam::held(game, b);
        if (held && captured[b] < 0) {
          captured[b] = i;
          CHECK(game.rules().score() > score);  // Scored on arrival.
          score = game.rules().score();
          CHECK(!find(game.balls(), b).visible);
        } else if (!held && captured[b] >= 0 && released[b] < 0 && Seam::live(game, b)) {
          released[b] = i;
          const BallView view = find(game.balls(), b);
          CHECK(view.id == b && view.visible);
          CHECK(distance(view.position, blackHole.exitPosition) < 0.01F);
          CHECK(same(find(game.balls(0.0F), b), view));
        }
      }
      sound = sound && ballsAreSound(game);
      deepest = std::max(deepest, deepestOverlap(game));
    }
    CHECK(sound);
    CHECK(game.holeCount(hole) == 3);
    CHECK(captured[2] == 0 && captured[1] == 5 && captured[0] == 10);
    // 0.9 s is 108 steps, 0.3 s is 36.
    CHECK(released[2] == captured[2] + 107);
    CHECK(released[0] >= released[2] + 36 && released[0] <= released[2] + 37);
    CHECK(released[1] >= released[0] + 36 && released[1] <= released[0] + 37);
    CHECK(deepest <= 2.0F);
  }

  {  // Three balls into the three wormholes in the same step: each comes out
     // of the next one along, and is not swallowed again where it appears,
     // though another ball's arrival there would be.
    Game game;
    const InputState input;
    Seam::placeBall(game, 0, table::kHoles[0].center);
    Seam::putInPlay(game);
    CHECK(Seam::addBall(game, table::kHoles[1].center) == 1);
    CHECK(Seam::addBall(game, table::kHoles[2].center) == 2);
    game.step(input);
    for (std::size_t b = 0; b < 3; ++b) {
      CHECK(Seam::held(game, b) && game.holeCount(b) == 1);
      CHECK(table::kHoles[b].type == table::HoleType::Wormhole);
    }
    CHECK(game.ballHeld());
    int steps = 1;
    while (steps < 200 && (Seam::held(game, 0) || Seam::held(game, 1) || Seam::held(game, 2))) {
      game.step(input);
      ++steps;
    }
    CHECK(steps == 108);
    const pinball::BallViews views = game.balls();
    CHECK(views.count == 3);
    for (std::size_t b = 0; b < 3; ++b) {
      CHECK(views.items[b].visible);
      CHECK(distance(views.items[b].position, table::kHoles[b].exitPosition) < 0.01F);
      CHECK(distance(table::kHoles[b].exitPosition, table::kHoles[(b + 1) % 3].center) < 0.01F);
    }
    for (int i = 0; i < 20; ++i) {
      game.step(input);
      CHECK(!Seam::held(game, 0) && !Seam::held(game, 1) && !Seam::held(game, 2));
    }
    CHECK(game.holeCount(0) + game.holeCount(1) + game.holeCount(2) == 3);
    CHECK(ballsAreSound(game));
  }

  {  // Two balls up the ramp one behind the other: both stay in sight, each
     // drawn on its own ride, and the exit lets them out 0.3 s apart. A ball
     // sitting on the exit holds the next one back until it has moved off.
    const std::size_t ramp = holeOfType(table::HoleType::Ramp);
    const table::Hole& rampHole = table::kHoles[ramp];
    const Vec2 from = rampHole.center + Vec2{12.0F, 30.0F};
    const Vec2 shot{-420.0F, -1300.0F};
    Game game;
    const InputState input;
    Seam::placeBall(game, 0, from, shot);
    Seam::putInPlay(game);
    CHECK(Seam::addBall(game, kOpenField + Vec2{100.0F, 0.0F}) == 1);
    CHECK(Seam::addBall(game, kOpenField + Vec2{-100.0F, -20.0F}) == 2);
    std::array<int, 2> taken{-1, -1};
    std::array<int, 2> out{-1, -1};
    int bothRiding = 0;
    int sitter = -1;  // The step at which the third ball left the exit.
    for (int i = 0; i < 500; ++i) {
      if (i == 10) {
        Seam::placeBall(game, 1, from, shot);
      } else if (i < 10) {
        Seam::placeBall(game, 1, kOpenField + Vec2{100.0F, 0.0F});
      }
      // The third ball is put on the exit, and held there until well after
      // the second rider is due out.
      if (i < 200) {
        Seam::placeBall(game, 2,
                        i < 100 ? kOpenField + Vec2{-100.0F, -20.0F} : rampHole.exitPosition);
      }
      const pinball::BallViews before = game.balls();
      game.step(input);
      const pinball::BallViews now = game.balls();
      const pinball::BallViews half = game.balls(0.5F);
      for (std::size_t b = 0; b < 2; ++b) {
        const bool held = Seam::held(game, b);
        if (held && taken[b] < 0) {
          taken[b] = i;
        } else if (!held && taken[b] >= 0 && out[b] < 0) {
          out[b] = i;
          CHECK(distance(find(now, b).position, rampHole.exitPosition) < 0.01F);
          // Nobody within reach of where it appears.
          CHECK(distance(find(now, 2).position, rampHole.exitPosition) >= Game::kHoleExitClearance);
        }
      }
      if (Seam::held(game, 0) && Seam::held(game, 1) && i > taken[1] && i < 100) {
        ++bothRiding;
        const BallView a = find(now, 0);
        const BallView b = find(now, 1);
        CHECK(a.visible && b.visible && a.scale > 1.0F && b.scale > 1.0F);
        CHECK(a.position.y < b.position.y);  // The first is further along.
        for (std::size_t each = 0; each < 2; ++each) {
          CHECK(distance(find(half, each).position,
                         (find(before, each).position + find(now, each).position) * 0.5F) < 0.01F);
        }
      }
      if (i >= 200 && sitter < 0 &&
          distance(find(now, 2).position, rampHole.exitPosition) >= Game::kHoleExitClearance) {
        sitter = i;
      }
      // A rider kept waiting is drawn at the exit, full size.
      if (i > 150 && i < 200) {
        CHECK(Seam::held(game, 0) && Seam::held(game, 1));
        CHECK(distance(find(now, 0).position, rampHole.exitPosition) < 0.01F);
        CHECK(find(now, 0).scale == 1.0F && find(now, 0).visible);
      }
    }
    CHECK(taken[0] >= 0 && taken[0] < 6 && taken[1] > taken[0] && taken[1] < 16);
    CHECK(game.holeCount(ramp) == 2);
    CHECK(bothRiding > 60);
    CHECK(sitter >= 200);
    // Not at 0.9 s: only once the exit was clear, lowest id first, and the
    // other 0.3 s later.
    CHECK(out[0] >= sitter && out[0] <= sitter + 1);
    CHECK(out[1] >= out[0] + 36 && out[1] <= out[0] + 40);
    CHECK(ballsAreSound(game));
  }

  {  // Two games played the same way with three balls on the table are the
     // same, bit for bit, for a minute.
    Game a;
    Game b;
    bool identical = true;
    int added = 0;
    for (int i = 0; i < 120 * 60 && identical; ++i) {
      if (i % (120 * 10) == 300) {
        added += fillTable(a);
        fillTable(b);
      }
      const InputState input = soakInput(a, i);
      a.step(input);
      b.step(input);
      const pinball::BallViews va = a.balls();
      const pinball::BallViews vb = b.balls();
      identical = va.count == vb.count && a.rules().score() == b.rules().score() &&
                  a.drainCount() == b.drainCount();
      for (std::size_t k = 0; k < va.count && identical; ++k) {
        identical = same(va.items[k], vb.items[k]) && va.items[k].id == vb.items[k].id;
      }
    }
    CHECK(identical);
    CHECK(added >= 6);
  }

  {  // Soak with several balls: two minutes of the scripted player, the
     // table topped up to three balls every ten seconds. Every ball stays
     // sane and inside the speed cap, none sits still on the playfield, no
     // two stay pressed into each other, and the game goes on serving.
    Game game;
    int added = 0;
    int threeBallSteps = 0;
    bool sound = true;
    float deepest = 0.0F;
    int overlapSteps = 0;
    int longestOverlap = 0;
    std::array<int, Game::kMaxBalls> still{};
    std::array<Vec2, Game::kMaxBalls> last{};
    int worstStill = 0;
    for (int i = 0; i < 120 * 120; ++i) {
      if (i % (120 * 10) == 300) {
        added += fillTable(game);
      }
      game.step(soakInput(game, i));
      sound = sound && ballsAreSound(game);
      threeBallSteps += game.ballCount() == 3 ? 1 : 0;

      const float deep = deepestOverlap(game);
      deepest = std::max(deepest, deep);
      overlapSteps = deep > 2.0F ? overlapSteps + 1 : 0;
      longestOverlap = std::max(longestOverlap, overlapSteps);

      const pinball::BallViews views = game.balls();
      for (std::size_t id = 0; id < Game::kMaxBalls; ++id) {
        const BallView view = find(views, id);
        if (view.id == kNoBall || Seam::held(game, id) || view.position.x > table::kLaneWallX) {
          still[id] = 0;
          continue;
        }
        still[id] = distance(view.position, last[id]) < 0.05F ? still[id] + 1 : 0;
        last[id] = view.position;
        worstStill = std::max(worstStill, still[id]);
      }
    }
    std::uint32_t holes = 0;
    for (std::size_t h = 0; h < table::kHoles.size(); ++h) {
      holes += game.holeCount(h);
    }
    std::fprintf(stderr,
                 "soak with several balls: added %d, three on the table for %d steps, drains %u, "
                 "bumper hits %u, hole captures %u, deepest overlap %.2f px, longest over 2 px %d "
                 "steps, longest still %d steps, ball searches %u\n",
                 added, threeBallSteps, game.drainCount(), game.bumperHits(), holes,
                 static_cast<double>(deepest), longestOverlap, worstStill, game.ballSearchCount());
    CHECK(sound);
    CHECK(added >= 12);
    CHECK(threeBallSteps > 120 * 10);
    CHECK(deepest <= 2.0F);
    CHECK(longestOverlap <= 30);
    CHECK(worstStill < 120 * 3);
    CHECK(game.ballSearchCount() == 0);
    CHECK(game.drainCount() >= 12);
    CHECK(game.bumperHits() > 30);
    CHECK(holes > 0);
  }
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
  {  // Fail safe: a ball lost outside the table goes back on the plunger, and
     // the rules hear of it. A tilt at that moment has no drain to end it and
     // would leave the plunger dead for good.
    Game game;
    InputState input;
    pinball::GameTestAccess::placeBall(game, {300.0F, 300.0F});
    pinball::GameTestAccess::putInPlay(game);
    for (int i = 0; i < 3; ++i) {
      input.nudgeLeft = true;
      game.step(input);
      input.nudgeLeft = false;
      game.step(input);
    }
    CHECK(game.rules().tilted());
    pinball::GameTestAccess::placeBall(game, {-500.0F, 300.0F});
    game.step(input);
    CHECK(distance(game.ball().position, table::kBallStart) < 2.0F);
    CHECK(!game.laneGateClosed());
    CHECK(!game.rules().tilted());
    CHECK(game.drainCount() == 0);
    CHECK(game.rules().ball() == 1);
    // Out of play again: shoves on the plunger do not count towards a tilt.
    for (int i = 0; i < 3; ++i) {
      input.nudgeLeft = true;
      game.step(input);
      input.nudgeLeft = false;
      game.step(input);
    }
    CHECK(!game.rules().tilted());
    // And the plunger works: a full draw sends the ball out of the lane.
    input.plunger = true;
    for (int i = 0; i < 150; ++i) {
      game.step(input);
    }
    input.plunger = false;
    bool launched = false;
    for (int i = 0; i < 360 && !launched; ++i) {
      game.step(input);
      launched = game.laneGateClosed();
    }
    CHECK(launched);
  }

  {  // In play means on the playfield, wherever that is. The right-hand
     // channel (hyperspace chute, booster targets, right outlane) lies
     // between the lane wall and the line the gate used to wait for.
    const Vec2 channel[] = {
        {650.0F, 500.0F}, {table::kHyperspace.x + 4.0F, 420.0F}, {646.0F, 680.0F}};
    for (const Vec2 place : channel) {
      Game game;
      const InputState input;
      game.step(input);
      CHECK(!game.rules().ballInPlay() && !game.laneGateClosed());
      CHECK(place.x >= table::kLaneGateCloseX);
      pinball::GameTestAccess::placeBall(game, place);
      game.step(input);
      CHECK(game.rules().ballInPlay());
      CHECK(game.laneGateClosed());
      CHECK(game.rules().ballSaveActive());
    }
  }

  {  // A ball taken by the hyperspace chute straight from the launch is in
     // play by the time it scores: the gate is shut and the ball save is on
     // while the chute holds it.
    Game game;
    const InputState input;
    pinball::GameTestAccess::placeBall(game, {table::kHyperspace.x + 5.0F, 425.0F});
    bool taken = false;
    for (int i = 0; i < 240 && !taken; ++i) {
      game.step(input);
      taken = game.ballHeld();
      CHECK(game.rules().score() == 0 || game.rules().ballInPlay());
    }
    CHECK(taken);
    CHECK(game.rules().score() > 0);
    CHECK(game.rules().ballInPlay() && game.laneGateClosed());
  }

  {  // A right-outlane drain straight from the launch gets the ball save.
    Game game;
    const InputState input;
    pinball::GameTestAccess::placeBall(game, {646.0F, 760.0F});
    for (int i = 0; i < 600 && game.drainCount() == 0; ++i) {
      game.step(input);
    }
    CHECK(game.drainCount() == 1);
    CHECK(game.rules().ball() == 1);
    CHECK(game.rules().message() == std::string_view("RE-DEPLOY"));
  }

  {  // A ball still in the lane, or passing the open gate, is not in play,
     // and the gate does not shut on it.
    const Vec2 lane[] = {table::kBallStart,
                         {table::kLaneCenterX, 500.0F},
                         {table::kLaneCenterX, table::kLaneWallTopY - 20.0F},
                         {table::kLaneWallX + 2.0F, 180.0F},
                         {table::kLaneWallX - 14.0F, table::kLaneWallTopY - 30.0F},
                         {table::kLaneGateCloseX + 1.0F, 200.0F}};
    for (const Vec2 place : lane) {
      Game game;
      const InputState input;
      pinball::GameTestAccess::placeBall(game, place);
      const Vec2 before = game.ball().position;
      game.step(input);
      CHECK(distance(before, game.ball().position) < 3.0F);
      CHECK(!game.rules().ballInPlay());
      CHECK(!game.laneGateClosed());
      CHECK(!game.rules().ballSaveActive());
    }
    // Waiting on the plunger for a long time changes nothing.
    Game game;
    const InputState input;
    for (int i = 0; i < 1200; ++i) {
      game.step(input);
    }
    CHECK(!game.rules().ballInPlay() && !game.laneGateClosed());
  }

  {  // A tilted or finished table is dead: hits that do not score make no
     // sound and light nothing, though the ball still bounces and the hits
     // are still counted. A live table does both.
    constexpr pinball::SoundSet kHitSounds =
        pinball::soundBit(pinball::Sound::Bumper) | pinball::soundBit(pinball::Sound::Slingshot) |
        pinball::soundBit(pinball::Sound::Target) | pinball::soundBit(pinball::Sound::Rollover) |
        pinball::soundBit(pinball::Sound::HoleIn);
    enum class State { Live, Tilted, Over };
    for (const State state : {State::Live, State::Tilted, State::Over}) {
      Game game;
      InputState input;
      if (state == State::Tilted) {
        pinball::GameTestAccess::placeBall(game, {450.0F, 600.0F});
        pinball::GameTestAccess::putInPlay(game);
        for (int i = 0; i < 3; ++i) {
          input.nudgeLeft = true;
          game.step(input);
          input.nudgeLeft = false;
          game.step(input);
        }
        CHECK(game.rules().tilted());
      } else if (state == State::Over) {
        for (int i = 0; i < pinball::Rules::kBallsPerGame; ++i) {
          pinball::GameTestAccess::placeBall(game, {300.0F, table::kDrainY + 5.0F});
          game.step(input);
        }
        CHECK(game.gameOver());
      }
      const std::uint64_t scoreBefore = game.rules().score();
      const std::uint32_t hitsBefore = game.bumperHits();
      // Dropped through the middle re-entry lane onto the top attack bumper,
      // and left to rattle around for a while.
      pinball::GameTestAccess::placeBall(game, {table::kFieldCenterX + 3.0F, 140.0F});
      pinball::SoundSet heard = 0;
      float brightest = 0.0F;
      for (int i = 0; i < 360; ++i) {
        game.step(input);
        heard |= game.sounds();
        for (std::size_t b = 0; b < table::kBumpers.size(); ++b) {
          brightest = std::max(brightest, game.bumperFlash(b));
        }
        for (std::size_t sling = 0; sling < table::kSlingshots.size(); ++sling) {
          brightest = std::max(brightest, game.slingshotFlash(sling));
        }
      }
      CHECK(game.bumperHits() > hitsBefore);
      CHECK(game.rolloverCount(1) > 0);
      if (state == State::Live) {
        CHECK((heard & pinball::soundBit(pinball::Sound::Bumper)) != 0U);
        CHECK((heard & pinball::soundBit(pinball::Sound::Rollover)) != 0U);
        CHECK(brightest == 1.0F);
        CHECK(game.rules().score() > scoreBefore);
      } else {
        CHECK((heard & kHitSounds) == 0U);
        CHECK(brightest == 0.0F);
        CHECK(game.rules().score() == scoreBefore);
      }
    }
  }

  testSeveralBalls();
}
