#include "core/Rules.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace pinball {

namespace {

// The one message a tilted table shows.
constexpr display::Message kTiltMessage{"TILT"};

}  // namespace

Rules::Rules(double stepSeconds) noexcept
    : stepSeconds_(std::isfinite(stepSeconds) && stepSeconds > 0.0 ? stepSeconds : 1.0 / 120.0) {
  startGame();
}

std::uint32_t Rules::stepsFor(double seconds) const noexcept {
  // Clamped before converting: an extreme step length must not ask for a
  // count outside the integer's range, and a timer always lasts one step.
  const double steps = std::round(seconds / stepSeconds_);
  return static_cast<std::uint32_t>(std::clamp(steps, 1.0, 4.0e9));
}

void Rules::startGame() noexcept {
  score_ = 0;
  ball_ = 1;
  gameOver_ = false;
  sounds_ = 0;
  ballInPlay_ = false;
  resetBall();
  say("LAUNCH THE BALL");
}

void Rules::resetBall() noexcept {
  // What a new ball starts without.
  ballSaveSteps_ = 0;
  ballSaveUsed_ = false;
  tilted_ = false;
  tiltHeat_ = 0.0;
}

void Rules::say(display::Message text) noexcept {
  // While tilted the display says so and nothing else.
  if (tilted_ && text.text() != kTiltMessage.text()) {
    return;
  }
  message_ = text.text();
  messageSteps_ = stepsFor(kMessageSeconds);
}

void Rules::award(std::uint64_t points) noexcept {
  // A dead table scores nothing.
  if (gameOver_ || tilted_) {
    return;
  }
  // Saturating: the score can never wrap.
  score_ = points >= points::kMaxScore - score_ ? points::kMaxScore : score_ + points;
}

void Rules::onBumper(std::size_t index) noexcept {
  if (index < table::kBumpers.size()) {
    award(points::kBumper);
  }
}

void Rules::onSlingshot(std::size_t index) noexcept {
  if (index < table::kSlingshots.size()) {
    award(points::kSlingshot);
  }
}

void Rules::onTarget(std::size_t index) noexcept {
  if (index < table::kTargets.size()) {
    award(points::kTarget);
  }
}

void Rules::onDropTarget(std::size_t index) noexcept {
  if (index < table::kDropTargets.size()) {
    award(points::kBayDoor);
  }
}

void Rules::onRollover(std::size_t index) noexcept {
  if (index >= table::kRollovers.size()) {
    return;
  }
  award(table::kRollovers[index].group == table::RolloverGroup::TopLane ? points::kRollCallLane
                                                                        : points::kReturnLane);
}

void Rules::onHole(std::size_t index) noexcept {
  if (index >= table::kHoles.size() || gameOver_ || tilted_) {
    return;
  }
  switch (table::kHoles[index].type) {
    using enum table::HoleType;
    case Scoop:
      award(points::kBriefingRoom);
      say("BRIEFING");
      break;
    case Hangar:
      award(points::kHangar);
      say("HANGAR");
      break;
    case RampLeft:
      award(points::kRamp);
      say("RUNWAY");
      break;
    case RampRight:
      award(points::kRamp);
      say("SKYWAY");
      break;
  }
}

void Rules::onBallInPlay() noexcept {
  if (gameOver_) {
    return;
  }
  ballInPlay_ = true;
  if (!ballSaveUsed_) {
    ballSaveSteps_ = stepsFor(kBallSaveSeconds);
  }
}

void Rules::onBallReturned() noexcept {
  if (gameOver_) {
    return;
  }
  ballInPlay_ = false;
  // A tilt cannot outlive the ball's time in play: on the plunger it would
  // leave the controls dead for good.
  tilted_ = false;
  tiltHeat_ = 0.0;
  say("BALL SAVED");
}

Rules::DrainResult Rules::onDrain() noexcept {
  ballInPlay_ = false;
  if (gameOver_) {
    return DrainResult::GameOver;
  }
  // A tilt ends with the ball, whatever happens next.
  const bool wasTilted = tilted_;
  tilted_ = false;
  tiltHeat_ = 0.0;
  if (ballSaveSteps_ > 0 && !wasTilted) {
    ballSaveSteps_ = 0;
    ballSaveUsed_ = true;
    say("BALL SAVED");
    cue(Sound::BallSaved);
    return DrainResult::BallSaved;
  }
  resetBall();

  if (ball_ >= kBallsPerGame) {
    gameOver_ = true;
    // No word on how to start again: that depends on the device (a key, a
    // button, a tap), so the platform draws the prompt.
    say(kGameOverMessage);
    cue(Sound::GameOver);
    return DrainResult::GameOver;
  }
  ++ball_;
  cue(Sound::Drain);
  say("NEXT BALL");
  return DrainResult::NextBall;
}

bool Rules::onNudge() noexcept {
  // Only a ball in play can be nudged or tilted. A tilt with the ball still
  // on the plunger would lock the table: the plunger would be dead and the
  // ball could never drain to clear it.
  if (gameOver_ || tilted_ || !ballInPlay_) {
    return false;
  }
  tiltHeat_ += 1.0;
  if (tiltHeat_ >= kTiltLimit) {
    tilted_ = true;
    tiltHeat_ = kTiltLimit;
    ballSaveSteps_ = 0;  // A tilted ball is not returned.
    say(kTiltMessage);
    cue(Sound::Tilt);
    return false;
  }
  if (tiltHeat_ >= kTiltLimit - 1.0) {
    say("DANGER  TILT WARNING");
    cue(Sound::TiltWarning);
  }
  return true;
}

void Rules::tick() noexcept {
  if (ballSaveSteps_ > 0) {
    --ballSaveSteps_;
  }
  if (!tilted_) {
    tiltHeat_ = std::max(0.0, tiltHeat_ - stepSeconds_ / kTiltCoolSeconds);
  }
  if (messageSteps_ > 0 && --messageSteps_ == 0 && !gameOver_ && !tilted_) {
    message_ = {};
  }
}

}  // namespace pinball
