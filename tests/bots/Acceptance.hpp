#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "bots/TableHarness.hpp"

// The measurements behind the acceptance tests of TABLE.md section 5. Each
// runs the real game with one of the bots and returns what it counted; the
// thresholds are applied by the callers (tests/AcceptanceTest.cpp for the
// quick subset that runs with every build, tests/AcceptanceMain.cpp for the
// full statistical suite). Sample sizes are arguments, so that both use the
// same code.
namespace pinball::bots::acceptance {

using TouchCounts = std::array<int, kTouchCount>;

// --- B1: launches across the plunger's range, idle flippers ---------------------
struct LaunchResult {
  int launches = 0;
  int backOnPlunger = 0;  // Too short a press to leave the lane.
  // First touch other than a flipper, an inlane or a roll call lane, or none
  // at all in the time allowed.
  int unsafe = 0;
  TouchCounts first{};
  // The most a ball's speed rose in one step before it was in play (the
  // plunger's own push aside): a ball crossing the one-way arch from behind
  // must not be thrown by it.
  float largestKick = 0.0F;
};
// Holds from 1 to 125 steps in strides of holdStride, with the first
// `offsets` of eight start positions each.
[[nodiscard]] LaunchResult launchSweep(int holdStride, int offsets);

// Balls coming slowly down each circuit lane, as after a weak launch or a
// failed circuit. Idle flippers.
struct DescentResult {
  int descents = 0;
  int throughGapUntouched = 0;  // Drained between the flippers, never having met one.
  TouchCounts first{};
};
[[nodiscard]] DescentResult slowDescents(int stride);

// B1s: launches that drop into a roll call lane, followed to the end with
// idle flippers.
struct SkillShotResult {
  int reachedRollCall = 0;
  int throughGapUntouched = 0;
};
[[nodiscard]] SkillShotResult skillShotDrains(int holdStride, int offsets);

// A ball on the plunger given a speed straight up.
struct AutoLaunchRow {
  int speed = 0;
  int launches = 0;
  int leftInlane = 0;
  int rollCall = 0;
  int rightInlane = 0;
  int backOnPlunger = 0;
  int other = 0;
};
[[nodiscard]] AutoLaunchRow autoLaunchAt(int speed, int launches);
// The game's own auto-launch (Game::requestBall), one ball at a time into
// an empty table.
struct GameAutoLaunchResult {
  int launches = 0;
  int safeFirstTouch = 0;  // Flipper, inlane or roll call lane.
  int leftInlane = 0;
  std::uint32_t relaunches = 0;
};
[[nodiscard]] GameAutoLaunchResult gameAutoLaunch(int launches);

// Balls fired at the one-way arch from the field side.
struct OneWayResult {
  int fired = 0;
  int crossed = 0;  // Ended up, at any moment, in the launch channel.
};
[[nodiscard]] OneWayResult oneWayArch(int balls);

// --- A1, A2, B4: the shot sweep ---------------------------------------------------
struct ShotWindow {
  int best = 0;      // Longest run of consecutive press steps making the shot, any feed.
  int cradle = 0;    // The same from a cradle,
  int inlane = 0;    // from the inlane rollover,
  int rampExit = 0;  // and from a ramp's rail.
  int firstHits = 0;
  int throughGapUntouched = 0;  // Of those, then drained without meeting a flipper.
  int reachedArch = 0;          // Circuits only.
  int completed = 0;
};
struct ShotMap {
  std::array<ShotWindow, kTouchCount> shots{};
  // One line per side and feed: a character per press step.
  std::vector<std::string> fans;
};
// variants: how many jittered copies of each feed to sweep (1 is the plain
// feeds of TABLE.md 5.1; more only add first hits for B4, the windows are
// those of the plain feeds).
[[nodiscard]] ShotMap shotSweep(int variants);
[[nodiscard]] char touchChar(Touch touch) noexcept;

// --- A5, A6: lanes and ramps at a speed --------------------------------------------
struct LaneResult {
  int balls = 0;
  int completed = 0;       // Circuit: reached the far inlane. Ramp: taken.
  int rollCallOrNest = 0;  // Circuit only: fell off the arch into the lanes or bumpers.
  double meanSeconds = 0;  // Circuit: mouth to the far inlane.
  double worstSeconds = 0;
};
[[nodiscard]] LaneResult circuitAt(bool rightSide, int speed);
[[nodiscard]] LaneResult rampAt(bool rightSide, int speed);

// --- B5: exits ----------------------------------------------------------------------
struct ExitResult {
  int releases = 0;
  TouchCounts first{};
  // Reached a flipper or an inlane rollover first.
  int safe = 0;
};
[[nodiscard]] ExitResult exitOf(std::size_t hole, int releases);
// B6, the geometry half: a ball kicked up the left outlane at a speed.
[[nodiscard]] ExitResult kickbackAt(int speed, int kicks);

// --- D2: no resting place --------------------------------------------------------------
struct RestResult {
  int placed = 0;
  int notDrained = 0;          // Still on the table after 30 s.
  std::uint64_t searches = 0;  // Times the game's ball search had to fire.
  std::vector<Vec2> restingAt;
};
[[nodiscard]] RestResult restingPlaces(int balls, std::uint64_t seed);

// Two balls arriving together at the mouth of each inlane, side by side
// and nearly so, idle flippers: the place where two balls once arched across
// the lane and stayed.
struct WedgeResult {
  int pairs = 0;
  int wedged = 0;  // A ball still on the table after 12 s, stuck, or searched for.
  std::vector<Vec2> wedgedAt;
};
[[nodiscard]] WedgeResult inlaneWedge(int stride);

// --- B2, B3, C1, C2, A3, D1 (one ball): a bot plays ball after ball ------------------------
enum class BotKind : std::uint8_t { Idle, Random, Reflex };
struct SoakOptions {
  BotKind bot = BotKind::Reflex;
  int reflexError = 5;  // Steps either way; ReflexBot only.
  int balls = 100;
  std::uint64_t seed = 1;
  // Above zero: a ball that drains within this long of entering play is put
  // back on the plunger and its clock runs on, any number of times.
  double saverSeconds = 0.0;
};
struct SoakResult {
  std::vector<double> ballSeconds;
  std::array<std::uint64_t, 3> drains{};  // Centre, left outlane, right outlane.
  int under5 = 0;
  int under10 = 0;
  int timeouts = 0;  // Balls ended by the 20 minute cap.
  // The longest a ball in play went without touching a flipper, in steps
  // (D3: a ball that circulates by itself is not being played).
  std::uint64_t longestNoFlipper = 0;
  int replunges = 0;  // Plunges that did not leave the lane.
  std::uint64_t saves = 0;
  std::uint64_t steps = 0;
  std::uint64_t stuck = 0;
  std::uint64_t searches = 0;
  std::uint64_t laneIntrusions = 0;
  std::uint64_t lostBalls = 0;
  std::uint64_t overSpeed = 0;
  std::vector<Vec2> stuckAt;
  // Hits, for A3 and the switch rates.
  std::array<std::uint64_t, kTouchCount> count{};
  std::array<std::uint64_t, table::kBumpers.size()> bumperHits{};
  std::array<std::uint64_t, table::kTargets.size()> targetHits{};
  std::array<std::uint64_t, table::kDropTargets.size()> doorHits{};
  std::array<std::uint64_t, table::kRollovers.size()> rolloverHits{};
  std::array<std::uint64_t, table::kHoles.size()> holeHits{};
  std::array<std::uint64_t, table::kSlingshots.size()> slingHits{};
  std::array<std::uint64_t, 2> circuitEntered{};
  std::array<std::uint64_t, 2> circuitTop{};
  std::array<std::uint64_t, 2> circuitDone{};
  // A hash of every drain time and way out: the same seed must give the same.
  std::uint64_t fingerprint = 0;

  [[nodiscard]] double percentile(double q) const;
  [[nodiscard]] double drainShare(std::size_t way) const;  // Percent.
};
[[nodiscard]] SoakResult soak(const SoakOptions& options);

// --- D1, D4, D5 with one to three balls: the stress run -------------------------------------
struct StressOptions {
  BotKind bot = BotKind::Random;
  int reflexError = 5;
  std::uint64_t steps = 100000;
  std::uint64_t seed = 1;
  std::size_t balls = 3;  // Kept in the game at all times: 1 to 3.
};
struct StressResult {
  std::uint64_t steps = 0;
  std::uint64_t stepsWithAllFree = 0;
  std::uint64_t drains = 0;
  std::uint32_t autoLaunches = 0;
  // Launches of a ball that had come back to the plunger: one that met a
  // ball in play at the one-way arch and was knocked back down the channel.
  std::uint32_t relaunches = 0;
  // The longest any ball waited between being served and being in play.
  std::uint64_t longestInLane = 0;
  std::uint64_t stuck = 0;     // 12 units for 5 s, not cradled.
  std::uint64_t searches = 0;  // The game's ball search fired.
  std::uint64_t laneIntrusions = 0;
  std::uint64_t lostBalls = 0;
  std::uint64_t overSpeed = 0;
  std::uint64_t overlapSteps = 0;  // Steps with two free balls more than 2 units into each other.
  std::uint64_t longestOverlap = 0;
  std::uint64_t countBroken = 0;  // Steps on which balls on the table plus waiting was not `balls`.
  std::vector<Vec2> stuckAt;
  std::uint64_t fingerprint = 0;
};
[[nodiscard]] StressResult stress(const StressOptions& options);

}  // namespace pinball::bots::acceptance
