#include "core/Game.hpp"

#include <algorithm>
#include <cmath>

#include "core/TableLayout.hpp"

namespace pinball {

Game::Game() { serveBall(); }

void Game::serveBall() {
  physics_.resetBall(table::kBallStart, {0.0F, 0.0F});
  physics_.setLaneGateClosed(false);
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
  updateBallSearch(input);
  rules_.tick();

  if (!physics_.ballIsSane()) {
    // Fail safe: a ball outside the table or with a corrupt state is put back.
    serveBall();
  } else if (physics_.ballPosition().y > table::kDrainY) {
    handleDrain();
  }

  // Once the launched ball is clear of the lane, shut the gate behind it.
  if (!physics_.laneGateClosed() && physics_.ballPosition().x < table::kLaneGateCloseX) {
    physics_.setLaneGateClosed(true);
    rules_.onBallInPlay();
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
      case table::ElementKind::Bumper:
        bumperFlash_[index] = 1.0F;
        ++bumperHits_;
        sounds_ |= soundBit(Sound::Bumper);
        rules_.onBumper(index);
        break;
      case table::ElementKind::Slingshot:
        slingshotFlash_[index] = 1.0F;
        sounds_ |= soundBit(Sound::Slingshot);
        rules_.onSlingshot(index);
        break;
      case table::ElementKind::Target:
        sounds_ |= soundBit(Sound::Target);
        rules_.onTarget(index);
        break;
      case table::ElementKind::Rollover:
        ++rolloverCounts_[index];
        sounds_ |= soundBit(Sound::Rollover);
        rules_.onRollover(index);
        break;
      case table::ElementKind::Hole:
        ++holeCounts_[index];
        heldBy_ = index;
        heldSteps_ = 0;
        sounds_ |= soundBit(Sound::HoleIn);
        rules_.onHole(index);
        break;
      case table::ElementKind::None:
        break;
    }
  }
}

void Game::updateHeldBall() {
  if (!physics_.ballHeld()) {
    return;
  }
  ++heldSteps_;
  if (static_cast<double>(heldSteps_) * kStepSeconds >= kHoleHoldSeconds) {
    const table::Hole& hole = table::kHoles[heldBy_];
    physics_.releaseBall(hole.exitPosition, hole.exitVelocity);
    sounds_ |= soundBit(Sound::HoleOut);
  }
}

void Game::updateBallSearch(const InputState& input) {
  const Vec2 p = physics_.ballPosition();
  // Not stuck: off the table in a hole, waiting on the plunger, or being
  // cradled on a raised flipper.
  const bool exempt = physics_.ballHeld() || p.x > table::kLaneWallX || input.leftFlipper ||
                      input.rightFlipper;
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
  // On the ramp the ball stays in sight, travelling over the table from the
  // entry to the exit and looking larger at the top of its arc.
  const float t = std::clamp(
      static_cast<float>(static_cast<double>(heldSteps_) * kStepSeconds / kHoleHoldSeconds), 0.0F,
      1.0F);
  view.position = hole.center + (hole.exitPosition - hole.center) * t;
  view.scale = 1.0F + 0.35F * 4.0F * t * (1.0F - t);
  return view;
}

}  // namespace pinball
