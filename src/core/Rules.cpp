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
    "FORMATION PASS  1 TO GO", "FORMATION PASS  2 TO GO", "FORMATION PASS  3 TO GO",
    "FORMATION PASS  4 TO GO", "FORMATION PASS  5 TO GO", "FORMATION PASS  6 TO GO"};
constexpr std::array<display::Message, kCourseCount> kPassedMessages{
    "LOOP DRILL COMPLETE", "RAMP RUNS COMPLETE", "GUNNERY COMPLETE",
    "FORMATION COMPLETE", "NIGHT LANDING COMPLETE", "DOGFIGHT WON"};
constexpr std::array<display::Message, kCourseCount> kStartMessages{
    "LOOP DRILL  SHOOT THE LOOPS", "RAMP RUNS  SHOOT LIT RAMP", "GUNNERY  HIT THE BANDIT",
    "FORMATION  SHOOT LIT ARROW", "NIGHT LANDING  RUNWAY", "DOGFIGHT  2 BALLS"};
constexpr std::array<display::Message, kCourseCount> kLitMessages{
    "LOOP DRILL IS LIT", "RAMP RUNS IS LIT",     "GUNNERY IS LIT",
    "FORMATION IS LIT",  "NIGHT LANDING IS LIT", "DOGFIGHT IS LIT"};
constexpr std::array<display::Message, 3> kAceMessages{"A-C-E  A", "A-C-E  A-C", "A-C-E COMPLETE"};
constexpr std::array<display::NumberLabel, 5> kBonusTotalLabels{
    display::NumberLabel("BONUS 1X", 2), display::NumberLabel("BONUS 2X", 2),
    display::NumberLabel("BONUS 3X", 2), display::NumberLabel("BONUS 4X", 2),
    display::NumberLabel("BONUS 5X", 2)};
static_assert(kBonusTotalLabels.size() == Rules::kMaxBonusMultiplier);

constexpr std::array<std::string_view, kFeatureCount> kFeatureNames{
    "slingshots",    "jets",          "spinners+bandit", "targets",       "r-a-d lanes",
    "return lanes",  "ramps",         "loops",           "bay doors",     "hangar+deck",
    "mission scoop", "skill shot",    "combos",          "loop drill",    "ramp runs",
    "gunnery",       "formation",     "night landing",   "dogfight",      "jackpots",
    "super jackpot", "scramble bonus", "extra credit",   "extra ball",    "wings+air show",
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
  turboRequests_ = 0;
  turboRequestsInFlight_ = 0;
  lockCommand_ = LockCommand::None;
  releasesPending_ = 0;
  locksHeld_ = 0;
  bayFull_.fill(false);
  hangarHolding_ = 0;
  doorsOpen_ = false;
  kickbackArmSteps_ = 0;
  dogfightMultiball_ = false;
  wingsSteps_ = 0;
  finaleBanditDown_ = false;
  jackpotAdd_ = 0;
  catBall_ = 0xFF;
  lastLoop_ = kNoShot;
  banks_ = 0;
}

void Rules::startGame() noexcept {
  score_ = 0;
  deckLit_ = false;
  flapsLit_.fill(false);
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
  kickbackRides_ = 0;
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
  altitude_.reset();
  bonusTally_ = {};
  taxiLit_.fill(false);
  radarLit_.fill(false);
  bogeyMarked_.fill(false);
  bogeyCleared_ = false;
  aceLit_.fill(false);
  aceDone_ = false;
  aceSets_ = 0;
  gForce_ = 0;
  deckRamps_ = 0;
  droneHits_ = 0;
  flapsSteps_ = 0;
  missionRepeatSteps_ = 0;
  qualify_.fill(0);
  ready_.fill(false);
  courseTried_.fill(false);
  wingsDone_ = false;
  wingsCeremonies_ = 0;
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
  // What Game took last step and did not refuse is in these numbers now.
  for (; turboRequestsInFlight_ > 0; --turboRequestsInFlight_) {
    turboBallServed();
  }
  requestsInFlight_ = 0;
  // The bays are the truth about the locks.
  releasesPending_ = std::min(releasesPending_, status_.locked);
  locksHeld_ = status_.locked - releasesPending_;
}

void Rules::addLiveBall() noexcept {
  // Never more balls than the table has: a ball counted live here may also
  // still be counted queued until the next setTable() says otherwise.
  if (status_.live + status_.queued + status_.locked < static_cast<int>(table::kMaxBalls)) {
    ++status_.live;
  }
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
  turboRequestsInFlight_ += turboRequests_;
  turboRequests_ = 0;
  return requests;
}

void Rules::onBallRefused() noexcept {
  requestsInFlight_ = std::max(requestsInFlight_ - 1, 0);
  // Only a full meter's ball has something else to give instead.
  if (turboRequestsInFlight_ > 0) {
    --turboRequestsInFlight_;
    turboBallRefused();
  }
}

Rules::LockCommand Rules::takeLockCommand() noexcept {
  const LockCommand command = lockCommand_;
  lockCommand_ = LockCommand::None;
  return command;
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
  if (!courseRunning() && !graduationRunning() && !allCoursesPassed() &&
      std::ranges::none_of(ready_, [](bool ready) { return ready; })) {
    // Choosing: each jet moves the Mission's offer to the next sortie not
    // yet passed (a qualified sortie holds the offer).
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
    cue(Sound::SetComplete);
    if constexpr (!table::kBogeyOnTable) {
      // BOGEY was cut in G2: the nest pair qualifies NIGHT LANDING again, as
      // it did before the bank was built.
      qualifyCourse(CourseId::NightLanding);
    }
  }
  addTurbo(tuning::kTurboPerTarget);
}

int Rules::droneArmour() const noexcept {
  if (gradStage_ == GradStage::Commencement) {
    return tuning::kFinaleArmour;
  }
  return tuning::kDroneArmourBase + altitude_.knobs().extraHits;
}

bool Rules::onDrone() noexcept {
  if (dead()) {
    return false;
  }
  noteSwitch();
  award(points::kDroneHit, Feature::Spinner);
  addTurbo(tuning::kTurboPerDroneHit);
  if (wingsRunning()) {
    // Every hit is a lit shot, and the armour stands again at once.
    droneHits_ = 0;
    award(points::kWingsShot, Feature::Graduation);
    say(display::Message("WINGS  BANDIT HIT 2X"));
    cue(Sound::Jackpot);
    return false;
  }
  if (gradStage_ == GradStage::Commencement && finaleBanditDown_) {
    return false;  // Down already: the Hangar is what counts now.
  }
  if (course_ == std::to_underlying(CourseId::Gunnery)) {
    courseSteps_ += stepsFor(kGunneryHitSeconds);
    courseStep(CourseId::Gunnery);
  } else if (course_ == std::to_underlying(CourseId::Dogfight) && !courseSecondPart_) {
    courseSecondPart_ = true;
    say(display::Message("DOGFIGHT JACKPOT IS LIT"));
    cue(Sound::Lit);
  } else if (!courseRunning()) {
    qualifyCourse(CourseId::Gunnery);
  }
  if (++droneHits_ < droneArmour()) {
    return false;
  }
  droneHits_ = 0;
  banditDown();
  return true;
}

void Rules::banditDown() noexcept {
  award(points::kBanditDown, Feature::Spinner);
  if (gradStage_ == GradStage::Commencement) {
    finaleBanditDown_ = true;
    say(display::Message("BANDIT DOWN  SHOOT HANGAR"));
    cue(Sound::Lit);
    return;
  }
  say(display::Message("BANDIT DOWN"));
  if (course_ == std::to_underlying(CourseId::Gunnery)) {
    courseSecondPart_ = true;  // The pass pays double.
  }
  if (academyMultiball_ && !superLit_) {
    superLit_ = true;
    jackpotsTowardSuper_ = 0;
    sayThen(display::Message("SUPER JACKPOT IS LIT"));
    cue(Sound::Lit);
  }
}

void Rules::onStandup(std::size_t index, float /*speed*/) noexcept {
  if (dead() || index >= table::kStandups.size()) {
    return;
  }
  noteSwitch();
  if (index < table::kTaxiStandups) {
    // T-A-X-I: dead stand-ups round the field.
    award(points::kTaxiLetter, Feature::TurboTargets);
    if (index >= taxiLit_.size() || taxiLit_[index]) {
      return;
    }
    taxiLit_[index] = true;
    if (std::ranges::all_of(taxiLit_, [](bool lit) { return lit; })) {
      taxiLit_.fill(false);
      award(points::kTaxiComplete, Feature::TurboTargets);
      ++laneSetsThisBall_;
      cue(Sound::SetComplete);
      say(display::Message("T-A-X-I COMPLETE"));
      if constexpr (!table::kRadarOnTable) {
        // RADAR was cut: T-A-X-I lights FORMATION, as before G2.
        qualifyCourse(CourseId::Formation);
      }
    }
    return;
  }
  if (index >= table::kRadarStandup) {
    if (!table::kRadarOnTable) {
      return;  // Cut: never on the table.
    }
    // RADAR: all three light FORMATION.
    const std::size_t letter = index - table::kRadarStandup;
    award(points::kTaxiLetter, Feature::TurboTargets);
    if (letter >= radarLit_.size() || radarLit_[letter]) {
      return;
    }
    radarLit_[letter] = true;
    if (std::ranges::all_of(radarLit_, [](bool lit) { return lit; })) {
      radarLit_.fill(false);
      award(points::kTaxiComplete, Feature::TurboTargets);
      ++laneSetsThisBall_;
      cue(Sound::SetComplete);
      say(display::Message("RADAR COMPLETE"));
      qualifyCourse(CourseId::Formation);
    }
    return;
  }
  // The Flight Deck's A-C-E.
  const std::size_t letter = index - table::kDeckStandupA;
  award(points::kAceLetter, Feature::TurboTargets);
  if (letter >= aceLit_.size() || aceLit_[letter]) {
    return;
  }
  aceLit_[letter] = true;
  const auto lit = static_cast<std::size_t>(std::ranges::count(aceLit_, true));
  if (lit < aceLit_.size()) {
    say(kAceMessages[lit - 1]);
    return;
  }
  aceLit_.fill(false);
  award(points::kAceComplete + points::kAceComplete / 2 * static_cast<std::uint64_t>(std::min(aceSets_, 8)),
        Feature::TurboTargets);
  ++aceSets_;
  ++laneSetsThisBall_;
  say(kAceMessages.back());
  cue(Sound::SetComplete);
  if (!aceDone_) {
    aceDone_ = true;
    checkAirShow();
  }
}

void Rules::onBogey(std::size_t index) noexcept {
  // The BOGEY drops: all three cleared light NIGHT LANDING and are what the
  // lock asks for beside the bay doors (DECISIONS.md 10).
  if (!table::kBogeyOnTable || dead() || index >= table::kBogeys.size() ||
      index >= bogeyMarked_.size()) {
    return;  // Cut in G2: no BOGEY progress, no BOGEY words.
  }
  noteSwitch();
  award(points::kTaxiLetter, Feature::TurboTargets);
  bogeyMarked_[index] = true;
  if (!std::ranges::all_of(bogeyMarked_, [](bool marked) { return marked; })) {
    return;
  }
  bogeyMarked_.fill(false);
  award(points::kTaxiComplete, Feature::TurboTargets);
  ++laneSetsThisBall_;
  cue(Sound::SetComplete);
  const bool wasCleared = bogeyCleared_;
  bogeyCleared_ = true;
  say(lockLit() && !wasCleared ? display::Message("BOGEY CLEARED  LOCK IS LIT")
                               : display::Message("BOGEY CLEARED"));
  qualifyCourse(CourseId::NightLanding);
}

void Rules::onPylon() noexcept {
  if (dead()) {
    return;
  }
  noteSwitch();
  award(points::kPylon, Feature::Slingshots);
  if (courseRunning()) {
    // A sortie's clock gets a little more.
    courseSteps_ += stepsFor(3.0);
    say(display::Message("PYLON  +3 SECONDS"));
  } else if (multiballRunning() && !graduationRunning()) {
    jackpotAdd_ = std::min<std::uint64_t>(jackpotAdd_ + points::kPylonJackpotAdd, 250'000);
    say(display::numbered("JACKPOT VALUE", jackpotValue()));
  }
}

void Rules::onCaptiveBall(float speed) noexcept {
  // The G-meter: a hit on the captive ball's switch adds 1G, a hard one 2G.
  // 3G lights DOGFIGHT; 5G pays and starts the meter again.
  if (dead()) {
    return;
  }
  noteSwitch();
  const int add = speed >= tuning::kGForceHardSpeed ? 2 : 1;
  const int before = gForce_;
  gForce_ = std::min(gForce_ + add, tuning::kGForceTop);
  award(points::kGForce * static_cast<std::uint64_t>(gForce_), Feature::TurboTargets);
  if (!courseRunning() && before < tuning::kGForceForDogfight &&
      gForce_ >= tuning::kGForceForDogfight) {
    qualifyCourse(CourseId::Dogfight);
  }
  if (gForce_ >= tuning::kGForceTop) {
    gForce_ = 0;
    award(points::kGForceTop, Feature::TurboTargets);
    say(display::Message("5G  PULL UP"));
    cue(Sound::SetComplete);
  }
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
  if (!std::ranges::all_of(doorMarked_, [](bool marked) { return marked; })) {
    return;
  }
  // All three: a bank toward the lock.
  award(points::kBayDoorsOpen, Feature::BayDoors);
  firstBank_ = false;
  cue(Sound::SetComplete);
  addBank();
}

void Rules::addBank() noexcept {
  // While a multiball runs, or with one ball at a time, the bank opens the
  // Hangar for a shot (a super jackpot, a plain Hangar shot) as it always
  // has; otherwise kBanksPerLock of them light the lock.
  if (++banks_ < banksPerLock_ && multiballEnabled_ && !multiballRunning()) {
    doorMarked_.fill(false);
    static constexpr std::array<display::Message, 3> kMore{
        "BAY DOORS  1 MORE FOR LOCK", "BAY DOORS  2 MORE FOR LOCK", "BAY DOORS  3 MORE FOR LOCK"};
    say(kMore[static_cast<std::size_t>(std::clamp(banksPerLock_ - banks_, 1, 3) - 1)]);
    return;
  }
  banks_ = 0;
  doorsOpen_ = true;
  if (lockNeedsBogey_ && !bogeyCleared_ && multiballEnabled_ && !multiballRunning()) {
    say(display::Message("CLEAR BOGEY FOR LOCK"));
    return;
  }
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
  if (rollover.group == table::RolloverGroup::Flaps) {
    // Stub (G5: completing F-L-A-P-S lights the Mission scoop and +1 bonus X).
    const std::size_t letter =
        std::to_underlying(rollover.id) - std::to_underlying(table::RolloverId::FlapF);
    if (letter < flapsLit_.size()) {
      award(points::kFlapsLetter, Feature::ReturnLanes);
      flapsLit_[letter] = true;
      // At FL300 and above the letters time out.
      const progression::Knobs& knobs = altitude_.knobs();
      flapsSteps_ = knobs.guardsUp ? stepsFor(tuning::kFlapsLetterSeconds * knobs.timerScale) : 0;
      if (std::ranges::all_of(flapsLit_, [](bool lit) { return lit; })) {
        flapsLit_.fill(false);
        flapsSteps_ = 0;
        award(points::kFlapsComplete, Feature::ReturnLanes);
        addBonusMultiplier();
        ++laneSetsThisBall_;
        cue(Sound::SetComplete);
        if (!courseRunning() && !courseLit_ && !graduationRunning() && !wingsRunning() &&
            !allCoursesPassed()) {
          // The pre-flight checklist relights the Mission.
          courseLit_ = true;
          rampsNeeded_ = 0;
          say(display::Message("FLAPS SET  MISSION LIT"));
        } else {
          say(display::Message("FLAPS SET  BONUS X"));
        }
      }
    }
    return;
  }
  if (rollover.group == table::RolloverGroup::TopLane) {
    const std::size_t lane = std::to_underlying(rollover.id);
    if (lane >= rollCallLit_.size()) {
      return;
    }
    if (skill) {
      if (lane == skillLane_) {
        award(points::kSkillShot * ballNumber, Feature::SkillShot, false);
        say(display::Message("SKILL SHOT"));
        cue(Sound::SkillShot);
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
      cue(Sound::SetComplete);
      if (overdrive_ > 1) {
        // Again while it runs: the most there is, and its time over again.
        overdrive_ = kMaxOverdrive;
        overdriveSteps_ = stepsFor(tuning::kOverdriveSeconds);
        say(display::Message("OVERDRIVE 3X"));
      } else {
        say(display::Message("R-A-D COMPLETE"));
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
    cue(Sound::SetComplete);
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
  addTurbo(tuning::kTurboPerSpin);
}

void Rules::onKickback() noexcept {
  if (gameOver_) {
    return;
  }
  // Spent, whether or not the table is live.
  kickbackLit_ = false;
  kickbackRides_ = std::min(kickbackRides_ + 1, static_cast<int>(table::kMaxBalls));
  if (tilted_) {
    return;
  }
  noteSwitch();
  award(points::kKickback, Feature::ReturnLanes);
  say(display::Message("KICKBACK"));
}

void Rules::onKickbackReturned() noexcept { kickbackRides_ = std::max(kickbackRides_ - 1, 0); }

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
  if (!courseRunning()) {
    qualifyCourse(CourseId::LoopDrill);
  }
  majorShot(shot);
  flowShot(shot);
}

void Rules::rampMade(Shot shot) noexcept {
  award(points::kRamp, Feature::Ramps);
  if (shot == Shot::Skyway && superSkillSteps_ > 0) {
    superSkillSteps_ = 0;
    award(points::kSuperSkillShot * static_cast<std::uint64_t>(ball_), Feature::SkillShot, false);
    say(display::Message("SUPER SKILL SHOT"));
    cue(Sound::SkillShot);
  }
  if (!courseRunning()) {
    qualifyCourse(CourseId::RampRuns);
  }
  // Ramps relight the Mission once a sortie is over, and the Air Show
  // after one that was not finished.
  if (!graduationRunning()) {
    if (gradRampsNeeded_ > 0) {
      if (--gradRampsNeeded_ == 0) {
        gradLit_ = true;
        say(display::Message("AIR SHOW IS LIT"));
        cue(Sound::Lit);
      } else {
        say(display::Message("RAMPS FOR AIR SHOW"));
      }
    } else if (rampsNeeded_ > 0 && !courseRunning()) {
      if (--rampsNeeded_ == 0) {
        courseLit_ = true;
        say(display::Message("MISSION IS LIT"));
        cue(Sound::Lit);
      } else {
        say(display::Message("RAMPS FOR MISSION"));
      }
    }
  }
  majorShot(shot);
  flowShot(shot);
  // Every kRampsForDeck ramps light the Flight Deck's diverter (a Skyway
  // the diverter took does not count).
  if (!deckLit_ && ++deckRamps_ >= tuning::kRampsForDeck && !dead()) {
    deckRamps_ = 0;
    deckLit_ = true;
    sayAlso(display::Message("FLIGHT DECK IS LIT"));
  }
}

Rules::HoleResult Rules::onHole(std::size_t index, std::size_t ball) noexcept {
  if (index >= table::kHoles.size()) {
    return HoleResult::Eject;
  }
  // A ball in a hole is not on its way round an arch.
  if (ball < circuits_.size()) {
    circuits_[ball].steps = 0;
  }
  // A ball that came by the CAT SHOT's ride: the Hangar locks it.
  bool cat = false;
  if (table::kHoles[index].type == table::HoleType::Hangar && catBall_ != 0xFF) {
    cat = ball == catBall_;
    if (cat) {
      catBall_ = 0xFF;
    }
  }
  if (dead()) {
    return HoleResult::Eject;
  }
  noteSwitch();
  switch (table::kHoles[index].type) {
    using enum table::HoleType;
    case RampLeft:
      rampMade(Shot::Runway);
      break;
    case RampRight: {
      // The diverter, as lit before this shot (Game reads deckLit() before
      // it tells the rules): the ball goes to the Flight Deck and the lamp
      // goes out.
      const bool toDeck = deckLit();
      if (toDeck && deckLit_) {
        deckLit_ = false;
        deckRamps_ = -1;  // This Skyway does not count toward the next.
      }
      rampMade(Shot::Skyway);
      if (toDeck) {
        sayAlso(display::Message("FLIGHT DECK"));
      }
      break;
    }
    case DeckRamp:
      break;  // Never taken from the field: Game diverts a Skyway to it.
    case CatShot:
      // The ride ends in the Hangar, which Game tells as a Hangar capture of
      // its own: that ball is locked (CAT SHOT spots a lock).
      award(points::kCatShot, Feature::Hangar);
      say(display::Message("CAT SHOT"));
      catBall_ = static_cast<std::uint8_t>(std::min<std::size_t>(ball, 0xFE));
      addTurbo(tuning::kTurboPerCatShot);
      if (multiballEnabled_ && !multiballRunning() && !doorsOpen_) {
        addBank();  // It counts as a bank; lit, its own ball locks.
      }
      break;
    case DeckDrain:
      award(points::kDeckDrain, Feature::Hangar);
      break;
    case Subway: {
      if (graduationRunning()) {
        majorShot(Shot::Briefing);
        break;
      }
      if (wingsRunning()) {
        majorShot(Shot::Briefing);
        break;
      }
      // Collected in this order; the first is what the display says first.
      bool collected = false;
      if (graduationLit() || wingsLit() || briefingLit() || extraBallsLit_ > 0 ||
          extraCreditLit_) {
        // What is collected is said at once, one thing after another.
        message_ = {};
        messageSteps_ = 0;
        waitingCount_ = 0;
      }
      if (graduationLit()) {
        startGraduation();
        collected = true;
      } else if (wingsLit()) {
        startWings();
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
      // The safest shot on the table does not pay for being looped: again
      // within kMissionRepeatSeconds of one that paid, a quarter.
      std::uint64_t value =
          collected || !briefingVisited_ ? points::kBriefingRoom : points::kBriefingRepeat;
      if (!collected && briefingVisited_ && missionRepeatSteps_ > 0) {
        value /= 4;
      }
      award(value, Feature::Briefing);
      missionRepeatSteps_ = stepsFor(tuning::kMissionRepeatSeconds);
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
      if (dogfightJackpotLit()) {
        // DOGFIGHT: the drone hit lit it; the Hangar pays and the next hit
        // lights it again.
        ++hangarHolding_;
        courseSecondPart_ = false;
        award(points::kDogfightJackpot, Feature::Jackpots);
        ++jackpotsCollected_;
        sayAlso(display::Message("DOGFIGHT JACKPOT"));
        cue(Sound::Jackpot);
        courseStep(CourseId::Dogfight);
        majorShot(Shot::Hangar);
        break;
      }
      // A course hears of the shot whatever the Hangar does with the ball
      // (NAVIGATION lights it while the doors are down, which is when it
      // locks). What the Hangar does is said after anything the course says.
      majorShot(Shot::Hangar);
      if (superLit_) {
        ++hangarHolding_;
        superLit_ = false;
        jackpotsTowardSuper_ = 0;
        award(points::kSuperJackpot, Feature::SuperJackpot);
        sayAlso(display::Message("SUPER JACKPOT"));
        cue(Sound::SuperJackpot);
        break;
      }
      award(points::kHangar, Feature::Hangar);
      // CAT SHOT spots a lock: its ball is locked whatever the doors say
      // (it came in behind them), while a lock could be lit at all.
      const bool catLock =
          cat && doorsOpen_ && (bogeyCleared_ || !lockNeedsBogey_) && multiballEnabled_ &&
          !multiballRunning() && !superLit_;
      if (catLock && locksHeld_ < kLocksNeeded) {
        return HoleResult::Lock;
      }
      if (lockLit() || catLock) {
        if (locksHeld_ < kLocksNeeded) {
          // Game says so (onBallLocked) once the ball is in its bay.
          return HoleResult::Lock;
        }
        // The third ball in: all three go.
        bogeyCleared_ = false;
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
        sayAlso(display::Message("SQUADRON MULTIBALL"));
        cue(Sound::MultiballStart);
        checkAirShow();
        return HoleResult::StartMultiball;
      }
      ++hangarHolding_;
      if (!multiballEnabled_) {
        // One ball at a time: the Hangar is shut again behind every ball.
        doorsOpen_ = false;
      }
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
  if (gameOver_) {
    return;
  }
  if (bay < bayFull_.size()) {
    bayFull_[bay] = true;
  }
  locksHeld_ = std::min(locksHeld_ + 1, kLocksNeeded);
  status_.locked = std::min(status_.locked + 1, kLocksNeeded);
  bogeyCleared_ = false;  // Each lock asks for the BOGEY bank again.
  status_.live = std::max(status_.live - 1, 0);
  // Free balls are live balls (setTable keeps it so; this must too).
  status_.free = std::min(status_.free, status_.live);
  // After what the shot that locked it said (a course step, say).
  sayAlso(locksHeld_ == 1 ? display::Message("BALL 1 LOCKED")
                          : display::Message("BALL 2 LOCKED"));
  cue(Sound::BallLocked);
  if (academyMultiballs_ > 0 || locksHeld_ > tuning::kFirstMultiballOpenLocks ||
      altitude_.knobs().guardsUp) {
    // Only the first multiball of a game keeps the Hangar open between locks.
    doorsOpen_ = false;
  }
  if (served) {
    // The player has a new ball to launch, with its own saver.
    addLiveBall();
    saverArmable_ = true;
    saverSteps_ = 0;
    offerSkillShot();
  }
}

void Rules::onLockRefused() noexcept {
  if (gameOver_) {
    return;
  }
  // It is returned by the Hangar like a ball that was never to be locked.
  ++hangarHolding_;
}

void Rules::onLockReleased(std::size_t bay) noexcept {
  if (bay < bayFull_.size()) {
    bayFull_[bay] = false;
  }
  if (releasesPending_ > 0) {
    --releasesPending_;
  } else {
    locksHeld_ = std::max(locksHeld_ - 1, 0);
  }
  status_.locked = std::max(status_.locked - 1, 0);
  addLiveBall();
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
  return doorsOpen_ || superLit_ || dogfightJackpotLit();
}

bool Rules::dogfightJackpotLit() const noexcept {
  return course_ == std::to_underlying(CourseId::Dogfight) && courseSecondPart_ && !dead();
}

bool Rules::lockLit() const noexcept {
  return doorsOpen_ && (bogeyCleared_ || !lockNeedsBogey_) && multiballEnabled_ && !dead() &&
         !multiballRunning() && !superLit_ && !dogfightJackpotLit();
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
  if (!multiballEnabled_ || graduationRunning()) {
    // Graduation Day suspends Turbo multiball.
    turboBallRefused();
    return;
  }
  // A ball is asked for; the table says whether there is room for it (the
  // three-ball limit counts every ball there is, locked and waiting ones
  // too), and the multiball starts when it has been given.
  ++ballRequests_;
  ++turboRequests_;
}

void Rules::turboBallRefused() noexcept {
  award(points::kTurboBonus, Feature::TurboBonus);
  say(display::Message("SCRAMBLE BONUS"));
}

void Rules::turboBallServed() noexcept {
  if (gameOver_) {
    return;
  }
  if (!multiballRunning()) {
    turboMultiball_ = true;
    ++turboMultiballs_;
    jackpotIndex_ = 0;
  }
  graceSteps_ = std::max(graceSteps_, stepsFor(tuning::kTurboGraceSeconds));
  say(display::Message("SCRAMBLE MULTIBALL"));
  cue(Sound::MultiballStart);
  checkAirShow();
}

void Rules::lightOverdrive() noexcept {
  if (overdrive_ == 1 && !overdriveLit_) {
    overdriveLit_ = true;
    sayThen(display::Message("OVERDRIVE IS LIT"));
    cue(Sound::Lit);
  }
}

void Rules::startOverdrive() noexcept {
  overdriveLit_ = false;
  overdrive_ = 2;
  overdriveSteps_ = stepsFor(tuning::kOverdriveSeconds);
  say(display::Message("OVERDRIVE 2X"));
  cue(Sound::Overdrive);
}

void Rules::lightExtraBall() noexcept {
  ++extraBallsLit_;
  sayThen(display::Message("EXTRA BALL IS LIT"));
  cue(Sound::Lit);
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
  // Overdrive lit while it runs, or lit already, would give nothing: then
  // it is not among the awards, and one roll picks from the rest by their
  // weights.
  const bool overdriveOffered = overdrive_ == 1 && !overdriveLit_;
  unsigned total = 0;
  for (const CreditWeight& entry : kCredits) {
    total += entry.credit != Credit::Overdrive || overdriveOffered ? entry.weight : 0U;
  }
  auto roll = static_cast<unsigned>(nextRandom() % total);
  Credit credit = kCredits.front().credit;
  for (const CreditWeight& entry : kCredits) {
    if (entry.credit == Credit::Overdrive && !overdriveOffered) {
      continue;
    }
    if (roll < entry.weight) {
      credit = entry.credit;
      break;
    }
    roll -= entry.weight;
  }
  cue(Sound::ExtraCredit);
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
      sayThen(display::Message("EXTRA CREDIT  SCRAMBLE BOOST"));
      break;
    case Credit::Overdrive:
      sayThen(display::Message("EXTRA CREDIT  OVERDRIVE LIT"));
      overdriveLit_ = true;
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
      // FORMATION PASS: every main shot once.
      if (examMade_[made]) {
        return;
      }
      examMade_[made] = true;
      award(points::kExamShot, Feature::Graduation);
      cue(Sound::Jackpot);
      const int left = examShotsLeft();
      if (left == 0) {
        gradStage_ = GradStage::Commencement;
        finaleBanditDown_ = false;
        droneHits_ = 0;
        say(display::Message("FINALE  KNOCK DOWN BANDIT"));
      } else {
        say(kExamMessages[static_cast<std::size_t>(left - 1)]);
      }
      return;
    }
    case GradStage::Commencement:
      // FINALE: the Bandit down, then the Hangar.
      if (shot == Shot::Hangar && finaleBanditDown_) {
        award(points::kCommencement, Feature::Graduation, false);
        rank_ = kTopRank;
        commencementThisTime_ = true;
        gradStage_ = GradStage::VictoryLaps;
        say(display::Message("AIR SHOW SUPER JACKPOT"));
        sayThen(display::Message("CHANCELLOR"));
        cue(Sound::SuperJackpot);
      }
      return;
    case GradStage::VictoryLaps:
      award(points::kVictoryLap, Feature::Graduation);
      return;
    case GradStage::None:
      break;
  }
  if (wingsRunning()) {
    award(points::kWingsShot, Feature::Graduation);
    say(display::Message("WINGS  2X SHOT"));
    cue(Sound::Jackpot);
    return;
  }

  if (jackpotLit(shot)) {
    jackpot();
  }
  if (!courseRunning()) {
    return;
  }
  switch (static_cast<CourseId>(course_)) {
    case CourseId::RampRuns:
      // The lit ramp, then the other one.
      if (shot == navShot_) {
        courseStep(CourseId::RampRuns);
        navShot_ = navShot_ == Shot::Runway ? Shot::Skyway : Shot::Runway;
      }
      break;
    case CourseId::LoopDrill:
      if (isCircuit(shot)) {
        loopMade(shot);
      }
      break;
    case CourseId::NightLanding:
      if (!courseSecondPart_ && shot == Shot::Runway) {
        // The approach: now the landing, for what the hurry-up has left.
        award(kCourses[course_].shotAward, courseFeature(course_));
        ++courseProgress_;
        courseSecondPart_ = true;
        say(display::Message("ON APPROACH  SHOOT SKYWAY"));
      } else if (courseSecondPart_ && shot == Shot::Skyway) {
        ++courseProgress_;
        passCourse();
      }
      break;
    case CourseId::Formation:
      if (shot == navShot_) {
        courseStep(CourseId::Formation);
        if (course_ == std::to_underlying(CourseId::Formation)) {
          hopNavigation();
        }
      }
      break;
    case CourseId::Gunnery:
    case CourseId::Dogfight:
      break;
  }
}

void Rules::loopMade(Shot shot) noexcept {
  // LOOP DRILL: a loop on the other side from the last pays double.
  const std::uint8_t side = std::to_underlying(shot);
  const std::uint64_t value = kCourses[course_].shotAward;
  award(lastLoop_ != kNoShot && lastLoop_ != side ? 2 * value : value, courseFeature(course_));
  lastLoop_ = side;
  if (++courseProgress_ >= kCourses[course_].goal) {
    passCourse();
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
  cue(Sound::Combo);
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
  return points::kJackpot[std::min(jackpotIndex_, points::kJackpot.size() - 1)] + jackpotAdd_;
}

void Rules::jackpot() noexcept {
  award(jackpotValue(), Feature::Jackpots);
  jackpotAdd_ = 0;
  jackpotIndex_ = std::min(jackpotIndex_ + 1, points::kJackpot.size());
  ++jackpotsCollected_;
  say(display::Message("JACKPOT"));
  cue(Sound::Jackpot);
  if (academyMultiball_ && !superLit_ && ++jackpotsTowardSuper_ >= tuning::kJackpotsForSuper) {
    jackpotsTowardSuper_ = 0;
    superLit_ = true;
    sayThen(display::Message("SUPER JACKPOT IS LIT"));
    cue(Sound::Lit);
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
  if (turboMultiball_) {
    return Multiball::Turbo;
  }
  return dogfightMultiball_ ? Multiball::Dogfight : Multiball::None;
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
  dogfightMultiball_ = false;
  superLit_ = false;
  jackpotIndex_ = 0;
  jackpotAdd_ = 0;
  jackpotsTowardSuper_ = 0;
  graceSteps_ = 0;
  if (announce) {
    say(display::Message("MULTIBALL OVER"));
  }
}

bool Rules::graduationLit() const noexcept {
  return gradLit_ && multiballEnabled_ && !graduationRunning() && !wingsRunning() && !gameOver_;
}

bool Rules::airShowReady() const noexcept {
  return allCoursesPassed() && academyMultiballs_ > 0 && turboMultiballs_ > 0 && aceDone_;
}

void Rules::checkAirShow() noexcept {
  if (gradLit_ || graduationRunning() || gradRampsNeeded_ > 0 || !airShowReady()) {
    return;
  }
  gradLit_ = true;
  sayThen(display::Message("AIR SHOW IS LIT"));
  cue(Sound::Lit);
}

bool Rules::wingsLit() const noexcept {
  return !wingsDone_ && !wingsRunning() && !courseRunning() && !graduationRunning() &&
         !gameOver_ && std::ranges::all_of(courseTried_, [](bool tried) { return tried; });
}

void Rules::startWings() noexcept {
  wingsDone_ = true;
  ++wingsCeremonies_;
  wingsSteps_ = courseStepsFor(kWingsSeconds);
  droneHits_ = 0;
  sayThen(display::Message("WINGS CEREMONY"));
  cue(Sound::CourseStart);
}

void Rules::endWings() noexcept {
  wingsSteps_ = 0;
  say(display::Message("WINGS CEREMONY OVER"));
}

int Rules::examShotsLeft() const noexcept {
  return static_cast<int>(std::ranges::count(examMade_, false));
}

void Rules::startGraduation() noexcept {
  gradLit_ = false;
  gradRampsNeeded_ = 0;
  gradStage_ = examShotsLeft() == 0 ? GradStage::Commencement : GradStage::Exam;
  commencementThisTime_ = false;
  // It takes over from any other multiball, sortie or ceremony.
  turboMultiball_ = false;
  academyMultiball_ = false;
  dogfightMultiball_ = false;
  wingsSteps_ = 0;
  finaleBanditDown_ = false;
  if (courseRunning()) {
    courseOver();
  }
  superLit_ = false;
  doorsOpen_ = false;
  // Every ball there is: the locked ones are let go, the rest are launched.
  if (locksHeld_ > 0) {
    releasesPending_ += locksHeld_;
    locksHeld_ = 0;
    lockCommand_ = LockCommand::Release;
  }
  ballRequests_ += std::max(static_cast<int>(table::kMaxBalls) - ballsInGame(), 0);
  graceSteps_ = stepsFor(tuning::kGraduationGraceSeconds);
  say(display::Message("AIR SHOW"));
  sayThen(display::Message("FORMATION PASS"));
  cue(Sound::MultiballStart);
}

void Rules::endGraduation() noexcept {
  gradStage_ = GradStage::None;
  graceSteps_ = 0;
  jackpotIndex_ = 0;
  jackpotsTowardSuper_ = 0;
  doorsOpen_ = false;
  finaleBanditDown_ = false;
  if (commencementThisTime_) {
    // Done: every sortie can be flown again, for twice the award, and the
    // ceremony earned again.
    commencementThisTime_ = false;
    ++graduations_;
    coursePassed_.fill(false);
    courseTried_.fill(false);
    ready_.fill(false);
    qualify_.fill(0);
    wingsDone_ = false;
    examMade_.fill(false);
    courseBlink_ = 0;
    courseLit_ = true;
    rampsNeeded_ = 0;
    coursesEnded_ = 0;
    say(display::Message("SORTIES REOPENED"));
    climb(progression::kTopLevel);
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
  return (courseLit_ || std::ranges::any_of(ready_, [](bool ready) { return ready; })) &&
         !courseRunning() && !graduationRunning() && !wingsRunning() && !allCoursesPassed() &&
         !gameOver_;
}

int Rules::rampsForBriefing() const noexcept {
  if (gradRampsNeeded_ > 0) {
    return gradRampsNeeded_;
  }
  return courseRunning() || courseLit_ ? 0 : rampsNeeded_;
}

int Rules::qualifyNeeded(std::size_t index) const noexcept {
  const int base = kCourses[index].qualify;
  return coursesStarted_ >= tuning::kSortiesBeforeHarder ? 2 * base : base;
}

int Rules::qualifyRemaining(std::size_t index) const noexcept {
  if (index >= kCourseCount || ready_[index] || coursePassed_[index]) {
    return 0;
  }
  return std::max(qualifyNeeded(index) - qualify_[index], 0);
}

void Rules::qualifyCourse(CourseId id, int amount) noexcept {
  const std::size_t index = std::to_underlying(id);
  if (dead() || coursePassed_[index] || ready_[index] || course_ == index ||
      graduationRunning()) {
    return;
  }
  qualify_[index] = std::min(qualify_[index] + amount, 255);
  if (qualify_[index] < qualifyNeeded(index)) {
    return;
  }
  qualify_[index] = 0;
  ready_[index] = true;
  if (!courseRunning()) {
    courseBlink_ = index;
  }
  sayAlso(kLitMessages[index]);
  cue(Sound::Lit);
}

std::uint32_t Rules::courseStepsFor(double seconds) const noexcept {
  return stepsFor(seconds * altitude_.knobs().timerScale);
}

void Rules::startCourse() noexcept {
  // A qualified sortie first (the one offered, else the next), else the one
  // the Mission offers.
  std::size_t pick = kNoCourse;
  for (std::size_t n = 0; n < kCourseCount && pick == kNoCourse; ++n) {
    const std::size_t index = (courseBlink_ + n) % kCourseCount;
    if (ready_[index] && !coursePassed_[index]) {
      pick = index;
    }
  }
  course_ = pick != kNoCourse ? pick : nextUnpassed(courseBlink_);
  ready_[course_] = false;
  qualify_[course_] = 0;
  courseTried_[course_] = true;
  courseBlink_ = course_;
  courseLit_ = false;
  ++coursesStarted_;
  courseSteps_ = courseStepsFor(kCourses[course_].seconds);
  courseElapsedSteps_ = 0;
  courseProgress_ = 0;
  courseSecondPart_ = false;
  lastLoop_ = kNoShot;
  switch (static_cast<CourseId>(course_)) {
    case CourseId::Formation:
      navShot_ = Shot::CircuitLeft;
      navSteps_ = stepsFor(kNavigationHopSeconds);
      break;
    case CourseId::RampRuns:
      navShot_ = Shot::Runway;
      break;
    case CourseId::Dogfight:
      // A second ball, if the table has room for one.
      if (multiballEnabled_ && ballsInGame() < static_cast<int>(table::kMaxBalls)) {
        ++ballRequests_;
        if (!multiballRunning()) {
          jackpotIndex_ = 0;
        }
        dogfightMultiball_ = true;
        graceSteps_ = std::max(graceSteps_, stepsFor(tuning::kTurboGraceSeconds));
      }
      break;
    case CourseId::LoopDrill:
    case CourseId::Gunnery:
    case CourseId::NightLanding:
      break;
  }
  sayThen(kStartMessages[course_]);
  cue(Sound::CourseStart);
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
  if (course_ != std::to_underlying(CourseId::NightLanding)) {
    return 0;
  }
  const std::uint64_t floor = kCourses[course_].passAward;
  const auto seconds = static_cast<std::uint64_t>(secondsOf(courseElapsedSteps_));
  const std::uint64_t fallen = std::min(seconds * kHurryUpPerSecond, kHurryUpStart - floor);
  return kHurryUpStart - fallen;
}

void Rules::climb(int levels) noexcept {
  // The display names each new flight level once, after what earned it.
  static constexpr std::array<display::Message, progression::kLevels> kClimbed{
      display::Message("FL100"), display::Message("CLIMB TO FL200"),
      display::Message("CLIMB TO FL300"), display::Message("CLIMB TO FL400"),
      display::Message("CLIMB TO FL500  NIGHT FLIGHT")};
  if (dead() || !progressionEnabled_ || altitude_.climb(levels) == 0) {
    return;
  }
  sayThen(kClimbed[static_cast<std::size_t>(altitude_.level())]);
}

void Rules::passCourse() noexcept {
  const std::size_t index = course_;
  std::uint64_t value = index == std::to_underlying(CourseId::NightLanding)
                            ? hurryUpValue()
                            : kCourses[index].passAward;
  if (graduations_ > 0) {
    value *= 2;  // Second time round.
  }
  if (index == std::to_underlying(CourseId::Gunnery) && courseSecondPart_) {
    value *= 2;  // BANDIT DOWN during it.
  }
  award(value, courseFeature(index), false);
  coursePassed_[index] = true;
  ++coursePasses_;
  cue(Sound::CoursePassed);
  say(kPassedMessages[index]);
  if (allCoursesPassed()) {
    sayThen(display::Message("ALL SORTIES FLOWN"));
    if (graduations_ == 0) {
      rank_ = kTopRank - 1;
    }
  } else if (graduations_ == 0) {
    rank_ = std::min(static_cast<std::size_t>(coursesPassed()), kTopRank - 1);
    sayThen(display::Message("RANK UP"));
    cue(Sound::RankUp);
  }
  climb(1);
  if (coursePasses_ == 3) {
    lightExtraBall();
  }
  courseOver();
  checkAirShow();
}

void Rules::failCourse() noexcept {
  say(display::Message("SORTIE OVER"));
  cue(Sound::CourseFailed);
  courseOver();
}

void Rules::courseOver() noexcept {
  course_ = kNoCourse;
  courseSteps_ = 0;
  courseElapsedSteps_ = 0;
  courseProgress_ = 0;
  courseSecondPart_ = false;
  navSteps_ = 0;
  lastLoop_ = kNoShot;
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
    return gradStage_ == GradStage::Exam ? kGraduationName : kFinaleName;
  }
  if (wingsRunning()) {
    return kWingsName;
  }
  return courseRunning() ? kCourses[course_].name : std::string_view{};
}

std::string_view Rules::goalText() const noexcept {
  switch (gradStage_) {
    case GradStage::Exam:
      return kGoalExam;
    case GradStage::Commencement:
      return finaleBanditDown_ ? kGoalCommencement : kGoalFinaleBandit;
    case GradStage::VictoryLaps:
      return kGoalExam;
    case GradStage::None:
      break;
  }
  if (wingsRunning()) {
    return kGoalWings;
  }
  if (courseRunning()) {
    if (course_ == std::to_underlying(CourseId::NightLanding) && courseSecondPart_) {
      return kGoalLanding;
    }
    if (dogfightJackpotLit()) {
      return kGoalDogfightJackpot;
    }
    return kCourses[course_].goalLine;
  }
  if (briefingLit() || graduationLit() || wingsLit()) {
    return kGoalBriefingLit;
  }
  if (allCoursesPassed() && !gradLit_ && gradRampsNeeded_ == 0) {
    // What the Air Show still asks for.
    if (academyMultiballs_ == 0) {
      return kGoalNeedSquadron;
    }
    if (turboMultiballs_ == 0) {
      return kGoalNeedScramble;
    }
    if (!aceDone_) {
      return kGoalNeedAce;
    }
  }
  return kGoalBriefingUnlit;
}

int Rules::goalRemaining() const noexcept {
  if (gradStage_ == GradStage::Exam) {
    return examShotsLeft();
  }
  if (!courseRunning() || graduationRunning()) {
    return 0;
  }
  return std::max(kCourses[course_].goal - courseProgress_, 0);
}

// --- Drains, nudges, time ------------------------------------------------------

bool Rules::onBallLost(const DrainInfo& info) noexcept {
  status_.live = std::max(status_.live - 1, 0);
  // The lost ball was free: never more free balls than live ones.
  status_.free = std::clamp(status_.free - 1, 0, status_.live);
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
    saverSteps_ =
        std::max(saverSteps_, stepsFor(kBallSaveSeconds * altitude_.knobs().ballSaveScale));
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

void Rules::tick(bool allBallsHeld) noexcept {
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
      if (wingsSteps_ > 0 && --wingsSteps_ == 0) {
        endWings();
      }
      if (flapsSteps_ > 0 && --flapsSteps_ == 0) {
        // The checklist timed out: start it again.
        flapsLit_.fill(false);
      }
      if (missionRepeatSteps_ > 0) {
        --missionRepeatSteps_;
      }
      // A sortie's clock stands still through a multiball, except the
      // DOGFIGHT's own.
      const bool frozen = multiballRunning() && course_ != std::to_underlying(CourseId::Dogfight);
      if (courseRunning() && !frozen) {
        ++courseElapsedSteps_;
        if (course_ == std::to_underlying(CourseId::Formation)) {
          // A lit shot that cannot be made any more moves on at once.
          if (!navigable(navShot_) || (navSteps_ > 0 && --navSteps_ == 0)) {
            hopNavigation();
          }
        }
        if (courseSteps_ > 0 && --courseSteps_ == 0) {
          failCourse();
        }
      }
    }
    // Overdrive runs on while a ball waits on the plunger; it stands still
    // only while every ball is held.
    if (!allBallsHeld && overdriveSteps_ > 0 && --overdriveSteps_ == 0) {
      overdrive_ = 1;
      say(display::Message("OVERDRIVE OVER"));
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

LampState Rules::doorLampState(std::size_t door) const noexcept {
  if (gameOver_ || door >= doorMarked_.size()) {
    return LampState::Off;
  }
  if (doorMarked_[door]) {
    return LampState::On;
  }
  // The doors still to hit blink while they stand between a lit lock's
  // worth of shots and nothing else is lit there.
  return LampState::Off;
}

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
        // The Bandit first (the Afterburner lane under it), then the Hangar.
        return shot == (finaleBanditDown_ ? Shot::Hangar : Shot::Turbine) ? LampState::FastBlink
                                                                          : LampState::Off;
      case GradStage::VictoryLaps:
        return LampState::On;
      case GradStage::None:
        break;
    }
    if (wingsRunning()) {
      return LampState::FastBlink;
    }
    if (jackpotLit(shot)) {
      return LampState::Blink;
    }
    if (courseRunning()) {
      switch (static_cast<CourseId>(course_)) {
        case CourseId::Gunnery:
          if (shot == Shot::Turbine) {
            return LampState::Blink;
          }
          break;
        case CourseId::RampRuns:
          if (shot == navShot_) {
            return LampState::Blink;
          }
          break;
        case CourseId::LoopDrill:
          if (isCircuit(shot)) {
            // The other side from the last loop pays double: it blinks fast.
            return lastLoop_ != kNoShot && lastLoop_ != std::to_underlying(shot)
                       ? LampState::FastBlink
                       : LampState::Blink;
          }
          break;
        case CourseId::NightLanding:
          if (shot == (courseSecondPart_ ? Shot::Skyway : Shot::Runway)) {
            return LampState::FastBlink;
          }
          break;
        case CourseId::Formation:
          if (shot == navShot_) {
            return LampState::Blink;
          }
          break;
        case CourseId::Dogfight:
          if (shot == Shot::Hangar && courseSecondPart_) {
            return LampState::FastBlink;
          }
          if (shot == Shot::Turbine && !courseSecondPart_) {
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
        if (graduationLit() || wingsLit()) {
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
    if (ready_[index]) {
      return LampState::Blink;
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
      return lit(bayFull_[0]);
    case Lock2:
      return lit(bayFull_[1]);
    case Course1:
    case Course2:
    case Course3:
    case Course4:
    case Course5:
    case Course6:
      return courseLamp(static_cast<std::size_t>(std::to_underlying(lamp) -
                                                 std::to_underlying(Course1)));
    case Graduation:
      if (graduationRunning() || wingsRunning()) {
        return LampState::FastBlink;
      }
      if (graduationLit() || wingsLit()) {
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
      return kickbackRides_ > 0 ? LampState::FastBlink : lit(kickbackLit_);
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
    // Revision 3's ladders (TABLE_REV3.md 3.10). Stubs from what the rules
    // keep today; the rules (phase 3) refine them.
    case DroneArmour1:
    case DroneArmour2:
    case DroneArmour3:
    case DroneArmour4:
    case DroneArmour5: {
      // One lamp a hit taken, the next to win blinking (the fifth stands for
      // any beyond); all five blink fast once the finale's Bandit is down.
      if (finaleHangarLit()) {
        return LampState::FastBlink;
      }
      const int k = std::to_underlying(lamp) - std::to_underlying(DroneArmour1);
      const int hits = std::min(droneHits_, 4);
      if (k < hits) {
        return LampState::On;
      }
      return k == hits && droneHits_ > 0 ? LampState::Blink : LampState::Off;
    }
    case GForce1:
    case GForce2:
    case GForce3:
    case GForce4:
    case GForce5:
      {
        const int g = std::to_underlying(lamp) - std::to_underlying(GForce1) + 1;
        if (g <= gForce_) {
          return LampState::On;
        }
        // The next G blinks once the meter has started.
        return g == gForce_ + 1 && gForce_ > 0 ? LampState::Blink : LampState::Off;
      }
    case TaxiT:
    case TaxiA:
    case TaxiX:
    case TaxiI:
      return lit(taxiLit_[static_cast<std::size_t>(std::to_underlying(lamp) -
                                                   std::to_underlying(TaxiT))]);
    case Rank1:
    case Rank2:
    case Rank3:
    case Rank4:
    case Rank5:
    case Rank6:
    case Rank7: {
      // Every rank won, held; none at the first.
      const auto k = static_cast<std::size_t>(std::to_underlying(lamp) - std::to_underlying(Rank1));
      return lit(k < rank_);
    }
    case Combo2X:
    case Combo3X:
    case Combo4X:
    case Combo5X: {
      // The chain's level, the next link blinking while it runs.
      const int level = comboLevel();
      const int x = std::to_underlying(lamp) - std::to_underlying(Combo2X) + 2;
      if (x <= level) {
        return LampState::On;
      }
      return level > 0 && x == level + 1 ? LampState::Blink : LampState::Off;
    }
    case Bonus2X:
    case Bonus3X:
    case Bonus4X:
    case Bonus5X: {
      const int x = std::to_underlying(lamp) - std::to_underlying(Bonus2X) + 2;
      return lit(x <= bonusMultiplier_);
    }
    // Concept A (G6): a first pass from the public queries alone, for the
    // rules (G5) to replace.
    case LadderCircuitLeft1:
    case LadderCircuitLeft2:
    case LadderCircuitLeft3:
    case LadderRunway1:
    case LadderRunway2:
    case LadderRunway3:
    case LadderHangar1:
    case LadderHangar2:
    case LadderHangar3:
    case LadderBriefing1:
    case LadderBriefing2:
    case LadderBriefing3:
    case LadderTurbine1:
    case LadderTurbine2:
    case LadderTurbine3:
    case LadderSkyway1:
    case LadderSkyway2:
    case LadderSkyway3:
    case LadderCircuitRight1:
    case LadderCircuitRight2:
    case LadderCircuitRight3:
    case AltitudeFL100:
    case AltitudeFL200:
    case AltitudeFL300:
    case AltitudeFL400:
    case AltitudeFL500:
    case FlapsF:
    case FlapsL:
    case FlapsA:
    case FlapsP:
    case FlapsS:
    case Bogey1:
    case Bogey2:
    case Bogey3:
    case Radar1:
    case Radar2:
    case Radar3:
    case SafetyPost:
    case Multiball:
    case DeckLit:
    case DeckA:
    case DeckC:
    case DeckE:
    case CatShot:
      return conceptLampState(*this, lamp);
    case Count:
      break;
  }
  return LampState::Off;
}

LampState conceptLampState(const Rules& rules, table::LampId lamp) noexcept {
  using enum table::LampId;
  if (rules.gameOver()) {
    return LampState::Off;
  }
  const auto lit = [](bool on) { return on ? LampState::On : LampState::Off; };
  const auto index = [lamp](table::LampId first) {
    return static_cast<std::size_t>(std::to_underlying(lamp) - std::to_underlying(first));
  };
  if (table::isPipLamp(lamp)) {
    // A shot's ladder: while its arrow blinks, every pip blinks with it (the
    // light show runs them up toward the arrow); otherwise the pips count
    // the shot's progress toward its next award, the first nearest the arrow.
    const std::size_t pips = index(LadderCircuitLeft1);
    const std::size_t arrow = pips / table::kShotLadderPips;
    const std::size_t k = pips % table::kShotLadderPips;
    const LampState arrowState = rules.lampState(static_cast<table::LampId>(arrow));
    if (arrowState == LampState::Blink || arrowState == LampState::FastBlink) {
      return arrowState;
    }
    std::size_t progress = 0;
    switch (static_cast<Shot>(arrow)) {
      case Shot::CircuitLeft:  // The R-A-D lanes it feeds.
        for (std::size_t lane = 0; lane < 3; ++lane) {
          progress += rules.rollCallLit(lane) ? 1U : 0U;
        }
        break;
      case Shot::CircuitRight:  // W-I-N-G.
        for (std::size_t letter = 0; letter < 4; ++letter) {
          progress += rules.wingLit(letter) ? 1U : 0U;
        }
        break;
      case Shot::Runway:
      case Shot::Skyway:  // Ramps made toward relighting the Mission.
        progress = rules.rampsForBriefing() > 0
                       ? static_cast<std::size_t>(
                             std::max(0, tuning::kRampsForBriefingLater - rules.rampsForBriefing()))
                       : 3U;
        break;
      case Shot::Hangar:  // The bay doors marked.
        for (std::size_t door = 0; door < table::kDropTargets.size(); ++door) {
          progress += rules.doorMarked(door) ? 1U : 0U;
        }
        break;
      case Shot::Turbine:  // The turbo meter, in thirds.
        progress = rules.turboGoal() > 0 ? static_cast<std::size_t>(3 * rules.turboMeter() /
                                                                    rules.turboGoal())
                                         : 0U;
        break;
      case Shot::Briefing: {  // F-L-A-P-S lights the Mission.
        std::size_t letters = 0;
        for (std::size_t letter = 0; letter < 5; ++letter) {
          letters += rules.flapLit(letter) ? 1U : 0U;
        }
        progress = letters * 3 / 5;
        break;
      }
    }
    return lit(k < progress);
  }
  if (lamp >= AltitudeFL100 && lamp <= AltitudeFL500) {
    // The altimeter: every level reached.
    return lit(static_cast<int>(index(AltitudeFL100)) <= rules.altitude());
  }
  if (lamp >= FlapsF && lamp <= FlapsS) {
    return lit(rules.flapLit(index(FlapsF)));
  }
  switch (lamp) {
    case Multiball:
      if (rules.multiball() != pinball::Multiball::None) {
        return LampState::On;
      }
      return rules.lockLit() ? LampState::Blink : lit(rules.locksHeld() > 0);
    case DeckLit:
      return rules.deckLit() ? LampState::Blink : LampState::Off;
    case CatShot:
      // CAT SHOT rides to the lock.
      return rules.lockLit() ? LampState::Blink : LampState::Off;
    default:
      // BOGEY, RADAR and the Safety Post wait for G2's targets; the deck's
      // A-C-E letters for the rules (G5). Unlit, the light show runs them.
      break;
  }
  return LampState::Off;
}

}  // namespace pinball
