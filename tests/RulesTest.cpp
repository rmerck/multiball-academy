#include <cstddef>
#include <cstdint>
#include <string_view>

#include "Check.hpp"
#include "RulesBench.hpp"
#include "Suites.hpp"
#include "core/Courses.hpp"
#include "core/Rules.hpp"
#include "core/TableLayout.hpp"

// The table's rules, an event at a time: one group for each rule of the
// scoring table, the skill shot, combos, overdrive, extra credit, the ball
// saver, the kickback, extra balls, the bonus and the tilt. Courses and
// Graduation Day are in CourseTest.cpp, the multiballs in MultiballTest.cpp.
namespace {

using pinball::DrainInfo;
using pinball::Feature;
using pinball::LampState;
using pinball::Rules;
using pinball::Shot;
using pinball_test::RulesBench;
namespace points = pinball::points;
namespace table = pinball::table;
namespace tuning = pinball::tuning;
using LampId = pinball::table::LampId;

// A bench whose ball has closed a switch already: no skill shot is on
// offer and the saver's clock is running.
RulesBench started() {
  RulesBench bench;
  bench.rules.onSlingshot(0);
  return bench;
}

// The same with the saver run out.
RulesBench unsaved() {
  RulesBench bench = started();
  bench.wait(Rules::kBallSaveSeconds);
  return bench;
}

}  // namespace

void testRules() {
  {  // A new game.
    RulesBench bench;
    const Rules& rules = bench.rules;
    CHECK(rules.score() == 0 && rules.ball() == 1 && !rules.gameOver() && rules.rank() == 0);
    CHECK(!rules.tilted() && rules.ballInPlay());
    CHECK(rules.message() == std::string_view("LAUNCH THE BALL"));
    CHECK(rules.kickbackLit() && rules.kickbackLamp());
    CHECK(rules.briefingLit() && rules.rampsForBriefing() == 0);
    CHECK(rules.goalText() == pinball::kGoalBriefingLit && rules.courseName().empty());
    CHECK(rules.turboMeter() == 0 && rules.turboGoal() == tuning::kTurboMeterFirst);
    CHECK(rules.locksHeld() == 0 && !rules.lockLit() && rules.multiball() == pinball::Multiball::None);
    CHECK(rules.overdrive() == 1 && !rules.overdriveLit() && rules.comboLevel() == 0);
    CHECK(rules.extraBalls() == 0 && !rules.extraBallLit() && !rules.extraCreditLit());
    CHECK(rules.bonusMultiplier() == 1 && rules.skillShotOffered());
    CHECK(bench.doorsUp);
  }

  {  // 4.13, the plain values: each element pays its own, every time.
    RulesBench bench = started();
    const Rules& rules = bench.rules;
    std::uint64_t expected = points::kSlingshot;
    bench.rules.onSlingshot(1);
    expected += points::kSlingshot;
    bench.bumper();
    expected += points::kBumper;
    bench.spin();
    expected += points::kSpin;
    bench.rules.onTarget(0);
    expected += points::kTarget;
    bench.door(0);
    expected += points::kBayDoor;
    bench.door(0);
    expected += points::kBayDoorAgain;
    bench.rollCall(0);
    expected += points::kRollCallLane;
    bench.wing(1);
    expected += points::kReturnLane;
    bench.wing(3);  // The right outlane: its lane and its own award.
    expected += points::kReturnLane + points::kRightOutlane;
    bench.rules.onKickback();
    expected += points::kKickback;
    bench.wait(tuning::kComboSeconds);
    bench.runway();
    expected += points::kRamp;
    bench.wait(tuning::kComboSeconds);
    bench.circuit(false);
    expected += points::kCircuit;
    CHECK(rules.score() == expected);
    CHECK(rules.scoreFrom(Feature::Slingshots) == 2 * points::kSlingshot);
    CHECK(rules.scoreFrom(Feature::Ramps) == points::kRamp);
    CHECK(rules.scoreFrom(Feature::ReturnLanes) ==
          2 * points::kReturnLane + points::kRightOutlane + points::kKickback);
    // Indices outside the layout are ignored.
    bench.rules.onBumper(99);
    bench.rules.onSlingshot(99);
    bench.rules.onTarget(99);
    bench.rules.onDropTarget(99);
    bench.rules.onRollover(99);
    bench.rules.onSensor(99, 1, 2000.0F, 0);
    CHECK(bench.rules.onHole(99) == Rules::HoleResult::Eject);
    bench.rules.onEject(99);
    CHECK(rules.score() == expected);
    // The shares add up to the score.
    std::uint64_t sum = 0;
    for (std::size_t f = 0; f < pinball::kFeatureCount; ++f) {
      sum += rules.scoreFrom(static_cast<Feature>(f));
      CHECK(!pinball::featureName(static_cast<Feature>(f)).empty());
    }
    CHECK(sum == rules.score());
  }

  {  // 4.2 Skill shot: the blinking lane pays 25,000 times the ball number,
     // another lane 5,000; the flipper buttons move the blinking lane.
    RulesBench bench;
    CHECK(bench.rules.skillShotLane() == 1);
    CHECK(bench.rules.lampState(LampId::RollCall2) == LampState::Blink);
    bench.rules.onFlipper(true);
    CHECK(bench.rules.skillShotLane() == 2);
    bench.rules.onFlipper(true);
    CHECK(bench.rules.skillShotLane() == 0);
    bench.rules.onFlipper(false);
    CHECK(bench.rules.skillShotLane() == 2);
    bench.rollCall(2);
    CHECK(bench.score() == points::kSkillShot + points::kRollCallLane);
    CHECK(bench.message() == std::string_view("SKILL SHOT"));
    CHECK(bench.rules.rollCallLit(2) && !bench.rules.skillShotOffered());
    // Only once: the next lane is a lane.
    bench.rollCall(0);
    CHECK(bench.score() == points::kSkillShot + 2 * points::kRollCallLane);

    RulesBench other;
    other.rollCall(0);
    CHECK(other.score() == points::kSkillShotOtherLane + points::kRollCallLane);

    // Any other switch first: no skill shot.
    RulesBench missed;
    missed.bumper();
    missed.rollCall(1);
    CHECK(missed.score() == points::kBumper + points::kRollCallLane);

    // Ball two: twice the award. It is offered again at the new ball.
    RulesBench second = unsaved();
    CHECK(second.drain() == Rules::DrainResult::NextBall);
    CHECK(second.rules.ball() == 2 && second.rules.skillShotOffered());
    const std::uint64_t before = second.score();
    second.rollCall(second.rules.skillShotLane());
    CHECK(second.score() - before == 2 * points::kSkillShot + points::kRollCallLane);
  }

  {  // 4.2 Super skill shot: a full plunge reaches the left inlane first,
     // then the Skyway within five seconds.
    RulesBench bench;
    bench.wing(1);  // I: the left inlane.
    CHECK(bench.rules.superSkillShotLit());
    CHECK(bench.rules.lampState(LampId::ArrowSkyway) == LampState::FastBlink);
    bench.wait(2.0);
    const std::uint64_t before = bench.score();
    bench.skyway();
    CHECK(bench.score() - before == points::kSuperSkillShot + points::kRamp);
    CHECK(!bench.rules.superSkillShotLit());
    // It was the first shot of a combo.
    CHECK(bench.rules.comboLevel() == 1);
    bench.runway();
    CHECK(bench.rules.comboLevel() == 2);

    // Too late.
    RulesBench late;
    late.wing(1);
    late.wait(tuning::kSuperSkillSeconds);
    const std::uint64_t lateBefore = late.score();
    late.skyway();
    CHECK(late.score() - lateBefore == points::kRamp);
    // The Runway is not the shot; and the inlane after another switch does
    // not light it.
    RulesBench wrong;
    wrong.bumper();
    wrong.wing(1);
    CHECK(!wrong.rules.superSkillShotLit());
  }

  {  // 4.3 Roll call lanes: three light overdrive; the buttons rotate them.
    RulesBench bench = started();
    bench.rollCall(0);
    CHECK(bench.rules.rollCallLit(0) && !bench.rules.rollCallLit(1));
    bench.rules.onFlipper(true);
    CHECK(bench.rules.rollCallLit(1) && !bench.rules.rollCallLit(0));
    bench.rules.onFlipper(false);
    bench.rules.onFlipper(false);
    CHECK(bench.rules.rollCallLit(2));
    const std::uint64_t before = bench.score();
    bench.rollCall(2);  // Lit already: its value, nothing more.
    CHECK(bench.score() - before == points::kRollCallLane);
    bench.rollCall(0);
    bench.rollCall(1);
    CHECK(bench.score() - before == 3 * points::kRollCallLane + points::kRollCallComplete);
    CHECK(!bench.rules.rollCallLit(0) && !bench.rules.rollCallLit(1) && !bench.rules.rollCallLit(2));
    CHECK(bench.rules.overdriveLit() && bench.rules.overdrive() == 1);
    CHECK(bench.message() == std::string_view("ROLL CALL COMPLETE"));
    bench.wait(1.0);
    CHECK(bench.message() == std::string_view("OVERDRIVE IS LIT"));
    CHECK(bench.rules.lampState(LampId::ArrowTurbine) == LampState::Blink);
  }

  {  // 4.9 Overdrive: started at the Turbine, doubles the playfield for 20 s;
     // roll call again makes it 3X and starts the time over; it stops while
     // no ball is free.
    RulesBench bench = started();
    for (std::size_t lane = 0; lane < 3; ++lane) {
      bench.rollCall(lane);
    }
    bench.wait(tuning::kComboSeconds);
    bench.turbine();
    CHECK(bench.rules.overdrive() == 2 && !bench.rules.overdriveLit());
    CHECK(bench.message() == std::string_view("OVERDRIVE 2X"));
    std::uint64_t before = bench.score();
    bench.bumper();
    CHECK(bench.score() - before == 2 * points::kBumper);
    bench.wait(10.0);
    CHECK(bench.rules.overdrive() == 2);
    // Every ball held: the clock stands.
    bench.balls.free = 0;
    bench.wait(30.0);
    CHECK(bench.rules.overdrive() == 2);
    bench.balls.free = 1;
    for (std::size_t lane = 0; lane < 3; ++lane) {
      bench.rollCall(lane);
    }
    CHECK(bench.rules.overdrive() == 3 && !bench.rules.overdriveLit());
    CHECK(bench.message() == std::string_view("OVERDRIVE 3X"));
    CHECK(bench.rules.overdriveSecondsLeft() > tuning::kOverdriveSeconds - 0.1);
    before = bench.score();
    bench.bumper();
    CHECK(bench.score() - before == 3 * points::kBumper);
    // Three is the most.
    for (std::size_t lane = 0; lane < 3; ++lane) {
      bench.rollCall(lane);
    }
    CHECK(bench.rules.overdrive() == 3);
    bench.wait(tuning::kOverdriveSeconds);
    CHECK(bench.rules.overdrive() == 1);
    CHECK(bench.message() == std::string_view("OVERDRIVE OVER"));
    before = bench.score();
    bench.bumper();
    CHECK(bench.score() - before == points::kBumper);
    // The Turbine with nothing lit starts nothing.
    bench.turbine();
    CHECK(bench.rules.overdrive() == 1);

    // It ends with the ball; awards "as stated" are never multiplied.
    RulesBench ending = unsaved();
    for (std::size_t lane = 0; lane < 3; ++lane) {
      ending.rollCall(lane);
    }
    ending.turbine();
    CHECK(ending.rules.overdrive() == 2);
    const std::uint64_t beforeDrain = ending.score();
    CHECK(ending.drain() == Rules::DrainResult::NextBall);
    CHECK(ending.rules.overdrive() == 1);
    CHECK(ending.score() - beforeDrain == ending.rules.bonusTally().total);
  }

  {  // 4.3 W-I-N-G: four letters relight the kickback; with it lit already
     // they raise the bonus multiplier, to 5X.
    RulesBench bench = started();
    bench.rules.onKickback();
    CHECK(!bench.rules.kickbackLit());
    bench.wing(1);
    bench.wing(2);
    bench.wing(3);
    CHECK(bench.rules.wingLit(1) && bench.rules.wingLit(2) && bench.rules.wingLit(3));
    bench.rules.onFlipper(true);  // G wraps round to W.
    CHECK(bench.rules.wingLit(0) && bench.rules.wingLit(2) && bench.rules.wingLit(3) &&
          !bench.rules.wingLit(1));
    const std::uint64_t before = bench.score();
    bench.wing(1);
    CHECK(bench.score() - before == points::kReturnLane + points::kWingComplete);
    CHECK(bench.rules.kickbackLit() && bench.message() == std::string_view("WINGS EARNED  KICKBACK LIT"));
    CHECK(!bench.rules.wingLit(0) && !bench.rules.wingLit(1));
    CHECK(bench.rules.bonusMultiplier() == 1);
    for (int set = 2; set <= 7; ++set) {
      for (std::size_t letter = 0; letter < 4; ++letter) {
        bench.wing(letter);
      }
      CHECK(bench.rules.bonusMultiplier() == (set < 5 ? set : 5));
    }
    CHECK(bench.message() == std::string_view("WINGS EARNED  BONUS X"));
  }

  {  // The kickback lit by the left outlane's own switch does not save the
     // ball that lit it: it is armed a moment later.
    RulesBench bench = started();
    bench.rules.onKickback();
    bench.wing(1);
    bench.wing(2);
    bench.wing(3);
    bench.wing(0);  // W last: the ball is in the left outlane.
    CHECK(bench.rules.kickbackLamp() && !bench.rules.kickbackLit());
    bench.wait(Rules::kKickbackArmSeconds);
    CHECK(bench.rules.kickbackLit());
    // Lit from another lane it is armed at once.
    bench.rules.onKickback();
    bench.wing(0);
    bench.wing(1);
    bench.wing(2);
    bench.wing(3);
    CHECK(bench.rules.kickbackLit());
    // It carries over between balls, lit or not.
    bench.rules.onKickback();
    bench.wait(Rules::kBallSaveSeconds);
    bench.drain();
    CHECK(!bench.rules.kickbackLit() && bench.rules.ball() == 2);
    CHECK(bench.message() != std::string_view("KICKBACK"));
  }

  {  // 4.3 Turbo targets: both light extra credit; 4.10: one award, shown.
    RulesBench bench = started();
    std::uint64_t before = bench.score();
    bench.rules.onTarget(0);
    CHECK(bench.rules.turboTargetLit(0) && !bench.rules.turboTargetLit(1));
    bench.rules.onTarget(0);
    CHECK(bench.score() - before == 2 * points::kTarget && !bench.rules.extraCreditLit());
    bench.rules.onTarget(1);
    CHECK(bench.score() - before == 3 * points::kTarget + points::kBothTargets);
    CHECK(bench.rules.extraCreditLit() && !bench.rules.turboTargetLit(0));
    CHECK(bench.message() == std::string_view("EXTRA CREDIT IS LIT"));
    CHECK(bench.rules.turboMeter() == 3 * tuning::kTurboPerTarget);
    CHECK(bench.rules.lampState(LampId::ExtraCredit) == LampState::Blink);
  }

  {  // 4.10 Extra credit: every award of the table turns up, each says what
     // it gave, none is an extra ball and none is over 50,000.
    int seen[7] = {};
    RulesBench bench = started();
    bench.rules.setMultiballEnabled(false);  // A turbo boost must not add balls here.
    for (int n = 0; n < 400; ++n) {
      bench.rules.onTarget(0);
      bench.rules.onTarget(1);
      CHECK(bench.rules.extraCreditLit());
      const std::uint64_t before = bench.score();
      const int extraBefore = bench.rules.extraBalls();
      bench.scoop();
      CHECK(!bench.rules.extraCreditLit());
      // The course it started, if it did, is not what is being looked at.
      const std::uint64_t gained = bench.score() - before - points::kBriefingRoom;
      CHECK(gained <= 50'000 || bench.rules.courseRunning());
      CHECK(bench.rules.extraBalls() == extraBefore);
      // Find what the display said about it, among what it says in turn.
      for (int i = 0; i < 6; ++i) {
        const std::string_view said = bench.message();
        if (said.starts_with("EXTRA CREDIT")) {
          const std::string_view what[] = {"EXTRA CREDIT 25,000",        "EXTRA CREDIT 50,000",
                                           "EXTRA CREDIT  KICKBACK LIT", "EXTRA CREDIT  BONUS X",
                                           "EXTRA CREDIT  BALL SAVER",   "EXTRA CREDIT  TURBO BOOST",
                                           "EXTRA CREDIT  OVERDRIVE LIT"};
          for (int k = 0; k < 7; ++k) {
            seen[k] += said == what[k] ? 1 : 0;
          }
          break;
        }
        bench.wait(Rules::kQueuedMessageSeconds);
      }
      bench.wait(1.0);
    }
    int total = 0;
    for (const int count : seen) {
      CHECK(count > 0);
      total += count;
    }
    CHECK(total >= 390);
    // Weighted: 25,000 is the commonest, overdrive the rarest.
    CHECK(seen[0] > seen[1] && seen[1] > seen[6] && seen[0] > 80 && seen[6] < 50);
  }

  {  // 4.3 Briefing room: 5,000 the first time on a ball and when something
     // is collected, 1,000 for a repeat with nothing lit.
    RulesBench bench = started();
    std::uint64_t before = bench.score();
    bench.scoop();  // Starts the first course.
    CHECK(bench.score() - before == points::kBriefingRoom && bench.rules.courseRunning());
    before = bench.score();
    bench.scoop();
    CHECK(bench.score() - before == points::kBriefingRepeat);
    bench.rules.onTarget(0);
    bench.rules.onTarget(1);
    before = bench.score();
    bench.scoop();
    CHECK(bench.score() - before >= points::kBriefingRoom);
    // A new ball: the first visit pays in full again.
    bench.wait(Rules::kBallSaveSeconds);
    bench.drain();
    before = bench.score();
    bench.rules.onSlingshot(0);
    bench.scoop();
    CHECK(bench.score() - before == points::kSlingshot + points::kBriefingRoom);
  }

  {  // 4.3 Circuits: the mouth going up at 900 or more, then the top of the
     // arch the same way round or a roll call lane, by the same ball, in 2.5 s.
    RulesBench bench = started();
    const Rules& rules = bench.rules;
    std::uint64_t before = bench.score();
    const auto gained = [&bench, &before] {
      const std::uint64_t now = bench.score() - before;
      before = bench.score();
      return now;
    };
    bench.rules.onSensor(0, 1, 1200.0F, 0);
    CHECK(gained() == 0);
    bench.rules.onSensor(2, 1, 800.0F, 0);
    CHECK(gained() == points::kCircuit);
    // Once: the top again pays nothing.
    bench.rules.onSensor(2, 1, 800.0F, 0);
    CHECK(gained() == 0);
    // Too slow at the mouth.
    bench.rules.onSensor(0, 1, 899.0F, 0);
    bench.rules.onSensor(2, 1, 800.0F, 0);
    CHECK(gained() == 0);
    // Down through the mouth is not an entry.
    bench.rules.onSensor(0, -1, 1500.0F, 0);
    bench.rules.onSensor(2, 1, 800.0F, 0);
    CHECK(gained() == 0);
    // The wrong way over the top (a launch goes that way).
    bench.rules.onSensor(0, 1, 1500.0F, 0);
    bench.rules.onSensor(2, -1, 800.0F, 0);
    CHECK(gained() == 0);
    bench.wait(tuning::kCircuitSeconds);
    bench.wait(tuning::kComboSeconds);
    // Another ball's crossing.
    bench.rules.onSensor(1, 1, 1500.0F, 1);
    bench.rules.onSensor(2, -1, 800.0F, 2);
    CHECK(gained() == 0);
    bench.rules.onSensor(2, -1, 800.0F, 1);
    CHECK(gained() == points::kCircuit);
    // Too late.
    bench.rules.onSensor(1, 1, 1500.0F, 0);
    bench.wait(tuning::kCircuitSeconds);
    bench.rules.onSensor(2, -1, 800.0F, 0);
    CHECK(gained() == 0);
    // Falling off the arch into a roll call lane is a made circuit.
    bench.rules.onSensor(1, 1, 1500.0F, 0);
    bench.rollCall(1, 0);
    CHECK(gained() == points::kCircuit + points::kRollCallLane);
    // A lane by another ball is not.
    bench.rules.onSensor(1, 1, 1500.0F, 0);
    bench.rollCall(1, 1);
    CHECK(gained() == points::kRollCallLane);
    CHECK(rules.scoreFrom(Feature::Circuits) == 3 * points::kCircuit);
  }

  {  // 4.4 Combos: a different flow shot within four seconds extends the
     // chain; 20, 40, 80, 150 thousand; the chain ends at 5X.
    RulesBench bench = started();
    const Rules& rules = bench.rules;
    std::uint64_t before = bench.score();
    const auto gained = [&bench, &before] {
      const std::uint64_t now = bench.score() - before;
      before = bench.score();
      return now;
    };
    bench.runway();
    CHECK(gained() == points::kRamp && rules.comboLevel() == 1);
    bench.wait(3.0);
    bench.skyway();
    CHECK(gained() == points::kRamp + points::kCombo[0] && rules.comboLevel() == 2);
    CHECK(bench.message() == std::string_view("COMBO 2X"));
    bench.circuit(false);
    CHECK(gained() == points::kCircuit + points::kCombo[1] && rules.comboLevel() == 3);
    bench.turbine();
    CHECK(gained() == points::kCombo[2] && rules.comboLevel() == 4);
    CHECK(!rules.extraBallLit());
    bench.circuit(true);
    CHECK(gained() == points::kCircuit + points::kCombo[3]);
    CHECK(bench.message() == std::string_view("COMBO 5X"));
    // The chain is over: the first 5X of a game lights the extra ball, and
    // the next flow shot is the first of a new chain.
    CHECK(rules.comboLevel() == 0 && rules.extraBallLit());
    bench.runway();
    CHECK(gained() == points::kRamp && rules.comboLevel() == 1);
    CHECK(rules.scoreFrom(Feature::Combos) == 290'000);

    // The same shot again neither extends nor breaks: it restarts the time.
    bench.wait(tuning::kComboSeconds);
    bench.runway();
    bench.wait(3.0);
    bench.runway();
    CHECK(gained() == 2 * points::kRamp && rules.comboLevel() == 1);
    bench.wait(3.0);  // Six seconds after the first, three after the second.
    bench.skyway();
    CHECK(gained() == points::kRamp + points::kCombo[0]);
    // Too late: a new chain.
    bench.wait(tuning::kComboSeconds);
    CHECK(rules.comboLevel() == 0);
    bench.runway();
    CHECK(gained() == points::kRamp && rules.comboLevel() == 1);
    // A second 5X chain lights no second extra ball.
    bench.wait(tuning::kComboSeconds);
    bench.scoop();  // Collect the first (and start a course).
    CHECK(!rules.extraBallLit() && rules.extraBalls() == 1);
    bench.wait(tuning::kComboSeconds);
    bench.runway();
    bench.skyway();
    bench.runway();
    bench.skyway();
    bench.runway();
    CHECK(rules.comboLevel() == 0 && !rules.extraBallLit());
    // Down the Turbine lane is not the shot.
    bench.wait(tuning::kComboSeconds);
    bench.rules.onSensor(table::kSpinnerSensor, -1, 900.0F, 0);
    CHECK(rules.comboLevel() == 0);
  }

  {  // 4.11 Ball saver: 15 s from the ball's first switch; every drain
     // inside it comes back, any number of times; its clock stands while no
     // ball is free.
    RulesBench bench;
    const Rules& rules = bench.rules;
    CHECK(rules.ballSaveActive());  // Waiting for the first switch.
    CHECK(rules.lampState(LampId::ShootAgain) == LampState::Blink);
    // A ball that drains having touched nothing is returned too.
    CHECK(bench.drain() == Rules::DrainResult::BallSaved);
    CHECK(rules.ball() == 1 && bench.message() == std::string_view("BALL SAVED"));
    CHECK(rules.skillShotOffered());
    bench.wait(60.0);
    CHECK(rules.ballSaveActive());
    bench.rules.onSlingshot(0);
    CHECK(rules.ballSaveSecondsLeft() > Rules::kBallSaveSeconds - 0.1);
    bench.wait(5.0);
    CHECK(bench.drain() == Rules::DrainResult::BallSaved);
    CHECK(bench.drain() == Rules::DrainResult::BallSaved);
    // On the plunger the clock stands.
    bench.balls.free = 0;
    bench.wait(60.0);
    CHECK(rules.ballSaveActive());
    bench.balls.free = 1;
    bench.wait(7.5);
    CHECK(rules.ballSaveActive());
    CHECK(rules.lampState(LampId::ShootAgain) == LampState::FastBlink);
    bench.wait(3.0);
    CHECK(!rules.ballSaveActive());
    CHECK(rules.lampState(LampId::ShootAgain) == LampState::Off);
    CHECK(bench.drain() == Rules::DrainResult::NextBall);
    CHECK(rules.ball() == 2 && rules.ballSaveActive());
  }

  {  // 4.7 The short save after an eject: any drain so marked comes back,
     // saver or no saver, without limit, and not on a tilted table.
    RulesBench bench = unsaved();
    CHECK(!bench.rules.ballSaveActive());
    for (int i = 0; i < 4; ++i) {
      CHECK(bench.drain(DrainInfo{.afterEject = true}) == Rules::DrainResult::BallSaved);
      CHECK(bench.rules.ball() == 1 && bench.message() == std::string_view("BALL SAVED"));
    }
    for (int i = 0; i < 3; ++i) {
      bench.rules.onNudge();
    }
    CHECK(bench.rules.tilted());
    CHECK(bench.drain(DrainInfo{.afterEject = true}) == Rules::DrainResult::NextBall);
  }

  {  // 4.10 Extra balls: lit by the first 5X chain, collected at the
     // Briefing room, played after the ball ends. One held at most.
    RulesBench bench = unsaved();
    const Rules& rules = bench.rules;
    bench.runway();
    bench.skyway();
    bench.runway();
    bench.skyway();
    bench.runway();
    CHECK(rules.extraBallLit() && rules.lampState(LampId::ExtraBall) == LampState::Blink);
    bench.scoop();
    CHECK(rules.extraBalls() == 1 && !rules.extraBallLit());
    CHECK(rules.lampState(LampId::ShootAgain) == LampState::On);
    CHECK(bench.drain() == Rules::DrainResult::ShootAgain);
    CHECK(rules.ball() == 1 && rules.extraBalls() == 0 && rules.skillShotOffered());
    bench.wait(4.0);  // After the bonus lines.
    CHECK(bench.message() == std::string_view("SHOOT AGAIN"));
    CHECK(bench.drainForGood() == Rules::DrainResult::NextBall);
    CHECK(rules.ball() == 2);
  }

  {  // 4.11 End-of-ball bonus: every part, times the multiplier, tallied a
     // line at a time; lost on tilt.
    RulesBench bench = unsaved();  // One switch so far: the slingshot.
    const Rules& rules = bench.rules;
    bench.runway();
    bench.skyway();  // One combo; two switches.
    for (std::size_t lane = 0; lane < 3; ++lane) {
      bench.rollCall(lane);  // One lane set; three switches.
    }
    for (std::size_t letter = 0; letter < 4; ++letter) {
      bench.wing(letter);  // A second set, and (kickback lit) 2X; four switches.
    }
    CHECK(rules.bonusMultiplier() == 2);
    const std::uint64_t base = points::kBonusCombo + 2 * points::kBonusLaneSet +
                               10 * points::kBonusSwitch;
    CHECK(rules.bonusSoFar() == base);
    const std::uint64_t before = bench.score();
    CHECK(bench.drain() == Rules::DrainResult::NextBall);
    CHECK(bench.score() - before == 2 * base);
    CHECK(rules.scoreFrom(Feature::Bonus) == 2 * base);
    const pinball::BonusTally& tally = rules.bonusTally();
    CHECK(tally.courses == 0 && tally.locks == 0 && tally.combos == points::kBonusCombo);
    CHECK(tally.lanes == 2 * points::kBonusLaneSet && tally.switches == 10 * points::kBonusSwitch);
    CHECK(tally.multiplier == 2 && tally.total == 2 * base);
    // The display: one line after another, then the next ball.
    CHECK(bench.message() == std::string_view("COMBOS 5,000"));
    bench.wait(Rules::kQueuedMessageSeconds);
    CHECK(bench.message() == std::string_view("LANES 10,000"));
    bench.wait(Rules::kQueuedMessageSeconds);
    CHECK(bench.message() == std::string_view("SWITCHES 2,500"));
    bench.wait(Rules::kQueuedMessageSeconds);
    CHECK(bench.message() == std::string_view("BONUS 2X  35,000"));
    bench.wait(Rules::kQueuedMessageSeconds);
    CHECK(bench.message() == std::string_view("NEXT BALL"));
    bench.wait(Rules::kMessageSeconds);
    CHECK(bench.message().empty());
    // The counts and the multiplier start again with the ball.
    CHECK(rules.bonusMultiplier() == 1 && rules.bonusSoFar() == 0);

    // The switch count stops at 400.
    RulesBench many = unsaved();
    for (int i = 0; i < 500; ++i) {
      many.rules.onSlingshot(0);
    }
    CHECK(many.rules.bonusSoFar() == 400 * points::kBonusSwitch);
  }

  {  // 4.11 Tilt: three nudges in a second; nothing scores, nothing is
     // saved, the bonus is lost, and it ends with the ball.
    RulesBench bench = started();
    const Rules& rules = bench.rules;
    CHECK(bench.rules.onNudge());
    CHECK(bench.rules.onNudge());
    CHECK(bench.message() == std::string_view("DANGER  TILT WARNING"));
    CHECK(!bench.rules.onNudge());
    CHECK(rules.tilted() && bench.message() == std::string_view("TILT"));
    CHECK(!rules.ballSaveActive() && !rules.kickbackLit());
    const std::uint64_t before = bench.score();
    bench.bumper(5);
    bench.spin(5);
    bench.runway();
    bench.rollCall(0);
    bench.door(0);
    bench.rules.onTarget(0);
    bench.circuit(false);
    bench.scoop();
    CHECK(bench.hangar() == Rules::HoleResult::Eject);
    CHECK(bench.score() == before && rules.turboMeter() == 0 && !rules.courseRunning());
    CHECK(!rules.rollCallLit(0) && !rules.doorMarked(0) && !rules.turboTargetLit(0));
    CHECK(bench.message() == std::string_view("TILT"));
    bench.rules.onFlipper(true);
    CHECK(rules.skillShotLane() == 1);
    CHECK(!bench.rules.onNudge());
    CHECK(bench.drain() == Rules::DrainResult::NextBall);
    CHECK(!rules.tilted() && rules.ball() == 2 && bench.score() == before);
    CHECK(rules.bonusTally().total == 0);
    CHECK(bench.message() == std::string_view("NEXT BALL"));
    // Nudges do not count with the ball on the plunger.
    RulesBench waiting;
    waiting.rules.onBallReturned();
    for (int i = 0; i < 5; ++i) {
      CHECK(!waiting.rules.onNudge());
    }
    CHECK(!waiting.rules.tilted());
    // Occasional nudges never tilt.
    RulesBench calm = started();
    for (int i = 0; i < 20; ++i) {
      CHECK(calm.rules.onNudge());
      calm.wait(Rules::kTiltCoolSeconds);
    }
    CHECK(!calm.rules.tilted());
  }

  {  // Three balls, then the game is over and stays over.
    RulesBench bench = unsaved();
    const Rules& rules = bench.rules;
    CHECK(bench.drain() == Rules::DrainResult::NextBall);
    CHECK(bench.drainForGood() == Rules::DrainResult::NextBall);
    CHECK(rules.ball() == 3);
    bench.scoop();
    CHECK(rules.courseRunning());
    CHECK(bench.drainForGood() == Rules::DrainResult::GameOver);
    CHECK(rules.gameOver() && rules.ball() == 3);
    // Nothing is left running, lit or asked for.
    CHECK(!rules.courseRunning() && rules.multiball() == pinball::Multiball::None);
    CHECK(!rules.ballSaveActive() && !rules.kickbackLit() && !rules.lockLit());
    CHECK(!rules.skillShotOffered() && !rules.briefingLit() && !rules.graduationLit());
    CHECK(bench.rules.takeBallRequests() == 0 && !bench.rules.takeLockRelease());
    for (std::size_t lamp = 0; lamp < std::to_underlying(LampId::Count); ++lamp) {
      CHECK(rules.lampState(static_cast<LampId>(lamp)) == LampState::Off);
    }
    const std::uint64_t score = bench.score();
    bench.bumper(3);
    bench.runway();
    bench.scoop();
    CHECK(bench.hangar() == Rules::HoleResult::Eject);
    CHECK(!bench.rules.onNudge());
    CHECK(bench.rules.onDrain() == Rules::DrainResult::GameOver);
    CHECK(!bench.rules.onBallLost({}));
    CHECK(bench.score() == score);
    // The tally runs out and the last line stays.
    bench.wait(30.0);
    CHECK(bench.message() == Rules::kGameOverMessage.text());
    // A new game is a clean one.
    bench.rules.startGame();
    CHECK(!rules.gameOver() && rules.score() == 0 && rules.ball() == 1);
    CHECK(rules.message() == std::string_view("LAUNCH THE BALL"));
    CHECK(rules.briefingLit() && rules.kickbackLit());
  }

  {  // The score never wraps, and neither does a share of it.
    RulesBench bench = started();
    pinball::Rules& rules = bench.rules;
    // Commencement is the largest single award; pile up far more than fits
    // by looping the largest awards with overdrive running.
    for (int round = 0; round < 400'000 && rules.score() < points::kMaxScore; ++round) {
      for (std::size_t lane = 0; lane < 3; ++lane) {
        bench.rollCall(lane);
      }
      bench.turbine();
      for (int i = 0; i < 40; ++i) {
        bench.runway();
        bench.skyway();
        bench.circuit(false);
        bench.turbine();
        bench.circuit(true);
      }
    }
    CHECK(rules.score() == points::kMaxScore);
    bench.runway();
    CHECK(rules.score() == points::kMaxScore);
    std::uint64_t sum = 0;
    for (std::size_t f = 0; f < pinball::kFeatureCount; ++f) {
      sum += rules.scoreFrom(static_cast<Feature>(f));
    }
    CHECK(sum == points::kMaxScore);
    // The bonus on top of a full score changes nothing either.
    bench.wait(Rules::kBallSaveSeconds);
    bench.drain();
    CHECK(rules.score() == points::kMaxScore);
  }

  {  // A step length that makes no sense is replaced, not divided by.
    for (const double step : {0.0, -1.0, 1e300, 1e-300}) {
      Rules rules(step);
      rules.onSlingshot(0);
      for (int i = 0; i < 10; ++i) {
        rules.tick();
      }
      CHECK(rules.score() == points::kSlingshot);
    }
  }

  {  // The fail-safe return: back on the plunger, no tilt, nothing lost.
    RulesBench bench = started();
    for (int i = 0; i < 3; ++i) {
      bench.rules.onNudge();
    }
    CHECK(bench.rules.tilted());
    bench.rules.onBallReturned();
    CHECK(!bench.rules.tilted() && !bench.rules.ballInPlay() && bench.rules.ball() == 1);
    CHECK(bench.message() == std::string_view("BALL SAVED"));
  }
}
