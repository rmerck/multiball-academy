#include <cstddef>
#include <cstdint>
#include <string_view>

#include "Check.hpp"
#include "RulesBench.hpp"
#include "Suites.hpp"
#include "core/Courses.hpp"
#include "core/DisplayText.hpp"
#include "core/Progression.hpp"
#include "core/Rules.hpp"
#include "core/Snapshot.hpp"
#include "core/Sound.hpp"
#include "core/TableLayout.hpp"
#include "platform/Font5x7.hpp"

// Concept A's sorties (courses in the code), rank, Wings Ceremony and the Air
// Show (concepts/A/CONCEPT.md section 6), and the arithmetic of the display
// limits every name and message is held to.
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

// Qualifies this sortie alone (as its own shots would) and starts it at the
// Mission scoop.
void start(RulesBench& bench, CourseId id) {
  bench.wait(tuning::kComboSeconds);
  pinball::save::RulesRecord record = bench.rules.record();
  record.ready = {};
  record.ready[idx(id)] = true;
  record.courseBlink = static_cast<std::uint8_t>(idx(id));
  bench.rules.restore(record);
  CHECK(bench.rules.briefingLit());
  bench.scoop();
  CHECK(bench.rules.course() == idx(id));
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

// Does what the running sortie asks for.
void finish(RulesBench& bench) {
  Rules& rules = bench.rules;
  const std::size_t course = rules.course();
  for (int i = 0; i < 64 && rules.course() == course; ++i) {
    switch (static_cast<CourseId>(course)) {
      case CourseId::LoopDrill:
        bench.circuit(false);  // The same loop: no combo.
        break;
      case CourseId::RampRuns:
        shoot(bench, rules.rampRunsShot());
        break;
      case CourseId::Gunnery:
        rules.onDrone();
        bench.step();
        break;
      case CourseId::Formation:
        bench.wait(tuning::kComboSeconds);
        shoot(bench, rules.navigationShot());
        break;
      case CourseId::NightLanding:
        shoot(bench, rules.goalText() == pinball::kGoalLanding ? Shot::Skyway : Shot::Runway);
        break;
      case CourseId::Dogfight:
        if (rules.dogfightJackpotLit()) {
          bench.step();
          CHECK(!bench.doorsUp);
          bench.hangar();
        } else {
          rules.onDrone();
          bench.step();
        }
        break;
    }
  }
  CHECK(!rules.courseRunning());
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

// What the Air Show asks for beside the six sorties, given as done: a
// Squadron and a Scramble multiball played, A-C-E spelt.
void giveExtras(RulesBench& bench) {
  pinball::save::RulesRecord record = bench.rules.record();
  record.academyMultiballs = 1;
  record.turboMultiballs = 1;
  record.aceDone = true;
  bench.rules.restore(record);
}

// Every ball but one drains, past any grace.
void downToOne(RulesBench& bench) {
  bench.rules.onSlingshot(0);
  bench.wait(30.0);
  for (int i = 0; i < 3 && bench.balls.live > 1; ++i) {
    bench.lose();
  }
  bench.step();
}


// One switch toward this sortie's qualifier, as play reports it (no saved
// record): with BOGEY cut, NIGHT LANDING is lit by the nest pair.
void qualifyStep(RulesBench& bench, CourseId id, int k) {
  Rules& rules = bench.rules;
  switch (id) {
    case CourseId::LoopDrill:
      bench.circuit(k % 2 == 1);
      break;
    case CourseId::RampRuns:
      if (k % 2 == 0) {
        bench.runway();
      } else {
        bench.skyway();
      }
      break;
    case CourseId::Gunnery:
      rules.onDrone();
      break;
    case CourseId::Formation:  // RADAR, or T-A-X-I with RADAR cut.
      if constexpr (table::kRadarOnTable) {
        rules.onStandup(table::kRadarStandup + static_cast<std::size_t>(k % 3), 100.0F);
      } else {
        rules.onStandup(static_cast<std::size_t>(k % 4), 100.0F);
      }
      break;
    case CourseId::NightLanding:
      if constexpr (table::kBogeyOnTable) {
        rules.onBogey(static_cast<std::size_t>(k % 3));
      } else {
        rules.onTarget(static_cast<std::size_t>(k % 2));
      }
      break;
    case CourseId::Dogfight:
      rules.onCaptiveBall(tuning::kGForceHardSpeed);
      break;
  }
  bench.step();
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
    static_assert(display::fits("NIGHT LANDING COMPLETE", display::kMaxMessageLength));
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
    CHECK(pinball::kCourses[0].name == std::string_view("LOOP DRILL"));
    CHECK(pinball::kCourses[5].name == std::string_view("DOGFIGHT"));
  }

  {  // Choice: the Mission is lit at the start; the jets move the offer
     // through the sorties not yet passed; the scoop starts it.
    RulesBench bench = single();
    const Rules& rules = bench.rules;
    CHECK(rules.briefingLit() && rules.courseOffered() == 0);
    CHECK(rules.lampState(LampId::Course1) == LampState::Blink);
    CHECK(rules.lampState(LampId::ArrowBriefing) == LampState::Blink);
    CHECK(rules.goalText() == pinball::kGoalBriefingLit);
    bench.bumper();
    CHECK(rules.courseOffered() == 1 && rules.lampState(LampId::Course2) == LampState::Blink);
    CHECK(rules.lampState(LampId::Course1) == LampState::Off);
    bench.bumper(5);
    CHECK(rules.courseOffered() == 0);
    bench.bumper(2);
    bench.scoop();
    CHECK(rules.course() == idx(CourseId::Gunnery) && rules.coursesStarted() == 1);
    CHECK(bench.message() == std::string_view("GUNNERY  HIT THE BANDIT"));
    CHECK(rules.courseName() == std::string_view("GUNNERY"));
    CHECK(rules.goalText() == std::string_view("HIT THE BANDIT") && rules.goalRemaining() == 4);
    CHECK(rules.courseSecondsLeft() > 29.9 && rules.courseSecondsLeft() <= 30.0);
    CHECK(rules.lampState(LampId::Course3) == LampState::FastBlink);
    CHECK(rules.lampState(LampId::ArrowTurbine) == LampState::Blink);
    CHECK(!rules.briefingLit() && rules.pylonWanted());
    // One at a time: the scoop and the jets change nothing now.
    bench.bumper(3);
    bench.scoop();
    CHECK(rules.course() == idx(CourseId::Gunnery) && rules.coursesStarted() == 1);
  }

  {  // Qualifiers (RADAR for FORMATION, BOGEY for NIGHT LANDING): each sortie is lit by its own shots, which hold the
     // Mission's offer, and the scoop starts the qualified sortie.
    RulesBench bench = single();
    Rules& rules = bench.rules;
    CHECK(rules.qualifyRemaining(idx(CourseId::RampRuns)) == 4);
    for (int i = 0; i < 3; ++i) {
      bench.runway();
    }
    CHECK(!rules.courseQualified(idx(CourseId::RampRuns)) &&
          rules.qualifyRemaining(idx(CourseId::RampRuns)) == 1);
    bench.skyway();
    CHECK(rules.courseQualified(idx(CourseId::RampRuns)) && rules.courseOffered() == 1);
    CHECK(rules.lampState(LampId::Course2) == LampState::Blink);
    bench.bumper(2);  // A qualified sortie holds the offer.
    CHECK(rules.courseOffered() == 1);
    bench.scoop();
    CHECK(rules.course() == idx(CourseId::RampRuns) && !rules.courseQualified(1));
    CHECK(rules.goalText() == std::string_view("SHOOT THE LIT RAMP"));
    bench.wait(40.0);
    CHECK(!rules.courseRunning() && bench.message() == std::string_view("SORTIE OVER"));
    bench.wait(20.0);
    // Loops, the Bandit, T-A-X-I (for RADAR), the nest pair (for BOGEY) and
    // the G-meter's 3G each light their own.
    bench.circuit(false);
    bench.circuit(true);
    bench.circuit(false);
    CHECK(rules.courseQualified(idx(CourseId::LoopDrill)));
    for (int i = 0; i < 3; ++i) {
      rules.onDrone();
    }
    CHECK(rules.courseQualified(idx(CourseId::Gunnery)));
    if constexpr (table::kRadarOnTable) {
      for (std::size_t i = 0; i < 4; ++i) {  // T-A-X-I scores and lights nothing.
        rules.onStandup(i, 100.0F);
      }
      CHECK(!rules.courseQualified(idx(CourseId::Formation)));
      rules.onStandup(table::kRadarStandup, 100.0F);
      rules.onStandup(table::kRadarStandup, 100.0F);  // A letter lit already counts once.
      rules.onStandup(table::kRadarStandup + 1, 100.0F);
      CHECK(rules.radarLit(0) && !rules.courseQualified(idx(CourseId::Formation)));
      rules.onStandup(table::kRadarStandup + 2, 100.0F);
      CHECK(rules.courseQualified(idx(CourseId::Formation)) && !rules.radarLit(0));
    } else {
      // RADAR cut: a RADAR switch does nothing; T-A-X-I lights FORMATION.
      for (std::size_t i = 0; i < 3; ++i) {
        rules.onStandup(table::kRadarStandup + i, 100.0F);
      }
      CHECK(!rules.radarLit(0) && !rules.courseQualified(idx(CourseId::Formation)));
      for (std::size_t i = 0; i < 3; ++i) {
        rules.onStandup(i, 100.0F);
      }
      CHECK(!rules.courseQualified(idx(CourseId::Formation)));
      rules.onStandup(3, 100.0F);
      CHECK(rules.courseQualified(idx(CourseId::Formation)));
    }
    if constexpr (table::kBogeyOnTable) {
      rules.onBogey(0);
      rules.onBogey(1);
      CHECK(rules.bogeyMarked(1) && !rules.courseQualified(idx(CourseId::NightLanding)));
      rules.onBogey(2);
      CHECK(rules.courseQualified(idx(CourseId::NightLanding)) && rules.bogeyCleared());
    } else {
      // BOGEY cut (G2): the nest pair lights NIGHT LANDING, and a BOGEY
      // switch, were one ever reported, does nothing.
      rules.onBogey(0);
      rules.onBogey(1);
      rules.onBogey(2);
      CHECK(!rules.bogeyMarked(0) && !rules.bogeyCleared() &&
            !rules.courseQualified(idx(CourseId::NightLanding)));
      rules.onTarget(0);
      CHECK(!rules.courseQualified(idx(CourseId::NightLanding)));
      rules.onTarget(1);
      CHECK(rules.courseQualified(idx(CourseId::NightLanding)));
    }
    rules.onCaptiveBall(100.0F);
    CHECK(rules.gForce() == 1 && rules.lampState(LampId::GForce1) == LampState::On &&
          rules.lampState(LampId::GForce2) == LampState::Blink);
    rules.onCaptiveBall(tuning::kGForceHardSpeed);  // A hard hit is 2G.
    CHECK(rules.gForce() == 3 && rules.courseQualified(idx(CourseId::Dogfight)));
    rules.onCaptiveBall(tuning::kGForceHardSpeed);  // 5G pays and starts again.
    CHECK(rules.gForce() == 0 && bench.message() == std::string_view("5G  PULL UP"));
    // The scoop takes the qualified sorties in turn from the one offered.
    const std::size_t offered = rules.courseOffered();
    bench.wait(tuning::kComboSeconds);
    bench.scoop();
    CHECK(rules.course() == offered && rules.coursesStarted() == 2);
    // From the third sortie started, each qualifier asks for twice as many.
    bench.wait(60.0);
    bench.wait(tuning::kComboSeconds);
    bench.scoop();
    CHECK(rules.coursesStarted() == 3);
    bench.wait(60.0);
    CHECK(rules.qualifyRemaining(idx(CourseId::RampRuns)) == 8);
  }

  {  // Each sortie: its task, its award per step, its pass award, a rank for
     // each and a climb of the Altitude.
    RulesBench bench = single();
    const Rules& rules = bench.rules;
    // LOOP DRILL: loops, the other side from the last paying double.
    start(bench, CourseId::LoopDrill);
    CHECK(rules.lampState(LampId::ArrowCircuitLeft) == LampState::Blink);
    bench.circuit(false);
    CHECK(rules.scoreFrom(Feature::Course1) == 25'000);
    CHECK(rules.lampState(LampId::ArrowCircuitRight) == LampState::FastBlink);
    bench.circuit(false);
    CHECK(rules.scoreFrom(Feature::Course1) == 50'000);
    bench.circuit(true);
    CHECK(!rules.courseRunning() && rules.coursePassed(0));
    CHECK(rules.scoreFrom(Feature::Course1) == 50'000 + 50'000 + 125'000);
    CHECK(bench.message() == std::string_view("LOOP DRILL COMPLETE"));
    CHECK(rules.rank() == 1 && rules.altitude() == 1);
    // RAMP RUNS: the lit ramp, then the other.
    start(bench, CourseId::RampRuns);
    CHECK(rules.rampRunsShot() == Shot::Runway);
    CHECK(rules.lampState(LampId::ArrowRunway) == LampState::Blink &&
          rules.lampState(LampId::ArrowSkyway) != LampState::Blink);
    bench.skyway();
    CHECK(rules.goalRemaining() == 4);
    bench.runway();
    CHECK(rules.goalRemaining() == 3 && rules.rampRunsShot() == Shot::Skyway);
    bench.runway();
    CHECK(rules.goalRemaining() == 3);
    finish(bench);
    CHECK(rules.scoreFrom(Feature::Course2) == 4 * 20'000 + 125'000 && rules.rank() == 2);
    // GUNNERY: Bandit hits.
    pass(bench, CourseId::Gunnery);
    CHECK(rules.scoreFrom(Feature::Course3) == 4 * 15'000 + 100'000 && rules.rank() == 3);
    {  // At FL100 (armour 3): each hit adds time; BANDIT DOWN during it
       // doubles the pass.
      RulesBench gun = single();
      start(gun, CourseId::Gunnery);
      gun.wait(10.0);
      const double before = gun.rules.courseSecondsLeft();
      CHECK(!gun.rules.onDrone());
      CHECK(gun.rules.courseSecondsLeft() > before + pinball::kGunneryHitSeconds - 0.1);
      CHECK(!gun.rules.onDrone());
      CHECK(gun.rules.onDrone());  // The third hit knocks it down.
      CHECK(gun.rules.droneHits() == 0 && gun.rules.goalRemaining() == 1);
      gun.rules.onDrone();
      CHECK(gun.rules.coursePassed(2));
      CHECK(gun.rules.scoreFrom(Feature::Course3) == 4 * 15'000 + 2 * 100'000);
    }
    // FORMATION: the lit arrow, which moves on every few seconds.
    start(bench, CourseId::Formation);
    CHECK(rules.navigationShot() == Shot::CircuitLeft);
    bench.wait(pinball::kNavigationHopSeconds + 0.1);
    CHECK(rules.navigationShot() == Shot::Runway);
    finish(bench);
    CHECK(rules.scoreFrom(Feature::Course4) == 3 * 30'000 + 150'000 && rules.rank() == 4);
    // NIGHT LANDING: the Runway (the approach), then the Skyway for what the
    // hurry-up has left.
    start(bench, CourseId::NightLanding);
    CHECK(rules.hurryUpValue() == pinball::kHurryUpStart);
    CHECK(rules.lampState(LampId::ArrowRunway) == LampState::FastBlink);
    bench.skyway();
    CHECK(rules.goalRemaining() == 2);
    bench.wait(4.0);
    CHECK(rules.hurryUpValue() == pinball::kHurryUpStart - 4 * pinball::kHurryUpPerSecond);
    bench.runway();
    CHECK(rules.goalText() == pinball::kGoalLanding && rules.goalRemaining() == 1);
    CHECK(rules.lampState(LampId::ArrowSkyway) == LampState::FastBlink);
    bench.skyway();
    CHECK(rules.coursePassed(4));
    CHECK(rules.scoreFrom(Feature::Course5) ==
          10'000 + pinball::kHurryUpStart - 4 * pinball::kHurryUpPerSecond);
    CHECK(rules.rank() == 5);
    // DOGFIGHT (one ball here): a Bandit hit lights the Hangar's jackpot.
    start(bench, CourseId::Dogfight);
    CHECK(bench.balls.live == 1 && rules.multiball() == Multiball::None);
    bench.rules.onDrone();
    CHECK(rules.dogfightJackpotLit() && rules.goalText() == pinball::kGoalDogfightJackpot);
    CHECK(rules.lampState(LampId::ArrowHangar) == LampState::FastBlink);
    bench.step();
    CHECK(!bench.doorsUp && !rules.lockLit());
    CHECK(bench.hangar() == Rules::HoleResult::Eject);
    CHECK(rules.scoreFrom(Feature::Jackpots) == pinball::points::kDogfightJackpot);
    CHECK(rules.jackpotsCollected() == 1 && !rules.dogfightJackpotLit());
    finish(bench);
    CHECK(rules.coursePassed(5) && rules.coursesPassed() == 6 && rules.rank() == 5);
    CHECK(rules.altitude() == pinball::progression::kTopLevel);
    bench.wait(Rules::kDoorRaiseSeconds + 0.1);
    CHECK(bench.doorsUp);
  }

  {  // The clock: it stands still through a multiball (not DOGFIGHT's own),
     // while no ball is free, and runs out to SORTIE OVER; the Altitude
     // shortens it; a drain carries it over.
    RulesBench bench;
    Rules& rules = bench.rules;
    rules.onSlingshot(0);
    bench.wait(Rules::kBallSaveSeconds);
    start(bench, CourseId::LoopDrill);
    bench.wait(10.0);
    bench.bumper(rules.turboGoal());
    bench.step(2);
    CHECK(rules.multiball() == Multiball::Turbo && bench.balls.live == 2);
    CHECK(bench.message() == std::string_view("SCRAMBLE MULTIBALL"));
    const double left = rules.courseSecondsLeft();
    bench.wait(20.0);
    CHECK(rules.courseSecondsLeft() == left);
    downToOne(bench);
    CHECK(rules.multiball() == Multiball::None && rules.courseRunning());
    bench.wait(left + 0.1);
    CHECK(!rules.courseRunning() && !rules.coursePassed(0) && rules.rank() == 0);
    CHECK(rules.rampsForBriefing() == 2);
    // At FL500 the clock is the timerScale's share of its own.
    RulesBench high = single();
    high.rules.setAltitudeFloor(pinball::progression::kTopLevel);
    start(high, CourseId::RampRuns);
    const double scaled = 40.0 * pinball::progression::knobs(4).timerScale;
    CHECK(high.rules.courseSecondsLeft() > scaled - 0.1 &&
          high.rules.courseSecondsLeft() <= scaled);
    // Drain: carried over with at least 15 s; a tilt ends it.
    RulesBench drain = single();
    start(drain, CourseId::RampRuns);
    drain.runway();
    drain.wait(30.0);
    CHECK(drain.drain() == Rules::DrainResult::NextBall);
    CHECK(drain.rules.courseRunning() && drain.rules.goalRemaining() == 3);
    CHECK(drain.rules.courseSecondsLeft() > 14.9 && drain.rules.courseSecondsLeft() <= 15.0);
    for (int i = 0; i < 3; ++i) {
      drain.rules.onNudge();
    }
    CHECK(drain.rules.tilted());
    CHECK(drain.drain() == Rules::DrainResult::NextBall);
    CHECK(!drain.rules.courseRunning() && !drain.rules.coursePassed(1));
  }

  {  // DOGFIGHT with multiball: a second ball, its jackpot counted as a
     // multiball's; the sortie runs on when it is one ball again.
    RulesBench bench;
    Rules& rules = bench.rules;
    rules.onSlingshot(0);
    bench.wait(Rules::kBallSaveSeconds);
    start(bench, CourseId::Dogfight);
    CHECK(bench.launched == 1 && bench.total() == 2);
    CHECK(rules.multiball() == Multiball::Dogfight && rules.ballsLive() == 2);
    CHECK(rules.ballSaveActive());
    rules.onDrone();
    bench.step();
    CHECK(bench.hangar() == Rules::HoleResult::Eject && rules.jackpotsCollected() == 1);
    downToOne(bench);
    CHECK(rules.multiball() == Multiball::None && rules.courseRunning());
    finish(bench);
    CHECK(rules.coursePassed(idx(CourseId::Dogfight)) &&
          bench.message() == std::string_view("DOGFIGHT WON"));
  }

  {  // WINGS CEREMONY: every sortie started lights it at the Mission; 60 s
     // of every shot lit at 2X, the Bandit's armour standing again at once.
    RulesBench bench = single();
    Rules& rules = bench.rules;
    for (std::size_t course = 0; course < pinball::kCourseCount; ++course) {
      CHECK(!rules.wingsLit());
      start(bench, static_cast<CourseId>(course));
      bench.wait(70.0);
      CHECK(!rules.courseRunning());
    }
    CHECK(rules.wingsLit() && !rules.briefingLit());
    CHECK(rules.lampState(LampId::ArrowBriefing) == LampState::FastBlink);
    CHECK(rules.lampState(LampId::Graduation) == LampState::Blink);
    CHECK(rules.goalText() == pinball::kGoalBriefingLit);
    bench.wait(tuning::kComboSeconds);
    bench.scoop();
    CHECK(rules.wingsRunning() && rules.wingsCeremonies() == 1 && !rules.courseRunning());
    CHECK(rules.courseName() == pinball::kWingsName && rules.goalText() == pinball::kGoalWings);
    CHECK(rules.wingsSecondsLeft() > 59.9 && rules.pylonWanted());
    CHECK(rules.lampState(LampId::ArrowSkyway) == LampState::FastBlink);
    const std::uint64_t before = rules.scoreFrom(Feature::Graduation);
    bench.runway();
    CHECK(rules.scoreFrom(Feature::Graduation) - before == pinball::points::kWingsShot);
    for (int i = 0; i < 8; ++i) {
      CHECK(!rules.onDrone());
    }
    CHECK(rules.scoreFrom(Feature::Graduation) - before == 9 * pinball::points::kWingsShot);
    bench.wait(61.0);
    CHECK(!rules.wingsRunning() && !rules.wingsLit());
    CHECK(rules.scoreFrom(Feature::Graduation) - before == 9 * pinball::points::kWingsShot);
  }

  {  // The Air Show asks for six sorties passed, both multiballs played and
     // A-C-E spelt; the sortie window says what is still to do.
    RulesBench bench = single();
    Rules& rules = bench.rules;
    passAll(bench);
    // Every sortie started has lit WINGS CEREMONY too: say it was flown.
    CHECK(rules.wingsLit() && rules.goalText() == pinball::kGoalBriefingLit);
    pinball::save::RulesRecord record = rules.record();
    record.wingsDone = true;
    rules.restore(record);
    rules.setMultiballEnabled(true);
    CHECK(!rules.graduationLit() && rules.goalText() == pinball::kGoalNeedSquadron);
    record.academyMultiballs = 1;
    record.multiballEnabled = true;
    rules.restore(record);
    CHECK(rules.goalText() == pinball::kGoalNeedScramble);
    record.turboMultiballs = 1;
    rules.restore(record);
    CHECK(rules.goalText() == pinball::kGoalNeedAce && !rules.graduationLit());
    rules.onStandup(table::kDeckStandupA, 100.0F);
    CHECK(bench.message() == std::string_view("A-C-E  A") && rules.aceLit(0));
    rules.onStandup(table::kDeckStandupA + 1, 100.0F);
    rules.onStandup(table::kDeckStandupA + 2, 100.0F);
    CHECK(rules.aceDone() && !rules.aceLit(0) && rules.graduationLit());
    CHECK(rules.scoreFrom(Feature::TurboTargets) == 3 * 5'000 + pinball::points::kAceComplete);
  }

  {  // The AIR SHOW: FORMATION PASS, then the FINALE (the Bandit at nine
     // armour, then the Hangar), victory laps, and every sortie again.
    RulesBench bench = single();
    Rules& rules = bench.rules;
    giveExtras(bench);
    passAll(bench);
    CHECK(rules.rank() == 5 && rules.coursesPassed() == 6);
    CHECK(!rules.graduationLit());
    rules.setMultiballEnabled(true);
    CHECK(rules.graduationLit());
    CHECK(rules.lampState(LampId::Graduation) == LampState::Blink);
    CHECK(rules.lampState(LampId::ArrowBriefing) == LampState::FastBlink);
    bench.wait(tuning::kComboSeconds);
    bench.scoop();
    CHECK(rules.graduationRunning() && rules.multiball() == Multiball::Graduation);
    CHECK(bench.message() == std::string_view("AIR SHOW"));
    CHECK(rules.courseName() == std::string_view("AIR SHOW"));
    CHECK(bench.launched == 2 && bench.total() == 3 && bench.refused == 0);
    CHECK(rules.ballsLive() == 3 && rules.graceSecondsLeft() > 29.0);
    CHECK(!bench.doorsUp && rules.examShotsLeft() == 7 && rules.goalRemaining() == 7);
    for (std::size_t lamp = 0; lamp < 7; ++lamp) {
      CHECK(rules.lampState(static_cast<LampId>(lamp)) == LampState::Blink);
    }
    CHECK(!rules.lockLit() && !rules.briefingLit() && !rules.jackpotLit(Shot::Runway));
    CHECK(bench.hangar() == Rules::HoleResult::Eject);
    CHECK(rules.scoreFrom(Feature::Graduation) == 100'000 && rules.examShotMade(Shot::Hangar));
    CHECK(bench.message() == std::string_view("FORMATION PASS  6 TO GO"));
    for (const Shot shot : {Shot::CircuitLeft, Shot::Runway, Shot::Briefing, Shot::Turbine,
                            Shot::Skyway}) {
      shoot(bench, shot);
      shoot(bench, shot);
    }
    CHECK(rules.examShotsLeft() == 1 && rules.scoreFrom(Feature::Graduation) == 600'000);
    shoot(bench, Shot::CircuitRight);
    // FINALE.
    CHECK(rules.examShotsLeft() == 0 && rules.courseName() == pinball::kFinaleName);
    CHECK(bench.message() == std::string_view("FINALE  KNOCK DOWN BANDIT"));
    CHECK(rules.goalText() == pinball::kGoalFinaleBandit);
    CHECK(rules.droneArmour() == tuning::kFinaleArmour);
    CHECK(rules.lampState(LampId::ArrowTurbine) == LampState::FastBlink &&
          rules.lampState(LampId::ArrowHangar) == LampState::Off);
    bench.step();
    CHECK(!bench.doorsUp);
    const std::uint64_t early = bench.score();
    bench.hangar();  // Not yet: the Hangar pays nothing more than its switch.
    CHECK(bench.score() - early < 100'000 && rules.rank() == 5);
    for (int i = 0; i < tuning::kFinaleArmour - 1; ++i) {
      CHECK(!rules.onDrone());
    }
    CHECK(!rules.finaleHangarLit());
    CHECK(rules.onDrone());
    CHECK(rules.finaleHangarLit() && rules.goalText() == pinball::kGoalCommencement);
    CHECK(rules.lampState(LampId::ArrowHangar) == LampState::FastBlink);
    const std::uint64_t before = bench.score();
    bench.step();
    bench.hangar();
    CHECK(bench.score() - before >= 1'000'000);
    CHECK(rules.rank() == pinball::kTopRank &&
          bench.message() == std::string_view("AIR SHOW SUPER JACKPOT"));
    const std::uint64_t laps = rules.scoreFrom(Feature::Graduation);
    shoot(bench, Shot::Turbine);
    shoot(bench, Shot::Briefing);
    CHECK(rules.scoreFrom(Feature::Graduation) - laps == 2 * pinball::points::kVictoryLap);
    downToOne(bench);
    CHECK(!rules.graduationRunning() && rules.multiball() == Multiball::None);
    CHECK(rules.graduations() == 1 && rules.coursesPassed() == 0 && rules.briefingLit());
    CHECK(rules.rank() == pinball::kTopRank && !rules.graduationLit());
    CHECK(rules.lampState(LampId::Graduation) == LampState::On);
    // Second time round: twice the pass award.
    rules.setMultiballEnabled(false);
    const std::uint64_t first = rules.scoreFrom(Feature::Course2);
    pass(bench, CourseId::RampRuns);
    CHECK(rules.scoreFrom(Feature::Course2) - first == 4 * 20'000 + 2 * 125'000);
    CHECK(rules.rank() == pinball::kTopRank);
  }

  {  // Not finished: the shots made stay made, and three ramps light it again.
    RulesBench bench = single();
    Rules& rules = bench.rules;
    giveExtras(bench);
    passAll(bench);
    rules.setMultiballEnabled(true);
    bench.wait(tuning::kComboSeconds);
    bench.scoop();
    shoot(bench, Shot::Runway);
    shoot(bench, Shot::Turbine);
    CHECK(rules.examShotsLeft() == 5);
    downToOne(bench);
    CHECK(!rules.graduationRunning() && bench.message() == std::string_view("MULTIBALL OVER"));
    CHECK(!rules.graduationLit() && rules.rampsForBriefing() == 3);
    bench.wait(tuning::kComboSeconds);
    bench.runway();
    bench.runway();
    CHECK(!rules.graduationLit());
    bench.runway();
    CHECK(rules.graduationLit());
    bench.scoop();
    CHECK(rules.graduationRunning() && rules.examShotsLeft() == 5 && bench.total() == 3);
  }

  {  // With balls locked: they are let go and count toward the three.
    for (int locked = 0; locked <= 2; ++locked) {
      RulesBench bench = single();
      Rules& rules = bench.rules;
      giveExtras(bench);
      passAll(bench);
      rules.setMultiballEnabled(true);
      bench.wait(1.0);  // DOGFIGHT's doors back up.
      bench.doors();
      for (int i = 0; i < locked; ++i) {
        CHECK(bench.hangar() == Rules::HoleResult::Lock);
        bench.step();
        if (bench.doorsUp && i + 1 < locked) {
          bench.doors();
        }
      }
      CHECK(rules.locksHeld() == locked && bench.balls.locked == locked);
      bench.wait(tuning::kComboSeconds);
      bench.scoop();
      CHECK(rules.graduationRunning());
      CHECK(bench.balls.locked == 0 && rules.locksHeld() == 0);
      CHECK(bench.launched == 2 - locked && bench.refused == 0 && bench.total() == 3);
    }
  }

  {  // Scramble multiball is suspended through the Air Show: a full meter
     // pays its bonus and adds no ball.
    RulesBench bench = single();
    Rules& rules = bench.rules;
    giveExtras(bench);
    passAll(bench);
    rules.setMultiballEnabled(true);
    bench.wait(tuning::kComboSeconds);
    bench.scoop();
    bench.wait(tuning::kGraduationGraceSeconds);
    CHECK(!bench.lose());
    bench.step();
    const int launched = bench.launched;
    const std::uint64_t bonus = rules.scoreFrom(Feature::TurboBonus);
    bench.bumper(rules.turboGoal() - rules.turboMeter());
    bench.step();
    CHECK(rules.scoreFrom(Feature::TurboBonus) - bonus == pinball::points::kTurboBonus);
    CHECK(bench.message() == std::string_view("SCRAMBLE BONUS"));
    CHECK(bench.launched == launched && rules.multiball() == Multiball::Graduation);
  }

  {  // The CAT SHOT spots a lock: its ball is locked with the doors up, and
     // the third starts SQUADRON MULTIBALL; the meter gets a boost.
    RulesBench bench;
    Rules& rules = bench.rules;
    rules.onSlingshot(0);
    bench.wait(Rules::kBallSaveSeconds);
    CHECK(bench.doorsUp && !rules.lockLit());
    const int meter = rules.turboMeter();
    rules.onHole(table::kCatHole);
    CHECK(rules.turboMeter() == meter + tuning::kTurboPerCatShot);
    // (A meter that cannot fill here, so that no Scramble joins in.)
    pinball::save::RulesRecord record = rules.record();
    record.turboGoal = 5'000;
    rules.restore(record);
    bench.step();
    CHECK(bench.hangar() == Rules::HoleResult::Lock && rules.locksHeld() == 1);
    rules.onHole(table::kCatHole);
    bench.step();
    CHECK(bench.hangar() == Rules::HoleResult::Lock && rules.locksHeld() == 2);
    // A Hangar shot of its own (not by the ride) is not a lock.
    CHECK(bench.hangar() == Rules::HoleResult::Eject);
    rules.onHole(table::kCatHole);
    bench.step();
    CHECK(bench.hangar() == Rules::HoleResult::StartMultiball);
    CHECK(rules.multiball() == Multiball::Academy && bench.total() == 3);
    CHECK(bench.message() == std::string_view("SQUADRON MULTIBALL"));
    // BANDIT DOWN lights the super jackpot.
    for (int i = 0; i < rules.droneArmour(); ++i) {
      rules.onDrone();
    }
    CHECK(rules.superJackpotLit());
    bench.step();
    CHECK(!bench.doorsUp);
    const std::uint64_t super = rules.scoreFrom(Feature::SuperJackpot);
    bench.hangar();
    CHECK(rules.scoreFrom(Feature::SuperJackpot) - super == pinball::points::kSuperJackpot);
  }

  {  // The Flight Deck's diverter: lit by two ramps, by the super skill
     // shot's window and all through Scramble; a Skyway it takes puts it out.
    RulesBench bench;
    Rules& rules = bench.rules;
    rules.onSlingshot(0);
    CHECK(!rules.deckLit());
    bench.wait(tuning::kComboSeconds);
    bench.runway();
    CHECK(!rules.deckLit());
    bench.runway();
    CHECK(rules.deckLit());
    bench.skyway();  // Diverted to the deck.
    CHECK(!rules.deckLit());
    bench.runway();
    CHECK(!rules.deckLit());
    bench.skyway();
    CHECK(rules.deckLit());
    rules.setDeckPolicy(2);
    CHECK(!rules.deckLit());
    rules.setDeckPolicy(0);
    // The super skill shot: a full plunge round to the left inlane.
    RulesBench skill;
    CHECK(skill.rules.skillShotOffered() && !skill.rules.deckLit());
    skill.wing(1);
    CHECK(skill.rules.superSkillShotLit() && skill.rules.deckLit());
    skill.skyway();
    CHECK(skill.rules.scoreFrom(Feature::SkillShot) > 0 && !skill.rules.deckLit());
    // Scramble keeps it open.
    RulesBench scramble;
    scramble.rules.onSlingshot(0);
    scramble.bumper(scramble.rules.turboGoal());
    scramble.step(2);
    CHECK(scramble.rules.multiball() == Multiball::Turbo && scramble.rules.deckLit());
  }

  {  // F-L-A-P-S: the checklist relights the Mission (and adds a bonus X);
     // at FL300 and above its letters time out.
    RulesBench bench = single();
    Rules& rules = bench.rules;
    start(bench, CourseId::LoopDrill);
    bench.wait(60.0);
    CHECK(!rules.briefingLit() && rules.rampsForBriefing() == 2);
    for (std::size_t letter = 0; letter < 5; ++letter) {
      rules.onRollover(7 + letter);
    }
    CHECK(rules.briefingLit() && bench.message() == std::string_view("FLAPS SET  MISSION LIT"));
    CHECK(rules.bonusMultiplier() == 2);
    rules.onRollover(7);
    bench.wait(30.0);
    CHECK(rules.flapLit(0));  // No time out at FL100.
    rules.setAltitudeFloor(2);
    rules.onRollover(8);
    bench.wait(tuning::kFlapsLetterSeconds * pinball::progression::knobs(2).timerScale - 0.5);
    CHECK(rules.flapLit(1));
    bench.wait(1.0);
    CHECK(!rules.flapLit(0) && !rules.flapLit(1));
  }

  {  // The Altitude's knobs in the rules: the Bandit's armour, the ball
     // saver's time, and the Mission's repeat paying a quarter.
    for (int level = 0; level <= pinball::progression::kTopLevel; ++level) {
      RulesBench bench;
      bench.rules.setAltitudeFloor(level);
      bench.rules.startGame();
      bench.apply();
      bench.rules.onBallInPlay();
      const pinball::progression::Knobs& knobs = pinball::progression::knobs(level);
      CHECK(bench.rules.droneArmour() == tuning::kDroneArmourBase + knobs.extraHits);
      const double saver = Rules::kBallSaveSeconds * knobs.ballSaveScale;
      CHECK(bench.rules.ballSaveSecondsLeft() > saver - 0.01 &&
            bench.rules.ballSaveSecondsLeft() < saver + 0.01);
    }
    RulesBench bench = single();
    Rules& rules = bench.rules;
    start(bench, CourseId::LoopDrill);
    const std::uint64_t before = rules.scoreFrom(Feature::Briefing);
    bench.scoop();
    CHECK(rules.scoreFrom(Feature::Briefing) - before == pinball::points::kBriefingRepeat / 4);
    bench.wait(tuning::kMissionRepeatSeconds + 0.1);
    bench.scoop();
    CHECK(rules.scoreFrom(Feature::Briefing) - before ==
          pinball::points::kBriefingRepeat / 4 + pinball::points::kBriefingRepeat);
  }

  {  // Each kind of event has its own sound: the rules' cues, one table row
     // each, from the events that make them.
    using pinball::Sound;
    using pinball::SoundSet;
    using pinball::soundBit;
    const auto has = [](SoundSet sounds, Sound sound) { return (sounds & soundBit(sound)) != 0U; };

    RulesBench fresh;  // A ball on the plunger: the skill shot is offered.
    fresh.rules.takeSounds();
    fresh.rollCall(fresh.rules.skillShotLane());
    CHECK(has(fresh.rules.takeSounds(), Sound::SkillShot));

    RulesBench bench = single();
    bench.rules.setMultiballEnabled(true);
    Rules& rules = bench.rules;
    rules.takeSounds();
    // R-A-D complete: a set, and overdrive lit.
    for (std::size_t lane = 0; lane < 3; ++lane) {
      bench.rollCall(lane);
    }
    SoundSet sounds = rules.takeSounds();
    CHECK(has(sounds, Sound::SetComplete) && has(sounds, Sound::Lit));
    // The Afterburner lane starts overdrive.
    bench.wait(tuning::kComboSeconds);
    rules.takeSounds();
    bench.turbine();
    CHECK(has(rules.takeSounds(), Sound::Overdrive));
    // Two different flow shots in a row: a combo.
    bench.wait(tuning::kComboSeconds);
    bench.runway();
    rules.takeSounds();
    bench.skyway();
    CHECK(has(rules.takeSounds(), Sound::Combo));
    // A sortie: started, passed with a rank, and another failed.
    bench.wait(tuning::kOverdriveSeconds);
    rules.takeSounds();
    start(bench, CourseId::LoopDrill);
    CHECK(has(rules.takeSounds(), Sound::CourseStart));
    finish(bench);
    sounds = rules.takeSounds();
    CHECK(has(sounds, Sound::CoursePassed) && has(sounds, Sound::RankUp));
    start(bench, CourseId::RampRuns);
    rules.takeSounds();
    bench.wait(pinball::kCourses[idx(CourseId::RampRuns)].seconds + 1.0);
    CHECK(!rules.courseRunning() && has(rules.takeSounds(), Sound::CourseFailed));
    // Both nest targets light extra credit; the Mission collects it (a
    // sortie on offer is started first).
    rules.onTarget(0);
    rules.onTarget(1);
    CHECK(rules.extraCreditLit());
    rules.takeSounds();
    for (int i = 0; i < 4 && rules.extraCreditLit(); ++i) {
      bench.wait(tuning::kComboSeconds);
      bench.scoop();
      if (rules.courseRunning()) {
        finish(bench);
      }
    }
    CHECK(!rules.extraCreditLit() && has(rules.takeSounds(), Sound::ExtraCredit));
    // The lock, then Squadron multiball, its jackpots and the super jackpot,
    // on a table of their own.
    {
      RulesBench mb;
      mb.rules.onSlingshot(0);
      mb.wait(Rules::kBallSaveSeconds);
      mb.rules.takeSounds();
      SoundSet heard = 0;
      for (int i = 0; i < 8 && mb.rules.multiball() != Multiball::Academy; ++i) {
        if (mb.doorsUp) {
          mb.doors();
        }
        mb.hangar();
        mb.step();
        heard |= mb.rules.takeSounds();
      }
      CHECK(mb.rules.multiball() == Multiball::Academy);
      CHECK(has(heard, Sound::BallLocked) && has(heard, Sound::MultiballStart));
      heard = 0;
      for (int i = 0; i < tuning::kJackpotsForSuper; ++i) {
        mb.wait(tuning::kComboSeconds);
        mb.runway();
        heard |= mb.rules.takeSounds();
      }
      CHECK(has(heard, Sound::Jackpot) && mb.rules.superJackpotLit() && has(heard, Sound::Lit));
      mb.step(2);
      mb.hangar();
      CHECK(has(mb.rules.takeSounds(), Sound::SuperJackpot));
    }
    // An extra ball: lit by the third sortie passed, collected at the
    // Mission.
    sounds = 0;
    for (int i = 0; i < 12 && rules.coursesPassed() < 3; ++i) {
      bench.wait(tuning::kComboSeconds);
      for (int ramp = 0; ramp < 4 && !rules.briefingLit(); ++ramp) {
        bench.runway();
      }
      bench.scoop();
      if (rules.courseRunning()) {
        finish(bench);
      }
    }
    for (int i = 0; i < 4 && rules.extraBalls() == 0; ++i) {
      bench.wait(tuning::kComboSeconds);
      bench.scoop();
      sounds |= rules.takeSounds();
      if (rules.courseRunning()) {
        finish(bench);
      }
    }
    CHECK(rules.extraBalls() == 1 && has(sounds, Sound::ExtraBall));
  }
  {  // Progression: the Altitude, FL100 to FL500 (core/Progression.hpp).
    namespace progression = pinball::progression;
    // The knobs: the plain tuning at level 0, then only tighter.
    static_assert(progression::knobsClimb());
    static_assert(progression::flightLevel(0) == 100 && progression::flightLevel(4) == 500);
    static_assert(progression::flightLevel(-3) == 100 && progression::flightLevel(9) == 500);
    CHECK(progression::knobs(-1).ballSaveScale == 1.0 && progression::knobs(7).lightEnergy == 1.0F);
    CHECK(!progression::knobs(1).guardsUp && progression::knobs(2).guardsUp);

    progression::Altitude altitude;
    CHECK(altitude.level() == 0 && !altitude.atTop());
    CHECK(altitude.climb() == 1 && altitude.level() == 1);
    CHECK(altitude.climb(-2) == 0 && altitude.level() == 1);  // Never down.
    CHECK(altitude.climb(10) == 3 && altitude.atTop() && altitude.climb() == 0);
    altitude.reset();
    CHECK(altitude.level() == 0);
    altitude.setFloor(2);
    CHECK(altitude.level() == 2 && altitude.knobs().guardsUp);
    altitude.reset();
    CHECK(altitude.level() == 2);
    altitude.restore(0);  // A restored value never goes under the floor.
    CHECK(altitude.level() == 2);
    altitude.setFloor(0);
    altitude.restore(3);
    CHECK(altitude.level() == 3);

    // A game starts at FL100 and climbs a level for each sortie passed.
    RulesBench bench = single();
    Rules& rules = bench.rules;
    CHECK(rules.altitude() == 0 && rules.flightLevel() == 100);
    pass(bench, CourseId::LoopDrill);
    CHECK(rules.altitude() == 1 && rules.flightLevel() == 200);
    CHECK(bench.message() == std::string_view("LOOP DRILL COMPLETE"));
    bench.wait(Rules::kMessageSeconds);
    CHECK(bench.message() == std::string_view("CLIMB TO FL200"));
    pass(bench, CourseId::RampRuns);
    CHECK(rules.altitude() == 2);
    for (const CourseId id : {CourseId::Gunnery, CourseId::Formation}) {
      if (!rules.coursePassed(idx(id))) {
        pass(bench, id);
      }
    }
    CHECK(rules.altitude() == 4 && rules.altitudeKnobs().lightEnergy == 1.0F);
    const int passed = rules.coursesPassed();
    for (int i = 0; i < 4 && rules.coursesPassed() == passed; ++i) {
      bench.wait(tuning::kComboSeconds);
      for (int ramp = 0; ramp < 4 && !rules.briefingLit(); ++ramp) {
        bench.runway();
      }
      bench.scoop();
      if (rules.courseRunning()) {
        finish(bench);
      }
    }
    CHECK(rules.coursesPassed() > passed && rules.altitude() == 4);  // The top holds.

    // A new game starts low again; a floor holds it up (the bench's and the
    // tools' setting); with progression off it stays where it is.
    rules.startGame();
    bench.apply();
    CHECK(rules.altitude() == 0);
    rules.setAltitudeFloor(progression::kTopLevel);
    rules.startGame();
    bench.apply();
    CHECK(rules.altitude() == progression::kTopLevel && rules.flightLevel() == 500);
    rules.setAltitudeFloor(0);
    rules.startGame();
    bench.apply();
    CHECK(rules.altitude() == 0);
    rules.setProgressionEnabled(false);
    rules.onBallInPlay();
    bench.wait(Rules::kBallSaveSeconds);
    pass(bench, CourseId::LoopDrill);
    CHECK(rules.altitude() == 0 && rules.coursePassed(idx(CourseId::LoopDrill)));
    rules.setProgressionEnabled(true);
  }
  {  // Every sortie can be qualified by its own switches and won, WINGS
     // CEREMONY lit and flown, and the AIR SHOW reached, with the table as
     // built (BOGEY and RADAR cut: NIGHT LANDING by the nest pair, FORMATION
     // by T-A-X-I). No BOGEY or RADAR words are ever shown.
    RulesBench bench = single();
    Rules& rules = bench.rules;
    bool bogeyWords = false;
    const auto look = [&] {
      const std::string_view said = rules.message();
      bogeyWords = bogeyWords || (!table::kBogeyOnTable && said.find("BOGEY") != std::string_view::npos) ||
                   (!table::kRadarOnTable && said.find("RADAR") != std::string_view::npos);
    };
    for (std::size_t course = 0; course < pinball::kCourseCount; ++course) {
      const auto id = static_cast<CourseId>(course);
      for (int round = 0; round < 8 && !rules.coursePassed(course); ++round) {
        for (int k = 0; k < 64 && !rules.courseQualified(course) && !rules.coursePassed(course);
             ++k) {
          qualifyStep(bench, id, k);
          look();
        }
        CHECK(rules.courseQualified(course) || rules.coursePassed(course));
        bench.wait(tuning::kComboSeconds);
        bench.scoop();
        look();
        if (rules.courseRunning()) {
          finish(bench);
          look();
        }
        bench.wait(tuning::kComboSeconds);
      }
      CHECK(rules.coursePassed(course));
    }
    CHECK(rules.coursesPassed() == pinball::kCourseCount && rules.rank() == 5);
    CHECK(rules.wingsLit());
    bench.wait(tuning::kComboSeconds);
    bench.scoop();
    CHECK(rules.wingsRunning());
    bench.wait(61.0);
    CHECK(!rules.wingsRunning() && rules.wingsCeremonies() == 1);
    // The Air Show also asks for both multiballs played (their own tests
    // light them; the lock asks for no BOGEY, MultiballTest) and A-C-E.
    pinball::save::RulesRecord record = rules.record();
    record.academyMultiballs = 1;
    record.turboMultiballs = 1;
    record.multiballEnabled = true;
    rules.restore(record);
    for (std::size_t i = 0; i < 3; ++i) {
      rules.onStandup(table::kDeckStandupA + i, 100.0F);
      look();
    }
    CHECK(rules.aceDone() && rules.graduationLit());
    bench.wait(tuning::kComboSeconds);
    bench.scoop();
    look();
    CHECK(rules.graduationRunning() && rules.multiball() == Multiball::Graduation);
    CHECK(!bogeyWords);
  }
}
