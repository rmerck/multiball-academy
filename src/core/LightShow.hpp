#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

#include "core/Rules.hpp"
#include "core/TableLayout.hpp"
#include "core/Vec2.hpp"

// The table's light show: what every lamp, flash, flasher and the general
// illumination are doing, worked out from the game's state alone. No SDL:
// unit tested in core_tests; src/platform/TableEffects.* draws it.
//
// It only reads the game. Call observe(game) after every Game::step (every
// step, not every frame: a hit inside a frame of two steps still flashes),
// then ask what to show. Everything it answers is a function of the steps
// it has observed, so a replay of the same game gives the same lights, step
// for step, and nothing it does can change the game.
//
// Photosensitivity (WCAG 2.3.1): anything that lights or darkens a large
// part of the table (the general illumination, a wash over the whole table,
// a flasher of a large radius) is a "large flash", and no two of those start
// within kMinLargeFlashSteps of each other: never more than three in any one
// second. Whether a flash is large is judged by what is drawn (a dome's
// reach, and the area of the small ones started together), and the jackpot
// ring and every show's start go through the same limiter. Hit flashes
// share gates by the part of the table they light (kMinHitSteps). With
// reduce motion on there is no strobe or flicker at all: lamps that blink
// breathe instead, hit flashes and bursts rise and fall softly, the dips of
// the lights are slow, sweeps, chases and rings are steady glows or none,
// and every flash, small or large, waits kMinLargeFadeSteps for the last.
namespace pinball {

class Game;

namespace fx {

// Blinking lamps, in steps of the game's clock (120 a second).
inline constexpr std::uint64_t kBlinkSteps = 60;      // Twice a second.
inline constexpr std::uint64_t kFastBlinkSteps = 24;  // Five times a second: hurry up.
// The shortest gap between the starts of two large flashes: 120 steps is
// one a second, well under the three of WCAG 2.3.1, leaving room on any one
// spot for the hit flashes (kMinHitSteps) and the domes (kMinDomeSteps).
inline constexpr std::uint64_t kMinLargeFlashSteps = 120;
// With reduce motion: well under once a second.
inline constexpr std::uint64_t kMinLargeFadeSteps = 150;
// Steps a ball's trail remembers.
inline constexpr std::size_t kTrailLength = 8;
// An unlit insert is never dark: it shows as coloured plastic lit from
// below at this level. Over that the quiet table's chases run: each head at
// kChaseHead with a smaller halo, its tail from kChaseTail down to the
// floor. A lamp the rules light blinks or holds, and has a star the chases
// never get.
inline constexpr float kUnlitFloor = 0.42F;
inline constexpr float kChaseHead = 0.85F;
inline constexpr float kChaseTail = 0.66F;
// The table gets busier and brighter as the game climbs (the Altitude,
// core/Progression.hpp; the show's energy is the level's lightEnergy, 0 to
// 1): at the top the unlit inserts sit kFloorRise higher, the chases run up
// to 1 + kChaseSpeedUp times as fast, two comets run where one did, more of
// the arrows run chevrons and more lights run round the rim. Steady
// brightness and running lights are not flashes, and no lamp is passed by a
// chase more than kMaxPassesPerSecond times a second at any level (under the
// three of WCAG 2.3.1); the limiter and reduce motion hold at every level.
inline constexpr float kFloorRise = 0.10F;
// In attract no lamp is darker than this (the patterns' own dark is under
// it): the title's table is never dim.
inline constexpr float kAttractFloor = 0.28F;
inline constexpr float kChaseSpeedUp = 0.8F;
inline constexpr float kMaxPassesPerSecond = 2.5F;
// A climb is eased in over this many steps (three seconds): the table
// brightens and its colour turns, never at once.
inline constexpr float kEnergyEaseSteps = 360.0F;
// Lights running round the playfield's rim, like a cabinet's GI bulbs
// chasing: kPerimeterLights at a new game, twice as many at the top. A place
// on the rim sees kPerimeterPassesPerSecond of them go by a second at every
// level.
inline constexpr int kPerimeterLights = 6;
inline constexpr float kPerimeterPassesPerSecond = 1.5F;
// The general illumination's colour follows the Altitude: day blue, clear
// white, dusk amber, sunset, night violet (the renderer's
// art::kAltitudeGi). In attract it turns through them, one a pattern.
// A struck element's flash: about 220 ms. Elements that light the same
// part of the table share a gate: a new flash starts there at most once in
// kMinHitSteps (with reduce motion, kMinLargeFadeSteps); a hit sooner lets
// the flash already lit run on.
inline constexpr std::uint32_t kHitFlashSteps = 26;
inline constexpr std::uint64_t kMinHitSteps = 90;
// A flasher dome draws glow.png out to this many times its radius: what
// decides whether it is large.
inline constexpr float kDomeReach = 1.6F;
// Small bursts started within kMinLargeFlashSteps may cover this share of
// the table between them; one more and it is treated as large.
inline constexpr float kSmallAreaShare = 0.12F;
// A dome starts a flash at most once in this many steps, whatever its size.
inline constexpr std::uint64_t kMinDomeSteps = 90;
// Bursts in flight at once (pending or lit).
inline constexpr std::size_t kMaxBursts = 24;

// The colours the light show works in; the renderer gives each its neon
// (generated/ArtPalette.hpp, kFx*).
enum class Colour : std::uint8_t { Cyan, Magenta, Lime, Orange, Yellow, Violet, White, Warm };

// Where a flasher dome sits: each over an element of the table
// (TableLayout.hpp), so it follows the element if the layout moves it.
// Table is the wash over the whole playfield; Lights is the general
// illumination itself (a dip in it).
// The first nine are revision 3's flasher domes or stand where one does
// (table::kFlasherDomes, FlasherId); the rest are lights without a dome.
enum class Flasher : std::uint8_t {
  Hangar,      // dome FlasherId::Hangar, on the Hangar's roof
  Briefing,    // dome FlasherId::Briefing
  RampLeft,    // dome FlasherId::Runway, at the Runway's mouth
  RampRight,   // dome FlasherId::Skyway
  Nest,        // dome FlasherId::Nest, above the bumpers
  Academy,     // the Graduation lamp, in the middle of the course lamps
  TopLanes,    // the middle of the roll call rollovers
  Turbine,     // dome FlasherId::Afterburner, by the lane's spinner
  Apron,       // the Shoot Again lamp
  SlingLeft,   // dome FlasherId::SlingLeft
  SlingRight,  // dome FlasherId::SlingRight
  Drone,       // the middle of the training drone's patrol
  Captive,     // the captive ball, at rest (table::kCaptiveHome)
  Table,       // the whole playfield
  Lights,      // the general illumination dips
  Count,
};
[[nodiscard]] Vec2 flasherPosition(Flasher flasher) noexcept;
// The dome a flasher fires, or FlasherId::Count for one without.
[[nodiscard]] table::FlasherId domeOf(Flasher flasher) noexcept;

// A lamp in this state at this step, 0 to 1. Blinks are square with a short
// rise and fall, as a bulb warms and cools; with reduce motion they breathe.
[[nodiscard]] float lampLevel(LampState state, std::uint64_t step, bool reduceMotion) noexcept;

// The big moments, each with its own show.
enum class Show : std::uint8_t {
  None,
  MultiballStart,  // Blackout, a sweep up the table, then three pulses.
  Graduation,      // The same in gold.
  Jackpot,         // A ring of light out from the ball and a wash.
  SuperJackpot,    // Two rings and two washes.
  Drain,           // The lights dim and flicker; the bonus counts down.
  Tilt,            // The lights flicker and go dark until the ball drains.
  GameOver,        // The lights fade down.
};

// A flasher burst as it is to be drawn now.
struct BurstView {
  Flasher where = Flasher::Nest;
  Colour colour = Colour::White;
  float level = 0.0F;   // 0 to 1, times the burst's peak already.
  float radius = 0.0F;  // Playfield units; 0 for Table and Lights.
};

// Fixed capacity, no allocation.
struct BurstViews {
  std::array<BurstView, kMaxBursts> items{};
  std::size_t count = 0;
  [[nodiscard]] std::span<const BurstView> span() const& noexcept {
    return std::span<const BurstView>(items).first(count);
  }
  std::span<const BurstView> span() const&& = delete;
};

// The trail behind one ball: where it was at the latest steps, newest first.
struct TrailView {
  std::array<Vec2, kTrailLength> points{};
  std::size_t count = 0;
  float speed = 0.0F;  // Playfield units per step over the trail.
};

// A shot arrow's chase: three chevrons running toward the shot while its
// arrow blinks.
struct ChaseView {
  bool active = false;
  // Not a shot the rules light: the table's own running chevrons, drawn
  // dimmer (more of the arrows run them the higher the game).
  bool ambient = false;
  float phase = 0.0F;   // 0 to 1, the leading chevron's place.
  bool steady = false;  // Reduce motion: all three lit, no running.
  bool fast = false;    // The arrow fast blinks (hurry up): runs twice as fast.
};

class LightShow {
 public:
  static constexpr std::size_t kLampCount = table::kLamps.size();


  // --- Settings -------------------------------------------------------------
  // The one setter for reduce motion (the OS setting or the game's own).
  void setReduceMotion(bool on) noexcept { reduceMotion_ = on; }
  [[nodiscard]] bool reduceMotion() const noexcept { return reduceMotion_; }

  // Attract mode, for the title screen while the table plays itself: the
  // lamps run cycling chases and sweeps of their own, whatever the rules
  // say, and the flashers fire now and then. Its clock is the steps
  // observed since it was turned on.
  void setAttract(bool on) noexcept;
  [[nodiscard]] bool attract() const noexcept { return attract_; }
  // Which of the attract patterns is running, 0 to kAttractPatterns - 1.
  static constexpr int kAttractPatterns = 5;
  static constexpr std::uint64_t kAttractPatternSteps = 720;  // Six seconds each.
  [[nodiscard]] int attractPattern() const noexcept;

  // Forget everything seen: for a new Game object.
  void reset() noexcept;

  // The Altitude the lights follow: -1 (the default) follows the game's
  // own; 0 to progression::kTopLevel holds that level, for the tools'
  // stills and the flash tests at the top level.
  void setAltitudeOverride(int level) noexcept;
  // The level shown, and the energy eased toward its lightEnergy (0 to 1).
  [[nodiscard]] int altitude() const noexcept { return altitude_; }
  [[nodiscard]] float energy() const noexcept { return energy_; }
  // The general illumination's colour: a place along the Altitude's
  // colours, 0 (day blue) to progression::kTopLevel (night violet), eased
  // between them.
  [[nodiscard]] float giColour() const noexcept { return giColour_; }

  // --- Feeding it -----------------------------------------------------------
  // Reads the step the game has just made. A game whose step count went
  // backwards is a new one: the show starts afresh.
  void observe(const Game& game) noexcept;

  // --- What to show ---------------------------------------------------------
  [[nodiscard]] std::uint64_t step() const noexcept { return now_; }
  // Each lamp's brightness, 0 to 1, in LampId order.
  [[nodiscard]] float lampLevel(table::LampId lamp) const noexcept;
  [[nodiscard]] const std::array<float, kLampCount>& lampLevels() const noexcept {
    return levels_;
  }
  // Whether the rules light the lamp (on, blinking): such a lamp is drawn
  // with its hot core and star; a chase's head is not.
  [[nodiscard]] bool lampLit(table::LampId lamp) const noexcept;
  // The state the rules gave each lamp at the latest step.
  [[nodiscard]] LampState lampState(table::LampId lamp) const noexcept;
  // A bay door's face lamp, 0 to 1, and its flash when struck.
  [[nodiscard]] float doorLevel(std::size_t door) const noexcept;
  [[nodiscard]] float doorFlash(std::size_t door) const noexcept;
  // Flashes on a hit, 1 at the hit and fading: turbo targets, rollovers,
  // holes (on entry and on kickout), lock bays, the kickback.
  [[nodiscard]] float targetFlash(std::size_t target) const noexcept;
  // Bumpers and slingshots, from the hits the physics reported.
  [[nodiscard]] float bumperFlash(std::size_t bumper) const noexcept;
  [[nodiscard]] float slingFlash(std::size_t sling) const noexcept;
  // Revision 3: the drone struck, a stand-up struck (kStandups order), the
  // The captive ball struck, or its switch closed.
  [[nodiscard]] float droneFlash() const noexcept;
  [[nodiscard]] float standupFlash(std::size_t standup) const noexcept;
  [[nodiscard]] float gForceFlash() const noexcept;
  [[nodiscard]] float rolloverFlash(std::size_t rollover) const noexcept;
  [[nodiscard]] float holeFlash(std::size_t hole) const noexcept;
  [[nodiscard]] float bayFlash(std::size_t bay) const noexcept;
  [[nodiscard]] float kickbackFlash() const noexcept;
  // A spinner's light (table::kSpinners; the lane's by default): brighter
  // the faster it spins, rippling with each turn (steady with reduce motion).
  [[nodiscard]] float spinnerLevel(std::size_t spinner = table::kLaneSpinner) const noexcept;
  // The general illumination, 0 (dark) to 1 (full).
  [[nodiscard]] float giLevel() const noexcept { return gi_; }
  // The arrow lamps' chases, in LampId order (the first seven lamps).
  [[nodiscard]] ChaseView chase(std::size_t arrow) const noexcept;
  // The lights running round the rim: how many, where the first is (0 to 1
  // round the rim), and how bright (0 to 1).
  struct PerimeterView {
    int count = 0;
    float phase = 0.0F;
    float level = 0.0F;
  };
  [[nodiscard]] PerimeterView perimeter() const noexcept;
  // Lamp passes a second of the fastest ambient chase now: what
  // kMaxPassesPerSecond caps. For the tests.
  [[nodiscard]] float fastestPassesPerSecond() const noexcept;
  // The running show and how far into it, in steps.
  [[nodiscard]] Show show() const noexcept { return show_; }
  [[nodiscard]] std::uint64_t showSteps() const noexcept { return now_ - showStart_; }
  // The multiball sweep: a band of light climbing the table. Negative when
  // there is none.
  [[nodiscard]] float sweepY() const noexcept { return sweepY_; }
  [[nodiscard]] Colour sweepColour() const noexcept { return sweepColour_; }
  // A jackpot's ring of light: its centre and radius, radius negative when
  // there is none.
  [[nodiscard]] Vec2 ringCentre() const noexcept { return ringCentre_; }
  [[nodiscard]] float ringRadius() const noexcept { return ringRadius_; }
  [[nodiscard]] Colour ringColour() const noexcept { return ringColour_; }
  // Every burst lit now.
  [[nodiscard]] BurstViews bursts() const noexcept;
  // A ball's trail, by slot.
  [[nodiscard]] const TrailView& trail(std::size_t slot) const noexcept;

  // --- For the tests ----------------------------------------------------------
  // How bright the large flashes are now, 0 to 1: the wash, the dips of the
  // general illumination, the large flashers. What the 3 Hz rule counts.
  [[nodiscard]] float largeFlashLevel() const noexcept;
  // Large flashes started, and those refused or dropped by the limiter.
  [[nodiscard]] std::uint32_t largeFlashes() const noexcept { return largeFlashes_; }
  [[nodiscard]] std::uint32_t largeFlashesHeld() const noexcept { return largeHeld_; }
  // Bursts started, of any size.
  [[nodiscard]] std::uint32_t burstsStarted() const noexcept { return burstsStarted_; }

  // Starts a burst (Table and Lights are always large; a dome is large if
  // its radius is kLargeRadius or more). delay: steps from now. Public so
  // that the tests can drive the limiter directly.
  // Drawn reach (radius x kDomeReach) at or past which a dome is large.
  static constexpr float kLargeRadius = 120.0F;
  void burst(Flasher where, Colour colour, float peak, float radius, std::uint32_t length,
             std::uint32_t delay = 0) noexcept;
  // Starts a show now, unless one that matters more is running.
  void startShow(Show show, Vec2 at = {}) noexcept;

 private:
  struct Burst {
    Flasher where = Flasher::Nest;
    Colour colour = Colour::White;
    bool used = false;
    bool started = false;
    bool large = false;
    float peak = 0.0F;
    float radius = 0.0F;
    std::uint64_t start = 0;  // The step it starts, or started.
    std::uint32_t length = 0;
    std::uint32_t late = 0;  // Steps it has waited for the limiter.
  };
  // A set of lamps lighting in turn.
  struct Sweep {
    std::uint64_t start = 0;
    std::uint8_t group = 0;
    bool used = false;
  };
  static constexpr std::size_t kMaxSweeps = 6;

  void readEvents(const Game& game) noexcept;
  void readSounds(const Game& game) noexcept;
  void updateBursts() noexcept;
  // The 3 Hz limiter for a large flash starting now: true if it may (and
  // it is counted); false if too soon after the last.
  bool largeOnset() noexcept;
  [[nodiscard]] std::uint32_t effectiveLength(const Burst& b) const noexcept;
  // The hit gates.
  enum : std::uint8_t {
    kGateNest,
    kGateSlingLeft,
    kGateSlingRight,
    kGateDoors,
    kGateLanesLeft,
    kGateLanesRight,
    kGateHole,  // One a hole.
    kGateBays = kGateHole + table::kHoles.size(),
    kGateKickback,
    kGateDrone,     // The drone and its armour lamps.
    kGateStandup,   // One a stand-up (T-A-X-I and the deck's A-C-E).
    kGateGForce = kGateStandup + table::kStandups.size(),
    kGateCount
  };
  [[nodiscard]] bool gateOpen(std::uint8_t gate) const noexcept;
  void strike(std::uint8_t gate, std::uint64_t& hit) noexcept;
  void updateShow(const Game& game) noexcept;
  void updateLamps(const Game& game) noexcept;
  void updateAltitude(const Game& game, std::uint64_t steps, bool first) noexcept;
  void updateTrails(const Game& game) noexcept;
  void sweepGroup(std::uint8_t group, std::uint32_t delay = 0) noexcept;
  [[nodiscard]] float burstLevel(const Burst& b) const noexcept;
  [[nodiscard]] float flash(std::uint64_t hitStep, std::uint32_t length) const noexcept;
  [[nodiscard]] float attractLevel(std::size_t lamp) const noexcept;
  [[nodiscard]] float showLevel(std::size_t lamp) const noexcept;
  [[nodiscard]] float sweepLevel(std::size_t lamp) const noexcept;
  [[nodiscard]] float ambientLevel(std::size_t lamp) const noexcept;

  bool reduceMotion_ = false;
  bool attract_ = false;
  bool seen_ = false;
  int altitudeOverride_ = -1;
  int altitude_ = 0;
  float energy_ = 0.0F;
  float giColour_ = 0.0F;
  // The ambient runs, each with its own clock in laps (0 to 1 a lap is its
  // phase), going faster the higher the game, each under its cap.
  enum Run : std::uint8_t {
    kRunArrows,
    kRunWing,
    kRunRollCall,
    kRunCourses,
    kRunLocks,
    kRunRest,
    kRunSweep,
    kRunChevrons,
    kRunCount
  };
  std::array<double, kRunCount> runs_{};
  [[nodiscard]] float runPhase(Run run) const noexcept {
    return static_cast<float>(runs_[run] - std::floor(runs_[run]));
  }
  // Steps a lap of a run takes at the energy now.
  [[nodiscard]] float runPeriod(std::size_t run) const noexcept;
  // A second comet runs on the rings (and the other way on the arrows).
  [[nodiscard]] bool twoComets() const noexcept { return energy_ >= 0.5F; }
  [[nodiscard]] int perimeterCount() const noexcept;
  double perimeterClock_ = 0.0;  // Rim lights gone round, in rims.
  std::uint64_t attractStart_ = 0;
  std::uint64_t now_ = 0;

  std::array<LampState, kLampCount> states_{};
  std::array<float, kLampCount> levels_{};
  std::array<float, table::kDropTargets.size()> doorLevels_{};
  // One more than the step of the latest hit; 0 for never.
  std::array<std::uint64_t, table::kDropTargets.size()> doorHit_{};
  std::array<std::uint64_t, table::kTargets.size()> targetHit_{};
  std::array<std::uint64_t, table::kBumpers.size()> bumperHit_{};
  std::array<std::uint64_t, table::kSlingshots.size()> slingHit_{};
  std::uint64_t droneHit_ = 0;
  std::array<std::uint64_t, table::kStandups.size()> standupHit_{};
  std::uint64_t gForceHit_ = 0;
  // The ladders as last seen, for their fills.
  std::size_t wasRank_ = 0;
  int wasCombo_ = 0;
  int wasBonus_ = 1;
  std::array<std::uint64_t, table::kRollovers.size()> rolloverHit_{};
  std::array<std::uint64_t, table::kHoles.size()> holeHit_{};
  std::array<std::uint64_t, table::kLockBays.size()> bayHit_{};
  std::uint64_t kickbackHit_ = 0;
  std::array<std::uint64_t, table::kSpinners.size()> spinHit_{};
  std::array<float, table::kSpinners.size()> spinEnergy_{};

  std::array<Burst, kMaxBursts> bursts_{};
  // One more than the step of each gate's latest flash; 0 for never.
  std::array<std::uint64_t, kGateCount> gates_{};
  // The small bursts started lately: one more than the step, and the area.
  std::array<std::pair<std::uint64_t, float>, 16> smallAreas_{};
  std::size_t smallNext_ = 0;
  // One more than the step each dome last started a flash; 0 for never.
  std::array<std::uint64_t, static_cast<std::size_t>(Flasher::Count)> domeGates_{};
  // The running drain or tilt started without its dip (limiter or reduce
  // motion): its lights fade slowly instead.
  bool quiet_ = false;
  std::array<Sweep, kMaxSweeps> sweeps_{};
  std::uint64_t lastLarge_ = 0;  // One more than the step of the latest large flash.
  std::uint32_t largeFlashes_ = 0;
  std::uint32_t largeHeld_ = 0;
  std::uint32_t burstsStarted_ = 0;

  Show show_ = Show::None;
  std::uint64_t showStart_ = 0;
  Vec2 showAt_{};
  float gi_ = 1.0F;
  float sweepY_ = -1.0F;
  Colour sweepColour_ = Colour::Magenta;
  Vec2 ringCentre_{};
  float ringRadius_ = -1.0F;
  Colour ringColour_ = Colour::Yellow;

  // What the rules said at the step before, for the edges.
  bool wasTilted_ = false;
  bool wasGameOver_ = false;
  Multiball wasMultiball_ = Multiball::None;

  std::array<TrailView, table::kMaxBalls> trails_{};
};

}  // namespace fx

using fx::LightShow;

}  // namespace pinball
