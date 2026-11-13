#include "core/Rules.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace pinball {

namespace {

constexpr std::size_t kFirstReEntryRollover = 0;
constexpr std::size_t kReEntryLaneCount = 3;

// The rules index the table by position. These fail to compile if the table
// is reordered in a way the rules would get wrong.

// The re-entry rollovers are the first three, left to right, and there are
// no others: lane light i belongs to rollover kFirstReEntryRollover + i.
constexpr bool reEntryRolloversLeadTheTable() noexcept {
  for (std::size_t i = 0; i < table::kRollovers.size(); ++i) {
    const bool inRun = i >= kFirstReEntryRollover && i < kFirstReEntryRollover + kReEntryLaneCount;
    if ((table::kRollovers[i].group == table::RolloverGroup::ReEntry) != inRun) {
      return false;
    }
    if (inRun && i > kFirstReEntryRollover &&
        table::kRollovers[i].zone.center.x <= table::kRollovers[i - 1].zone.center.x) {
      return false;
    }
  }
  return table::kRollovers.size() >= kFirstReEntryRollover + kReEntryLaneCount;
}
static_assert(reEntryRolloversLeadTheTable());

// The mission bank has one target per choice, listed top to bottom: a
// target's place in its bank is the mission it selects.
constexpr bool missionTargetsRunTopToBottom() noexcept {
  std::size_t count = 0;
  float previousY = 0.0F;
  for (const table::Target& target : table::kTargets) {
    if (target.bank != table::TargetBank::Mission) {
      continue;
    }
    const float y = (target.shape.a.y + target.shape.b.y) * 0.5F;
    if (count > 0 && y <= previousY) {
      return false;
    }
    previousY = y;
    ++count;
  }
  return count == kSpecialMission;
}
static_assert(missionTargetsRunTopToBottom());

}  // namespace

Rules::Rules(double stepSeconds) noexcept
    : stepSeconds_(std::isfinite(stepSeconds) && stepSeconds > 0.0 ? stepSeconds : 1.0 / 120.0),
      missions_(stepSeconds_) {
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
  sounds_ = 0;
  ballInPlay_ = false;
  missionMessageThisStep_ = false;
  abortUnannounced_ = false;
  resetBall();
  say("PULL THE PLUNGER TO LAUNCH");
}

void Rules::resetBall() noexcept {
  // What a new ball starts without.
  bonus_ = 0;
  multiplierLevel_ = 0;
  ballSaveSteps_ = 0;
  ballSaveUsed_ = false;
  rampReflexSteps_ = 0;
  hyperspaceReflexSteps_ = 0;
  tilted_ = false;
  tiltHeat_ = 0.0;
}

void Rules::sayMission(std::string_view text) noexcept {
  missionMessageThisStep_ = false;
  say(text);
  missionMessageThisStep_ = true;
}

void Rules::say(std::string_view text) noexcept {
  // While tilted the display says so and nothing else.
  if (missionMessageThisStep_ || (tilted_ && text != "TILT")) {
    return;
  }
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

bool Rules::awardExtraBall() noexcept {
  if (extraBalls_ >= kMaxExtraBalls) {
    return false;  // Already holding the most allowed: nothing to announce.
  }
  ++extraBalls_;
  say("EXTRA BALL");
  cue(Sound::ExtraBall);
  return true;
}

void Rules::clearBank(table::TargetBank bank) noexcept {
  for (std::size_t i = 0; i < table::kTargets.size(); ++i) {
    if (table::kTargets[i].bank == bank) {
      targetLit_[i] = false;
    }
  }
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
    sayMission(outcome.promoted ? "MISSION COMPLETE  PROMOTED" : "MISSION COMPLETE");
    cue(Sound::MissionComplete);
    if (outcome.promoted) {
      cue(Sound::Promotion);
    }
  } else if (outcome.aborted) {
    // A tilted display says TILT and nothing else, so the news waits for
    // the message that ends the tilt.
    abortUnannounced_ = tilted_;
    sayMission("OUT OF FUEL  MISSION ABORTED");
    cue(Sound::TiltWarning);
  } else if (outcome.stageCompleted) {
    sayMission("STAGE COMPLETE");
    cue(Sound::BankComplete);
  } else if (outcome.accepted) {
    sayMission("MISSION ACCEPTED");
    cue(Sound::MissionAccept);
  } else if (outcome.selected) {
    sayMission("MISSION SELECTED  SHOOT THE RAMP");
    cue(Sound::MissionSelect);
  } else if (outcome.refueled) {
    sayMission("REFUELED");
    cue(Sound::MissionSelect);
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

  const table::TargetBank bank = table::kTargets[index].bank;
  const bool missionWasActive = missions_.phase() == MissionControl::Phase::Active;
  // The mission bank is dark during a mission: its targets still score, but
  // lights collected then would hand over the next special mission early.
  if (bank != table::TargetBank::Mission || !missionWasActive) {
    targetLit_[index] = true;
  }
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
  if (bank == table::TargetBank::Mission && !missionWasActive) {
    // Each mission target picks a mission; lighting all three picks the
    // special. Not while a mission is active, and not with the hit that just
    // completed one: that hit belongs to the finished mission, and selecting
    // with it would replace the completion message.
    handleMission(missions_.onMissionTarget(positionInBank, bankComplete));
  }

  // The mission bank pays nothing for being lit. Its three lights are the
  // special mission's selection and stay on until the ramp accepts it, so
  // that one more hit cannot swap the special for an ordinary mission.
  if (!bankComplete || bank == table::TargetBank::Mission) {
    return;
  }
  // The whole bank is lit: pay it out and reset it.
  clearBank(bank);
  completeBank(bank);
}

void Rules::completeBank(table::TargetBank bank) noexcept {
  cue(Sound::BankComplete);
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
        say("FIELD MULTIPLIER RAISED");
      } else {
        say("FIELD MULTIPLIER AT MAXIMUM");
      }
      break;
    case table::TargetBank::Mission:
      // Selection is handled where the target is hit; never paid out here.
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
        cue(Sound::BankComplete);
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
        if (!awardExtraBall()) {
          // No room for another ball: the rung pays the top bonus instead of
          // nothing at all.
          award(points::kHyperspace[2]);
          say("HYPERSPACE BONUS");
        }
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
    case table::HoleType::Ramp: {
      award(points::kRamp);
      if (rampReflexSteps_ > 0) {
        award(points::kReflexShot);
        say("REFLEX SHOT");
      } else {
        say("LAUNCH RAMP");
      }
      rampReflexSteps_ = stepsFor(kReflexSeconds);
      // Accepts a selected mission, or refuels and advances an active one.
      const MissionControl::Outcome outcome = missions_.onRamp();
      if (outcome.accepted) {
        // The selection is spent: the next special needs all three again.
        clearBank(table::TargetBank::Mission);
      }
      handleMission(outcome);
      break;
    }
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
  // Its own event, like a drain. A tilt cannot outlive the ball's time in
  // play: on the plunger it would leave the controls dead for good.
  missionMessageThisStep_ = false;
  tilted_ = false;
  tiltHeat_ = 0.0;
  say(std::exchange(abortUnannounced_, false) ? "RE-DEPLOY  MISSION ABORTED" : "RE-DEPLOY");
}

Rules::DrainResult Rules::onDrain() noexcept {
  ballInPlay_ = false;
  if (gameOver_) {
    return DrainResult::GameOver;
  }
  // The drain is its own event: what it says is not a lesser message of a
  // mission announcement made earlier in the step.
  missionMessageThisStep_ = false;
  // A tilt ends with the ball, whatever happens next.
  const bool wasTilted = tilted_;
  tilted_ = false;
  tiltHeat_ = 0.0;
  // A mission that ran dry behind the tilt message is reported with the
  // drain, or the player would only find it gone.
  const bool abortNews = std::exchange(abortUnannounced_, false);
  if (ballSaveSteps_ > 0 && !wasTilted) {
    ballSaveSteps_ = 0;
    ballSaveUsed_ = true;
    say("RE-DEPLOY");
    cue(Sound::BallSaved);
    return DrainResult::BallSaved;
  }

  // End-of-ball bonus, multiplied once here. A tilt forfeits it.
  if (!wasTilted) {
    addScore(bonus_ * static_cast<std::uint64_t>(fieldMultiplier()));
  }
  resetBall();

  // The game-over phrase replaces the drain phrase; the two would clash.
  if (extraBalls_ > 0) {
    --extraBalls_;
    cue(Sound::Drain);
    say(abortNews ? "SHOOT AGAIN  MISSION ABORTED" : "SHOOT AGAIN");
    return DrainResult::ShootAgain;
  }
  if (ball_ >= kBallsPerGame) {
    gameOver_ = true;
    // The mission lights go out with the game: nothing is left selected or
    // running on a dead table.
    clearBank(table::TargetBank::Mission);
    missions_.standDown();
    say(kGameOverMessage);
    cue(Sound::GameOver);
    return DrainResult::GameOver;
  }
  ++ball_;
  cue(Sound::Drain);
  say(abortNews ? "NEXT BALL  MISSION ABORTED" : "NEXT BALL");
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
    missionMessageThisStep_ = false;  // Nothing outranks the tilt.
    say("TILT");
    cue(Sound::Tilt);
    return false;
  }
  if (tiltHeat_ >= kTiltLimit - 1.0) {
    say("DANGER  TILT WARNING");
    cue(Sound::TiltWarning);
  }
  return true;
}

void Rules::onFlipper(bool left) noexcept {
  if (gameOver_ || tilted_) {
    return;
  }
  if (left) {
    std::ranges::rotate(laneLit_, laneLit_.begin() + 1);
  } else {
    std::ranges::rotate(laneLit_, laneLit_.end() - 1);
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
  // Fuel burns only while a ball is in play, not while one waits on the plunger.
  if (!gameOver_ && ballInPlay_) {
    handleMission(missions_.tick());
  }
  missionMessageThisStep_ = false;
  if (messageSteps_ > 0 && --messageSteps_ == 0 && !gameOver_ && !tilted_) {
    message_ = {};
  }
}

}  // namespace pinball
