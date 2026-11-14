#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "core/DisplayText.hpp"

// The academy's courses and its ranks: names, tasks, times and awards. The
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

// --- Courses ---------------------------------------------------------------------
enum class CourseId : std::uint8_t {
  BumperBasics,     // Thruster bumper hits.
  RunwayDrills,     // Ramps, either one.
  CircuitTraining,  // Circuits made.
  HangarSafety,     // Drop the bay doors, then enter the Hangar.
  TurbineTheory,    // Spins, against a falling award.
  Navigation,       // The lit shot, which moves.
};
inline constexpr std::size_t kCourseCount = 6;

struct Course {
  std::string_view name;
  double seconds;  // Time allowed.
  int goal;        // How many of its task pass it.
  // Paid for each step of the task, times overdrive.
  std::uint64_t shotAward;
  // Paid for passing, as stated.
  std::uint64_t passAward;
  // The goal line beside the count.
  std::string_view goalLine;
};

// Balance: times, goals and awards are tuned against the acceptance suite
// (tests/AcceptanceMain.cpp, the rule rows).
inline constexpr std::array<Course, kCourseCount> kCourses{{
    {"BUMPER BASICS 101", 35.0, 10, 3'000, 100'000, "HIT THE THRUSTERS"},
    {"RUNWAY DRILLS 103", 35.0, 3, 20'000, 125'000, "SHOOT RUNWAY OR SKYWAY"},
    {"CIRCUIT TRAINING 201", 40.0, 2, 30'000, 150'000, "COMPLETE A CIRCUIT"},
    // The goal is the three doors; the Hangar itself is the second part.
    {"HANGAR SAFETY 202", 40.0, 3, 10'000, 150'000, "HIT THE BAY DOORS"},
    // The pass award is the hurry-up's floor.
    {"TURBINE THEORY 301", 35.0, 30, 0, 100'000, "SPIN THE TURBINE"},
    {"NAVIGATION 302", 45.0, 3, 30'000, 200'000, "SHOOT THE LIT ARROW"},
}};

// Other goal lines of the course window.
inline constexpr std::string_view kGoalEnterHangar = "ENTER THE HANGAR";
inline constexpr std::string_view kGoalExam = "SHOOT EVERY LIT ARROW";
inline constexpr std::string_view kGoalCommencement = "SHOOT THE HANGAR";
inline constexpr std::string_view kGoalBriefingLit = "SHOOT THE BRIEFING ROOM";
inline constexpr std::string_view kGoalBriefingUnlit = "SHOOT RAMPS FOR BRIEFING";
inline constexpr std::string_view kGraduationName = "GRADUATION DAY";

// TURBINE THEORY's hurry-up: the pass award starts here and falls by the
// rate for every whole second of the course's clock, down to its floor.
inline constexpr std::uint64_t kHurryUpStart = 250'000;
inline constexpr std::uint64_t kHurryUpPerSecond = 5'000;
// NAVIGATION's lit shot moves on after this long if it is not made.
inline constexpr double kNavigationHopSeconds = 8.0;
// A course carried over a drain has at least this long left.
inline constexpr double kCourseCarrySeconds = 15.0;

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
  for (const std::string_view line : {kGoalEnterHangar, kGoalExam, kGoalCommencement,
                                      kGoalBriefingLit, kGoalBriefingUnlit}) {
    if (!display::fits(line, display::kMaxGoalTextLength)) {
      return false;
    }
  }
  return display::fits(kGraduationName, display::kMaxMissionNameLength);
}
}  // namespace detail
static_assert(detail::catalogueFits(), "a rank, course or goal line does not fit its window");

}  // namespace pinball
