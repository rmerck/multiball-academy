#include "core/Game.hpp"

#include <algorithm>

#include "core/TableLayout.hpp"

namespace pinball {

Game::Game() { serveBall(); }

void Game::serveBall() {
  physics_.resetBall(table::kBallStart, {0.0F, 0.0F});
  physics_.setLaneGateClosed(false);
}

void Game::step(const InputState& input) {
  physics_.setFlippers(input.leftFlipper, input.rightFlipper);
  physics_.setPlunger(input.plunger);
  physics_.step(static_cast<float>(kStepSeconds));

  handleEvents();
  updateHeldBall();
  updateBallSearch(input);

  if (!physics_.ballIsSane()) {
    // Fail safe: a ball outside the table or with a corrupt state is put back.
    serveBall();
  } else if (physics_.ballPosition().y > table::kDrainY) {
    ++drainCount_;
    serveBall();
  }

  // Once the launched ball is clear of the lane, shut the gate behind it.
  if (!physics_.laneGateClosed() && physics_.ballPosition().x < table::kLaneGateCloseX) {
    physics_.setLaneGateClosed(true);
  }

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
        break;
      case table::ElementKind::Slingshot:
        slingshotFlash_[index] = 1.0F;
        break;
      case table::ElementKind::Target:
        lightTarget(index);
        break;
      case table::ElementKind::Rollover:
        ++rolloverCounts_[index];
        break;
      case table::ElementKind::Hole:
        ++holeCounts_[index];
        break;
      case table::ElementKind::None:
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
  }
}

void Game::updateBallSearch(const InputState& input) {
  const Vec2 p = physics_.ballPosition();
  // Not stuck: off the table in a hole, waiting on the plunger, or being
  // cradled on a raised flipper.
  // A cradle means resting on a flipper that is being held up, so only a
  // ball within reach of a held flipper counts.
  const auto nearFlipper = [p](std::size_t index) {
    const Vec2 pivot = table::kFlippers[index].pivot;
    const float fx = p.x - pivot.x;
    const float fy = p.y - pivot.y;
    constexpr float kReach = table::kFlipperLength + 3.0F * table::kBallRadius;
    return fx * fx + fy * fy < kReach * kReach;
  };
  const bool cradled = (input.leftFlipper && nearFlipper(table::kLeftFlipperIndex)) ||
                       (input.rightFlipper && nearFlipper(table::kRightFlipperIndex));
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

void Game::lightTarget(std::size_t index) {
  targetLit_[index] = true;
  const table::TargetBank bank = table::kTargets[index].bank;

  // When the whole bank is lit, count it and reset the bank.
  for (std::size_t i = 0; i < table::kTargets.size(); ++i) {
    if (table::kTargets[i].bank == bank && !targetLit_[i]) {
      return;
    }
  }
  ++bankCompletions_[static_cast<std::size_t>(bank)];
  for (std::size_t i = 0; i < table::kTargets.size(); ++i) {
    if (table::kTargets[i].bank == bank) {
      targetLit_[i] = false;
    }
  }
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
