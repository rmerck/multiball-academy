#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "core/DisplayText.hpp"

// The academy's sorties (courses in the code) and its ranks: names, tasks, times and awards. The
// rules (core/Rules.hpp) run them; this is the catalogue.
namespace pinball {

// --- Ranks -----------------------------------------------------------------------
// One for each step of a game: the start, five courses passed, and
// commencement on Graduation Day.
inline constexpr std::size_t kRankCount = 7;
inline constexpr std::array<std::string_view, kRankCount> kRankNames{
    "ROOKIE", "TRAINEE", "WINGMATE", "PILOT", "ACE", "INSTRUCTOR", "CHANCELLOR"};
inline constexpr std::size_t kTopRank = kRankCount - 1;

// The name of a rank; the index is clamped into range.
std::string_view rankName(std::size_t rank) noexcept;

// --- Sorties (the code calls them courses) ----------------------------------------
// Concept A's six flight-school sorties. Each is qualified by its own kind
// of shot (Course::qualify of them, counted all game while it is not
// running), and started at the Mission scoop: a qualified sortie first, or
// the one the Mission offers when ramps (or F-L-A-P-S) light it.
enum class CourseId : std::uint8_t {
  LoopDrill,     // Loops; alternating sides pay double.
  RampRuns,      // Ramps; the lit ramp moves to the other side.
  Gunnery,       // Drone hits; each adds time; BANDIT DOWN doubles the pass.
  Formation,     // The lit shot, which moves.
  NightLanding,  // Runway (the approach), then Skyway (the landing): a hurry-up.
  Dogfight,      // Two balls; a drone hit lights the Hangar's dogfight jackpot.
};
inline constexpr std::size_t kCourseCount = 6;

struct Course {
  std::string_view name;
  double seconds;  // Time allowed, at FL100 (times the Altitude's timerScale).
  int goal;        // How many of its task pass it.
  // Paid for each step of the task, times overdrive.
  std::uint64_t shotAward;
  // Paid for passing, as stated.
  std::uint64_t passAward;
  // The goal line beside the count.
  std::string_view goalLine;
  // Qualifying shots (or sets) that light it at the Mission.
  int qualify;
};

// Balance: times, goals and awards are tuned against the acceptance suite
// (tests/AcceptanceMain.cpp, the rule rows). Qualifiers while BOGEY and
// RADAR are not built (geometry G2): Formation by T-A-X-I, Night Landing by
// the two nest targets.
inline constexpr std::array<Course, kCourseCount> kCourses{{
    {"LOOP DRILL", 40.0, 3, 25'000, 125'000, "SHOOT THE LOOPS", 3},
    {"RAMP RUNS", 40.0, 4, 20'000, 125'000, "SHOOT THE LIT RAMP", 4},
    {"GUNNERY", 30.0, 4, 15'000, 100'000, "HIT THE BANDIT", 3},
    {"FORMATION", 45.0, 3, 30'000, 150'000, "SHOOT THE LIT ARROW", 1},
    // The pass award is the hurry-up's floor; the goal is the two shots.
    {"NIGHT LANDING", 40.0, 2, 10'000, 100'000, "APPROACH: SHOOT RUNWAY", 1},
    {"DOGFIGHT", 45.0, 2, 25'000, 200'000, "HIT BANDIT THEN HANGAR", 1},
}};

// Other goal lines of the sortie window.
inline constexpr std::string_view kGoalLanding = "LAND IT: SHOOT SKYWAY";
inline constexpr std::string_view kGoalDogfightJackpot = "SHOOT THE HANGAR";
inline constexpr std::string_view kGoalWings = "SHOOT EVERY ARROW 2X";
inline constexpr std::string_view kGoalExam = "SHOOT EVERY LIT ARROW";
inline constexpr std::string_view kGoalFinaleBandit = "KNOCK DOWN THE BANDIT";
inline constexpr std::string_view kGoalCommencement = "SHOOT THE HANGAR";
inline constexpr std::string_view kGoalBriefingLit = "SHOOT THE MISSION SCOOP";
inline constexpr std::string_view kGoalBriefingUnlit = "SHOOT RAMPS FOR A MISSION";
inline constexpr std::string_view kGoalNeedSquadron = "PLAY SQUADRON MULTIBALL";
inline constexpr std::string_view kGoalNeedScramble = "PLAY SCRAMBLE MULTIBALL";
inline constexpr std::string_view kGoalNeedAce = "SPELL A-C-E ON THE DECK";
// The mini-wizard (all six sorties started) and the wizard (all six
// passed, both multiballs played and A-C-E spelt).
inline constexpr std::string_view kWingsName = "WINGS CEREMONY";
inline constexpr std::string_view kGraduationName = "AIR SHOW";
inline constexpr std::string_view kFinaleName = "AIR SHOW FINALE";

// NIGHT LANDING's hurry-up: the pass award starts here and falls by the
// rate for every whole second of the sortie's clock, down to its floor.
inline constexpr std::uint64_t kHurryUpStart = 300'000;
inline constexpr std::uint64_t kHurryUpPerSecond = 7'500;
// FORMATION's lit shot moves on after this long if it is not made.
inline constexpr double kNavigationHopSeconds = 5.0;
// GUNNERY: each hit on the drone adds this much to the clock.
inline constexpr double kGunneryHitSeconds = 5.0;
// A sortie carried over a drain has at least this long left.
inline constexpr double kCourseCarrySeconds = 15.0;
// WINGS CEREMONY lasts this long (times the Altitude's timerScale).
inline constexpr double kWingsSeconds = 60.0;

// Every name fits wherever it is shown.
namespace detail {
constexpr bool catalogueFits() noexcept {
  for (const std::string_view name : kRankNames) {
    if (!display::fits(name, display::kMaxRankNameLength)) {
      return false;
    }
  }
  for (const Course& course : kCourses) {
    if (!display::fits(course.name, display::kMaxMissionNameLength) ||
        !display::fits(course.goalLine, display::kMaxGoalTextLength) || course.goal < 1 ||
        course.goal > 255 || !(course.seconds > 0.0)) {
      return false;
    }
  }
  for (const Course& course : kCourses) {
    if (course.qualify < 1) {
      return false;
    }
  }
  for (const std::string_view line :
       {kGoalLanding, kGoalDogfightJackpot, kGoalWings, kGoalExam, kGoalFinaleBandit,
        kGoalCommencement, kGoalBriefingLit, kGoalBriefingUnlit, kGoalNeedSquadron,
        kGoalNeedScramble, kGoalNeedAce}) {
    if (!display::fits(line, display::kMaxGoalTextLength)) {
      return false;
    }
  }
  return display::fits(kGraduationName, display::kMaxMissionNameLength) &&
         display::fits(kWingsName, display::kMaxMissionNameLength) &&
         display::fits(kFinaleName, display::kMaxMissionNameLength);
}
}  // namespace detail
static_assert(detail::catalogueFits(), "a rank, course or goal line does not fit its window");

}  // namespace pinball
