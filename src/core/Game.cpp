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
  for (std::size_t slot = 0; slot < kMaxBalls; ++slot) {
    if (physics_.ballActive(slot)) {
      return slot;
    }
  }
  return 0;
}

void Game::serveBall() {
  // One ball from here on: the first slot's, and nobody else's.
  for (std::size_t slot = 1; slot < kMaxBalls; ++slot) {
    removeBall(slot);
  }
  queuedBalls_ = 0;
  if (physics_.ballActive(0)) {
    physics_.resetBall(table::kBallStart, {0.0F, 0.0F});
  } else {
    // Its state was cleared when it was taken off the table.
    physics_.activateBall(0, table::kBallStart, {0.0F, 0.0F});
  }
  balls_[0].inPlay = false;
  balls_[0].autoLaunch = false;
  balls_[0].placed = true;
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

void Game::handleDrain() {
  ++drainCount_;
  // Whatever the outcome, a ball goes back on the plunger. After the last
  // ball it simply waits there: the controls are off until a new game.
  rules_.onDrain();
  serveBall();
}

void Game::removeBall(std::size_t slot) {
  physics_.deactivateBall(slot);
  // The slot's next ball starts with nothing of this one's.
  balls_[slot] = {};
}

void Game::loseBallWithOthersLive(std::size_t slot, BallLoss loss) {
  // Play goes on with the others and nothing is served. A drained ball is
  // counted; one lost outside the table is simply gone. (When the rules
  // learn about several balls, this is where they are told.)
  if (loss == BallLoss::Drained) {
    ++drainCount_;
  }
  removeBall(slot);
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
    if (physics_.activeBallCount() > 1) {
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
      handleDrain();
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
    anyInPlay = anyInPlay || ball.inPlay;
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
    if (!physics_.ballActive(slot) || !ball.autoLaunch || ball.inPlay) {
      continue;
    }
    const Vec2 p = physics_.ballPosition(slot);
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
  // The plunger lets go: a launch sound if it was drawn back at all.
  if (playable && !input.plunger && previousInput_.plunger && physics_.plungerPull() > 4.0F) {
    sounds_ |= soundBit(Sound::Launch);
  }

  physics_.setFlippers(playable && input.leftFlipper, playable && input.rightFlipper);
  physics_.setPlunger(playable && input.plunger);
  physics_.step(static_cast<float>(kStepSeconds));

  handleEvents();
  // Balls are always dealt with in slot order, so a step has one outcome.
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
  rules_.tick();

  handleLostBalls();
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
    // the ball in play even if it has not been seen clear of the lane yet.
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
        rules_.onRollover(index);
        break;
      case Hole:
        ++holeCounts_[index];
        hitSounds |= soundBit(Sound::HoleIn);
        rules_.onHole(index);
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

bool Game::exitIsClear(std::size_t slot, std::size_t hole) const {
  // The last ball out by this exit needs time to get out of the way.
  const std::uint64_t last = lastReleaseStep_[hole];
  if (last != 0 &&
      static_cast<double>(stepCount_ + 1 - last) * kStepSeconds < kHoleReleaseSpacingSeconds) {
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

void Game::updateHeldBall(std::size_t slot) {
  BallState& ball = balls_[slot];
  const bool held = physics_.ballHeld(slot);
  if (held && !ball.wasHeld) {
    // Taken from the physics state itself, not from the event list, which
    // can drop entries when a step is very busy.
    ball.heldBy = std::min(physics_.heldHole(slot), table::kHoles.size() - 1);
    ball.heldSteps = 0;
    // The held body stays where it was taken.
    ball.takenAt = physics_.ballPosition(slot);
  }
  ball.wasHeld = held;
  if (!held) {
    return;
  }
  ++ball.heldSteps;
  const table::Hole& hole = table::kHoles[ball.heldBy];
  if (static_cast<double>(ball.heldSteps) * kStepSeconds >= holdSeconds(hole.type) &&
      exitIsClear(slot, ball.heldBy)) {
    Vec2 velocity = hole.exitVelocity;
    if (table::isRamp(hole.type)) {
      // Set down a little faster or slower each time.
      constexpr std::uint64_t kValues =
          2U * static_cast<std::uint64_t>(table::kRampExitJitter) + 1U;
      velocity.y +=
          static_cast<float>(static_cast<int>(nextRandom() % kValues) - table::kRampExitJitter);
    }
    physics_.releaseBall(slot, hole.exitPosition, velocity);
    lastReleaseStep_[ball.heldBy] = stepCount_ + 1;
    ball.placed = true;
    // So that a capture on the very next step still counts as a new one.
    ball.wasHeld = false;
    sounds_ |= soundBit(Sound::HoleOut);
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
  const table::Hole& hole = table::kHoles[state.heldBy];
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
