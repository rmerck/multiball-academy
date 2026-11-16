#include "core/LightShow.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <span>
#include <utility>

#include "core/Game.hpp"
#include "core/Physics.hpp"
#include "core/Progression.hpp"
#include "core/Sound.hpp"

namespace pinball::fx {

namespace {

using table::LampId;

constexpr float kTau = 2.0F * table::kPi;

constexpr std::size_t lampIndex(LampId id) noexcept { return static_cast<std::size_t>(id); }

constexpr Vec2 centreOf(const auto& points) noexcept {
  Vec2 sum{};
  for (const auto& p : points) {
    sum = sum + p;
  }
  return sum * (1.0F / static_cast<float>(points.size()));
}

// The lamps that light together: a sweep runs through one group in order.
struct Group {
  std::array<LampId, 8> ids{};
  std::size_t size = 0;
};
enum : std::uint8_t {
  kGroupRollCall,
  kGroupWing,
  kGroupTurbo,
  kGroupHangar,
  kGroupCourses,
  kGroupArrows,
  kGroupApron,
  kGroupDrone,
  kGroupGForce,
  kGroupTaxi,
  kGroupRank,
  kGroupCombo,
  kGroupBonus,
  // Concept A (G6).
  kGroupFlaps,
  kGroupAltitude,
  kGroupBogey,
  kGroupRadar,
  kGroupDeck,
  kGroupDroneDown,  // Down the Afterburner lane: the drone is beaten.
  kGroupLadder,     // Seven: each shot's pips, farthest first, then its arrow.
  kGroupCount = kGroupLadder + 7
};
constexpr std::array<Group, kGroupCount> kGroups{{
    {{LampId::RollCall1, LampId::RollCall2, LampId::RollCall3}, 3},
    {{LampId::WingW, LampId::WingI, LampId::WingN, LampId::WingG}, 4},
    {{LampId::TurboCharge, LampId::Overdrive, LampId::ArrowTurbine}, 3},
    {{LampId::Lock1, LampId::Lock2, LampId::ArrowHangar}, 3},
    {{LampId::Course1, LampId::Course2, LampId::Course3, LampId::Course4, LampId::Course5,
      LampId::Course6, LampId::Graduation},
     7},
    {{LampId::ArrowCircuitLeft, LampId::ArrowRunway, LampId::ArrowHangar, LampId::ArrowBriefing,
      LampId::ArrowTurbine, LampId::ArrowSkyway, LampId::ArrowCircuitRight},
     7},
    {{LampId::ShootAgain, LampId::ExtraCredit, LampId::ExtraBall}, 3},
    {{LampId::DroneArmour1, LampId::DroneArmour2, LampId::DroneArmour3, LampId::DroneArmour4,
      LampId::DroneArmour5},
     5},
    {{LampId::GForce1, LampId::GForce2, LampId::GForce3, LampId::GForce4, LampId::GForce5}, 5},
    {{LampId::TaxiT, LampId::TaxiA, LampId::TaxiX, LampId::TaxiI}, 4},
    {{LampId::Rank1, LampId::Rank2, LampId::Rank3, LampId::Rank4, LampId::Rank5, LampId::Rank6,
      LampId::Rank7},
     7},
    {{LampId::Combo2X, LampId::Combo3X, LampId::Combo4X, LampId::Combo5X}, 4},
    {{LampId::Bonus2X, LampId::Bonus3X, LampId::Bonus4X, LampId::Bonus5X}, 4},
    {{LampId::FlapsF, LampId::FlapsL, LampId::FlapsA, LampId::FlapsP, LampId::FlapsS}, 5},
    {{LampId::AltitudeFL100, LampId::AltitudeFL200, LampId::AltitudeFL300, LampId::AltitudeFL400,
      LampId::AltitudeFL500},
     5},
    {{LampId::Bogey1, LampId::Bogey2, LampId::Bogey3}, 3},
    {{LampId::Radar1, LampId::Radar2, LampId::Radar3}, 3},
    {{LampId::DeckLit, LampId::CatShot, LampId::DeckA, LampId::DeckC, LampId::DeckE}, 5},
    {{LampId::DroneArmour5, LampId::DroneArmour4, LampId::DroneArmour3, LampId::DroneArmour2,
      LampId::DroneArmour1, LampId::ArrowTurbine, LampId::LadderTurbine1, LampId::LadderTurbine2},
     8},
    {{table::ladderLamp(0, 2), table::ladderLamp(0, 1), table::ladderLamp(0, 0),
      LampId::ArrowCircuitLeft},
     4},
    {{table::ladderLamp(1, 2), table::ladderLamp(1, 1), table::ladderLamp(1, 0), LampId::ArrowRunway},
     4},
    {{table::ladderLamp(2, 2), table::ladderLamp(2, 1), table::ladderLamp(2, 0), LampId::ArrowHangar},
     4},
    {{table::ladderLamp(3, 2), table::ladderLamp(3, 1), table::ladderLamp(3, 0),
      LampId::ArrowBriefing},
     4},
    {{table::ladderLamp(4, 2), table::ladderLamp(4, 1), table::ladderLamp(4, 0), LampId::ArrowTurbine},
     4},
    {{table::ladderLamp(5, 2), table::ladderLamp(5, 1), table::ladderLamp(5, 0), LampId::ArrowSkyway},
     4},
    {{table::ladderLamp(6, 2), table::ladderLamp(6, 1), table::ladderLamp(6, 0),
      LampId::ArrowCircuitRight},
     4},
}};
// Every shot's ladder at once, a little after each other, left to right.
constexpr std::uint32_t kLadderStagger = 5;
// Each lamp of a sweep lights this many steps after the one before it.
constexpr std::uint64_t kSweepStride = 6;
constexpr std::uint64_t kSweepFinale = 60;  // Three pulses at the end.

// The end-of-ball bonus counts down through these, last lit first out.
constexpr std::array<LampId, 18> kBonusLamps{{
    LampId::WingG, LampId::WingN, LampId::WingI, LampId::WingW, LampId::RollCall3,
    LampId::RollCall2, LampId::RollCall1, LampId::TurboCharge, LampId::Overdrive,
    LampId::Graduation, LampId::Course6, LampId::Course5, LampId::Course4, LampId::Course3,
    LampId::Course2, LampId::Course1, LampId::ExtraCredit, LampId::ExtraBall,
}};
constexpr std::uint64_t kBonusStart = 10;
constexpr std::uint64_t kBonusStride = 7;
constexpr std::uint64_t kBonusHold = 30;

// Show lengths, in steps.
constexpr std::uint64_t kBlackoutSteps = 36;
constexpr std::uint64_t kSweepSteps = 84;
constexpr std::uint64_t kMultiballShowSteps = 270;
constexpr std::uint64_t kJackpotShowSteps = 90;
constexpr std::uint64_t kSuperShowSteps = 150;
constexpr std::uint64_t kDrainShowSteps = 300;
constexpr std::uint64_t kSortieShowSteps = 100;
constexpr std::uint64_t kClimbShowSteps = 240;
constexpr std::uint64_t kClimbBandSteps = 150;  // The band's climb, bottom to top.
constexpr std::uint64_t kPulseSpacing = kMinLargeFlashSteps;  // Never refused.

int priority(Show show) noexcept {
  switch (show) {
    case Show::None:
      return 0;
    case Show::Drain:
      return 1;
    case Show::Jackpot:
    case Show::SortieStart:
    case Show::Climb:
      return 2;
    case Show::MultiballStart:
      return 3;
    case Show::Graduation:
      return 4;
    case Show::SuperJackpot:
      return 5;
    case Show::Tilt:
      return 6;
    case Show::GameOver:
      return 7;
  }
  return 0;
}

std::uint64_t showLength(Show show) noexcept {
  switch (show) {
    case Show::MultiballStart:
    case Show::Graduation:
      return kMultiballShowSteps;
    case Show::Jackpot:
      return kJackpotShowSteps;
    case Show::SuperJackpot:
      return kSuperShowSteps;
    case Show::Drain:
      return kDrainShowSteps;
    case Show::SortieStart:
      return kSortieShowSteps;
    case Show::Climb:
      return kClimbShowSteps;
    case Show::None:
    case Show::Tilt:
    case Show::GameOver:
      break;
  }
  return 0;  // Until the rules let it go.
}

// The lamp an element lights when struck.
// Concept A: the deck's diverter and drain light DECK, the CAT SHOT its own.
constexpr std::array<LampId, table::kHoles.size()> kHoleLamps{
    {LampId::ArrowBriefing, LampId::ArrowHangar, LampId::ArrowRunway, LampId::ArrowSkyway,
     LampId::DeckLit, LampId::CatShot, LampId::DeckLit}};
static_assert(table::kScoopHole == 0 && table::kHangarHole == 1 && table::kRunwayHole == 2 &&
              table::kSkywayHole == 3 && table::kDeckRampHole == 4 && table::kCatHole == 5 &&
              table::kDeckDrainHole == 6);
// Every rollover has a lamp: the roll call lanes, W-I-N-G and F-L-A-P-S.
constexpr std::size_t kLitRollovers = table::kRollovers.size();
constexpr std::size_t kFirstFlap = 7;
constexpr std::array<LampId, table::kRollovers.size()> kRolloverLamps{
    {LampId::RollCall1, LampId::RollCall2, LampId::RollCall3, LampId::WingW, LampId::WingI,
     LampId::WingN, LampId::WingG, LampId::FlapsF, LampId::FlapsL, LampId::FlapsA, LampId::FlapsP,
     LampId::FlapsS}};
static_assert(table::kRollovers[kFirstFlap].group == table::RolloverGroup::Flaps);

float clamp01(float v) noexcept { return v > 0.0F ? std::min(v, 1.0F) : 0.0F; }

bool flapsAll(const Rules& rules) noexcept {
  for (std::size_t letter = 0; letter < 5; ++letter) {
    if (!rules.flapLit(letter)) {
      return false;
    }
  }
  return true;
}

bool blinking(LampState state) noexcept {
  return state == LampState::Blink || state == LampState::FastBlink;
}

// The domes, left to right and bottom to top (a jackpot's run across the
// table, an Altitude climb's run up it).
constexpr std::array<Flasher, 10> kDomesLeftToRight{
    Flasher::SlingLeft, Flasher::LeftArch, Flasher::RampLeft, Flasher::Hangar,  Flasher::Nest,
    Flasher::Turbine,   Flasher::Briefing, Flasher::Deck,     Flasher::RampRight, Flasher::SlingRight};
constexpr std::array<Flasher, 10> kDomesBottomToTop{
    Flasher::SlingLeft, Flasher::SlingRight, Flasher::RampLeft, Flasher::RampRight, Flasher::Turbine,
    Flasher::Briefing,  Flasher::LeftArch,   Flasher::Hangar,   Flasher::Deck,      Flasher::Nest};

float bell(float distance, float width) noexcept {
  const float u = distance / width;
  return std::exp(-u * u);
}

constexpr float kStepsPerSecond = 120.0F;

// The ambient runs (LightShow::Run order): steps a lap at a new game, and
// how many times one lamp of the run is passed in a lap with one comet and
// with two. The higher the game the shorter a lap, but never so short that
// a lamp is passed more than kMaxPassesPerSecond times a second.
struct RunSpeed {
  float base;
  float passesOne;
  float passesTwo;
};
constexpr std::array<RunSpeed, 12> kRunSpeeds{{
    {168.0F, 2.0F, 4.0F},  // Arrows: there and back; a second comet the other way.
    {64.0F, 1.0F, 1.0F},   // W-I-N-G (four lamps: one comet).
    {54.0F, 1.0F, 1.0F},   // Roll call (three: one comet).
    {140.0F, 1.0F, 2.0F},  // The courses and Graduation.
    {80.0F, 1.0F, 1.0F},   // The lock lamps, in turn.
    {150.0F, 1.0F, 1.0F},  // The rest, breathing.
    {360.0F, 1.0F, 1.0F},  // A band sweeping up over the rest.
    {48.0F, 1.0F, 1.0F},   // The arrows' own chevrons.
    {84.0F, 1.0F, 2.0F},   // Concept A: the shots' pips, toward their arrows (two comets high up).
    {108.0F, 1.0F, 1.0F},  // The altimeter's rungs still to climb.
    {90.0F, 1.0F, 1.0F},   // F-L-A-P-S.
    {72.0F, 1.0F, 1.0F},   // The deck's letters and CAT SHOT.
}};
static_assert(kRunSpeeds.size() == 12);

// Lamps of targets cut from the table stay dark, whatever runs
// (table::lampCut: the BOGEY drops, cut in G2).
void darkenCutLamps(std::span<float> levels) noexcept {
  for (std::size_t i = 0; i < levels.size() && i < table::kLamps.size(); ++i) {
    if (table::lampCut(table::kLamps[i].id)) {
      levels[i] = 0.0F;
    }
  }
}

}  // namespace

table::FlasherId domeOf(Flasher flasher) noexcept {
  using table::FlasherId;
  switch (flasher) {
    case Flasher::Hangar:
      return FlasherId::Hangar;
    case Flasher::Briefing:
      return FlasherId::Briefing;
    case Flasher::RampLeft:
      return FlasherId::Runway;
    case Flasher::RampRight:
      return FlasherId::Skyway;
    case Flasher::Nest:
      return FlasherId::Nest;
    case Flasher::Turbine:
      return FlasherId::Afterburner;
    case Flasher::SlingLeft:
      return FlasherId::SlingLeft;
    case Flasher::SlingRight:
      return FlasherId::SlingRight;
    case Flasher::Deck:
      return FlasherId::Deck;
    case Flasher::LeftArch:
      return FlasherId::LeftArch;
    case Flasher::Pylon:
    case Flasher::PropWash:
    case Flasher::AirPocket:
    case Flasher::Flaps:
    case Flasher::Altimeter:
    case Flasher::Academy:
    case Flasher::TopLanes:
    case Flasher::Apron:
    case Flasher::Drone:
    case Flasher::Captive:
    case Flasher::Table:
    case Flasher::Lights:
    case Flasher::Count:
      break;
  }
  return FlasherId::Count;
}

Vec2 flasherPosition(Flasher flasher) noexcept {
  const table::FlasherId dome = domeOf(flasher);
  if (dome != table::FlasherId::Count) {
    return table::kFlasherDomes[static_cast<std::size_t>(dome)];
  }
  switch (flasher) {
    case Flasher::Academy:
      return table::kLamps[lampIndex(LampId::Graduation)].position;
    case Flasher::TopLanes:
      return centreOf(std::array<Vec2, 3>{table::kRollovers[0].zone.center,
                                          table::kRollovers[1].zone.center,
                                          table::kRollovers[2].zone.center});
    case Flasher::Apron:
      return table::kLamps[lampIndex(LampId::ShootAgain)].position;
    case Flasher::Drone:
      return {(table::kDroneLeftX + table::kDroneRightX) * 0.5F, table::kDroneY};
    case Flasher::Captive:
      return table::kCaptiveHome;
    case Flasher::Pylon:
      return table::kPylon.center;
    case Flasher::PropWash:
      return table::kPropWash.center;
    case Flasher::AirPocket:
      return table::kAirPocketCenter;
    case Flasher::Flaps:
      return table::kLamps[lampIndex(LampId::FlapsA)].position;
    case Flasher::Altimeter:
      return (table::kLamps[lampIndex(LampId::AltitudeFL100)].position +
              table::kLamps[lampIndex(LampId::AltitudeFL500)].position) *
             0.5F;
    default:
      break;
  }
  return {table::kWidth * 0.5F, table::kHeight * 0.5F};
}

float lampLevel(LampState state, std::uint64_t step, bool reduceMotion) noexcept {
  const auto blink = [step, reduceMotion](std::uint64_t period, float low, float breathSteps) {
    if (reduceMotion) {
      // A slow breath between low and full: no edge at all.
      const float phase = static_cast<float>(step % static_cast<std::uint64_t>(breathSteps)) /
                          breathSteps;
      return low + (1.0F - low) * (0.5F + 0.5F * std::cos(kTau * phase));
    }
    const std::uint64_t t = step % period;
    const std::uint64_t half = period / 2;
    if (t < half) {
      // A bulb warming: full within three steps.
      return std::min(1.0F, static_cast<float>(t + 1) / 3.0F);
    }
    // And cooling, a little slower.
    return std::max(0.0F, 1.0F - static_cast<float>(t - half + 1) / 4.0F);
  };
  switch (state) {
    case LampState::On:
      return 1.0F;
    case LampState::Blink:
      return blink(kBlinkSteps, 0.45F, 180.0F);
    case LampState::FastBlink:
      return blink(kFastBlinkSteps, 0.70F, 90.0F);
    case LampState::Off:
      break;
  }
  return 0.0F;
}

void LightShow::reset() noexcept {
  // The settings stay; everything seen goes.
  const bool reduce = reduceMotion_;
  const bool attract = attract_;
  const int altitude = altitudeOverride_;
  *this = LightShow();
  reduceMotion_ = reduce;
  attract_ = attract;
  altitudeOverride_ = altitude;
}

void LightShow::setAltitudeOverride(int level) noexcept {
  altitudeOverride_ = level < 0 ? -1 : progression::clampLevel(level);
}

void LightShow::setAttract(bool on) noexcept {
  if (on && !attract_) {
    attractStart_ = now_;
  }
  attract_ = on;
}

int LightShow::attractPattern() const noexcept {
  if (!attract_) {
    return 0;
  }
  return static_cast<int>(((now_ - attractStart_) / kAttractPatternSteps) %
                          static_cast<std::uint64_t>(kAttractPatterns));
}

void LightShow::observe(const Game& game) noexcept {
  const std::uint64_t step = game.stepCount();
  if (seen_ && step < now_) {
    reset();
  }
  if (!seen_ && attract_) {
    attractStart_ = step;
  }
  if (!seen_) {
    // The first look (a new show, or one reset for a restored game) takes
    // the table as it finds it: a multiball, tilt or game over already
    // running is not an edge, and its start show is not played again.
    wasTilted_ = game.rules().tilted();
    wasGameOver_ = game.rules().gameOver();
    wasMultiball_ = game.rules().multiball();
    wasRank_ = game.rules().rank();
    wasCombo_ = game.rules().comboLevel();
    wasBonus_ = game.rules().bonusMultiplier();
    wasAltitude_ = game.rules().altitude();
    wasDeckLit_ = game.rules().deckLit();
    wasFlapsAll_ = flapsAll(game.rules());
    airPocketCatches_ = game.airPocketCatches();
  }
  const bool first = !seen_;
  const std::uint64_t steps = first ? 0 : step - now_;
  seen_ = true;
  now_ = step;
  updateAltitude(game, steps, first);
  readEvents(game);
  readSounds(game);
  readConceptA(game);
  updateShow(game);
  updateBursts();
  updateLamps(game);
  updateTrails(game);
  updateHint(game);
}

// --- Hits -------------------------------------------------------------------

bool LightShow::gateOpen(std::uint8_t gate) const noexcept {
  const std::uint64_t gap = reduceMotion_ ? kMinLargeFadeSteps : kMinHitSteps;
  // Struck in the same step as the flash that closed it: the same flash.
  return gates_[gate] == 0 || gates_[gate] == now_ + 1 || now_ + 1 - gates_[gate] >= gap;
}

void LightShow::strike(std::uint8_t gate, std::uint64_t& hit) noexcept {
  // Struck again too soon after the last flash of its group: the flash
  // already lit runs on, and no new one starts (WCAG 2.3.1).
  if (gateOpen(gate)) {
    hit = now_ + 1;
    gates_[gate] = now_ + 1;
  }
}

void LightShow::readEvents(const Game& game) noexcept {
  const std::uint64_t mark = now_ + 1;
  for (const PhysicsEvent& event : game.events()) {
    const std::size_t i = event.element.index;
    switch (event.type) {
      case PhysicsEvent::Type::Hit:
        if (event.element.kind == table::ElementKind::Bumper && i < bumperHit_.size()) {
          strike(kGateNest, bumperHit_[i]);
        } else if (event.element.kind == table::ElementKind::Slingshot && i < slingHit_.size()) {
          strike(static_cast<std::uint8_t>(kGateSlingLeft + i), slingHit_[i]);
        } else if (event.element.kind == table::ElementKind::Target && i < targetHit_.size()) {
          strike(kGateNest, targetHit_[i]);
        } else if (event.element.kind == table::ElementKind::DropTarget && i < doorHit_.size()) {
          strike(kGateDoors, doorHit_[i]);
        } else if (event.element.kind == table::ElementKind::Drone) {
          strike(kGateDrone, droneHit_);
        } else if (event.element.kind == table::ElementKind::Standup &&
                   i < standupHit_.size()) {
          strike(static_cast<std::uint8_t>(kGateStandup + i), standupHit_[i]);
        } else if (event.element.kind == table::ElementKind::Pylon) {
          // Concept A's pylon: a small flash, and the Multiball lamp beside it.
          if (gateOpen(kGatePylon)) {
            strike(kGatePylon, pylonHit_);
            burst(Flasher::Pylon, Colour::Yellow, 0.7F, 40.0F, 26);
          }
        } else if (event.element.kind == table::ElementKind::CaptiveBall) {
          // Index 0: the captive ball struck; index 1: driven up its channel
          // far enough to close the switch behind it (2a).
          if (i == 0) {
            strike(kGateGForce, gForceHit_);
          } else if (gateOpen(kGateGForce)) {
            strike(kGateGForce, gForceHit_);
            sweepGroup(kGroupGForce);  // The meter runs up its ladder.
            burst(Flasher::Captive, Colour::Lime, 0.7F, 56.0F, 28);
          }
        }
        break;
      case PhysicsEvent::Type::Enter:
        if (event.element.kind == table::ElementKind::Rollover && i < kLitRollovers &&
            i < rolloverHit_.size()) {
          // The roll call lanes light the same part of the table as the nest.
          strike(i < 3            ? kGateNest
                 : i >= kFirstFlap ? kGateFlaps
                 : i < 5           ? kGateLanesLeft
                                   : kGateLanesRight,
                 rolloverHit_[i]);
        }
        break;
      case PhysicsEvent::Type::Capture:
        if (event.element.kind == table::ElementKind::Hole && i < holeHit_.size()) {
          strike(static_cast<std::uint8_t>(kGateHole + i), holeHit_[i]);
          if (i == table::kDeckRampHole) {
            // On to the Flight Deck: its dome, and its letters run.
            burst(Flasher::Deck, Colour::Magenta, 0.85F, 60.0F, 36);
            sweepGroup(kGroupDeck);
          } else if (i == table::kCatHole) {
            // CAT SHOT: the deck's dome, then the Hangar's as the rail
            // brings the ball there.
            burst(Flasher::Deck, Colour::Orange, 0.85F, 60.0F, 36);
            sweepGroup(kGroupDeck);
            burst(Flasher::Hangar, Colour::Orange, 0.8F, 60.0F, 36,
                  static_cast<std::uint32_t>(table::kCatRideSeconds * 120.0));
          }
        } else if (event.element.kind == table::ElementKind::Kickback) {
          strike(kGateKickback, kickbackHit_);
        }
        break;
      case PhysicsEvent::Type::BallHit:
      case PhysicsEvent::Type::Cross:
        break;
    }
  }
  for (const GameEvent& event : game.gameEvents()) {
    const std::size_t i = event.index;
    switch (event.type) {
      case GameEvent::Type::HoleEject:
        if (i < holeHit_.size()) {
          strike(static_cast<std::uint8_t>(kGateHole + i), holeHit_[i]);
        }
        break;
      case GameEvent::Type::BallLocked:
      case GameEvent::Type::LockReleased:
        if (i < bayHit_.size()) {
          strike(kGateBays, bayHit_[i]);
        }
        break;
      case GameEvent::Type::KickbackFired:
      case GameEvent::Type::KickbackReturned:
        strike(kGateKickback, kickbackHit_);
        break;
      case GameEvent::Type::Spin:
        if (event.index < spinHit_.size()) {
          spinHit_[event.index] = mark;
          spinEnergy_[event.index] = std::min(1.0F, spinEnergy_[event.index] + 0.2F);
        }
        break;
      case GameEvent::Type::DoorBankDropped:
      case GameEvent::Type::DoorBankRaised:
        if (gateOpen(kGateDoors)) {
          doorHit_.fill(mark);
          gates_[kGateDoors] = mark;
        }
        break;
      case GameEvent::Type::EjectSave:
        break;
      case GameEvent::Type::DroneHit:
        if (gateOpen(kGateDrone)) {
          strike(kGateDrone, droneHit_);
          burst(Flasher::Drone, Colour::Cyan, 0.55F, 34.0F, 22);
        }
        break;
      case GameEvent::Type::DroneDown:
        // Knocked down: the armour runs up and the lane's dome fires (a
        // large flash, through the limiter).
        sweepGroup(kGroupDrone);
        burst(Flasher::Turbine, Colour::Lime, 0.9F, 110.0F, 40);
        burst(Flasher::Drone, Colour::Cyan, 0.8F, 70.0F, 36);
        // And a run of light down the lane it guarded.
        sweepGroup(kGroupDroneDown, 12);
        break;
      case GameEvent::Type::DroneDive:
      case GameEvent::Type::DroneRise:
        break;
      case GameEvent::Type::SubwayRide:
        burst(Flasher::Briefing, Colour::Yellow, 0.6F, 60.0F, 30);
        break;
    }
  }
  for (float& energy : spinEnergy_) {
    energy *= 0.985F;
  }
}

void LightShow::readSounds(const Game& game) noexcept {
  const SoundSet sounds = game.sounds();
  const auto has = [sounds](Sound sound) { return (sounds & soundBit(sound)) != 0; };
  const auto hitThisStep = [this](const auto& hits) {
    return std::ranges::any_of(hits, [this](std::uint64_t hit) { return hit == now_ + 1; });
  };
  const bool rm = reduceMotion_;

  if (has(Sound::SetComplete)) {
    // Which set: whatever was struck in this step.
    const auto topLanes = std::span(rolloverHit_).first(3);
    const auto wingLanes = std::span(rolloverHit_).subspan(3);
    if (hitThisStep(topLanes)) {
      sweepGroup(kGroupRollCall);
    }
    if (hitThisStep(wingLanes)) {
      sweepGroup(kGroupWing);
    }
    if (hitThisStep(targetHit_)) {
      sweepGroup(kGroupTurbo);
    }
    if (hitThisStep(doorHit_)) {
      sweepGroup(kGroupHangar);
    }
  }
  if (has(Sound::SkillShot)) {
    sweepGroup(kGroupRollCall);
    burst(Flasher::TopLanes, Colour::Cyan, 0.8F, 90.0F, 40);
  }
  if (has(Sound::Lit)) {
    burst(Flasher::Briefing, Colour::Yellow, 0.5F, 70.0F, 30);
  }
  if (has(Sound::Combo)) {
    sweepGroup(kGroupArrows);
  }
  if (has(Sound::CourseStart)) {
    // A sortie starts: a ring out from the Mission scoop, the sortie lamps
    // run, and every shot's ladder runs toward its arrow.
    sweepGroup(kGroupCourses);
    burst(Flasher::Briefing, Colour::Violet, 0.8F, 70.0F, 40);
    startShow(Show::SortieStart, table::kScoopCup);
    for (std::uint8_t k = 0; k < 7; ++k) {
      sweepGroup(static_cast<std::uint8_t>(kGroupLadder + k), 20 + k * kLadderStagger);
    }
  }
  if (has(Sound::CoursePassed)) {
    sweepGroup(kGroupCourses);
    sweepGroup(kGroupCourses, 70);
    burst(Flasher::Academy, Colour::Violet, 0.7F, 100.0F, 40);
  }
  if (has(Sound::RankUp)) {
    burst(Flasher::Academy, Colour::Yellow, 0.9F, 150.0F, 40, 24);
  }
  if (has(Sound::BallLocked)) {
    sweepGroup(kGroupHangar);
    burst(Flasher::Hangar, Colour::Orange, 0.9F, 110.0F, 36);
    if (!rm) {
      burst(Flasher::Hangar, Colour::Orange, 0.9F, 110.0F, 36, 45);
    }
  }
  if (has(Sound::ExtraBall)) {
    sweepGroup(kGroupApron);
    burst(Flasher::Briefing, Colour::Magenta, 0.9F, 110.0F, 40);
    burst(Flasher::Apron, Colour::Magenta, 0.8F, 80.0F, 40, 20);
  }
  if (has(Sound::ExtraCredit)) {
    burst(Flasher::Briefing, Colour::Cyan, 0.8F, 100.0F, 36);
  }
  if (has(Sound::Overdrive)) {
    sweepGroup(kGroupTurbo);
    burst(Flasher::Turbine, Colour::Yellow, 0.8F, 100.0F, 36);
  }
  if (has(Sound::BallSaved)) {
    sweepGroup(kGroupApron);
    burst(Flasher::Apron, Colour::Cyan, 0.7F, 80.0F, 36);
  }
  if (has(Sound::TiltWarning)) {
    burst(Flasher::Lights, Colour::Warm, 0.45F, 0.0F, 24);
  }
  const Vec2 at = game.ball().position;
  if (has(Sound::SuperJackpot)) {
    startShow(Show::SuperJackpot, at);
  } else if (has(Sound::Jackpot)) {
    startShow(Show::Jackpot, at);
  }
  if (has(Sound::Jackpot) || has(Sound::SuperJackpot)) {
    // Every shot's ladder runs up to its arrow, one after another.
    for (std::uint8_t k = 0; k < 7; ++k) {
      sweepGroup(static_cast<std::uint8_t>(kGroupLadder + k), k * kLadderStagger);
    }
  }

  if (has(Sound::MultiballStart)) {
    multiballKind_ = game.rules().multiball();
    startShow(game.rules().multiball() == Multiball::Graduation ? Show::Graduation
                                                                : Show::MultiballStart);
  }
  if (has(Sound::Drain)) {
    startShow(Show::Drain);
  }
}

float LightShow::flash(std::uint64_t hitStep, std::uint32_t length) const noexcept {
  if (hitStep == 0 || hitStep > now_ + 1) {
    return 0.0F;
  }
  const std::uint64_t age = now_ + 1 - hitStep;
  if (reduceMotion_) {
    // A soft glow up and down, twice as long, never full.
    const std::uint64_t soft = 2 * static_cast<std::uint64_t>(length);
    if (age >= soft) {
      return 0.0F;
    }
    constexpr std::uint64_t kRise = 12;
    const float level = age < kRise ? static_cast<float>(age + 1) / static_cast<float>(kRise)
                                    : 1.0F - static_cast<float>(age - kRise) /
                                                 static_cast<float>(soft - kRise);
    return 0.6F * level;
  }
  if (age >= length) {
    return 0.0F;
  }
  const float f = 1.0F - static_cast<float>(age) / static_cast<float>(length);
  return f * std::sqrt(f);
}

float LightShow::bumperFlash(std::size_t bumper) const noexcept {
  return bumper < bumperHit_.size() ? flash(bumperHit_[bumper], kHitFlashSteps) : 0.0F;
}
float LightShow::slingFlash(std::size_t sling) const noexcept {
  return sling < slingHit_.size() ? flash(slingHit_[sling], kHitFlashSteps) : 0.0F;
}

float LightShow::droneFlash() const noexcept { return flash(droneHit_, kHitFlashSteps); }
float LightShow::standupFlash(std::size_t standup) const noexcept {
  return standup < standupHit_.size() ? flash(standupHit_[standup], kHitFlashSteps) : 0.0F;
}
float LightShow::gForceFlash() const noexcept { return flash(gForceHit_, kHitFlashSteps); }

float LightShow::doorLevel(std::size_t door) const noexcept {
  return door < doorLevels_.size() ? doorLevels_[door] : 0.0F;
}
float LightShow::doorFlash(std::size_t door) const noexcept {
  return door < doorHit_.size() ? flash(doorHit_[door], kHitFlashSteps) : 0.0F;
}
float LightShow::targetFlash(std::size_t target) const noexcept {
  return target < targetHit_.size() ? flash(targetHit_[target], kHitFlashSteps) : 0.0F;
}
float LightShow::rolloverFlash(std::size_t rollover) const noexcept {
  return rollover < rolloverHit_.size() ? flash(rolloverHit_[rollover], kHitFlashSteps) : 0.0F;
}
float LightShow::holeFlash(std::size_t hole) const noexcept {
  return hole < holeHit_.size() ? flash(holeHit_[hole], 40) : 0.0F;
}
float LightShow::bayFlash(std::size_t bay) const noexcept {
  return bay < bayHit_.size() ? flash(bayHit_[bay], 48) : 0.0F;
}
float LightShow::kickbackFlash() const noexcept { return flash(kickbackHit_, 48); }

float LightShow::spinnerLevel(std::size_t spinner) const noexcept {
  if (spinner >= spinEnergy_.size()) {
    return 0.0F;
  }
  if (reduceMotion_) {
    return spinEnergy_[spinner];
  }
  // Brighter the faster it spins, with a ripple at each turn too small to be
  // a flash (WCAG 2.3.1: the turns come up to twenty a second).
  return spinEnergy_[spinner] * (0.85F + 0.15F * flash(spinHit_[spinner], 4));
}

float LightShow::pylonFlash() const noexcept { return flash(pylonHit_, kHitFlashSteps); }
float LightShow::airPocketFlash() const noexcept { return flash(airPocketHit_, 40); }

// --- Concept A --------------------------------------------------------------

void LightShow::readConceptA(const Game& game) noexcept {
  const Rules& rules = game.rules();
  // The Air Pocket caught a ball: a violet flash where it holds it.
  const std::uint32_t catches = game.airPocketCatches();
  if (catches > airPocketCatches_ && gateOpen(kGatePylon)) {
    strike(kGatePylon, airPocketHit_);
    burst(Flasher::AirPocket, Colour::Violet, 0.65F, 44.0F, 30);
  }
  airPocketCatches_ = catches;
  // F-L-A-P-S complete: the letters run twice and the Mission's ladder (it
  // lights the Mission) runs to its arrow.
  const bool all = flapsAll(rules);
  if (all && !wasFlapsAll_ && !rules.gameOver()) {
    sweepGroup(kGroupFlaps);
    sweepGroup(kGroupFlaps, 50);
    burst(Flasher::Flaps, Colour::White, 0.7F, 56.0F, 32);
    sweepGroup(static_cast<std::uint8_t>(kGroupLadder + std::to_underlying(LampId::ArrowBriefing)),
               30);
  }
  wasFlapsAll_ = all;
  // DECK lit: the Skyway's next ball goes to the Flight Deck.
  const bool deckLit = rules.deckLit() && !rules.gameOver();
  if (deckLit && !wasDeckLit_) {
    burst(Flasher::Deck, Colour::Magenta, 0.75F, 50.0F, 30);
    sweepGroup(kGroupDeck);
    sweepGroup(static_cast<std::uint8_t>(kGroupLadder + std::to_underlying(LampId::ArrowSkyway)));
  }
  wasDeckLit_ = deckLit;
  // Steady lights, eased so that they are never a flash: the deck glows
  // while a ball is on it; the Prop Wash glows with its spin at the level
  // shown.
  bool onDeck = false;
  for (const BallView& ball : game.balls(1.0F)) {
    onDeck = onDeck || (ball.visible && !ball.locked && table::onDeck(ball.position));
  }
  const float ease = reduceMotion_ ? 1.0F / 90.0F : 1.0F / 30.0F;
  const float deckTarget = onDeck ? 1.0F : 0.0F;
  deck_ = deck_ < deckTarget ? std::min(deckTarget, deck_ + ease) : std::max(deckTarget, deck_ - ease);
  const float spin = progression::knobs(altitude_).propWashSpin / 9.0F;
  const float washTarget = rules.gameOver() && !attract_ ? 0.0F : clamp01(attract_ ? 1.0F : spin);
  propWash_ = propWash_ < washTarget ? std::min(washTarget, propWash_ + ease)
                                     : std::max(washTarget, propWash_ - ease);
}

float LightShow::targetEnergy(const Game& game) const noexcept {
  // Busier in a multiball, the busiest in the finale; in attract the table
  // shows off.
  if (attract_) {
    return 1.0F;
  }
  const float level = progression::knobs(altitude_).lightEnergy;
  switch (game.rules().multiball()) {
    case Multiball::Graduation:
      return 1.0F;
    case Multiball::None:
      return level;
    case Multiball::Turbo:
    case Multiball::Academy:
    case Multiball::Dogfight:  // Concept A's G5 multiball: as busy as the others.
      break;
  }
  return std::min(1.0F, level + 0.35F);
}

void LightShow::updateHint(const Game& game) noexcept {
  // A ball at rest (cradled on a flipper) for kHintSteps: the lights point
  // at the shot to make. Every ball on the table at rest, none moving.
  const Rules& rules = game.rules();
  bool resting = !attract_ && rules.ballInPlay() && !rules.tilted() && !rules.gameOver();
  bool any = false;
  for (const TrailView& trail : trails_) {
    if (trail.count == 0) {
      continue;
    }
    any = true;
    resting = resting && trail.count == kTrailLength && trail.speed < 0.5F;
  }
  if (!resting || !any) {
    restSince_ = 0;
    hint_ = -1;
    return;
  }
  if (restSince_ == 0) {
    restSince_ = now_ + 1;
  }
  if (now_ + 1 - restSince_ < kHintSteps) {
    hint_ = -1;
    return;
  }
  // The shot that matters most: a hurry-up (fast blink), else a lit shot,
  // else the Afterburner, which always does something.
  int best = -1;
  for (const LampState wanted : {LampState::FastBlink, LampState::Blink}) {
    for (std::size_t a = 0; a < 7 && best < 0; ++a) {
      if (states_[a] == wanted) {
        best = static_cast<int>(a);
      }
    }
  }
  hint_ = best >= 0 ? best : static_cast<int>(std::to_underlying(LampId::ArrowTurbine));
}

float LightShow::pipChase(std::size_t lamp) const noexcept {
  // The pips run up to the arrow, a pip every 12 steps, then the arrow's
  // own beat: one lap in 48 steps, so each pip is passed 2.5 times a second.
  const auto id = static_cast<LampId>(lamp);
  if (!table::isPipLamp(id)) {
    return 0.0F;
  }
  const std::size_t n = lamp - lampIndex(LampId::LadderCircuitLeft1);
  const std::size_t arrow = n / table::kShotLadderPips;
  const std::size_t k = n % table::kShotLadderPips;
  const bool driven = blinking(states_[arrow]) || hint_ == static_cast<int>(arrow);
  if (!driven) {
    return 0.0F;
  }
  if (reduceMotion_) {
    return 0.7F;
  }
  constexpr std::uint64_t kLap = 48;
  constexpr std::uint64_t kPerPlace = kLap / (table::kShotLadderPips + 1);
  const float head = static_cast<float>(now_ % kLap) / static_cast<float>(kPerPlace);
  const auto place = static_cast<float>(table::kShotLadderPips - 1 - k);
  return clamp01(1.0F - std::fabs(head - place) * 0.8F);
}

// --- Sweeps -----------------------------------------------------------------

void LightShow::sweepGroup(std::uint8_t group, std::uint32_t delay) noexcept {
  Sweep* slot = &sweeps_[0];
  for (Sweep& sweep : sweeps_) {
    if (!sweep.used) {
      slot = &sweep;
      break;
    }
    if (sweep.start < slot->start) {
      slot = &sweep;  // The oldest makes way.
    }
  }
  *slot = Sweep{.start = now_ + delay, .group = group, .used = true};
}

float LightShow::sweepLevel(std::size_t lamp) const noexcept {
  float level = 0.0F;
  for (const Sweep& sweep : sweeps_) {
    if (!sweep.used || sweep.start > now_) {
      continue;
    }
    const Group& group = kGroups[sweep.group];
    const std::uint64_t length = group.size * kSweepStride + kSweepStride + kSweepFinale;
    const std::uint64_t t = now_ - sweep.start;
    if (t >= length) {
      continue;
    }
    for (std::size_t k = 0; k < group.size; ++k) {
      if (lampIndex(group.ids[k]) != lamp) {
        continue;
      }
      if (reduceMotion_) {
        // The group glows up and down together.
        level = std::max(level, std::sin(table::kPi * static_cast<float>(t) /
                                         static_cast<float>(length)));
        continue;
      }
      const std::uint64_t on = k * kSweepStride;
      const std::uint64_t finale = group.size * kSweepStride + kSweepStride;
      if (t >= on && t < on + 2 * kSweepStride + 2) {
        level = std::max(level, 1.0F - static_cast<float>(t - on) / 16.0F);
      } else if (t >= finale && ((t - finale) / 10) % 2 == 0) {
        level = 1.0F;
      }
    }
  }
  return level;
}

// --- Bursts and the limiter -------------------------------------------------

void LightShow::burst(Flasher where, Colour colour, float peak, float radius,
                      std::uint32_t length, std::uint32_t delay) noexcept {
  Burst* slot = nullptr;
  for (Burst& b : bursts_) {
    if (!b.used) {
      slot = &b;
      break;
    }
  }
  if (slot == nullptr) {
    // Full: a burst still waiting makes way, the one asked for last. One
    // that is lit is never cut off (that would be a flash of its own); with
    // none waiting the new one is not started.
    for (Burst& b : bursts_) {
      if (!b.started && (slot == nullptr || b.start > slot->start)) {
        slot = &b;
      }
    }
    if (slot == nullptr) {
      return;
    }
  }
  *slot = Burst{.where = where,
                .colour = colour,
                .used = true,
                .started = false,
                .large = false,
                .peak = clamp01(peak),
                .radius = radius,
                .start = now_ + delay,
                .length = std::max<std::uint32_t>(length, 4),
                .late = 0};
}

void LightShow::updateBursts() noexcept {
  const std::uint64_t gap = reduceMotion_ ? kMinLargeFadeSteps : kMinLargeFlashSteps;
  // Pending bursts in the order they asked to start.
  for (;;) {
    Burst* next = nullptr;
    for (Burst& b : bursts_) {
      if (b.used && !b.started && b.start <= now_ && (next == nullptr || b.start < next->start)) {
        next = &b;
      }
    }
    if (next == nullptr) {
      break;
    }
    Burst& b = *next;
    // Large by what is drawn: a dome lights glow.png out to kDomeReach of
    // its radius. A small one is large too when the small ones started in
    // the last kMinLargeFlashSteps would cover more than kSmallAreaShare of
    // the table between them. With reduce motion every burst is large.
    const float reach = b.radius * kDomeReach;
    const float area = table::kPi * reach * reach;
    float recent = 0.0F;
    for (const auto& [at, a] : smallAreas_) {
      recent += at != 0 && now_ + 1 - at < kMinLargeFlashSteps ? a : 0.0F;
    }
    b.large = reduceMotion_ || b.where == Flasher::Table || b.where == Flasher::Lights ||
              reach >= kLargeRadius ||
              recent + area > kSmallAreaShare * table::kWidth * table::kHeight;
    // Each dome has a gate of its own, as the hit flashes have: one place
    // starts a flash at most once in kMinDomeSteps, large or small, so the
    // small-area budget and the large limiter together cannot make it flash
    // more than three times a second. Domes lit in the same step pass
    // together.
    const auto place = static_cast<std::size_t>(b.where);
    if (place < domeGates_.size() && b.where != Flasher::Table && b.where != Flasher::Lights) {
      const std::uint64_t gate = domeGates_[place];
      const std::uint64_t domeGap = reduceMotion_ ? kMinLargeFadeSteps : kMinDomeSteps;
      if (gate != 0 && gate != now_ + 1 && now_ + 1 - gate < domeGap) {
        ++b.late;
        if (b.late > 60) {
          b.used = false;
        } else {
          b.start = now_ + 1;
        }
        continue;
      }
    }
    if (b.large) {
      const bool sameFlash = lastLarge_ == now_ + 1;  // Lit together: one flash.
      if (!sameFlash && lastLarge_ != 0 && now_ + 1 - lastLarge_ < gap) {
        // Too soon after the last: it waits, and gives up after half a
        // second. It is started (or dropped) at a later step.
        if (b.late == 0) {
          ++largeHeld_;
        }
        ++b.late;
        if (b.late > 60) {
          b.used = false;
        } else {
          b.start = now_ + 1;
        }
        continue;
      }
      if (!sameFlash) {
        ++largeFlashes_;
        lastLarge_ = now_ + 1;
      }
    }
    if (place < domeGates_.size() && b.where != Flasher::Table && b.where != Flasher::Lights) {
      domeGates_[place] = now_ + 1;
    }
    if (!b.large) {
      smallAreas_[smallNext_] = {now_ + 1, area};
      smallNext_ = (smallNext_ + 1) % smallAreas_.size();
    }
    b.started = true;
    b.start = now_;
    ++burstsStarted_;
  }
  for (Burst& b : bursts_) {
    if (b.used && b.started && now_ >= b.start + effectiveLength(b)) {
      b.used = false;
    }
  }
}

float LightShow::burstLevel(const Burst& b) const noexcept {
  if (!b.used || !b.started || now_ < b.start) {
    return 0.0F;
  }
  // Shaped at the time it is drawn, so a change of reduce motion reshapes
  // what is already queued or lit.
  const auto age = static_cast<float>(now_ - b.start);
  const float attack = reduceMotion_ ? 24.0F : 3.0F;
  const auto length = static_cast<float>(effectiveLength(b));
  const float peak = reduceMotion_ ? b.peak * 0.6F : b.peak;
  if (age >= length) {
    return 0.0F;
  }
  if (age < attack) {
    return peak * (age + 1.0F) / attack;
  }
  const float fall = 1.0F - (age - attack) / std::max(length - attack, 1.0F);
  return peak * fall * std::sqrt(fall);
}

std::uint32_t LightShow::effectiveLength(const Burst& b) const noexcept {
  if (!reduceMotion_) {
    return b.length;
  }
  // Slow: the dips of the lights slowest of all.
  return b.where == Flasher::Lights ? std::max<std::uint32_t>(b.length * 3, 180)
                                    : std::max<std::uint32_t>(b.length * 2, 90);
}

bool LightShow::largeOnset() noexcept {
  const std::uint64_t gap = reduceMotion_ ? kMinLargeFadeSteps : kMinLargeFlashSteps;
  if (lastLarge_ == now_ + 1) {
    return true;  // The same flash.
  }
  if (lastLarge_ != 0 && now_ + 1 - lastLarge_ < gap) {
    ++largeHeld_;
    return false;
  }
  ++largeFlashes_;
  lastLarge_ = now_ + 1;
  return true;
}

BurstViews LightShow::bursts() const noexcept {
  BurstViews views;
  for (const Burst& b : bursts_) {
    const float level = burstLevel(b);
    if (level > 0.0F && b.where != Flasher::Lights) {
      views.items[views.count++] =
          BurstView{.where = b.where, .colour = b.colour, .level = level, .radius = b.radius};
    }
  }
  return views;
}

float LightShow::largeFlashLevel() const noexcept {
  float level = 1.0F - gi_;
  for (const Burst& b : bursts_) {
    if (b.large && b.where != Flasher::Lights) {
      level = std::max(level, burstLevel(b));
    }
  }
  return clamp01(level);
}

// --- Shows ------------------------------------------------------------------

void LightShow::startShow(Show show, Vec2 at) noexcept {
  const std::uint64_t length = showLength(show_);
  const bool running = show_ != Show::None && (length == 0 || now_ - showStart_ < length);
  if (running && priority(show) < priority(show_)) {
    return;
  }
  if (running && show == show_ && (show == Show::MultiballStart || show == Show::Graduation)) {
    return;  // Told twice (the cue and the rules' own edge): once is enough.
  }
  if (show == Show::Jackpot || show == Show::SuperJackpot || show == Show::SortieStart) {
    // The ring and the wash are a large flash: through the limiter. Too
    // soon after the last, the jackpot is not shown again (the one running
    // runs on).
    if (!largeOnset()) {
      return;
    }
  }
  // The drain's and the tilt's fall of the lights is a large change too:
  // too soon after the last large flash (or with reduce motion) the show
  // still runs, its lights only fading slowly, with no dip.
  quiet_ = false;
  if (show == Show::Drain || show == Show::Tilt || show == Show::Climb) {
    quiet_ = reduceMotion_ || !largeOnset();
  }
  show_ = show;
  showStart_ = now_;
  showAt_ = at;
  const bool rm = reduceMotion_;
  switch (show) {
    case Show::MultiballStart:
    case Show::Graduation: {
      const bool gold = show == Show::Graduation;
      // The blackout is the dip of the lights, lifting as the sweep climbs.
      burst(Flasher::Lights, Colour::Warm, 0.85F, 0.0F,
            static_cast<std::uint32_t>(kBlackoutSteps + kSweepSteps));
      // Each multiball its own colours: the lock's (Squadron) magenta and
      // orange, the quick one's cyan and lime, the finale's gold.
      std::array<Colour, 3> pulses{Colour::Magenta, Colour::Orange, Colour::Yellow};
      if (gold) {
        pulses = {Colour::Yellow, Colour::White, Colour::Yellow};
      } else if (multiballKind_ == Multiball::Turbo) {
        pulses = {Colour::Cyan, Colour::Lime, Colour::Cyan};
      }
      const auto first = static_cast<std::uint32_t>(kBlackoutSteps + kSweepSteps + 6);
      for (std::size_t k = 0; k < (rm ? 1U : pulses.size()); ++k) {
        const auto delay = first + static_cast<std::uint32_t>(k * kPulseSpacing);
        for (const Flasher dome : {Flasher::Hangar, Flasher::Briefing, Flasher::RampLeft,
                                   Flasher::RampRight, Flasher::Deck, Flasher::LeftArch}) {
          burst(dome, pulses[k], 0.9F, 130.0F, 36, delay);
        }
      }
      sweepColour_ = gold ? Colour::Yellow : pulses[0];
      // Every shot's ladder runs as the sweep passes it.
      for (std::uint8_t k = 0; k < 7; ++k) {
        sweepGroup(static_cast<std::uint8_t>(kGroupLadder + k),
                   static_cast<std::uint32_t>(kBlackoutSteps + kSweepSteps / 2) + k * kLadderStagger);
      }
      break;
    }
    case Show::Jackpot:
      ringColour_ = Colour::Yellow;
      // The ring, and the domes left to right behind it, small: each part
      // of the table lights once as the ring passes, and a dome's own glow
      // is a small flash of its own place (the small-area budget and each
      // dome's gate hold them).
      if (!rm) {
        for (std::size_t k = 0; k < kDomesLeftToRight.size(); ++k) {
          burst(kDomesLeftToRight[k], Colour::Yellow, 0.75F, 28.0F, 24,
                static_cast<std::uint32_t>(4 + 5 * k));
        }
      }
      break;
    case Show::SortieStart:
      ringColour_ = Colour::Violet;
      break;
    case Show::Climb:
      // Up the table: the band, the altimeter run twice, the domes bottom to
      // top in the new level's colour, small.
      sweepColour_ = Colour::Cyan;
      sweepGroup(kGroupAltitude);
      sweepGroup(kGroupAltitude, 60);
      if (!rm) {
        for (std::size_t k = 0; k < kDomesBottomToTop.size(); ++k) {
          burst(kDomesBottomToTop[k], k % 2 == 0 ? Colour::Cyan : Colour::Violet, 0.75F, 30.0F,
                26, static_cast<std::uint32_t>(10 + 12 * k));
        }
      }
      break;
    case Show::SuperJackpot:
      // No ring: a wash and every dome at once, one flash; a white wash
      // the limiter's gap later, the second.
      burst(Flasher::Table, Colour::Magenta, 0.45F, 0.0F, 40);
      for (const Flasher dome :
           {Flasher::Hangar, Flasher::Briefing, Flasher::RampLeft, Flasher::RampRight}) {
        burst(dome, Colour::Magenta, 0.9F, 140.0F, 40);
      }
      if (!rm) {
        burst(Flasher::Table, Colour::White, 0.40F, 0.0F, 40,
              static_cast<std::uint32_t>(kPulseSpacing));
      }
      break;
    case Show::Drain:
      if (!quiet_) {
        burst(Flasher::Lights, Colour::Warm, 0.55F, 0.0F, 40);
        burst(Flasher::Lights, Colour::Warm, 0.45F, 0.0F, 40,
              static_cast<std::uint32_t>(kPulseSpacing));
      }
      break;
    case Show::Tilt:
      if (!quiet_) {
        burst(Flasher::Lights, Colour::Warm, 0.8F, 0.0F, 30);
        burst(Flasher::Lights, Colour::Warm, 0.8F, 0.0F, 30,
              static_cast<std::uint32_t>(kPulseSpacing));
      }
      break;
    case Show::GameOver:
    case Show::None:
      break;
  }
}

void LightShow::updateShow(const Game& game) noexcept {
  const Rules& rules = game.rules();
  // The edges the sounds do not mark.
  if (rules.tilted() && !wasTilted_) {
    startShow(Show::Tilt);
  }
  if (rules.gameOver() && !wasGameOver_) {
    startShow(Show::GameOver);
  }
  if (rules.multiball() == Multiball::Graduation && wasMultiball_ != Multiball::Graduation) {
    multiballKind_ = Multiball::Graduation;
    startShow(Show::Graduation);
  }
  if (rules.altitude() > wasAltitude_ && !rules.gameOver()) {
    startShow(Show::Climb);
  }
  wasAltitude_ = rules.altitude();
  wasTilted_ = rules.tilted();
  wasGameOver_ = rules.gameOver();
  wasMultiball_ = rules.multiball();

  const std::uint64_t t = now_ - showStart_;
  const std::uint64_t length = showLength(show_);
  if ((length != 0 && t >= length) || (show_ == Show::Tilt && !rules.tilted()) ||
      (show_ == Show::GameOver && !rules.gameOver())) {
    show_ = Show::None;
  }
  const bool rm = reduceMotion_;

  float gi = 1.0F;
  sweepY_ = -1.0F;
  ringRadius_ = -1.0F;
  switch (show_) {
    case Show::MultiballStart:
    case Show::Graduation:
      if (!rm && t >= kBlackoutSteps && t < kBlackoutSteps + kSweepSteps) {
        const float u = static_cast<float>(t - kBlackoutSteps) / static_cast<float>(kSweepSteps);
        sweepY_ = table::kHeight - u * (table::kHeight - 80.0F);
      }
      break;
    case Show::SuperJackpot:
      break;
    case Show::Jackpot:
      if (!rm && t < 70) {
        ringCentre_ = showAt_;
        ringRadius_ = static_cast<float>(t) * 11.0F;
      }
      break;
    case Show::SortieStart:
      // Slower than a jackpot's: out from the scoop over the whole table.
      if (!rm && t < 90) {
        ringCentre_ = showAt_;
        ringRadius_ = static_cast<float>(t) * 9.0F;
      }
      break;
    case Show::Climb:
      if (!rm && !quiet_ && t < kClimbBandSteps) {
        const float u = static_cast<float>(t) / static_cast<float>(kClimbBandSteps);
        sweepY_ = table::kHeight - u * (table::kHeight - 80.0F);
      }
      break;
    case Show::Drain: {
      // Down to half for the count, back up at the end; slowly when quiet.
      const float fall = quiet_ ? 150.0F : 30.0F;
      const float down = std::min(1.0F, static_cast<float>(t) / fall);
      const float up = t > 240 ? static_cast<float>(t - 240) / 60.0F : 0.0F;
      gi = 1.0F - 0.5F * down * (1.0F - clamp01(up));
      break;
    }
    case Show::Tilt: {
      const float fall = quiet_ ? 150.0F : 30.0F;
      gi = 0.3F + 0.7F * (1.0F - std::min(1.0F, static_cast<float>(t) / fall));
      break;
    }
    case Show::GameOver:
      gi = 1.0F - 0.45F * std::min(1.0F, static_cast<float>(t) / 120.0F);
      break;
    case Show::None:
      break;
  }
  // The dips.
  for (const Burst& b : bursts_) {
    if (b.where == Flasher::Lights) {
      gi *= 1.0F - burstLevel(b);
    }
  }
  if (attract_) {
    gi = 1.0F;
  }
  gi_ = clamp01(gi);
}

float LightShow::showLevel(std::size_t lamp) const noexcept {
  const std::uint64_t t = now_ - showStart_;
  const Vec2 at = table::kLamps[lamp].position;
  switch (show_) {
    case Show::MultiballStart:
    case Show::Graduation: {
      if (reduceMotion_) {
        if (t >= kBlackoutSteps && t < kBlackoutSteps + 2 * kSweepSteps) {
          return std::sin(table::kPi * static_cast<float>(t - kBlackoutSteps) /
                          static_cast<float>(2 * kSweepSteps));
        }
        return 0.0F;
      }
      float level = sweepY_ >= 0.0F ? bell(at.y - sweepY_, 70.0F) : 0.0F;
      // Every lamp with the pulses.
      for (const Burst& b : bursts_) {
        if (b.large && b.where == Flasher::Hangar) {
          level = std::max(level, burstLevel(b) / std::max(b.peak, 0.01F));
        }
      }
      return level;
    }
    case Show::Climb:
      return sweepY_ >= 0.0F ? 0.9F * bell(at.y - sweepY_, 60.0F) : 0.0F;
    case Show::Jackpot:
    case Show::SuperJackpot:
    case Show::SortieStart:
      if (ringRadius_ >= 0.0F) {
        const Vec2 d = at - ringCentre_;
        return bell(std::sqrt(d.x * d.x + d.y * d.y) - ringRadius_, 45.0F);
      }
      return 0.0F;
    case Show::Drain:
      for (std::size_t k = 0; k < kBonusLamps.size(); ++k) {
        if (lampIndex(kBonusLamps[k]) == lamp) {
          const std::uint64_t out = kBonusStart + kBonusHold + k * kBonusStride;
          if (t >= kBonusStart && t < out) {
            return 1.0F;
          }
          if (t >= out && t < out + 6) {
            return 1.0F - static_cast<float>(t - out) / 6.0F;
          }
        }
      }
      return 0.0F;
    case Show::Tilt:
    case Show::GameOver:
    case Show::None:
      break;
  }
  return 0.0F;
}

// --- Attract ----------------------------------------------------------------

float LightShow::attractLevel(std::size_t lamp) const noexcept {
  const std::uint64_t ta = now_ - attractStart_;
  const std::uint64_t tp = ta % kAttractPatternSteps;
  const Vec2 at = table::kLamps[lamp].position;
  if (reduceMotion_) {
    // One slow wave across the table, ten seconds a crossing.
    const float phase = static_cast<float>(ta % 1200) / 1200.0F + at.x / table::kWidth;
    return 0.3F + 0.45F * (0.5F + 0.5F * std::cos(kTau * phase));
  }
  const auto id = static_cast<LampId>(lamp);
  switch (attractPattern()) {
    case 0: {  // Comets along the arrows; the rest turn slowly.
      if (table::isArrowLamp(id)) {
        const float head = static_cast<float>(tp % 84) / 12.0F;
        return clamp01(1.0F - std::fabs(head - static_cast<float>(lamp)) * 0.6F);
      }
      const std::uint64_t k = (tp / 15 + lamp) % 6;
      return k == 0 ? 1.0F : 0.12F;
    }
    case 1: {  // Sweeps up the table.
      // Over the lamps' own rows, 940 up to 140.
      const float y = 940.0F - static_cast<float>(tp % 120) / 120.0F * 800.0F;
      return std::max(0.1F, bell(at.y - y, 90.0F));
    }
    case 2: {  // The course lamps go round; lanes alternate.
      if (table::isCourseLamp(id)) {
        const std::size_t k = lamp - lampIndex(LampId::Course1);
        const std::size_t head = static_cast<std::size_t>(tp / 8) % 7;
        return k == head ? 1.0F : (k + 1) % 7 == head ? 0.4F : 0.08F;
      }
      return ((tp / 30) + lamp) % 2 == 0 ? 0.9F : 0.1F;
    }
    case 3:  // Left and right halves in turn, once a second.
      return ((tp / 60) % 2 == 0) == (at.x < table::kWidth * 0.5F) ? 1.0F : 0.08F;
    case 5: {  // Pre-flight: every shot's pips run up to its arrow, the
               // altimeter climbs, F-L-A-P-S go round; the rest glow.
      if (table::isPipLamp(id) || table::isArrowLamp(id)) {
        const bool arrow = table::isArrowLamp(id);
        const std::size_t n = arrow ? 0 : lamp - lampIndex(LampId::LadderCircuitLeft1);
        const float place =
            arrow ? 3.0F : static_cast<float>(table::kShotLadderPips - 1 - n % table::kShotLadderPips);
        const float head = static_cast<float>(tp % 48) / 48.0F * 5.0F;
        return std::max(0.1F, 1.0F - std::fabs(head - place) * 0.8F);
      }
      if (id >= LampId::AltitudeFL100 && id <= LampId::AltitudeFL500) {
        const float head = static_cast<float>(tp % 60) / 60.0F * 7.0F;
        return std::max(0.1F, 1.0F - std::fabs(head - static_cast<float>(lamp - lampIndex(
                                                                    LampId::AltitudeFL100))) *
                                         0.8F);
      }
      if (id >= LampId::FlapsF && id <= LampId::FlapsS) {
        const std::size_t head = static_cast<std::size_t>(tp / 12) % 5;
        return lamp - lampIndex(LampId::FlapsF) == head ? 1.0F : 0.15F;
      }
      return 0.35F;
    }
    default: {  // Rings out of the bumper nest.
      const Vec2 d = at - flasherPosition(Flasher::Nest);
      const float r = static_cast<float>(tp % 90) * 6.5F;
      return std::max(0.08F, bell(std::sqrt(d.x * d.x + d.y * d.y) - r, 70.0F));
    }
  }
}

// --- Lamps ------------------------------------------------------------------

void LightShow::updateAltitude(const Game& game, std::uint64_t steps, bool first) noexcept {
  // The level the rules have reached, or the one a tool holds. In attract
  // the table shows off: the energy of the top level, and the general
  // illumination turning through the Altitude's colours, one a pattern.
  const int level = altitudeOverride_ >= 0 ? altitudeOverride_ : game.rules().altitude();
  altitude_ = progression::clampLevel(level);
  const float target = targetEnergy(game);
  const float colour = attract_ ? static_cast<float>(attractPattern() % progression::kLevels)
                                : static_cast<float>(altitude_);
  if (first || altitudeOverride_ >= 0) {
    // As found: a look at any step shows the lights as the level has them
    // (the one look a still takes included).
    energy_ = target;
    giColour_ = colour;
  } else {
    const float ease = static_cast<float>(steps) / kEnergyEaseSteps;
    energy_ = energy_ < target ? std::min(target, energy_ + ease) : std::max(target, energy_ - ease);
    // Through the colours between, a level's worth every kEnergyEaseSteps.
    giColour_ = giColour_ < colour ? std::min(colour, giColour_ + ease)
                                   : std::max(colour, giColour_ - ease);
  }
  if (first) {
    // Each run where its clock would have it at this step: a replay, or a
    // restored game, picks the lights up where they would be.
    for (std::size_t k = 0; k < runs_.size(); ++k) {
      runs_[k] = static_cast<double>(now_) / static_cast<double>(runPeriod(k));
    }
    perimeterClock_ = static_cast<double>(now_) / static_cast<double>(kStepsPerSecond) *
                      static_cast<double>(kPerimeterPassesPerSecond) /
                      static_cast<double>(perimeterCount());
    return;
  }
  if (reduceMotion_) {
    return;  // Nothing runs: the ambient lights hold still.
  }
  for (std::size_t k = 0; k < runs_.size(); ++k) {
    runs_[k] += static_cast<double>(steps) / static_cast<double>(runPeriod(k));
  }
  // Each of the rim's lights goes round at passes / count rims a second, so
  // a place on the rim sees kPerimeterPassesPerSecond of them go by.
  perimeterClock_ += static_cast<double>(steps) / static_cast<double>(kStepsPerSecond) *
                     static_cast<double>(kPerimeterPassesPerSecond) /
                     static_cast<double>(perimeterCount());
}

float LightShow::runPeriod(std::size_t run) const noexcept {
  const RunSpeed& speed = kRunSpeeds[run];
  const float passes = twoComets() ? speed.passesTwo : speed.passesOne;
  const float fastest = kStepsPerSecond * passes / kMaxPassesPerSecond;
  return std::max(fastest, speed.base / (1.0F + kChaseSpeedUp * energy_));
}

float LightShow::fastestPassesPerSecond() const noexcept {
  float most = 0.0F;
  for (std::size_t k = 0; k < kRunSpeeds.size(); ++k) {
    const float passes = twoComets() ? kRunSpeeds[k].passesTwo : kRunSpeeds[k].passesOne;
    most = std::max(most, kStepsPerSecond * passes / runPeriod(k));
  }
  return most;
}

int LightShow::perimeterCount() const noexcept {
  return kPerimeterLights + static_cast<int>(std::lround(energy_ * kPerimeterLights));
}

LightShow::PerimeterView LightShow::perimeter() const noexcept {
  PerimeterView view;
  if (!seen_) {
    return view;
  }
  view.count = perimeterCount();
  view.phase = static_cast<float>(perimeterClock_ - std::floor(perimeterClock_));
  // Brighter the higher the game; down with the lights in a drain or tilt.
  view.level = (0.55F + 0.35F * energy_) * gi_;
  return view;
}

float LightShow::ambientLevel(std::size_t lamp) const noexcept {
  // A machine between shots is never still: every unlit insert shows as
  // coloured plastic lit from below (the floor, higher the higher the
  // game), and over that the lamps run chases of their own, the head at
  // kChaseHead, the tail from kChaseTail down. A lamp the rules light is
  // told apart by its blink and its star (lampLit). The higher the game the
  // faster the runs (each under its cap: kRunSpeeds) and the more of them at
  // once. With reduce motion nothing runs: a steady glow.
  const auto id = static_cast<LampId>(lamp);
  const float floor = kUnlitFloor + kFloorRise * energy_;
  if (reduceMotion_) {
    return floor + 0.08F;
  }
  // A comet's head at `head`, this lamp at `place`, on a ring of `count`
  // places when `ring`.
  const auto comet = [floor](float head, float place, float count, bool ring) {
    float d = std::fabs(head - place);
    if (ring) {
      d = std::min(d, count - d);
    }
    if (d < 0.5F) {
      return kChaseHead - (kChaseHead - kChaseTail) * d * 2.0F;
    }
    const float k = std::max(0.0F, 1.0F - (d - 0.5F) / 2.0F);
    return floor + (kChaseTail - floor) * k;
  };
  const bool two = twoComets();
  const auto ring = [&](Run run, std::size_t first, std::size_t count) {
    const auto n = static_cast<float>(count);
    const auto place = static_cast<float>(lamp - first);
    const float head = runPhase(run) * n;
    float level = comet(head, place, n, true);
    if (two && count >= 5) {
      level = std::max(level, comet(std::fmod(head + n * 0.5F, n), place, n, true));
    }
    return level;
  };
  const std::size_t i = lampIndex(id);
  if (table::isArrowLamp(id)) {
    // Back and forth along the seven shots; a second comet the other way
    // once the game is high.
    const float p = runPhase(kRunArrows) * 12.0F;
    float level = comet(p < 6.0F ? p : 12.0F - p, static_cast<float>(i), 7.0F, false);
    if (two) {
      level = std::max(level, comet(p < 6.0F ? 6.0F - p : p - 6.0F, static_cast<float>(i), 7.0F,
                                    false));
    }
    return level;
  }
  if (id >= LampId::WingW && id <= LampId::WingG) {
    return ring(kRunWing, lampIndex(LampId::WingW), 4);
  }
  if (id >= LampId::RollCall1 && id <= LampId::RollCall3) {
    return ring(kRunRollCall, lampIndex(LampId::RollCall1), 3);
  }
  if (table::isCourseLamp(id)) {
    // Round the six courses and Graduation in the middle of them.
    return ring(kRunCourses, lampIndex(LampId::Course1), 7);
  }
  // Revision 3's ladders: each runs its own way, up the table or out from
  // the middle; the T-A-X-I letters go round. Each on a clock of its own
  // period in steps (not yet on the Altitude's runs).
  const auto at = [this](float period) {
    const auto steps = static_cast<std::uint64_t>(period);
    return static_cast<float>(now_ % steps) / static_cast<float>(steps);
  };
  const auto ladder = [&](LampId first, std::size_t count, float period) {
    return comet(at(period) * static_cast<float>(count + 2),
                 static_cast<float>(i - lampIndex(first)), static_cast<float>(count + 2), true);
  };
  if (id >= LampId::DroneArmour1 && id <= LampId::DroneArmour5) {
    return ladder(LampId::DroneArmour1, 5, 70.0F);
  }
  if (id >= LampId::GForce1 && id <= LampId::GForce5) {
    return ladder(LampId::GForce1, 5, 84.0F);
  }
  if (id >= LampId::TaxiT && id <= LampId::TaxiI) {
    return comet(at(96.0F) * 4.0F, static_cast<float>(i - lampIndex(LampId::TaxiT)), 4.0F, true);
  }
  if (id >= LampId::Rank1 && id <= LampId::Rank7) {
    return ladder(LampId::Rank1, 7, 150.0F);
  }
  if (id >= LampId::Combo2X && id <= LampId::Combo5X) {
    return ladder(LampId::Combo2X, 4, 72.0F);
  }
  if (id >= LampId::Bonus2X && id <= LampId::Bonus5X) {
    return ladder(LampId::Bonus2X, 4, 72.0F);
  }
  // Concept A. Each shot's pips run up toward its arrow, the shots a beat
  // apart so that a wave crosses the table; the altimeter's rungs still to
  // climb run upward; the F-L-A-P-S letters and the deck's go round;
  // BOGEY and RADAR run up their banks.
  if (table::isPipLamp(id)) {
    const std::size_t n = i - lampIndex(LampId::LadderCircuitLeft1);
    const auto arrow = static_cast<float>(n / table::kShotLadderPips);
    const auto place = static_cast<float>(table::kShotLadderPips - 1 - n % table::kShotLadderPips);
    float phase = runPhase(kRunPips) - 0.12F * arrow;
    phase -= std::floor(phase);
    float level = comet(phase * 5.0F, place, 5.0F, true);
    if (two) {
      level = std::max(level, comet(std::fmod(phase * 5.0F + 2.5F, 5.0F), place, 5.0F, true));
    }
    return level;
  }
  if (id >= LampId::AltitudeFL100 && id <= LampId::AltitudeFL500) {
    return comet(runPhase(kRunAltimeter) * 7.0F,
                 static_cast<float>(i - lampIndex(LampId::AltitudeFL100)), 7.0F, true);
  }
  if (id >= LampId::FlapsF && id <= LampId::FlapsS) {
    return ring(kRunFlaps, lampIndex(LampId::FlapsF), 5);
  }
  if (id >= LampId::Bogey1 && id <= LampId::Radar3) {
    const std::size_t k = (i - lampIndex(LampId::Bogey1)) % 3;
    return comet(runPhase(kRunPips) * 5.0F, static_cast<float>(2 - k), 5.0F, true);
  }
  if (id >= LampId::DeckA && id <= LampId::CatShot) {
    return ring(kRunDeck, lampIndex(LampId::DeckA), 4);
  }
  if (id == LampId::Lock1 || id == LampId::Lock2) {
    const bool first = runPhase(kRunLocks) < 0.5F;
    return (first == (id == LampId::Lock1)) ? 0.6F : floor;
  }
  // The rest breathe, each a little after the one before; and as the game
  // climbs a band of light sweeps up over them.
  const float wave =
      0.5F + 0.5F * std::cos(kTau * (runPhase(kRunRest) - static_cast<float>(i) * 0.13F));
  const float breathe = floor + (0.55F - floor) * wave;
  const float bandY = table::kHeight * (1.1F - 1.3F * runPhase(kRunSweep));
  const float band =
      std::min(1.0F, energy_ * 3.0F) * bell(table::kLamps[i].position.y - bandY, 70.0F);
  return std::max(breathe, floor + (kChaseHead - floor) * band);
}

void LightShow::updateLamps(const Game& game) noexcept {
  const Rules& rules = game.rules();
  const bool rm = reduceMotion_;
  for (std::size_t i = 0; i < kLampCount; ++i) {
    states_[i] = rules.lampState(table::kLamps[i].id);
  }
  // The lock lamps follow the bays themselves (as TableLights does).
  states_[lampIndex(LampId::Lock1)] = game.bayTaken(0) ? LampState::On : LampState::Off;
  states_[lampIndex(LampId::Lock2)] = game.bayTaken(1) ? LampState::On : LampState::Off;
  // A shot's pips while its arrow blinks (or the lights hint at it) are
  // the light show's: they run up to the arrow (pipChase), not blink.
  for (std::size_t a = 0; a < 7; ++a) {
    if (blinking(states_[a]) || hint_ == static_cast<int>(a)) {
      for (std::size_t k = 0; k < table::kShotLadderPips; ++k) {
        states_[lampIndex(table::ladderLamp(a, k))] = LampState::Off;
      }
    }
  }
  for (std::size_t d = 0; d < doorLevels_.size(); ++d) {
    doorLevels_[d] = fx::lampLevel(rules.doorLampState(d), now_, rm);
  }

  if (attract_) {
    // Attract: a fresh burst on a dome every three quarters of a second.
    const std::uint64_t ta = now_ - attractStart_;
    if (!rm && ta % 90 == 0) {
      constexpr std::array<Flasher, 10> kDomes{
          Flasher::Hangar, Flasher::RampLeft,  Flasher::Briefing, Flasher::RampRight,
          Flasher::Nest,   Flasher::LeftArch,  Flasher::Turbine,  Flasher::Deck,
          Flasher::SlingLeft, Flasher::SlingRight};
      constexpr std::array<Colour, 10> kColours{
          Colour::Orange, Colour::Violet, Colour::Yellow, Colour::Magenta, Colour::Cyan,
          Colour::Cyan,   Colour::Lime,   Colour::Magenta, Colour::Cyan,  Colour::Cyan};
      const std::size_t k = static_cast<std::size_t>(ta / 90) % kDomes.size();
      burst(kDomes[k], kColours[k], 0.6F, 100.0F, 40);
    }
    // Brighter than it was (lit plastic at the least, kAttractFloor), yet
    // dark enough between the patterns' lights for them to read.
    for (std::size_t i = 0; i < kLampCount; ++i) {
      levels_[i] = clamp01(std::max(attractLevel(i), kAttractFloor));
    }
    darkenCutLamps(levels_);
    return;
  }

  const std::uint64_t t = now_ - showStart_;
  float factor = 1.0F;
  if (rules.tilted()) {
    factor = 0.0F;
  } else if ((show_ == Show::MultiballStart || show_ == Show::Graduation) && t < kBlackoutSteps) {
    factor = rm ? 0.35F : 0.0F;
  }
  // The bonus counting down: the chases rest, so the count reads; a lamp
  // the rules light stays as bright as ever.
  const bool counting = show_ == Show::Drain &&
                        t < kBonusStart + kBonusHold + kBonusLamps.size() * kBonusStride;

  // In a blackout only the show itself lights a lamp.
  const bool blackout = factor < 0.5F && !rules.tilted() && show_ != Show::Drain;
  std::array<float, kLampCount> hits{};
  for (std::size_t k = 0; k < kLitRollovers && k < rolloverHit_.size(); ++k) {
    float& level = hits[lampIndex(kRolloverLamps[k])];
    level = std::max(level, rolloverFlash(k));
  }
  for (std::size_t k = 0; k < holeHit_.size(); ++k) {
    float& level = hits[lampIndex(kHoleLamps[k])];
    level = std::max(level, holeFlash(k));
  }
  for (std::size_t k = 0; k < targetHit_.size(); ++k) {
    float& level = hits[lampIndex(LampId::TurboCharge)];
    level = std::max(level, targetFlash(k));
  }
  hits[lampIndex(LampId::Lock1)] = std::max(hits[lampIndex(LampId::Lock1)], bayFlash(0));
  hits[lampIndex(LampId::Lock2)] = std::max(hits[lampIndex(LampId::Lock2)], bayFlash(1));
  hits[lampIndex(LampId::Kickback)] = kickbackFlash();
  // Revision 3: the armour lamp a hit has just won, the letter beside a
  // struck stand-up.
  if (const int won = rules.droneHits(); won > 0 && won <= 5) {
    const std::size_t k = lampIndex(LampId::DroneArmour1) + static_cast<std::size_t>(won - 1);
    hits[k] = std::max(hits[k], droneFlash());
  }
  for (std::size_t k = 0; k < table::kTaxiStandups && k < standupHit_.size(); ++k) {
    float& level = hits[lampIndex(table::taxiLamp(k))];
    level = std::max(level, standupFlash(k));
  }
  // Concept A: the deck's letters by their stand-ups, the Multiball lamp
  // by the pylon, the pips running to a lit (or hinted) shot, the hinted
  // arrow breathing up.
  for (std::size_t k = table::kDeckStandupA; k < table::kDeckStandupA + 3; ++k) {
    float& level = hits[lampIndex(table::deckLetterLamp(k))];
    level = std::max(level, standupFlash(k));
  }
  hits[lampIndex(LampId::Multiball)] = std::max(hits[lampIndex(LampId::Multiball)], pylonFlash());
  for (std::size_t k = lampIndex(LampId::LadderCircuitLeft1);
       k <= lampIndex(LampId::LadderCircuitRight3); ++k) {
    hits[k] = std::max(hits[k], pipChase(k));
  }
  if (hint_ >= 0 && !blinking(states_[static_cast<std::size_t>(hint_)])) {
    const float breath =
        0.5F + 0.5F * std::cos(kTau * static_cast<float>(now_ % 120) / 120.0F);
    float& level = hits[static_cast<std::size_t>(hint_)];
    level = std::max(level, 0.6F + 0.4F * breath);
  }
  // The ladders fill as they grow.
  if (rules.rank() > wasRank_) {
    sweepGroup(kGroupRank);
  }
  if (rules.comboLevel() > wasCombo_ && rules.comboLevel() >= 2) {
    sweepGroup(kGroupCombo);
  }
  if (rules.bonusMultiplier() > wasBonus_) {
    sweepGroup(kGroupBonus);
  }
  wasRank_ = rules.rank();
  wasCombo_ = rules.comboLevel();
  wasBonus_ = rules.bonusMultiplier();
  // Each spinner lights its shot's arrow as it turns.
  for (const auto& [spinner, arrow] :
       {std::pair{table::kLaneSpinner, LampId::ArrowTurbine},
        std::pair{table::kCircuitSpinnerLeft, LampId::ArrowCircuitLeft},
        std::pair{table::kCircuitSpinnerRight, LampId::ArrowCircuitRight}}) {
    hits[lampIndex(arrow)] = std::max(hits[lampIndex(arrow)], spinnerLevel(spinner) * 0.7F);
  }

  for (std::size_t i = 0; i < kLampCount; ++i) {
    if (!blackout && !rules.tilted() && states_[i] != LampState::On) {
      // Unlit, or a blink's dark half: never darker than coloured plastic.
      const float ambient = counting ? kUnlitFloor : ambientLevel(i);
      hits[i] = std::max(hits[i], states_[i] == LampState::Off ? ambient : kUnlitFloor);
    }
    const float base = fx::lampLevel(states_[i], now_, rm) * factor;
    const float overlay = rules.tilted() ? 0.0F
                          : blackout     ? showLevel(i)
                                         : std::max({hits[i], sweepLevel(i), showLevel(i)});
    levels_[i] = clamp01(std::max(base, overlay));
  }
  darkenCutLamps(levels_);
}

float LightShow::lampLevel(table::LampId lamp) const noexcept {
  const std::size_t i = lampIndex(lamp);
  return i < levels_.size() ? levels_[i] : 0.0F;
}

bool LightShow::lampLit(table::LampId lamp) const noexcept {
  const LampState state = lampState(lamp);
  return !attract_ && state != LampState::Off;
}

LampState LightShow::lampState(table::LampId lamp) const noexcept {
  const std::size_t i = lampIndex(lamp);
  return i < states_.size() ? states_[i] : LampState::Off;
}

ChaseView LightShow::chase(std::size_t arrow) const noexcept {
  ChaseView view;
  if (arrow >= kLampCount || !table::isArrowLamp(table::kLamps[arrow].id)) {
    return view;
  }
  const LampState state = attract_ ? LampState::Off : states_[arrow];
  if (!attract_ && hint_ == static_cast<int>(arrow) && !blinking(state)) {
    // The shot the lights point the player to: its chevrons run.
    view.active = true;
    view.steady = reduceMotion_;
    view.phase = static_cast<float>(now_ % 48) / 48.0F;
    return view;
  }
  if (state != LampState::Blink && state != LampState::FastBlink) {
    // The table's own chevrons under the arrows the rules leave dark: one
    // shot at a time at a new game, more the higher the game, all seven at
    // the top and in attract; the set moves on round the shots. Not with
    // reduce motion.
    if (reduceMotion_ || state == LampState::On || !seen_) {
      return view;
    }
    const auto count = static_cast<std::size_t>(1 + std::lround(energy_ * 6.0F));
    const auto start = static_cast<std::size_t>(runs_[kRunChevrons] / 6.0) % 7;
    if ((arrow + 7 - start) % 7 >= count) {
      return view;
    }
    view.active = true;
    view.ambient = true;
    view.phase = runPhase(kRunChevrons);
    return view;
  }
  view.active = true;
  view.fast = state == LampState::FastBlink;
  view.steady = reduceMotion_;
  const std::uint64_t period = view.fast ? 24 : 48;
  view.phase = static_cast<float>(now_ % period) / static_cast<float>(period);
  return view;
}

// --- Trails -----------------------------------------------------------------

void LightShow::updateTrails(const Game& game) noexcept {
  std::array<bool, table::kMaxBalls> seen{};
  const BallViews balls = game.balls(1.0F);
  for (const BallView& ball : balls) {
    if (ball.id >= trails_.size()) {
      continue;
    }
    seen[ball.id] = true;
    TrailView& trail = trails_[ball.id];
    if (!ball.visible || ball.locked || ball.sink > 0.0F) {
      trail.count = 0;  // None while a hole draws it in or out.
      continue;
    }
    const Vec2 at = ball.drawnAt();
    if (trail.count > 0) {
      const Vec2 d = at - trail.points[0];
      if (d.x * d.x + d.y * d.y > 80.0F * 80.0F) {
        trail.count = 0;  // Moved by hand: no streak across the table.
      }
    }
    for (std::size_t k = std::min(trail.count, kTrailLength - 1); k > 0; --k) {
      trail.points[k] = trail.points[k - 1];
    }
    trail.points[0] = at;
    trail.count = std::min(trail.count + 1, kTrailLength);
    if (trail.count > 1) {
      const Vec2 d = trail.points[0] - trail.points[trail.count - 1];
      trail.speed = std::sqrt(d.x * d.x + d.y * d.y) / static_cast<float>(trail.count - 1);
    } else {
      trail.speed = 0.0F;
    }
  }
  for (std::size_t slot = 0; slot < trails_.size(); ++slot) {
    if (!seen[slot]) {
      trails_[slot] = TrailView{};
    }
  }
}

const TrailView& LightShow::trail(std::size_t slot) const noexcept {
  static const TrailView kNone{};
  return slot < trails_.size() ? trails_[slot] : kNone;
}

}  // namespace pinball::fx
