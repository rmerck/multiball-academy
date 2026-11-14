#include <cstddef>
#include <cstdint>
#include <string_view>

#include "Check.hpp"
#include "RulesBench.hpp"
#include "Suites.hpp"
#include "core/Courses.hpp"
#include "core/Rules.hpp"
#include "core/TableLayout.hpp"

// The bay doors and the lock, Academy multiball, Turbo multiball, and the
// three-ball limit (TABLE.md 4.7, 4.8), an event at a time.
namespace {

using pinball::DrainInfo;
using pinball::Feature;
using pinball::LampState;
using pinball::Multiball;
using pinball::Rules;
using pinball::Shot;
using pinball_test::RulesBench;
namespace points = pinball::points;
namespace table = pinball::table;
namespace tuning = pinball::tuning;
using LampId = pinball::table::LampId;

// The saver run out, no skill shot on offer.
RulesBench unsaved() {
  RulesBench bench;
  bench.rules.onSlingshot(0);
  bench.wait(Rules::kBallSaveSeconds);
  return bench;
}

// Opens the Hangar, locks two balls and sends the third in.
void startAcademy(RulesBench& bench) {
  for (int lock = 0; lock < Rules::kLocksNeeded; ++lock) {
    if (bench.doorsUp) {
      bench.doors();
    }
    CHECK(bench.hangar() == Rules::HoleResult::Lock);
    bench.step();
  }
  if (bench.doorsUp) {
    bench.doors();
  }
  CHECK(bench.hangar() == Rules::HoleResult::StartMultiball);
  CHECK(bench.rules.multiball() == Multiball::Academy);
}

// Fills the turbo meter with spins.
void fillTurbo(RulesBench& bench) {
  const int goal = bench.rules.turboGoal();
  bench.spin(goal - bench.rules.turboMeter());
}

}  // namespace

void testMultiball() {
  {  // 4.3 Bay doors: a hit marks a door; all three drop the bank and light
     // the lock; marks carry over between balls.
    RulesBench bench = unsaved();
    const Rules& rules = bench.rules;
    std::uint64_t before = bench.score();
    bench.door(1);
    CHECK(rules.doorMarked(1) && !rules.doorMarked(0) && !rules.doorMarked(2));
    bench.door(1);
    CHECK(bench.score() - before == points::kBayDoor + points::kBayDoorAgain);
    bench.step(5);
    CHECK(bench.doorsUp && !rules.lockLit());
    CHECK(bench.drain() == Rules::DrainResult::NextBall);
    CHECK(rules.doorMarked(1));
    before = bench.score();
    bench.door(0);
    bench.door(2);
    CHECK(bench.score() - before == 2 * points::kBayDoor + points::kBayDoorsOpen);
    CHECK(bench.message() == std::string_view("LOCK IS LIT") && rules.lockLit());
    CHECK(bench.doorsUp);
    bench.step();
    CHECK(!bench.doorsUp && bench.dropped == 1);
    CHECK(rules.lampState(LampId::ArrowHangar) == LampState::Blink);
    CHECK(rules.scoreFrom(Feature::BayDoors) ==
          3 * points::kBayDoor + points::kBayDoorAgain + points::kBayDoorsOpen);
    // They stay down, and the lit lock survives a drain.
    bench.wait(20.0);
    bench.drainForGood();
    CHECK(!bench.doorsUp && rules.lockLit());
  }

  {  // 4.7 Lock, lock, start: the first multiball of a game keeps the doors
     // down, so three Hangar shots after one bank start it.
    RulesBench bench = unsaved();
    const Rules& rules = bench.rules;
    bench.doors();
    std::uint64_t before = bench.score();
    CHECK(bench.hangar() == Rules::HoleResult::Lock);
    CHECK(bench.score() - before == points::kHangar);
    CHECK(rules.locksHeld() == 1 && bench.message() == std::string_view("BALL 1 LOCKED"));
    CHECK(rules.lampState(LampId::Lock1) == LampState::On &&
          rules.lampState(LampId::Lock2) == LampState::Off);
    // A new ball for the player, with its skill shot and its saver.
    CHECK(bench.balls.live == 1 && bench.balls.locked == 1);
    CHECK(rules.skillShotOffered() && rules.ballSaveActive() && rules.ball() == 1);
    CHECK(rules.multiball() == Multiball::None && rules.ballsLive() == 1);
    bench.step(240);
    CHECK(!bench.doorsUp && rules.lockLit());
    CHECK(bench.hangar() == Rules::HoleResult::Lock);
    CHECK(rules.locksHeld() == 2 && bench.message() == std::string_view("BALL 2 LOCKED"));
    CHECK(rules.lampState(LampId::Lock2) == LampState::On);
    CHECK(bench.total() == 3);
    bench.step(240);
    CHECK(!bench.doorsUp && rules.lockLit() && rules.multiball() == Multiball::None);
    before = bench.score();
    CHECK(bench.hangar() == Rules::HoleResult::StartMultiball);
    CHECK(bench.score() - before == points::kHangar);
    CHECK(bench.message() == std::string_view("ACADEMY MULTIBALL"));
    CHECK(rules.multiball() == Multiball::Academy && rules.academyMultiballs() == 1);
    CHECK(bench.balls.live == 3 && bench.balls.locked == 0 && rules.locksHeld() == 0);
    CHECK(rules.ballsLive() == 3 && bench.launched == 0);
    CHECK(rules.lampState(LampId::Lock1) == LampState::Off);
    // The doors come back up behind the balls and the lock is out.
    bench.wait(Rules::kDoorRaiseSeconds);
    CHECK(bench.doorsUp && !rules.lockLit() && !rules.doorMarked(0));
    CHECK(rules.graceSecondsLeft() > tuning::kAcademyGraceSeconds - 1.0);
    // Nothing locks during a multiball: the doors open the Hangar and no more.
    bench.doors();
    CHECK(bench.message() == std::string_view("BAY DOORS OPEN") && !rules.lockLit());
    CHECK(bench.hangar() == Rules::HoleResult::Eject);
    CHECK(bench.balls.locked == 0 && rules.multiball() == Multiball::Academy);
  }

  {  // 4.7 Jackpots on both ramps and both circuits: 50, 75, 100, 150, 150
     // thousand; the super jackpot at the Hangar after every third.
    RulesBench bench = unsaved();
    const Rules& rules = bench.rules;
    CHECK(!rules.jackpotLit(Shot::Runway) && !rules.multiballJackpotLit());
    startAcademy(bench);
    bench.wait(Rules::kDoorRaiseSeconds);
    for (const Shot shot : {Shot::CircuitLeft, Shot::Runway, Shot::Skyway, Shot::CircuitRight}) {
      CHECK(rules.jackpotLit(shot));
    }
    CHECK(!rules.jackpotLit(Shot::Hangar) && !rules.jackpotLit(Shot::Briefing) &&
          !rules.jackpotLit(Shot::Turbine));
    CHECK(rules.multiballJackpotLit() && rules.jackpotValue() == 50'000);
    CHECK(rules.lampState(LampId::ArrowCircuitLeft) == LampState::Blink);
    bench.runway();
    CHECK(rules.scoreFrom(Feature::Jackpots) == 50'000 && rules.jackpotValue() == 75'000);
    CHECK(bench.message() == std::string_view("JACKPOT"));
    bench.wait(tuning::kComboSeconds);
    bench.circuit(true);
    CHECK(rules.scoreFrom(Feature::Jackpots) == 125'000 && !rules.superJackpotLit());
    bench.wait(tuning::kComboSeconds);
    CHECK(bench.doorsUp);
    bench.skyway();
    CHECK(rules.scoreFrom(Feature::Jackpots) == 225'000 && rules.jackpotValue() == 150'000);
    // The third: the doors drop for the super jackpot.
    CHECK(rules.superJackpotLit());
    bench.step();
    CHECK(!bench.doorsUp && rules.doorMarked(0) && rules.doorMarked(2));
    CHECK(rules.lampState(LampId::ArrowHangar) == LampState::FastBlink);
    bench.wait(1.0);
    CHECK(bench.message() == std::string_view("SUPER JACKPOT IS LIT"));
    // Jackpots go on while it is lit, and do not count toward the next.
    bench.wait(tuning::kComboSeconds);
    bench.circuit(false);
    bench.wait(tuning::kComboSeconds);
    bench.runway();
    CHECK(rules.scoreFrom(Feature::Jackpots) == 525'000 && rules.jackpotValue() == 150'000);
    const std::uint64_t before = bench.score();
    CHECK(bench.hangar() == Rules::HoleResult::Eject);
    CHECK(bench.score() - before == points::kSuperJackpot);
    CHECK(bench.message() == std::string_view("SUPER JACKPOT") && !rules.superJackpotLit());
    CHECK(rules.rank() == 0);  // It gives no rank.
    bench.wait(Rules::kDoorRaiseSeconds);
    CHECK(bench.doorsUp && !rules.doorMarked(0));
    // Once per lighting: three more jackpots light it again.
    bench.wait(tuning::kComboSeconds);
    bench.runway();
    bench.runway();
    CHECK(!rules.superJackpotLit());
    bench.runway();
    CHECK(rules.superJackpotLit() && rules.jackpotsCollected() == 8);
    // The multiball ends with the super lit: doors up, nothing lit.
    bench.wait(tuning::kAcademyGraceSeconds);
    CHECK(!bench.lose() && !bench.lose());
    bench.step();
    CHECK(rules.multiball() == Multiball::None && !rules.superJackpotLit());
    CHECK(bench.message() == std::string_view("MULTIBALL OVER"));
    CHECK(!rules.jackpotLit(Shot::Runway) && !rules.lockLit());
    bench.wait(Rules::kDoorRaiseSeconds);
    CHECK(bench.doorsUp);
    const std::uint64_t jackpots = rules.scoreFrom(Feature::Jackpots);
    bench.runway();
    CHECK(rules.scoreFrom(Feature::Jackpots) == jackpots);
  }

  {  // 4.7 Grace: for 15 s every ball that drains is launched again; after
     // it, a drained ball is gone, and one ball left ends the multiball.
    RulesBench bench = unsaved();
    const Rules& rules = bench.rules;
    startAcademy(bench);
    for (int i = 0; i < 6; ++i) {
      CHECK(bench.lose());
      CHECK(bench.message() == std::string_view("BALL SAVED"));
      bench.step();
      CHECK(rules.multiball() == Multiball::Academy && bench.total() == 3);
    }
    bench.wait(tuning::kAcademyGraceSeconds);
    CHECK(!rules.ballSaveActive());
    CHECK(!bench.lose());
    bench.step();
    CHECK(rules.multiball() == Multiball::Academy && rules.ballsLive() == 2);
    CHECK(!bench.lose());
    bench.step();
    CHECK(rules.multiball() == Multiball::None);
    // Courses, rank, bonus and the ball in play are untouched.
    CHECK(rules.ball() == 1 && !rules.gameOver() && rules.briefingLit());
    // The last ball of a grace period drains alone: it is given back for
    // the player to plunge.
    RulesBench last = unsaved();
    startAcademy(last);
    last.wait(tuning::kAcademyGraceSeconds - 2.0);
    last.balls.live = 1;
    last.balls.free = 1;
    CHECK(last.drain() == Rules::DrainResult::BallSaved);
    CHECK(last.rules.multiball() == Multiball::None && last.rules.ball() == 1);
  }

  {  // 4.7 Later multiballs: the doors come back up after each lock; the
     // second start lights the extra ball.
    RulesBench bench = unsaved();
    const Rules& rules = bench.rules;
    startAcademy(bench);
    bench.wait(tuning::kAcademyGraceSeconds);
    bench.lose();
    bench.lose();
    bench.step();
    bench.wait(1.0);
    CHECK(rules.multiball() == Multiball::None && bench.doorsUp);
    bench.doors();
    CHECK(rules.lockLit());
    CHECK(bench.hangar() == Rules::HoleResult::Lock);
    bench.step(2);
    CHECK(bench.doorsUp && !rules.lockLit() && rules.locksHeld() == 1);
    CHECK(!rules.doorMarked(0));
    bench.doors();
    CHECK(bench.hangar() == Rules::HoleResult::Lock);
    bench.step(2);
    CHECK(bench.doorsUp && rules.locksHeld() == 2);
    bench.doors();
    CHECK(rules.lockLit() && !rules.extraBallLit());
    CHECK(bench.hangar() == Rules::HoleResult::StartMultiball);
    CHECK(rules.academyMultiballs() == 2 && rules.extraBallLit());
    // The ladder starts again with each multiball.
    CHECK(rules.jackpotValue() == 50'000);
  }

  {  // 4.7 Carry-over: locked balls survive drains and count in the bonus;
     // a lock on the last ball of the game is a lock like any other; the
     // bays are emptied when the game is over.
    RulesBench bench = unsaved();
    const Rules& rules = bench.rules;
    bench.doors();
    bench.hangar();
    bench.rules.onSlingshot(0);
    bench.wait(Rules::kBallSaveSeconds);
    const std::uint64_t before = bench.score();
    CHECK(bench.drain() == Rules::DrainResult::NextBall);
    CHECK(rules.bonusTally().locks == points::kBonusLock);
    CHECK(bench.score() - before == rules.bonusTally().total);
    CHECK(rules.locksHeld() == 1 && rules.lockLit() && bench.balls.locked == 1);
    CHECK(bench.drainForGood() == Rules::DrainResult::NextBall);
    CHECK(rules.ball() == 3);
    // The last ball is locked: the player still has a ball, and it is
    // still ball three.
    bench.rules.onSlingshot(0);
    bench.wait(Rules::kBallSaveSeconds);
    CHECK(bench.hangar() == Rules::HoleResult::Lock);
    CHECK(rules.locksHeld() == 2 && rules.ball() == 3 && !rules.gameOver());
    CHECK(bench.balls.live == 1 && rules.ballSaveActive());
    CHECK(bench.drainForGood() == Rules::DrainResult::GameOver);
    CHECK(rules.gameOver() && rules.locksHeld() == 0 && !rules.lockLit());
    CHECK(rules.bonusTally().locks == 2 * points::kBonusLock);
    CHECK(rules.lampState(LampId::Lock1) == LampState::Off);
    bench.wait(1.0);
    CHECK(bench.doorsUp);
    CHECK(bench.hangar() == Rules::HoleResult::Eject);
  }

  {  // 4.8 Turbo meter: +1 a bumper, +1 a spin, +5 a turbo target; full at
     // 50, then 75, 100; it carries over between balls.
    RulesBench bench = unsaved();
    const Rules& rules = bench.rules;
    bench.bumper(3);
    bench.spin(4);
    bench.rules.onTarget(0);
    CHECK(rules.turboMeter() == 12 && rules.turboGoal() == 50);
    CHECK(rules.turboFraction() > 0.23 && rules.turboFraction() < 0.25);
    CHECK(rules.lampState(LampId::TurboCharge) == LampState::Blink);
    bench.drain();
    CHECK(rules.turboMeter() == 12 && rules.ball() == 2);
    bench.wait(Rules::kBallSaveSeconds);
    bench.spin(30);
    CHECK(rules.lampState(LampId::TurboCharge) == LampState::FastBlink);
    bench.spin(7);
    CHECK(rules.turboMeter() == 49 && rules.multiball() == Multiball::None);
    bench.spin();
    // Full: a second ball is launched from the lane.
    CHECK(rules.turboMeter() == 0 && rules.turboGoal() == 75);
    CHECK(bench.message() == std::string_view("TURBO MULTIBALL"));
    CHECK(rules.multiball() == Multiball::Turbo && rules.turboMultiballs() == 1);
    CHECK(rules.ballsLive() == 2);
    bench.step();
    CHECK(bench.launched == 1 && bench.balls.live == 2 && rules.ballsLive() == 2);
    CHECK(rules.lampState(LampId::TurboCharge) == LampState::On);
    // Jackpots on the two ramps only, on the same ladder; no super jackpot.
    CHECK(rules.jackpotLit(Shot::Runway) && rules.jackpotLit(Shot::Skyway));
    CHECK(!rules.jackpotLit(Shot::CircuitLeft) && !rules.jackpotLit(Shot::CircuitRight));
    bench.runway();
    bench.runway();
    bench.runway();
    bench.runway();
    bench.wait(tuning::kComboSeconds);
    bench.circuit(false);
    CHECK(rules.scoreFrom(Feature::Jackpots) == 50'000 + 75'000 + 100'000 + 150'000);
    CHECK(!rules.superJackpotLit() && bench.doorsUp);
    // Grace 10 s, then it ends at one ball.
    CHECK(rules.ballSaveActive());
    CHECK(bench.lose());
    bench.wait(tuning::kTurboGraceSeconds);
    CHECK(!rules.ballSaveActive() && rules.multiball() == Multiball::Turbo);
    CHECK(!bench.lose());
    bench.step();
    CHECK(rules.multiball() == Multiball::None);
    CHECK(bench.message() == std::string_view("MULTIBALL OVER"));
    // The next fill is at 75.
    bench.spin(74);
    CHECK(rules.multiball() == Multiball::None);
    bench.spin();
    CHECK(rules.multiball() == Multiball::Turbo && rules.turboGoal() == 100);
    CHECK(rules.jackpotValue() == 50'000);
  }

  {  // 4.7 The three-ball limit counts free, held, waiting, queued and locked
     // balls. A ball is never asked for past it.
    // Two locked and one in play: a full meter pays its bonus.
    RulesBench bench = unsaved();
    const Rules& rules = bench.rules;
    bench.doors();
    bench.hangar();
    bench.hangar();
    bench.step();
    CHECK(bench.total() == 3 && rules.locksHeld() == 2);
    std::uint64_t before = bench.score();
    fillTurbo(bench);
    bench.step();
    CHECK(bench.message() == std::string_view("TURBO BONUS"));
    CHECK(rules.multiball() == Multiball::None && bench.launched == 0 && bench.refused == 0);
    CHECK(rules.scoreFrom(Feature::TurboBonus) == points::kTurboBonus);
    CHECK(bench.score() - before == 50 * points::kSpin + points::kTurboBonus);
    CHECK(rules.turboGoal() == 75 && rules.turboMeter() == 0);

    // One locked: the second ball makes three in all, and the Hangar then
    // locks nothing more until the multiball is over.
    RulesBench one = unsaved();
    one.doors();
    one.hangar();
    one.wait(Rules::kBallSaveSeconds);  // The served ball's saver.
    fillTurbo(one);
    one.step();
    CHECK(one.rules.multiball() == Multiball::Turbo && one.launched == 1 && one.total() == 3);
    CHECK(!one.rules.lockLit());
    CHECK(one.hangar() == Rules::HoleResult::Eject);
    CHECK(one.rules.locksHeld() == 1 && one.balls.locked == 1);
    // The open Hangar is still open when the multiball is over.
    one.wait(tuning::kTurboGraceSeconds);
    one.lose();
    one.step();
    CHECK(one.rules.multiball() == Multiball::None && !one.doorsUp && one.rules.lockLit());
    // A full meter again with one locked and one in play: a new multiball.
    fillTurbo(one);
    one.step();
    CHECK(one.rules.multiball() == Multiball::Turbo && one.total() == 3);
    // And again with three there already: the bonus, and nothing asked for.
    fillTurbo(one);
    one.step();
    CHECK(one.total() == 3 && one.refused == 0 && one.launched == 2);
    CHECK(one.rules.scoreFrom(Feature::TurboBonus) == points::kTurboBonus);

    // A ball still waiting to be served counts: two fills before the first
    // ball has been launched cannot make four.
    RulesBench queued = unsaved();
    queued.doors();
    queued.hangar();
    queued.step();
    fillTurbo(queued);  // Asked for, not yet taken by the table.
    CHECK(queued.rules.ballsLive() == 2);
    fillTurbo(queued);
    queued.step();
    CHECK(queued.launched == 1 && queued.refused == 0 && queued.total() == 3);
    CHECK(queued.rules.scoreFrom(Feature::TurboBonus) == points::kTurboBonus);

    // 4.7 Add a ball: during an Academy multiball with fewer than three
    // live, a full meter launches one; with three, it pays.
    RulesBench academy = unsaved();
    startAcademy(academy);
    academy.wait(tuning::kAcademyGraceSeconds);
    academy.lose();
    academy.step();
    CHECK(academy.rules.ballsLive() == 2);
    fillTurbo(academy);
    academy.step();
    CHECK(academy.launched == 1 && academy.total() == 3);
    CHECK(academy.rules.multiball() == Multiball::Academy && academy.rules.turboMultiballs() == 0);
    before = academy.rules.scoreFrom(Feature::TurboBonus);
    fillTurbo(academy);
    academy.step();
    CHECK(academy.rules.scoreFrom(Feature::TurboBonus) - before == points::kTurboBonus);
    CHECK(academy.launched == 1 && academy.refused == 0);
  }

  {  // Stacking: a course runs through a multiball and its shots count for
     // both; a multiball may start during a course.
    RulesBench bench = unsaved();
    const Rules& rules = bench.rules;
    bench.bumper();  // The offer moves to RUNWAY DRILLS.
    bench.scoop();
    CHECK(rules.course() == std::to_underlying(pinball::CourseId::RunwayDrills));
    fillTurbo(bench);
    bench.step();
    CHECK(rules.multiball() == Multiball::Turbo && rules.courseRunning());
    const std::uint64_t before = bench.score();
    bench.runway();
    CHECK(bench.score() - before == points::kRamp + 50'000 + 20'000);
    bench.runway();
    bench.runway();
    CHECK(rules.coursePassed(1) && rules.multiball() == Multiball::Turbo && rules.rank() == 1);
    // The course clock runs on in a multiball, and is not stopped by one
    // ball in a hold while another is free.
    bench.runway();
    bench.runway();  // Relit.
    bench.scoop();
    CHECK(rules.courseRunning());
    const double left = rules.courseSecondsLeft();
    bench.balls.free = 1;
    bench.wait(5.0);
    CHECK(rules.courseSecondsLeft() < left - 4.9);
  }

  {  // Multiball ended by a tilt: no ball comes back, the last drain ends
     // the ball, nothing is left running, and the locked balls stay.
    RulesBench bench = unsaved();
    const Rules& rules = bench.rules;
    bench.doors();
    bench.hangar();
    bench.step();
    bench.rules.onSlingshot(0);
    fillTurbo(bench);
    bench.step();
    CHECK(rules.multiball() == Multiball::Turbo && rules.ballSaveActive());
    for (int i = 0; i < 3; ++i) {
      bench.rules.onNudge();
    }
    CHECK(rules.tilted() && !rules.ballSaveActive());
    CHECK(!bench.lose(DrainInfo{.afterEject = true}));
    const std::uint64_t before = bench.score();
    CHECK(bench.drain() == Rules::DrainResult::NextBall);
    CHECK(rules.multiball() == Multiball::None && !rules.tilted() && rules.ball() == 2);
    CHECK(bench.score() == before && rules.locksHeld() == 1 && bench.balls.locked == 1);
    CHECK(bench.message() == std::string_view("NEXT BALL"));
    CHECK(rules.lockLit());
  }

  {  // Two balls drain in the same step with no grace: the first is lost,
     // the second ends the ball, once.
    RulesBench bench = unsaved();
    const Rules& rules = bench.rules;
    fillTurbo(bench);
    bench.step();
    bench.wait(tuning::kTurboGraceSeconds);
    CHECK(rules.multiball() == Multiball::Turbo);
    CHECK(!bench.rules.onBallLost({}));
    CHECK(bench.drain() == Rules::DrainResult::NextBall);
    CHECK(rules.ball() == 2 && rules.multiball() == Multiball::None);
    bench.step(3);
    CHECK(rules.ball() == 2 && rules.multiball() == Multiball::None);
    // With grace both come back: the first asked for again, the second
    // given to the player. The multiball is over all the same (one ball).
    RulesBench graced = unsaved();
    fillTurbo(graced);
    graced.step();
    CHECK(graced.rules.onBallLost({}));
    CHECK(graced.rules.onDrain() == Rules::DrainResult::BallSaved);
    CHECK(graced.rules.ball() == 1);
  }

  {  // With multiball switched off (the single-ball bench) the Hangar returns
     // every ball and shuts behind it, and a full meter pays its bonus.
    RulesBench bench = unsaved();
    bench.rules.setMultiballEnabled(false);
    bench.doors();
    CHECK(!bench.doorsUp && !bench.rules.lockLit());
    CHECK(bench.message() == std::string_view("BAY DOORS OPEN"));
    CHECK(bench.hangar() == Rules::HoleResult::Eject);
    bench.wait(Rules::kDoorRaiseSeconds);
    CHECK(bench.doorsUp && !bench.rules.doorMarked(0));
    fillTurbo(bench);
    bench.step();
    CHECK(bench.launched == 0 && bench.rules.multiball() == Multiball::None);
  }
}
