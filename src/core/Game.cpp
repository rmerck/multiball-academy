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
      case table::ElementKind::None:
        break;
    }
  }
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

BallView Game::ball() const { return {physics_.ballPosition(), physics_.ballAngle()}; }

}  // namespace pinball
