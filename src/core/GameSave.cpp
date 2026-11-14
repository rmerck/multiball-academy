// A game in progress taken down as plain data (core/Snapshot.hpp) and put
// back: the rules, the game, and the physics, each with its own part.
#include <box2d/box2d.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include "core/Game.hpp"
#include "core/Physics.hpp"
#include "core/Rules.hpp"
#include "core/Snapshot.hpp"

namespace pinball {

static_assert(save::kPixelsPerMeter == PhysicsWorld::kPixelsPerMeter);
static_assert(save::kMaxBallSpeedPixels ==
              PhysicsWorld::kMaxBallSpeed * PhysicsWorld::kPixelsPerMeter);
static_assert(save::kMaxPlungerSpeedPixels ==
              PhysicsWorld::kPlungerMaxSpeed * PhysicsWorld::kPixelsPerMeter);

// --- Rules ---------------------------------------------------------------------------

save::RulesRecord Rules::record() const noexcept {
  save::RulesRecord r;
  r.score = score_;
  r.featureScore = featureScore_;
  r.ball = ball_;
  r.tilted = tilted_;
  r.tiltHeat = tiltHeat_;
  r.ballInPlay = ballInPlay_;
  r.rank = static_cast<std::uint8_t>(rank_);
  r.statusLive = status_.live;
  r.statusFree = status_.free;
  r.statusQueued = status_.queued;
  r.statusLocked = status_.locked;
  r.ballRequests = ballRequests_;
  r.requestsInFlight = requestsInFlight_;
  r.turboRequests = turboRequests_;
  r.turboRequestsInFlight = turboRequestsInFlight_;
  r.lockCommand = std::to_underlying(lockCommand_);
  r.multiballEnabled = multiballEnabled_;
  r.ballSaveEnabled = ballSaveEnabled_;

  r.saverArmable = saverArmable_;
  r.saverSteps = saverSteps_;
  r.graceSteps = graceSteps_;
  r.extraBallsLit = extraBallsLit_;
  r.extraBallsHeld = extraBallsHeld_;
  r.extraBallsAwarded = extraBallsAwarded_;

  r.skillOffered = skillOffered_;
  r.skillLane = static_cast<std::uint8_t>(skillLane_);
  r.superSkillSteps = superSkillSteps_;

  r.rollCallLit = rollCallLit_;
  r.wingLit = wingLit_;
  r.turboTargetLit = turboTargetLit_;
  r.kickbackLit = kickbackLit_;
  r.kickbackArmSteps = kickbackArmSteps_;
  r.kickbackRides = kickbackRides_;
  r.extraCreditLit = extraCreditLit_;

  r.turboMeter = turboMeter_;
  r.turboGoal = turboGoal_;

  r.doorMarked = doorMarked_;
  r.doorsOpen = doorsOpen_;
  r.firstBank = firstBank_;
  r.doorsDown = doorsDown_;
  r.doorCommand = std::to_underlying(doorCommand_);
  r.doorRaiseSteps = doorRaiseSteps_;
  r.hangarHolding = hangarHolding_;
  r.locksHeld = locksHeld_;
  r.bayFull = bayFull_;
  r.releasesPending = releasesPending_;

  r.turboMultiball = turboMultiball_;
  r.academyMultiball = academyMultiball_;
  r.jackpotIndex = static_cast<std::uint8_t>(jackpotIndex_);
  r.jackpotsTowardSuper = jackpotsTowardSuper_;
  r.superLit = superLit_;
  r.turboMultiballs = turboMultiballs_;
  r.academyMultiballs = academyMultiballs_;
  r.jackpotsCollected = jackpotsCollected_;

  r.comboShot = comboShot_;
  r.comboChain = comboChain_;
  r.comboSteps = comboSteps_;
  r.fiveComboMade = fiveComboMade_;

  r.overdriveLit = overdriveLit_;
  r.overdrive = overdrive_;
  r.overdriveSteps = overdriveSteps_;

  for (std::size_t i = 0; i < circuits_.size(); ++i) {
    r.circuits[i] = {circuits_[i].side, circuits_[i].steps};
  }

  r.coursePassed = coursePassed_;
  r.course = static_cast<std::uint8_t>(course_);
  r.courseBlink = static_cast<std::uint8_t>(courseBlink_);
  r.courseLit = courseLit_;
  r.rampsNeeded = rampsNeeded_;
  r.coursesEnded = coursesEnded_;
  r.coursesStarted = coursesStarted_;
  r.coursePasses = coursePasses_;
  r.courseSteps = courseSteps_;
  r.courseElapsedSteps = courseElapsedSteps_;
  r.courseProgress = courseProgress_;
  r.courseSecondPart = courseSecondPart_;
  r.navShot = std::to_underlying(navShot_);
  r.navSteps = navSteps_;

  r.gradStage = std::to_underlying(gradStage_);
  r.gradLit = gradLit_;
  r.gradRampsNeeded = gradRampsNeeded_;
  r.examMade = examMade_;
  r.graduations = graduations_;
  r.commencementThisTime = commencementThisTime_;

  r.briefingVisited = briefingVisited_;
  r.combosThisBall = combosThisBall_;
  r.laneSetsThisBall = laneSetsThisBall_;
  r.switchesThisBall = switchesThisBall_;
  r.bonusMultiplier = bonusMultiplier_;
  r.bonusTally = bonusTally_;

  r.random = random_;
  return r;
}

void Rules::restore(const save::RulesRecord& r) noexcept {
  score_ = r.score;
  featureScore_ = r.featureScore;
  ball_ = r.ball;
  // Only a game in progress is ever saved.
  gameOver_ = false;
  tilted_ = r.tilted;
  tiltHeat_ = r.tiltHeat;
  ballInPlay_ = r.ballInPlay;
  rank_ = std::min<std::size_t>(r.rank, kRankCount - 1);
  status_ = {.live = r.statusLive,
             .free = r.statusFree,
             .queued = r.statusQueued,
             .locked = r.statusLocked};
  ballRequests_ = r.ballRequests;
  requestsInFlight_ = r.requestsInFlight;
  turboRequests_ = r.turboRequests;
  turboRequestsInFlight_ = r.turboRequestsInFlight;
  lockCommand_ = r.lockCommand == std::to_underlying(LockCommand::Release) ? LockCommand::Release
                                                                           : LockCommand::None;
  multiballEnabled_ = r.multiballEnabled;
  ballSaveEnabled_ = r.ballSaveEnabled;

  saverArmable_ = r.saverArmable;
  saverSteps_ = r.saverSteps;
  graceSteps_ = r.graceSteps;
  extraBallsLit_ = r.extraBallsLit;
  extraBallsHeld_ = r.extraBallsHeld;
  extraBallsAwarded_ = r.extraBallsAwarded;

  skillOffered_ = r.skillOffered;
  skillLane_ = std::min<std::size_t>(r.skillLane, 2);
  superSkillSteps_ = r.superSkillSteps;

  rollCallLit_ = r.rollCallLit;
  wingLit_ = r.wingLit;
  turboTargetLit_ = r.turboTargetLit;
  kickbackLit_ = r.kickbackLit;
  kickbackArmSteps_ = r.kickbackArmSteps;
  kickbackRides_ = r.kickbackRides;
  extraCreditLit_ = r.extraCreditLit;

  turboMeter_ = r.turboMeter;
  turboGoal_ = std::max(r.turboGoal, 1);

  doorMarked_ = r.doorMarked;
  doorsOpen_ = r.doorsOpen;
  firstBank_ = r.firstBank;
  doorsDown_ = r.doorsDown;
  doorCommand_ = r.doorCommand <= std::to_underlying(DoorCommand::Raise)
                     ? static_cast<DoorCommand>(r.doorCommand)
                     : DoorCommand::None;
  doorRaiseSteps_ = r.doorRaiseSteps;
  hangarHolding_ = r.hangarHolding;
  locksHeld_ = r.locksHeld;
  bayFull_ = r.bayFull;
  releasesPending_ = r.releasesPending;

  turboMultiball_ = r.turboMultiball;
  academyMultiball_ = r.academyMultiball;
  jackpotIndex_ = std::min<std::size_t>(r.jackpotIndex, points::kJackpot.size());
  jackpotsTowardSuper_ = r.jackpotsTowardSuper;
  superLit_ = r.superLit;
  turboMultiballs_ = r.turboMultiballs;
  academyMultiballs_ = r.academyMultiballs;
  jackpotsCollected_ = r.jackpotsCollected;

  comboShot_ = r.comboShot < kShotCount ? r.comboShot : kNoShot;
  comboChain_ = r.comboChain;
  comboSteps_ = r.comboSteps;
  fiveComboMade_ = r.fiveComboMade;

  overdriveLit_ = r.overdriveLit;
  overdrive_ = r.overdrive;
  overdriveSteps_ = r.overdriveSteps;

  for (std::size_t i = 0; i < circuits_.size(); ++i) {
    circuits_[i] = {static_cast<std::uint8_t>(r.circuits[i].side & 1U), r.circuits[i].steps};
  }

  coursePassed_ = r.coursePassed;
  course_ = std::min<std::size_t>(r.course, kNoCourse);
  courseBlink_ = std::min<std::size_t>(r.courseBlink, kCourseCount - 1);
  courseLit_ = r.courseLit;
  rampsNeeded_ = r.rampsNeeded;
  coursesEnded_ = r.coursesEnded;
  coursesStarted_ = r.coursesStarted;
  coursePasses_ = r.coursePasses;
  courseSteps_ = r.courseSteps;
  courseElapsedSteps_ = r.courseElapsedSteps;
  courseProgress_ = r.courseProgress;
  courseSecondPart_ = r.courseSecondPart;
  navShot_ = static_cast<Shot>(std::min<std::size_t>(r.navShot, kShotCount - 1));
  navSteps_ = r.navSteps;

  gradStage_ = static_cast<GradStage>(std::min<std::uint8_t>(r.gradStage, 3));
  gradLit_ = r.gradLit;
  gradRampsNeeded_ = r.gradRampsNeeded;
  examMade_ = r.examMade;
  graduations_ = r.graduations;
  commencementThisTime_ = r.commencementThisTime;

  briefingVisited_ = r.briefingVisited;
  combosThisBall_ = r.combosThisBall;
  laneSetsThisBall_ = r.laneSetsThisBall;
  switchesThisBall_ = r.switchesThisBall;
  bonusMultiplier_ = r.bonusMultiplier;
  bonusTally_ = r.bonusTally;

  random_ = r.random != 0 ? r.random : 0xD1B54A32D192ED03ULL;

  // What was showing is not kept: the display starts clear.
  message_ = {};
  messageSteps_ = 0;
  messageFresh_ = false;
  waitingCount_ = 0;
  sounds_ = 0;
}

// --- Physics -------------------------------------------------------------------------

namespace {

constexpr float toMeters(float pixels) noexcept { return pixels / PhysicsWorld::kPixelsPerMeter; }
constexpr b2Vec2 toMeters(Vec2 p) noexcept { return {toMeters(p.x), toMeters(p.y)}; }

}  // namespace

void PhysicsWorld::saveBall(std::size_t slot, save::BallRecord& r) const {
  if (!ballActive(slot)) {
    return;
  }
  const BallSlot& ball = balls_[slot];
  r.active = true;
  const b2Pos p = b2Body_GetPosition(ball.body);
  r.x = static_cast<double>(p.x);
  r.y = static_cast<double>(p.y);
  const b2Vec2 v = b2Body_GetLinearVelocity(ball.body);
  r.vx = v.x;
  r.vy = v.y;
  const b2Rot q = b2Body_GetRotation(ball.body);
  r.rotationCos = q.c;
  r.rotationSin = q.s;
  r.spin = b2Body_GetAngularVelocity(ball.body);
  r.held = ball.held;
  r.heldBy = ball.held ? ball.heldBy : table::ElementRef{};
  r.sensorFromKnown = ball.sensorFromKnown;
  r.sensorFrom = ball.sensorFrom;
  r.touchingBumper = ball.touchingBumper;
  r.onRollover = ball.onRollover;
  r.holeArmed = ball.holeArmed;
}

void PhysicsWorld::saveTable(save::TableRecord& r) const {
  for (std::size_t i = 0; i < flippers_.size(); ++i) {
    r.flipperAngles[i] = flippers_[i].angle;
  }
  r.plungerPull = plungerPull_;
  r.plungerReleaseSpeed = plungerReleaseSpeed_;
  r.doorBankUp = doorBankUp_;
  r.kickbackArmed = kickbackArmed_;
}

void PhysicsWorld::restoreBalls(std::span<const save::BallRecord> records) {
  for (std::size_t slot = 0; slot < balls_.size(); ++slot) {
    deactivateBall(slot);
    BallSlot& ball = balls_[slot];
    ball.sensorFromKnown = false;
    clearLatches(slot);
    if (slot >= records.size() || !records[slot].active) {
      continue;
    }
    const save::BallRecord& r = records[slot];
    b2Body_Enable(ball.body);
    ball.active = true;
    ball.held = false;
    b2Pos position{};
    position.x = static_cast<decltype(position.x)>(r.x);
    position.y = static_cast<decltype(position.y)>(r.y);
    // Box2D wants a unit rotation; a saved one is, to within rounding.
    b2Rot rotation{r.rotationCos, r.rotationSin};
    if (!b2IsNormalizedRot(rotation)) {
      rotation = b2NormalizeRot(rotation);
    }
    b2Body_SetTransform(ball.body, position, rotation);
    b2Body_SetLinearVelocity(ball.body, {r.vx, r.vy});
    clampBallSpeed(slot);
    b2Body_SetAngularVelocity(ball.body, r.spin);
    b2Body_SetAwake(ball.body, true);
    ball.sensorFromKnown = r.sensorFromKnown;
    ball.sensorFrom = r.sensorFrom;
    ball.touchingBumper = r.touchingBumper;
    ball.onRollover = r.onRollover;
    ball.holeArmed = r.holeArmed;
    if (r.held) {
      // As holdBall leaves it: still, and out of every collision.
      ball.held = true;
      ball.heldBy = r.heldBy;
      b2Body_SetLinearVelocity(ball.body, {0.0F, 0.0F});
      b2Body_SetAngularVelocity(ball.body, 0.0F);
      b2Body_Disable(ball.body);
    }
  }
  events_.clear();
  skipTouches_ = true;
}

void PhysicsWorld::restoreTable(const save::TableRecord& r) {
  for (std::size_t i = 0; i < flippers_.size(); ++i) {
    const table::Flipper& layout = table::kFlippers[i];
    FlipperState& flipper = flippers_[i];
    flipper.angle = std::clamp(r.flipperAngles[i], std::min(layout.restAngle, layout.activeAngle),
                               std::max(layout.restAngle, layout.activeAngle));
    flipper.held = false;
    b2Body_SetTransform(flipper.body, b2ToPos(toMeters(layout.pivot)), b2MakeRot(flipper.angle));
    b2Body_SetLinearVelocity(flipper.body, {0.0F, 0.0F});
    b2Body_SetAngularVelocity(flipper.body, 0.0F);
  }
  plungerHeld_ = false;
  plungerPull_ = std::clamp(r.plungerPull, 0.0F, table::kPlungerTravel);
  plungerReleaseSpeed_ = std::max(r.plungerReleaseSpeed, 0.0F);
  const Vec2 top{table::kLaneCenterX, table::kPlungerRestTopY + plungerPull_};
  b2Body_SetTransform(plunger_, b2ToPos(toMeters(top)), b2MakeRot(0.0F));
  b2Body_SetLinearVelocity(plunger_, {0.0F, 0.0F});
  setDoorBank(r.doorBankUp);
  kickbackArmed_ = r.kickbackArmed;
}

// --- Game ----------------------------------------------------------------------------

save::Snapshot Game::snapshot() const {
  save::Snapshot s;
  s.rules = rules_.record();

  save::GameRecord& g = s.game;
  g.stepCount = stepCount_;
  g.drainCount = drainCount_;
  g.bumperHits = bumperHits_;
  g.rolloverCounts = rolloverCounts_;
  g.holeCounts = holeCounts_;
  g.lastReleaseStep = lastReleaseStep_;
  g.ballSearchCount = ballSearchCount_;
  g.arrivals = arrivals_;
  g.hangarBurst = hangarBurst_;
  g.doorRaisePending = doorRaisePending_;
  g.lockCount = lockCount_;
  g.lockReleaseCount = lockReleaseCount_;
  g.kickbackCount = kickbackCount_;
  g.spinsOwed = spinsOwed_;
  g.spinCooldown = spinCooldown_;
  g.spinCount = spinCount_;
  g.spinDirection = spinDirection_;
  g.spinnerTicks = spinnerTicks_;
  g.random = random_;
  g.queuedBalls = static_cast<std::uint8_t>(queuedBalls_);
  g.lastAutoServeStep = lastAutoServeStep_;
  g.autoLaunchCount = autoLaunchCount_;
  g.autoRelaunchCount = autoRelaunchCount_;

  physics_.saveTable(s.table);

  for (std::size_t slot = 0; slot < kMaxBalls; ++slot) {
    save::BallRecord& r = s.balls[slot];
    if (!physics_.ballActive(slot)) {
      continue;  // An empty slot is the default record.
    }
    physics_.saveBall(slot, r);
    const BallState& ball = balls_[slot];
    r.holder = ball.holder;
    r.heldSteps = ball.heldSteps;
    r.wasHeld = ball.wasHeld;
    r.arrival = ball.arrival;
    r.lockBay = ball.lockBay;
    r.releasing = ball.releasing;
    r.ejectStep = ball.ejectStep;
    r.searchAnchor = ball.searchAnchor;
    r.stillSteps = ball.stillSteps;
    r.inPlay = ball.inPlay;
    r.takenAt = ball.takenAt;
    r.autoLaunch = ball.autoLaunch;
    r.launchSteps = ball.launchSteps;
    r.launched = ball.launched;
  }
  return s;
}

bool Game::restore(const save::Snapshot& s) {
  if (!save::valid(s)) {
    return false;
  }
  rules_.restore(s.rules);

  const save::GameRecord& g = s.game;
  stepCount_ = g.stepCount;
  drainCount_ = g.drainCount;
  bumperHits_ = g.bumperHits;
  rolloverCounts_ = g.rolloverCounts;
  holeCounts_ = g.holeCounts;
  lastReleaseStep_ = g.lastReleaseStep;
  ballSearchCount_ = g.ballSearchCount;
  arrivals_ = g.arrivals;
  hangarBurst_ = g.hangarBurst;
  doorRaisePending_ = g.doorRaisePending;
  lockCount_ = g.lockCount;
  lockReleaseCount_ = g.lockReleaseCount;
  kickbackCount_ = g.kickbackCount;
  spinsOwed_ = g.spinsOwed;
  spinCooldown_ = g.spinCooldown;
  spinCount_ = g.spinCount;
  spinDirection_ = g.spinDirection < 0 ? -1 : 1;
  spinnerTicks_ = g.spinnerTicks;
  random_ = g.random;
  queuedBalls_ = g.queuedBalls;
  lastAutoServeStep_ = g.lastAutoServeStep;
  autoLaunchCount_ = g.autoLaunchCount;
  autoRelaunchCount_ = g.autoRelaunchCount;

  physics_.restoreTable(s.table);
  physics_.restoreBalls(s.balls);

  for (std::size_t slot = 0; slot < kMaxBalls; ++slot) {
    const save::BallRecord& r = s.balls[slot];
    BallState& ball = balls_[slot];
    ball = {};
    if (!r.active) {
      continue;
    }
    ball.holder = r.holder;
    ball.heldSteps = r.heldSteps;
    ball.wasHeld = r.wasHeld;
    ball.arrival = r.arrival;
    ball.lockBay = r.lockBay;
    ball.releasing = r.releasing;
    ball.ejectStep = r.ejectStep;
    ball.searchAnchor = r.searchAnchor;
    ball.stillSteps = r.stillSteps;
    ball.inPlay = r.inPlay;
    ball.takenAt = r.takenAt;
    ball.autoLaunch = r.autoLaunch;
    ball.launchSteps = r.launchSteps;
    ball.launched = r.launched;
  }

  // Only for the moment: nothing flashes, shakes or sounds, no button is
  // held, and nothing is drawn between an old place and the new.
  previousInput_ = {};
  bumperFlash_.fill(0.0F);
  slingshotFlash_.fill(0.0F);
  shake_ = {};
  sounds_ = 0;
  gameEventCount_ = 0;
  snapshotForDrawing();
  for (BallState& ball : balls_) {
    ball.placed = false;
  }
  started_ = true;
  ++gamesBegun_;
  return true;
}

}  // namespace pinball
