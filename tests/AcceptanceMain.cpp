// The full acceptance suite of TABLE.md section 5, as a report.
//
//   pinball_acceptance [--quick] [section ...]
//
// Sections: launch shots lanes exits rest wedge p1 p2 p2saver stress. With none
// named, all run (well over an hour in one process; name sections to split
// the work between processes, as the CTest label `acceptance` does). --quick cuts every sample to a
// fraction, to try the program out; its numbers are not the suite's.
//
// Each criterion is one line: id, what is measured, the threshold, the
// measured value and a verdict.
//   PASS        met.
//   FAIL        a fairness or structural criterion is not met. The program
//               then ends with status 1.
//   KNOWN-MISS  a pacing criterion that the design is known to miss
//               (TABLE.md 6.8), no worse than its pin: the value recorded
//               for it below plus a margin for the size of the sample. Not
//               a failure of the build. Beyond the pin it is a FAIL: the
//               miss is known, a regression from it is not. (With --quick
//               the samples are others and the pins are not applied.)
//   INFO        measured for the record; by the owner's decision not a test.
//   DEFERRED    needs an element type that the game does not have yet.
#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include "bots/Acceptance.hpp"
#include "core/Format.hpp"
#include "core/TableLayout.hpp"

namespace {

using namespace pinball::bots;
using namespace pinball::bots::acceptance;
namespace table = pinball::table;

int failures = 0;
bool quickRun = false;

// --- The pins ---------------------------------------------------------------------
// What each known miss measured when it was recorded (2026-11-13, x86-64, g++ 13 -O2), and
// how far a run may be from that before it fails. The suite is seeded, so
// an unchanged game repeats these numbers exactly; the margins are there
// so that a change which only reshuffles the chances (two standard errors
// of the statistic at its sample size) is told from one that makes the
// table worse.
//
// B1s: skill shots out through the gap untouched. Recorded 12 of 382: a
// binomial count with a standard deviation of 3.4.
constexpr int kPinSkillShotDrains = 19;
// A1e: the third easiest shot's window. Recorded 5 steps (wanted: 6). A
// sweep of the geometry, not a sample: no margin.
constexpr int kPinThirdShotWindow = 5;
// B2a at timing errors 5, 6 and 8: percent of 5,000 balls ending within
// 5 s (wanted: under 2). Recorded 2.3, 3.5 and 3.2; a share near 3 percent of
// 5,000 has a standard error of 0.25 points, and the pin is 0.6 over.
constexpr double kPinUnder5Margin = 0.6;
constexpr std::array<double, 3> kRecordedUnder5{2.3, 3.5, 3.2};  // E = 5, 6, 8.
// B3b at E = 6: outlanes apart, in points of 5,000 drains (wanted: within
// 10). Recorded 8.9, a pass, where an earlier sample missed; the difference of two shares near 25
// percent has a standard error of 1.0 point.
constexpr double kPinOutlanesApart = 10.9;
// C2a and C2c, 400 balls with the 15 s saver at E = 3, 5, 8: the median
// (wanted: 45 to 90 s) and the 90th percentile (wanted: at most 300 s).
// Ball times are close to exponential, for which the standard error of the
// median of 400 is 7 percent and of the 90th percentile 6.5 percent: the
// pins are 15 and 13 percent over what was recorded.
constexpr std::array<double, 3> kRecordedMedian{131.4, 100.5, 97.2};
constexpr std::array<double, 3> kRecordedNinetieth{381.4, 282.8, 288.3};
constexpr double kPinMedianFactor = 1.15;
constexpr double kPinNinetiethFactor = 1.13;
// D3 at E = 3, 5,000 balls: balls that reach the 20 minute cap, recorded 4:
// a Poisson count, pinned two standard deviations over. And the longest a
// ball in play went without touching a flipper, recorded 1101.1 s: there is
// a cycle on this table that needs no player, and a ball in it runs to the
// cap. (Where it runs was not traced here. The Briefing room kicks every
// ball out the same way, and the element that varies its kick is to come.) The pin is therefore the
// cap itself, and says only that this row is measured; bring it down to the 20 s wanted when the
// cycle is broken.
constexpr int kPinTimeCapBalls = 8;
constexpr double kPinNoFlipperSeconds = 1200.0;

enum class Kind { Hard, KnownMiss, Info, Deferred };

PINBALL_PRINTF_LIKE(1, 2)
std::string text(const char* format, ...) {
  std::array<char, 512> buffer{};
  va_list args;
  va_start(args, format);
  std::vsnprintf(buffer.data(), buffer.size(), format, args);
  va_end(args);
  return buffer.data();
}

double percentOf(int part, std::size_t whole) {
  return whole == 0 ? 0.0 : 100.0 * part / static_cast<double>(whole);
}

void row(Kind kind, bool met, std::string_view id, std::string_view what,
         std::string_view threshold, const std::string& measured) {
  const char* verdict = "PASS";
  if (kind == Kind::Info) {
    verdict = "INFO";
  } else if (kind == Kind::Deferred) {
    verdict = "DEFERRED";
  } else if (!met) {
    verdict = kind == Kind::KnownMiss ? "KNOWN-MISS" : "FAIL";
    failures += kind == Kind::Hard ? 1 : 0;
  }
  std::printf("ROW | %-4.*s | %-58.*s | %-34.*s | %-44s | %s\n", static_cast<int>(id.size()),
              id.data(), static_cast<int>(what.size()), what.data(),
              static_cast<int>(threshold.size()), threshold.data(), measured.c_str(), verdict);
  std::fflush(stdout);
}

// A known miss: PASS if the criterion is met after all, KNOWN-MISS while it
// is no worse than its pin, FAIL beyond.
void pinnedRow(bool met, bool withinPin, std::string_view id, std::string_view what,
               const std::string& threshold, const std::string& measured) {
  const bool regressed = !met && !withinPin && !quickRun;
  row(regressed ? Kind::Hard : Kind::KnownMiss, met, id, what, threshold, measured);
}

std::string firsts(const TouchCounts& counts) {
  std::string out;
  for (std::size_t i = 0; i < counts.size(); ++i) {
    if (counts[i] > 0) {
      out += text("%s%s %d", out.empty() ? "" : ", ", touchName(static_cast<Touch>(i)), counts[i]);
    }
  }
  return out;
}

void sectionLaunch(bool quick) {
  const LaunchResult launch = launchSweep(quick ? 6 : 1, quick ? 2 : 8);
  std::printf("# launch: %d launches, back on the plunger %d; first touch: %s\n", launch.launches,
              launch.backOnPlunger, firsts(launch.first).c_str());
  row(Kind::Hard, launch.unsafe == 0, "B1",
      "launches whose first touch is not a flipper, inlane or roll call lane", "0",
      text("%d of %d (%d too short to leave)", launch.unsafe, launch.launches,
           launch.backOnPlunger));
  row(Kind::Hard, launch.largestKick < 400.0F, "D4a",
      "largest one-step speed gain of a ball leaving the launch channel", "under 400 px/s",
      text("%.0f px/s", static_cast<double>(launch.largestKick)));
  const DescentResult descents = slowDescents(quick ? 6 : 1);
  std::printf("# descents: first touch: %s\n", firsts(descents.first).c_str());
  row(Kind::Hard, descents.throughGapUntouched == 0, "B1d",
      "slow descents down a circuit lane out through the gap untouched", "0",
      text("%d of %d", descents.throughGapUntouched, descents.descents));
  const SkillShotResult skill = skillShotDrains(quick ? 6 : 1, quick ? 2 : 8);
  pinnedRow(skill.throughGapUntouched == 0, skill.throughGapUntouched <= kPinSkillShotDrains, "B1s",
            "skill-shot launches, idle flippers, out through the gap untouched",
            text("0; pinned at most %d", kPinSkillShotDrains),
            text("%d of %d", skill.throughGapUntouched, skill.reachedRollCall));
  for (int speed = 1200; speed <= 1900; speed += 100) {
    const AutoLaunchRow sweep = autoLaunchAt(speed, quick ? 8 : 40);
    std::printf(
        "# auto-launch %d px/s: left inlane %d, roll call %d, right inlane %d, back %d, "
        "other %d of %d\n",
        sweep.speed, sweep.leftInlane, sweep.rollCall, sweep.rightInlane, sweep.backOnPlunger,
        sweep.other, sweep.launches);
    if (speed == static_cast<int>(table::kAutoLaunchSpeed)) {
      row(Kind::Hard, sweep.leftInlane == sweep.launches, "B1a",
          "auto-launches at the chosen speed that lap the arch to the left inlane", "all",
          text("%d of %d at %d px/s", sweep.leftInlane, sweep.launches, speed));
    }
  }
  const GameAutoLaunchResult served = gameAutoLaunch(quick ? 2 : 8);
  row(Kind::Hard, served.safeFirstTouch == served.launches && served.relaunches == 0, "B1g",
      "balls served and launched by the game: safe first touch, one launch", "all, 0 second kicks",
      text("%d of %d, %u second kicks", served.safeFirstTouch, served.launches, served.relaunches));
  const OneWayResult arch = oneWayArch(quick ? 40 : 200);
  row(Kind::Hard, arch.crossed == 0, "D4b",
      "balls fired at the one-way arch from the field that cross it", "0",
      text("%d of %d", arch.crossed, arch.fired));
}

void sectionShots(bool quick) {
  // Sixteen copies of each feed, a little apart: about 200 first hits a shot.
  const ShotMap map = shotSweep(quick ? 1 : 16);
  for (const std::string& fan : map.fans) {
    std::printf("# fan %s\n", fan.c_str());
  }
  struct Major {
    Touch touch;
    const char* name;
  };
  // The Hangar cannot be entered while the bay doors are fixed targets: the
  // shot is measured by the door bank it must hit, which has the same mouth.
  constexpr std::array<Major, 7> kMajor{{{Touch::CircuitLeft, "left circuit"},
                                         {Touch::Runway, "Runway"},
                                         {Touch::Door, "Hangar (bay doors)"},
                                         {Touch::Briefing, "Briefing room"},
                                         {Touch::Turbine, "Turbine lane"},
                                         {Touch::Skyway, "Skyway"},
                                         {Touch::CircuitRight, "right circuit"}}};
  std::vector<int> best;
  int narrowest = 1000;
  int fromCradle = 0;
  std::string windows;
  std::string cradles;
  for (const Major& major : kMajor) {
    const ShotWindow& window = map.shots[static_cast<std::size_t>(major.touch)];
    std::printf(
        "# shot %-20s best %2d  cradle %2d  inlane %2d  ramp exit %2d  first hits %3d  "
        "through the gap untouched %d  reached arch %d  completed %d\n",
        major.name, window.best, window.cradle, window.inlane, window.rampExit, window.firstHits,
        window.throughGapUntouched, window.reachedArch, window.completed);
    best.push_back(window.best);
    narrowest = std::min(narrowest, window.best);
    fromCradle += window.cradle >= 3 ? 1 : 0;
    windows += text("%s%d", windows.empty() ? "" : "/", window.best);
    cradles += text("%s%d", cradles.empty() ? "" : "/", window.cradle);
  }
  std::ranges::sort(best, std::greater<>());
  row(Kind::Hard, narrowest >= 3, "A1", "widest timing window of each major shot, steps",
      "each at least 3",
      windows + " (circuit, Runway, Hangar, Briefing, Turbine, Skyway, circuit)");
  pinnedRow(best[2] >= 6, best[2] >= kPinThirdShotWindow, "A1e", "easiest three shots, steps",
            text("each at least 6; pinned %d", kPinThirdShotWindow),
            text("%d/%d/%d", best[0], best[1], best[2]));
  row(Kind::Hard, fromCradle >= 5, "A2", "major shots with a window of 3 or more from a cradle",
      "at least 5", text("%d (%s)", fromCradle, cradles.c_str()));
  // B4: per first-hit feature, the share that then left through the gap
  // without meeting a flipper. A feature is judged once it has enough first
  // hits for a share to mean something; the others are listed.
  constexpr int kEnoughHits = 40;
  bool allUnder = true;
  int fewest = 1000000;
  std::string worst = "none";
  double worstShare = 0.0;
  std::string tooFew;
  for (const Touch touch :
       {Touch::CircuitLeft, Touch::Runway, Touch::Door, Touch::Briefing, Touch::Turbine,
        Touch::Skyway, Touch::CircuitRight, Touch::Bumper, Touch::Slingshot, Touch::TargetLeft,
        Touch::TargetRight, Touch::RollCall}) {
    const ShotWindow& window = map.shots[static_cast<std::size_t>(touch)];
    if (window.firstHits == 0) {
      continue;
    }
    const double share = 100.0 * window.throughGapUntouched / window.firstHits;
    std::printf("# rebound %-14s first hits %4d, through the gap untouched %3d (%.1f%%)\n",
                touchName(touch), window.firstHits, window.throughGapUntouched, share);
    if (window.firstHits < kEnoughHits) {
      tooFew += text("%s%s %d/%d", tooFew.empty() ? "" : ", ", touchName(touch),
                     window.throughGapUntouched, window.firstHits);
      continue;
    }
    fewest = std::min(fewest, window.firstHits);
    if (share >= worstShare) {
      worstShare = share;
      worst = touchName(touch);
    }
    allUnder = allUnder && share < 5.0;
  }
  if (!tooFew.empty()) {
    std::printf("# rebound: too few first hits to judge: %s\n", tooFew.c_str());
  }
  row(Kind::Hard, allUnder, "B4", "rebounds out through the gap untouched, per first-hit feature",
      "under 5 percent each",
      text("worst %.1f%% (%s); %d or more hits each", worstShare, worst.c_str(), fewest));
}

void sectionLanes(bool quick) {
  bool circuitsMet = true;
  bool rampsMet = true;
  double slowest = 0.0;
  int circuitWorst = 40;
  for (int side = 0; side < 2; ++side) {
    for (int speed = 900; speed <= 2000; speed += quick ? 300 : 100) {
      const LaneResult lane = circuitAt(side == 1, speed);
      std::printf(
          "# %s circuit at %4d px/s: completed %2d of %d (mean %.2f s, worst %.2f s), "
          "roll call or nest %d\n",
          side == 1 ? "right" : "left", speed, lane.completed, lane.balls, lane.meanSeconds,
          lane.worstSeconds, lane.rollCallOrNest);
      if (speed >= 1500) {
        // The stated minimum: what a firm flipper shot delivers.
        circuitsMet = circuitsMet && lane.completed * 100 >= lane.balls * 95;
        circuitWorst = std::min(circuitWorst, lane.completed);
        slowest = std::max(slowest, lane.worstSeconds);
      }
    }
  }
  int rampWorst = 40;
  int rampSlow = 0;
  for (int side = 0; side < 2; ++side) {
    for (int speed = 500; speed <= 1500; speed += quick ? 200 : 100) {
      const LaneResult ramp = rampAt(side == 1, speed);
      std::printf("# %s at %4d px/s: taken %2d of %d\n", side == 1 ? "Skyway" : "Runway", speed,
                  ramp.completed, ramp.balls);
      if (speed >= 700) {
        rampsMet = rampsMet && ramp.completed * 100 >= ramp.balls * 95;
        rampWorst = std::min(rampWorst, ramp.completed);
      } else {
        rampSlow = std::max(rampSlow, ramp.completed);
      }
    }
  }
  row(Kind::Hard, circuitsMet, "A6c", "circuits entered at 1500 px/s or more that complete",
      "95 percent at each speed", text("fewest %d of 40", circuitWorst));
  row(Kind::Hard, rampsMet, "A6r", "ramps entered at 700 px/s or more that are taken",
      "95 percent at each speed",
      text("fewest %d of 40 (most below 700: %d)", rampWorst, rampSlow));
  // A5: a made ramp is on its rail for the ride and set down at the inlane;
  // a made circuit is timed above.
  row(Kind::Hard, slowest > 0.0 && slowest <= 3.5 && pinball::table::kRampRideSeconds <= 3.5, "A5",
      "four flow shots back at an inlane within 3.5 s of entry", "85 percent of made shots",
      text("circuits at most %.2f s, rails %.2f s", slowest, pinball::table::kRampRideSeconds));
}

void sectionExits(bool quick) {
  const int n = quick ? 40 : 200;
  static constexpr std::array<const char*, 4> kNames{"Briefing room", "Hangar", "Runway rail",
                                                     "Skyway rail"};
  bool met = true;
  std::string measured;
  std::array<ExitResult, 4> exits{};
  for (std::size_t hole = 0; hole < table::kHoles.size(); ++hole) {
    exits[hole] = exitOf(hole, n);
    std::printf("# exit of the %s: %s\n", kNames[hole], firsts(exits[hole].first).c_str());
    met = met && exits[hole].safe * 100 >= exits[hole].releases * 98;
    measured += text("%s%d", measured.empty() ? "" : "/", exits[hole].safe);
  }
  row(Kind::Hard, met, "B5", "releases from each exit that reach a flipper or inlane first",
      "98 percent each", measured + text(" of %d (Briefing, Hangar, Runway, Skyway)", n));
  // A4: the rails cross the ball over to the other flipper's inlane.
  const int runway = exits[table::kRunwayHole].first[static_cast<std::size_t>(Touch::InlaneLeft)];
  const int skyway = exits[table::kSkywayHole].first[static_cast<std::size_t>(Touch::InlaneRight)];
  row(Kind::Hard, runway * 100 >= n * 80 && skyway * 100 >= n * 80, "A4",
      "each flipper has a shot that feeds the opposite inlane within 4 s",
      "80 percent of made shots", text("Runway %d, Skyway %d of %d", runway, skyway, n));
  for (int speed = 900; speed <= 1300; speed += 200) {
    const ExitResult kick = kickbackAt(speed, quick ? 12 : 60);
    std::printf("# kick up the left outlane at %d px/s: %s\n", speed, firsts(kick.first).c_str());
    if (speed == static_cast<int>(-table::kKickbackVelocity.y)) {
      row(Kind::Hard, kick.safe * 100 >= kick.releases * 98, "B6k",
          "a ball kicked up the left outlane at the kickback's speed reaches a flipper or inlane",
          "98 percent", text("%d of %d", kick.safe, kick.releases));
    }
  }
  row(Kind::Deferred, true, "B6", "kickback fires and the ball is not lost again within 3 s (P2)",
      "100 percent; under 2 percent", "no kickback element yet");
}

void sectionRest(bool quick) {
  const RestResult rest = restingPlaces(quick ? 500 : 10000, 9);
  for (const pinball::Vec2 p : rest.restingAt) {
    std::printf("# resting at (%.1f, %.1f)\n", static_cast<double>(p.x), static_cast<double>(p.y));
  }
  row(Kind::Hard, rest.notDrained == 0 && rest.searches == 0, "D2",
      "balls from random places, idle flippers, not drained in 30 s", "0, and no ball search",
      text("%d of %d; %llu ball searches", rest.notDrained, rest.placed,
           static_cast<unsigned long long>(rest.searches)));
}

void sectionWedge(bool quick) {
  const WedgeResult wedge = inlaneWedge(quick ? 7 : 1);
  for (const pinball::Vec2 p : wedge.wedgedAt) {
    std::printf("# wedged at (%.1f, %.1f)\n", static_cast<double>(p.x), static_cast<double>(p.y));
  }
  row(Kind::Hard, wedge.wedged == 0, "D1w",
      "pairs of balls arriving abreast at an inlane mouth that wedge there", "0",
      text("%d of %d", wedge.wedged, wedge.pairs));
}

void reportSoak(const char* label, const SoakResult& soak) {
  std::printf(
      "# %s: balls %zu, steps %llu, median %.1f s, p10 %.1f, p90 %.1f, under 5 s %.1f%%, "
      "under 10 s %.1f%%, drains centre/left/right %.1f/%.1f/%.1f%%, saves %llu, "
      "timeouts %d, replunges %d, stuck %llu, searches %llu, fingerprint %016llx\n",
      label, soak.ballSeconds.size(), static_cast<unsigned long long>(soak.steps),
      soak.percentile(0.5), soak.percentile(0.1), soak.percentile(0.9),
      percentOf(soak.under5, soak.ballSeconds.size()),
      percentOf(soak.under10, soak.ballSeconds.size()), soak.drainShare(0), soak.drainShare(1),
      soak.drainShare(2), static_cast<unsigned long long>(soak.saves), soak.timeouts,
      soak.replunges, static_cast<unsigned long long>(soak.stuck),
      static_cast<unsigned long long>(soak.searches),
      static_cast<unsigned long long>(soak.fingerprint));
  const double perBall =
      1.0 / static_cast<double>(std::max<std::size_t>(soak.ballSeconds.size(), 1));
  const auto of = [&soak, perBall](Touch touch) {
    return static_cast<double>(soak.count[static_cast<std::size_t>(touch)]) * perBall;
  };
  std::printf(
      "#   per ball: bumpers %.1f, slingshots %.1f, Turbine %.1f, doors %.1f, Briefing "
      "%.1f, Runway %.1f, Skyway %.1f, targets %.1f, roll call %.1f, inlanes %.1f, "
      "circuits entered %.1f, reached the arch %.1f, completed %.1f\n",
      of(Touch::Bumper), of(Touch::Slingshot), of(Touch::Turbine), of(Touch::Door),
      of(Touch::Briefing), of(Touch::Runway), of(Touch::Skyway),
      of(Touch::TargetLeft) + of(Touch::TargetRight), of(Touch::RollCall),
      of(Touch::InlaneLeft) + of(Touch::InlaneRight),
      static_cast<double>(soak.circuitEntered[0] + soak.circuitEntered[1]) * perBall,
      static_cast<double>(soak.circuitTop[0] + soak.circuitTop[1]) * perBall,
      static_cast<double>(soak.circuitDone[0] + soak.circuitDone[1]) * perBall);
  for (std::size_t i = 0; i < soak.stuckAt.size() && i < 12; ++i) {
    std::printf("#   stuck at (%.0f, %.0f)\n", static_cast<double>(soak.stuckAt[i].x),
                static_cast<double>(soak.stuckAt[i].y));
  }
}

// Stuck balls and the bounds checks, for any soak.
void rowsForSoundness(const char* id, const char* who, std::uint64_t steps, std::uint64_t stuck,
                      std::uint64_t searches, std::uint64_t laneIntrusions, std::uint64_t lost,
                      std::uint64_t overSpeed) {
  row(Kind::Hard, stuck == 0 && searches == 0, id,
      text("%s: balls stuck within 12 units for 5 s, or freed by the ball search", who), "0",
      text("%llu stuck, %llu searches in %.1f million steps",
           static_cast<unsigned long long>(stuck), static_cast<unsigned long long>(searches),
           static_cast<double>(steps) / 1.0e6));
  row(Kind::Hard, laneIntrusions == 0 && lost == 0 && overSpeed == 0, "D4",
      text("%s: ball in play inside the launch lane, lost outside, or over the speed cap", who),
      "0 steps",
      text("%llu / %llu / %llu", static_cast<unsigned long long>(laneIntrusions),
           static_cast<unsigned long long>(lost), static_cast<unsigned long long>(overSpeed)));
}

void sectionRandom(bool quick) {
  SoakOptions options;
  options.bot = BotKind::Random;
  options.balls = quick ? 150 : 2000;
  options.seed = 11;
  const SoakResult plain = soak(options);
  reportSoak("P1, saver off", plain);
  options.balls = quick ? 100 : 1000;
  options.seed = 12;
  options.saverSeconds = 15.0;
  const SoakResult saved = soak(options);
  reportSoak("P1, saver 15 s", saved);
  row(Kind::Info, true, "C1", "P1 median ball time, saver on", "15 to 40 s (informational)",
      text("%.1f s (saver off %.1f s, %.0f%% under 10 s)", saved.percentile(0.5),
           plain.percentile(0.5), percentOf(plain.under10, plain.ballSeconds.size())));
  // A3: everything registers. The Hangar is behind its doors until they can drop.
  std::uint64_t least = ~std::uint64_t{0};
  std::string leastName = "none";
  const auto see = [&least, &leastName](std::uint64_t hits, const std::string& name) {
    if (hits < least) {
      least = hits;
      leastName = name;
    }
  };
  for (std::size_t i = 0; i < plain.bumperHits.size(); ++i) {
    see(plain.bumperHits[i], text("bumper %zu", i));
  }
  for (std::size_t i = 0; i < plain.targetHits.size(); ++i) {
    see(plain.targetHits[i], text("turbo target %zu", i));
  }
  for (std::size_t i = 0; i < plain.doorHits.size(); ++i) {
    see(plain.doorHits[i], text("bay door %zu", i));
  }
  for (std::size_t i = 0; i < plain.rolloverHits.size(); ++i) {
    see(plain.rolloverHits[i], text("rollover %zu", i));
  }
  for (std::size_t i = 0; i < plain.slingHits.size(); ++i) {
    see(plain.slingHits[i], text("slingshot %zu", i));
  }
  for (std::size_t i = 0; i < plain.holeHits.size(); ++i) {
    if (i != table::kHangarHole) {
      see(plain.holeHits[i], text("hole %zu", i));
    }
  }
  row(Kind::Hard, least > 0, "A3", "every bumper, target, door, rollover and hole registers (P1)",
      "each at least once",
      text("least hit: %s, %llu (Hangar entry deferred: %llu)", leastName.c_str(),
           static_cast<unsigned long long>(least),
           static_cast<unsigned long long>(plain.holeHits[table::kHangarHole])));
  rowsForSoundness("D1", "P1, one ball", plain.steps + saved.steps, plain.stuck + saved.stuck,
                   plain.searches + saved.searches, plain.laneIntrusions + saved.laneIntrusions,
                   plain.lostBalls + saved.lostBalls, plain.overSpeed + saved.overSpeed);
}

constexpr std::array<int, 5> kErrors{3, 4, 5, 6, 8};

void sectionReflex(bool quick, int onlyError) {
  for (const int error : kErrors) {
    if (onlyError != 0 && onlyError != error) {
      continue;
    }
    SoakOptions options;
    options.bot = BotKind::Reflex;
    options.reflexError = error;
    // Five thousand balls, as the criterion is written: with five hundred
    // a share is uncertain by two points either way, which is as wide as
    // the margins it is judged by.
    options.balls = quick ? 60 : 5000;
    options.seed = 21;
    const SoakResult result = soak(options);
    reportSoak(text("P2 E=%d, saver off", error).c_str(), result);
    const double under5 = percentOf(result.under5, result.ballSeconds.size());
    const double under10 = percentOf(result.under10, result.ballSeconds.size());
    if (error <= 4) {
      row(Kind::Hard, under5 < 2.0, "B2a", text("P2 E=%d: balls ending within 5 s", error),
          "under 2 percent", text("%.2f%%", under5));
    } else {
      const double pin = kRecordedUnder5[error == 5 ? 0 : (error == 6 ? 1 : 2)] + kPinUnder5Margin;
      pinnedRow(under5 < 2.0, under5 <= pin, "B2a", text("P2 E=%d: balls ending within 5 s", error),
                text("under 2 percent; pinned %.2f", pin), text("%.2f%%", under5));
    }
    row(Kind::Hard, under10 < 10.0, "B2b", text("P2 E=%d: balls ending within 10 s", error),
        "under 10 percent", text("%.1f%%", under10));
    const double centre = result.drainShare(0);
    const double leftOut = result.drainShare(1);
    const double rightOut = result.drainShare(2);
    row(Kind::Hard,
        centre >= 35.0 && centre <= 60.0 && leftOut >= 15.0 && leftOut <= 35.0 &&
            rightOut >= 15.0 && rightOut <= 35.0,
        "B3a", text("P2 E=%d: drain split centre / left / right", error),
        "centre 35-60, outlanes 15-35 percent",
        text("%.1f / %.1f / %.1f", centre, leftOut, rightOut));
    const double apart = leftOut > rightOut ? leftOut - rightOut : rightOut - leftOut;
    // Known to be missed at E = 6 only (and there only in some samples).
    if (error == 6) {
      pinnedRow(
          apart <= 10.0, apart <= kPinOutlanesApart, "B3b", text("P2 E=%d: outlanes apart", error),
          text("within 10 points; pinned %.1f", kPinOutlanesApart), text("%.1f points", apart));
    } else {
      row(Kind::Hard, apart <= 10.0, "B3b", text("P2 E=%d: outlanes apart", error),
          "within 10 points", text("%.1f points", apart));
    }
    const double median = result.percentile(0.5);
    row(Kind::Info, true, "C2n", text("P2 E=%d: median ball, saver off (not a criterion)", error),
        "45 to 90 s",
        text("%.1f s (p10 %.1f, p90 %.1f)", median, result.percentile(0.1),
             result.percentile(0.9)));
    rowsForSoundness("D1", text("P2 E=%d, one ball", error).c_str(), result.steps, result.stuck,
                     result.searches, result.laneIntrusions, result.lostBalls, result.overSpeed);
    if (error == 3) {
      // D3, the loop or safe cycle, where the bot is at its most exact.
      const double noFlipper = static_cast<double>(result.longestNoFlipper) / 120.0;
      pinnedRow(result.timeouts == 0, result.timeouts <= kPinTimeCapBalls, "D3a",
                "P2 E=3: balls kept in play to the 20 minute cap",
                text("0; pinned at most %d", kPinTimeCapBalls),
                text("%d of %zu", result.timeouts, result.ballSeconds.size()));
      pinnedRow(noFlipper <= 20.0, noFlipper <= kPinNoFlipperSeconds, "D3b",
                "P2 E=3: longest a ball in play went without touching a flipper",
                text("at most 20 s; pinned %.1f", kPinNoFlipperSeconds), text("%.1f s", noFlipper));
    }
  }
}

void sectionReflexSaver(bool quick, int onlyError) {
  for (const int error : {3, 5, 8}) {
    if (onlyError != 0 && onlyError != error) {
      continue;
    }
    SoakOptions options;
    options.bot = BotKind::Reflex;
    options.reflexError = error;
    options.balls = quick ? 50 : 400;
    options.seed = 23;
    options.saverSeconds = 15.0;
    const SoakResult result = soak(options);
    reportSoak(text("P2 E=%d, saver 15 s", error).c_str(), result);
    const double median = result.percentile(0.5);
    const std::size_t which = error == 3 ? 0 : (error == 5 ? 1 : 2);
    const double medianPin = kRecordedMedian[which] * kPinMedianFactor;
    pinnedRow(median >= 45.0 && median <= 90.0, median >= 45.0 && median <= medianPin, "C2a",
              text("P2 E=%d: median ball, 15 s saver (no kickback yet)", error),
              text("45 to 90 s; pinned 45 to %.1f", medianPin), text("%.1f s", median));
    row(Kind::Hard, result.percentile(0.1) >= 12.0, "C2b",
        text("P2 E=%d: 10th percentile ball, 15 s saver", error), "at least 12 s",
        text("%.1f s", result.percentile(0.1)));
    const double ninetiethPin = kRecordedNinetieth[which] * kPinNinetiethFactor;
    pinnedRow(result.percentile(0.9) <= 300.0, result.percentile(0.9) <= ninetiethPin, "C2c",
              text("P2 E=%d: 90th percentile ball, 15 s saver", error),
              text("at most 300 s; pinned %.1f", ninetiethPin),
              text("%.1f s", result.percentile(0.9)));
    rowsForSoundness("D1", text("P2 E=%d with saver, one ball", error).c_str(), result.steps,
                     result.stuck, result.searches, result.laneIntrusions, result.lostBalls,
                     result.overSpeed);
  }
}

void sectionStress(bool quick, std::string_view which) {
  struct Run {
    BotKind bot;
    std::uint64_t seed;
    const char* name;
  };
  for (const Run& run : {Run{BotKind::Random, 31, "P1"}, Run{BotKind::Reflex, 32, "P2 E=5"}}) {
    if (!which.empty() && which != (run.bot == BotKind::Random ? "p1" : "p2")) {
      continue;
    }
    StressOptions options;
    options.bot = run.bot;
    options.seed = run.seed;
    options.steps = quick ? 200000 : 5000000;
    options.balls = 3;
    const StressResult result = stress(options);
    std::printf(
        "# stress %s: steps %llu, three free %llu, drains %llu, auto-launches %u (second "
        "kicks %u), overlap steps %llu (longest %llu), fingerprint %016llx\n",
        run.name, static_cast<unsigned long long>(result.steps),
        static_cast<unsigned long long>(result.stepsWithAllFree),
        static_cast<unsigned long long>(result.drains), result.autoLaunches, result.relaunches,
        static_cast<unsigned long long>(result.overlapSteps),
        static_cast<unsigned long long>(result.longestOverlap),
        static_cast<unsigned long long>(result.fingerprint));
    for (std::size_t i = 0; i < result.stuckAt.size() && i < 12; ++i) {
      std::printf("#   stuck at (%.0f, %.0f)\n", static_cast<double>(result.stuckAt[i].x),
                  static_cast<double>(result.stuckAt[i].y));
    }
    const std::string who = text("%s, three balls", run.name);
    rowsForSoundness("D1", who.c_str(), result.steps, result.stuck, result.searches,
                     result.laneIntrusions, result.lostBalls, result.overSpeed);
    // A launched ball can meet a ball in play at the one-way arch and be
    // knocked back down the channel; the game launches it again. What must
    // not happen is a ball left waiting in the lane.
    row(Kind::Hard, result.longestInLane <= 10U * 120U, "B1r",
        text("%s: longest wait of a served ball in the launch lane", who.c_str()), "at most 10 s",
        text("%.1f s; %u of %u launches needed a second kick",
             static_cast<double>(result.longestInLane) / 120.0, result.relaunches,
             result.autoLaunches));
    row(Kind::Hard, result.countBroken == 0, "D5",
        text("%s: balls on the table plus balls waiting never above three, never none",
             who.c_str()),
        "0 steps", text("%llu", static_cast<unsigned long long>(result.countBroken)));
    row(Kind::Hard, result.longestOverlap <= 4, "D4o",
        text("%s: two free balls more than 2 units into each other", who.c_str()),
        "never for more than 4 steps",
        text("%llu steps, longest run %llu", static_cast<unsigned long long>(result.overlapSteps),
             static_cast<unsigned long long>(result.longestOverlap)));
  }
}

}  // namespace

int main(int argc, char** argv) {
  bool quick = false;
  std::vector<std::string_view> sections;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--quick") {
      quick = true;
      quickRun = true;
    } else {
      sections.push_back(arg);
    }
  }
  if (sections.empty()) {
    sections = {"launch", "shots", "lanes", "exits",   "rest",
                "wedge",  "p1",    "p2",    "p2saver", "stress"};
  }
  std::printf("# Acceptance suite, TABLE.md section 5%s\n",
              quick ? " (QUICK: reduced samples)" : "");
  std::printf("ROW | id   | %-58s | %-34s | %-44s | verdict\n", "criterion", "threshold",
              "measured");
  for (const std::string_view section : sections) {
    // "p2:5" runs one timing error; "stress:p1" one bot.
    const std::size_t colon = section.find(':');
    const std::string_view name = section.substr(0, colon);
    const std::string_view option =
        colon == std::string_view::npos ? std::string_view{} : section.substr(colon + 1);
    const int number = option.empty() ? 0 : std::atoi(std::string(option).c_str());
    if (name == "launch") {
      sectionLaunch(quick);
    } else if (name == "shots") {
      sectionShots(quick);
    } else if (name == "lanes") {
      sectionLanes(quick);
    } else if (name == "exits") {
      sectionExits(quick);
    } else if (name == "rest") {
      sectionRest(quick);
    } else if (name == "wedge") {
      sectionWedge(quick);
    } else if (name == "p1") {
      sectionRandom(quick);
    } else if (name == "p2") {
      sectionReflex(quick, number);
    } else if (name == "p2saver") {
      sectionReflexSaver(quick, number);
    } else if (name == "stress") {
      sectionStress(quick, option);
    } else {
      std::fprintf(stderr, "pinball_acceptance: unknown section '%.*s'\n",
                   static_cast<int>(section.size()), section.data());
      return 2;
    }
  }
  std::printf("# %d failure(s)\n", failures);
  return failures == 0 ? 0 : 1;
}
