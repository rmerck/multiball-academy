#include "core/Game.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "core/TableLayout.hpp"

namespace pinball {

namespace {

// Guards the blend factor: outside [0, 1] is clamped, not-a-number is 1.
float blendFactor(float alpha) noexcept {
  return alpha >= 0.0F && alpha <= 1.0F ? alpha : (alpha < 0.0F ? 0.0F : 1.0F);
}

// Length of a vector. Exactly rounded operations only, so that every
// platform draws a riding ball at the same place.
float lengthOf(Vec2 v) noexcept { return std::sqrt(v.x * v.x + v.y * v.y); }

std::uint32_t stepsIn(double seconds) noexcept {
  return static_cast<std::uint32_t>(seconds / Game::kStepSeconds + 0.5);
}

}  // namespace

Game::Game() {
  serveBall();
  snapshotForDrawing();
}

std::size_t Game::primaryBall() const noexcept {
  // A ball that is not locked away, if there is one.
  for (std::size_t slot = 0; slot < kMaxBalls; ++slot) {
    if (ballLive(slot)) {
      return slot;
    }
  }
  for (std::size_t slot = 0; slot < kMaxBalls; ++slot) {
    if (physics_.ballActive(slot)) {
      return slot;
    }
  }
  return 0;
}

std::size_t Game::lockedBalls() const noexcept {
  std::size_t count = 0;
  for (std::size_t slot = 0; slot < kMaxBalls; ++slot) {
    count += ballLocked(slot) ? 1U : 0U;
  }
  return count;
}

std::size_t Game::otherLiveBalls(std::size_t slot) const noexcept {
  std::size_t count = 0;
  for (std::size_t other = 0; other < kMaxBalls; ++other) {
    count += other != slot && ballLive(other) ? 1U : 0U;
  }
  return count;
}

void Game::note(GameEvent::Type type, std::size_t ball, std::size_t index) noexcept {
  if (gameEventCount_ < gameEvents_.size()) {
    gameEvents_[gameEventCount_++] = {type, static_cast<std::uint8_t>(ball),
                                      static_cast<std::uint8_t>(index)};
  }
}

void Game::serveBall() {
  // One ball in play from here on, in the lowest slot that is not a locked
  // ball's (with no lock in the game, always the first): locked balls stay
  // in their bays, every other ball goes.
  std::size_t served = kMaxBalls;
  for (std::size_t slot = kMaxBalls; slot-- > 0;) {
    if (!ballLocked(slot)) {
      served = slot;
    }
  }
  for (std::size_t slot = 0; slot < kMaxBalls; ++slot) {
    if (slot != served && ballLive(slot)) {
      removeBall(slot);
    }
  }
  queuedBalls_ = 0;
  if (served >= kMaxBalls) {
    return;  // Cannot happen: there are more slots than bays.
  }
  if (physics_.ballActive(served)) {
    physics_.resetBall(served, table::kBallStart, {0.0F, 0.0F});
  } else {
    // Its state was cleared when it was taken off the table.
    physics_.activateBall(served, table::kBallStart, {0.0F, 0.0F});
  }
  BallState& ball = balls_[served];
  ball.inPlay = false;
  ball.autoLaunch = false;
  ball.launched = false;
  ball.ejectStep = 0;
  ball.placed = true;
}

std::size_t Game::serveForPlayer() {
  for (std::size_t slot = 0; slot < kMaxBalls; ++slot) {
    if (physics_.ballActive(slot) ||
        !physics_.activateBall(slot, table::kBallStart, {0.0F, 0.0F})) {
      continue;
    }
    BallState& ball = balls_[slot];
    ball = {};
    ball.searchAnchor = table::kBallStart;
    ball.previous = ballView(slot);
    ball.placed = true;
    return slot;
  }
  return kMaxBalls;
}

void Game::lockBall(std::size_t slot) {
  BallState& ball = balls_[slot];
  std::array<bool, table::kLockBays.size()> taken{};
  for (std::size_t other = 0; other < kMaxBalls; ++other) {
    if (ballLocked(other)) {
      taken[balls_[other].lockBay - 1U] = true;
    }
  }
  for (std::size_t bay = 0; bay < taken.size(); ++bay) {
    if (taken[bay]) {
      continue;
    }
    ball.lockBay = static_cast<std::uint8_t>(bay + 1);
    ball.releasing = false;
    ball.placed = true;  // It is drawn in the bay from now on.
    ++lockCount_;
    note(GameEvent::Type::BallLocked, slot, bay);
    // With nothing else to play, the player gets a new ball to launch.
    bool served = false;
    if (!rules_.gameOver() && otherLiveBalls(slot) == 0 && queuedBalls_ == 0) {
      served = serveForPlayer() < kMaxBalls;
    }
    rules_.onBallLocked(bay, served);
    return;
  }
  // Both bays full: the ball is returned like any other.
  rules_.onLockRefused();
}

void Game::releaseLocks() noexcept {
  for (std::size_t slot = 0; slot < kMaxBalls; ++slot) {
    if (ballLocked(slot)) {
      balls_[slot].releasing = true;
    }
  }
}

void Game::clearLocks() {
  for (std::size_t slot = 0; slot < kMaxBalls; ++slot) {
    if (ballLocked(slot)) {
      removeBall(slot);
    }
  }
}

void Game::snapshotForDrawing() {
  for (std::size_t slot = 0; slot < kMaxBalls; ++slot) {
    if (physics_.ballActive(slot)) {
      balls_[slot].previous = ballView(slot);
    }
  }
  for (std::size_t i = 0; i < previousFlipperAngles_.size(); ++i) {
    previousFlipperAngles_[i] = physics_.flipperAngle(i);
  }
}

void Game::startNewGame() {
  rules_.startGame();
  bumperFlash_.fill(0.0F);
  slingshotFlash_.fill(0.0F);
  for (BallState& ball : balls_) {
    ball.stillSteps = 0;
  }
  shake_ = {};
  // The bays are emptied and the spinner stops where it is.
  clearLocks();
  spinsOwed_ = 0;
  serveBall();
}

void Game::handleNudges(const InputState& input) {
  // The drawn displacement springs back quickly.
  shake_ = shake_ * 0.86F;
  if (std::fabs(shake_.x) < 0.05F && std::fabs(shake_.y) < 0.05F) {
    shake_ = {};
  }

  // A bump from one side sends the ball towards the other: bump from the
  // left and the ball jumps right. A bump from the bottom sends it up the
  // table.
  struct Nudge {
    bool pressed;
    Vec2 ballKick;
    Vec2 tableShift;
  };
  const Nudge nudges[] = {
      {input.nudgeLeft && !previousInput_.nudgeLeft, {kNudgeSideSpeed, -30.0F}, {5.0F, 0.0F}},
      {input.nudgeRight && !previousInput_.nudgeRight, {-kNudgeSideSpeed, -30.0F}, {-5.0F, 0.0F}},
      {input.nudgeUp && !previousInput_.nudgeUp, {0.0F, -kNudgeUpSpeed}, {0.0F, -5.0F}},
  };
  for (const Nudge& nudge : nudges) {
    if (!nudge.pressed) {
      continue;
    }
    // The table moves even when the shove tilts it; the ball is only helped
    // by a shove that does not.
    const bool wasTilted = rules_.tilted();
    const bool helps = rules_.onNudge();
    if (!wasTilted && !rules_.gameOver()) {
      shake_ = shake_ + nudge.tableShift;
      sounds_ |= soundBit(Sound::Nudge);
    }
    if (helps) {
      // Every ball on the table feels it. A ball in a hole does not.
      for (std::size_t slot = 0; slot < kMaxBalls; ++slot) {
        physics_.pushBall(slot, nudge.ballKick);
      }
    }
  }
}

Rules::DrainInfo Game::drainInfo(std::size_t slot) const noexcept {
  const std::uint64_t ejected = balls_[slot].ejectStep;
  return {.afterEject = ejected != 0 && static_cast<double>(stepCount_ + 1 - ejected) *
                                                kStepSeconds <= table::kEjectSaveSeconds};
}

void Game::handleDrain(std::size_t slot) {
  ++drainCount_;
  const Rules::DrainInfo info = drainInfo(slot);
  if (info.afterEject) {
    note(GameEvent::Type::EjectSave, slot);
  }
  // Whatever the outcome, a ball goes back on the plunger. After the last
  // ball it simply waits there: the controls are off until a new game.
  rules_.onDrain(info);
  serveBall();
}

void Game::removeBall(std::size_t slot) {
  physics_.deactivateBall(slot);
  // The slot's next ball starts with nothing of this one's.
  balls_[slot] = {};
}

void Game::loseBallWithOthersLive(std::size_t slot, BallLoss loss) {
  // Play goes on with the others and nothing is served. A drained ball is
  // counted and the rules are told; one lost outside the table is simply
  // gone.
  bool owedBack = false;
  if (loss == BallLoss::Drained) {
    ++drainCount_;
    const Rules::DrainInfo info = drainInfo(slot);
    if (info.afterEject) {
      note(GameEvent::Type::EjectSave, slot);
    }
    owedBack = rules_.onBallLost(info);
  }
  removeBall(slot);
  if (owedBack) {
    requestBall();
  }
}

void Game::handleLostBalls() {
  for (std::size_t slot = 0; slot < kMaxBalls; ++slot) {
    if (!physics_.ballActive(slot)) {
      continue;
    }
    const bool lost = !physics_.ballIsSane(slot);
    if (!lost && !(physics_.ballPosition(slot).y > table::kDrainY)) {
      continue;
    }
    // Locked balls are not in play: a ball that leaves them behind is the
    // last one.
    if (otherLiveBalls(slot) > 0) {
      loseBallWithOthersLive(slot, lost ? BallLoss::OutOfBounds : BallLoss::Drained);
    } else if (queuedBalls_ > 0) {
      // The last ball on the table, but not the last in the game: one is
      // still waiting to be served. It takes this one's place at once, so
      // that there is always a ball on the table.
      loseBallWithOthersLive(slot, lost ? BallLoss::OutOfBounds : BallLoss::Drained);
      serveQueuedBall(slot);
    } else if (lost) {
      // Fail safe: a ball outside the table or with a corrupt state is put
      // back. Rules must hear of it: there is no drain to end a tilt, and a
      // tilted table with the ball on the plunger could never launch it again.
      rules_.onBallReturned();
      serveBall();
    } else {
      handleDrain(slot);
    }
  }
}

void Game::updateInPlay() {
  bool anyInPlay = false;
  for (std::size_t slot = 0; slot < kMaxBalls; ++slot) {
    if (!physics_.ballActive(slot)) {
      continue;
    }
    // A ball in a hole is on the playfield wherever the hole is. There is
    // no gate to shut behind a launched ball: the inner arch lets a ball
    // out of the launch channel and never back in.
    BallState& ball = balls_[slot];
    ball.inPlay =
        ball.inPlay || physics_.ballHeld(slot) || table::inPlayArea(physics_.ballPosition(slot));
    // A locked ball is out of play: it does not keep the ball "in play"
    // for the rules while the player has a new one on the plunger.
    anyInPlay = anyInPlay || (ball.inPlay && ball.lockBay == 0);
  }
  if (anyInPlay) {
    enterPlay();
  }
}

bool Game::requestBall() noexcept {
  if (rules_.gameOver() || physics_.activeBallCount() + queuedBalls_ >= kMaxBalls) {
    return false;
  }
  ++queuedBalls_;
  return true;
}

void Game::serveQueuedBall(std::size_t slot) {
  if (queuedBalls_ == 0 || !physics_.activateBall(slot, table::kBallStart, {0.0F, 0.0F})) {
    return;
  }
  --queuedBalls_;
  BallState& ball = balls_[slot];
  ball = {};
  ball.searchAnchor = table::kBallStart;
  ball.previous = ballView(slot);
  ball.placed = true;
  ball.autoLaunch = true;
  ball.launchSteps = stepsIn(kAutoLaunchDelaySeconds);
  lastAutoServeStep_ = stepCount_ + 1;
}

void Game::updateAutoLaunch() {
  // Launch, or launch again, the balls that are served this way.
  for (std::size_t slot = 0; slot < kMaxBalls; ++slot) {
    BallState& ball = balls_[slot];
    if (!physics_.ballActive(slot) || physics_.ballHeld(slot) || ball.inPlay) {
      continue;
    }
    const Vec2 p = physics_.ballPosition(slot);
    if (!ball.autoLaunch && rules_.tilted() &&
        p.y > table::kBallStart.y - 2.0F * table::kBallRadius &&
        std::fabs(physics_.ballVelocity(slot).y) < 8.0F) {
      // A ball waiting on the plunger of a tilted table: the plunger is
      // dead, so it is launched for the player. Otherwise it would sit
      // there and the tilt, which ends when the last ball drains, never would.
      ball.autoLaunch = true;
      ball.launched = false;
      ball.launchSteps = stepsIn(kAutoLaunchDelaySeconds);
    }
    if (!ball.autoLaunch) {
      continue;
    }
    if (!ball.launched) {
      if (ball.launchSteps > 0) {
        --ball.launchSteps;
        continue;
      }
    } else {
      // Launched and seen back at rest on the plunger: once more.
      const bool atRest = p.y > table::kBallStart.y - 2.0F * table::kBallRadius &&
                          std::fabs(physics_.ballVelocity(slot).y) < 8.0F;
      ball.launchSteps = atRest ? ball.launchSteps + 1 : 0;
      if (ball.launchSteps < stepsIn(kAutoRelaunchSeconds)) {
        continue;
      }
      ++autoRelaunchCount_;
    }
    physics_.resetBall(slot, p, {0.0F, -table::kAutoLaunchSpeed});
    ball.launched = true;
    ball.launchSteps = 0;
    ++autoLaunchCount_;
    sounds_ |= soundBit(Sound::Launch);
  }
  if (queuedBalls_ == 0) {
    return;
  }
  // Serve the next one into an empty lane, a while after the last.
  if (lastAutoServeStep_ != 0 &&
      stepCount_ + 1 - lastAutoServeStep_ < stepsIn(kAutoServeSpacingSeconds)) {
    return;
  }
  std::size_t freeSlot = kMaxBalls;
  for (std::size_t slot = kMaxBalls; slot-- > 0;) {
    if (!physics_.ballActive(slot)) {
      freeSlot = slot;
    } else if (!balls_[slot].inPlay) {
      return;  // A ball is still in the lane.
    }
  }
  if (freeSlot < kMaxBalls) {
    serveQueuedBall(freeSlot);
  }
}

std::size_t Game::addBall(Vec2 position, Vec2 velocity) {
  for (std::size_t slot = 0; slot < kMaxBalls; ++slot) {
    if (physics_.ballActive(slot)) {
      continue;
    }
    if (!physics_.activateBall(slot, position, velocity)) {
      break;
    }
    balls_[slot].searchAnchor = position;
    balls_[slot].previous = ballView(slot);
    balls_[slot].placed = true;
    return slot;
  }
  return kMaxBalls;
}

void Game::step(const InputState& input) {
  snapshotForDrawing();
  for (BallState& ball : balls_) {
    ball.placed = false;
  }
  sounds_ = 0;
  gameEventCount_ = 0;
  // Controls act on the press, not for as long as the key is held.
  if (input.newGame && !previousInput_.newGame) {
    startNewGame();
  }
  handleNudges(input);
  const bool playable = !rules_.gameOver() && !rules_.tilted();
  if (playable && ((input.leftFlipper && !previousInput_.leftFlipper) ||
                   (input.rightFlipper && !previousInput_.rightFlipper))) {
    sounds_ |= soundBit(Sound::Flipper);
  }
  // The flipper buttons also move the lit lanes.
  if (input.leftFlipper && !previousInput_.leftFlipper) {
    rules_.onFlipper(false);
  }
  if (input.rightFlipper && !previousInput_.rightFlipper) {
    rules_.onFlipper(true);
  }
  // The plunger lets go: a launch sound if it was drawn back at all.
  if (playable && !input.plunger && previousInput_.plunger && physics_.plungerPull() > 4.0F) {
    sounds_ |= soundBit(Sound::Launch);
  }

  physics_.setFlippers(playable && input.leftFlipper, playable && input.rightFlipper);
  physics_.setPlunger(playable && input.plunger);
  physics_.step(static_cast<float>(kStepSeconds));

  rules_.setTable(tableStatus());
  handleEvents();
  // Balls are always dealt with in slot order, so a step has one outcome.
  // Every ball taken during the step is noted before any held ball is
  // returned, so that the order in which a hole returns its balls does not
  // depend on which slots they are in.
  for (std::size_t slot = 0; slot < kMaxBalls; ++slot) {
    if (physics_.ballActive(slot)) {
      noteHeldBall(slot);
    }
  }
  for (std::size_t slot = 0; slot < kMaxBalls; ++slot) {
    if (physics_.ballActive(slot)) {
      updateHeldBall(slot);
    }
  }
  for (std::size_t slot = 0; slot < kMaxBalls; ++slot) {
    if (physics_.ballActive(slot)) {
      updateBallSearch(slot, playable && input.leftFlipper, playable && input.rightFlipper);
    }
  }
  updateSpinner();
  rules_.tick(allBallsHeld());
  applyRules();

  handleLostBalls();
  if (rules_.gameOver()) {
    // The bays are emptied when the game ends.
    clearLocks();
  }
  updateInPlay();
  updateAutoLaunch();

  for (std::size_t slot = 0; slot < kMaxBalls; ++slot) {
    if (physics_.ballActive(slot) && balls_[slot].placed) {
      // Nothing to draw between the old place and the new one.
      balls_[slot].previous = ballView(slot);
    }
  }

  sounds_ |= rules_.takeSounds();
  ++stepCount_;
  previousInput_ = input;
}

void Game::enterPlay() {
  // Once per ball: the rules arm the ball save when they hear it.
  if (!rules_.ballInPlay()) {
    rules_.onBallInPlay();
  }
}

void Game::handleEvents() {
  constexpr float kFlashSeconds = 0.25F;
  const float fade = static_cast<float>(kStepSeconds) / kFlashSeconds;
  for (float& flash : bumperFlash_) {
    flash = std::max(flash - fade, 0.0F);
  }

  for (float& flash : slingshotFlash_) {
    flash = std::max(flash - fade, 0.0F);
  }

  // A tilted or finished table is dead: the ball still bounces and the
  // holes still take and return it, but nothing lights or sounds for a hit
  // that does not score. The hit counts go on; they record what the ball did.
  const bool live = !rules_.gameOver() && !rules_.tilted();
  SoundSet hitSounds = 0;
  for (const PhysicsEvent& event : physics_.events()) {
    if (!table::isValid(event.element)) {
      continue;
    }
    // Anything the ball can hit is on the playfield, so the first hit puts
    // the ball in play even if it has not been seen clear of the lane yet:
    // the corner of the field under the arch on the right (x 440 to 623
    // above y 300) is not in table::inPlayArea, because the launch channel
    // crosses it.
    if (event.ball < kMaxBalls) {
      balls_[event.ball].inPlay = true;
    }
    enterPlay();
    const std::size_t index = event.element.index;
    switch (event.element.kind) {
      using enum table::ElementKind;
      case Bumper:
        if (live) {
          bumperFlash_[index] = 1.0F;
        }
        ++bumperHits_;
        hitSounds |= soundBit(Sound::Bumper);
        rules_.onBumper(index);
        break;
      case Slingshot:
        if (live) {
          slingshotFlash_[index] = 1.0F;
        }
        hitSounds |= soundBit(Sound::Slingshot);
        rules_.onSlingshot(index);
        break;
      case Target:
        hitSounds |= soundBit(Sound::Target);
        rules_.onTarget(index);
        break;
      case DropTarget:
        hitSounds |= soundBit(Sound::Target);
        rules_.onDropTarget(index);
        break;
      case Rollover:
        ++rolloverCounts_[index];
        hitSounds |= soundBit(Sound::Rollover);
        rules_.onRollover(index, event.ball);
        break;
      case Hole:
      case Kickback:
        // Told to the rules from the physics state itself (noteHeldBall),
        // not from this list, which can drop entries in a very busy step.
        break;
      case Sensor:
        rules_.onSensor(index, event.direction, event.speed, event.ball);
        if (index == table::kSpinnerSensor) {
          addSpins(event.speed, event.direction);
        }
        break;
      case None:
        break;
    }
  }
  if (live) {
    sounds_ |= hitSounds;
  }
}

std::uint64_t Game::nextRandom() noexcept {
  random_ ^= random_ << 13U;
  random_ ^= random_ >> 7U;
  random_ ^= random_ << 17U;
  return random_;
}

bool Game::exitIsClear(std::size_t slot, std::size_t hole, double spacingSeconds) const {
  // The last ball out by this exit needs time to get out of the way.
  const std::uint64_t last = lastReleaseStep_[hole];
  if (last != 0 && static_cast<double>(stepCount_ + 1 - last) * kStepSeconds < spacingSeconds) {
    return false;
  }
  // And no ball may be sitting where this one would appear.
  const Vec2 exit = table::kHoles[hole].exitPosition;
  for (std::size_t other = 0; other < kMaxBalls; ++other) {
    if (other == slot || !physics_.ballActive(other) || physics_.ballHeld(other)) {
      continue;
    }
    const Vec2 d = physics_.ballPosition(other) - exit;
    if (d.x * d.x + d.y * d.y < kHoleExitClearance * kHoleExitClearance) {
      return false;
    }
  }
  return true;
}

bool Game::firstInHole(std::size_t slot) const noexcept {
  // Balls passing through leave in the order they came; locked balls that
  // have been let go follow them, bay by bay.
  const BallState& ball = balls_[slot];
  for (std::size_t other = 0; other < kMaxBalls; ++other) {
    const BallState& rival = balls_[other];
    if (other == slot || !physics_.ballHeld(other) || rival.holder != ball.holder ||
        (rival.lockBay != 0 && !rival.releasing)) {
      continue;
    }
    const bool rivalFirst =
        (rival.lockBay == 0) != (ball.lockBay == 0)
            ? rival.lockBay == 0
            : (ball.lockBay == 0 ? rival.arrival < ball.arrival : rival.lockBay < ball.lockBay);
    if (rivalFirst) {
      return false;
    }
  }
  return true;
}

Vec2 Game::nextHangarExitVelocity() noexcept {
  // Balls leaving close behind one another go left, right, left: the first
  // lobbed to the left inlane, the next kicked down to the right flipper.
  const std::uint64_t last = lastReleaseStep_[table::kHangarHole];
  const bool follows = last != 0 && static_cast<double>(stepCount_ + 1 - last) * kStepSeconds <=
                                        table::kHangarBurstSeconds;
  hangarBurst_ = follows ? hangarBurst_ + 1 : 0;
  return hangarBurst_ % 2 == 1 ? table::kHangarEjectRight
                               : table::kHoles[table::kHangarHole].exitVelocity;
}

Vec2 Game::nextScoopExitVelocity() noexcept {
  // A little different each time, both ways (table::kScoopExitJitterLeft).
  constexpr int left = table::kScoopExitJitterLeft;
  constexpr int right = table::kScoopExitJitterRight;
  constexpr int down = table::kScoopExitJitterDown;
  Vec2 velocity = table::kHoles[table::kScoopHole].exitVelocity;
  const auto across = static_cast<std::uint64_t>(left + right + 1);
  const auto along = static_cast<std::uint64_t>(2 * down + 1);
  velocity.x += static_cast<float>(static_cast<int>(nextRandom() % across) - left);
  velocity.y += static_cast<float>(static_cast<int>(nextRandom() % along) - down);
  return velocity;
}

void Game::returnBall(std::size_t slot, std::size_t exitHole, Vec2 position, Vec2 velocity) {
  BallState& ball = balls_[slot];
  physics_.releaseBall(slot, position, velocity);
  lastReleaseStep_[exitHole] = stepCount_ + 1;
  ball.placed = true;
  // So that a capture on the very next step still counts as a new one.
  ball.wasHeld = false;
  sounds_ |= soundBit(Sound::HoleOut);
}

void Game::noteHeldBall(std::size_t slot) {
  BallState& ball = balls_[slot];
  const bool held = physics_.ballHeld(slot);
  if (held && !ball.wasHeld) {
    // Taken from the physics state itself, not from the event list, which
    // can drop entries when a step is very busy.
    const table::ElementRef by = physics_.heldBy(slot);
    ball.holder = table::isValid(by) ? by : table::ElementRef{table::ElementKind::Hole, 0};
    ball.heldSteps = 0;
    ball.arrival = ++arrivals_;
    ball.lockBay = 0;
    ball.releasing = false;
    // The held body stays where it was taken.
    ball.takenAt = physics_.ballPosition(slot);
    tellRulesOfCapture(slot);
  }
  ball.wasHeld = held;
}

void Game::tellRulesOfCapture(std::size_t slot) {
  const table::ElementRef by = physics_.heldBy(slot);
  if (!table::isValid(by)) {
    return;
  }
  // A dead table makes no sound for what does not score.
  const bool live = !rules_.gameOver() && !rules_.tilted();
  if (by.kind == table::ElementKind::Kickback) {
    ++kickbackCount_;
    if (live) {
      sounds_ |= soundBit(Sound::Launch);
    }
    note(GameEvent::Type::KickbackFired, slot);
    rules_.onKickback();
    return;
  }
  if (by.kind != table::ElementKind::Hole || by.index >= table::kHoles.size()) {
    return;
  }
  ++holeCounts_[by.index];
  if (live) {
    sounds_ |= soundBit(Sound::HoleIn);
  }
  const Rules::HoleResult result = rules_.onHole(by.index, slot);
  if (table::kHoles[by.index].type != table::HoleType::Hangar) {
    return;  // Only the Hangar can do anything but return the ball.
  }
  if (result == Rules::HoleResult::Lock) {
    lockBall(slot);
  } else if (result == Rules::HoleResult::StartMultiball) {
    releaseLocks();
  }
}

void Game::updateHeldBall(std::size_t slot) {
  BallState& ball = balls_[slot];
  if (!physics_.ballHeld(slot)) {
    return;
  }
  // Set down a little faster or slower each time.
  const auto jittered = [this](Vec2 velocity) {
    constexpr std::uint64_t kValues = 2U * static_cast<std::uint64_t>(table::kRampExitJitter) + 1U;
    velocity.y +=
        static_cast<float>(static_cast<int>(nextRandom() % kValues) - table::kRampExitJitter);
    return velocity;
  };

  if (ball.holder.kind == table::ElementKind::Kickback) {
    // Carried along the kickback's path, then set down where the Runway's
    // rail sets its balls down, and as it does.
    ++ball.heldSteps;
    const table::Hole& runway = table::kHoles[table::kRunwayHole];
    if (static_cast<double>(ball.heldSteps) * kStepSeconds >= table::kKickbackSeconds &&
        exitIsClear(slot, table::kRunwayHole, kHoleReleaseSpacingSeconds)) {
      returnBall(slot, table::kRunwayHole, runway.exitPosition,
                 jittered(table::kKickbackExitVelocity));
      note(GameEvent::Type::KickbackReturned, slot);
      rules_.onKickbackReturned();
    }
    return;
  }

  const std::size_t index = ball.holder.index;
  const table::Hole& hole = table::kHoles[index];
  if (table::isRamp(hole.type)) {
    // Each ball rides the rail on its own clock.
    ++ball.heldSteps;
    if (static_cast<double>(ball.heldSteps) * kStepSeconds >= holdSeconds(hole.type) &&
        exitIsClear(slot, index, kHoleReleaseSpacingSeconds)) {
      returnBall(slot, index, hole.exitPosition, jittered(hole.exitVelocity));
      note(GameEvent::Type::HoleEject, slot, index);
      rules_.onEject(index);
    }
    return;
  }

  // The scoop and the hangar hold one ball at a time. A locked ball stays
  // until it is told to go; any other waits its turn, and its hold runs
  // only once it is the next to leave.
  const bool locked = ball.lockBay != 0;
  if ((locked && !ball.releasing) || !firstInHole(slot)) {
    return;
  }
  // The step in which the ball ahead left belongs to that ball: the next
  // one's turn starts with the step after, whichever slot it is in.
  if (lastReleaseStep_[index] == stepCount_ + 1) {
    return;
  }
  if (!locked) {
    ++ball.heldSteps;
    if (static_cast<double>(ball.heldSteps) * kStepSeconds < holdSeconds(hole.type)) {
      return;
    }
  }
  const bool hangar = hole.type == table::HoleType::Hangar;
  if (!exitIsClear(slot, index,
                   hangar ? table::kLockReleaseSpacingSeconds : kHoleReleaseSpacingSeconds)) {
    return;
  }
  const Vec2 velocity = hangar ? nextHangarExitVelocity() : nextScoopExitVelocity();
  returnBall(slot, index, hole.exitPosition, velocity);
  ball.ejectStep = stepCount_ + 1;
  note(GameEvent::Type::HoleEject, slot, index);
  if (locked) {
    const std::size_t bay = ball.lockBay - 1U;
    ball.lockBay = 0;
    ball.releasing = false;
    ++lockReleaseCount_;
    note(GameEvent::Type::LockReleased, slot, bay);
    rules_.onLockReleased(bay);
  } else {
    rules_.onEject(index);
  }
}

bool Game::doorMouthClear() const {
  for (std::size_t slot = 0; slot < kMaxBalls; ++slot) {
    if (!physics_.ballActive(slot) || physics_.ballHeld(slot)) {
      continue;
    }
    const Vec2 p = physics_.ballPosition(slot);
    if (std::fabs(p.x - table::kDoorMouthCenter.x) < table::kDoorMouthHalfSize.x &&
        std::fabs(p.y - table::kDoorMouthCenter.y) < table::kDoorMouthHalfSize.y) {
      return false;
    }
  }
  return true;
}

void Game::applyRules() {
  switch (rules_.takeDoorCommand()) {
    case Rules::DoorCommand::Drop:
      doorRaisePending_ = false;
      if (physics_.doorBankUp()) {
        physics_.setDoorBank(false);
        note(GameEvent::Type::DoorBankDropped, 0);
        rules_.onDoorBank(false);
      }
      break;
    case Rules::DoorCommand::Raise:
      doorRaisePending_ = !physics_.doorBankUp();
      break;
    case Rules::DoorCommand::None:
      break;
  }
  // The bank never comes up onto a ball: it waits, a step at a time, until
  // the mouth is clear. A ball held inside the Hangar is no obstacle: it
  // comes out below the door line.
  if (doorRaisePending_ && doorMouthClear()) {
    doorRaisePending_ = false;
    physics_.setDoorBank(true);
    note(GameEvent::Type::DoorBankRaised, 0);
    rules_.onDoorBank(true);
  }
  physics_.setKickbackArmed(rules_.kickbackLit());
  // Locked balls the rules let go without a ball entering the Hangar, and
  // balls they want launched. A request past the three-ball limit is
  // refused, never kept.
  if (rules_.takeLockCommand() == Rules::LockCommand::Release) {
    releaseLocks();
  }
  for (int request = rules_.takeBallRequests(); request > 0; --request) {
    if (!requestBall()) {
      rules_.onBallRefused();
    }
  }
}

bool Game::allBallsHeld() const noexcept {
  bool any = false;
  for (std::size_t slot = 0; slot < kMaxBalls; ++slot) {
    if (!ballLive(slot)) {
      continue;
    }
    if (!physics_.ballHeld(slot)) {
      return false;
    }
    any = true;
  }
  return any;
}

TableStatus Game::tableStatus() const noexcept {
  TableStatus status{.live = 0, .free = 0, .queued = static_cast<int>(queuedBalls_), .locked = 0};
  for (std::size_t slot = 0; slot < kMaxBalls; ++slot) {
    if (!physics_.ballActive(slot)) {
      continue;
    }
    if (balls_[slot].lockBay != 0) {
      ++status.locked;
      continue;
    }
    ++status.live;
    status.free += balls_[slot].inPlay && !physics_.ballHeld(slot) ? 1 : 0;
  }
  return status;
}

bool Game::ballAwaitingLaunch() const noexcept {
  if (rules_.gameOver()) {
    return false;
  }
  for (std::size_t slot = 0; slot < kMaxBalls; ++slot) {
    if (ballLive(slot) && !balls_[slot].inPlay && !balls_[slot].autoLaunch &&
        !physics_.ballHeld(slot)) {
      return true;
    }
  }
  return false;
}

void Game::addSpins(float speedAcross, int direction) noexcept {
  spinsOwed_ = std::min(spinsOwed_ + table::spinsFor(speedAcross), table::kMaxSpinsOwed);
  spinDirection_ = direction < 0 ? -1 : 1;
}

void Game::updateSpinner() {
  constexpr std::uint32_t kTurn = 2U * static_cast<std::uint32_t>(table::kSpinPayoutSteps);
  if (spinCooldown_ > 0) {
    // Half a turn for each spin, a step at a time, either way round.
    --spinCooldown_;
    spinnerTicks_ = (spinnerTicks_ + (spinDirection_ > 0 ? 1U : kTurn - 1U)) % kTurn;
  }
  if (spinsOwed_ > 0 && spinCooldown_ == 0) {
    --spinsOwed_;
    spinCooldown_ = table::kSpinPayoutSteps;
    ++spinCount_;
    note(GameEvent::Type::Spin, 0);
    rules_.onSpin();
  }
}

void Game::updateBallSearch(std::size_t slot, bool leftRaised, bool rightRaised) {
  BallState& ball = balls_[slot];
  const Vec2 p = physics_.ballPosition(slot);
  // Not stuck: off the table in a hole, waiting on the plunger, or being
  // cradled on a raised flipper.
  // A cradle means resting on a flipper that is being held up, so only a
  // ball within reach of a raised flipper counts. A key held while the
  // controls are dead (tilt, game over) raises nothing.
  const auto nearFlipper = [p](std::size_t index) {
    const Vec2 pivot = table::kFlippers[index].pivot;
    const float fx = p.x - pivot.x;
    const float fy = p.y - pivot.y;
    constexpr float kReach = table::kFlipperLength + 3.0F * table::kBallRadius;
    return fx * fx + fy * fy < kReach * kReach;
  };
  const bool cradled = (leftRaised && nearFlipper(table::kLeftFlipperIndex)) ||
                       (rightRaised && nearFlipper(table::kRightFlipperIndex));
  const bool exempt = physics_.ballHeld(slot) || p.x > table::kLaneWallX || cradled;
  const float dx = p.x - ball.searchAnchor.x;
  const float dy = p.y - ball.searchAnchor.y;
  constexpr float kMovedPixels = 3.0F;
  if (exempt || dx * dx + dy * dy > kMovedPixels * kMovedPixels) {
    ball.searchAnchor = p;
    ball.stillSteps = 0;
    return;
  }
  ++ball.stillSteps;
  if (static_cast<double>(ball.stillSteps) * kStepSeconds < kBallSearchSeconds) {
    return;
  }
  // Shove it up the table and towards the centre.
  const float towardCentre = p.x < table::kFieldCenterX ? 1.0F : -1.0F;
  physics_.pushBall(slot, {towardCentre * 260.0F, -420.0F});
  ++ballSearchCount_;
  ball.stillSteps = 0;
}

FlipperView Game::flipper(std::size_t index) const {
  if (index >= table::kFlippers.size()) {
    return {};
  }
  return {table::kFlippers[index].pivot, physics_.flipperAngle(index)};
}

BallView Game::ball() const { return ballView(primaryBall()); }

BallView Game::ball(float alpha) const { return ballView(primaryBall(), alpha); }

BallViews Game::balls(float alpha) const {
  BallViews views;
  for (std::size_t slot = 0; slot < kMaxBalls; ++slot) {
    if (physics_.ballActive(slot)) {
      views.items[views.count++] = ballView(slot, alpha);
    }
  }
  return views;
}

BallView Game::ballView(std::size_t slot) const {
  BallView view{physics_.ballPosition(slot), physics_.ballAngle(slot)};
  view.id = static_cast<std::uint8_t>(slot);
  if (!physics_.ballHeld(slot)) {
    return view;
  }
  const BallState& state = balls_[slot];
  if (state.holder.kind == table::ElementKind::Kickback) {
    // Carried up the outlane and over the divider: fast at first, slowing
    // to a stop where it is set down. From where it was taken.
    const auto& path = table::kKickbackPath;
    const auto point = [&state](std::size_t i) {
      return i == 0 ? state.takenAt : table::kKickbackPath[i];
    };
    float length = 0.0F;
    for (std::size_t i = 1; i < path.size(); ++i) {
      length += lengthOf(point(i) - point(i - 1));
    }
    const float t = std::clamp(static_cast<float>(static_cast<double>(state.heldSteps) *
                                                  kStepSeconds / table::kKickbackSeconds),
                               0.0F, 1.0F);
    float left = (1.0F - (1.0F - t) * (1.0F - t)) * length;
    view.position = path.back();
    for (std::size_t i = 1; i < path.size(); ++i) {
      const Vec2 run = point(i) - point(i - 1);
      const float runLength = lengthOf(run);
      if (left < runLength && runLength > 0.0F) {
        view.position = point(i - 1) + run * (left / runLength);
        break;
      }
      left -= runLength;
    }
    return view;
  }
  const table::Hole& hole = table::kHoles[state.holder.index];
  if (state.lockBay != 0) {
    // In its bay, in view, until it is let go.
    view.position = table::kLockBays[state.lockBay - 1U];
    view.angle = 0.0F;
    view.locked = true;
    return view;
  }
  if (!table::isRamp(hole.type)) {
    view.visible = false;
    return view;
  }
  // On a ramp the ball stays in sight, riding the rail over the table to the
  // exit at a steady speed. The ride starts from where the ball was taken,
  // which can be anywhere across the ramp's mouth, and joins the rail at its
  // second point.
  const std::array<Vec2, 9>& rail = table::rampRail(hole.type);
  const auto point = [&rail, &state](std::size_t i) { return i == 0 ? state.takenAt : rail[i]; };
  float length = 0.0F;
  for (std::size_t i = 1; i < rail.size(); ++i) {
    length += lengthOf(point(i) - point(i - 1));
  }
  const float t = std::clamp(static_cast<float>(static_cast<double>(state.heldSteps) *
                                                kStepSeconds / table::kRampRideSeconds),
                             0.0F, 1.0F);
  view.raised = true;
  // Larger while it rides; a ball kept waiting at the end for the exit to
  // clear is drawn there full size.
  view.scale = t < 1.0F ? kRailScale : 1.0F;
  view.position = rail.back();
  float left = t * length;
  for (std::size_t i = 1; i < rail.size(); ++i) {
    const Vec2 run = point(i) - point(i - 1);
    const float runLength = lengthOf(run);
    if (left < runLength && runLength > 0.0F) {
      view.position = point(i - 1) + run * (left / runLength);
      break;
    }
    left -= runLength;
  }
  return view;
}

BallView Game::ballView(std::size_t slot, float alpha) const {
  const BallView now = ballView(slot);
  const BallView& before = balls_[slot].previous;
  const float t = blendFactor(alpha);
  // A hidden ball has no place to draw from or to.
  if (t >= 1.0F || !now.visible || !before.visible) {
    return now;
  }
  BallView view = now;
  view.position = before.position + (now.position - before.position) * t;
  // The angle wraps at half a turn either way, so go round the short way.
  const float turn = std::remainder(now.angle - before.angle, 2.0F * table::kPi);
  view.angle = before.angle + turn * t;
  view.scale = before.scale + (now.scale - before.scale) * t;
  return view;
}

FlipperView Game::flipper(std::size_t index, float alpha) const {
  FlipperView view = flipper(index);
  const float t = blendFactor(alpha);
  if (t < 1.0F && index < previousFlipperAngles_.size()) {
    const float before = previousFlipperAngles_[index];
    view.angle = before + (view.angle - before) * t;
  }
  return view;
}

}  // namespace pinball
