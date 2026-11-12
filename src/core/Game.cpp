#include "core/Game.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "core/TableLayout.hpp"

namespace pinball {

namespace {

// Guards the blend factor: outside [0, 1] is clamped, not-a-number is 1.
float blendFactor(float alpha) noexcept {
  return alpha >= 0.0F && alpha <= 1.0F ? alpha : (alpha < 0.0F ? 0.0F : 1.0F);
}

}  // namespace

Game::Game() {
  serveBall();
  snapshotForDrawing();
}

void Game::serveBall() {
  physics_.resetBall(table::kBallStart, {0.0F, 0.0F});
  physics_.setLaneGateClosed(false);
  ballPlaced_ = true;
}

void Game::snapshotForDrawing() {
  previousBall_ = ball();
  for (std::size_t i = 0; i < previousFlipperAngles_.size(); ++i) {
    previousFlipperAngles_[i] = physics_.flipperAngle(i);
  }
}

void Game::startNewGame() {
  rules_.startGame();
  bumperFlash_.fill(0.0F);
  slingshotFlash_.fill(0.0F);
  stillSteps_ = 0;
  shake_ = {};
  serveBall();
}

void Game::handleNudges(const InputState& input) {
  // The drawn displacement springs back quickly.
  shake_ = shake_ * 0.86F;
  if (std::fabs(shake_.x) < 0.05F && std::fabs(shake_.y) < 0.05F) {
    shake_ = {};
  }

  // As in the original, a bump from one side sends the ball towards the
  // other: bump from the left and the ball jumps right. A bump from the
  // bottom sends it up the table.
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
      physics_.pushBall(nudge.ballKick);
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

void Game::step(const InputState& input) {
  snapshotForDrawing();
  ballPlaced_ = false;
  sounds_ = 0;
  // Controls act on the press, not for as long as the key is held.
  if (input.newGame && !previousInput_.newGame) {
    startNewGame();
  }
  handleNudges(input);
  const bool playable = !rules_.gameOver() && !rules_.tilted();
  if (playable && input.leftFlipper && !previousInput_.leftFlipper) {
    rules_.onFlipper(true);
    sounds_ |= soundBit(Sound::Flipper);
  }
  if (playable && input.rightFlipper && !previousInput_.rightFlipper) {
    rules_.onFlipper(false);
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
  updateHeldBall();
  updateBallSearch(playable && input.leftFlipper, playable && input.rightFlipper);
  rules_.tick();

  if (!physics_.ballIsSane()) {
    // Fail safe: a ball outside the table or with a corrupt state is put back.
    // Rules must hear of it: there is no drain to end a tilt, and a tilted
    // table with the ball on the plunger could never launch it again.
    rules_.onBallReturned();
    serveBall();
  } else if (physics_.ballPosition().y > table::kDrainY) {
    handleDrain();
  }

  // Once the launched ball is clear of the lane, shut the gate behind it.
  if (!physics_.laneGateClosed() && physics_.ballPosition().x < table::kLaneGateCloseX) {
    physics_.setLaneGateClosed(true);
    rules_.onBallInPlay();
  }

  if (ballPlaced_) {
    // Nothing to draw between the old place and the new one.
    previousBall_ = ball();
  }

  sounds_ |= rules_.takeSounds();
  ++stepCount_;
  previousInput_ = input;
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

  for (const PhysicsEvent& event : physics_.events()) {
    if (!table::isValid(event.element)) {
      continue;
    }
    const std::size_t index = event.element.index;
    switch (event.element.kind) {
      using enum table::ElementKind;
      case Bumper:
        bumperFlash_[index] = 1.0F;
        ++bumperHits_;
        sounds_ |= soundBit(Sound::Bumper);
        rules_.onBumper(index);
        break;
      case Slingshot:
        slingshotFlash_[index] = 1.0F;
        sounds_ |= soundBit(Sound::Slingshot);
        rules_.onSlingshot(index);
        break;
      case Target:
        sounds_ |= soundBit(Sound::Target);
        rules_.onTarget(index);
        break;
      case Rollover:
        ++rolloverCounts_[index];
        sounds_ |= soundBit(Sound::Rollover);
        rules_.onRollover(index);
        break;
      case Hole:
        ++holeCounts_[index];
        sounds_ |= soundBit(Sound::HoleIn);
        rules_.onHole(index);
        break;
      case None:
        break;
    }
  }
}

void Game::updateHeldBall() {
  const bool held = physics_.ballHeld();
  if (held && !wasHeld_) {
    // Taken from the physics state itself, not from the event list, which
    // can drop entries when a step is very busy.
    heldBy_ = std::min(physics_.heldHole(), table::kHoles.size() - 1);
    heldSteps_ = 0;
  }
  wasHeld_ = held;
  if (!held) {
    return;
  }
  ++heldSteps_;
  if (static_cast<double>(heldSteps_) * kStepSeconds >= kHoleHoldSeconds) {
    const table::Hole& hole = table::kHoles[heldBy_];
    physics_.releaseBall(hole.exitPosition, hole.exitVelocity);
    ballPlaced_ = true;
    // So that a capture on the very next step still counts as a new one.
    wasHeld_ = false;
    sounds_ |= soundBit(Sound::HoleOut);
  }
}

void Game::updateBallSearch(bool leftRaised, bool rightRaised) {
  const Vec2 p = physics_.ballPosition();
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
  const bool exempt = physics_.ballHeld() || p.x > table::kLaneWallX || cradled;
  const float dx = p.x - searchAnchor_.x;
  const float dy = p.y - searchAnchor_.y;
  constexpr float kMovedPixels = 3.0F;
  if (exempt || dx * dx + dy * dy > kMovedPixels * kMovedPixels) {
    searchAnchor_ = p;
    stillSteps_ = 0;
    return;
  }
  ++stillSteps_;
  if (static_cast<double>(stillSteps_) * kStepSeconds < kBallSearchSeconds) {
    return;
  }
  // Shove it up the table and towards the centre.
  const float towardCentre = p.x < table::kFieldCenterX ? 1.0F : -1.0F;
  physics_.pushBall({towardCentre * 260.0F, -420.0F});
  ++ballSearchCount_;
  stillSteps_ = 0;
}

FlipperView Game::flipper(std::size_t index) const {
  if (index >= table::kFlippers.size()) {
    return {};
  }
  return {table::kFlippers[index].pivot, physics_.flipperAngle(index)};
}

BallView Game::ball() const {
  BallView view{physics_.ballPosition(), physics_.ballAngle()};
  if (!physics_.ballHeld()) {
    return view;
  }
  const table::Hole& hole = table::kHoles[heldBy_];
  if (hole.type != table::HoleType::Ramp) {
    view.visible = false;
    return view;
  }
  // On the ramp the ball stays in sight, travelling over the table to the
  // exit and looking larger at the top of its arc. It starts from where it
  // was taken (the held body stays there), which can be anywhere across the
  // ramp's mouth.
  const float t = std::clamp(
      static_cast<float>(static_cast<double>(heldSteps_) * kStepSeconds / kHoleHoldSeconds), 0.0F,
      1.0F);
  view.position = view.position + (hole.exitPosition - view.position) * t;
  view.scale = 1.0F + 0.35F * 4.0F * t * (1.0F - t);
  return view;
}

BallView Game::ball(float alpha) const {
  const BallView now = ball();
  const float t = blendFactor(alpha);
  // A hidden ball has no place to draw from or to.
  if (t >= 1.0F || !now.visible || !previousBall_.visible) {
    return now;
  }
  BallView view = now;
  view.position = previousBall_.position + (now.position - previousBall_.position) * t;
  // The angle wraps at half a turn either way, so go round the short way.
  const float turn = std::remainder(now.angle - previousBall_.angle, 2.0F * table::kPi);
  view.angle = previousBall_.angle + turn * t;
  view.scale = previousBall_.scale + (now.scale - previousBall_.scale) * t;
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
