#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>
#include <limits>
#include <memory>
#include <vector>

#include "Check.hpp"
#include "GameTestAccess.hpp"
#include "Suites.hpp"
#include "core/Game.hpp"
#include "core/Physics.hpp"
#include "core/HoleLook.hpp"
#include "core/Rules.hpp"
#include "core/Snapshot.hpp"
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
  // One bank lights the lock for these tests (three in play).
  Seam::rules(game).setBanksPerLock(1);
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
    Seam::rules(game).setBanksPerLock(1);
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
    // The doors come back up behind it; a bank lights the lock again.
    run(game, kSecond);
    CHECK(game.doorBankUp() && !game.rules().lockLit());
    markDoors(game);
    run(game, 2);
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

  {  // Let go on command, with no ball arriving: the first leaves as soon as
     // it has rolled from its bay to the cup and come up at the exit, lobbed
     // left; the second 0.4 s later, kicked right.
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
    CHECK(game.bayTaken(0) && game.bayTaken(1));
    for (int i = 0; i < 2 * kSecond; ++i) {
      game.step(idle);
      if (const GameEvent* event = findEvent(game, GameEvent::Type::LockReleased)) {
        steps.push_back(i);
        velocities.push_back(Seam::velocity(game, event->ball));
        // The first bay empties first; the second's lamp is still its own.
        if (steps.size() == 1) {
          CHECK(!game.bayTaken(0) && game.bayTaken(1));
        }
      }
    }
    CHECK(!game.bayTaken(0) && !game.bayTaken(1));
    CHECK(steps.size() == 2);
    if (steps.size() == 2) {
      CHECK(steps[0] == static_cast<int>(Game::kLockRollSteps + Game::kHoleRiseSteps) - 1);
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
    run(game, kSecond);
    markDoors(game);  // Open again for the next.
    run(game, 2);
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
        CHECK(rules.message() == std::string_view("SCRAMBLE BONUS"));
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
      putBall(game, 0, {kScoop.center.x - 12.0F, 445.0F}, {0.3F * speed, -speed});
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
          // Drawn going down into it (testHoleViews), not gone at once.
          CHECK(game.ball().visible && game.ball().sink > 0.0F);
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
      putBall(game, 0, {kScoop.center.x - 10.0F, 440.0F}, {270.0F, -900.0F});
      const InputState idle;
      std::size_t second = Game::kMaxBalls;
      std::vector<int> outAt;
      std::vector<std::size_t> outBall;
      int bothTakenAt = -1;
      for (int i = 0; i < 5 * kSecond; ++i) {
        if (i == gap) {
          second = Seam::addBall(game, {kScoop.center.x - 16.0F, 470.0F}, {270.0F, -900.0F});
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
     // waits, down in the cup, until the exit is clear.
    Game game;
    run(game, 2);
    putBall(game, 0, {kScoop.center.x - 12.0F, 445.0F}, {270.0F, -900.0F});
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
    // Once clear it comes up over the rise, and goes.
    CHECK(returnedAt >= kBlockedSteps + static_cast<int>(Game::kHoleRiseSteps) - 1);
    CHECK(returnedAt <= kBlockedSteps + static_cast<int>(Game::kHoleRiseSteps) + 2);
  }

  {  // The short save after an eject. A ball that drains within 2.5 s of
     // being returned is flagged and, by these rules, given back; the ball
     // save is not used up by it. Later than that it is an ordinary drain.
    for (const bool soon : {true, false}) {
      Game game;
      run(game, 2);
      putBall(game, 0, {kScoop.center.x - 12.0F, 445.0F}, {270.0F, -900.0F});
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
    putBall(game, 0, {kScoop.center.x - 12.0F, 445.0F}, {270.0F, -900.0F});
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
      {3, {356.0F, 419.0F}, {0.0F, -1500.0F}, 1},
      {3, {356.0F, 389.0F}, {0.0F, 700.0F}, -1},
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
    world.resetBall({300.0F, 430.0F}, {0.0F, -1200.0F});  // Beside the spinner, past its end.
    for (int s = 0; s < 10; ++s) {
      world.step(kStep);
      for (const PhysicsEvent& e : world.events()) {
        CHECK(e.type != PhysicsEvent::Type::Cross);
      }
    }
    world.resetBall({356.0F, 419.0F}, {0.0F, 0.0F});
    world.step(kStep);
    world.resetBall({356.0F, 389.0F}, {0.0F, 0.0F});
    world.step(kStep);
    for (const PhysicsEvent& e : world.events()) {
      CHECK(e.type != PhysicsEvent::Type::Cross);
    }
  }

  {  // A sensor is never collided with: the ball's path is the path it has
     // on a table that reports nothing. (Same world, same ball; compare with
     // free flight over the spinner.)
    PhysicsWorld world;
    world.resetBall({356.0F, 430.0F}, {0.0F, -900.0F});
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
Vec2 spinnerStart(float speedUp, std::size_t spinner = table::kLaneSpinner) {
  const float lead = 1.5F + std::fabs(speedUp) / 400.0F;
  const table::Spinner& s = table::kSpinners[spinner];
  return {(s.a.x + s.b.x) * 0.5F, s.a.y + (speedUp > 0.0F ? lead : -lead)};
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

// Revision 3: three spinners, each with its own turns and its own blade.
void testThreeSpinners() {
  static_assert(Game::kSpinners == 3);
  static_assert(table::kSpinners[table::kLaneSpinner].bothWays);
  static_assert(!table::kSpinners[table::kCircuitSpinnerLeft].bothWays &&
                !table::kSpinners[table::kCircuitSpinnerRight].bothWays);
  const InputState idle;
  {  // Up a circuit: its spinner turns, the others stay as they were; the
     // events name it; the score is a spin's for each turn.
    for (const std::size_t which : {table::kCircuitSpinnerLeft, table::kCircuitSpinnerRight}) {
      Game game;
      run(game, 2);
      const std::uint64_t scoreBefore = game.rules().score();
      putBall(game, 0, spinnerStart(1270.0F, which), {0.0F, -1270.0F});
      int crossedAt = -1;
      std::vector<int> spinAt;
      for (int i = 0; i < 2 * kSecond; ++i) {
        if (crossedAt >= 0) {
          Seam::placeBall(game, 0, kParked, {0.0F, 0.0F});
        }
        game.step(idle);
        for (const PhysicsEvent& e : game.events()) {
          if (e.type == PhysicsEvent::Type::Cross &&
              e.element.index == table::kSpinners[which].sensor) {
            crossedAt = i;
          }
        }
        for (const GameEvent& e : game.gameEvents()) {
          if (e.type == GameEvent::Type::Spin) {
            CHECK(e.index == which);
            spinAt.push_back(i);
          }
        }
        for (std::size_t other = 0; other < Game::kSpinners; ++other) {
          if (other != which) {
            CHECK(game.spinnerPhase(other) == 0.0F && game.spinCount(other) == 0);
          }
        }
      }
      CHECK(crossedAt >= 0);
      // 1270 px/s less what gravity took on the way: eight turns.
      CHECK(spinAt.size() == 8);
      CHECK(!spinAt.empty() && spinAt[0] == crossedAt);
      CHECK(game.spinCount(which) == 8 && game.spinCount() == 8);
      CHECK(game.rules().score() - scoreBefore == 8U * pinball::points::kSpin);
    }
  }

  {  // Down a circuit (a made circuit's return) the ball crosses the mouth,
     // and its spinner does not turn for it.
    Game game;
    run(game, 2);
    putBall(game, 0, spinnerStart(-900.0F, table::kCircuitSpinnerLeft), {0.0F, 900.0F});
    bool crossed = false;
    for (int i = 0; i < kSecond; ++i) {
      if (crossed) {
        Seam::placeBall(game, 0, kParked, {0.0F, 0.0F});
      }
      game.step(idle);
      for (const PhysicsEvent& e : game.events()) {
        crossed = crossed || (e.type == PhysicsEvent::Type::Cross &&
                              e.element.index == table::kSpinners[table::kCircuitSpinnerLeft].sensor);
      }
      CHECK(countEvents(game, GameEvent::Type::Spin) == 0);
    }
    CHECK(crossed);
    CHECK(game.spinCount() == 0 && game.spinnerPhase(table::kCircuitSpinnerLeft) == 0.0F);
  }

  {  // Two at once: the lane's and the right circuit's, crossed in the same
     // step. Each pays one turn every six steps on its own count, side by
     // side, and each blade turns its own way at its own pace.
    Game game;
    run(game, 2);
    putBall(game, 0, spinnerStart(1570.0F, table::kLaneSpinner), {0.0F, -1570.0F});
    const std::size_t second = Seam::addBall(
        game, spinnerStart(1270.0F, table::kCircuitSpinnerRight), {0.0F, -1270.0F});
    CHECK(second == 1);
    Seam::putInPlay(game, second);
    std::array<std::vector<int>, Game::kSpinners> spinAt{};
    for (int i = 0; i < 2 * kSecond; ++i) {
      if (i >= 3) {
        Seam::placeBall(game, 0, kParked, {0.0F, 0.0F});
        Seam::placeBall(game, second, {kParked.x + 60.0F, kParked.y}, {0.0F, 0.0F});
      }
      game.step(idle);
      for (const GameEvent& e : game.gameEvents()) {
        if (e.type == GameEvent::Type::Spin) {
          CHECK(e.index < Game::kSpinners);
          spinAt[e.index].push_back(i);
        }
      }
      if (i == 4) {
        // Both under way, neither waiting on the other.
        CHECK(game.spinsOwed(table::kLaneSpinner) > 0 &&
              game.spinsOwed(table::kCircuitSpinnerRight) > 0);
        CHECK(game.spinsOwed() == game.spinsOwed(table::kLaneSpinner) +
                                      game.spinsOwed(table::kCircuitSpinnerRight));
        CHECK(game.spinnerPhase(table::kLaneSpinner) > 0.0F &&
              game.spinnerPhase(table::kCircuitSpinnerRight) > 0.0F);
      }
    }
    CHECK(spinAt[table::kLaneSpinner].size() == 10);
    CHECK(spinAt[table::kCircuitSpinnerRight].size() == 8);
    CHECK(spinAt[table::kCircuitSpinnerLeft].empty());
    // They started together and kept their own pace: the first eight side by side.
    for (std::size_t k = 0; k < 8 && k < spinAt[table::kCircuitSpinnerRight].size(); ++k) {
      CHECK(spinAt[table::kLaneSpinner][k] == spinAt[table::kCircuitSpinnerRight][k]);
    }
    for (const std::vector<int>& at : spinAt) {
      for (std::size_t k = 1; k < at.size(); ++k) {
        CHECK(at[k] - at[k - 1] == table::kSpinPayoutSteps);
      }
    }
    CHECK(game.spinCount() == 18 && game.spinsOwed() == 0);
    // At rest flat, one way up or the other, each by its own count.
    CHECK(game.spinnerPhase(table::kLaneSpinner) == 0.0F);
    CHECK(game.spinnerPhase(table::kCircuitSpinnerRight) == 0.0F);
  }
}

// Revision 3: the training drone.
constexpr int kPatrolSteps = static_cast<int>(table::kDronePatrolSeconds * kSecond + 0.5);
constexpr int kDiveSteps = static_cast<int>(table::kDroneDiveSeconds * kSecond + 0.5);

// Steps a game with its ball parked out of the way until an event; returns
// the step it came in, or -1.
int stepUntil(Game& game, GameEvent::Type type, int steps, const GameEvent** found = nullptr) {
  const InputState idle;
  for (int i = 0; i < steps; ++i) {
    Seam::placeBall(game, 0, kParked, {0.0F, 0.0F});
    game.step(idle);
    if (const GameEvent* e = findEvent(game, type)) {
      if (found != nullptr) {
        *found = e;
      }
      return i;
    }
  }
  return -1;
}

// Shoots the ball up the lane, through the gate, into the drone from
// below; returns the DroneHit event's index, or -1 if it was not hit.
int hitDrone(Game& game) {
  const InputState idle;
  // Where the lane is clear of its walls under it: the middle of its patrol.
  for (int i = 0; i < 2 * (kPatrolSteps + kDiveSteps) &&
                  !(game.drone().up && game.drone().sink == 0.0F &&
                    std::fabs(game.drone().position.x - 356.0F) < 12.0F);
       ++i) {
    Seam::placeBall(game, 0, kParked, {0.0F, 0.0F});
    game.step(idle);
  }
  Seam::placeBall(game, 0, {game.drone().position.x, 350.0F}, {0.0F, -900.0F});
  Seam::putInPlay(game, 0);
  int stage = -1;
  for (int i = 0; i < 30 && stage < 0; ++i) {
    game.step(idle);
    if (const GameEvent* e = findEvent(game, GameEvent::Type::DroneHit)) {
      CHECK(e->ball == 0);
      stage = e->index;
    }
  }
  return stage;
}

void testDrone() {
  const InputState idle;
  {  // Its patrol: on its line and within its range, the same every time;
     // at the end of the patrol it dives (DroneDive), drawn sinking out of
     // sight, and comes up again (DroneRise, index 0) a dive later, drawn
     // coming up, solid from its first step up.
    Game game;
    run(game, 2);
    float lowest = 1.0e9F;
    float highest = -1.0e9F;
    int diveAt = -1;
    int riseAt = -1;
    float lastSink = 0.0F;
    for (int i = 0; i < kPatrolSteps + kDiveSteps + kSecond; ++i) {
      Seam::placeBall(game, 0, kParked, {0.0F, 0.0F});
      game.step(idle);
      const pinball::DroneView drone = game.drone();
      CHECK(drone.position.y == table::kDroneY && drone.recoil == 0.0F && !drone.knocked);
      CHECK(drone.sink >= 0.0F && drone.sink <= 1.0F);
      if (findEvent(game, GameEvent::Type::DroneDive) != nullptr) {
        CHECK(diveAt < 0 && drone.up == false);
        diveAt = i;
      }
      if (const GameEvent* rise = findEvent(game, GameEvent::Type::DroneRise)) {
        CHECK(riseAt < 0 && drone.up && rise->index == 0);
        riseAt = i;
        lastSink = 1.0F;
      }
      CHECK(countEvents(game, GameEvent::Type::DroneDown) == 0);
      if (diveAt < 0) {
        CHECK(drone.up && drone.sink == 0.0F);
        lowest = std::min(lowest, drone.position.x);
        highest = std::max(highest, drone.position.x);
      } else if (riseAt < 0) {
        // Sinking, then out of sight.
        CHECK(!drone.up && drone.sink >= lastSink);
        const int down = i - diveAt + 1;
        CHECK((drone.sink == 1.0F) == (down >= static_cast<int>(table::kDroneSinkSeconds * kSecond + 0.5)));
        lastSink = drone.sink;
      } else {
        // Coming up, then up.
        CHECK(drone.up && drone.sink <= lastSink);
        const int up = i - riseAt + 1;
        CHECK((drone.sink == 0.0F) == (up >= static_cast<int>(table::kDroneRiseSeconds * kSecond + 0.5)));
        lastSink = drone.sink;
      }
    }
    CHECK(diveAt == kPatrolSteps - 3);  // Two steps before the loop, and the patrol's last.
    CHECK(riseAt - diveAt == kDiveSteps);
    // Out to the right end and back to the left.
    CHECK(lowest == table::kDroneLeftX || lowest < table::kDroneLeftX + 0.5F);
    CHECK(highest > table::kDroneRightX - 0.5F && highest <= table::kDroneRightX);
  }

  {  // It comes up only when its place is clear: a ball held where it rises
     // keeps it down; the step after the ball has gone, it is up.
    Game game;
    run(game, 2);
    CHECK(stepUntil(game, GameEvent::Type::DroneDive, kPatrolSteps) >= 0);
    const Vec2 risesAt{table::kDroneLeftX, table::kDroneY};
    for (int i = 0; i < kDiveSteps + kSecond; ++i) {
      Seam::placeBall(game, 0, risesAt, {0.0F, 0.0F});
      game.step(idle);
      CHECK(!game.drone().up && countEvents(game, GameEvent::Type::DroneRise) == 0);
    }
    Seam::placeBall(game, 0, kParked, {0.0F, 0.0F});
    game.step(idle);
    CHECK(game.drone().up && countEvents(game, GameEvent::Type::DroneRise) == 1);
  }

  {  // A hit from below: DroneHit, the first armour lamp; drawn thrown back up
     // the table, away from the ball, kDroneRecoil at the most, and back on
     // its line within kDroneRecoilSeconds. Only the drawing moves: the body
     // keeps to its patrol.
    Game game;
    run(game, 2);
    const std::uint64_t scoreBefore = game.rules().score();
    CHECK(hitDrone(game) == 1);
    CHECK(game.droneHitCount() == 1 && game.rules().droneHits() == 1);
    CHECK(game.rules().score() > scoreBefore);
    float most = 0.0F;
    float lastRecoil = game.drone().recoil;
    bool rising = true;
    const int recoilSteps = static_cast<int>(table::kDroneRecoilSeconds * kSecond + 0.5);
    for (int i = 0; i < recoilSteps + 2; ++i) {
      const pinball::DroneView drone = game.drone();
      // Up the table, away from the ball below it (straight up for a ball
      // straight below).
      const float back = table::kDroneY - drone.position.y;
      CHECK(back >= 0.0F && back <= table::kDroneRecoil * drone.recoil + 0.001F);
      CHECK(back >= 0.8F * table::kDroneRecoil * drone.recoil);
      most = std::max(most, back);
      if (drone.recoil < lastRecoil) {
        rising = false;
      }
      // Out, then back: never out again once it has started back.
      CHECK(rising || drone.recoil <= lastRecoil);
      lastRecoil = drone.recoil;
      Seam::placeBall(game, 0, kParked, {0.0F, 0.0F});
      game.step(idle);
    }
    CHECK(most > 0.8F * table::kDroneRecoil);
    CHECK(game.drone().recoil == 0.0F && game.drone().position.y == table::kDroneY);
  }

  {  // Five hits: the armour lamps one to four, then DRONE DOWN with the
     // fifth (DroneHit index 5 and DroneDown). Knocked down it is not solid,
     // is drawn sinking out of sight, its down time runs from 1 to 0, and it
     // comes up (DroneRise, index 1) when the time is out, not at the end of
     // the patrol after it.
    Game game;
    run(game, 2);
    std::vector<int> stages;
    int downAt = -1;
    const int armour = game.rules().droneArmour();
    CHECK(armour == pinball::tuning::kDroneArmourBase);  // FL100.
    for (int hit = 0; hit < armour; ++hit) {
      stages.push_back(hitDrone(game));
      if (findEvent(game, GameEvent::Type::DroneDown) != nullptr) {
        downAt = static_cast<int>(game.stepCount());
      }
      // Away, and the recoil over, before the next.
      for (int i = 0; i < 30 && hit < armour - 1; ++i) {
        Seam::placeBall(game, 0, kParked, {0.0F, 0.0F});
        game.step(idle);
      }
    }
    CHECK((stages == std::vector<int>{1, 2, table::kDroneArmour}));
    CHECK(downAt >= 0 && game.rules().droneHits() == 0);
    pinball::DroneView drone = game.drone();
    CHECK(drone.knocked && !drone.up && drone.downLeft > 0.99F);
    // It does not come back for twenty seconds, whatever its patrol clock says.
    float lastLeft = drone.downLeft;
    const GameEvent* rise = nullptr;
    const int rose = stepUntil(game, GameEvent::Type::DroneRise,
                               static_cast<int>(table::kDroneDownSeconds * kSecond) + kSecond, &rise);
    CHECK(rose >= 0 && rise != nullptr && rise->index == 1);
    const int downSteps = static_cast<int>(game.stepCount()) - downAt;
    CHECK(downSteps >= static_cast<int>(table::kDroneDownSeconds * kSecond) &&
          downSteps <= static_cast<int>(table::kDroneDownSeconds * kSecond) + 2);
    drone = game.drone();
    CHECK(drone.up && !drone.knocked && drone.downLeft == 0.0F && drone.sink > 0.8F);
    (void)lastLeft;
    // And it can be hit again.
    run(game, kSecond / 4);
    CHECK(hitDrone(game) == 1);
  }

  {  // While it is down its time counts down, and it is drawn out of sight.
    Game game;
    run(game, 2);
    const int armour = game.rules().droneArmour();
    for (int hit = 0; hit < armour; ++hit) {
      CHECK(hitDrone(game) == (hit + 1 < armour ? hit + 1 : table::kDroneArmour));
      for (int i = 0; i < 30 && hit < armour - 1; ++i) {
        Seam::placeBall(game, 0, kParked, {0.0F, 0.0F});
        game.step(idle);
      }
    }
    float lastLeft = 2.0F;
    for (int i = 0; i < 10 * kSecond; ++i) {
      Seam::placeBall(game, 0, kParked, {0.0F, 0.0F});
      game.step(idle);
      const pinball::DroneView drone = game.drone();
      CHECK(drone.knocked && !drone.up && drone.downLeft < lastLeft);
      if (i > kSecond) {
        CHECK(drone.sink == 1.0F);
      }
      lastLeft = drone.downLeft;
    }
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
      if (table::kRollovers[i].group != table::RolloverGroup::TopLane &&
          table::kRollovers[i].group != table::RolloverGroup::Flaps) {
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


// --- How a hole with a cup is seen to take a ball and give it back. ----------
//
// Whatever the table: a hole with a cup takes the ball only once its centre
// is over the cup; it is drawn pulled onto the cup's centre while it shrinks
// and darkens (the pull-in, table::kHoleSinkSeconds, 80 to 150 ms), is out
// of sight while it is held, is drawn coming up at the exit growing and
// brightening (the rise, table::kHoleRiseSeconds), and the physics has it
// again exactly where it was drawn last. All in fixed steps.

// How far a point is from the segment a to b.
float distanceToSegment(Vec2 p, Vec2 a, Vec2 b) {
  const Vec2 ab = b - a;
  const float lengthSquared = ab.x * ab.x + ab.y * ab.y;
  float t = 0.0F;
  if (lengthSquared > 0.0F) {
    t = std::clamp(((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / lengthSquared, 0.0F, 1.0F);
  }
  return distance(p, a + ab * t);
}

const int kSinkSteps = static_cast<int>(Game::kHoleSinkSteps);
const int kRiseSteps = static_cast<int>(Game::kHoleRiseSteps);

// What a ball looked like after one step: enough to compare two games.
struct Frame {
  bool visible = false;
  bool locked = false;
  Vec2 drawnAt{};
  float sink = 0.0F;
  bool operator==(const Frame& o) const {
    return visible == o.visible && locked == o.locked && drawnAt.x == o.drawnAt.x &&
           drawnAt.y == o.drawnAt.y && sink == o.sink;
  }
};
Frame frameOf(const Game& game, std::size_t slot) {
  const BallView view = viewOf(game, slot);
  return {view.visible, view.locked, view.drawnAt(), view.sink};
}

// What following a ball through a hole with a cup found.
struct CupRun {
  bool released = false;
  int pullFrames = 0;    // Drawn on its way in, sinking.
  int hiddenFrames = 0;  // Out of sight.
  int riseFrames = 0;    // Drawn coming up, before it is let go.
  std::vector<Frame> frames;
};

// Follows a ball that a hole with a cup has just taken (call it right after
// the step that took it) to the step it is let go, checking every frame.
CupRun followCup(Game& game, std::size_t slot, const table::Hole& hole) {
  const InputState idle;
  CupRun run;
  const Vec2 takenAt = Seam::position(game, slot);
  // Taken with its centre over the cup.
  CHECK(distance(takenAt, hole.cup) <= hole.cupRadius + 0.01F);
  enum class Phase { Pull, Hidden, Rise } phase = Phase::Pull;
  float lastAlong = -1.0F;
  float lastSink = -1.0F;
  Frame last{};
  for (int i = 0; i < 4 * kSecond; ++i) {
    const BallView view = viewOf(game, slot);
    const Frame frame = frameOf(game, slot);
    run.frames.push_back(frame);
    if (phase == Phase::Pull && !view.visible) {
      phase = Phase::Hidden;
    }
    if (phase == Phase::Hidden && view.visible) {
      phase = Phase::Rise;
      lastSink = 2.0F;
    }
    switch (phase) {
      case Phase::Pull: {
        // On the line from where it was taken to the cup's centre, never
        // back, sinking further each step; the game's own place for the ball
        // is untouched.
        CHECK(view.visible);
        CHECK(distanceToSegment(view.drawnAt(), takenAt, hole.cup) < 0.05F);
        const float along = distance(view.drawnAt(), takenAt);
        CHECK(along >= lastAlong - 0.001F);
        lastAlong = along;
        CHECK(view.sink > lastSink && view.sink > 0.0F && view.sink < 1.0F);
        lastSink = view.sink;
        CHECK(near(view.position, Seam::position(game, slot)));
        ++run.pullFrames;
        break;
      }
      case Phase::Hidden:
        CHECK(!view.visible);
        ++run.hiddenFrames;
        break;
      case Phase::Rise:
        // At the exit, rising (sink falling) each step, not moved off it.
        CHECK(view.visible && near(view.position, table::holeRisePoint(hole)));
        CHECK(view.drawOffset.x == 0.0F && view.drawOffset.y == 0.0F);
        CHECK(view.sink < lastSink && view.sink >= 0.0F);
        lastSink = view.sink;
        ++run.riseFrames;
        break;
    }
    last = frame;
    game.step(idle);
    if (!Seam::held(game, slot)) {
      // Let go exactly where it was last drawn, at full size, in view.
      const BallView out = viewOf(game, slot);
      CHECK(phase == Phase::Rise);
      CHECK(out.visible && out.sink == 0.0F && !out.locked);
      CHECK(out.drawOffset.x == 0.0F && out.drawOffset.y == 0.0F);
      CHECK(distance(Seam::position(game, slot), table::holeRisePoint(hole)) < 1.0e-3F);
      CHECK(distance(out.drawnAt(), last.drawnAt) < 1.0e-3F);
      run.frames.push_back(frameOf(game, slot));
      run.released = true;
      return run;
    }
  }
  CHECK(false);
  return run;
}

// Puts a ball still in the pocket of a hole with a cup, a little off the
// cup, and steps until the hole takes it. Returns whether it did.
bool dropIntoCup(Game& game, std::size_t slot, const table::Hole& hole, Vec2 off) {
  const InputState idle;
  putBall(game, slot, hole.cup + off, {0.0F, 0.0F});
  for (int i = 0; i < kSecond; ++i) {
    game.step(idle);
    if (Seam::held(game, slot)) {
      return true;
    }
  }
  return false;
}

// Every hole open and passing balls through: no lock, the doors down.
void openTable(Game& game) {
  Seam::rules(game).setMultiballEnabled(false);
  run(game, 2);
  Seam::dropDoorBank(game);
  run(game, 2);
}

void checkCupTimes(const CupRun& run) {
  CHECK(run.released);
  // The pull-in: 80 to 150 ms, every step of it drawn.
  CHECK(run.pullFrames == kSinkSteps - 1);
  CHECK(static_cast<double>(run.pullFrames + 1) / kSecond >= 0.08 &&
        static_cast<double>(run.pullFrames + 1) / kSecond <= 0.15);
  // The rise, every step of it drawn, ending as the physics takes over.
  CHECK(run.riseFrames == kRiseSteps - 1);
  CHECK(run.hiddenFrames > 0);
}

void testHoleViews() {
  const InputState idle;
  static_assert(table::kHoleSinkSeconds >= 0.08 && table::kHoleSinkSeconds <= 0.15);
  static_assert(table::kHoleRiseSeconds >= 0.08 && table::kHoleRiseSeconds <= 0.15);

  {  // The look of a sinking ball (core/HoleLook.hpp). Full motion: smaller,
     // darker and fainter the further down, gone at the bottom. Reduce
     // Motion: never a change of size, and a shorter change: nothing in the
     // first quarter of the sink, all done by the last quarter.
    float lastScale = 2.0F;
    for (int i = 0; i <= 20; ++i) {
      const float sink = static_cast<float>(i) / 20.0F;
      const pinball::HoleLook full = pinball::holeLook(sink, false);
      const pinball::HoleLook calm = pinball::holeLook(sink, true);
      CHECK(full.scale < lastScale);
      lastScale = full.scale;
      CHECK(calm.scale == 1.0F);
      if (sink <= 0.25F) {
        CHECK(calm.shade == 1.0F && calm.alpha == 1.0F);
      }
      if (sink >= 0.75F) {
        CHECK(calm.alpha == 0.0F);
      }
    }
    CHECK(pinball::holeLook(0.0F, false).scale == 1.0F && pinball::holeLook(0.0F, false).alpha == 1.0F);
    CHECK(pinball::holeLook(1.0F, false).alpha == 0.0F && pinball::holeLook(1.0F, false).scale < 0.6F);
    // Out of range: no further than all the way down or all the way up.
    CHECK(pinball::holeLook(5.0F, false).alpha == 0.0F && pinball::holeLook(-1.0F, true).alpha == 1.0F);
  }

  // Every hole with a cup, whatever table this is: a ball dropped near the
  // cup is pulled in, held out of sight and comes up at the exit.
  int cups = 0;
  for (std::size_t h = 0; h < table::kHoles.size(); ++h) {
    const table::Hole& hole = table::kHoles[h];
    // (The CAT SHOT's ball rides in view into the Hangar: its own test.)
    if (!table::hasCup(hole) || hole.type == table::HoleType::CatShot) {
      continue;
    }
    ++cups;
    Game game;
    openTable(game);
    CHECK(dropIntoCup(game, 0, hole, {0.0F, -0.5F * hole.cupRadius}));
    CHECK(Seam::held(game, 0) && !game.ballLocked(0));
    const CupRun run = followCup(game, 0, hole);
    checkCupTimes(run);
    // Its whole hold, as before: pulled in and risen within it.
    CHECK(static_cast<double>(run.frames.size() - 1) / kSecond >= Game::holdSeconds(hole.type) -
                                                                       1.0 / kSecond);
  }
  CHECK(cups >= 2);

  {  // The Briefing room: a ball shot gently into its pocket rolls on, in
     // view and under the physics, past the line where it used to vanish,
     // and is taken only at the cup.
    Game game;
    openTable(game);
    // (Up and to the right: the Mission scoop takes only a ball that comes
    // in that way, table::kScoopMinRightward.)
    putBall(game, 0, {kScoop.cup.x - 6.0F, 450.0F}, {100.0F, -420.0F});
    int freeInPocket = 0;
    for (int i = 0; i < kSecond && !Seam::held(game, 0); ++i) {
      game.step(idle);
      const Vec2 p = Seam::position(game, 0);
      const bool inPocket = std::fabs(p.x - kScoop.center.x) < kScoop.halfSize.x &&
                            std::fabs(p.y - kScoop.center.y) < kScoop.halfSize.y;
      if (inPocket && !Seam::held(game, 0)) {
        ++freeInPocket;
        CHECK(viewOf(game, 0).visible && viewOf(game, 0).sink == 0.0F);
        CHECK(distance(p, kScoop.cup) > kScoop.cupRadius);
      }
    }
    CHECK(Seam::held(game, 0));
    CHECK(freeInPocket >= 2);
    checkCupTimes(followCup(game, 0, kScoop));
  }

  {  // A ball that comes to rest in the pocket away from the cup does not
     // sit there: the pocket's funnel rolls it to the cup.
    Game game;
    openTable(game);
    CHECK(dropIntoCup(game, 0, kScoop, {12.0F, -25.0F}));
    Game other;
    openTable(other);
    CHECK(dropIntoCup(other, 0, kHangar, {-20.0F, -15.0F}));
  }

  {  // A ball passing straight over the cup at speed is taken (the path of
     // its step, not where it stops, is what counts).
    Game game;
    openTable(game);
    // (Up and to the right, as the Mission scoop takes it: the line passes
    // over the cup.)
    putBall(game, 0, {kScoop.cup.x - 20.0F, 470.0F}, {600.0F, -2400.0F});
    bool taken = false;
    for (int i = 0; i < kSecond / 4 && !taken; ++i) {
      game.step(idle);
      taken = Seam::held(game, 0);
    }
    CHECK(taken && distance(Seam::position(game, 0), kScoop.cup) <= kScoop.cupRadius + 0.01F);
  }

  // A subway's exit (a hole that lets its ball up away from its cup) is
  // drawn open only around the rise: shut while the ball goes in and is
  // down, fully open for every frame it comes up, closing after it leaves.
  // A hole that kicks the ball back out of its own cup has none.
  for (std::size_t h = 0; h < table::kHoles.size(); ++h) {
    const table::Hole& hole = table::kHoles[h];
    if (!table::hasCup(hole) || hole.type == table::HoleType::CatShot) {
      CHECK(Game().holeExitOpening(h) == 0.0F);
      continue;
    }
    const Vec2 rise = table::holeRisePoint(hole);
    const bool subway = rise.x != hole.cup.x || rise.y != hole.cup.y;
    Game game;
    openTable(game);
    CHECK(game.holeExitOpening(h) == 0.0F);
    CHECK(dropIntoCup(game, 0, hole, {0.0F, -2.0F}));
    int shutSteps = 0;
    int risingOpen = 0;
    int rising = 0;
    for (int i = 0; i < 4 * kSecond && Seam::held(game, 0); ++i) {
      const BallView view = viewOf(game, 0);
      const float open = game.holeExitOpening(h);
      CHECK(open >= 0.0F && open <= 1.0F);
      const bool risingNow = view.visible && near(view.position, rise) && view.sink < 1.0F &&
                             i > kSinkSteps;
      if (risingNow) {
        ++rising;
        risingOpen += open == 1.0F ? 1 : 0;
      } else if (open == 0.0F) {
        ++shutSteps;
      }
      CHECK(subway || open == 0.0F);
      game.step(idle);
    }
    CHECK(!Seam::held(game, 0));
    if (!subway) {
      continue;
    }
    CHECK(rising == kRiseSteps - 1 && risingOpen == rising);
    // Shut from the pull-in to a moment before the rise.
    CHECK(shutSteps >= static_cast<int>(Game::holdSeconds(hole.type) * kSecond) - 3 * kRiseSteps);
    // Closing behind it, the same after a save part-way, then shut.
    float last = game.holeExitOpening(h);
    CHECK(last > 0.0F);
    for (int i = 0; i < kRiseSteps + 2; ++i) {
      if (i == kRiseSteps / 2) {
        Game restored;
        CHECK(restored.restore(game.snapshot()));
        CHECK(restored.holeExitOpening(h) == game.holeExitOpening(h));
      }
      game.step(idle);
      const float now = game.holeExitOpening(h);
      CHECK(now <= last);
      last = now;
    }
    CHECK(last == 0.0F);
  }

  {  // Played twice, the same frames; and a game saved part-way through any
     // phase and put back plays on with the same frames to the step.
    Game game;
    openTable(game);
    CHECK(dropIntoCup(game, 0, kScoop, {0.0F, -4.0F}));
    Game twin;
    openTable(twin);
    CHECK(dropIntoCup(twin, 0, kScoop, {0.0F, -4.0F}));
    const CupRun run = followCup(game, 0, kScoop);
    const CupRun again = followCup(twin, 0, kScoop);
    CHECK(run.frames == again.frames);

    const int hold = static_cast<int>(run.frames.size()) - 1;
    for (const int at : {kSinkSteps / 2, kSinkSteps + 10, hold - kRiseSteps / 2}) {
      Game original;
      openTable(original);
      CHECK(dropIntoCup(original, 0, kScoop, {0.0F, -4.0F}));
      for (int i = 0; i < at; ++i) {
        original.step(idle);
      }
      Game restored;
      CHECK(restored.restore(original.snapshot()));
      for (int i = at; i <= hold + 5; ++i) {
        CHECK(frameOf(original, 0) == frameOf(restored, 0));
        CHECK(frameOf(original, 0) == run.frames[static_cast<std::size_t>(std::min(i, hold))] ||
              i >= hold);
        CHECK(Seam::held(original, 0) == Seam::held(restored, 0));
        original.step(idle);
        restored.step(idle);
      }
    }
  }

  {  // A lock: taken at the cup, drawn rolling into its bay and in view
     // there; let go, drawn rolling back to the cup and dropping in, then
     // coming up at the exit like any other.
    Game game;
    run(game, 2);
    markDoors(game);
    run(game, 2);
    CHECK(shootHangar(game, 0));
    CHECK(game.ballLocked(0));
    const Vec2 takenAt = Seam::position(game, 0);
    CHECK(distance(takenAt, kHangar.cup) <= kHangar.cupRadius + 0.01F);
    const Vec2 bay = table::kLockBays[0];
    float lastAlong = -1.0F;
    for (int i = 0; i < kSecond / 2; ++i) {
      const BallView view = viewOf(game, 0);
      CHECK(view.visible && view.locked && view.sink == 0.0F);
      CHECK(distanceToSegment(view.drawnAt(), takenAt, bay) < 0.05F);
      const float along = distance(view.drawnAt(), takenAt);
      CHECK(along >= lastAlong - 0.001F);
      lastAlong = along;
      game.step(idle);
    }
    CHECK(near(viewOf(game, 0).drawnAt(), bay));
    // Saved in the bay and put back: still there, still locked.
    {
      Game restored;
      CHECK(restored.restore(game.snapshot()));
      CHECK(frameOf(restored, 0) == frameOf(game, 0));
    }
    Seam::releaseLocks(game);
    float lastSink = -1.0F;
    lastAlong = -1.0F;
    bool rising = false;
    int rollFrames = 0;
    int riseFrames = 0;
    Frame last{};
    // Saved part-way back to the cup, the game put back goes on alike.
    std::unique_ptr<Game> restored;
    for (int i = 0; i < kSecond && Seam::held(game, 0); ++i) {
      if (i == 9) {
        restored = std::make_unique<Game>();
        CHECK(restored->restore(game.snapshot()));
      }
      game.step(idle);
      if (restored) {
        restored->step(idle);
        CHECK(frameOf(*restored, 0) == frameOf(game, 0));
        CHECK(Seam::held(*restored, 0) == Seam::held(game, 0));
      }
      if (!Seam::held(game, 0)) {
        break;
      }
      const BallView view = viewOf(game, 0);
      CHECK(view.visible);  // Never out of sight on the way out.
      if (!rising && near(view.position, kHangar.exitPosition)) {
        rising = true;
        lastSink = 2.0F;
      }
      if (!rising) {
        CHECK(view.locked);
        CHECK(distanceToSegment(view.drawnAt(), bay, kHangar.cup) < 0.05F);
        const float along = distance(view.drawnAt(), bay);
        CHECK(along >= lastAlong - 0.001F);
        lastAlong = along;
        CHECK(view.sink >= lastSink);
        lastSink = view.sink;
        ++rollFrames;
      } else {
        CHECK(!view.locked && view.sink < lastSink);
        lastSink = view.sink;
        ++riseFrames;
      }
      last = frameOf(game, 0);
    }
    CHECK(!Seam::held(game, 0));
    CHECK(rollFrames == static_cast<int>(Game::kLockRollSteps));
    CHECK(riseFrames == kRiseSteps - 1);
    CHECK(near(last.drawnAt, kHangar.exitPosition));
    CHECK(distance(Seam::position(game, 0), kHangar.exitPosition) < 1.0e-3F);
    CHECK(distance(viewOf(game, 0).drawnAt(), last.drawnAt) < 1.0e-3F);
  }
}
}  // namespace

// --- The captive ball (TABLE_REV3 3.5) ------------------------------------------------

// A small generator of the tests' own, so that every run is the same.
struct Shuffle {
  std::uint64_t state;
  float unit() {
    state ^= state << 13U;
    state ^= state >> 7U;
    state ^= state << 17U;
    return static_cast<float>(state >> 40U) / 16777216.0F;
  }
};

float distanceToCapsule(Vec2 p, const table::Capsule& c) {
  const Vec2 ab = c.b - c.a;
  const float t =
      std::clamp(((p.x - c.a.x) * ab.x + (p.y - c.a.y) * ab.y) / (ab.x * ab.x + ab.y * ab.y),
                 0.0F, 1.0F);
  return distance(p, c.a + ab * t);
}

// Inside the channel, past its mouth: where no playfield ball's centre can be.
bool inChannel(Vec2 p) {
  const float along = table::captiveTranslationAt(p);
  return along > 4.0F && along < 40.0F && std::fabs(table::captiveOffsetAt(p)) < 16.5F;
}

// A ball shot at the captive ball from below the mouth, up and a little
// left (a weak Runway shot's line): the switch, the events and their order
// in the first stroke (the ball can come back and strike it again later).
struct CaptiveShot {
  int strikes = 0;
  int switches = 0;
  float switchSpeed = 0.0F;
  std::size_t switchBall = 99;
  float furthest = 0.0F;
};
CaptiveShot shootCaptive(float speed, std::size_t slot = 0) {
  PhysicsWorld world;
  const Vec2 direction{-0.3F, -0.954F};
  const Vec2 from = table::kCaptiveHome - direction * 70.0F;
  if (slot == 0) {
    world.resetBall(0, from, direction * speed);
  } else {
    world.deactivateBall(0);
    CHECK(world.activateBall(slot, from, direction * speed));
  }
  CaptiveShot shot;
  bool moved = false;
  bool strokeOver = false;
  for (int i = 0; i < 2 * kSecond; ++i) {
    world.step(kStep);
    const float along = world.captiveTranslation();
    strokeOver = strokeOver || (moved && along < 0.5F);
    moved = moved || along > 1.0F;
    if (strokeOver) {
      continue;
    }
    shot.furthest = std::max(shot.furthest, along);
    for (const PhysicsEvent& e : world.events()) {
      if (e.element.kind != table::ElementKind::CaptiveBall) {
        continue;
      }
      if (e.element.index == table::kCaptiveBallStruck) {
        CHECK(e.ball == slot && e.type == PhysicsEvent::Type::Hit);
        ++shot.strikes;
      } else {
        // The switch is never reported before the ball that drove it there.
        CHECK(shot.strikes > 0);
        ++shot.switches;
        shot.switchSpeed = e.speed;
        shot.switchBall = e.ball;
      }
    }
  }
  CHECK(world.captiveResets() == 0);
  // Back at rest at the mouth, switch open, by the end.
  CHECK(std::fabs(world.captiveTranslation()) < 0.05F && !world.captiveSwitchClosed());
  return shot;
}

void testCaptiveBall() {
  {  // At rest: it stays at its mouth, on its line, in no contact, for a minute.
    PhysicsWorld world;
    world.deactivateBall(0);
    CHECK(near(world.captivePosition(), table::kCaptiveHome));
    float drift = 0.0F;
    int events = 0;
    for (int i = 0; i < 60 * kSecond; ++i) {
      world.step(kStep);
      drift = std::max({drift, std::fabs(world.captiveTranslation()),
                        std::fabs(table::captiveOffsetAt(world.captivePosition()))});
      events += static_cast<int>(world.events().size());
    }
    CHECK(drift < 0.05F);
    CHECK(events == 0);
    CHECK(!world.captiveSwitchClosed() && world.captiveResets() == 0);
  }

  {  // The switch takes a firm hit: soft ones lift the ball and let it fall
     // back; from somewhere above 400 px/s on this line every one closes the
     // switch, once, for the ball that struck, with the stroke's speed.
    float softest = 1.0e9F;
    float hardestMiss = 0.0F;
    for (float speed = 100.0F; speed <= 3000.0F; speed += 50.0F) {
      const CaptiveShot shot = shootCaptive(speed);
      CHECK(shot.switches <= 1);
      if (shot.switches == 1) {
        softest = std::min(softest, speed);
        CHECK(shot.switchBall == 0);
        CHECK(shot.switchSpeed > 200.0F && shot.switchSpeed <= speed * 1.01F);
        CHECK(shot.furthest >= table::kCaptiveSwitchAt);
      } else {
        hardestMiss = std::max(hardestMiss, speed);
        CHECK(shot.furthest < table::kCaptiveSwitchAt);
      }
    }
    std::fprintf(stderr, "captive ball: switch closed from %.0f px/s, missed up to %.0f px/s\n",
                 static_cast<double>(softest), static_cast<double>(hardestMiss));
    CHECK(hardestMiss < softest);
    CHECK(softest >= 400.0F && softest <= 800.0F);
    // Any ball slot drives it and is named by its switch.
    const CaptiveShot second = shootCaptive(1200.0F, 2);
    CHECK(second.switches == 1 && second.switchBall == 2);
  }

  {  // At the speed cap, one to three balls at a time from every side of the
     // mouth: the joint holds the ball to its line and its stop (no
     // explosion), no ball goes through it or into its channel or through
     // the channel's walls (no tunnelling), and the system of balls,
     // captive ball and its spring never gains energy.
    constexpr float kCap = PhysicsWorld::kMaxBallSpeed * PhysicsWorld::kPixelsPerMeter;
    // Masses go with the disc's area; gravity 950 px/s^2; the spring's
    // stiffness from its frequency (test code: the library's pi is fine).
    constexpr double kBallMass = 144.0;
    constexpr double kCaptiveMass = 121.0;
    constexpr double kGravity = 950.0;
    const double omega = 2.0 * 3.14159265358979 * static_cast<double>(table::kCaptiveSpringHertz);
    const double stiffness = kCaptiveMass * omega * omega;
    Shuffle rng{0x2545F4914F6CDD1DULL};
    double worstGain = -1.0;
    float worstAside = 0.0F;
    float lowest = 0.0F;
    float highest = 0.0F;
    float nearestWall = 1.0e9F;
    float deepest = 0.0F;
    int inside = 0;
    int strikes = 0;
    int switches = 0;
    for (int n = 0; n < 1500; ++n) {
      PhysicsWorld world;
      const std::size_t balls = 1U + static_cast<std::size_t>(n % 3);
      for (std::size_t slot = 0; slot < balls; ++slot) {
        Vec2 from{170.0F + rng.unit() * 90.0F, 320.0F + rng.unit() * 60.0F};
        from.x += static_cast<float>(slot) * 28.0F;
        const Vec2 at{table::kCaptiveHome.x - 25.0F + rng.unit() * 50.0F,
                      table::kCaptiveHome.y - 25.0F + rng.unit() * 50.0F};
        const Vec2 d = at - from;
        const Vec2 v = d * (kCap / std::sqrt(d.x * d.x + d.y * d.y));
        if (slot == 0) {
          world.resetBall(from, v);
        } else {
          CHECK(world.activateBall(slot, from, v));
        }
      }
      const auto energy = [&world, balls, stiffness] {
        double e = 0.0;
        for (std::size_t slot = 0; slot < balls; ++slot) {
          if (!world.ballHeld(slot)) {
            const Vec2 v = world.ballVelocity(slot);
            e += 0.5 * kBallMass * static_cast<double>(v.x * v.x + v.y * v.y) -
                 kBallMass * kGravity * static_cast<double>(world.ballPosition(slot).y);
          }
        }
        const Vec2 v = world.captiveVelocity();
        const double x = std::max(0.0, static_cast<double>(world.captiveTranslation()));
        return e + 0.5 * kCaptiveMass * static_cast<double>(v.x * v.x + v.y * v.y) -
               kCaptiveMass * kGravity * static_cast<double>(world.captivePosition().y) +
               0.5 * stiffness * x * x;
      };
      const double start = energy();
      const double scale = 0.5 * kBallMass * static_cast<double>(kCap * kCap) *
                           static_cast<double>(balls);
      for (int i = 0; i < 48; ++i) {
        world.step(kStep);
        for (const PhysicsEvent& e : world.events()) {
          if (e.element.kind == table::ElementKind::CaptiveBall) {
            (e.element.index == table::kCaptiveBallSwitch ? switches : strikes) += 1;
          }
        }
        worstGain = std::max(worstGain, (energy() - start) / scale);
        const Vec2 c = world.captivePosition();
        worstAside = std::max(worstAside, std::fabs(table::captiveOffsetAt(c)));
        lowest = std::min(lowest, world.captiveTranslation());
        highest = std::max(highest, world.captiveTranslation());
        for (std::size_t slot = 0; slot < balls; ++slot) {
          if (world.ballHeld(slot)) {
            continue;
          }
          const Vec2 p = world.ballPosition(slot);
          inside += inChannel(p) ? 1 : 0;
          nearestWall = std::min({nearestWall, distanceToCapsule(p, table::kCaptiveWallUpper),
                                  distanceToCapsule(p, table::kCaptiveWallLower)});
          deepest = std::max(deepest, table::kBallRadius + table::kCaptiveBallRadius - distance(p, c));
        }
      }
      CHECK(world.captiveResets() == 0);
    }
    std::fprintf(stderr,
                 "captive ball at the speed cap: %d strikes, %d switches; off its line %.2f px, "
                 "travel %.2f to %.2f; nearest a ball came to a wall's line %.1f (17 touching), "
                 "deepest into the captive ball %.2f px; %d ball steps in the channel; energy "
                 "gained at most %.5f of the shots'\n",
                 strikes, switches, static_cast<double>(worstAside), static_cast<double>(lowest),
                 static_cast<double>(highest), static_cast<double>(nearestWall),
                 static_cast<double>(deepest), inside, worstGain);
    CHECK(strikes > 600 && switches > 400);
    CHECK(worstAside < 1.0F);
    CHECK(lowest > -0.5F && highest < table::kCaptiveTravel + 0.5F);
    CHECK(nearestWall > 12.0F);
    CHECK(deepest < 6.0F);
    CHECK(inside == 0);
    CHECK(worstGain < 1.0e-3);
  }

  {  // D1/D6 on a bench: balls dropped anywhere around the channel, the
     // flippers idle. None comes to rest there (none still for three
     // seconds) and every one leaves (drained) within fifteen seconds.
    Shuffle rng{0x9E3779B97F4A7C15ULL};
    int placed = 0;
    int stuck = 0;
    int stayed = 0;
    std::vector<Vec2> where;
    for (int n = 0; n < 150; ++n) {
      PhysicsWorld world;
      const std::size_t balls = 1U + static_cast<std::size_t>(n % 3);
      for (std::size_t slot = 0; slot < balls; ++slot) {
        Vec2 p{};
        for (int tries = 0; tries < 200; ++tries) {
          p = {150.0F + rng.unit() * 95.0F, 225.0F + rng.unit() * 110.0F};
          const float along = table::captiveTranslationAt(p);
          bool free = distance(p, table::kCaptiveHome) > 24.0F &&
                      distanceToCapsule(p, table::kCaptiveWallUpper) > 18.0F &&
                      distanceToCapsule(p, table::kCaptiveWallLower) > 18.0F &&
                      distanceToCapsule(p, table::kNestTargetLeft) > 18.0F &&
                      distance(p, table::kBumpers[0].shape.center) > 37.0F &&
                      !(p.x > 230.0F && p.y > 300.0F) &&
                      !(along > -2.0F && along < 45.0F &&
                        std::fabs(table::captiveOffsetAt(p)) < 22.0F);
          for (std::size_t other = 0; other < slot; ++other) {
            free = free && distance(p, world.ballPosition(other)) > 25.0F;
          }
          if (free) {
            break;
          }
        }
        const Vec2 v{(rng.unit() - 0.5F) * 300.0F, (rng.unit() - 0.5F) * 300.0F};
        if (slot == 0) {
          world.resetBall(p, v);
        } else {
          CHECK(world.activateBall(slot, p, v));
        }
        ++placed;
      }
      std::array<int, table::kMaxBalls> still{};
      std::array<Vec2, table::kMaxBalls> last{};
      std::array<bool, table::kMaxBalls> gone{};
      for (int i = 0; i < 15 * kSecond; ++i) {
        world.step(kStep);
        for (std::size_t slot = 0; slot < balls; ++slot) {
          const Vec2 p = world.ballPosition(slot);
          gone[slot] = gone[slot] || world.ballHeld(slot) || p.y > table::kDrainY;
          if (gone[slot]) {
            continue;
          }
          still[slot] = distance(p, last[slot]) < 0.05F ? still[slot] + 1 : 0;
          last[slot] = p;
          if (still[slot] == 3 * kSecond) {
            ++stuck;
            where.push_back(p);
          }
        }
      }
      for (std::size_t slot = 0; slot < balls; ++slot) {
        stayed += gone[slot] ? 0 : 1;
      }
      CHECK(world.captiveResets() == 0);
    }
    for (const Vec2 p : where) {
      std::fprintf(stderr, "captive ball bench: a ball came to rest at (%.1f, %.1f)\n",
                   static_cast<double>(p.x), static_cast<double>(p.y));
    }
    CHECK(placed > 250);
    CHECK(stuck == 0);
    CHECK(stayed == 0);
  }

  {  // Deterministic: the same shots on two tables, the captive ball's every
     // place bit for bit the same.
    PhysicsWorld a;
    PhysicsWorld b;
    bool same = true;
    for (int i = 0; i < 6 * kSecond && same; ++i) {
      if (i % kSecond == 0) {
        const Vec2 from{205.0F + static_cast<float>(i % 7), 360.0F};
        const Vec2 v{-200.0F - static_cast<float>(i % 300), -1500.0F};
        a.resetBall(from, v);
        b.resetBall(from, v);
      }
      a.step(kStep);
      b.step(kStep);
      const Vec2 pa = a.captivePosition();
      const Vec2 pb = b.captivePosition();
      same = pa.x == pb.x && pa.y == pb.y && a.captiveSwitchClosed() == b.captiveSwitchClosed();
    }
    CHECK(same);
  }

  {  // In a game: the switch is the rules' G-force switch, scored once per
     // stroke; a strike that does not reach it scores nothing. A dead table
     // still counts it, as every other switch.
    Game game;
    run(game, 2);
    const Vec2 direction{-0.3F, -0.954F};
    const Vec2 from = table::kCaptiveHome - direction * 70.0F;
    putBall(game, 0, from, direction * 1500.0F);
    const std::uint64_t before = game.rules().score();
    bool closedSeen = false;
    for (int i = 0; i < kSecond / 2; ++i) {
      run(game, 1);
      closedSeen = closedSeen || game.captiveSwitchClosed();
    }
    CHECK(closedSeen);
    CHECK(game.captiveSwitchCount() == 1);
    CHECK(game.rules().score() - before >= 5'000);
    // Soft: no switch, no score from the captive ball.
    run(game, 2 * kSecond);
    putBall(game, 0, from, direction * 300.0F);
    const std::uint64_t soft = game.rules().score();
    run(game, kSecond / 2);
    CHECK(game.captiveSwitchCount() == 1);
    CHECK(game.rules().score() == soft);
  }
}

void testElements() {
  testCaptiveBall();
  testNothingTunnels();
  testDoorBank();
  testLock();
  testScoop();
  testHoleViews();
  testSensors();
  testSpinner();
  testThreeSpinners();
  testDrone();
  testKickback();
}
