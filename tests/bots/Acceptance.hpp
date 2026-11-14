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
// bankDown: with the bay doors dropped (and the lock off), so that the
// Hangar itself is the shot; otherwise they stand and the doors are.
[[nodiscard]] ShotMap shotSweep(int variants, bool bankDown = false);
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
// The same for a ball let go anywhere, at any velocity (the Hangar's second
// way out, to the right flipper).
[[nodiscard]] ExitResult exitAt(Vec2 position, Vec2 velocity, int releases);

// The Briefing room's kick over the whole of its range of variation.
[[nodiscard]] ExitResult scoopKicks(int releases);

// The lock: two balls locked by the game and a third sent in, so that all
// three are let go. Each is nudged a little off its exact release, and each
// is followed to the first thing it meets, flippers idle.
struct LockBurstResult {
  int bursts = 0;
  int released = 0;      // Balls let go: three a burst.
  int safe = 0;          // First met a flipper or an inlane.
  int outOfOrder = 0;    // Bursts not in the order arriving ball, bay 1, bay 2.
  int badSpacing = 0;    // Releases not kLockReleaseSpacingSeconds after the one before.
  int heldMoved = 0;     // Steps on which a locked ball was not in its bay.
  int ballHits = 0;      // Touches between the released balls before their first touch.
  TouchCounts first{};
};
[[nodiscard]] LockBurstResult lockBursts(int bursts);

// The Briefing room with two balls: a second ball sent in while the first
// is held, at every gap. Both must come out, one at a time, each to a
// flipper or an inlane, and never onto each other.
struct ScoopPairResult {
  int pairs = 0;      // Pairs of which both balls got into the scoop.
  int turnedAway = 0; // The second ball met the first coming out and never got in.
  int bothOut = 0;
  int tooClose = 0;  // Second out less than a full hold after the first.
  int struck = 0;    // Returned balls knocked off their way by the other ball.
  int returned = 0;  // The other balls returned by the scoop,
  int safe = 0;      // and of those, the ones whose first touch was a flipper or an inlane.
  int longestOverlap = 0;  // Steps running with the two more than 2 units into each other.
};
[[nodiscard]] ScoopPairResult scoopPairs(int pairs);

// B6, the mechanism: balls coming down the left outlane, at every speed and
// across the lane, with the kickback lit. Idle flippers.
struct KickbackResult {
  int balls = 0;
  int fired = 0;
  int returned = 0;  // Set down by the kickback.
  int safe = 0;      // And then met the left inlane's switch or a flipper first.
  int lostByOutlane = 0;  // Lost down the left outlane without the kickback firing.
};
[[nodiscard]] KickbackResult kickbackSweep(int balls);

// B6, in play: a ball down the lit left outlane with the reflex bot at the
// flippers, each with its own seed, followed for three seconds after the
// kickback has set it down.
struct KickbackPlayResult {
  int kicks = 0;
  int lostAgain = 0;  // Drained within three seconds of the set-down.
};
[[nodiscard]] KickbackPlayResult kickbackUnderBot(int reflexError, int balls);

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
  // The kickback is lit at the start of every ball. Otherwise it is kept
  // out: the table before the kickback.
  bool kickback = false;
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
  // The new elements.
  std::uint64_t hangarEntries = 0;
  std::uint64_t hangarEntriesDoorsUp = 0;
  std::uint64_t bankDrops = 0;
  std::uint64_t bankRaises = 0;
  std::uint64_t spins = 0;
  std::uint64_t kickbacks = 0;
  std::uint64_t kickbackReturns = 0;
  std::uint64_t kickbackSafe = 0;
  std::uint64_t kickbackLostAgain = 0;
  std::uint64_t ejectSaves = 0;
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
  // The lock and the other new elements, as the game's own rules use them.
  std::uint64_t hangarEntries = 0;
  std::uint64_t hangarEntriesDoorsUp = 0;
  std::uint64_t bankDrops = 0;
  std::uint64_t bankRaises = 0;
  std::uint64_t locks = 0;
  std::uint64_t lockReleases = 0;
  std::uint64_t locksEmptied = 0;  // Taken out of the bays at a game's end.
  std::uint64_t lockedAstray = 0;
  std::uint64_t spins = 0;
  std::uint64_t kickbacks = 0;
  std::uint64_t kickbackReturns = 0;
  std::uint64_t kickbackSafe = 0;
  std::uint64_t kickbackLostAgain = 0;
  std::uint64_t fingerprint = 0;
};
[[nodiscard]] StressResult stress(const StressOptions& options);

}  // namespace pinball::bots::acceptance
