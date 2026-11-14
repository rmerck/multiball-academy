#include <cstddef>
#include <cstdint>
#include <string_view>

#include "Check.hpp"
#include "RulesBench.hpp"
#include "Suites.hpp"
#include "core/Courses.hpp"
#include "core/DisplayText.hpp"
#include "core/Rules.hpp"
#include "core/TableLayout.hpp"
#include "platform/Font5x7.hpp"

// Courses, rank and Graduation Day (TABLE.md 4.5, 4.6, 4.12), and the
// arithmetic of the display limits every name and message is held to.
namespace {

using pinball::CourseId;
using pinball::Feature;
using pinball::LampState;
using pinball::Multiball;
using pinball::Rules;
using pinball::Shot;
using pinball_test::RulesBench;
namespace display = pinball::display;
namespace points = pinball::points;
namespace table = pinball::table;
namespace tuning = pinball::tuning;
using LampId = pinball::table::LampId;

constexpr std::size_t idx(CourseId id) { return std::to_underlying(id); }

// One ball at a time, the saver run out, no skill shot on offer.
RulesBench single() {
  RulesBench bench;
  bench.rules.setMultiballEnabled(false);
  bench.rules.onSlingshot(0);
  bench.wait(Rules::kBallSaveSeconds);
  return bench;
}

// Lights the Briefing room if it is not, picks a course with the bumpers
// and starts it.
void start(RulesBench& bench, CourseId id) {
  bench.wait(tuning::kComboSeconds);
  for (int i = 0; i < 4 && !bench.rules.briefingLit(); ++i) {
    bench.runway();
  }
  CHECK(bench.rules.briefingLit());
  for (int i = 0; i < 6 && bench.rules.courseOffered() != idx(id); ++i) {
    bench.bumper();
  }
  CHECK(bench.rules.courseOffered() == idx(id));
  bench.scoop();
  CHECK(bench.rules.course() == idx(id));
}

// Does what the running course asks for, with shots that make no combos.
void finish(RulesBench& bench) {
  const std::size_t course = bench.rules.course();
  for (int i = 0; i < 64 && bench.rules.course() == course; ++i) {
    switch (static_cast<CourseId>(course)) {
      case CourseId::BumperBasics:
        bench.bumper();
        break;
      case CourseId::RunwayDrills:
        bench.runway();
        break;
      case CourseId::CircuitTraining:
        bench.circuit(false);
        break;
      case CourseId::HangarSafety:
        if (bench.rules.goalText() == pinball::kGoalEnterHangar) {
          bench.hangar();
        } else {
          bench.doors();
        }
        break;
      case CourseId::TurbineTheory:
        bench.spin();
        break;
      case CourseId::Navigation:
        bench.wait(tuning::kComboSeconds);
        switch (bench.rules.navigationShot()) {
          case Shot::CircuitLeft:
            bench.circuit(false);
            break;
          case Shot::Runway:
            bench.runway();
            break;
          case Shot::Hangar:
            bench.hangar();
            break;
          case Shot::Briefing:
            bench.scoop();
            break;
          case Shot::Turbine:
            bench.turbine();
            break;
          case Shot::Skyway:
            bench.skyway();
            break;
          case Shot::CircuitRight:
            bench.circuit(true);
            break;
        }
        break;
    }
  }
  CHECK(!bench.rules.courseRunning());
}

void pass(RulesBench& bench, CourseId id) {
  start(bench, id);
  finish(bench);
  CHECK(bench.rules.coursePassed(idx(id)));
}

void passAll(RulesBench& bench) {
  for (std::size_t course = 0; course < pinball::kCourseCount; ++course) {
    pass(bench, static_cast<CourseId>(course));
  }
}

// Makes one of the seven major shots.
void shoot(RulesBench& bench, Shot shot) {
  switch (shot) {
    case Shot::CircuitLeft:
      bench.circuit(false);
      break;
    case Shot::Runway:
      bench.runway();
      break;
    case Shot::Hangar:
      bench.hangar();
      break;
    case Shot::Briefing:
      bench.scoop();
      break;
    case Shot::Turbine:
      bench.turbine();
      break;
    case Shot::Skyway:
      bench.skyway();
      break;
    case Shot::CircuitRight:
      bench.circuit(true);
      break;
  }
}

}  // namespace

void testCourses() {
  {  // The display limits. They are worked out from the display windows and
     // the font, and every rank and message is held to them when the game
     // is compiled: this pins the arithmetic behind that.
    static_assert(display::kAdvance == pinball::font::kAdvance &&
                      display::kFirstChar == pinball::font::kFirstChar &&
                      display::kLastChar == pinball::font::kLastChar,
                  "the limits assume the font the display draws with");
    // 440 pixels inside a window: a line of n characters is 6n - 1 font
    // pixels wide.
    static_assert(display::charsThatFit(240.0F, 3.0F) == 13);  // 77 * 3 = 231, and 14 need 249.
    // At 2.5 units to the font pixel: the strip's rank window is 148 inside
    // (10 characters are 59 * 2.5 = 147.5), its course window 388 (26 are
    // 155 * 2.5 = 387.5) and the panel's message window 440 (29 are 433.75).
    static_assert(display::charsThatFit(148.0F, 2.5F) == 10 &&
                  display::charsThatFit(147.0F, 2.5F) == 9);
    static_assert(display::charsThatFit(388.0F, 2.5F) == 26 &&
                  display::charsThatFit(387.0F, 2.5F) == 25);
    static_assert(display::charsThatFit(440.0F, 2.5F) == 29);
    static_assert(display::kMaxRankNameLength == 10 && display::kMaxMessageLength == 29);
    static_assert(display::charsThatFit(440.0F, 4.0F) == 18);  // 107 * 4 = 428, and 19 need 452.
    static_assert(display::charsThatFit(440.0F, 3.0F) == 24);  // 143 * 3 = 429, and 25 need 447.
    static_assert(display::charsThatFit(440.0F, 2.0F) == 36);  // 215 * 2 = 430, and 37 need 442.
    static_assert(display::charsThatFit(10.0F, 2.0F) == 1);
    static_assert(display::charsThatFit(9.0F, 2.0F) == 0);
    static_assert(display::charsThatFit(0.0F, 2.0F) == 0);
    static_assert(display::charsThatFit(440.0F, 0.0F) == 0);
    static_assert(display::fits("COURSE PASSED  RANK UP", display::kMaxMessageLength));
    static_assert(display::fits("FIELD TRIP: 101", 15) && !display::fits("FIELD TRIP: 101", 14));
    static_assert(!display::fits("", 10));
    static_assert(!display::fits(" PAD", 10) && !display::fits("PAD ", 10));
    // Lower case, and anything else the font has no glyph for, is refused.
    static_assert(!display::fits("Rookie", 10) && !display::fits("A\tB", 10) &&
                  !display::fits("A~B", 10) && !display::fits("\xC9", 10));
    // A line made while the game runs: the longest label with the largest
    // number is exactly the window, and nothing can be added past it.
    static_assert(display::numbered("THIRTEEN CHAR", display::kMaxNumber).view().size() ==
                  display::kMaxMessageLength);
    display::Text text = display::numbered("THIRTEEN CHAR", ~std::uint64_t{0});
    text.append("MORE");
    CHECK(text.view().size() == display::kMaxMessageLength);
    CHECK(display::fits(text.view(), display::kMaxMessageLength));
    // What the rules say comes through unchanged, and fits.
    Rules rules(pinball_test::kRuleStep);
    CHECK(display::fits(rules.message(), display::kMaxMessageLength));
    CHECK(rules.message() == display::Message("LAUNCH THE BALL").text());
  }

  {  // The rank ladder: seven, every name fits wherever a rank is shown, no
     // two are the same, and an index past the end is the top rank.
    static_assert(pinball::kRankCount == 7);
    for (std::size_t rank = 0; rank < pinball::kRankCount; ++rank) {
      CHECK(display::fits(pinball::rankName(rank), display::kMaxRankNameLength));
      CHECK(pinball::rankName(rank) != std::string_view("NAVIGATOR"));
      CHECK(pinball::rankName(rank) != std::string_view("FLIGHT DEAN"));
      for (std::size_t other = rank + 1; other < pinball::kRankCount; ++other) {
        CHECK(pinball::rankName(rank) != pinball::rankName(other));
      }
    }
    CHECK(pinball::rankName(0) == std::string_view("ROOKIE"));
    CHECK(pinball::rankName(5) == std::string_view("INSTRUCTOR"));
    CHECK(pinball::rankName(6) == std::string_view("CHANCELLOR"));
    CHECK(pinball::rankName(999) == pinball::rankName(pinball::kRankCount - 1));
    // The catalogue as the design gives it.
    CHECK(pinball::kCourses[0].name == std::string_view("BUMPER BASICS 101"));
    CHECK(pinball::kCourses[5].name == std::string_view("NAVIGATION 302"));
  }

  {  // 4.5 Choice: the Briefing room is lit at the start; the bumpers move
     // the offer through the courses not yet passed; the scoop starts it.
    RulesBench bench = single();
    const Rules& rules = bench.rules;
    CHECK(rules.briefingLit() && rules.courseOffered() == 0);
    CHECK(rules.lampState(LampId::Course1) == LampState::Blink);
    CHECK(rules.lampState(LampId::ArrowBriefing) == LampState::Blink);
    bench.bumper();
    CHECK(rules.courseOffered() == 1 && rules.lampState(LampId::Course2) == LampState::Blink);
    CHECK(rules.lampState(LampId::Course1) == LampState::Off);
    bench.bumper(5);
    CHECK(rules.courseOffered() == 0);
    bench.bumper(2);
    bench.scoop();
    CHECK(rules.course() == idx(CourseId::CircuitTraining) && rules.coursesStarted() == 1);
    CHECK(bench.message() == std::string_view("CLASS IN SESSION"));
    CHECK(rules.courseName() == std::string_view("CIRCUIT TRAINING 201"));
    CHECK(rules.goalText() == std::string_view("COMPLETE A CIRCUIT") && rules.goalRemaining() == 2);
    CHECK(rules.courseSecondsLeft() > 39.9 && rules.courseSecondsLeft() <= 40.0);
    CHECK(rules.lampState(LampId::Course3) == LampState::FastBlink);
    CHECK(rules.lampState(LampId::ArrowCircuitLeft) == LampState::Blink &&
          rules.lampState(LampId::ArrowCircuitRight) == LampState::Blink);
    CHECK(!rules.briefingLit());
    // One at a time: the scoop and the bumpers change nothing now.
    bench.bumper(3);
    bench.scoop();
    CHECK(rules.course() == idx(CourseId::CircuitTraining) && rules.coursesStarted() == 1);
  }

  {  // 4.5 Each course: its task, its award per step, its pass award, and a
     // rank for each of the first five.
    RulesBench bench = single();
    const Rules& rules = bench.rules;

    start(bench, CourseId::BumperBasics);
    CHECK(rules.goalText() == std::string_view("HIT THE THRUSTERS") && rules.goalRemaining() == 12);
    CHECK(rules.lampState(LampId::ArrowTurbine) == LampState::Blink);
    bench.bumper(11);
    CHECK(rules.scoreFrom(Feature::Course1) == 11 * 3'000 && rules.goalRemaining() == 1);
    bench.bumper();
    CHECK(rules.scoreFrom(Feature::Course1) == 12 * 3'000 + 100'000);
    CHECK(!rules.courseRunning() && rules.coursePassed(0) && rules.rank() == 1);
    CHECK(bench.message() == std::string_view("COURSE PASSED  RANK UP"));
    CHECK(rules.lampState(LampId::Course1) == LampState::On);
    // Relit by two ramps.
    CHECK(!rules.briefingLit() && rules.rampsForBriefing() == 2);
    CHECK(rules.goalText() == pinball::kGoalBriefingUnlit);
    CHECK(rules.lampState(LampId::ArrowRunway) == LampState::On);
    bench.wait(tuning::kComboSeconds);
    bench.runway();
    CHECK(bench.message() == std::string_view("RAMPS FOR BRIEFING") && rules.rampsForBriefing() == 1);
    bench.runway();
    CHECK(bench.message() == std::string_view("BRIEFING IS LIT") && rules.briefingLit());
    CHECK(rules.goalText() == pinball::kGoalBriefingLit);
    CHECK(rules.courseOffered() == 1);  // The first is passed: it is not offered again.

    start(bench, CourseId::RunwayDrills);
    bench.runway();
    bench.runway();
    bench.runway();
    CHECK(rules.scoreFrom(Feature::Course2) == 3 * 20'000 && rules.goalRemaining() == 1);
    bench.runway();
    CHECK(rules.scoreFrom(Feature::Course2) == 4 * 20'000 + 125'000 && rules.rank() == 2);

    start(bench, CourseId::CircuitTraining);
    bench.circuit(true);
    bench.circuit(true);
    CHECK(rules.scoreFrom(Feature::Course3) == 2 * 30'000 + 150'000 && rules.rank() == 3);
    // The third course passed lights the extra ball, and from now on the
    // Briefing room takes three ramps.
    CHECK(rules.extraBallLit() && rules.rampsForBriefing() == 3);

    // HANGAR SAFETY: the doors are raised for it; three doors, then the
    // Hangar, and the entry locks nothing.
    bench.doors();
    CHECK(!bench.doorsUp);
    start(bench, CourseId::HangarSafety);
    bench.step(2);
    CHECK(bench.doorsUp && !rules.doorMarked(0) && !rules.lockLit());
    CHECK(rules.goalText() == std::string_view("HIT THE BAY DOORS") && rules.goalRemaining() == 3);
    // The doors still to hit blink on their faces; a marked one is lit.
    CHECK(rules.doorLampState(0) == pinball::LampState::Blink);
    bench.door(0);
    CHECK(rules.doorLampState(0) == pinball::LampState::On &&
          rules.doorLampState(1) == pinball::LampState::Blink);
    CHECK(rules.doorLampState(table::kDropTargets.size()) == pinball::LampState::Off);
    bench.door(0);  // Marked already: nothing for the course.
    CHECK(rules.scoreFrom(Feature::Course4) == 10'000 && rules.goalRemaining() == 2);
    bench.door(1);
    bench.door(2);
    bench.step();
    CHECK(!bench.doorsUp && rules.goalText() == pinball::kGoalEnterHangar);
    CHECK(rules.goalRemaining() == 1 && !rules.lockLit());
    CHECK(bench.message() == std::string_view("BAY DOORS OPEN"));
    CHECK(rules.lampState(LampId::ArrowHangar) == LampState::Blink);
    CHECK(bench.hangar() == Rules::HoleResult::Eject);
    CHECK(rules.scoreFrom(Feature::Course4) == 3 * 10'000 + 150'000 && rules.rank() == 4);
    CHECK(rules.locksHeld() == 0);
    bench.wait(Rules::kDoorRaiseSeconds);
    CHECK(bench.doorsUp && !rules.doorMarked(1));

    // TURBINE THEORY: the award falls 5,000 a second from 250,000.
    start(bench, CourseId::TurbineTheory);
    CHECK(rules.hurryUpValue() == 250'000 && rules.goalRemaining() == 36);
    bench.spin(35);
    bench.wait(10.4);
    CHECK(rules.hurryUpValue() == 200'000);
    bench.spin();
    CHECK(rules.scoreFrom(Feature::Course5) == 200'000 && rules.rank() == 5);
    CHECK(rules.hurryUpValue() == 0);

    // NAVIGATION: the lit shot moves on when it is made, or after 8 s.
    start(bench, CourseId::Navigation);
    CHECK(rules.navigationShot() == Shot::CircuitLeft && rules.goalRemaining() == 3);
    CHECK(rules.lampState(LampId::ArrowCircuitLeft) == LampState::Blink);
    bench.runway();  // Not the lit shot.
    CHECK(rules.scoreFrom(Feature::Course6) == 0);
    bench.circuit(false);
    CHECK(rules.scoreFrom(Feature::Course6) == 30'000 && rules.navigationShot() == Shot::Runway);
    bench.wait(pinball::kNavigationHopSeconds);
    // The Hangar is passed over while its doors stand.
    CHECK(rules.navigationShot() == Shot::Briefing);
    CHECK(rules.lampState(LampId::ArrowBriefing) == LampState::Blink);
    bench.scoop();
    CHECK(rules.navigationShot() == Shot::Turbine && rules.goalRemaining() == 1);
    bench.wait(pinball::kNavigationHopSeconds);
    bench.wait(pinball::kNavigationHopSeconds);
    CHECK(rules.navigationShot() == Shot::CircuitRight);
    bench.wait(pinball::kNavigationHopSeconds);
    CHECK(rules.navigationShot() == Shot::CircuitLeft);  // Round again.
    bench.wait(tuning::kComboSeconds);
    bench.circuit(false);
    CHECK(rules.scoreFrom(Feature::Course6) == 3 * 30'000 + 200'000);
    // The sixth pass: no new rank name; Graduation Day is what it opens.
    CHECK(rules.rank() == 5 && rules.coursesPassed() == 6);
    CHECK(bench.message() == std::string_view("ALL COURSES PASSED"));
    for (std::size_t lamp = 0; lamp < 6; ++lamp) {
      CHECK(rules.lampState(static_cast<LampId>(std::to_underlying(LampId::Course1) + lamp)) ==
            LampState::On);
    }
    CHECK(!rules.briefingLit() && rules.rampsForBriefing() == 0);
  }

  {  // The Hangar is the lit NAVIGATION shot when its doors are down.
    RulesBench bench = single();
    bench.doors();
    start(bench, CourseId::Navigation);
    bench.circuit(false);
    bench.wait(tuning::kComboSeconds);
    bench.runway();
    CHECK(bench.rules.navigationShot() == Shot::Hangar);
    bench.hangar();
    CHECK(!bench.rules.courseRunning() && bench.rules.coursePassed(5));
  }

  {  // 4.5 The clock: it runs out to COURSE FAILED with nothing lost; it
     // stands while no ball is free; locked balls do not stop it.
    RulesBench bench = single();
    const Rules& rules = bench.rules;
    start(bench, CourseId::RunwayDrills);
    bench.runway();
    const std::uint64_t score = bench.score();
    bench.wait(20.0);
    CHECK(rules.courseRunning() && rules.courseSecondsLeft() < 15.1);
    // In a hold or on the plunger: stopped.
    bench.balls.free = 0;
    bench.wait(100.0);
    CHECK(rules.courseRunning() && rules.courseSecondsLeft() > 14.0);
    // A ball locked beside the one in play changes nothing: it runs.
    bench.balls.free = 1;
    bench.balls.locked = 2;
    bench.wait(10.0);
    CHECK(rules.courseRunning() && rules.courseSecondsLeft() < 5.1);
    bench.balls.locked = 0;
    bench.wait(5.1);
    CHECK(!rules.courseRunning() && !rules.coursePassed(1) && rules.rank() == 0);
    CHECK(bench.message() == std::string_view("COURSE FAILED"));
    CHECK(bench.score() == score);
    // It stays available: relit by ramps, offered again.
    CHECK(rules.rampsForBriefing() == 2);
    start(bench, CourseId::RunwayDrills);
    CHECK(rules.goalRemaining() == 4);  // From the beginning.
    // A ramp during a course does not count toward relighting.
    finish(bench);
    CHECK(rules.rampsForBriefing() == 2);
  }

  {  // 4.5 Drain: the course carries over with its progress and at least
     // 15 s; a tilt fails it at the drain.
    RulesBench bench = single();
    const Rules& rules = bench.rules;
    start(bench, CourseId::RunwayDrills);
    bench.runway();
    bench.wait(30.0);
    CHECK(rules.courseSecondsLeft() < 5.1);
    CHECK(bench.drain() == Rules::DrainResult::NextBall);
    CHECK(rules.courseRunning() && rules.goalRemaining() == 3);
    CHECK(rules.courseSecondsLeft() > 14.9 && rules.courseSecondsLeft() <= 15.0);
    // With more than that left it keeps what it has.
    bench.runway();
    bench.runway();
    bench.runway();
    start(bench, CourseId::CircuitTraining);
    bench.wait(5.0);
    bench.rules.onSlingshot(0);
    bench.wait(Rules::kBallSaveSeconds);
    const double left = rules.courseSecondsLeft();
    CHECK(left > 15.0);
    bench.drain();
    CHECK(rules.courseRunning() && rules.courseSecondsLeft() == left);
    // Tilt.
    for (int i = 0; i < 3; ++i) {
      bench.rules.onNudge();
    }
    CHECK(rules.tilted() && rules.courseRunning());
    bench.wait(60.0);  // The clock does not run a tilted course out.
    CHECK(rules.courseRunning());
    CHECK(bench.drain() == Rules::DrainResult::GameOver);
    CHECK(!rules.courseRunning() && !rules.coursePassed(2));
  }

  {  // 4.12 Graduation Day, from six courses to CHANCELLOR and after.
    RulesBench bench = single();
    Rules& rules = bench.rules;
    passAll(bench);
    CHECK(rules.rank() == 5 && rules.coursesPassed() == 6);
    // With one ball at a time it cannot start; with multiball it is lit.
    CHECK(!rules.graduationLit());
    rules.setMultiballEnabled(true);
    CHECK(rules.graduationLit());
    CHECK(rules.lampState(LampId::Graduation) == LampState::Blink);
    CHECK(rules.lampState(LampId::ArrowBriefing) == LampState::FastBlink);
    CHECK(rules.goalText() == pinball::kGoalBriefingLit);
    bench.wait(tuning::kComboSeconds);
    bench.scoop();
    CHECK(rules.graduationRunning() && rules.multiball() == Multiball::Graduation);
    CHECK(bench.message() == std::string_view("GRADUATION DAY"));
    CHECK(rules.courseName() == std::string_view("GRADUATION DAY"));
    // Balls are launched until three are live; 30 s grace.
    CHECK(bench.launched == 2 && bench.total() == 3 && bench.refused == 0);
    CHECK(rules.ballsLive() == 3 && rules.ballSaveActive());
    CHECK(rules.graceSecondsLeft() > 29.0);
    // Part one: all seven lit, the doors held down for the Hangar.
    CHECK(!bench.doorsUp && rules.examShotsLeft() == 7 && rules.goalRemaining() == 7);
    CHECK(rules.goalText() == std::string_view("SHOOT EVERY LIT ARROW"));
    for (std::size_t lamp = 0; lamp < 7; ++lamp) {
      CHECK(rules.lampState(static_cast<LampId>(lamp)) == LampState::Blink);
    }
    // Lock, course and jackpot rules are suspended.
    CHECK(!rules.lockLit() && !rules.briefingLit() && !rules.jackpotLit(Shot::Runway));
    const std::uint64_t exam = rules.scoreFrom(Feature::Graduation);
    CHECK(exam == 0);
    CHECK(bench.hangar() == Rules::HoleResult::Eject);
    CHECK(rules.scoreFrom(Feature::Graduation) == 100'000 && rules.examShotMade(Shot::Hangar));
    CHECK(bench.message() == std::string_view("FINAL EXAM  6 TO GO"));
    CHECK(rules.locksHeld() == 0 && bench.balls.locked == 0);
    // Made once: again pays nothing, and the doors come back up.
    bench.wait(Rules::kDoorRaiseSeconds);
    CHECK(bench.doorsUp);
    CHECK(rules.lampState(LampId::ArrowHangar) == LampState::Off);
    for (const Shot shot : {Shot::CircuitLeft, Shot::Runway, Shot::Briefing, Shot::Turbine,
                            Shot::Skyway}) {
      shoot(bench, shot);
      shoot(bench, shot);
    }
    CHECK(rules.examShotsLeft() == 1 && bench.message() == std::string_view("FINAL EXAM  1 TO GO"));
    CHECK(rules.scoreFrom(Feature::Graduation) == 600'000);
    CHECK(!rules.courseRunning() && rules.coursesStarted() == 6);
    shoot(bench, Shot::CircuitRight);
    // Part two: the Hangar alone, doors down.
    CHECK(rules.examShotsLeft() == 0 && bench.message() == std::string_view("SHOOT THE HANGAR"));
    CHECK(rules.goalText() == std::string_view("SHOOT THE HANGAR") && rules.goalRemaining() == 0);
    bench.step();
    CHECK(!bench.doorsUp);
    CHECK(rules.lampState(LampId::ArrowHangar) == LampState::FastBlink &&
          rules.lampState(LampId::ArrowRunway) == LampState::Off);
    shoot(bench, Shot::Runway);
    CHECK(rules.scoreFrom(Feature::Graduation) == 700'000 && rules.rank() == 5);
    const std::uint64_t before = bench.score();
    CHECK(bench.hangar() == Rules::HoleResult::Eject);
    CHECK(bench.score() - before == 1'000'000);
    CHECK(rules.rank() == pinball::kTopRank && bench.message() == std::string_view("CHANCELLOR"));
    // Victory laps: every major shot 50,000 until one ball is left.
    shoot(bench, Shot::Turbine);
    shoot(bench, Shot::Briefing);
    bench.hangar();
    CHECK(rules.scoreFrom(Feature::Graduation) == 1'700'000 + 3 * 50'000);
    // Grace: a drain inside it comes back.
    CHECK(bench.lose());
    bench.wait(tuning::kGraduationGraceSeconds);
    CHECK(rules.graduationRunning());
    CHECK(!bench.lose());
    bench.step();
    CHECK(rules.graduationRunning());
    CHECK(!bench.lose());
    bench.step();
    // After: the courses are open again and everything else is kept.
    CHECK(!rules.graduationRunning() && rules.multiball() == Multiball::None);
    CHECK(bench.message() == std::string_view("COURSES REOPENED"));
    CHECK(rules.graduations() == 1 && rules.coursesPassed() == 0 && rules.briefingLit());
    CHECK(rules.rank() == pinball::kTopRank && !rules.graduationLit());
    CHECK(rules.lampState(LampId::Graduation) == LampState::On);
    bench.wait(Rules::kDoorRaiseSeconds);
    CHECK(bench.doorsUp && !rules.doorMarked(0) && rules.locksHeld() == 0);
    // Second time round: twice the pass award, the rank cannot change.
    rules.setMultiballEnabled(false);
    const std::uint64_t first = rules.scoreFrom(Feature::Course2);
    pass(bench, CourseId::RunwayDrills);
    CHECK(rules.scoreFrom(Feature::Course2) - first == 4 * 20'000 + 2 * 125'000);
    CHECK(rules.rank() == pinball::kTopRank);
    CHECK(bench.message() == std::string_view("COURSE PASSED"));
    // A second Graduation Day can be qualified.
    for (const CourseId id : {CourseId::BumperBasics, CourseId::CircuitTraining,
                              CourseId::HangarSafety, CourseId::TurbineTheory,
                              CourseId::Navigation}) {
      pass(bench, id);
    }
    rules.setMultiballEnabled(true);
    CHECK(rules.graduationLit() && rules.rank() == pinball::kTopRank);
  }

  {  // 4.12 Not finished: the course lamps stay, the exam shots made stay
     // made, and three ramps light it again.
    RulesBench bench = single();
    Rules& rules = bench.rules;
    passAll(bench);
    rules.setMultiballEnabled(true);
    bench.wait(tuning::kComboSeconds);
    bench.scoop();
    shoot(bench, Shot::Runway);
    shoot(bench, Shot::Turbine);
    CHECK(rules.examShotsLeft() == 5);
    bench.wait(tuning::kGraduationGraceSeconds);
    bench.lose();
    bench.lose();
    bench.step();
    CHECK(!rules.graduationRunning() && bench.message() == std::string_view("MULTIBALL OVER"));
    CHECK(rules.coursesPassed() == 6 && rules.rank() == 5 && rules.graduations() == 0);
    CHECK(!rules.graduationLit() && rules.rampsForBriefing() == 3);
    bench.wait(Rules::kDoorRaiseSeconds);
    CHECK(bench.doorsUp);
    bench.wait(tuning::kComboSeconds);
    bench.runway();
    bench.runway();
    CHECK(!rules.graduationLit());
    bench.runway();
    CHECK(rules.graduationLit() && bench.message() == std::string_view("GRADUATION DAY IS LIT"));
    bench.scoop();
    CHECK(rules.graduationRunning() && rules.examShotsLeft() == 5);
    CHECK(rules.examShotMade(Shot::Runway) && rules.examShotMade(Shot::Turbine));
    CHECK(bench.total() == 3);
    // The last ball of the game drains during it: the game is over and
    // nothing is left running.
    bench.rules.onSlingshot(0);
    for (int i = 0; i < 3; ++i) {
      bench.rules.onNudge();
    }
    CHECK(rules.tilted());
    CHECK(!bench.lose() && !bench.lose());
    while (!rules.gameOver()) {
      bench.drain();
      bench.rules.onSlingshot(0);
      bench.wait(Rules::kBallSaveSeconds);
    }
    CHECK(!rules.graduationRunning() && rules.multiball() == Multiball::None);
    CHECK(rules.rank() == 5);
  }

  {  // 4.12 with balls locked: they are let go and count toward the three.
    for (int locked = 0; locked <= 2; ++locked) {
      RulesBench bench = single();
      Rules& rules = bench.rules;
      passAll(bench);
      rules.setMultiballEnabled(true);
      bench.doors();
      for (int i = 0; i < locked; ++i) {
        CHECK(bench.hangar() == Rules::HoleResult::Lock);
      }
      CHECK(rules.locksHeld() == locked && bench.balls.locked == locked);
      bench.wait(tuning::kComboSeconds);
      bench.rules.setTable(bench.balls);
      bench.scoop();
      CHECK(rules.graduationRunning());
      CHECK(bench.balls.locked == 0 && rules.locksHeld() == 0);
      CHECK(bench.launched == 2 - locked && bench.refused == 0);
      CHECK(bench.total() == 3 && bench.balls.live == 3 && rules.ballsLive() == 3);
      bench.wait(1.0);
      CHECK(rules.graduationRunning() && bench.launched == 2 - locked);
    }
  }

  {  // 4.12 Turbo multiball is suspended on Graduation Day: with room for
     // a ball, a full meter still pays its bonus and adds none.
    RulesBench bench = single();
    Rules& rules = bench.rules;
    passAll(bench);
    rules.setMultiballEnabled(true);
    bench.wait(tuning::kComboSeconds);
    bench.scoop();
    CHECK(rules.graduationRunning() && bench.total() == 3);
    bench.wait(tuning::kGraduationGraceSeconds);
    CHECK(!bench.lose());
    bench.step();
    CHECK(rules.graduationRunning() && bench.total() == 2);
    const int launched = bench.launched;
    const std::uint64_t bonus = rules.scoreFrom(Feature::TurboBonus);
    bench.bumper(rules.turboGoal() - rules.turboMeter());
    bench.step();
    CHECK(rules.scoreFrom(Feature::TurboBonus) - bonus == pinball::points::kTurboBonus);
    CHECK(bench.message() == std::string_view("TURBO BONUS"));
    CHECK(bench.launched == launched && bench.refused == 0 && bench.total() == 2);
    CHECK(rules.multiball() == Multiball::Graduation && rules.turboMultiballs() == 0);
    CHECK(rules.turboMeter() == 0);
  }
}
