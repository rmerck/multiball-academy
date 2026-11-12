#include "core/Game.hpp"

#include "core/TableLayout.hpp"

namespace pinball {

namespace {

// Temporary automatic launch until the plunger exists: fast enough to clear
// the lane and travel over the arch.
constexpr Vec2 kAutoLaunchVelocity{0.0F, -1400.0F};

}  // namespace

Game::Game() { serveBall(); }

void Game::serveBall() { physics_.resetBall(table::kBallStart, kAutoLaunchVelocity); }

void Game::step(const InputState& input) {
  physics_.setFlippers(input.leftFlipper, input.rightFlipper);
  physics_.step(static_cast<float>(kStepSeconds));

  if (!physics_.ballIsSane()) {
    // Fail safe: a ball outside the table or with a corrupt state is put back.
    serveBall();
  } else if (physics_.ballPosition().y > table::kDrainY) {
    ++drainCount_;
    serveBall();
  }

  ++stepCount_;
  previousInput_ = input;
}

FlipperView Game::flipper(std::size_t index) const {
  if (index >= table::kFlippers.size()) {
    return {};
  }
  return {table::kFlippers[index].pivot, physics_.flipperAngle(index)};
}

BallView Game::ball() const { return {physics_.ballPosition(), physics_.ballAngle()}; }

}  // namespace pinball
