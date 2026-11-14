#include "bots/TableHarness.hpp"

#include <box2d/math_functions.h>

#include <algorithm>
#include <cmath>
#include <span>

#include "GameTestAccess.hpp"
#include "core/Physics.hpp"

namespace pinball::bots {

namespace {

using Seam = GameTestAccess;

constexpr float kSpeedCap = PhysicsWorld::kMaxBallSpeed * PhysicsWorld::kPixelsPerMeter;
// A ball the kickback set down is lost again if it drains this soon.
constexpr std::uint64_t kKickLostSteps = 360;
// A drain is by an outlane if that outlane's switch was passed this recently.
constexpr std::uint64_t kOutlaneDrainSteps = 240;

}  // namespace

const char* touchName(Touch touch) noexcept {
  static constexpr std::array<const char*, kTouchCount> kNames{
      "none",          "left circuit", "Runway",        "left target",  "bay door",
      "Hangar",        "Briefing",     "Turbine",       "right target", "Skyway",
      "right circuit", "bumper",       "slingshot",     "roll call",    "left inlane",
      "right inlane",  "left outlane", "right outlane", "centre drain", "flipper"};
  const auto index = static_cast<std::size_t>(touch);
  return index < kNames.size() ? kNames[index] : "?";
}

TableHarness::TableHarness() {
  balls_[0].tracked = true;
  balls_[0].previous = table::kBallStart;
  balls_[0].anchor = table::kBallStart;
}

Vec2 TableHarness::position(std::size_t slot) const { return Seam::position(game_, slot); }
Vec2 TableHarness::velocity(std::size_t slot) const { return Seam::velocity(game_, slot); }
bool TableHarness::live(std::size_t slot) const { return Seam::live(game_, slot); }
bool TableHarness::held(std::size_t slot) const { return Seam::held(game_, slot); }

Sense TableHarness::sense() const {
  Sense sense;
  for (std::size_t slot = 0; slot < table::kMaxBalls; ++slot) {
    if (live(slot) && !held(slot)) {
      sense.balls[sense.count++] = {position(slot), velocity(slot)};
    }
  }
  return sense;
}

void TableHarness::put(std::size_t slot, Vec2 position, Vec2 velocity, bool inPlay) {
  if (slot >= balls_.size() || !live(slot)) {
    return;
  }
  Seam::placeBall(game_, slot, position, velocity);
  balls_[slot] = {};
  balls_[slot].tracked = true;
  balls_[slot].previous = position;
  balls_[slot].anchor = position;
  if (inPlay) {
    Seam::putInPlay(game_, slot);
  }
}

std::size_t TableHarness::add(Vec2 position, Vec2 velocity) {
  const std::size_t slot = Seam::addBall(game_, position, velocity);
  if (slot < balls_.size()) {
    balls_[slot] = {};
    balls_[slot].tracked = true;
    balls_[slot].previous = position;
    balls_[slot].anchor = position;
    Seam::putInPlay(game_, slot);
  }
  return slot;
}

bool TableHarness::nearFlipper(Vec2 p, std::size_t i, float slack) const {
  if (i >= table::kFlippers.size()) {
    return false;
  }
  const FlipperView flipper = game_.flipper(i);
  // Box2D's own cosine and sine: the same on every platform.
  const b2CosSin turn = b2ComputeCosSin(flipper.angle);
  const Vec2 tip{flipper.pivot.x + table::kFlipperLength * turn.cosine,
                 flipper.pivot.y + table::kFlipperLength * turn.sine};
  const SegmentReach reach = reachOf(p, flipper.pivot, tip);
  const float u = std::clamp(reach.along / table::kFlipperLength, 0.0F, 1.0F);
  const float batRadius =
      table::kFlipperBaseRadius + (table::kFlipperTipRadius - table::kFlipperBaseRadius) * u;
  return reach.distance < table::kBallRadius + batRadius + slack;
}

void TableHarness::lightKickback() { Seam::rules(game_).setKickbackLit(true); }
void TableHarness::setLocks(bool enabled) { Seam::rules(game_).setMultiballEnabled(enabled); }

void TableHarness::settleKick(std::size_t slot, bool safe) {
  if (slot < balls_.size() && balls_[slot].kickTouchPending) {
    balls_[slot].kickTouchPending = false;
    kickbackSafe += safe ? 1U : 0U;
  }
}

void TableHarness::log(Touch touch, float speed, std::size_t ball) {
  events.push_back({step_, touch, speed, static_cast<std::uint8_t>(ball)});
  ++count[static_cast<std::size_t>(touch)];
  // The first thing a ball set down by the kickback meets.
  if (touch != Touch::CircuitLeft && touch != Touch::CircuitRight && touch != Touch::Turbine) {
    settleKick(ball, touch == Touch::InlaneLeft);
  }
}

Touch TableHarness::advance() {
  // Where each free ball was, to tell afterwards which one left and how.
  std::array<bool, table::kMaxBalls> wasFree{};
  for (std::size_t slot = 0; slot < table::kMaxBalls; ++slot) {
    wasFree[slot] = live(slot) && !held(slot);
    if (wasFree[slot] && !balls_[slot].tracked) {
      // Served by the game since the last step.
      balls_[slot] = {};
      balls_[slot].tracked = true;
      balls_[slot].previous = position(slot);
      balls_[slot].anchor = balls_[slot].previous;
    }
  }
  const std::uint32_t drainsBefore = game_.drainCount();
  const bool doorsUpBefore = game_.doorBankUp();
  const std::size_t lockedBefore = game_.lockedBalls();
  const std::uint64_t locksBefore = locks;
  const std::uint64_t releasesBefore = lockReleases;
  if (kickbackOff) {
    Seam::rules(game_).setKickbackLit(false);
  }

  InputState input;
  input.leftFlipper = left;
  input.rightFlipper = right;
  input.plunger = plunger;
  // A finished game is started again at once: the bench never idles.
  input.newGame = game_.gameOver();
  game_.step(input);
  ++step_;

  for (const PhysicsEvent& event : game_.events()) {
    const std::size_t i = event.element.index;
    const std::size_t ball = event.ball;
    switch (event.element.kind) {
      using enum table::ElementKind;
      case Bumper:
        ++bumperHits[i];
        log(Touch::Bumper, event.speed, ball);
        break;
      case Slingshot:
        ++slingHits[i];
        log(Touch::Slingshot, event.speed, ball);
        break;
      case Target:
        ++targetHits[i];
        log(table::kTargets[i].id == table::TargetId::NestLeft ? Touch::TargetLeft
                                                               : Touch::TargetRight,
            event.speed, ball);
        break;
      case DropTarget:
        ++doorHits[i];
        log(Touch::Door, event.speed, ball);
        break;
      case Rollover: {
        ++rolloverHits[i];
        const table::Rollover& rollover = table::kRollovers[i];
        const bool leftSide = rollover.zone.center.x < table::kFieldCenterX;
        Touch touch = Touch::RollCall;
        if (rollover.group == table::RolloverGroup::Inlane) {
          touch = leftSide ? Touch::InlaneLeft : Touch::InlaneRight;
          // A circuit ends at the far inlane.
          Ball& state = balls_[ball];
          if (leftSide && state.circuitRight) {
            ++circuitDone[1];
            state.circuitRight = false;
          }
          if (!leftSide && state.circuitLeft) {
            ++circuitDone[0];
            state.circuitLeft = false;
          }
        } else if (rollover.group == table::RolloverGroup::Outlane) {
          touch = leftSide ? Touch::OutlaneLeft : Touch::OutlaneRight;
          balls_[ball].outlaneStep = step_ + 1;
          balls_[ball].outlane = touch;
        }
        log(touch, event.speed, ball);
        break;
      }
      case Hole: {
        ++holeHits[i];
        Touch touch = Touch::Briefing;
        switch (table::kHoles[i].type) {
          case table::HoleType::Scoop:
            break;
          case table::HoleType::Hangar:
            touch = Touch::Hangar;
            break;
          case table::HoleType::RampLeft:
            touch = Touch::Runway;
            break;
          case table::HoleType::RampRight:
            touch = Touch::Skyway;
            break;
        }
        if (table::kHoles[i].type == table::HoleType::Hangar) {
          ++hangarEntries;
          hangarEntriesDoorsUp += doorsUpBefore ? 1U : 0U;
        }
        log(touch, event.speed, ball);
        break;
      }
      case Sensor: {
        Ball& state = balls_[ball];
        switch (table::kSensors[i].id) {
          case table::SensorId::CircuitMouthLeft:
          case table::SensorId::CircuitMouthRight:
            // A circuit is entered going up.
            if (event.direction > 0) {
              const bool leftSide = table::kSensors[i].id == table::SensorId::CircuitMouthLeft;
              ++circuitEntered[leftSide ? 0 : 1];
              state.circuitLeft = leftSide;
              state.circuitRight = !leftSide;
              log(leftSide ? Touch::CircuitLeft : Touch::CircuitRight, event.speed, ball);
            }
            break;
          case table::SensorId::ArchTop:
            // The top of the arch, crossed the way the circuit runs.
            if (state.circuitLeft && event.direction > 0) {
              ++circuitTop[0];
            }
            if (state.circuitRight && event.direction < 0) {
              ++circuitTop[1];
            }
            break;
          case table::SensorId::Spinner:
            if (event.direction > 0) {
              log(Touch::Turbine, event.speed, ball);
            }
            break;
        }
        break;
      }
      case Kickback:
        ++kickbacks;
        break;
      case None:
        break;
    }
  }
  for (const GameEvent& event : game_.gameEvents()) {
    switch (event.type) {
      using enum GameEvent::Type;
      case DoorBankDropped:
        ++bankDrops;
        break;
      case DoorBankRaised:
        ++bankRaises;
        break;
      case BallLocked:
        ++locks;
        break;
      case LockReleased:
        ++lockReleases;
        break;
      case Spin:
        ++spins;
        break;
      case KickbackReturned:
        ++kickbackReturns;
        if (event.ball < balls_.size()) {
          balls_[event.ball].kickStep = step_ + 1;
          balls_[event.ball].kickTouchPending = true;
        }
        break;
      case EjectSave:
        ++ejectSaves;
        break;
      case HoleEject:
      case KickbackFired:
        break;
    }
  }
  // Every ball that left a bay was let go, or was taken out with the game over.
  const std::uint64_t expected = lockedBefore + (locks - locksBefore) - (lockReleases - releasesBefore);
  if (game_.lockedBalls() < expected) {
    locksEmptied += expected - game_.lockedBalls();
  }
  // A locked ball is in its bay and nowhere else.
  for (const BallView& view : game_.balls()) {
    if (!view.locked) {
      continue;
    }
    bool inBay = false;
    for (const Vec2 bay : table::kLockBays) {
      inBay = inBay || (view.position.x == bay.x && view.position.y == bay.y);
    }
    lockedAstray += inBay && view.visible ? 0U : 1U;
  }

  // Which balls left during the step. A drained ball was low on the table;
  // the game has taken it off or put a ball on the plunger in its place.
  Touch slotZeroDrain = Touch::None;
  std::uint32_t drained = game_.drainCount() - drainsBefore;
  for (std::size_t slot = 0; slot < table::kMaxBalls; ++slot) {
    if (!wasFree[slot]) {
      continue;
    }
    Ball& state = balls_[slot];
    const bool gone = !live(slot) || position(slot).y < state.previous.y - 60.0F;
    const bool wasLow = state.previous.y > table::kDrainY - 60.0F;
    if (!gone || !wasLow) {
      if (!live(slot)) {
        // A ball that was waiting on the plunger when a new game began is
        // not lost: the game put its one ball there again, in another slot.
        const Vec2 fromStart = state.previous - table::kBallStart;
        const bool onPlunger = fromStart.x * fromStart.x + fromStart.y * fromStart.y < 16.0F;
        lostBalls += onPlunger ? 0U : 1U;
        state = {};
      }
      continue;
    }
    if (drained == 0) {
      // Taken off the low end of the table without a drain being counted.
      ++lostBalls;
      state = {};
      continue;
    }
    --drained;
    if (state.kickStep != 0 && step_ + 1 - state.kickStep <= kKickLostSteps) {
      ++kickbackLostAgain;
    }
    const bool byOutlane =
        state.outlaneStep != 0 && step_ + 1 - state.outlaneStep < kOutlaneDrainSteps;
    const Touch way = byOutlane ? state.outlane : Touch::DrainCentre;
    ++drains[way == Touch::DrainCentre ? 0 : (way == Touch::OutlaneLeft ? 1 : 2)];
    if (slot == 0) {
      slotZeroDrain = way;
    }
    state = {};  // Whatever is in the slot now is another ball.
  }

  for (std::size_t slot = 0; slot < table::kMaxBalls; ++slot) {
    if (live(slot) && !held(slot) && balls_[slot].tracked) {
      watchBall(slot);
    } else if (live(slot) && held(slot)) {
      balls_[slot].still = 0;
    }
  }
  return slotZeroDrain;
}

void TableHarness::watchBall(std::size_t slot) {
  Ball& state = balls_[slot];
  const Vec2 p = position(slot);
  const Vec2 v = velocity(slot);
  const bool inPlay = game_.ballInPlay(slot);
  if (inPlay && p.x > table::kLaneWallX + table::kWallRadius) {
    ++laneIntrusions;
  }
  if (v.x * v.x + v.y * v.y > kSpeedCap * kSpeedCap * 1.002F) {
    ++overSpeed;
  }
  bool cradled = false;
  for (std::size_t f = 0; f < table::kFlippers.size(); ++f) {
    if (nearFlipper(p, f, 1.0F)) {
      settleKick(slot, true);
      ++flipperTouches;
      state.circuitLeft = false;
      state.circuitRight = false;
      state.flipperStep = step_ + 1;
    }
    const bool raised = f == table::kLeftFlipperIndex ? left : right;
    cradled = cradled || (raised && nearFlipper(p, f, 6.0F));
  }
  const Vec2 moved = p - state.anchor;
  if (moved.x * moved.x + moved.y * moved.y > kStuckRadius * kStuckRadius) {
    state.anchor = p;
    state.still = 0;
  } else if (inPlay && !cradled) {
    if (++state.still > kStuckSteps) {
      ++stuck;
      stuckAt.push_back(p);
      Seam::pushBall(game_, slot, {90.0F, -160.0F});
      state.still = 0;
    }
  }
  state.previous = p;
}

FirstTouch firstTouch(TableHarness& harness, int maxSteps) {
  for (int i = 0; i < maxSteps; ++i) {
    const std::size_t before = harness.events.size();
    const std::uint64_t touchesBefore = harness.flipperTouches;
    const Touch drain = harness.advance();
    for (const TouchEvent& event : std::span(harness.events).subspan(before)) {
      if (event.touch != Touch::CircuitLeft && event.touch != Touch::CircuitRight &&
          event.touch != Touch::Turbine) {
        return {event.touch, i};
      }
    }
    if (harness.flipperTouches > touchesBefore) {
      return {Touch::Flipper, i};
    }
    if (drain != Touch::None) {
      return {drain, i};
    }
  }
  return {Touch::None, maxSteps};
}

}  // namespace pinball::bots
