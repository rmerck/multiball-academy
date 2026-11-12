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

  for (const PhysicsEvent& event : physics_.events()) {
    if (event.element.kind == table::ElementKind::Bumper &&
        event.element.index < bumperFlash_.size()) {
      bumperFlash_[event.element.index] = 1.0F;
      ++bumperHits_;
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
