#include "core/Rules.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

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
  // The bank stands, nothing is locked, the kickback is lit.
  doorMarked_.fill(false);
  doorCommand_ = DoorCommand::Raise;
  doorRaiseSteps_ = 0;
  lockLit_ = false;
  locksHeld_ = 0;
  kickbackLit_ = true;
  wingLit_.fill(false);
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
  if (index >= table::kDropTargets.size() || gameOver_ || tilted_) {
    return;
  }
  if (doorMarked_[index]) {
    award(points::kBayDoorAgain);
    return;
  }
  award(points::kBayDoor);
  doorMarked_[index] = true;
  if (std::ranges::all_of(doorMarked_, [](bool marked) { return marked; })) {
    // All three: the bank drops as one.
    award(points::kBayDoorsOpen);
    doorCommand_ = DoorCommand::Drop;
    doorRaiseSteps_ = 0;
    lockLit_ = locksEnabled_;
    say(lockLit_ ? display::Message("LOCK IS LIT") : display::Message("BAY DOORS OPEN"));
  }
}

void Rules::onRollover(std::size_t index) noexcept {
  if (index >= table::kRollovers.size()) {
    return;
  }
  const table::Rollover& rollover = table::kRollovers[index];
  award(rollover.group == table::RolloverGroup::TopLane ? points::kRollCallLane
                                                        : points::kReturnLane);
  if (rollover.id >= table::RolloverId::WingW && !gameOver_ && !tilted_) {
    // The four return lanes together relight the kickback.
    wingLit_[std::to_underlying(rollover.id) - std::to_underlying(table::RolloverId::WingW)] =
        true;
    if (std::ranges::all_of(wingLit_, [](bool lit) { return lit; })) {
      wingLit_.fill(false);
      kickbackLit_ = true;
    }
  }
}

Rules::HoleResult Rules::onHole(std::size_t index) noexcept {
  if (index >= table::kHoles.size() || gameOver_ || tilted_) {
    return HoleResult::Eject;
  }
  switch (table::kHoles[index].type) {
    using enum table::HoleType;
    case Scoop:
      award(points::kBriefingRoom);
      say("BRIEFING");
      break;
    case Hangar:
      award(points::kHangar);
      if (!lockLit_ || !locksEnabled_) {
        say("HANGAR");
        break;
      }
      if (locksHeld_ < static_cast<int>(table::kLockBays.size())) {
        ++locksHeld_;
        say(locksHeld_ == 1 ? display::Message("BALL 1 LOCKED")
                            : display::Message("BALL 2 LOCKED"));
        return HoleResult::Lock;
      }
      // The third ball in: all three go, and the doors come back up.
      locksHeld_ = 0;
      lockLit_ = false;
      doorMarked_.fill(false);
      doorCommand_ = DoorCommand::Raise;
      say("MULTIBALL");
      return HoleResult::StartMultiball;
    case RampLeft:
      award(points::kRamp);
      say("RUNWAY");
      break;
    case RampRight:
      award(points::kRamp);
      say("SKYWAY");
      break;
  }
  return HoleResult::Eject;
}

void Rules::onEject(std::size_t index) noexcept {
  if (index >= table::kHoles.size() || table::kHoles[index].type != table::HoleType::Hangar) {
    return;
  }
  // A ball came back out of the Hangar with no lock lit: shut it again.
  if (!lockLit_) {
    doorRaiseSteps_ = stepsFor(kDoorRaiseSeconds);
  }
}

void Rules::onSensor(std::size_t index, int direction, float speed) noexcept {
  // Nothing scores on a line yet: circuits and the Turbine's shot are for
  // the table's own rules. The spinner's turns arrive through onSpin().
  static_cast<void>(index);
  static_cast<void>(direction);
  static_cast<void>(speed);
}

void Rules::onSpin() noexcept { award(points::kSpin); }

void Rules::onKickback() noexcept {
  kickbackLit_ = false;
  award(points::kKickback);
  say("KICKBACK");
}

void Rules::onBallLocked(std::size_t bay, bool served) noexcept {
  static_cast<void>(bay);
  if (served && !gameOver_) {
    // The player has a new ball to launch.
    ballInPlay_ = false;
  }
}

void Rules::onLockReleased(std::size_t bay) noexcept { static_cast<void>(bay); }

void Rules::onDoorBank(bool up) noexcept {
  if (up) {
    doorMarked_.fill(false);
  }
}

bool Rules::onBallLost(const DrainInfo& info) noexcept {
  if (gameOver_ || tilted_ || !info.afterEject) {
    return false;
  }
  say("BALL SAVED");
  cue(Sound::BallSaved);
  return true;
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

Rules::DrainResult Rules::onDrain(const DrainInfo& info) noexcept {
  ballInPlay_ = false;
  if (gameOver_) {
    return DrainResult::GameOver;
  }
  // A tilt ends with the ball, whatever happens next.
  const bool wasTilted = tilted_;
  tilted_ = false;
  tiltHeat_ = 0.0;
  if (info.afterEject && !wasTilted) {
    // The short save after an eject. It does not use up the ball save.
    say("BALL SAVED");
    cue(Sound::BallSaved);
    return DrainResult::BallSaved;
  }
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
    // The bays are emptied (Game does that) and the lock is off.
    lockLit_ = false;
    locksHeld_ = 0;
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
  if (doorRaiseSteps_ > 0 && --doorRaiseSteps_ == 0) {
    doorCommand_ = DoorCommand::Raise;
  }
  if (messageSteps_ > 0 && --messageSteps_ == 0 && !gameOver_ && !tilted_) {
    message_ = {};
  }
}

}  // namespace pinball
