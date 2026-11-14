#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "core/Courses.hpp"
#include "core/DisplayText.hpp"
#include "core/Sound.hpp"
#include "core/TableLayout.hpp"

namespace pinball {

// Point values. Every award is multiplied by overdrive unless it says "as
// stated".
namespace points {
inline constexpr std::uint64_t kSlingshot = 250;
inline constexpr std::uint64_t kSpin = 500;
inline constexpr std::uint64_t kBumper = 1'000;
inline constexpr std::uint64_t kReturnLane = 1'000;  // Each W-I-N-G lane.
inline constexpr std::uint64_t kBayDoor = 2'000;
inline constexpr std::uint64_t kBayDoorAgain = 500;  // A door already marked.
inline constexpr std::uint64_t kBayDoorsOpen = 10'000;
inline constexpr std::uint64_t kTarget = 2'500;
inline constexpr std::uint64_t kBothTargets = 10'000;
inline constexpr std::uint64_t kRollCallLane = 3'000;
inline constexpr std::uint64_t kRollCallComplete = 15'000;
inline constexpr std::uint64_t kWingComplete = 20'000;
inline constexpr std::uint64_t kBriefingRoom = 5'000;
inline constexpr std::uint64_t kBriefingRepeat = 1'000;  // Again on a ball, nothing lit.
inline constexpr std::uint64_t kRightOutlane = 5'000;
inline constexpr std::uint64_t kKickback = 5'000;
inline constexpr std::uint64_t kRamp = 6'000;
inline constexpr std::uint64_t kCircuit = 10'000;
inline constexpr std::uint64_t kHangar = 10'000;
// The second to the fifth shot of a combo chain.
inline constexpr std::array<std::uint64_t, 4> kCombo{20'000, 40'000, 80'000, 150'000};
// As stated, times the ball number.
inline constexpr std::uint64_t kSkillShot = 25'000;
inline constexpr std::uint64_t kSkillShotOtherLane = 5'000;
inline constexpr std::uint64_t kSuperSkillShot = 75'000;
inline constexpr std::uint64_t kTurboBonus = 25'000;
// The jackpot ladder: the last value repeats.
inline constexpr std::array<std::uint64_t, 4> kJackpot{50'000, 75'000, 100'000, 150'000};
inline constexpr std::uint64_t kSuperJackpot = 500'000;
// An extra ball that cannot be given (as stated).
inline constexpr std::uint64_t kExtraBallBeyondLimit = 100'000;
inline constexpr std::uint64_t kExamShot = 100'000;
inline constexpr std::uint64_t kVictoryLap = 50'000;
inline constexpr std::uint64_t kCommencement = 1'000'000;  // As stated.
// End-of-ball bonus, as stated, times the bonus multiplier.
inline constexpr std::uint64_t kBonusCourse = 20'000;
inline constexpr std::uint64_t kBonusLock = 10'000;
inline constexpr std::uint64_t kBonusCombo = 5'000;
inline constexpr std::uint64_t kBonusLaneSet = 5'000;
inline constexpr std::uint64_t kBonusSwitch = 250;
inline constexpr int kBonusSwitchCap = 400;
inline constexpr std::uint64_t kMaxScore = 999'999'999'999ULL;
}  // namespace points

// The numbers that set the pace of a game. Tuned against the rule rows of
// the acceptance suite (tests/AcceptanceMain.cpp).
namespace tuning {
// The turbo meter fills at this, then at this much more each time.
inline constexpr int kTurboMeterFirst = 50;
inline constexpr int kTurboMeterStep = 25;
inline constexpr int kTurboPerBumper = 1;
inline constexpr int kTurboPerSpin = 1;
inline constexpr int kTurboPerTarget = 5;
inline constexpr int kTurboExtraCredit = 15;
// Ramp shots that relight the Briefing room after a course, and after the
// third course of a game.
inline constexpr int kRampsForBriefing = 2;
inline constexpr int kRampsForBriefingLater = 3;
inline constexpr int kRampsForGraduation = 3;
// For the first bank of a game any three door hits open the Hangar: a hit
// on a door already marked marks the next one.
inline constexpr bool kFirstBankAnyThreeHits = false;
inline constexpr int kJackpotsForSuper = 3;
inline constexpr double kBallSaveSeconds = 15.0;
inline constexpr double kExtraCreditSaveSeconds = 10.0;
inline constexpr double kTurboGraceSeconds = 10.0;
inline constexpr double kAcademyGraceSeconds = 15.0;
inline constexpr double kGraduationGraceSeconds = 30.0;
inline constexpr double kComboSeconds = 4.0;
inline constexpr double kOverdriveSeconds = 20.0;
inline constexpr double kSuperSkillSeconds = 5.0;
// A circuit: the mouth crossed going up at this speed or more, then the top
// of the arch or a roll call lane by the same ball within this long.
inline constexpr float kCircuitMinSpeed = 900.0F;
inline constexpr double kCircuitSeconds = 2.5;
}  // namespace tuning

// How a ball came to drain.
struct DrainInfo {
  // Within table::kEjectSaveSeconds of being returned by the Briefing room
  // or the Hangar: the short save after an eject applies.
  bool afterEject = false;
};

// What Game tells the rules about the balls, once a step, before the step's
// events. Together the four may never pass table::kMaxBalls.
struct TableStatus {
  int live = 1;    // On the table and not locked: free, held or on the plunger.
  int free = 0;    // Of those, the ones in play and not in a hold: on the playfield.
  int queued = 0;  // Asked for and not yet served.
  int locked = 0;  // In the Hangar's bays (until each has left).
};

// The seven major shots, from left to right.
enum class Shot : std::uint8_t {
  CircuitLeft,
  Runway,
  Hangar,
  Briefing,
  Turbine,
  Skyway,
  CircuitRight,
};
inline constexpr std::size_t kShotCount = 7;

// Where points came from, for the balance report.
enum class Feature : std::uint8_t {
  Slingshots,
  Bumpers,
  Spinner,
  TurboTargets,
  RollCall,
  ReturnLanes,  // W-I-N-G, right outlane, kickback.
  Ramps,
  Circuits,
  BayDoors,
  Hangar,
  Briefing,
  SkillShot,
  Combos,
  Course1,
  Course2,
  Course3,
  Course4,
  Course5,
  Course6,
  Jackpots,
  SuperJackpot,
  TurboBonus,
  ExtraCredit,
  ExtraBall,
  Graduation,
  Bonus,
};
inline constexpr std::size_t kFeatureCount = 26;
[[nodiscard]] std::string_view featureName(Feature feature) noexcept;

enum class LampState : std::uint8_t { Off, On, Blink, FastBlink };

enum class Multiball : std::uint8_t { None, Turbo, Academy, Graduation };

// The end-of-ball bonus as it was last tallied.
struct BonusTally {
  std::uint64_t courses = 0;
  std::uint64_t locks = 0;
  std::uint64_t combos = 0;
  std::uint64_t lanes = 0;
  std::uint64_t switches = 0;
  int multiplier = 1;
  std::uint64_t total = 0;  // The sum times the multiplier: what was paid.
};

// The rules of the game: scoring, balls, courses, multiball and messages.
// It knows nothing about physics. Game tells it what the balls did and how
// many there are, asks it what the table should do (doors, kickback, balls
// to launch, locks to let go), and the display asks it what to show.
//
// The game in short (docs/RULES.md is the player's version): three balls.
// The Briefing room starts one of six timed courses; passing them earns
// rank; all six light Graduation Day. The turbo meter (bumpers, spinner,
// turbo targets) launches a second ball. The three bay doors open the
// Hangar, which locks two balls and starts a three-ball multiball with the
// third. Shots made one after another are combos; the roll call lanes light
// overdrive, which multiplies the playfield.
class Rules {
 public:
  static constexpr int kBallsPerGame = 3;
  static constexpr int kLocksNeeded = 2;
  static constexpr int kMaxExtraBalls = 2;      // Awarded in a game.
  static constexpr int kMaxExtraBallsHeld = 1;  // Waiting to be played.
  static constexpr int kMaxBonusMultiplier = 5;
  static constexpr int kMaxOverdrive = 3;
  static constexpr int kMaxCombo = 5;
  // A drain this soon after the ball's first switch is returned.
  static constexpr double kBallSaveSeconds = tuning::kBallSaveSeconds;
  // The saver's lamp blinks fast for its last seconds.
  static constexpr double kBallSaveWarningSeconds = 3.0;
  static constexpr double kMessageSeconds = 3.0;
  // A message that has others waiting behind it stays this long.
  static constexpr double kQueuedMessageSeconds = 0.9;
  // Tilt: each nudge adds one unit of heat, which cools at one unit per
  // kTiltCoolSeconds. Reaching kTiltLimit tilts the table for the rest of
  // the ball. The limit sits just under three so that three nudges inside
  // about a second tilt, while occasional nudges never do.
  static constexpr double kTiltLimit = 2.5;
  static constexpr double kTiltCoolSeconds = 2.5;
  // The doors come back up this long after the Hangar has returned a ball
  // with nothing to keep them down.
  static constexpr double kDoorRaiseSeconds = 0.5;
  // A ball that lights the kickback from the left outlane's own switch is
  // not the ball it saves: the kickback is armed this long afterwards.
  static constexpr double kKickbackArmSeconds = 1.5;

  enum class DrainResult : std::uint8_t { BallSaved, NextBall, ShootAgain, GameOver };
  // What the Hangar does with a ball that has entered it. Eject: returns it
  // after the usual hold. Lock: keeps it in a bay, in view. StartMultiball:
  // returns it and lets every locked ball go after it.
  enum class HoleResult : std::uint8_t { Eject, Lock, StartMultiball };
  // What the rules want done with the bay doors.
  enum class DoorCommand : std::uint8_t { None, Drop, Raise };
  using DrainInfo = pinball::DrainInfo;

  explicit Rules(double stepSeconds) noexcept;

  // Begins a new game: ball one, score zero.
  void startGame() noexcept;

  // --- What the table tells the rules ---------------------------------------
  // The balls, once a step, before the step's events.
  void setTable(const TableStatus& status) noexcept;
  // A flipper button went down. They move the lit lanes (and the skill
  // shot's lane) one place left or right.
  void onFlipper(bool right) noexcept;

  // What a ball did. Indices are into the TableLayout arrays; out of range
  // values are ignored. ball is the ball's slot. Nothing scores once the
  // game is over or while the table is tilted.
  void onBumper(std::size_t index) noexcept;
  void onSlingshot(std::size_t index) noexcept;
  void onTarget(std::size_t index) noexcept;
  void onDropTarget(std::size_t index) noexcept;
  void onRollover(std::size_t index, std::size_t ball = 0) noexcept;
  // A ball dropped into a hole. The answer matters for the Hangar only; any
  // other hole returns its ball whatever is said.
  HoleResult onHole(std::size_t index) noexcept;
  // A hole returned a ball to the table.
  void onEject(std::size_t index) noexcept;
  // A ball crossed a sensor line (table::kSensors). The spinner's line is
  // also counted out as spins.
  void onSensor(std::size_t index, int direction, float speed, std::size_t ball = 0) noexcept;
  // The spinner turned once.
  void onSpin() noexcept;
  // The kickback took a ball and is spent.
  void onKickback() noexcept;
  // A ball was locked in a bay (0 or 1). served: no other ball was in play,
  // so a new one has been put on the plunger for the player.
  void onBallLocked(std::size_t bay, bool served) noexcept;
  // A locked ball has left its bay and is back on the table.
  void onLockReleased(std::size_t bay) noexcept;
  // The bay doors dropped, or came back up.
  void onDoorBank(bool up) noexcept;
  // A ball drained while others played on. Returns whether it is owed back
  // (Game then serves and launches another).
  bool onBallLost(const DrainInfo& info) noexcept;
  // A launched ball has left the lane and entered play.
  void onBallInPlay() noexcept;
  // The ball is back on the plunger without having drained (Game's fail-safe
  // for a lost ball). It is out of play until launched again: nudges stop
  // counting, and a tilt is cleared so that the plunger works. Nothing is
  // scored or forfeited and the ball number stays.
  void onBallReturned() noexcept;
  // The last ball on the table went down the drain. Says what happens next;
  // whatever it is, Game puts a ball on the plunger.
  DrainResult onDrain(const DrainInfo& info = {}) noexcept;
  // The player shoved the table. Returns true if the shove should move the
  // ball: false once tilted or when the game is over.
  bool onNudge() noexcept;
  // Once per simulation step.
  void tick() noexcept;

  // --- What the rules want of the table. Game asks every step. ---------------
  // The command is given once: asking clears it.
  DoorCommand takeDoorCommand() noexcept;
  // Whether the kickback should fire for the next ball in its pocket.
  bool kickbackLit() const noexcept {
    return kickbackLit_ && kickbackArmSteps_ == 0 && !gameOver_ && !tilted_;
  }
  // Balls to launch from the lane without the player. Asking clears it.
  int takeBallRequests() noexcept;
  // Whether every locked ball is to be let go. Asking clears it.
  bool takeLockRelease() noexcept;

  // --- For the bench ---------------------------------------------------------
  // Lights the kickback, or puts it out.
  void setKickbackLit(bool lit) noexcept { kickbackLit_ = lit; }
  // One ball at a time: with this off the Hangar never locks, a full turbo
  // meter pays its bonus and Graduation Day cannot start. For measuring the
  // table itself with a single ball.
  void setMultiballEnabled(bool enabled) noexcept { multiballEnabled_ = enabled; }

  // --- What to show ----------------------------------------------------------
  std::uint64_t score() const noexcept { return score_; }
  int ball() const noexcept { return ball_; }
  bool gameOver() const noexcept { return gameOver_; }
  // Tilted: the controls are dead and nothing scores until the ball drains.
  bool tilted() const noexcept { return tilted_; }
  // Tilt heat as a fraction of the limit, 0 to 1. Drives a warning light.
  double tiltWarning() const noexcept { return tiltHeat_ / kTiltLimit; }
  // A drain now would be returned by the ball saver or a multiball's grace.
  bool ballSaveActive() const noexcept {
    return !gameOver_ && !tilted_ && (saverPending_ || saverSteps_ > 0 || graceSteps_ > 0);
  }
  double ballSaveSecondsLeft() const noexcept { return secondsOf(saverSteps_); }
  // Between entering play and draining (or being returned to the plunger).
  bool ballInPlay() const noexcept { return ballInPlay_; }
  // Index into the rank names (core/Courses.hpp), 0 to kRankCount - 1.
  std::size_t rank() const noexcept { return rank_; }

  // Skill shot: offered while a ball waits for the player's plunge.
  bool skillShotOffered() const noexcept { return skillOffered_ && !gameOver_; }
  // The roll call lane that blinks for it, 0 to 2.
  std::size_t skillShotLane() const noexcept { return skillLane_; }
  bool superSkillShotLit() const noexcept { return superSkillSteps_ > 0; }

  // Lanes and targets.
  bool rollCallLit(std::size_t lane) const noexcept { return lane < 3 && rollCallLit_[lane]; }
  // The letters W, I, N, G: 0 to 3, left to right.
  bool wingLit(std::size_t letter) const noexcept { return letter < 4 && wingLit_[letter]; }
  bool turboTargetLit(std::size_t index) const noexcept {
    return index < turboTargetLit_.size() && turboTargetLit_[index];
  }
  // The kickback's lamp (kickbackLit() is whether it would fire this step).
  bool kickbackLamp() const noexcept { return kickbackLit_ && !gameOver_; }

  // Bay doors and the lock.
  bool doorMarked(std::size_t index) const noexcept {
    return index < doorMarked_.size() && doorMarked_[index];
  }
  // The Hangar would lock a ball now (or, with two locked, start multiball).
  bool lockLit() const noexcept;
  int locksHeld() const noexcept { return locksHeld_; }
  static constexpr int locksNeeded() noexcept { return kLocksNeeded; }

  // Turbo meter.
  int turboMeter() const noexcept { return turboMeter_; }
  int turboGoal() const noexcept { return turboGoal_; }
  double turboFraction() const noexcept {
    return static_cast<double>(turboMeter_) / static_cast<double>(turboGoal_);
  }

  // Multiball.
  Multiball multiball() const noexcept;
  // Balls in the game that are not locked: on the table or about to be.
  int ballsLive() const noexcept;
  // What the next jackpot pays, before overdrive.
  std::uint64_t jackpotValue() const noexcept;
  bool jackpotLit(Shot shot) const noexcept;
  bool multiballJackpotLit() const noexcept;
  bool superJackpotLit() const noexcept { return superLit_; }
  double graceSecondsLeft() const noexcept { return secondsOf(graceSteps_); }

  // Combos: how many shots the chain has, 0 with no chain running, to 5.
  int comboLevel() const noexcept { return comboSteps_ > 0 ? comboChain_ : 0; }

  // Overdrive: the playfield multiplier, 1 to 3.
  int overdrive() const noexcept { return overdrive_; }
  bool overdriveLit() const noexcept { return overdriveLit_; }
  double overdriveSecondsLeft() const noexcept { return secondsOf(overdriveSteps_); }

  // Courses. A running course's index (CourseId), or kCourseCount for none.
  std::size_t course() const noexcept { return course_; }
  bool courseRunning() const noexcept { return course_ < kCourseCount; }
  bool coursePassed(std::size_t index) const noexcept {
    return index < kCourseCount && coursePassed_[index];
  }
  int coursesPassed() const noexcept;
  // The course the Briefing room would start now (its lamp blinks).
  std::size_t courseOffered() const noexcept { return courseBlink_; }
  // The Briefing room is lit to start a course.
  bool briefingLit() const noexcept;
  // Ramp shots still needed to light it again; 0 when it is lit.
  int rampsForBriefing() const noexcept;
  // The name for the course window: the running course, GRADUATION DAY, or
  // nothing.
  std::string_view courseName() const noexcept;
  // The goal line for the course window. Never empty while a game runs.
  std::string_view goalText() const noexcept;
  // How many of the goal are still to go; 0 when there is no count to show.
  int goalRemaining() const noexcept;
  double courseSecondsLeft() const noexcept { return secondsOf(courseSteps_); }
  // TURBINE THEORY's pass award as it stands; 0 for any other course.
  std::uint64_t hurryUpValue() const noexcept;
  // NAVIGATION's lit shot; meaningful only while that course runs.
  Shot navigationShot() const noexcept { return navShot_; }

  // Graduation Day.
  bool graduationLit() const noexcept;
  bool graduationRunning() const noexcept { return gradStage_ != GradStage::None; }
  // Exam shots still to make: 7 to 0.
  int examShotsLeft() const noexcept;
  bool examShotMade(Shot shot) const noexcept { return examMade_[std::to_underlying(shot)]; }
  // Times Graduation Day has been completed in this game.
  int graduations() const noexcept { return graduations_; }

  // Awards waiting at the Briefing room.
  bool extraBallLit() const noexcept { return extraBallsLit_ > 0; }
  bool extraCreditLit() const noexcept { return extraCreditLit_; }
  // Extra balls waiting to be played.
  int extraBalls() const noexcept { return extraBallsHeld_; }

  // End-of-ball bonus: the multiplier now, and the last tally.
  int bonusMultiplier() const noexcept { return bonusMultiplier_; }
  const BonusTally& bonusTally() const noexcept { return bonusTally_; }
  // What the bonus would be if the ball ended now, before the multiplier.
  std::uint64_t bonusSoFar() const noexcept;

  // What a lamp does (table::kLamps).
  LampState lampState(table::LampId lamp) const noexcept;

  // Points earned from each feature this game.
  std::uint64_t scoreFrom(Feature feature) const noexcept {
    return featureScore_[std::to_underlying(feature)];
  }
  // Counts for the balance report, this game.
  int turboMultiballs() const noexcept { return turboMultiballs_; }
  int academyMultiballs() const noexcept { return academyMultiballs_; }
  int coursesStarted() const noexcept { return coursesStarted_; }
  int jackpotsCollected() const noexcept { return jackpotsCollected_; }

  // Sounds the rules asked for since the last call. Calling it clears them.
  SoundSet takeSounds() noexcept {
    const SoundSet sounds = sounds_;
    sounds_ = 0;
    return sounds;
  }

  // Text for the message display. Empty when there is nothing to say.
  std::string_view message() const noexcept { return message_.view(); }
  // What it says once the last ball has drained. It names no key: how a new
  // game is started depends on the device, so the platform adds that.
  static constexpr display::Message kGameOverMessage{"GAME OVER"};

 private:
  enum class GradStage : std::uint8_t { None, Exam, Commencement, VictoryLaps };
  static constexpr std::size_t kNoCourse = kCourseCount;
  static constexpr std::uint8_t kNoShot = 0xFF;

  void award(std::uint64_t points, Feature feature, bool timesOverdrive = true) noexcept;
  // Every message is checked against the display when it is compiled. say
  // shows it now, in place of anything showing or waiting; sayThen shows it
  // after what is showing and waiting.
  void say(const display::Text& text) noexcept;
  void sayThen(const display::Text& text) noexcept;
  void cue(Sound sound) noexcept { sounds_ |= soundBit(sound); }
  bool dead() const noexcept { return gameOver_ || tilted_; }
  std::uint32_t stepsFor(double seconds) const noexcept;
  double secondsOf(std::uint32_t steps) const noexcept {
    return static_cast<double>(steps) * stepSeconds_;
  }
  std::uint64_t nextRandom() noexcept;

  // A ball is on the plunger for the player.
  void offerSkillShot() noexcept;
  // What every new ball starts with and without.
  void beginBall() noexcept;
  // Any switch closed: the first of a ball starts the saver's clock.
  void noteSwitch() noexcept;
  void rotateLanes(bool right) noexcept;
  void addTurbo(int amount) noexcept;
  void turboFull() noexcept;
  void lightOverdrive() noexcept;
  void startOverdrive() noexcept;
  void lightKickback(bool fromLeftOutlane) noexcept;
  void addBonusMultiplier() noexcept;
  void lightExtraBall() noexcept;
  void collectExtraBall() noexcept;
  void collectExtraCredit() noexcept;

  // A major shot was made: courses, Graduation Day and jackpots hear of it.
  void majorShot(Shot shot) noexcept;
  // A flow shot was made: the combo chain.
  void flowShot(Shot shot) noexcept;
  void rampMade(Shot shot) noexcept;
  void circuitMade(Shot shot) noexcept;
  void jackpot() noexcept;

  // Balls the game has or is about to have, locked ones included.
  int ballsInGame() const noexcept;
  bool multiballRunning() const noexcept { return multiball() != Multiball::None; }
  void endMultiball(bool announce) noexcept;
  void startGraduation() noexcept;
  void endGraduation() noexcept;

  std::size_t nextUnpassed(std::size_t from) const noexcept;
  bool allCoursesPassed() const noexcept;
  void startCourse() noexcept;
  void courseStep(CourseId id) noexcept;
  void passCourse() noexcept;
  void failCourse() noexcept;
  void courseOver() noexcept;
  void hopNavigation() noexcept;
  bool navigable(Shot shot) const noexcept;

  // Whether the rules want the bay doors down now.
  bool doorsWantedDown() const noexcept;
  void markAllDoors() noexcept;
  void payBonus() noexcept;
  void clearModes() noexcept;

  double stepSeconds_;
  std::uint64_t score_ = 0;
  std::array<std::uint64_t, kFeatureCount> featureScore_{};
  int ball_ = 1;
  bool gameOver_ = false;
  bool tilted_ = false;
  double tiltHeat_ = 0.0;
  bool ballInPlay_ = false;
  std::size_t rank_ = 0;
  TableStatus status_{};
  // Ball requests not yet taken by Game, and taken but not yet in a status.
  int ballRequests_ = 0;
  int requestsInFlight_ = 0;
  bool lockRelease_ = false;
  bool multiballEnabled_ = true;

  // Ball saver, grace, extra balls.
  bool saverPending_ = false;  // Until the ball's first switch.
  std::uint32_t saverSteps_ = 0;
  std::uint32_t graceSteps_ = 0;
  int extraBallsLit_ = 0;
  int extraBallsHeld_ = 0;
  int extraBallsAwarded_ = 0;

  // Skill shot.
  bool skillOffered_ = false;
  std::size_t skillLane_ = 1;
  std::uint32_t superSkillSteps_ = 0;

  // Lanes, targets, kickback.
  std::array<bool, 3> rollCallLit_{};
  std::array<bool, 4> wingLit_{};
  std::array<bool, table::kTargets.size()> turboTargetLit_{};
  bool kickbackLit_ = true;
  std::uint32_t kickbackArmSteps_ = 0;
  bool extraCreditLit_ = false;

  // Turbo meter.
  int turboMeter_ = 0;
  int turboGoal_ = tuning::kTurboMeterFirst;

  // Bay doors and lock.
  std::array<bool, table::kDropTargets.size()> doorMarked_{};
  // The bank is down because its three doors were marked.
  bool doorsOpen_ = false;
  // The first bank of the game has not been opened yet.
  bool firstBank_ = true;
  // The last command given: the doors are, or are about to be, down.
  bool doorsDown_ = false;
  DoorCommand doorCommand_ = DoorCommand::None;
  std::uint32_t doorRaiseSteps_ = 0;
  // Balls the Hangar has taken and not yet returned (locked ones aside).
  int hangarHolding_ = 0;
  int locksHeld_ = 0;
  // Locked balls told to go and not yet out.
  int releasesPending_ = 0;

  // Multiball.
  bool turboMultiball_ = false;
  bool academyMultiball_ = false;
  std::size_t jackpotIndex_ = 0;
  int jackpotsTowardSuper_ = 0;
  bool superLit_ = false;
  int turboMultiballs_ = 0;
  int academyMultiballs_ = 0;
  int jackpotsCollected_ = 0;

  // Combos.
  std::uint8_t comboShot_ = kNoShot;
  int comboChain_ = 0;
  std::uint32_t comboSteps_ = 0;
  bool fiveComboMade_ = false;

  // Overdrive.
  bool overdriveLit_ = false;
  int overdrive_ = 1;
  std::uint32_t overdriveSteps_ = 0;

  // Circuits: for each ball, the side whose mouth it went up (0 left, 1
  // right) and the steps it has left to finish.
  struct CircuitTry {
    std::uint8_t side = 0;
    std::uint32_t steps = 0;
  };
  std::array<CircuitTry, table::kMaxBalls> circuits_{};

  // Courses.
  std::array<bool, kCourseCount> coursePassed_{};
  std::size_t course_ = kNoCourse;
  std::size_t courseBlink_ = 0;
  bool courseLit_ = true;
  int rampsNeeded_ = 0;
  int coursesEnded_ = 0;
  int coursesStarted_ = 0;
  // Passes in this game, every round counted (for the bonus).
  int coursePasses_ = 0;
  std::uint32_t courseSteps_ = 0;
  std::uint32_t courseElapsedSteps_ = 0;
  int courseProgress_ = 0;
  bool courseSecondPart_ = false;  // HANGAR SAFETY: the doors are down.
  Shot navShot_ = Shot::CircuitLeft;
  std::uint32_t navSteps_ = 0;

  // Graduation Day.
  GradStage gradStage_ = GradStage::None;
  bool gradLit_ = false;
  int gradRampsNeeded_ = 0;
  std::array<bool, kShotCount> examMade_{};
  int graduations_ = 0;
  bool commencementThisTime_ = false;

  // Bonus, for the ball in play.
  bool briefingVisited_ = false;
  int combosThisBall_ = 0;
  int laneSetsThisBall_ = 0;
  int switchesThisBall_ = 0;
  int bonusMultiplier_ = 1;
  BonusTally bonusTally_{};

  // The display: what shows now and what waits behind it.
  static constexpr std::size_t kMessageQueue = 8;
  display::Text message_{};
  std::uint32_t messageSteps_ = 0;
  std::array<display::Text, kMessageQueue> waiting_{};
  std::size_t waitingCount_ = 0;

  // The rules' own source of chance: the same events give the same game.
  std::uint64_t random_ = 0xD1B54A32D192ED03ULL;
  SoundSet sounds_ = 0;
};

}  // namespace pinball
