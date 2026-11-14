#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <limits>
#include <vector>

#include "Check.hpp"
#include "GameTestAccess.hpp"
#include "Suites.hpp"
#include "core/Game.hpp"
#include "core/Physics.hpp"
#include "core/Rules.hpp"
#include "core/TableLayout.hpp"

// The table's element types: the bay door bank, the lock, the Briefing
// room's scoop, the spinner, the kickback and the sensor lines. Each is
// tested as a mechanism; when it acts is for the rules.
namespace {

using pinball::BallView;
using pinball::Game;
using pinball::GameEvent;
using pinball::InputState;
using pinball::PhysicsEvent;
using pinball::PhysicsWorld;
using pinball::Rules;
using pinball::Vec2;
using Seam = pinball::GameTestAccess;
namespace table = pinball::table;

constexpr float kStep = 1.0F / 120.0F;
constexpr int kSecond = 120;
constexpr Vec2 kOffTable{355.5F, table::kDrainY + 4.0F};
// In front of the Hangar's mouth, below the door line.
constexpr Vec2 kBelowDoors{279.0F, 440.0F};

const int kScoopHoldSteps = static_cast<int>(table::kScoopHoldSeconds * kSecond + 0.5);
const table::Hole& kScoop = table::kHoles[table::kScoopHole];
const table::Hole& kHangar = table::kHoles[table::kHangarHole];
constexpr table::ElementRef kHangarElement{table::ElementKind::Hole, table::kHangarHole};

float distance(Vec2 a, Vec2 b) {
  const Vec2 d = a - b;
  return std::sqrt(d.x * d.x + d.y * d.y);
}

// Distance from a point to a door's centre line.
float distanceToDoor(Vec2 p, const table::Capsule& door) {
  const float x = std::clamp(p.x, door.a.x, door.b.x);
  return distance(p, {x, door.a.y});
}

// How deep a ball at p is inside any standing door: 0 if it is clear.
float depthInDoors(Vec2 p) {
  float depth = -1.0e9F;
  for (const table::Capsule& door : table::kDropTargets) {
    depth = std::max(depth, door.radius + table::kBallRadius - distanceToDoor(p, door));
  }
  return depth;
}

int countEvents(const Game& game, GameEvent::Type type) {
  int count = 0;
  for (const GameEvent& event : game.gameEvents()) {
    count += event.type == type ? 1 : 0;
  }
  return count;
}

const GameEvent* findEvent(const Game& game, GameEvent::Type type) {
  for (const GameEvent& event : game.gameEvents()) {
    if (event.type == type) {
      return &event;
    }
  }
  return nullptr;
}

void run(Game& game, int steps) {
  const InputState idle;
  for (int i = 0; i < steps; ++i) {
    game.step(idle);
  }
}

// A game whose first ball is in play at a place, moving.
void putBall(Game& game, std::size_t slot, Vec2 position, Vec2 velocity) {
  Seam::placeBall(game, slot, position, velocity);
  Seam::putInPlay(game, slot);
}

// Keeps a ball where it is harmless, in play, for a while: long enough for
// the ball save to run out, for one.
constexpr Vec2 kParked{300.0F, 560.0F};
void park(Game& game, std::size_t slot, int steps) {
  const InputState idle;
  for (int i = 0; i < steps; ++i) {
    Seam::placeBall(game, slot, kParked, {0.0F, 0.0F});
    Seam::putInPlay(game, slot);
    game.step(idle);
  }
}
constexpr int kPastBallSave = static_cast<int>(Rules::kBallSaveSeconds * kSecond) + 2;

bool near(Vec2 a, Vec2 b) { return distance(a, b) < 0.01F; }

// Marks all three doors, as three hits would: the bank drops on the next step.
void markDoors(Game& game) {
  for (std::size_t i = 0; i < table::kDropTargets.size(); ++i) {
    Seam::rules(game).onDropTarget(i);
  }
}

// Sends the ball in a slot up into the open Hangar and steps until it is
// taken. Returns whether it was.
bool shootHangar(Game& game, std::size_t slot) {
  const InputState idle;
  if (game.doorBankUp()) {
    // The rules shut the Hangar after some locks: open it again.
    markDoors(game);
    game.step(idle);
    game.step(idle);
  }
  putBall(game, slot, kBelowDoors, {0.0F, -900.0F});
  for (int i = 0; i < kSecond; ++i) {
    game.step(idle);
    if (Seam::held(game, slot)) {
      return true;
    }
  }
  return false;
}

// The live ball that is not locked and not the given one; kMaxBalls if none.
std::size_t otherFreeSlot(const Game& game, std::size_t notThis) {
  for (std::size_t slot = 0; slot < Game::kMaxBalls; ++slot) {
    if (slot != notThis && Seam::live(game, slot) && !game.ballLocked(slot)) {
      return slot;
    }
  }
  return Game::kMaxBalls;
}

BallView viewOf(const Game& game, std::size_t slot) {
  for (const BallView& ball : game.balls()) {
    if (ball.id == slot) {
      return ball;
    }
  }
  return {};
}

// --- The spinner's arithmetic, decided by the compiler. -----------------------------
static_assert(table::spinsFor(0.0F) == 1);
static_assert(table::spinsFor(149.0F) == 1);
static_assert(table::spinsFor(299.0F) == 1);
static_assert(table::spinsFor(300.0F) == 2);
static_assert(table::spinsFor(450.0F) == 3);
static_assert(table::spinsFor(1799.0F) == 11);
static_assert(table::spinsFor(1800.0F) == 12);
static_assert(table::spinsFor(1.0e9F) == 12);
static_assert(table::spinsFor(-500.0F) == 1);
static_assert(table::spinsFor(std::numeric_limits<float>::quiet_NaN()) == 1);
static_assert(table::kMaxSpinsOwed == 2 * table::kMaxSpinsPerPass);

// --- Ids. ---------------------------------------------------------------------------
static_assert(table::kRollovers[0].id == table::RolloverId::RollCall1);
static_assert(table::kRollovers[3].id == table::RolloverId::WingW);
static_assert(table::kRollovers[4].id == table::RolloverId::WingI);
static_assert(table::kRollovers[5].id == table::RolloverId::WingN);
static_assert(table::kRollovers[6].id == table::RolloverId::WingG);
static_assert(table::kSensors[table::kSpinnerSensor].id == table::SensorId::Spinner);
static_assert(table::elementCount(table::ElementKind::Sensor) == 4);
static_assert(table::elementCount(table::ElementKind::Kickback) == 1);

void testDoorBank() {
  {  // Standing, the bank shuts the Hangar: balls fired at every part of the
     // mouth at every speed hit a door and none gets in.
    int hits = 0;
    for (int xi = 0; xi < 18; ++xi) {
      for (const float speed : {300.0F, 900.0F, 1700.0F, 2900.0F}) {
        PhysicsWorld world;
        const float x = 257.0F + static_cast<float>(xi) * 2.2F;
        world.resetBall({x, 440.0F}, {(static_cast<float>(xi % 5) - 2.0F) * 40.0F, -speed});
        bool hit = false;
        for (int s = 0; s < kSecond; ++s) {
          world.step(kStep);
          for (const PhysicsEvent& e : world.events()) {
            hit = hit || e.element.kind == table::ElementKind::DropTarget;
            CHECK(e.type != PhysicsEvent::Type::Capture);
          }
          CHECK(world.ballPosition().y > table::kDoorY);
          CHECK(!world.ballHeld());
        }
        hits += hit ? 1 : 0;
      }
    }
    CHECK(hits >= 60);  // A slow ball can fall back before it reaches a door.
  }

  {  // Each door reports its own hits, with its own index.
    for (std::size_t door = 0; door < table::kDropTargets.size(); ++door) {
      PhysicsWorld world;
      const table::Capsule& shape = table::kDropTargets[door];
      world.resetBall({(shape.a.x + shape.b.x) * 0.5F, 430.0F}, {0.0F, -1200.0F});
      bool reported = false;
      for (int s = 0; s < 30 && !reported; ++s) {
        world.step(kStep);
        for (const PhysicsEvent& e : world.events()) {
          if (e.element.kind == table::ElementKind::DropTarget) {
            CHECK(e.element.index == door);
            CHECK(e.type == PhysicsEvent::Type::Hit);
            reported = true;
          }
        }
      }
      CHECK(reported);
    }
  }

  {  // Down, the doors are not there: the same balls pass and are taken.
    for (int xi = 0; xi < 18; ++xi) {
      PhysicsWorld world;
      world.setDoorBank(false);
      CHECK(!world.doorBankUp());
      // The mouth is narrower than the pocket at its foot, by the Briefing
      // room's flare beside it.
      world.resetBall({257.0F + static_cast<float>(xi) * 2.2F, 440.0F}, {0.0F, -1200.0F});
      bool taken = false;
      for (int s = 0; s < kSecond && !taken; ++s) {
        world.step(kStep);
        for (const PhysicsEvent& e : world.events()) {
          CHECK(e.element.kind != table::ElementKind::DropTarget);
          taken = taken || (e.type == PhysicsEvent::Type::Capture &&
                            e.element == table::ElementRef{table::ElementKind::Hole,
                                                           table::kHangarHole});
        }
      }
      CHECK(taken);
      // And back up it is a wall again. Asking twice changes nothing.
      world.setDoorBank(true);
      world.setDoorBank(true);
      CHECK(world.doorBankUp());
    }
  }

  {  // The bank drops as one, and only when all three doors have been hit:
     // one door hit again and again opens nothing.
    Game game;
    run(game, 2);
    CHECK(game.doorBankUp());
    for (int i = 0; i < 5; ++i) {
      putBall(game, 0, {254.0F, 430.0F}, {0.0F, -1000.0F});
      run(game, 40);
    }
    CHECK(game.rules().doorMarked(0) && !game.rules().doorMarked(1) && !game.rules().doorMarked(2));
    CHECK(game.doorBankUp());
    putBall(game, 0, {279.0F, 430.0F}, {0.0F, -1000.0F});
    run(game, 40);
    CHECK(game.rules().doorMarked(1));
    CHECK(game.doorBankUp());
    // The third.
    putBall(game, 0, {304.0F, 430.0F}, {0.0F, -1000.0F});
    int dropped = 0;
    const InputState idle;
    for (int i = 0; i < 40; ++i) {
      game.step(idle);
      dropped += countEvents(game, GameEvent::Type::DoorBankDropped);
    }
    CHECK(dropped == 1);
    CHECK(!game.doorBankUp());
  }

  {  // Three balls at the bank at once: each hit is its own event with its
     // own ball, all three doors are marked and the bank drops once.
    Game game;
    run(game, 2);
    Seam::rules(game).setMultiballEnabled(false);
    putBall(game, 0, {254.0F, 430.0F}, {0.0F, -1100.0F});
    const std::size_t second = Seam::addBall(game, {279.0F, 430.0F}, {0.0F, -1100.0F});
    const std::size_t third = Seam::addBall(game, {304.0F, 430.0F}, {0.0F, -1100.0F});
    CHECK(second == 1 && third == 2);
    std::array<int, 3> hitBy{-1, -1, -1};
    int dropped = 0;
    const InputState idle;
    for (int i = 0; i < 30; ++i) {
      game.step(idle);
      for (const PhysicsEvent& e : game.events()) {
        if (e.element.kind == table::ElementKind::DropTarget && hitBy[e.element.index] < 0) {
          hitBy[e.element.index] = e.ball;
        }
      }
      dropped += countEvents(game, GameEvent::Type::DoorBankDropped);
    }
    CHECK(hitBy[0] == 0 && hitBy[1] == 1 && hitBy[2] == 2);
    CHECK(dropped == 1);
    CHECK(!game.doorBankUp());
    CHECK(game.ballCount() == 3);
  }

  {  // The bank waits for the mouth to clear before it comes up, and comes
     // up at once when it is.
    Game game;
    run(game, 2);
    Seam::dropDoorBank(game);
    const InputState idle;
    // A ball kept in the mouth, on the door line.
    for (int i = 0; i < 60; ++i) {
      Seam::placeBall(game, 0, {279.0F, table::kDoorY}, {0.0F, 0.0F});
      Seam::putInPlay(game);
      if (i == 5) {
        Seam::raiseDoorBank(game);
      }
      game.step(idle);
      CHECK(!game.doorBankUp());
      CHECK(game.doorRaisePending() == (i >= 5));
    }
    // Moved away: up on the next step, once.
    Seam::placeBall(game, 0, {279.0F, 520.0F}, {0.0F, 0.0F});
    game.step(idle);
    CHECK(game.doorBankUp());
    CHECK(!game.doorRaisePending());
    CHECK(countEvents(game, GameEvent::Type::DoorBankRaised) == 1);
    game.step(idle);
    CHECK(countEvents(game, GameEvent::Type::DoorBankRaised) == 0);
  }

  {  // A ball arriving at the mouth as the bank is told to rise, for every
     // timing and several speeds and places: the bank never comes up onto
     // it, the ball is never inside a standing door, and it never gets past
     // a standing bank. It is taken by the open Hangar or turned back.
    int taken = 0;
    int turnedBack = 0;
    for (int delay = 0; delay < 36; ++delay) {
      for (const float speed : {500.0F, 1300.0F, 2600.0F}) {
        for (const float x : {250.0F, 268.0F, 279.0F, 301.0F}) {
          Game game;
          run(game, 2);
          Seam::rules(game).setMultiballEnabled(false);
          Seam::dropDoorBank(game);
          putBall(game, 0, {x, 520.0F}, {0.0F, -speed});
          const InputState idle;
          bool wasTaken = false;
          for (int i = 0; i < 70; ++i) {
            if (i == delay) {
              Seam::raiseDoorBank(game);
            }
            const bool upBefore = game.doorBankUp();
            const Vec2 before = Seam::position(game, 0);
            game.step(idle);
            if (Seam::held(game, 0)) {
              wasTaken = true;
              // Only an open Hangar takes a ball.
              CHECK(!upBefore);
              break;
            }
            const Vec2 after = Seam::position(game, 0);
            if (game.doorBankUp()) {
              CHECK(depthInDoors(after) < 2.5F);
              // Not through it, either way.
              CHECK(!(upBefore && before.y > table::kDoorY && after.y < table::kDoorY));
            }
          }
          taken += wasTaken ? 1 : 0;
          turnedBack += wasTaken ? 0 : 1;
        }
      }
    }
    // Both outcomes happen across the sweep.
    CHECK(taken > 20);
    CHECK(turnedBack > 20);
  }

  {  // The Hangar's exit is below the door line: a returned ball is never
     // inside a standing door.
    CHECK(depthInDoors(kHangar.exitPosition) < -5.0F);
    // And inside the box the bank waits on, so it cannot come up onto a
    // ball that has just come out.
    CHECK(std::fabs(kHangar.exitPosition.y - table::kDoorMouthCenter.y) <
          table::kDoorMouthHalfSize.y);
  }

  {  // With multiball off: the Hangar returns the ball and the
     // bank comes back up half a second later, with its marks cleared.
    Game game;
    run(game, 2);
    Seam::rules(game).setMultiballEnabled(false);
    markDoors(game);
    run(game, 1);
    CHECK(!game.doorBankUp());
    CHECK(shootHangar(game, 0));
    const InputState idle;
    int ejectAt = -1;
    int raisedAt = -1;
    for (int i = 0; i < 4 * kSecond && raisedAt < 0; ++i) {
      game.step(idle);
      if (findEvent(game, GameEvent::Type::HoleEject) != nullptr) {
        ejectAt = i;
      }
      if (findEvent(game, GameEvent::Type::DoorBankRaised) != nullptr) {
        raisedAt = i;
      }
    }
    CHECK(ejectAt >= 0 && raisedAt > ejectAt);
    CHECK(raisedAt - ejectAt >= static_cast<int>(Rules::kDoorRaiseSeconds * kSecond) - 1);
    CHECK(game.doorBankUp());
    CHECK(!game.rules().doorMarked(0) && !game.rules().doorMarked(2));
    CHECK(game.lockedBalls() == 0);
  }
}

void testLock() {
  {  // A ball into the open Hangar with the lock lit is held in the first
     // bay, in view; the player gets a new ball. The second goes to the
     // second bay. They stay.
    Game game;
    run(game, 2);
    markDoors(game);
    run(game, 1);
    CHECK(game.rules().lockLit());
    putBall(game, 0, kBelowDoors, {0.0F, -900.0F});
    const InputState idle;
    const GameEvent* locked = nullptr;
    for (int i = 0; i < kSecond && locked == nullptr; ++i) {
      game.step(idle);
      locked = findEvent(game, GameEvent::Type::BallLocked);
    }
    CHECK(locked != nullptr);
    if (locked == nullptr) {
      return;
    }
    CHECK(locked->ball == 0 && locked->index == 0);
    CHECK(game.ballLocked(0));
    CHECK(game.lockedBalls() == 1 && game.lockCount() == 1);
    CHECK(game.rules().locksHeld() == 1);
    // A new ball on the plunger, for the player to launch.
    CHECK(game.ballCount() == 2);
    CHECK(Seam::live(game, 1) && !game.ballLocked(1));
    CHECK(distance(Seam::position(game, 1), table::kBallStart) < 2.0F);
    CHECK(!game.rules().ballInPlay());
    CHECK(!game.ballInPlay(1));
    // The game's own ball is the one in play, not the one in the bay.
    CHECK(game.ball().id == 1);
    CHECK(!game.ballHeld());
    const BallView inBay = viewOf(game, 0);
    CHECK(inBay.visible && inBay.locked && !inBay.raised);
    CHECK(inBay.position.x == table::kLockBays[0].x && inBay.position.y == table::kLockBays[0].y);
    // The doors stay down and the lock stays lit.
    CHECK(!game.doorBankUp());

    CHECK(shootHangar(game, 1));
    CHECK(game.ballLocked(1) && game.lockedBalls() == 2);
    CHECK(viewOf(game, 1).position.x == table::kLockBays[1].x);
    CHECK(game.ballCount() == 3);
    CHECK(Seam::live(game, 2) && !game.ballLocked(2));

    // Ten seconds on, nothing has moved and nothing has been searched for.
    run(game, 10 * kSecond);
    CHECK(game.lockedBalls() == 2);
    CHECK(viewOf(game, 0).position.x == table::kLockBays[0].x);
    CHECK(viewOf(game, 1).position.y == table::kLockBays[1].y);
    CHECK(game.ballSearchCount() == 0);

    // Three is the most: two locked and one to play leave no room.
    CHECK(!game.requestBall());
    CHECK(game.queuedBalls() == 0);

    // The ball in play drains. The locked balls stay where they are, the
    // game goes on to its next ball, and that ball is served.
    const int ballBefore = game.rules().ball();
    park(game, 2, kPastBallSave);
    Seam::placeBall(game, 2, kOffTable);
    run(game, 2);
    CHECK(game.rules().ball() == ballBefore + 1);
    CHECK(game.lockedBalls() == 2 && game.ballLocked(0) && game.ballLocked(1));
    CHECK(game.ballCount() == 3);
    CHECK(distance(Seam::position(game, 2), table::kBallStart) < 2.0F);
    CHECK(game.ball().id == 2);

    // The third ball in lets all three go: itself after the usual hold,
    // lobbed left; then the first bay's, kicked right; then the second
    // bay's, lobbed left; each 0.4 s after the one before, from the exit.
    CHECK(shootHangar(game, 2));
    CHECK(!game.ballLocked(2));
    struct Leaving {
      int step;
      std::size_t ball;
      Vec2 velocity;
      Vec2 position;
    };
    std::vector<Leaving> leaving;
    std::vector<std::size_t> baysLeft;
    for (int i = 0; i < 4 * kSecond; ++i) {
      game.step(idle);
      for (const GameEvent& event : game.gameEvents()) {
        if (event.type == GameEvent::Type::HoleEject) {
          CHECK(event.index == table::kHangarHole);
          leaving.push_back({i, event.ball, Seam::velocity(game, event.ball),
                             Seam::position(game, event.ball)});
        }
        if (event.type == GameEvent::Type::LockReleased) {
          baysLeft.push_back(event.index);
        }
      }
    }
    CHECK(leaving.size() == 3);
    if (leaving.size() == 3) {
      CHECK(leaving[0].ball == 2 && leaving[1].ball == 0 && leaving[2].ball == 1);
      const int spacing = static_cast<int>(table::kLockReleaseSpacingSeconds * kSecond);
      CHECK(leaving[1].step - leaving[0].step == spacing);
      CHECK(leaving[2].step - leaving[1].step == spacing);
      for (const Leaving& ball : leaving) {
        CHECK(near(ball.position, kHangar.exitPosition));
      }
      CHECK(near(leaving[0].velocity, kHangar.exitVelocity));
      CHECK(near(leaving[1].velocity, table::kHangarEjectRight));
      CHECK(near(leaving[2].velocity, kHangar.exitVelocity));
    }
    CHECK(baysLeft.size() == 2 && baysLeft[0] == 0 && baysLeft[1] == 1);
    CHECK(game.lockedBalls() == 0 && game.lockReleaseCount() == 2);
    CHECK(game.rules().locksHeld() == 0);
    // The doors came back up behind them.
    CHECK(game.doorBankUp());
  }

  {  // Both bays taken when a lock is asked for: the ball is not locked and
     // is returned like any other, and the rules hear so (no lock, no lock
     // message).
    Game game;
    run(game, 2);
    markDoors(game);
    run(game, 1);
    CHECK(shootHangar(game, 0));
    CHECK(shootHangar(game, 1));
    CHECK(game.lockedBalls() == 2 && game.rules().locksHeld() == 2);
    // The third is taken by a Hangar told to return it, then told to lock.
    Seam::rules(game).setMultiballEnabled(false);
    CHECK(shootHangar(game, 2));
    const std::string_view before = game.rules().message();
    Seam::lockBall(game, 2);
    CHECK(game.lockedBalls() == 2 && !game.ballLocked(2) && game.lockCount() == 2);
    CHECK(game.rules().locksHeld() == 2);
    CHECK(game.rules().message() == before);
    CHECK(findEvent(game, GameEvent::Type::BallLocked) == nullptr);
    const InputState idle;
    bool returned = false;
    for (int i = 0; i < 2 * kSecond && !returned; ++i) {
      game.step(idle);
      returned = !Seam::held(game, 2);
    }
    CHECK(returned && game.lockedBalls() == 2);
  }

  {  // Let go on command, with no ball arriving: the first leaves at once,
     // lobbed left, the second 0.4 s later, kicked right.
    Game game;
    run(game, 2);
    markDoors(game);
    run(game, 1);
    CHECK(shootHangar(game, 0));
    CHECK(shootHangar(game, 1));
    CHECK(game.lockedBalls() == 2);
    run(game, kSecond);
    Seam::releaseLocks(game);
    const InputState idle;
    std::vector<int> steps;
    std::vector<Vec2> velocities;
    for (int i = 0; i < 2 * kSecond; ++i) {
      game.step(idle);
      if (const GameEvent* event = findEvent(game, GameEvent::Type::LockReleased)) {
        steps.push_back(i);
        velocities.push_back(Seam::velocity(game, event->ball));
      }
    }
    CHECK(steps.size() == 2);
    if (steps.size() == 2) {
      CHECK(steps[0] == 0);
      CHECK(steps[1] - steps[0] == static_cast<int>(table::kLockReleaseSpacingSeconds * kSecond));
      CHECK(near(velocities[0], kHangar.exitVelocity));
      CHECK(near(velocities[1], table::kHangarEjectRight));
    }
    CHECK(game.lockedBalls() == 0);
  }

  {  // A locked ball is not there for the others: a ball crossing a bay is
     // not turned by the ball in it, and is taken by the Hangar.
    Game game;
    run(game, 2);
    markDoors(game);
    run(game, 1);
    CHECK(shootHangar(game, 0));
    const std::size_t next = otherFreeSlot(game, 0);
    CHECK(next == 1);
    putBall(game, next, {table::kLockBays[0].x, 440.0F}, {0.0F, -900.0F});
    const InputState idle;
    bool taken = false;
    for (int i = 0; i < kSecond && !taken; ++i) {
      game.step(idle);
      taken = Seam::held(game, next);
      for (const PhysicsEvent& e : game.events()) {
        CHECK(e.type != PhysicsEvent::Type::BallHit);
      }
    }
    CHECK(taken);
  }

  {  // One locked, one in play: one more ball may be asked for, not two.
    Game game;
    run(game, 2);
    markDoors(game);
    run(game, 1);
    CHECK(shootHangar(game, 0));
    CHECK(game.requestBall());
    CHECK(!game.requestBall());
  }

  {  // A full turbo meter asks the table for a ball. With one locked and
     // one in play it is served and Turbo multiball starts; with two locked
     // the table refuses it and the rules pay the turbo bonus instead.
    for (const int locks : {1, 2}) {
      Game game;
      run(game, 2);
      markDoors(game);
      run(game, 1);
      for (int lock = 0; lock < locks; ++lock) {
        CHECK(shootHangar(game, static_cast<std::size_t>(lock)));
      }
      Rules& rules = Seam::rules(game);
      const std::uint64_t bonus = rules.scoreFrom(pinball::Feature::TurboBonus);
      const int fill = rules.turboGoal() - rules.turboMeter();
      for (int spin = 0; spin < fill; ++spin) {
        rules.onSpin();
      }
      run(game, 2);
      if (locks == 1) {
        CHECK(game.queuedBalls() + game.ballCount() == 3);
        CHECK(rules.multiball() == pinball::Multiball::Turbo);
        CHECK(rules.scoreFrom(pinball::Feature::TurboBonus) == bonus);
      } else {
        CHECK(game.queuedBalls() == 0 && game.ballCount() == 3);
        CHECK(rules.multiball() == pinball::Multiball::None);
        CHECK(rules.scoreFrom(pinball::Feature::TurboBonus) - bonus == pinball::points::kTurboBonus);
        CHECK(rules.message() == std::string_view("TURBO BONUS"));
      }
    }
  }

  {  // Tilt. The locked ball stays locked, and a ball waiting on the plunger
     // of a tilted table is launched for the player, so the tilt can end.
    Game game;
    run(game, 2);
    markDoors(game);
    run(game, 1);
    CHECK(shootHangar(game, 0));
    // Slot 1 waits on the plunger; a third ball is in play.
    const std::size_t inPlay = Seam::addBall(game, {300.0F, 560.0F});
    CHECK(inPlay == 2);
    Seam::putInPlay(game, inPlay);
    run(game, 2);
    InputState nudge;
    for (int i = 0; i < 8 && !game.rules().tilted(); ++i) {
      nudge.nudgeUp = (i % 2) == 0;
      game.step(nudge);
    }
    CHECK(game.rules().tilted());
    CHECK(!game.ballInPlay(1));
    bool launched = false;
    for (int i = 0; i < 3 * kSecond && !launched; ++i) {
      run(game, 1);
      launched = Seam::live(game, 1) && game.ballInPlay(1);
    }
    CHECK(launched);
    CHECK(game.ballLocked(0));
    // Both free balls drain; the tilt ends; the lock is still held.
    for (std::size_t slot = 1; slot < Game::kMaxBalls; ++slot) {
      if (Seam::live(game, slot) && !game.ballLocked(slot)) {
        Seam::placeBall(game, slot, kOffTable);
      }
    }
    run(game, 2);
    CHECK(!game.rules().tilted());
    CHECK(game.ballLocked(0) && game.lockedBalls() == 1);
    CHECK(game.ballCount() == 2);
  }

  {  // Game over empties the bays; so does a new game.
    Game game;
    run(game, 2);
    markDoors(game);
    run(game, 1);
    CHECK(shootHangar(game, 0));
    CHECK(game.lockedBalls() == 1);
    for (int ball = 0; ball < Rules::kBallsPerGame + 2 && !game.gameOver(); ++ball) {
      const std::size_t slot = otherFreeSlot(game, Game::kMaxBalls);
      park(game, slot, kPastBallSave);
      Seam::placeBall(game, slot, kOffTable);
      run(game, 2);
    }
    CHECK(game.gameOver());
    CHECK(game.lockedBalls() == 0);
    CHECK(game.ballCount() == 1);
    CHECK(game.rules().locksHeld() == 0);

    InputState again;
    again.newGame = true;
    game.step(again);
    run(game, 2);
    CHECK(!game.gameOver());
    CHECK(game.lockedBalls() == 0 && game.ballCount() == 1);
    CHECK(game.doorBankUp());
    CHECK(game.ball().id == 0 && !game.ball().locked);
  }

  {  // A new game started with balls in the bays: they are gone, and the
     // doors, which were down, are back up.
    Game game;
    run(game, 2);
    markDoors(game);
    run(game, 1);
    CHECK(shootHangar(game, 0));
    InputState again;
    again.newGame = true;
    game.step(again);
    run(game, 3);
    CHECK(game.lockedBalls() == 0 && game.ballCount() == 1);
    CHECK(game.doorBankUp());
    CHECK(!game.rules().lockLit());
  }
}

void testScoop() {
  {  // The Briefing room takes a ball at any speed, holds it for its time
     // and returns it from its exit at its speed.
    for (const float speed : {40.0F, 300.0F, 1200.0F, 2900.0F}) {
      Game game;
      run(game, 2);
      putBall(game, 0, {kScoop.center.x, 440.0F}, {0.0F, -speed});
      // A slow ball needs a push to get in at all: start it inside the mouth.
      if (speed < 100.0F) {
        Seam::placeBall(game, 0, {kScoop.center.x, kScoop.center.y + kScoop.halfSize.y - 1.0F},
                        {0.0F, -speed});
      }
      const InputState idle;
      int takenAt = -1;
      int returnedAt = -1;
      for (int i = 0; i < 3 * kSecond && returnedAt < 0; ++i) {
        game.step(idle);
        if (takenAt < 0 && Seam::held(game, 0)) {
          takenAt = i;
          CHECK(!game.ball().visible);
        }
        if (const GameEvent* out = findEvent(game, GameEvent::Type::HoleEject)) {
          returnedAt = i;
          CHECK(out->index == table::kScoopHole && out->ball == 0);
          CHECK(near(Seam::position(game, 0), kScoop.exitPosition));
          // Its own kick, a little different each time, within its range.
          const Vec2 kick = Seam::velocity(game, 0) - kScoop.exitVelocity;
          CHECK(kick.x >= -static_cast<float>(table::kScoopExitJitterLeft) - 0.01F &&
                kick.x <= static_cast<float>(table::kScoopExitJitterRight) + 0.01F);
          CHECK(std::fabs(kick.y) <= static_cast<float>(table::kScoopExitJitterDown) + 0.01F);
        }
      }
      CHECK(takenAt >= 0);
      // The step it is taken is the first of its hold.
      CHECK(returnedAt - takenAt == kScoopHoldSteps - 1);
      CHECK(game.holeCount(table::kScoopHole) == 1);
      // The rules hear of it once, from the capture itself.
      CHECK(game.rules().scoreFrom(pinball::Feature::Briefing) == pinball::points::kBriefingRoom);
    }
  }

  {  // Two balls arriving together: both are taken and counted on arrival,
     // but the scoop holds one at a time. The second waits behind the first
     // and its own hold starts when the first has left, so they come out a
     // full hold apart, in the order they went in.
    for (const int gap : {0, 1, 7, 40}) {
      Game game;
      run(game, 2);
      putBall(game, 0, {kScoop.center.x - 6.0F, 436.0F}, {0.0F, -900.0F});
      const InputState idle;
      std::size_t second = Game::kMaxBalls;
      std::vector<int> outAt;
      std::vector<std::size_t> outBall;
      int bothTakenAt = -1;
      for (int i = 0; i < 5 * kSecond; ++i) {
        if (i == gap) {
          second = Seam::addBall(game, {kScoop.center.x + 6.0F, 462.0F}, {0.0F, -900.0F});
          Seam::putInPlay(game, second);
        }
        game.step(idle);
        if (bothTakenAt < 0 && second < Game::kMaxBalls && Seam::held(game, 0) &&
            Seam::held(game, second)) {
          bothTakenAt = i;
          CHECK(game.holeCount(table::kScoopHole) == 2);
        }
        CHECK(countEvents(game, GameEvent::Type::HoleEject) <= 1);
        if (const GameEvent* out = findEvent(game, GameEvent::Type::HoleEject)) {
          outAt.push_back(i);
          outBall.push_back(out->ball);
        }
      }
      CHECK(second == 1);
      CHECK(bothTakenAt >= 0);
      CHECK(outAt.size() >= 2);
      if (outAt.size() >= 2) {
        CHECK(outBall[0] == 0 && outBall[1] == 1);
        CHECK(outAt[1] - outAt[0] == kScoopHoldSteps);
      }
    }
  }

  {  // The scoop does not return a ball onto one sitting at its exit: it
     // waits until the exit is clear.
    Game game;
    run(game, 2);
    putBall(game, 0, {kScoop.center.x, 440.0F}, {0.0F, -900.0F});
    const std::size_t blocker = Seam::addBall(game, {600.0F, 600.0F});
    const InputState idle;
    int returnedAt = -1;
    constexpr int kBlockedSteps = 3 * kSecond;
    for (int i = 0; i < 5 * kSecond && returnedAt < 0; ++i) {
      if (i < kBlockedSteps) {
        // Kept just beside the exit, still.
        Seam::placeBall(game, blocker, {kScoop.exitPosition.x + 14.0F, kScoop.exitPosition.y},
                        {0.0F, 0.0F});
      } else if (i == kBlockedSteps) {
        Seam::placeBall(game, blocker, {600.0F, 600.0F}, {0.0F, 0.0F});
      }
      game.step(idle);
      if (findEvent(game, GameEvent::Type::HoleEject) != nullptr) {
        returnedAt = i;
        CHECK(distance(Seam::position(game, blocker), kScoop.exitPosition) >=
              Game::kHoleExitClearance);
      }
    }
    CHECK(returnedAt >= kBlockedSteps);
    CHECK(returnedAt <= kBlockedSteps + 2);
  }

  {  // The short save after an eject. A ball that drains within 2.5 s of
     // being returned is flagged and, by these rules, given back; the ball
     // save is not used up by it. Later than that it is an ordinary drain.
    for (const bool soon : {true, false}) {
      Game game;
      run(game, 2);
      putBall(game, 0, {kScoop.center.x, 440.0F}, {0.0F, -900.0F});
      // Let the ball save run out while the ball is being placed around.
      const InputState idle;
      bool out = false;
      for (int i = 0; i < 3 * kSecond && !out; ++i) {
        game.step(idle);
        out = findEvent(game, GameEvent::Type::HoleEject) != nullptr;
      }
      CHECK(out);
      // Keep it clear of everything until the moment.
      park(game, 0, static_cast<int>(table::kEjectSaveSeconds * kSecond) + (soon ? -3 : 3));
      const int ballBefore = game.rules().ball();
      const bool saveActive = game.rules().ballSaveActive();
      Seam::placeBall(game, 0, kOffTable);
      game.step(idle);
      CHECK((countEvents(game, GameEvent::Type::EjectSave) == 1) == soon);
      if (soon) {
        CHECK(game.rules().ball() == ballBefore);
        CHECK(game.rules().message() == std::string_view("BALL SAVED"));
        // It was not the ball save that did it: that is still running, and
        // still unused, for the ball now back on the plunger.
        CHECK(saveActive && game.rules().ballSaveActive());
      }
      CHECK(game.ballCount() == 1);
    }
  }

  {  // With other balls in play, a ball lost just after an eject is owed
     // back: another is served and launched.
    Game game;
    run(game, 2);
    putBall(game, 0, {kScoop.center.x, 440.0F}, {0.0F, -900.0F});
    const std::size_t other = Seam::addBall(game, {600.0F, 600.0F});
    Seam::putInPlay(game, other);
    const InputState idle;
    bool out = false;
    for (int i = 0; i < 3 * kSecond && !out; ++i) {
      Seam::placeBall(game, other, {600.0F, 600.0F}, {0.0F, 0.0F});
      game.step(idle);
      out = findEvent(game, GameEvent::Type::HoleEject) != nullptr;
    }
    CHECK(out);
    Seam::placeBall(game, 0, kOffTable);
    game.step(idle);
    CHECK(countEvents(game, GameEvent::Type::EjectSave) == 1);
    CHECK(game.ballCount() + game.queuedBalls() == 2);
  }
}

void testSensors() {
  struct Pass {
    std::size_t sensor;
    Vec2 from;
    Vec2 velocity;
    int direction;
  };
  // Up and down through each circuit mouth and the spinner; right and left
  // over the top of the arch.
  const std::array<Pass, 8> passes{{
      {0, {116.0F, 455.0F}, {0.0F, -1200.0F}, 1},
      {0, {116.0F, 425.0F}, {0.0F, 600.0F}, -1},
      {1, {595.0F, 455.0F}, {0.0F, -1200.0F}, 1},
      {1, {595.0F, 425.0F}, {0.0F, 600.0F}, -1},
      {2, {340.0F, 100.0F}, {900.0F, 0.0F}, 1},
      {2, {371.0F, 100.0F}, {-900.0F, 0.0F}, -1},
      {3, {432.0F, 430.0F}, {0.0F, -1500.0F}, 1},
      {3, {432.0F, 400.0F}, {0.0F, 700.0F}, -1},
  }};
  for (const Pass& pass : passes) {
    // In slot 0 and in slot 1: the event names the ball.
    for (std::size_t slot = 0; slot < 2; ++slot) {
      PhysicsWorld world;
      if (slot == 1) {
        CHECK(world.activateBall(1, pass.from, pass.velocity));
      } else {
        world.resetBall(pass.from, pass.velocity);
      }
      int crossings = 0;
      for (int s = 0; s < 12; ++s) {
        world.step(kStep);
        for (const PhysicsEvent& e : world.events()) {
          if (e.type != PhysicsEvent::Type::Cross) {
            continue;
          }
          ++crossings;
          CHECK(e.element.kind == table::ElementKind::Sensor);
          CHECK(e.element.index == pass.sensor);
          CHECK(e.direction == pass.direction);
          CHECK(e.ball == slot);
          // The speed across the line: all of it here, give or take gravity.
          const float speed = std::fabs(pass.velocity.x) + std::fabs(pass.velocity.y);
          CHECK(std::fabs(e.speed - speed) < 80.0F);
        }
      }
      CHECK(crossings == 1);
    }
  }

  {  // Past the end of a line is not across it, and a ball put somewhere by
     // hand has crossed nothing on the way.
    PhysicsWorld world;
    world.resetBall({480.0F, 430.0F}, {0.0F, -1200.0F});  // Beside the spinner, behind the rail.
    for (int s = 0; s < 10; ++s) {
      world.step(kStep);
      for (const PhysicsEvent& e : world.events()) {
        CHECK(e.type != PhysicsEvent::Type::Cross);
      }
    }
    world.resetBall({432.0F, 430.0F}, {0.0F, 0.0F});
    world.step(kStep);
    world.resetBall({432.0F, 400.0F}, {0.0F, 0.0F});
    world.step(kStep);
    for (const PhysicsEvent& e : world.events()) {
      CHECK(e.type != PhysicsEvent::Type::Cross);
    }
  }

  {  // A sensor is never collided with: the ball's path is the path it has
     // on a table that reports nothing. (Same world, same ball; compare with
     // free flight over the spinner.)
    PhysicsWorld world;
    world.resetBall({432.0F, 430.0F}, {0.0F, -900.0F});
    Vec2 before = world.ballVelocity();
    for (int s = 0; s < 6; ++s) {
      world.step(kStep);
      const Vec2 after = world.ballVelocity();
      CHECK(after.x == before.x);
      // Gravity only: 950 px/s^2 for a step.
      CHECK(std::fabs((after.y - before.y) - 950.0F * kStep) < 0.5F);
      before = after;
    }
  }
}

// Where to put a ball so that it crosses the spinner within a step or two
// at a speed up the table (negative: down). A ball put somewhere by hand is
// watched from the end of its first slice, so it starts more than a slice's
// travel short of the line.
Vec2 spinnerStart(float speedUp) {
  const float lead = 1.5F + std::fabs(speedUp) / 400.0F;
  return {432.0F, table::kSpinner.a.y + (speedUp > 0.0F ? lead : -lead)};
}

void testSpinner() {
  struct Case {
    float speed;  // Negative: down the table.
    int spins;
  };
  // Slowly, fast, flat out, and backwards.
  // (Gravity takes or adds about 10 px/s before the ball is at the line, so
  // the speeds are chosen from the middle of a count's range.)
  for (const Case& c : {Case{100.0F, 1}, Case{520.0F, 3}, Case{1570.0F, 10}, Case{2950.0F, 12},
                        Case{-970.0F, 6}, Case{-40.0F, 1}}) {
    Game game;
    run(game, 2);
    const bool up = c.speed > 0.0F;
    const std::uint64_t scoreBefore = game.rules().score();
    putBall(game, 0, spinnerStart(c.speed), {0.0F, -c.speed});
    const InputState idle;
    std::vector<int> spinAt;
    int crossedAt = -1;
    for (int i = 0; i < 2 * kSecond; ++i) {
      // Out of the way once it is through, so that it crosses once.
      if (crossedAt >= 0) {
        Seam::placeBall(game, 0, {300.0F, 560.0F}, {0.0F, 0.0F});
      }
      game.step(idle);
      for (const PhysicsEvent& e : game.events()) {
        if (e.type == PhysicsEvent::Type::Cross && e.element.index == table::kSpinnerSensor) {
          CHECK(crossedAt < 0);
          crossedAt = i;
          CHECK(e.direction == (up ? 1 : -1));
        }
      }
      for (int n = 0; n < countEvents(game, GameEvent::Type::Spin); ++n) {
        spinAt.push_back(i);
      }
      CHECK(game.spinnerPhase() >= 0.0F && game.spinnerPhase() < 1.0F);
      CHECK(game.spinsOwed() >= 0 && game.spinsOwed() <= table::kMaxSpinsOwed);
    }
    CHECK(crossedAt >= 0);
    CHECK(static_cast<int>(spinAt.size()) == c.spins);
    CHECK(game.spinCount() == static_cast<std::uint32_t>(c.spins));
    // The first at the crossing, then one every six steps.
    if (!spinAt.empty()) {
      CHECK(spinAt[0] == crossedAt);
    }
    for (std::size_t i = 1; i < spinAt.size(); ++i) {
      CHECK(spinAt[i] - spinAt[i - 1] == table::kSpinPayoutSteps);
    }
    CHECK(game.spinsOwed() == 0);
    CHECK(game.rules().score() - scoreBefore ==
          static_cast<std::uint64_t>(c.spins) * pinball::points::kSpin);
    // Half a turn a spin: at rest it lies flat, one way up or the other.
    const float phase = game.spinnerPhase();
    CHECK(phase == (c.spins % 2 == 0 ? 0.0F : 0.5F));
  }

  {  // The blade turns the way the ball went: forwards its phase rises
     // through the turn, backwards it falls.
    for (const bool up : {true, false}) {
      Game game;
      run(game, 2);
      putBall(game, 0, spinnerStart(up ? 1500.0F : -1500.0F), {0.0F, up ? -1500.0F : 1500.0F});
      run(game, 3);
      const float early = game.spinnerPhase();
      run(game, 2);
      const float later = game.spinnerPhase();
      CHECK(early > 0.0F && later > 0.0F);
      CHECK(up ? later > early : later < early);
    }
  }

  {  // A second and a third ball through while spins are owed add theirs,
     // up to 24 owed and no more, and the count still comes one every six
     // steps.
    Game game;
    run(game, 2);
    const InputState idle;
    std::vector<int> spinAt;
    int mostOwed = 0;
    for (int i = 0; i < 3 * kSecond; ++i) {
      if (i == 0 || i == 4 || i == 8) {
        putBall(game, 0, spinnerStart(2900.0F), {0.0F, -2900.0F});
      } else if (i > 10) {
        Seam::placeBall(game, 0, {300.0F, 560.0F}, {0.0F, 0.0F});
      }
      game.step(idle);
      for (int n = 0; n < countEvents(game, GameEvent::Type::Spin); ++n) {
        spinAt.push_back(i);
      }
      mostOwed = std::max(mostOwed, game.spinsOwed());
      CHECK(game.spinsOwed() <= table::kMaxSpinsOwed);
    }
    CHECK(mostOwed == table::kMaxSpinsOwed);
    // Twelve from the first less the two counted before the cap was met,
    // and so on: what matters is the cap and the pace.
    CHECK(spinAt.size() > 24 && spinAt.size() <= 27);
    for (std::size_t i = 1; i < spinAt.size(); ++i) {
      CHECK(spinAt[i] - spinAt[i - 1] == table::kSpinPayoutSteps);
    }
  }

  {  // The spinner turns on a dead table too, but nothing scores.
    Game game;
    run(game, 2);
    putBall(game, 0, {300.0F, 560.0F}, {0.0F, 0.0F});
    InputState nudge;
    for (int i = 0; i < 8 && !game.rules().tilted(); ++i) {
      nudge.nudgeUp = (i % 2) == 0;
      game.step(nudge);
    }
    CHECK(game.rules().tilted());
    const std::uint64_t score = game.rules().score();
    putBall(game, 0, spinnerStart(970.0F), {0.0F, -970.0F});
    run(game, 3);
    Seam::placeBall(game, 0, {300.0F, 560.0F}, {0.0F, 0.0F});
    run(game, kSecond);
    CHECK(game.spinCount() == 6);
    CHECK(game.rules().score() == score);
  }
}

void testKickback() {
  // Above the pocket, in the left outlane.
  constexpr float kTopY = 700.0F;
  {  // Not lit: the ball goes down the outlane and is lost.
    Game game;
    run(game, 2);
    Seam::rules(game).setKickbackLit(false);
    run(game, 1);
    CHECK(!game.kickbackArmed());
    const std::uint32_t drains = game.drainCount();
    putBall(game, 0, {59.0F, kTopY}, {0.0F, 200.0F});
    run(game, 2 * kSecond);
    CHECK(game.kickbackCount() == 0);
    CHECK(game.drainCount() == drains + 1);
  }

  {  // Lit: whatever the ball's speed and wherever across the lane it is,
     // the kickback takes it, once, goes out, carries it over the divider
     // and sets it down above the left inlane; the ball rolls over the
     // inlane's switch and is back in play. Every time.
    int kicks = 0;
    for (const float speed : {0.0F, 60.0F, 400.0F, 810.0F, 900.0F, 990.0F, 1600.0F, 2900.0F}) {
      for (const float x : {53.0F, 56.0F, 59.0F, 62.0F, 65.0F}) {
        for (const float vx : {-60.0F, 0.0F, 60.0F}) {
          Game game;
          run(game, 2);
          CHECK(game.kickbackArmed());
          putBall(game, 0, {x, kTopY}, {vx, speed});
          const InputState idle;
          int firedAt = -1;
          int returnedAt = -1;
          bool inlane = false;
          Vec2 last = Seam::position(game, 0);
          float lastSpeed = 1.0e9F;
          for (int i = 0; i < 4 * kSecond && !inlane; ++i) {
            game.step(idle);
            CHECK(countEvents(game, GameEvent::Type::KickbackFired) <= 1);
            if (findEvent(game, GameEvent::Type::KickbackFired) != nullptr) {
              CHECK(firedAt < 0);
              firedAt = i;
              CHECK(!game.kickbackArmed());
              CHECK(Seam::held(game, 0));
              last = game.ball().position;
              // Taken in the pocket, and already a step on its way.
              CHECK(distance(last, table::kKickbackZone.center) <
                    table::kKickbackZone.radius + 10.0F);
            } else if (firedAt >= 0 && returnedAt < 0 && Seam::held(game, 0)) {
              // On its way: in view, moving along the path, ever slower.
              const BallView view = game.ball();
              CHECK(view.visible && !view.locked);
              const float moved = distance(view.position, last);
              // (A step that turns a corner of the path is a little shorter.)
              CHECK(moved <= lastSpeed + 1.0F);
              CHECK(moved < 10.0F);
              lastSpeed = moved;
              last = view.position;
            }
            if (findEvent(game, GameEvent::Type::KickbackReturned) != nullptr) {
              returnedAt = i;
              const Vec2 p = Seam::position(game, 0);
              const Vec2 v = Seam::velocity(game, 0);
              const table::Hole& runway = table::kHoles[table::kRunwayHole];
              CHECK(near(p, runway.exitPosition));
              CHECK(v.x == 0.0F);
              // It had all but stopped there.
              CHECK(lastSpeed < 0.5F);
              CHECK(v.y >= table::kKickbackExitVelocity.y -
                               static_cast<float>(table::kRampExitJitter));
              CHECK(v.y <= table::kKickbackExitVelocity.y +
                               static_cast<float>(table::kRampExitJitter));
              CHECK(distance(last, p) < 6.0F);
            }
            if (returnedAt >= 0) {
              for (const PhysicsEvent& e : game.events()) {
                inlane = inlane ||
                         (e.type == PhysicsEvent::Type::Enter &&
                          e.element.kind == table::ElementKind::Rollover &&
                          table::kRollovers[e.element.index].id == table::RolloverId::WingI);
              }
            }
          }
          CHECK(firedAt >= 0);
          CHECK(returnedAt - firedAt ==
                static_cast<int>(table::kKickbackSeconds * kSecond + 0.5) - 1);
          CHECK(inlane);
          CHECK(game.kickbackCount() == 1);
          CHECK(game.drainCount() == 0);
          CHECK(game.rules().message() == std::string_view("KICKBACK"));
          ++kicks;
        }
      }
    }
    CHECK(kicks == 120);
  }

  {  // It fires once. Two balls come down together: the first is taken, the
     // second, with the kickback already in use and out, is lost.
    Game game;
    Seam::rules(game).setBallSaveEnabled(false);  // The second ball is to stay lost.
    run(game, 2);
    putBall(game, 0, {59.0F, kTopY}, {0.0F, 300.0F});
    const std::size_t second = Seam::addBall(game, {59.0F, kTopY - 30.0F}, {0.0F, 300.0F});
    Seam::putInPlay(game, second);
    const std::uint32_t drains = game.drainCount();
    run(game, kSecond);
    CHECK(game.kickbackCount() == 1);
    CHECK(game.drainCount() == drains + 1);
    CHECK(Seam::live(game, 0) && !Seam::live(game, second));
  }

  {  // A second ball already in the pocket when the kickback is lit again
     // while the first is still on its way: it is taken too, and the two
     // are set down apart, not on top of each other.
    Game game;
    run(game, 2);
    putBall(game, 0, {59.0F, kTopY + 20.0F}, {0.0F, 300.0F});
    const std::size_t second = Seam::addBall(game, {600.0F, 600.0F});
    Seam::putInPlay(game, second);
    const InputState idle;
    bool first = false;
    for (int i = 0; i < kSecond && !first; ++i) {
      Seam::placeBall(game, second, {600.0F, 600.0F}, {0.0F, 0.0F});
      game.step(idle);
      first = game.kickbackCount() == 1;
    }
    CHECK(first);
    // The second sits in the pocket, the kickback out: nothing.
    Seam::placeBall(game, second, table::kKickbackZone.center, {0.0F, 0.0F});
    game.step(idle);
    CHECK(game.kickbackCount() == 1);
    CHECK(!Seam::held(game, second));
    // Lit again with the ball inside.
    Seam::placeBall(game, second, table::kKickbackZone.center, {0.0F, 0.0F});
    Seam::rules(game).setKickbackLit(true);
    game.step(idle);  // The rules' word reaches the table at the end of a step,
    Seam::placeBall(game, second, table::kKickbackZone.center, {0.0F, 0.0F});
    game.step(idle);  // and the ball is taken in the next.
    CHECK(game.kickbackCount() == 2);
    CHECK(Seam::held(game, second));
    std::vector<int> setDownAt;
    for (int i = 0; i < 2 * kSecond; ++i) {
      game.step(idle);
      CHECK(countEvents(game, GameEvent::Type::KickbackReturned) <= 1);
      if (findEvent(game, GameEvent::Type::KickbackReturned) != nullptr) {
        setDownAt.push_back(i);
      }
      if (Seam::live(game, 0) && Seam::live(game, second) && !Seam::held(game, 0) &&
          !Seam::held(game, second)) {
        CHECK(distance(Seam::position(game, 0), Seam::position(game, second)) >
              2.0F * table::kBallRadius - 2.0F);
      }
    }
    CHECK(setDownAt.size() == 2);
    if (setDownAt.size() == 2) {
      CHECK(setDownAt[1] - setDownAt[0] >=
            static_cast<int>(Game::kHoleReleaseSpacingSeconds * kSecond));
    }
  }

  {  // The kickback does not set a ball down onto one resting at the end of
     // its path: it waits.
    Game game;
    run(game, 2);
    putBall(game, 0, {59.0F, kTopY}, {0.0F, 300.0F});
    const std::size_t blocker = Seam::addBall(game, {600.0F, 600.0F});
    const Vec2 exit = table::kHoles[table::kRunwayHole].exitPosition;
    const InputState idle;
    int returnedAt = -1;
    constexpr int kBlocked = 2 * kSecond;
    for (int i = 0; i < 4 * kSecond && returnedAt < 0; ++i) {
      Seam::placeBall(game, blocker, i < kBlocked ? Vec2{exit.x, exit.y + 10.0F}
                                                  : Vec2{600.0F, 600.0F},
                      {0.0F, 0.0F});
      game.step(idle);
      if (findEvent(game, GameEvent::Type::KickbackReturned) != nullptr) {
        returnedAt = i;
      }
    }
    CHECK(returnedAt >= kBlocked && returnedAt <= kBlocked + 2);
  }

  {  // Two balls in the pocket in the same step, the kickback armed: the
     // table takes one and is spent at once; the other is not taken.
    PhysicsWorld world;
    world.deactivateBall(0);
    CHECK(world.activateBall(0, {59.0F, 736.0F}, {0.0F, 0.0F}));
    CHECK(world.activateBall(1, {59.0F, 744.0F}, {0.0F, 0.0F}));
    world.setKickbackArmed(true);
    world.step(1.0F / 120.0F);
    int taken = 0;
    for (std::size_t slot = 0; slot < 2; ++slot) {
      if (world.ballHeld(slot) && world.heldBy(slot).kind == table::ElementKind::Kickback) {
        ++taken;
      }
    }
    CHECK(taken == 1);
    CHECK(!world.kickbackArmed());
    world.step(1.0F / 120.0F);
    CHECK(!world.ballHeld(0) || !world.ballHeld(1));
  }

  {  // The rules hear of the kickback's ball when it is taken and again when
     // it is set down; its lamp flashes in between.
    Game game;
    run(game, 2);
    putBall(game, 0, {59.0F, kTopY}, {0.0F, 300.0F});
    const InputState idle;
    bool firing = false;
    bool done = false;
    for (int i = 0; i < 2 * kSecond && !done; ++i) {
      game.step(idle);
      if (Seam::held(game, 0)) {
        firing = firing || (game.rules().kickbackFiring() &&
                            game.rules().lampState(table::LampId::Kickback) ==
                                pinball::LampState::FastBlink);
      }
      done = findEvent(game, GameEvent::Type::KickbackReturned) != nullptr;
    }
    CHECK(firing && done);
    CHECK(!game.rules().kickbackFiring());
    CHECK(game.rules().lampState(table::LampId::Kickback) == pinball::LampState::Off);
  }

  {  // The kickback: lit at the start of a game, out when it has fired,
     // lit again by all four return lanes; never lit on a tilted table.
    Rules rules(1.0 / 120.0);
    CHECK(rules.kickbackLit());
    rules.onKickback();
    CHECK(!rules.kickbackLit());
    for (std::size_t i = 0; i < table::kRollovers.size(); ++i) {
      if (table::kRollovers[i].group != table::RolloverGroup::TopLane) {
        CHECK(!rules.kickbackLit());
        rules.onRollover(i);
      }
    }
    CHECK(rules.kickbackLit());
    rules.onBallInPlay();
    for (int i = 0; i < 4; ++i) {
      rules.onNudge();
    }
    CHECK(rules.tilted());
    CHECK(!rules.kickbackLit());
  }
}

// Nothing gets through at the speed cap: the standing door bank against one
// to three balls at once from every angle, the bank while it is dropped and
// raised under them, and the kickback's pocket, which must not be stepped
// over. 3,000 shots at the doors.
void testNothingTunnels() {
  constexpr float kCap = PhysicsWorld::kMaxBallSpeed * PhysicsWorld::kPixelsPerMeter;
  // A small generator of the test's own, so that the shots are the same
  // on every run.
  std::uint64_t state = 0x2545F4914F6CDD1DULL;
  const auto unit = [&state] {
    state ^= state << 13U;
    state ^= state >> 7U;
    state ^= state << 17U;
    return static_cast<float>(state >> 40U) / 16777216.0F;
  };
  // A place below the doors and a velocity at the cap towards a point on
  // the door line.
  struct Shot {
    Vec2 from;
    Vec2 velocity;
  };
  const auto shot = [&unit] {
    const Vec2 from{200.0F + unit() * 160.0F, 470.0F + unit() * 120.0F};
    const Vec2 at{246.0F + unit() * 66.0F, table::kDoorY};
    const Vec2 d = at - from;
    const float length = std::sqrt(d.x * d.x + d.y * d.y);
    return Shot{from, {d.x / length * kCap, d.y / length * kCap}};
  };
  // Inside the Hangar: between its walls, above the door line and under
  // its peaked roof.
  const auto insidePocket = [](Vec2 p) {
    const float peak = table::kHangarRoofLeft.b.x;
    const float slope = (table::kHangarRoofLeft.a.y - table::kHangarRoofLeft.b.y) /
                        (peak - table::kHangarRoofLeft.a.x);
    const float roof = table::kHangarRoofLeft.b.y + std::fabs(p.x - peak) * slope;
    return p.x > table::kHangarLeft.a.x && p.x < table::kHangarRight.a.x &&
           p.y < table::kDoorY - 2.0F && p.y > roof;
  };

  int shots = 0;
  int through = 0;
  int doorHits = 0;
  for (int n = 0; n < 1500; ++n) {
    PhysicsWorld world;
    const std::size_t balls = 1U + static_cast<std::size_t>(n % 3);
    for (std::size_t slot = 0; slot < balls; ++slot) {
      Shot one = shot();
      // Apart from each other at the start.
      one.from.x += static_cast<float>(slot) * 30.0F;
      if (slot == 0) {
        world.resetBall(one.from, one.velocity);
      } else {
        CHECK(world.activateBall(slot, one.from, one.velocity));
      }
      ++shots;
    }
    bool got = false;
    for (int s = 0; s < 40; ++s) {
      world.step(kStep);
      for (const PhysicsEvent& e : world.events()) {
        doorHits += e.element.kind == table::ElementKind::DropTarget ? 1 : 0;
        got = got || (e.type == PhysicsEvent::Type::Capture &&
                      e.element == kHangarElement);
      }
      for (std::size_t slot = 0; slot < balls; ++slot) {
        got = got || insidePocket(world.ballPosition(slot));
      }
    }
    through += got ? 1 : 0;
  }
  CHECK(shots == 3000);
  CHECK(through == 0);
  CHECK(doorHits > 1000);

  // The bank dropped and raised again and again while balls arrive at the
  // cap: a ball is taken only through an open mouth, is never inside a
  // standing door, and never passes a standing bank.
  int transitions = 0;
  int taken = 0;
  int violations = 0;
  for (int n = 0; n < 500; ++n) {
    Game game;
    run(game, 2);
    Seam::rules(game).setMultiballEnabled(false);
    Seam::rules(game).setKickbackLit(false);
    const std::size_t balls = 1U + static_cast<std::size_t>(n % 3);
    for (std::size_t slot = 0; slot < balls; ++slot) {
      const Shot one = shot();
      const Vec2 from{one.from.x + static_cast<float>(slot) * 30.0F, one.from.y};
      if (slot == 0) {
        putBall(game, 0, from, one.velocity);
      } else {
        const std::size_t added = Seam::addBall(game, from, one.velocity);
        Seam::putInPlay(game, added);
      }
    }
    const int dropAt = n % 9;
    const int raiseAt = dropAt + 1 + (n / 9) % 12;
    const InputState idle;
    for (int i = 0; i < 45; ++i) {
      if (i == dropAt) {
        Seam::dropDoorBank(game);
        ++transitions;
      }
      if (i == raiseAt) {
        Seam::raiseDoorBank(game);
      }
      const bool upBefore = game.doorBankUp();
      std::array<Vec2, Game::kMaxBalls> before{};
      std::array<bool, Game::kMaxBalls> freeBefore{};
      for (std::size_t slot = 0; slot < Game::kMaxBalls; ++slot) {
        freeBefore[slot] = Seam::live(game, slot) && !Seam::held(game, slot);
        before[slot] = Seam::position(game, slot);
      }
      game.step(idle);
      transitions += !upBefore && game.doorBankUp() ? 1 : 0;
      for (std::size_t slot = 0; slot < Game::kMaxBalls; ++slot) {
        if (!freeBefore[slot] || !Seam::live(game, slot)) {
          continue;
        }
        if (Seam::held(game, slot)) {
          // Another hole may take a ball that was turned away.
          const bool byHangar = distance(before[slot], kHangar.center) < 60.0F &&
                                before[slot].y < table::kDoorY;
          taken += byHangar ? 1 : 0;
          violations += byHangar && upBefore ? 1 : 0;
          continue;
        }
        const Vec2 after = Seam::position(game, slot);
        const bool inMouth = std::fabs(after.x - table::kDoorMouthCenter.x) < 37.0F;
        // (At the cap a ball presses up to about 3 units into a door for
        // the step of the impact before it is thrown back.)
        if (game.doorBankUp() && depthInDoors(after) > 4.0F) {
          ++violations;
        }
        if (upBefore && game.doorBankUp() && inMouth &&
            (before[slot].y - table::kDoorY) * (after.y - table::kDoorY) < 0.0F) {
          ++violations;
        }
      }
    }
  }
  CHECK(transitions > 800);
  CHECK(taken > 30);
  CHECK(violations == 0);

  // The kickback's pocket at the cap: one to three balls straight down the
  // outlane, from every phase of the step. The first is always taken.
  int kicked = 0;
  int outlaneRuns = 0;
  for (int n = 0; n < 300; ++n) {
    Game game;
    run(game, 2);
    const std::size_t balls = 1U + static_cast<std::size_t>(n % 3);
    const float phase = static_cast<float>(n % 25);
    putBall(game, 0, {53.0F + static_cast<float>(n % 13), 640.0F + phase}, {0.0F, kCap});
    for (std::size_t slot = 1; slot < balls; ++slot) {
      const std::size_t added =
          Seam::addBall(game, {59.0F, 640.0F + phase - 30.0F * static_cast<float>(slot)},
                        {0.0F, kCap});
      Seam::putInPlay(game, added);
    }
    ++outlaneRuns;
    run(game, 20);
    kicked += game.kickbackCount() == 1 ? 1 : 0;
    CHECK(Seam::held(game, 0) || game.kickbackCount() == 0);
  }
  CHECK(kicked == outlaneRuns);
  std::fprintf(stderr,
               "tunnelling at the speed cap: door bank standing %d of %d shots through (%d door "
               "hits); bank in transition %d violations in %d transitions (%d taken through the "
               "open mouth); kickback took the first ball in %d of %d\n",
               through, shots, doorHits, violations, transitions, taken, kicked, outlaneRuns);
}

}  // namespace

void testElements() {
  testNothingTunnels();
  testDoorBank();
  testLock();
  testScoop();
  testSensors();
  testSpinner();
  testKickback();
}
