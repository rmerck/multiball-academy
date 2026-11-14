#include "core/Rules.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

namespace pinball {

namespace {

// The one message a tilted table shows.
constexpr display::Message kTiltMessage{"TILT"};

constexpr bool isRamp(Shot shot) noexcept { return shot == Shot::Runway || shot == Shot::Skyway; }
constexpr bool isCircuit(Shot shot) noexcept {
  return shot == Shot::CircuitLeft || shot == Shot::CircuitRight;
}

constexpr Feature courseFeature(std::size_t course) noexcept {
  return static_cast<Feature>(std::to_underlying(Feature::Course1) + course);
}

constexpr std::array<display::Message, 4> kComboMessages{"COMBO 2X", "COMBO 3X", "COMBO 4X",
                                                         "COMBO 5X"};
constexpr std::array<display::Message, 6> kExamMessages{
    "FINAL EXAM  1 TO GO", "FINAL EXAM  2 TO GO", "FINAL EXAM  3 TO GO",
    "FINAL EXAM  4 TO GO", "FINAL EXAM  5 TO GO", "FINAL EXAM  6 TO GO"};
constexpr std::array<display::NumberLabel, 5> kBonusTotalLabels{
    display::NumberLabel("BONUS 1X", 2), display::NumberLabel("BONUS 2X", 2),
    display::NumberLabel("BONUS 3X", 2), display::NumberLabel("BONUS 4X", 2),
    display::NumberLabel("BONUS 5X", 2)};
static_assert(kBonusTotalLabels.size() == Rules::kMaxBonusMultiplier);

constexpr std::array<std::string_view, kFeatureCount> kFeatureNames{
    "slingshots",    "bumpers",      "spinner",     "turbo targets", "roll call",
    "return lanes",  "ramps",        "circuits",    "bay doors",     "hangar",
    "briefing room", "skill shot",   "combos",      "course 101",    "course 103",
    "course 201",    "course 202",   "course 301",  "course 302",    "jackpots",
    "super jackpot", "turbo bonus",  "extra credit", "extra ball",   "graduation",
    "bonus"};
static_assert(std::to_underlying(Feature::Bonus) + 1 == kFeatureCount);

// Extra credit: what is given, by weight out of 100.
enum class Credit : std::uint8_t { Points25, Points50, Kickback, BonusX, Saver, Turbo, Overdrive };
struct CreditWeight {
  Credit credit;
  unsigned weight;
};
constexpr std::array<CreditWeight, 7> kCredits{{{Credit::Points25, 30},
                                                {Credit::Points50, 20},
                                                {Credit::Kickback, 15},
                                                {Credit::BonusX, 10},
                                                {Credit::Saver, 10},
                                                {Credit::Turbo, 10},
                                                {Credit::Overdrive, 5}}};
static_assert([] {
  unsigned total = 0;
  for (const CreditWeight& entry : kCredits) {
    total += entry.weight;
  }
  return total;
}() == 100);

}  // namespace

std::string_view featureName(Feature feature) noexcept {
  const std::size_t index = std::to_underlying(feature);
  return index < kFeatureNames.size() ? kFeatureNames[index] : std::string_view{};
}

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

std::uint64_t Rules::nextRandom() noexcept {
  random_ ^= random_ << 13U;
  random_ ^= random_ >> 7U;
  random_ ^= random_ << 17U;
  return random_;
}

// --- A game and its balls ------------------------------------------------------

void Rules::clearModes() noexcept {
  course_ = kNoCourse;
  courseSteps_ = 0;
  courseElapsedSteps_ = 0;
  courseProgress_ = 0;
  courseSecondPart_ = false;
  navSteps_ = 0;
  turboMultiball_ = false;
  academyMultiball_ = false;
  superLit_ = false;
  jackpotIndex_ = 0;
  jackpotsTowardSuper_ = 0;
  gradStage_ = GradStage::None;
  commencementThisTime_ = false;
  overdrive_ = 1;
  overdriveSteps_ = 0;
  overdriveLit_ = false;
  comboShot_ = kNoShot;
  comboChain_ = 0;
  comboSteps_ = 0;
  superSkillSteps_ = 0;
  skillOffered_ = false;
  circuits_ = {};
  saverArmable_ = false;
  saverSteps_ = 0;
  graceSteps_ = 0;
  ballRequests_ = 0;
  requestsInFlight_ = 0;
  lockRelease_ = false;
  releasesPending_ = 0;
  locksHeld_ = 0;
  hangarHolding_ = 0;
  doorsOpen_ = false;
  kickbackArmSteps_ = 0;
}

void Rules::startGame() noexcept {
  score_ = 0;
  featureScore_.fill(0);
  ball_ = 1;
  gameOver_ = false;
  tilted_ = false;
  tiltHeat_ = 0.0;
  sounds_ = 0;
  rank_ = 0;
  status_ = {};
  clearModes();
  extraBallsLit_ = 0;
  extraBallsHeld_ = 0;
  extraBallsAwarded_ = 0;
  skillLane_ = 1;
  rollCallLit_.fill(false);
  wingLit_.fill(false);
  turboTargetLit_.fill(false);
  kickbackLit_ = true;
  extraCreditLit_ = false;
  turboMeter_ = 0;
  turboGoal_ = tuning::kTurboMeterFirst;
  // The bank stands and nothing is locked.
  doorMarked_.fill(false);
  firstBank_ = true;
  doorsDown_ = false;
  doorCommand_ = DoorCommand::Raise;
  doorRaiseSteps_ = 0;
  turboMultiballs_ = 0;
  academyMultiballs_ = 0;
  jackpotsCollected_ = 0;
  fiveComboMade_ = false;
  coursePassed_.fill(false);
  courseBlink_ = 0;
  courseLit_ = true;
  rampsNeeded_ = 0;
  coursesEnded_ = 0;
  coursesStarted_ = 0;
  coursePasses_ = 0;
  gradLit_ = false;
  gradRampsNeeded_ = 0;
  examMade_.fill(false);
  graduations_ = 0;
  bonusTally_ = {};
  waitingCount_ = 0;
  beginBall();
  say(display::Message("LAUNCH THE BALL"));
}

void Rules::offerSkillShot() noexcept {
  ballInPlay_ = false;
  skillOffered_ = true;
  superSkillSteps_ = 0;
}

void Rules::beginBall() noexcept {
  tilted_ = false;
  tiltHeat_ = 0.0;
  saverArmable_ = true;
  saverSteps_ = 0;
  graceSteps_ = 0;
  briefingVisited_ = false;
  combosThisBall_ = 0;
  laneSetsThisBall_ = 0;
  switchesThisBall_ = 0;
  bonusMultiplier_ = 1;
  offerSkillShot();
}

void Rules::setTable(const TableStatus& status) noexcept {
  status_.live = std::clamp(status.live, 0, static_cast<int>(table::kMaxBalls));
  status_.free = std::clamp(status.free, 0, status_.live);
  status_.queued = std::clamp(status.queued, 0, static_cast<int>(table::kMaxBalls));
  status_.locked = std::clamp(status.locked, 0, kLocksNeeded);
  // What Game took last step is in these numbers now.
  requestsInFlight_ = 0;
  // The bays are the truth about the locks.
  releasesPending_ = std::min(releasesPending_, status_.locked);
  locksHeld_ = status_.locked - releasesPending_;
}

int Rules::ballsInGame() const noexcept {
  return status_.live + status_.queued + status_.locked + ballRequests_ + requestsInFlight_;
}

int Rules::ballsLive() const noexcept {
  return status_.live + status_.queued + ballRequests_ + requestsInFlight_ + releasesPending_;
}

Rules::DoorCommand Rules::takeDoorCommand() noexcept {
  const DoorCommand command = doorCommand_;
  doorCommand_ = DoorCommand::None;
  return command;
}

int Rules::takeBallRequests() noexcept {
  const int requests = ballRequests_;
  ballRequests_ = 0;
  requestsInFlight_ += requests;
  return requests;
}

bool Rules::takeLockRelease() noexcept {
  const bool release = lockRelease_;
  lockRelease_ = false;
  return release;
}

// --- The display ---------------------------------------------------------------

void Rules::say(const display::Text& text) noexcept {
  // While tilted the display says so and nothing else.
  if (tilted_ && text.view() != kTiltMessage.text()) {
    return;
  }
  message_ = text;
  messageSteps_ = stepsFor(kMessageSeconds);
  messageFresh_ = true;
  waitingCount_ = 0;
}

void Rules::sayAlso(const display::Text& text) noexcept {
  if (messageFresh_) {
    sayThen(text);
  } else {
    say(text);
  }
}

void Rules::sayThen(const display::Text& text) noexcept {
  if (tilted_) {
    return;
  }
  if (message_.empty() || messageSteps_ == 0) {
    say(text);
    return;
  }
  if (waitingCount_ < waiting_.size()) {
    waiting_[waitingCount_++] = text;
    // What is showing makes way sooner.
    messageSteps_ = std::min(messageSteps_, stepsFor(kQueuedMessageSeconds));
  }
}

void Rules::award(std::uint64_t points, Feature feature, bool timesOverdrive) noexcept {
  // A dead table scores nothing.
  if (dead()) {
    return;
  }
  if (timesOverdrive) {
    points *= static_cast<std::uint64_t>(overdrive_);
  }
  // Saturating: neither the score nor a share of it can ever wrap.
  points = std::min(points, points::kMaxScore);
  const std::uint64_t before = score_;
  score_ = points >= points::kMaxScore - score_ ? points::kMaxScore : score_ + points;
  featureScore_[std::to_underlying(feature)] += score_ - before;
}

void Rules::noteSwitch() noexcept {
  switchesThisBall_ = std::min(switchesThisBall_ + 1, points::kBonusSwitchCap);
  skillOffered_ = false;
}

// --- Switches ------------------------------------------------------------------

void Rules::onFlipper(bool right) noexcept {
  if (dead()) {
    return;
  }
  rotateLanes(right);
}

void Rules::rotateLanes(bool right) noexcept {
  if (right) {
    std::ranges::rotate(rollCallLit_, rollCallLit_.end() - 1);
    std::ranges::rotate(wingLit_, wingLit_.end() - 1);
    skillLane_ = (skillLane_ + 1) % rollCallLit_.size();
  } else {
    std::ranges::rotate(rollCallLit_, rollCallLit_.begin() + 1);
    std::ranges::rotate(wingLit_, wingLit_.begin() + 1);
    skillLane_ = (skillLane_ + rollCallLit_.size() - 1) % rollCallLit_.size();
  }
}

void Rules::onBumper(std::size_t index) noexcept {
  if (index >= table::kBumpers.size() || dead()) {
    return;
  }
  noteSwitch();
  award(points::kBumper, Feature::Bumpers);
  if (course_ == std::to_underlying(CourseId::BumperBasics)) {
    courseStep(CourseId::BumperBasics);
  } else if (!courseRunning() && !graduationRunning() && !allCoursesPassed()) {
    // Choosing: each hit moves the offer to the next course not yet passed.
    courseBlink_ = nextUnpassed(courseBlink_ + 1);
  }
  addTurbo(tuning::kTurboPerBumper);
}

void Rules::onSlingshot(std::size_t index) noexcept {
  if (index >= table::kSlingshots.size() || dead()) {
    return;
  }
  noteSwitch();
  award(points::kSlingshot, Feature::Slingshots);
}

void Rules::onTarget(std::size_t index) noexcept {
  if (index >= table::kTargets.size() || dead()) {
    return;
  }
  noteSwitch();
  award(points::kTarget, Feature::TurboTargets);
  turboTargetLit_[index] = true;
  if (std::ranges::all_of(turboTargetLit_, [](bool lit) { return lit; })) {
    award(points::kBothTargets, Feature::TurboTargets);
    turboTargetLit_.fill(false);
    extraCreditLit_ = true;
    say(display::Message("EXTRA CREDIT IS LIT"));
    cue(Sound::BankComplete);
  }
  addTurbo(tuning::kTurboPerTarget);
}

void Rules::markAllDoors() noexcept { doorMarked_.fill(true); }

void Rules::onDropTarget(std::size_t index) noexcept {
  if (index >= table::kDropTargets.size() || dead()) {
    return;
  }
  noteSwitch();
  std::size_t door = index;
  if (doorMarked_[door]) {
    const auto unmarked = std::ranges::find(doorMarked_, false);
    if (!tuning::kFirstBankAnyThreeHits || !firstBank_ || unmarked == doorMarked_.end()) {
      award(points::kBayDoorAgain, Feature::BayDoors);
      return;
    }
    door = static_cast<std::size_t>(unmarked - doorMarked_.begin());
  }
  award(points::kBayDoor, Feature::BayDoors);
  doorMarked_[door] = true;
  const bool safety = course_ == std::to_underlying(CourseId::HangarSafety) && !courseSecondPart_;
  if (safety) {
    award(kCourses[course_].shotAward, courseFeature(course_));
    ++courseProgress_;
  }
  if (!std::ranges::all_of(doorMarked_, [](bool marked) { return marked; })) {
    return;
  }
  // All three: the bank drops as one.
  award(points::kBayDoorsOpen, Feature::BayDoors);
  doorsOpen_ = true;
  firstBank_ = false;
  if (safety) {
    courseSecondPart_ = true;
  }
  cue(Sound::BankComplete);
  say(lockLit() ? display::Message("LOCK IS LIT") : display::Message("BAY DOORS OPEN"));
}

void Rules::onRollover(std::size_t index, std::size_t ball) noexcept {
  if (index >= table::kRollovers.size() || dead()) {
    return;
  }
  const table::Rollover& rollover = table::kRollovers[index];
  const bool skill = skillOffered_;
  noteSwitch();
  const auto ballNumber = static_cast<std::uint64_t>(ball_);
  if (rollover.group == table::RolloverGroup::TopLane) {
    const std::size_t lane = std::to_underlying(rollover.id);
    if (lane >= rollCallLit_.size()) {
      return;
    }
    if (skill) {
      if (lane == skillLane_) {
        award(points::kSkillShot * ballNumber, Feature::SkillShot, false);
        say(display::Message("SKILL SHOT"));
        cue(Sound::MissionSelect);
      } else {
        award(points::kSkillShotOtherLane * ballNumber, Feature::SkillShot, false);
      }
    }
    award(points::kRollCallLane, Feature::RollCall);
    // A ball that went up a circuit's mouth and fell off the arch into a
    // lane has still made the circuit.
    if (ball < circuits_.size() && circuits_[ball].steps > 0) {
      const Shot made = circuits_[ball].side == 0 ? Shot::CircuitLeft : Shot::CircuitRight;
      circuits_[ball].steps = 0;
      circuitMade(made);
    }
    rollCallLit_[lane] = true;
    if (std::ranges::all_of(rollCallLit_, [](bool lit) { return lit; })) {
      award(points::kRollCallComplete, Feature::RollCall);
      rollCallLit_.fill(false);
      ++laneSetsThisBall_;
      cue(Sound::BankComplete);
      if (overdrive_ > 1) {
        // Again while it runs: the most there is, and its time over again.
        overdrive_ = kMaxOverdrive;
        overdriveSteps_ = stepsFor(tuning::kOverdriveSeconds);
        say(display::Message("OVERDRIVE 3X"));
      } else {
        say(display::Message("ROLL CALL COMPLETE"));
        lightOverdrive();
      }
    }
    return;
  }

  // The four return lanes: W and G are the outlanes.
  const std::size_t letter =
      std::to_underlying(rollover.id) - std::to_underlying(table::RolloverId::WingW);
  if (letter >= wingLit_.size()) {
    return;
  }
  if (skill && rollover.id == table::RolloverId::WingI) {
    // A full plunge came round the arch to the left flipper.
    superSkillSteps_ = stepsFor(tuning::kSuperSkillSeconds);
  }
  award(points::kReturnLane, Feature::ReturnLanes);
  if (rollover.id == table::RolloverId::WingG) {
    award(points::kRightOutlane, Feature::ReturnLanes);
  }
  wingLit_[letter] = true;
  if (std::ranges::all_of(wingLit_, [](bool lit) { return lit; })) {
    award(points::kWingComplete, Feature::ReturnLanes);
    wingLit_.fill(false);
    ++laneSetsThisBall_;
    cue(Sound::BankComplete);
    if (kickbackLit_) {
      addBonusMultiplier();
      say(display::Message("WINGS EARNED  BONUS X"));
    } else {
      lightKickback(rollover.id == table::RolloverId::WingW);
      say(display::Message("WINGS EARNED  KICKBACK LIT"));
    }
  }
}

void Rules::lightKickback(bool fromLeftOutlane) noexcept {
  if (!kickbackLit_ && fromLeftOutlane) {
    // The ball that lit it is already on its way out past it.
    kickbackArmSteps_ = stepsFor(kKickbackArmSeconds);
  }
  kickbackLit_ = true;
}

void Rules::addBonusMultiplier() noexcept {
  bonusMultiplier_ = std::min(bonusMultiplier_ + 1, kMaxBonusMultiplier);
}

void Rules::onSpin() noexcept {
  if (dead()) {
    return;
  }
  noteSwitch();
  award(points::kSpin, Feature::Spinner);
  if (course_ == std::to_underlying(CourseId::TurbineTheory)) {
    courseStep(CourseId::TurbineTheory);
  }
  addTurbo(tuning::kTurboPerSpin);
}

void Rules::onKickback() noexcept {
  if (gameOver_) {
    return;
  }
  // Spent, whether or not the table is live.
  kickbackLit_ = false;
  if (tilted_) {
    return;
  }
  noteSwitch();
  award(points::kKickback, Feature::ReturnLanes);
  say(display::Message("KICKBACK"));
}

void Rules::onSensor(std::size_t index, int direction, float speed, std::size_t ball) noexcept {
  if (index >= table::kSensors.size() || dead()) {
    return;
  }
  switch (table::kSensors[index].id) {
    case table::SensorId::CircuitMouthLeft:
    case table::SensorId::CircuitMouthRight:
      if (direction > 0 && speed >= tuning::kCircuitMinSpeed && ball < circuits_.size()) {
        const bool left = table::kSensors[index].id == table::SensorId::CircuitMouthLeft;
        circuits_[ball] = {static_cast<std::uint8_t>(left ? 0 : 1),
                           stepsFor(tuning::kCircuitSeconds)};
      }
      break;
    case table::SensorId::ArchTop:
      // The left circuit runs to the right over the top, the right one to
      // the left.
      if (ball < circuits_.size() && circuits_[ball].steps > 0 &&
          (circuits_[ball].side == 0) == (direction > 0)) {
        const Shot made = circuits_[ball].side == 0 ? Shot::CircuitLeft : Shot::CircuitRight;
        circuits_[ball].steps = 0;
        circuitMade(made);
      }
      break;
    case table::SensorId::Spinner:
      if (direction > 0) {
        // Up the Turbine lane: the shot. The spins follow by themselves.
        if (overdriveLit_) {
          startOverdrive();
        }
        majorShot(Shot::Turbine);
        flowShot(Shot::Turbine);
      }
      break;
  }
}

void Rules::circuitMade(Shot shot) noexcept {
  award(points::kCircuit, Feature::Circuits);
  majorShot(shot);
  flowShot(shot);
}

void Rules::rampMade(Shot shot) noexcept {
  award(points::kRamp, Feature::Ramps);
  if (shot == Shot::Skyway && superSkillSteps_ > 0) {
    superSkillSteps_ = 0;
    award(points::kSuperSkillShot * static_cast<std::uint64_t>(ball_), Feature::SkillShot, false);
    say(display::Message("SUPER SKILL SHOT"));
    cue(Sound::MissionSelect);
  }
  // Ramps relight the Briefing room once a course is over, and Graduation
  // Day after one that was not finished.
  if (!graduationRunning()) {
    if (gradRampsNeeded_ > 0) {
      if (--gradRampsNeeded_ == 0) {
        gradLit_ = true;
        say(display::Message("GRADUATION DAY IS LIT"));
        cue(Sound::MissionSelect);
      } else {
        say(display::Message("RAMPS FOR BRIEFING"));
      }
    } else if (rampsNeeded_ > 0 && !courseRunning()) {
      if (--rampsNeeded_ == 0) {
        courseLit_ = true;
        say(display::Message("BRIEFING IS LIT"));
        cue(Sound::MissionSelect);
      } else {
        say(display::Message("RAMPS FOR BRIEFING"));
      }
    }
  }
  majorShot(shot);
  flowShot(shot);
}

Rules::HoleResult Rules::onHole(std::size_t index) noexcept {
  if (index >= table::kHoles.size() || dead()) {
    return HoleResult::Eject;
  }
  noteSwitch();
  switch (table::kHoles[index].type) {
    using enum table::HoleType;
    case RampLeft:
      rampMade(Shot::Runway);
      break;
    case RampRight:
      rampMade(Shot::Skyway);
      break;
    case Scoop: {
      if (graduationRunning()) {
        majorShot(Shot::Briefing);
        break;
      }
      // Collected in this order; the first is what the display says first.
      bool collected = false;
      if (graduationLit() || briefingLit() || extraBallsLit_ > 0 || extraCreditLit_) {
        // What is collected is said at once, one thing after another.
        message_ = {};
        messageSteps_ = 0;
        waitingCount_ = 0;
      }
      if (graduationLit()) {
        startGraduation();
        collected = true;
      } else if (briefingLit()) {
        startCourse();
        collected = true;
      }
      while (extraBallsLit_ > 0) {
        collectExtraBall();
        collected = true;
      }
      if (extraCreditLit_) {
        collectExtraCredit();
        collected = true;
      }
      // The safest shot on the table does not pay for being looped.
      award(collected || !briefingVisited_ ? points::kBriefingRoom : points::kBriefingRepeat,
            Feature::Briefing);
      briefingVisited_ = true;
      if (!graduationRunning()) {
        majorShot(Shot::Briefing);
      }
      break;
    }
    case Hangar: {
      if (graduationRunning()) {
        ++hangarHolding_;
        majorShot(Shot::Hangar);
        break;
      }
      if (superLit_) {
        ++hangarHolding_;
        superLit_ = false;
        jackpotsTowardSuper_ = 0;
        award(points::kSuperJackpot, Feature::SuperJackpot);
        say(display::Message("SUPER JACKPOT"));
        cue(Sound::Promotion);
        break;
      }
      award(points::kHangar, Feature::Hangar);
      if (lockLit()) {
        if (locksHeld_ < kLocksNeeded) {
          // Game says so (onBallLocked) once the ball is in its bay.
          return HoleResult::Lock;
        }
        // The third ball in: all three go.
        ++hangarHolding_;
        academyMultiball_ = true;
        turboMultiball_ = false;
        ++academyMultiballs_;
        releasesPending_ = locksHeld_;
        locksHeld_ = 0;
        doorsOpen_ = false;
        jackpotIndex_ = 0;
        jackpotsTowardSuper_ = 0;
        graceSteps_ = stepsFor(tuning::kAcademyGraceSeconds);
        say(display::Message("ACADEMY MULTIBALL"));
        cue(Sound::Promotion);
        if (academyMultiballs_ == 2) {
          lightExtraBall();
        }
        return HoleResult::StartMultiball;
      }
      ++hangarHolding_;
      if (!multiballEnabled_) {
        // One ball at a time: the Hangar is shut again behind every ball.
        doorsOpen_ = false;
      }
      majorShot(Shot::Hangar);
      break;
    }
  }
  return HoleResult::Eject;
}

void Rules::onEject(std::size_t index) noexcept {
  if (index >= table::kHoles.size() || table::kHoles[index].type != table::HoleType::Hangar) {
    return;
  }
  hangarHolding_ = std::max(hangarHolding_ - 1, 0);
  // If the doors are to come back up, they do so a moment after the ball.
  doorRaiseSteps_ = stepsFor(kDoorRaiseSeconds);
}

void Rules::onBallLocked(std::size_t bay, bool served) noexcept {
  static_cast<void>(bay);
  if (gameOver_) {
    return;
  }
  locksHeld_ = std::min(locksHeld_ + 1, kLocksNeeded);
  status_.locked = std::min(status_.locked + 1, kLocksNeeded);
  status_.live = std::max(status_.live - 1, 0);
  say(locksHeld_ == 1 ? display::Message("BALL 1 LOCKED") : display::Message("BALL 2 LOCKED"));
  cue(Sound::MissionAccept);
  if (academyMultiballs_ > 0 || locksHeld_ > tuning::kFirstMultiballOpenLocks) {
    // Only the first multiball of a game keeps the Hangar open between locks.
    doorsOpen_ = false;
  }
  if (served) {
    // The player has a new ball to launch, with its own saver.
    ++status_.live;
    saverArmable_ = true;
    saverSteps_ = 0;
    offerSkillShot();
  }
}

void Rules::onLockReleased(std::size_t bay) noexcept {
  static_cast<void>(bay);
  if (releasesPending_ > 0) {
    --releasesPending_;
  } else {
    locksHeld_ = std::max(locksHeld_ - 1, 0);
  }
  status_.locked = std::max(status_.locked - 1, 0);
  ++status_.live;
}

void Rules::onDoorBank(bool up) noexcept {
  doorsDown_ = !up;
  if (up) {
    doorMarked_.fill(false);
  }
}

bool Rules::doorsWantedDown() const noexcept {
  if (gameOver_) {
    return false;
  }
  switch (gradStage_) {
    case GradStage::Exam:
      return !examMade_[std::to_underlying(Shot::Hangar)];
    case GradStage::Commencement:
    case GradStage::VictoryLaps:
      return true;
    case GradStage::None:
      break;
  }
  return doorsOpen_ || superLit_;
}

bool Rules::lockLit() const noexcept {
  return doorsOpen_ && multiballEnabled_ && !dead() && !multiballRunning() && !superLit_ &&
         course_ != std::to_underlying(CourseId::HangarSafety);
}

// --- Turbo meter, overdrive, awards at the Briefing room -----------------------

void Rules::addTurbo(int amount) noexcept {
  if (dead()) {
    return;
  }
  turboMeter_ += amount;
  if (turboMeter_ >= turboGoal_) {
    turboFull();
  }
}

void Rules::turboFull() noexcept {
  turboMeter_ = 0;
  turboGoal_ = std::min(turboGoal_ + tuning::kTurboMeterStep, 10'000);
  if (!multiballEnabled_ || ballsInGame() >= static_cast<int>(table::kMaxBalls)) {
    // No room for another ball: the three-ball limit counts every ball
    // there is, locked and waiting ones too.
    award(points::kTurboBonus, Feature::TurboBonus);
    say(display::Message("TURBO BONUS"));
    return;
  }
  ++ballRequests_;
  if (!multiballRunning()) {
    turboMultiball_ = true;
    ++turboMultiballs_;
    jackpotIndex_ = 0;
  }
  graceSteps_ = std::max(graceSteps_, stepsFor(tuning::kTurboGraceSeconds));
  say(display::Message("TURBO MULTIBALL"));
  cue(Sound::Promotion);
}

void Rules::lightOverdrive() noexcept {
  if (overdrive_ == 1 && !overdriveLit_) {
    overdriveLit_ = true;
    sayThen(display::Message("OVERDRIVE IS LIT"));
  }
}

void Rules::startOverdrive() noexcept {
  overdriveLit_ = false;
  overdrive_ = 2;
  overdriveSteps_ = stepsFor(tuning::kOverdriveSeconds);
  say(display::Message("OVERDRIVE 2X"));
  cue(Sound::MissionAccept);
}

void Rules::lightExtraBall() noexcept {
  ++extraBallsLit_;
  sayThen(display::Message("EXTRA BALL IS LIT"));
}

void Rules::collectExtraBall() noexcept {
  --extraBallsLit_;
  if (extraBallsAwarded_ < kMaxExtraBalls && extraBallsHeld_ < kMaxExtraBallsHeld) {
    ++extraBallsAwarded_;
    ++extraBallsHeld_;
    sayThen(display::Message("EXTRA BALL"));
    cue(Sound::ExtraBall);
    return;
  }
  award(points::kExtraBallBeyondLimit, Feature::ExtraBall, false);
  sayThen(display::Message("EXTRA BALL BONUS 100,000"));
}

void Rules::collectExtraCredit() noexcept {
  extraCreditLit_ = false;
  auto roll = static_cast<unsigned>(nextRandom() % 100U);
  Credit credit = kCredits.back().credit;
  for (const CreditWeight& entry : kCredits) {
    if (roll < entry.weight) {
      credit = entry.credit;
      break;
    }
    roll -= entry.weight;
  }
  cue(Sound::MissionSelect);
  switch (credit) {
    case Credit::Points25:
      award(25'000, Feature::ExtraCredit, false);
      sayThen(display::Message("EXTRA CREDIT 25,000"));
      break;
    case Credit::Points50:
      award(50'000, Feature::ExtraCredit, false);
      sayThen(display::Message("EXTRA CREDIT 50,000"));
      break;
    case Credit::Kickback:
      lightKickback(false);
      sayThen(display::Message("EXTRA CREDIT  KICKBACK LIT"));
      break;
    case Credit::BonusX:
      addBonusMultiplier();
      sayThen(display::Message("EXTRA CREDIT  BONUS X"));
      break;
    case Credit::Saver:
      saverSteps_ = std::max(saverSteps_, stepsFor(tuning::kExtraCreditSaveSeconds));
      sayThen(display::Message("EXTRA CREDIT  BALL SAVER"));
      break;
    case Credit::Turbo:
      // What a full meter says comes first.
      addTurbo(tuning::kTurboExtraCredit);
      sayThen(display::Message("EXTRA CREDIT  TURBO BOOST"));
      break;
    case Credit::Overdrive:
      sayThen(display::Message("EXTRA CREDIT  OVERDRIVE LIT"));
      if (overdrive_ == 1) {
        overdriveLit_ = true;
      }
      break;
  }
}

// --- Shots: courses, jackpots, combos ------------------------------------------

void Rules::majorShot(Shot shot) noexcept {
  if (dead()) {
    return;
  }
  const std::size_t made = std::to_underlying(shot);
  switch (gradStage_) {
    case GradStage::Exam: {
      if (examMade_[made]) {
        return;
      }
      examMade_[made] = true;
      award(points::kExamShot, Feature::Graduation);
      cue(Sound::MissionAccept);
      const int left = examShotsLeft();
      if (left == 0) {
        gradStage_ = GradStage::Commencement;
        say(display::Message("SHOOT THE HANGAR"));
      } else {
        say(kExamMessages[static_cast<std::size_t>(left - 1)]);
      }
      return;
    }
    case GradStage::Commencement:
      if (shot == Shot::Hangar) {
        award(points::kCommencement, Feature::Graduation, false);
        rank_ = kTopRank;
        commencementThisTime_ = true;
        gradStage_ = GradStage::VictoryLaps;
        say(display::Message("CHANCELLOR"));
        cue(Sound::Promotion);
      }
      return;
    case GradStage::VictoryLaps:
      award(points::kVictoryLap, Feature::Graduation);
      return;
    case GradStage::None:
      break;
  }

  if (jackpotLit(shot)) {
    jackpot();
  }
  if (!courseRunning()) {
    return;
  }
  switch (static_cast<CourseId>(course_)) {
    case CourseId::RunwayDrills:
      if (isRamp(shot)) {
        courseStep(CourseId::RunwayDrills);
      }
      break;
    case CourseId::CircuitTraining:
      if (isCircuit(shot)) {
        courseStep(CourseId::CircuitTraining);
      }
      break;
    case CourseId::HangarSafety:
      if (shot == Shot::Hangar && courseSecondPart_) {
        // The bank shuts behind the ball.
        doorsOpen_ = false;
        passCourse();
      }
      break;
    case CourseId::Navigation:
      if (shot == navShot_) {
        courseStep(CourseId::Navigation);
        if (course_ == std::to_underlying(CourseId::Navigation)) {
          hopNavigation();
        }
      }
      break;
    case CourseId::BumperBasics:
    case CourseId::TurbineTheory:
      break;
  }
}

void Rules::flowShot(Shot shot) noexcept {
  if (dead()) {
    return;
  }
  const std::uint8_t made = std::to_underlying(shot);
  if (comboSteps_ == 0 || comboShot_ == kNoShot) {
    // The first shot of a chain.
    comboChain_ = 1;
    comboShot_ = made;
    comboSteps_ = stepsFor(tuning::kComboSeconds);
    return;
  }
  comboSteps_ = stepsFor(tuning::kComboSeconds);
  if (made == comboShot_) {
    // The same shot again neither extends the chain nor breaks it.
    return;
  }
  comboShot_ = made;
  ++comboChain_;
  const auto link = static_cast<std::size_t>(std::clamp(comboChain_, 2, kMaxCombo) - 2);
  award(points::kCombo[link], Feature::Combos);
  ++combosThisBall_;
  sayAlso(kComboMessages[link]);
  cue(Sound::MissionSelect);
  if (comboChain_ >= kMaxCombo) {
    // The chain is complete: the next flow shot starts a new one.
    comboChain_ = 0;
    comboShot_ = kNoShot;
    comboSteps_ = 0;
    if (!fiveComboMade_) {
      fiveComboMade_ = true;
      lightExtraBall();
    }
  }
}

bool Rules::jackpotLit(Shot shot) const noexcept {
  if (dead() || graduationRunning()) {
    return false;
  }
  if (academyMultiball_) {
    return isRamp(shot) || isCircuit(shot);
  }
  return turboMultiball_ && isRamp(shot);
}

bool Rules::multiballJackpotLit() const noexcept {
  return jackpotLit(Shot::Runway);
}

std::uint64_t Rules::jackpotValue() const noexcept {
  return points::kJackpot[std::min(jackpotIndex_, points::kJackpot.size() - 1)];
}

void Rules::jackpot() noexcept {
  award(jackpotValue(), Feature::Jackpots);
  jackpotIndex_ = std::min(jackpotIndex_ + 1, points::kJackpot.size());
  ++jackpotsCollected_;
  say(display::Message("JACKPOT"));
  cue(Sound::MissionComplete);
  if (academyMultiball_ && !superLit_ && ++jackpotsTowardSuper_ >= tuning::kJackpotsForSuper) {
    jackpotsTowardSuper_ = 0;
    superLit_ = true;
    sayThen(display::Message("SUPER JACKPOT IS LIT"));
  }
}

// --- Multiball -----------------------------------------------------------------

Multiball Rules::multiball() const noexcept {
  if (graduationRunning()) {
    return Multiball::Graduation;
  }
  if (academyMultiball_) {
    return Multiball::Academy;
  }
  return turboMultiball_ ? Multiball::Turbo : Multiball::None;
}

void Rules::endMultiball(bool announce) noexcept {
  if (graduationRunning()) {
    endGraduation();
    return;
  }
  if (academyMultiball_) {
    // Doors up, lock unlit.
    doorsOpen_ = false;
  }
  turboMultiball_ = false;
  academyMultiball_ = false;
  superLit_ = false;
  jackpotIndex_ = 0;
  jackpotsTowardSuper_ = 0;
  graceSteps_ = 0;
  if (announce) {
    say(display::Message("MULTIBALL OVER"));
  }
}

bool Rules::graduationLit() const noexcept {
  return gradLit_ && multiballEnabled_ && !graduationRunning() && !gameOver_;
}

int Rules::examShotsLeft() const noexcept {
  return static_cast<int>(std::ranges::count(examMade_, false));
}

void Rules::startGraduation() noexcept {
  gradLit_ = false;
  gradRampsNeeded_ = 0;
  gradStage_ = examShotsLeft() == 0 ? GradStage::Commencement : GradStage::Exam;
  commencementThisTime_ = false;
  // It takes over from any other multiball.
  turboMultiball_ = false;
  academyMultiball_ = false;
  superLit_ = false;
  doorsOpen_ = false;
  // Every ball there is: the locked ones are let go, the rest are launched.
  if (locksHeld_ > 0) {
    releasesPending_ += locksHeld_;
    locksHeld_ = 0;
    lockRelease_ = true;
  }
  ballRequests_ += std::max(static_cast<int>(table::kMaxBalls) - ballsInGame(), 0);
  graceSteps_ = stepsFor(tuning::kGraduationGraceSeconds);
  say(display::Message("GRADUATION DAY"));
  cue(Sound::Promotion);
}

void Rules::endGraduation() noexcept {
  gradStage_ = GradStage::None;
  graceSteps_ = 0;
  jackpotIndex_ = 0;
  jackpotsTowardSuper_ = 0;
  doorsOpen_ = false;
  if (commencementThisTime_) {
    // Done: every course can be taken again, for twice the award.
    commencementThisTime_ = false;
    ++graduations_;
    coursePassed_.fill(false);
    examMade_.fill(false);
    courseBlink_ = 0;
    courseLit_ = true;
    rampsNeeded_ = 0;
    coursesEnded_ = 0;
    say(display::Message("COURSES REOPENED"));
    return;
  }
  // Not finished: the exam shots made stay made, and ramps light it again.
  gradRampsNeeded_ = tuning::kRampsForGraduation;
  say(display::Message("MULTIBALL OVER"));
}

// --- Courses -------------------------------------------------------------------

int Rules::coursesPassed() const noexcept {
  return static_cast<int>(std::ranges::count(coursePassed_, true));
}

bool Rules::allCoursesPassed() const noexcept {
  return std::ranges::all_of(coursePassed_, [](bool passed) { return passed; });
}

std::size_t Rules::nextUnpassed(std::size_t from) const noexcept {
  for (std::size_t n = 0; n < kCourseCount; ++n) {
    const std::size_t index = (from + n) % kCourseCount;
    if (!coursePassed_[index]) {
      return index;
    }
  }
  return 0;
}

bool Rules::briefingLit() const noexcept {
  return courseLit_ && !courseRunning() && !graduationRunning() && !allCoursesPassed() &&
         !gameOver_;
}

int Rules::rampsForBriefing() const noexcept {
  if (gradRampsNeeded_ > 0) {
    return gradRampsNeeded_;
  }
  return courseRunning() || courseLit_ ? 0 : rampsNeeded_;
}

void Rules::startCourse() noexcept {
  course_ = nextUnpassed(courseBlink_);
  courseBlink_ = course_;
  courseLit_ = false;
  ++coursesStarted_;
  courseSteps_ = stepsFor(kCourses[course_].seconds);
  courseElapsedSteps_ = 0;
  courseProgress_ = 0;
  courseSecondPart_ = false;
  if (course_ == std::to_underlying(CourseId::HangarSafety)) {
    // The doors are raised for it, whatever state they were in.
    doorsOpen_ = false;
    doorMarked_.fill(false);
  }
  if (course_ == std::to_underlying(CourseId::Navigation)) {
    navShot_ = Shot::CircuitLeft;
    navSteps_ = stepsFor(kNavigationHopSeconds);
  }
  sayThen(display::Message("CLASS IN SESSION"));
  cue(Sound::MissionAccept);
}

void Rules::courseStep(CourseId id) noexcept {
  const std::size_t index = std::to_underlying(id);
  if (course_ != index) {
    return;
  }
  award(kCourses[index].shotAward, courseFeature(index));
  if (++courseProgress_ >= kCourses[index].goal) {
    passCourse();
  }
}

std::uint64_t Rules::hurryUpValue() const noexcept {
  if (course_ != std::to_underlying(CourseId::TurbineTheory)) {
    return 0;
  }
  const std::uint64_t floor = kCourses[course_].passAward;
  const auto seconds = static_cast<std::uint64_t>(secondsOf(courseElapsedSteps_));
  const std::uint64_t fallen = std::min(seconds * kHurryUpPerSecond, kHurryUpStart - floor);
  return kHurryUpStart - fallen;
}

void Rules::passCourse() noexcept {
  const std::size_t index = course_;
  std::uint64_t value = index == std::to_underlying(CourseId::TurbineTheory)
                            ? hurryUpValue()
                            : kCourses[index].passAward;
  if (graduations_ > 0) {
    value *= 2;  // Second time round.
  }
  award(value, courseFeature(index), false);
  coursePassed_[index] = true;
  ++coursePasses_;
  cue(Sound::MissionComplete);
  if (allCoursesPassed()) {
    gradLit_ = true;
    say(display::Message("ALL COURSES PASSED"));
    sayThen(display::Message("GRADUATION DAY IS LIT"));
  } else if (graduations_ == 0) {
    rank_ = std::min(static_cast<std::size_t>(coursesPassed()), kTopRank - 1);
    say(display::Message("COURSE PASSED  RANK UP"));
    cue(Sound::Promotion);
  } else {
    // The top rank is held already.
    say(display::Message("COURSE PASSED"));
  }
  if (coursePasses_ == 3) {
    lightExtraBall();
  }
  courseOver();
}

void Rules::failCourse() noexcept {
  say(display::Message("COURSE FAILED"));
  courseOver();
}

void Rules::courseOver() noexcept {
  course_ = kNoCourse;
  courseSteps_ = 0;
  courseElapsedSteps_ = 0;
  courseProgress_ = 0;
  courseSecondPart_ = false;
  navSteps_ = 0;
  ++coursesEnded_;
  courseLit_ = false;
  rampsNeeded_ = allCoursesPassed()
                     ? 0
                     : (coursesEnded_ >= 3 ? tuning::kRampsForBriefingLater
                                           : tuning::kRampsForBriefing);
  courseBlink_ = nextUnpassed(courseBlink_);
}

bool Rules::navigable(Shot shot) const noexcept {
  // The Hangar can be made only with its doors down.
  return shot != Shot::Hangar || doorsDown_;
}

void Rules::hopNavigation() noexcept {
  std::size_t next = std::to_underlying(navShot_);
  for (std::size_t n = 0; n < kShotCount; ++n) {
    next = (next + 1) % kShotCount;
    if (navigable(static_cast<Shot>(next))) {
      break;
    }
  }
  navShot_ = static_cast<Shot>(next);
  navSteps_ = stepsFor(kNavigationHopSeconds);
}

std::string_view Rules::courseName() const noexcept {
  if (graduationRunning()) {
    return kGraduationName;
  }
  return courseRunning() ? kCourses[course_].name : std::string_view{};
}

std::string_view Rules::goalText() const noexcept {
  switch (gradStage_) {
    case GradStage::Exam:
      return kGoalExam;
    case GradStage::Commencement:
      return kGoalCommencement;
    case GradStage::VictoryLaps:
      return kGoalExam;
    case GradStage::None:
      break;
  }
  if (courseRunning()) {
    return courseSecondPart_ ? kGoalEnterHangar : kCourses[course_].goalLine;
  }
  return briefingLit() || graduationLit() ? kGoalBriefingLit : kGoalBriefingUnlit;
}

int Rules::goalRemaining() const noexcept {
  if (gradStage_ == GradStage::Exam) {
    return examShotsLeft();
  }
  if (!courseRunning() || graduationRunning()) {
    return 0;
  }
  return courseSecondPart_ ? 1 : std::max(kCourses[course_].goal - courseProgress_, 0);
}

// --- Drains, nudges, time ------------------------------------------------------

bool Rules::onBallLost(const DrainInfo& info) noexcept {
  status_.live = std::max(status_.live - 1, 0);
  if (dead()) {
    return false;
  }
  const bool owed =
      info.afterEject || ballSaveActive();
  if (owed) {
    ++status_.queued;
    say(display::Message("BALL SAVED"));
    cue(Sound::BallSaved);
  }
  return owed;
}

void Rules::onBallInPlay() noexcept {
  if (gameOver_) {
    return;
  }
  ballInPlay_ = true;
  if (saverArmable_) {
    // Once for each ball the player is given: a ball the saver returns
    // has what is left of its time.
    saverArmable_ = false;
    saverSteps_ = std::max(saverSteps_, stepsFor(kBallSaveSeconds));
  }
}

void Rules::onBallReturned() noexcept {
  if (gameOver_) {
    return;
  }
  // A tilt cannot outlive the ball's time in play: on the plunger it would
  // leave the controls dead for good.
  tilted_ = false;
  tiltHeat_ = 0.0;
  offerSkillShot();
  say(display::Message("BALL SAVED"));
}

std::uint64_t Rules::bonusSoFar() const noexcept {
  return static_cast<std::uint64_t>(coursePasses_) * points::kBonusCourse +
         static_cast<std::uint64_t>(locksHeld_) * points::kBonusLock +
         static_cast<std::uint64_t>(combosThisBall_) * points::kBonusCombo +
         static_cast<std::uint64_t>(laneSetsThisBall_) * points::kBonusLaneSet +
         static_cast<std::uint64_t>(switchesThisBall_) * points::kBonusSwitch;
}

void Rules::payBonus() noexcept {
  BonusTally tally;
  tally.courses = static_cast<std::uint64_t>(coursePasses_) * points::kBonusCourse;
  tally.locks = static_cast<std::uint64_t>(locksHeld_) * points::kBonusLock;
  tally.combos = static_cast<std::uint64_t>(combosThisBall_) * points::kBonusCombo;
  tally.lanes = static_cast<std::uint64_t>(laneSetsThisBall_) * points::kBonusLaneSet;
  tally.switches = static_cast<std::uint64_t>(switchesThisBall_) * points::kBonusSwitch;
  tally.multiplier = std::clamp(bonusMultiplier_, 1, kMaxBonusMultiplier);
  tally.total = (tally.courses + tally.locks + tally.combos + tally.lanes + tally.switches) *
                static_cast<std::uint64_t>(tally.multiplier);
  bonusTally_ = tally;
  // Not multiplied by overdrive, which has ended with the ball anyway.
  award(tally.total, Feature::Bonus, false);
  // One line at a time; a line with nothing in it is left out.
  waitingCount_ = 0;
  message_ = {};
  messageSteps_ = 0;
  if (tally.courses > 0) {
    sayThen(display::numbered("COURSES", tally.courses));
  }
  if (tally.locks > 0) {
    sayThen(display::numbered("LOCKS", tally.locks));
  }
  if (tally.combos > 0) {
    sayThen(display::numbered("COMBOS", tally.combos));
  }
  if (tally.lanes > 0) {
    sayThen(display::numbered("LANES", tally.lanes));
  }
  if (tally.switches > 0) {
    sayThen(display::numbered("SWITCHES", tally.switches));
  }
  sayThen(display::numbered(kBonusTotalLabels[static_cast<std::size_t>(tally.multiplier - 1)],
                            tally.total));
}

Rules::DrainResult Rules::onDrain(const DrainInfo& info) noexcept {
  ballInPlay_ = false;
  // Whatever comes next, Game puts one ball on the plunger.
  status_.live = 1;
  status_.free = 0;
  if (gameOver_) {
    return DrainResult::GameOver;
  }
  // A tilt ends with the ball, whatever happens next.
  const bool wasTilted = tilted_;
  tilted_ = false;
  tiltHeat_ = 0.0;
  const bool saved =
      !wasTilted && (info.afterEject || ballSaveActive());
  // With the last ball gone no multiball is left, saved or not.
  if (multiballRunning()) {
    endMultiball(false);
  }
  if (saved) {
    // Back on the plunger for the player. The saver's clock runs on.
    offerSkillShot();
    say(display::Message("BALL SAVED"));
    cue(Sound::BallSaved);
    return DrainResult::BallSaved;
  }

  // The end of the ball.
  if (courseRunning()) {
    if (wasTilted) {
      failCourse();
    } else {
      // It goes on with the next ball, with time to do something about it.
      courseSteps_ = std::max(courseSteps_, stepsFor(kCourseCarrySeconds));
    }
  }
  overdrive_ = 1;
  overdriveSteps_ = 0;
  comboShot_ = kNoShot;
  comboChain_ = 0;
  comboSteps_ = 0;
  superSkillSteps_ = 0;
  circuits_ = {};
  if (wasTilted) {
    // The bonus is lost with the tilt.
    waitingCount_ = 0;
    message_ = {};
    messageSteps_ = 0;
    bonusTally_ = {};
  } else {
    payBonus();
  }

  if (extraBallsHeld_ > 0) {
    --extraBallsHeld_;
    beginBall();
    sayThen(display::Message("SHOOT AGAIN"));
    cue(Sound::ExtraBall);
    return DrainResult::ShootAgain;
  }
  if (ball_ >= kBallsPerGame) {
    // The bays are emptied (Game does that) and nothing is left running.
    clearModes();
    gameOver_ = true;
    // No word on how to start again: that depends on the device (a key, a
    // button, a tap), so the platform draws the prompt.
    sayThen(kGameOverMessage);
    cue(Sound::GameOver);
    return DrainResult::GameOver;
  }
  ++ball_;
  beginBall();
  cue(Sound::Drain);
  sayThen(display::Message("NEXT BALL"));
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
    // A tilted ball is not returned: saver, grace and the short save after
    // an eject are all off (the last by Game asking while tilted).
    saverArmable_ = false;
    saverSteps_ = 0;
    graceSteps_ = 0;
    say(kTiltMessage);
    cue(Sound::Tilt);
    return false;
  }
  if (tiltHeat_ >= kTiltLimit - 1.0) {
    say(display::Message("DANGER  TILT WARNING"));
    cue(Sound::TiltWarning);
  }
  return true;
}

void Rules::tick() noexcept {
  if (!tilted_) {
    tiltHeat_ = std::max(0.0, tiltHeat_ - stepSeconds_ / kTiltCoolSeconds);
  }
  if (!dead()) {
    // These clocks run only while a ball is free on the playfield: not
    // while every live ball is in a hold or waiting on the plunger. Locked
    // balls are not live balls and never stop them.
    if (status_.free > 0) {
      if (saverSteps_ > 0) {
        --saverSteps_;
      }
      if (courseRunning()) {
        ++courseElapsedSteps_;
        if (course_ == std::to_underlying(CourseId::Navigation)) {
          // A lit shot that cannot be made any more moves on at once.
          if (!navigable(navShot_) || (navSteps_ > 0 && --navSteps_ == 0)) {
            hopNavigation();
          }
        }
        if (courseSteps_ > 0 && --courseSteps_ == 0) {
          failCourse();
        }
      }
      if (overdriveSteps_ > 0 && --overdriveSteps_ == 0) {
        overdrive_ = 1;
        say(display::Message("OVERDRIVE OVER"));
      }
    }
    if (graceSteps_ > 0) {
      --graceSteps_;
    }
    if (comboSteps_ > 0 && --comboSteps_ == 0) {
      comboShot_ = kNoShot;
      comboChain_ = 0;
    }
    if (superSkillSteps_ > 0) {
      --superSkillSteps_;
    }
    for (CircuitTry& circuit : circuits_) {
      if (circuit.steps > 0) {
        --circuit.steps;
      }
    }
  }
  if (kickbackArmSteps_ > 0) {
    --kickbackArmSteps_;
  }
  if (doorRaiseSteps_ > 0) {
    --doorRaiseSteps_;
  }

  // The doors follow what the rules want of them.
  if (doorsWantedDown()) {
    if (!doorsDown_) {
      doorsDown_ = true;
      doorCommand_ = DoorCommand::Drop;
      markAllDoors();
    }
  } else if (doorsDown_ && hangarHolding_ == 0 && doorRaiseSteps_ == 0) {
    // Not while the Hangar still has a ball to return.
    doorsDown_ = false;
    doorCommand_ = DoorCommand::Raise;
  }

  // A multiball is over when one ball is left of it.
  if (!gameOver_ && multiballRunning() && ballsLive() <= 1) {
    endMultiball(true);
  }

  messageFresh_ = false;
  if (messageSteps_ > 0 && --messageSteps_ == 0) {
    if (waitingCount_ > 0) {
      message_ = waiting_[0];
      std::ranges::rotate(waiting_, waiting_.begin() + 1);
      --waitingCount_;
      messageSteps_ = stepsFor(waitingCount_ > 0 ? kQueuedMessageSeconds : kMessageSeconds);
    } else if (!gameOver_ && !tilted_) {
      message_ = {};
    }
  }
}

// --- Lamps ---------------------------------------------------------------------

LampState Rules::lampState(table::LampId lamp) const noexcept {
  using enum table::LampId;
  if (gameOver_) {
    return LampState::Off;
  }
  const auto arrow = [this](Shot shot) {
    const std::size_t index = std::to_underlying(shot);
    switch (gradStage_) {
      case GradStage::Exam:
        return examMade_[index] ? LampState::Off : LampState::Blink;
      case GradStage::Commencement:
        return shot == Shot::Hangar ? LampState::FastBlink : LampState::Off;
      case GradStage::VictoryLaps:
        return LampState::On;
      case GradStage::None:
        break;
    }
    if (jackpotLit(shot)) {
      return LampState::Blink;
    }
    if (courseRunning()) {
      switch (static_cast<CourseId>(course_)) {
        case CourseId::BumperBasics:
        case CourseId::TurbineTheory:
          if (shot == Shot::Turbine) {
            return LampState::Blink;
          }
          break;
        case CourseId::RunwayDrills:
          if (isRamp(shot)) {
            return LampState::Blink;
          }
          break;
        case CourseId::CircuitTraining:
          if (isCircuit(shot)) {
            return LampState::Blink;
          }
          break;
        case CourseId::HangarSafety:
          if (shot == Shot::Hangar) {
            return LampState::Blink;
          }
          break;
        case CourseId::Navigation:
          if (shot == navShot_) {
            return LampState::Blink;
          }
          break;
      }
    }
    switch (shot) {
      case Shot::Hangar:
        if (superLit_) {
          return LampState::FastBlink;
        }
        return lockLit() ? LampState::Blink : LampState::Off;
      case Shot::Briefing:
        if (graduationLit()) {
          return LampState::FastBlink;
        }
        if (briefingLit()) {
          return LampState::Blink;
        }
        return extraBallsLit_ > 0 || extraCreditLit_ ? LampState::On : LampState::Off;
      case Shot::Turbine:
        return overdriveLit_ ? LampState::Blink : LampState::Off;
      case Shot::Skyway:
        if (superSkillSteps_ > 0) {
          return LampState::FastBlink;
        }
        return rampsForBriefing() > 0 ? LampState::On : LampState::Off;
      case Shot::Runway:
        return rampsForBriefing() > 0 ? LampState::On : LampState::Off;
      case Shot::CircuitLeft:
      case Shot::CircuitRight:
        break;
    }
    return LampState::Off;
  };
  const auto courseLamp = [this](std::size_t index) {
    if (coursePassed_[index]) {
      return LampState::On;
    }
    if (course_ == index) {
      return LampState::FastBlink;
    }
    return briefingLit() && courseBlink_ == index ? LampState::Blink : LampState::Off;
  };
  const auto lit = [](bool on) { return on ? LampState::On : LampState::Off; };
  switch (lamp) {
    case ArrowCircuitLeft:
      return arrow(Shot::CircuitLeft);
    case ArrowRunway:
      return arrow(Shot::Runway);
    case ArrowHangar:
      return arrow(Shot::Hangar);
    case ArrowBriefing:
      return arrow(Shot::Briefing);
    case ArrowTurbine:
      return arrow(Shot::Turbine);
    case ArrowSkyway:
      return arrow(Shot::Skyway);
    case ArrowCircuitRight:
      return arrow(Shot::CircuitRight);
    case Lock1:
      return lit(locksHeld_ >= 1);
    case Lock2:
      return lit(locksHeld_ >= 2);
    case Course1:
    case Course2:
    case Course3:
    case Course4:
    case Course5:
    case Course6:
      return courseLamp(static_cast<std::size_t>(std::to_underlying(lamp) -
                                                 std::to_underlying(Course1)));
    case Graduation:
      if (graduationRunning()) {
        return LampState::FastBlink;
      }
      if (graduationLit()) {
        return LampState::Blink;
      }
      return lit(graduations_ > 0);
    case ExtraCredit:
      return extraCreditLit_ ? LampState::Blink : LampState::Off;
    case ExtraBall:
      return extraBallsLit_ > 0 ? LampState::Blink : LampState::Off;
    case Overdrive:
      if (overdrive_ > 1) {
        return LampState::On;
      }
      return overdriveLit_ ? LampState::Blink : LampState::Off;
    case TurboCharge:
      if (turboMultiball_) {
        return LampState::On;
      }
      if (turboMeter_ == 0) {
        return LampState::Off;
      }
      // Faster as it fills.
      return 5 * turboMeter_ >= 4 * turboGoal_ ? LampState::FastBlink : LampState::Blink;
    case Kickback:
      return lit(kickbackLit_);
    case ShootAgain:
      if (tilted_) {
        return LampState::Off;
      }
      if (ballSaveEnabled_ &&
          (graceSteps_ > 0 || saverSteps_ > stepsFor(kBallSaveWarningSeconds))) {
        return LampState::Blink;
      }
      if (ballSaveEnabled_ && saverSteps_ > 0) {
        return LampState::FastBlink;
      }
      return lit(extraBallsHeld_ > 0);
    case RollCall1:
    case RollCall2:
    case RollCall3: {
      const auto lane =
          static_cast<std::size_t>(std::to_underlying(lamp) - std::to_underlying(RollCall1));
      if (skillOffered_ && lane == skillLane_) {
        return LampState::Blink;
      }
      return lit(rollCallLit_[lane]);
    }
    case WingW:
    case WingI:
    case WingN:
    case WingG:
      return lit(wingLit_[static_cast<std::size_t>(std::to_underlying(lamp) -
                                                   std::to_underlying(WingW))]);
    case Count:
      break;
  }
  return LampState::Off;
}

}  // namespace pinball
