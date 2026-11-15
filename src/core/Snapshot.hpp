#pragma once

// A game in progress, as plain data: what is written to the saved-game file
// when the application leaves the foreground, and what Game::restore() puts
// back. No Box2D in here, so the parser can be fuzzed on its own.
//
// docs/RESUME.md says what is kept, what is not, and why.
//
// Everything is keyed to the element ids of core/TableLayout.hpp (a hole is
// its index in table::kHoles, a bay its index in table::kLockBays), never to
// a shape or a body. The file carries the count of every kind of element and
// a fingerprint of the geometry a saved ball depends on (layoutFingerprint):
// a file written with another table is refused, as is one written by another
// build. Bump kFormatVersion whenever a record below changes.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "core/Courses.hpp"
#include "core/Rules.hpp"
#include "core/Sha256.hpp"
#include "core/TableLayout.hpp"
#include "core/Vec2.hpp"

namespace pinball::save {

// 3: table revision 3's drone. 4: per-spinner state, the drone's knock-down
// and its look (recoil, sinking). 5: the captive ball (table revision 3
// integrated: the captive and motion branches each called their own record
// format 4; neither shipped, so one version, 5, holds both).
inline constexpr std::uint16_t kFormatVersion = 6;

// The longest a build id may be ("2.0.0+123456789" is 15).
inline constexpr std::size_t kMaxBuildIdLength = 32;

// --- The rules ----------------------------------------------------------------------
// Rules' state, field for field, minus what is only for the display of the
// moment (the message and its queue, the sounds of the step).
struct CircuitRecord {
  std::uint8_t side = 0;
  std::uint32_t steps = 0;
};

struct RulesRecord {
  std::uint64_t score = 0;
  std::array<std::uint64_t, kFeatureCount> featureScore{};
  std::int32_t ball = 1;
  bool tilted = false;
  double tiltHeat = 0.0;
  bool ballInPlay = false;
  std::uint8_t rank = 0;
  std::int32_t statusLive = 1;
  std::int32_t statusFree = 0;
  std::int32_t statusQueued = 0;
  std::int32_t statusLocked = 0;
  std::int32_t ballRequests = 0;
  std::int32_t requestsInFlight = 0;
  std::int32_t turboRequests = 0;
  std::int32_t turboRequestsInFlight = 0;
  std::uint8_t lockCommand = 0;
  bool multiballEnabled = true;
  bool ballSaveEnabled = true;

  bool saverArmable = false;
  std::uint32_t saverSteps = 0;
  std::uint32_t graceSteps = 0;
  std::int32_t extraBallsLit = 0;
  std::int32_t extraBallsHeld = 0;
  std::int32_t extraBallsAwarded = 0;

  bool skillOffered = false;
  std::uint8_t skillLane = 1;
  std::uint32_t superSkillSteps = 0;

  std::array<bool, 3> rollCallLit{};
  std::array<bool, 4> wingLit{};
  std::array<bool, table::kTargets.size()> turboTargetLit{};
  bool kickbackLit = true;
  std::uint32_t kickbackArmSteps = 0;
  std::int32_t kickbackRides = 0;
  bool extraCreditLit = false;

  std::int32_t turboMeter = 0;
  std::int32_t turboGoal = tuning::kTurboMeterFirst;

  std::array<bool, table::kDropTargets.size()> doorMarked{};
  bool doorsOpen = false;
  bool firstBank = true;
  bool doorsDown = false;
  std::uint8_t doorCommand = 0;
  std::uint32_t doorRaiseSteps = 0;
  std::int32_t droneHits = 0;  // Format 3.
  // Format 6 (concept A): altitude 0 to 4, the deck diverter, F-L-A-P-S.
  std::int32_t altitude = 0;
  bool deckLit = false;
  std::array<bool, 5> flapsLit{};
  std::int32_t hangarHolding = 0;
  std::int32_t locksHeld = 0;
  std::array<bool, Rules::kLocksNeeded> bayFull{};
  std::int32_t releasesPending = 0;

  bool turboMultiball = false;
  bool academyMultiball = false;
  std::uint8_t jackpotIndex = 0;
  std::int32_t jackpotsTowardSuper = 0;
  bool superLit = false;
  std::int32_t turboMultiballs = 0;
  std::int32_t academyMultiballs = 0;
  std::int32_t jackpotsCollected = 0;

  std::uint8_t comboShot = 0xFF;
  std::int32_t comboChain = 0;
  std::uint32_t comboSteps = 0;
  bool fiveComboMade = false;

  bool overdriveLit = false;
  std::int32_t overdrive = 1;
  std::uint32_t overdriveSteps = 0;

  std::array<CircuitRecord, table::kMaxBalls> circuits{};

  std::array<bool, kCourseCount> coursePassed{};
  std::uint8_t course = kCourseCount;
  std::uint8_t courseBlink = 0;
  bool courseLit = true;
  std::int32_t rampsNeeded = 0;
  std::int32_t coursesEnded = 0;
  std::int32_t coursesStarted = 0;
  std::int32_t coursePasses = 0;
  std::uint32_t courseSteps = 0;
  std::uint32_t courseElapsedSteps = 0;
  std::int32_t courseProgress = 0;
  bool courseSecondPart = false;
  std::uint8_t navShot = 0;
  std::uint32_t navSteps = 0;

  std::uint8_t gradStage = 0;
  bool gradLit = false;
  std::int32_t gradRampsNeeded = 0;
  std::array<bool, kShotCount> examMade{};
  std::int32_t graduations = 0;
  bool commencementThisTime = false;

  bool briefingVisited = false;
  std::int32_t combosThisBall = 0;
  std::int32_t laneSetsThisBall = 0;
  std::int32_t switchesThisBall = 0;
  std::int32_t bonusMultiplier = 1;
  BonusTally bonusTally{};

  std::uint64_t random = 0xD1B54A32D192ED03ULL;
};

// --- The table --------------------------------------------------------------------------
// One ball slot: the physics body and latches, and what Game keeps beside
// them. A slot with no ball is all defaults.
struct BallRecord {
  bool active = false;
  // Physics: the body exactly as Box2D has it, in metres, metres per
  // second, a rotation as cosine and sine, and radians per second.
  double x = 0.0;
  double y = 0.0;
  float vx = 0.0F;
  float vy = 0.0F;
  float rotationCos = 1.0F;
  float rotationSin = 0.0F;
  float spin = 0.0F;
  bool held = false;
  table::ElementRef heldBy{};
  bool sensorFromKnown = false;
  Vec2 sensorFrom{};
  std::array<bool, table::kBumpers.size()> touchingBumper{};
  std::array<bool, table::kRollovers.size()> onRollover{};
  std::array<bool, table::kHoles.size()> holeArmed{};
  // Game.
  table::ElementRef holder{};
  std::uint32_t heldSteps = 0;
  std::uint32_t deckSteps = 0;  // Format 6 (concept A): steps on the Flight Deck.
  bool wasHeld = false;
  std::uint64_t arrival = 0;
  std::uint8_t lockBay = 0;  // One more than the bay; 0 if not locked.
  bool releasing = false;
  std::uint64_t ejectStep = 0;
  Vec2 searchAnchor{};
  std::uint32_t stillSteps = 0;
  bool inPlay = false;
  Vec2 takenAt{};
  // For drawing a ball on its way into a hole and out of it.
  std::uint64_t takenStep = 0;
  Vec2 ejectFrom{};
  bool ejectFromBay = false;
  bool autoLaunch = false;
  std::uint32_t launchSteps = 0;
  bool launched = false;
};

struct TableRecord {
  std::array<float, table::kFlippers.size()> flipperAngles{};
  float plungerPull = 0.0F;
  float plungerReleaseSpeed = 0.0F;
  bool doorBankUp = true;
  bool kickbackArmed = false;
  // Format 3 (table revision 3): the drone.
  bool droneUp = true;
  std::uint32_t droneClock = 0;
  std::uint32_t droneDownSteps = 0;
  float droneX = table::kDroneLeftX;
  bool droneKnocked = false;  // Format 4.
  bool pylonUp = false;       // Format 6 (concept A).
};

// Format 5 (table revision 3): the captive ball. Its body exactly as Box2D
// has it (metres, metres per second, a rotation as cosine and sine, radians
// per second), its switch, the slot of the ball that struck it last (the
// ball the switch is reported for) and the stroke's fastest speed so far. Its joint is made afresh at the start of
// every step from the body alone (PhysicsWorld::renewCaptiveJoint), so the
// body is the joint's whole state: its translation and its speed along the
// channel follow from these, and valid() checks the ball is in its channel
// and that the switch agrees with where it is.
struct CaptiveRecord {
  double x = static_cast<double>(table::kCaptiveHome.x / 100.0F);
  double y = static_cast<double>(table::kCaptiveHome.y / 100.0F);
  float vx = 0.0F;
  float vy = 0.0F;
  float rotationCos = 1.0F;
  float rotationSin = 0.0F;
  float spin = 0.0F;
  bool switchClosed = false;
  std::uint8_t striker = 0;
  float peakSpeed = 0.0F;  // Pixels per second, this stroke's fastest so far.
};

struct GameRecord {
  std::uint64_t stepCount = 0;
  std::uint32_t drainCount = 0;
  std::uint32_t bumperHits = 0;
  std::array<std::uint32_t, table::kRollovers.size()> rolloverCounts{};
  std::array<std::uint32_t, table::kHoles.size()> holeCounts{};
  std::array<std::uint64_t, table::kHoles.size()> lastReleaseStep{};
  std::uint32_t ballSearchCount = 0;
  std::uint64_t arrivals = 0;
  std::uint32_t hangarBurst = 0;
  bool doorRaisePending = false;
  std::uint32_t lockCount = 0;
  std::uint32_t lockReleaseCount = 0;
  std::uint32_t kickbackCount = 0;
  // Format 4: one of each per spinner (table::kSpinners), and the total.
  std::array<std::int32_t, table::kSpinners.size()> spinsOwed{};
  std::array<std::int32_t, table::kSpinners.size()> spinCooldown{};
  std::array<std::uint32_t, table::kSpinners.size()> spinCounts{};
  std::array<std::int32_t, table::kSpinners.size()> spinDirection{1, 1, 1};
  std::array<std::uint32_t, table::kSpinners.size()> spinnerTicks{};
  std::uint32_t spinCount = 0;
  // Format 4: the drone as drawn (Game::drone()) and its events.
  bool droneWasUp = true;
  bool droneWasKnocked = false;
  std::uint64_t droneMoveStep = 0;
  std::uint64_t droneHitStep = 0;
  // One more than the step of the latest flipper press; 0 if none (the Air
  // Pocket rests after one).
  std::uint64_t lastFlipStep = 0;
  Vec2 droneRecoilDirection{0.0F, -1.0F};
  std::uint32_t droneHitCount = 0;
  std::uint64_t random = 0x9E3779B97F4A7C15ULL;
  std::uint8_t queuedBalls = 0;
  std::uint64_t lastAutoServeStep = 0;
  std::uint32_t autoLaunchCount = 0;
  std::uint32_t autoRelaunchCount = 0;
  std::uint32_t captiveSwitchCount = 0;  // Format 5.
};

struct Snapshot {
  RulesRecord rules;
  GameRecord game;
  TableRecord table;
  std::array<BallRecord, table::kMaxBalls> balls{};
  CaptiveRecord captive;  // Format 5.
  // The subway ride (table revision 3, 2c) needs no record of its own: a
  // ball riding it is a held ball (BallRecord holder and heldSteps).
};

// --- The file ------------------------------------------------------------------------
// "MBA-SAVE", the format version, the build id, the layout fingerprint and
// the element counts, the fixed-size payload, and a SHA-256 of everything
// before it. Little-endian throughout. Every file of a format version is the
// same size.

// This build's id: the version and the build number ("2.0.0+1").
[[nodiscard]] std::string currentBuildId();
// A hash of the table geometry that a saved ball's place and hold depend on:
// the walls, flippers, holes and their exits, bays, sensors, rollovers, the
// kickback, the plunger. Changes with any of it.
[[nodiscard]] Sha256Digest layoutFingerprint() noexcept;

// The exact size of a file of this format.
[[nodiscard]] std::size_t fileSize() noexcept;
// Most bytes ever read for one: anything larger is refused unread.
inline constexpr std::size_t kMaxFileBytes = 8192;

// The whole file, for this build.
[[nodiscard]] std::string encode(const Snapshot& snapshot);
[[nodiscard]] std::string encode(const Snapshot& snapshot, std::string_view buildId);

// Reads a file. Refused (nullopt) unless every byte is right: the size, the
// magic, the version, the build id (this build's, or the one given), the
// layout, the checksum, every field within its range and the fields
// consistent with each other (valid()). Allocates nothing from the input.
[[nodiscard]] std::optional<Snapshot> decode(std::span<const std::uint8_t> bytes);
[[nodiscard]] std::optional<Snapshot> decode(std::span<const std::uint8_t> bytes,
                                             std::string_view buildId);

// The payload alone: the records, without header or checksum. For the
// fuzzer, which cannot forge a checksum, and the tests.
[[nodiscard]] std::string encodePayload(const Snapshot& snapshot);
[[nodiscard]] std::optional<Snapshot> decodePayload(std::span<const std::uint8_t> bytes);
[[nodiscard]] std::size_t payloadSize() noexcept;

// Every field within its range and the records consistent with each other:
// what decode() checks after reading. (The records have no "game over":
// only a game in progress is ever saved, and a restored game is one.)
[[nodiscard]] bool valid(const Snapshot& snapshot);

// Field for field. (Two snapshots that encode the same are equal.)
[[nodiscard]] bool operator==(const Snapshot& a, const Snapshot& b);

// Bounds, shared with the tests.
// Countdown clocks of the rules and the game, in steps: ten minutes.
inline constexpr std::uint32_t kMaxTimerSteps = 120U * 600U;
// Counters of things done in a game (jackpots, courses, ...).
inline constexpr std::int32_t kMaxCounter = 1'000'000;
// Per ball: steps waiting on the plunger to be launched or launched again
// (Game::kAutoRelaunchSeconds, the longer of the two waits), steps without
// moving before the ball search shoves it (Game::kBallSearchSeconds), and
// steps in a hold, its wait for a clear exit included (two minutes: the
// ball search clears an exit within seconds). GameSave.cpp checks the first
// two against Game's own at compile time.
inline constexpr std::uint32_t kMaxLaunchSteps = 60U;
inline constexpr std::uint32_t kMaxStillSteps = 480U;
inline constexpr std::uint32_t kMaxHeldSteps = 120U * 120U;
// Steps in a game: far more than any game lasts, and far from overflowing.
inline constexpr std::uint64_t kMaxStepCount = 1ULL << 40U;
// Box2D's scale, and the physics' speed caps in pixels per second (PhysicsWorld checks these
// against its own at compile time; the fuzzer is built without it).
inline constexpr float kPixelsPerMeter = 100.0F;
inline constexpr float kMaxBallSpeedPixels = 3000.0F;
inline constexpr float kMaxPlungerSpeedPixels = 1250.0F;

}  // namespace pinball::save
