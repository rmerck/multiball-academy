#include "core/Rules.hpp"

#include <algorithm>
#include <cmath>

namespace pinball {

namespace {

constexpr std::size_t kFirstReEntryRollover = 0;

}  // namespace

Rules::Rules(double stepSeconds) noexcept
    : stepSeconds_(std::isfinite(stepSeconds) && stepSeconds > 0.0 ? stepSeconds : 1.0 / 120.0),
      missions_(stepSeconds_) {
  startGame();
}

std::uint32_t Rules::stepsFor(double seconds) const noexcept {
  return static_cast<std::uint32_t>(std::lround(seconds / stepSeconds_));
}

void Rules::startGame() noexcept {
  score_ = 0;
  ball_ = 1;
  extraBalls_ = 0;
  gameOver_ = false;
  attackLevel_ = 0;
  boosterLevel_ = 0;
  medalLevel_ = 0;
  hyperspaceLevel_ = 0;
  jackpotLit_ = false;
  bonusLaneLit_ = false;
  targetLit_.fill(false);
  laneLit_.fill(false);
  missions_.reset();
  resetBall();
  say("PULL THE PLUNGER TO LAUNCH");
}

void Rules::resetBall() noexcept {
  // What a new ball starts without.
  bonus_ = 0;
  multiplierLevel_ = 0;
  ballSaveSteps_ = 0;
  rampReflexSteps_ = 0;
  hyperspaceReflexSteps_ = 0;
  tilted_ = false;
  tiltHeat_ = 0.0;
}

void Rules::say(std::string_view text) noexcept {
  message_ = text;
  messageSteps_ = stepsFor(kMessageSeconds);
}

void Rules::addScore(std::uint64_t amount) noexcept {
  // Saturating: the score can never wrap.
  score_ = amount >= points::kMaxScore - score_ ? points::kMaxScore : score_ + amount;
}

void Rules::award(std::uint64_t basePoints) noexcept {
  addScore(basePoints * static_cast<std::uint64_t>(fieldMultiplier()));
}

void Rules::addBonus() noexcept {
  bonus_ = std::min<std::uint64_t>(bonus_ + points::kBonusPerHit, 1'000'000);
}

void Rules::awardExtraBall() noexcept {
  if (extraBalls_ < kMaxExtraBalls) {
    ++extraBalls_;
  }
  say("EXTRA BALL");
}

void Rules::report(GoalSet goals) noexcept { handleMission(missions_.onEvent(goals)); }

void Rules::handleMission(const MissionControl::Outcome& outcome) noexcept {
  if (outcome.completed) {
    // Mission awards are paid as stated, not multiplied.
    addScore(outcome.award);
    if (jackpotLit_) {
      addScore(points::kJackpot);
      jackpotLit_ = false;
    }
    say(outcome.promoted ? "MISSION COMPLETE  PROMOTED" : "MISSION COMPLETE");
  } else if (outcome.aborted) {
    say("OUT OF FUEL  MISSION ABORTED");
  } else if (outcome.stageCompleted) {
    say("STAGE COMPLETE");
  } else if (outcome.accepted) {
    say("MISSION ACCEPTED");
  } else if (outcome.selected) {
    say("MISSION SELECTED  SHOOT THE RAMP");
  } else if (outcome.refueled) {
    say("REFUELED");
  }
}

void Rules::onBumper(std::size_t index) noexcept {
  if (gameOver_ || tilted_ || index >= table::kBumpers.size()) {
    return;
  }
  switch (table::kBumpers[index].group) {
    case table::BumperGroup::Attack:
      award(points::kAttackBumper[attackLevel_]);
      report(goalBit(Goal::AttackBumpers) | goalBit(Goal::AnyBumper));
      break;
    case table::BumperGroup::Engine:
      award(points::kEngineBumper);
      report(goalBit(Goal::EngineBumpers) | goalBit(Goal::AnyBumper));
      break;
    case table::BumperGroup::Satellite:
      award(points::kSatelliteBumper);
      report(goalBit(Goal::SatelliteBumper) | goalBit(Goal::AnyBumper));
      break;
  }
  addBonus();
}

void Rules::onSlingshot(std::size_t index) noexcept {
  if (gameOver_ || tilted_ || index >= table::kSlingshots.size()) {
    return;
  }
  award(points::kSlingshot);
  report(goalBit(Goal::Slingshots));
}

void Rules::onTarget(std::size_t index) noexcept {
  if (gameOver_ || tilted_ || index >= table::kTargets.size()) {
    return;
  }
  award(points::kTarget);
  addBonus();
  targetLit_[index] = true;

  const table::TargetBank bank = table::kTargets[index].bank;
  bool bankComplete = true;
  std::size_t positionInBank = 0;
  for (std::size_t i = 0; i < table::kTargets.size(); ++i) {
    if (table::kTargets[i].bank != bank) {
      continue;
    }
    bankComplete = bankComplete && targetLit_[i];
    if (i < index) {
      ++positionInBank;
    }
  }

  // Mission progress first, so the hit that completes a bank still counts.
  GoalSet goals = goalBit(Goal::AnyTarget);
  if (bank == table::TargetBank::Booster) {
    goals |= goalBit(Goal::BoosterTargets);
  } else if (bank == table::TargetBank::Medal) {
    goals |= goalBit(Goal::MedalTargets);
  } else if (bank == table::TargetBank::Multiplier) {
    goals |= goalBit(Goal::MultiplierTargets);
  }
  report(goals);
  if (bank == table::TargetBank::Mission) {
    // Each mission target picks a mission; lighting all three picks the special.
    handleMission(missions_.onMissionTarget(positionInBank, bankComplete));
  }

  if (!bankComplete) {
    return;
  }
  // The whole bank is lit: pay it out and reset it.
  for (std::size_t i = 0; i < table::kTargets.size(); ++i) {
    if (table::kTargets[i].bank == bank) {
      targetLit_[i] = false;
    }
  }
  completeBank(bank);
}

void Rules::completeBank(table::TargetBank bank) noexcept {
  switch (bank) {
    case table::TargetBank::Booster:
      award(points::kBoosterBank);
      boosterLevel_ = std::min(boosterLevel_ + 1, 4);
      if (boosterLevel_ == 1) {
        say("BOOSTER ONLINE");
      } else if (boosterLevel_ == 2) {
        jackpotLit_ = true;
        say("JACKPOT ACTIVATED");
      } else if (boosterLevel_ == 3) {
        bonusLaneLit_ = true;
        say("BONUS LANES LIT");
      } else {
        awardExtraBall();
        boosterLevel_ = 0;
      }
      break;
    case table::TargetBank::Medal:
      award(points::kMedalBank[std::min(medalLevel_, 2)]);
      medalLevel_ = std::min(medalLevel_ + 1, 3);
      if (medalLevel_ == 1) {
        say("LEVEL ONE COMMENDATION");
      } else if (medalLevel_ == 2) {
        say("LEVEL TWO COMMENDATION");
      } else {
        awardExtraBall();
        medalLevel_ = 0;
      }
      break;
    case table::TargetBank::Multiplier:
      award(points::kMultiplierBank);
      if (multiplierLevel_ + 1 < kFieldMultipliers.size()) {
        ++multiplierLevel_;
      }
      say("FIELD MULTIPLIER RAISED");
      break;
    case table::TargetBank::Mission:
      // Selection is handled where the target is hit.
      break;
  }
}

void Rules::onRollover(std::size_t index) noexcept {
  if (gameOver_ || tilted_ || index >= table::kRollovers.size()) {
    return;
  }
  switch (table::kRollovers[index].group) {
    case table::RolloverGroup::ReEntry: {
      award(points::kReEntryLane);
      report(goalBit(Goal::ReEntryLanes));
      const std::size_t lane = index - kFirstReEntryRollover;
      if (lane < laneLit_.size()) {
        laneLit_[lane] = true;
      }
      if (laneLit_[0] && laneLit_[1] && laneLit_[2]) {
        laneLit_.fill(false);
        award(points::kReEntryComplete);
        if (attackLevel_ < 3) {
          ++attackLevel_;
          say("ATTACK BUMPERS UPGRADED");
        }
      }
      break;
    }
    case table::RolloverGroup::Inlane:
      award(points::kInlane);
      report(goalBit(Goal::ReturnLanes));
      break;
    case table::RolloverGroup::Outlane:
      report(goalBit(Goal::ReturnLanes));
      if (bonusLaneLit_) {
        award(points::kBonusLane);
        bonusLaneLit_ = false;
        say("BONUS LANE");
      }
      break;
  }
}

void Rules::onHole(std::size_t index) noexcept {
  if (gameOver_ || tilted_ || index >= table::kHoles.size()) {
    return;
  }
  switch (table::kHoles[index].type) {
    case table::HoleType::Wormhole:
      award(points::kWormhole);
      say("WORMHOLE");
      report(goalBit(Goal::Wormholes));
      break;
    case table::HoleType::BlackHole:
      award(points::kBlackHole);
      say("BLACK HOLE");
      report(goalBit(Goal::BlackHole));
      break;
    case table::HoleType::Hyperspace: {
      const bool reflex = hyperspaceReflexSteps_ > 0;
      hyperspaceReflexSteps_ = stepsFor(kReflexSeconds);
      // Each shot climbs one rung: bonus, jackpot (if lit) or bonus, bonus,
      // extra ball, then back to the bottom.
      if (hyperspaceLevel_ == 3) {
        awardExtraBall();
        hyperspaceLevel_ = 0;
      } else {
        if (hyperspaceLevel_ == 1 && jackpotLit_) {
          award(points::kJackpot);
          jackpotLit_ = false;
          say("JACKPOT");
        } else {
          award(points::kHyperspace[hyperspaceLevel_]);
          say("HYPERSPACE BONUS");
        }
        ++hyperspaceLevel_;
      }
      if (reflex) {
        award(points::kReflexShot);
        say("REFLEX SHOT");
      }
      report(goalBit(Goal::Hyperspace));
      break;
    }
    case table::HoleType::Ramp:
      award(points::kRamp);
      if (rampReflexSteps_ > 0) {
        award(points::kReflexShot);
        say("REFLEX SHOT");
      } else {
        say("LAUNCH RAMP");
      }
      rampReflexSteps_ = stepsFor(kReflexSeconds);
      // Accepts a selected mission, or refuels and advances an active one.
      handleMission(missions_.onRamp());
      break;
  }
}

void Rules::onBallInPlay() noexcept {
  if (gameOver_) {
    return;
  }
  ballSaveSteps_ = stepsFor(kBallSaveSeconds);
}

Rules::DrainResult Rules::onDrain() noexcept {
  if (gameOver_) {
    return DrainResult::GameOver;
  }
  if (ballSaveSteps_ > 0) {
    ballSaveSteps_ = 0;
    say("RE-DEPLOY");
    return DrainResult::BallSaved;
  }

  // End-of-ball bonus, multiplied once here. A tilt forfeits it.
  if (!tilted_) {
    addScore(bonus_ * static_cast<std::uint64_t>(fieldMultiplier()));
  }
  resetBall();

  if (extraBalls_ > 0) {
    --extraBalls_;
    say("SHOOT AGAIN");
    return DrainResult::ShootAgain;
  }
  if (ball_ >= kBallsPerGame) {
    gameOver_ = true;
    say("GAME OVER   PRESS F2");
    return DrainResult::GameOver;
  }
  ++ball_;
  say("NEXT BALL");
  return DrainResult::NextBall;
}

bool Rules::onNudge() noexcept {
  if (gameOver_ || tilted_) {
    return false;
  }
  tiltHeat_ += 1.0;
  if (tiltHeat_ >= kTiltLimit) {
    tilted_ = true;
    tiltHeat_ = kTiltLimit;
    ballSaveSteps_ = 0;  // A tilted ball is not returned.
    say("TILT");
    return false;
  }
  if (tiltHeat_ >= kTiltLimit - 1.0) {
    say("DANGER  TILT WARNING");
  }
  return true;
}

void Rules::onFlipper(bool left) noexcept {
  if (gameOver_ || tilted_) {
    return;
  }
  if (left) {
    std::rotate(laneLit_.begin(), laneLit_.begin() + 1, laneLit_.end());
  } else {
    std::rotate(laneLit_.rbegin(), laneLit_.rbegin() + 1, laneLit_.rend());
  }
}

void Rules::tick() noexcept {
  const auto countDown = [](std::uint32_t& steps) {
    if (steps > 0) {
      --steps;
    }
  };
  countDown(ballSaveSteps_);
  countDown(rampReflexSteps_);
  countDown(hyperspaceReflexSteps_);
  if (!tilted_) {
    tiltHeat_ = std::max(0.0, tiltHeat_ - stepSeconds_ / kTiltCoolSeconds);
  }
  if (!gameOver_) {
    handleMission(missions_.tick());
  }
  if (messageSteps_ > 0 && --messageSteps_ == 0 && !gameOver_ && !tilted_) {
    message_ = {};
  }
}

}  // namespace pinball
