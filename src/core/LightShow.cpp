#include "core/LightShow.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <utility>

#include "core/Game.hpp"
#include "core/Physics.hpp"
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
  kGroupCount
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
}};
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
constexpr std::uint64_t kPulseSpacing = kMinLargeFlashSteps;  // Never refused.

int priority(Show show) noexcept {
  switch (show) {
    case Show::None:
      return 0;
    case Show::Drain:
      return 1;
    case Show::Jackpot:
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
    case Show::None:
    case Show::Tilt:
    case Show::GameOver:
      break;
  }
  return 0;  // Until the rules let it go.
}

// The lamp an element lights when struck.
constexpr std::array<LampId, table::kHoles.size()> kHoleLamps{
    {LampId::ArrowBriefing, LampId::ArrowHangar, LampId::ArrowRunway, LampId::ArrowSkyway}};
static_assert(table::kScoopHole == 0 && table::kHangarHole == 1 && table::kRunwayHole == 2 &&
              table::kSkywayHole == 3);
constexpr std::array<LampId, table::kRollovers.size()> kRolloverLamps{
    {LampId::RollCall1, LampId::RollCall2, LampId::RollCall3, LampId::WingW, LampId::WingI,
     LampId::WingN, LampId::WingG}};

float clamp01(float v) noexcept { return v > 0.0F ? std::min(v, 1.0F) : 0.0F; }

float bell(float distance, float width) noexcept {
  const float u = distance / width;
  return std::exp(-u * u);
}

}  // namespace

Vec2 flasherPosition(Flasher flasher) noexcept {
  switch (flasher) {
    case Flasher::Hangar:
      return table::kHoles[table::kHangarHole].center;
    case Flasher::Briefing:
      return table::kHoles[table::kScoopHole].center;
    case Flasher::RampLeft:
      return table::kHoles[table::kRunwayHole].center;
    case Flasher::RampRight:
      return table::kHoles[table::kSkywayHole].center;
    case Flasher::Nest: {
      Vec2 sum{};
      for (const table::Bumper& bumper : table::kBumpers) {
        sum = sum + bumper.shape.center;
      }
      return sum * (1.0F / static_cast<float>(table::kBumpers.size()));
    }
    case Flasher::Academy:
      return table::kLamps[lampIndex(LampId::Graduation)].position;
    case Flasher::TopLanes:
      return centreOf(std::array<Vec2, 3>{table::kRollovers[0].zone.center,
                                          table::kRollovers[1].zone.center,
                                          table::kRollovers[2].zone.center});
    case Flasher::Turbine:
      return (table::kSpinner.a + table::kSpinner.b) * 0.5F;
    case Flasher::Apron:
      return table::kLamps[lampIndex(LampId::ShootAgain)].position;
    case Flasher::Table:
    case Flasher::Lights:
    case Flasher::Count:
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
  *this = LightShow();
  reduceMotion_ = reduce;
  attract_ = attract;
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
  }
  seen_ = true;
  now_ = step;
  readEvents(game);
  readSounds(game);
  updateShow(game);
  updateBursts();
  updateLamps(game);
  updateTrails(game);
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
        }
        break;
      case PhysicsEvent::Type::Enter:
        if (event.element.kind == table::ElementKind::Rollover && i < rolloverHit_.size()) {
          // The roll call lanes light the same part of the table as the nest.
          strike(i < 3 ? kGateNest : (i < 5 ? kGateLanesLeft : kGateLanesRight),
                 rolloverHit_[i]);
        }
        break;
      case PhysicsEvent::Type::Capture:
        if (event.element.kind == table::ElementKind::Hole && i < holeHit_.size()) {
          strike(static_cast<std::uint8_t>(kGateHole + i), holeHit_[i]);
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
        spinHit_ = mark;
        spinEnergy_ = std::min(1.0F, spinEnergy_ + 0.2F);
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
    }
  }
  spinEnergy_ *= 0.985F;
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
    sweepGroup(kGroupCourses);
    burst(Flasher::Academy, Colour::Violet, 0.7F, 100.0F, 40);
  }
  if (has(Sound::CoursePassed)) {
    sweepGroup(kGroupCourses);
    sweepGroup(kGroupCourses, 70);
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
  if (has(Sound::MultiballStart)) {
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

float LightShow::spinnerLevel() const noexcept {
  if (reduceMotion_) {
    return spinEnergy_;
  }
  // Brighter the faster it spins, with a ripple at each turn too small to be
  // a flash (WCAG 2.3.1: the turns come up to twenty a second).
  return spinEnergy_ * (0.85F + 0.15F * flash(spinHit_, 4));
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
  if (show == Show::Jackpot || show == Show::SuperJackpot) {
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
  if (show == Show::Drain || show == Show::Tilt) {
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
      const std::array<Colour, 3> pulses =
          gold ? std::array{Colour::Yellow, Colour::White, Colour::Yellow}
               : std::array{Colour::Magenta, Colour::Cyan, Colour::Yellow};
      const auto first = static_cast<std::uint32_t>(kBlackoutSteps + kSweepSteps + 6);
      for (std::size_t k = 0; k < (rm ? 1U : pulses.size()); ++k) {
        const auto delay = first + static_cast<std::uint32_t>(k * kPulseSpacing);
        for (const Flasher dome :
             {Flasher::Hangar, Flasher::Briefing, Flasher::RampLeft, Flasher::RampRight}) {
          burst(dome, pulses[k], 0.9F, 130.0F, 36, delay);
        }
      }
      sweepColour_ = gold ? Colour::Yellow : Colour::Magenta;
      break;
    }
    case Show::Jackpot:
      ringColour_ = Colour::Yellow;
      // The ring alone: each part of the table lights once as it passes,
      // one flash (a wash or domes as well would make it two).
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
    startShow(Show::Graduation);
  }
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
    case Show::Jackpot:
    case Show::SuperJackpot:
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
    default: {  // Rings out of the bumper nest.
      const Vec2 d = at - flasherPosition(Flasher::Nest);
      const float r = static_cast<float>(tp % 90) * 6.5F;
      return std::max(0.08F, bell(std::sqrt(d.x * d.x + d.y * d.y) - r, 70.0F));
    }
  }
}

// --- Lamps ------------------------------------------------------------------

float LightShow::ambientLevel(std::size_t lamp) const noexcept {
  // A machine between shots is never still: every unlit insert shows as dim
  // coloured plastic (kUnlitFloor), and over that the lamps run chases of
  // their own, the head at full, the tail from kChaseTail down. A lamp the
  // rules light is told apart by its blink and its star (lampLit). With
  // reduce motion the chases run four times slower, as soft waves.
  const auto id = static_cast<LampId>(lamp);
  if (reduceMotion_) {
    // No running lights at all: a steady gentle glow.
    return kUnlitFloor + 0.08F;
  }
  const float slow = 1.0F;
  const auto at = [this, slow](float period) {
    const auto steps = static_cast<std::uint64_t>(period * slow);
    return static_cast<float>(now_ % steps) / static_cast<float>(steps);
  };
  // A comet's head at `head`, this lamp at `place`, on a ring of `count`
  // places when `ring`.
  const auto comet = [](float head, float place, float count, bool ring) {
    float d = std::fabs(head - place);
    if (ring) {
      d = std::min(d, count - d);
    }
    // The head at kChaseHead (never as bright as a lamp the rules light),
    // the tail from kChaseTail down to the floor over the next two places.
    if (d < 0.5F) {
      return kChaseHead - (kChaseHead - kChaseTail) * d * 2.0F;
    }
    const float k = std::max(0.0F, 1.0F - (d - 0.5F) / 2.0F);
    return kUnlitFloor + (kChaseTail - kUnlitFloor) * k;
  };
  const std::size_t i = lampIndex(id);
  if (table::isArrowLamp(id)) {
    // Back and forth along the seven shots.
    const float p = at(168.0F) * 12.0F;
    return comet(p < 6.0F ? p : 12.0F - p, static_cast<float>(i), 7.0F, false);
  }
  if (id >= LampId::WingW && id <= LampId::WingG) {
    return comet(at(64.0F) * 4.0F, static_cast<float>(i - lampIndex(LampId::WingW)), 4.0F, true);
  }
  if (id >= LampId::RollCall1 && id <= LampId::RollCall3) {
    return comet(at(54.0F) * 3.0F, static_cast<float>(i - lampIndex(LampId::RollCall1)), 3.0F,
                 true);
  }
  if (table::isCourseLamp(id)) {
    // Round the six courses and Graduation in the middle of them.
    return comet(at(140.0F) * 7.0F, static_cast<float>(i - lampIndex(LampId::Course1)), 7.0F, true);
  }
  if (id == LampId::Lock1 || id == LampId::Lock2) {
    const bool first = at(80.0F) < 0.5F;
    return (first == (id == LampId::Lock1)) ? 0.5F : kUnlitFloor;
  }
  // The rest breathe, each a little after the one before.
  const float wave = 0.5F + 0.5F * std::cos(kTau * (at(150.0F) - static_cast<float>(i) * 0.13F));
  return kUnlitFloor + (0.45F - kUnlitFloor) * wave;
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
  for (std::size_t d = 0; d < doorLevels_.size(); ++d) {
    doorLevels_[d] = fx::lampLevel(rules.doorLampState(d), now_, rm);
  }

  if (attract_) {
    // Attract: a fresh burst on a dome every three quarters of a second.
    const std::uint64_t ta = now_ - attractStart_;
    if (!rm && ta % 90 == 0) {
      constexpr std::array<Flasher, 5> kDomes{Flasher::Hangar, Flasher::RampLeft,
                                              Flasher::Briefing, Flasher::RampRight,
                                              Flasher::Nest};
      constexpr std::array<Colour, 5> kColours{Colour::Orange, Colour::Violet, Colour::Yellow,
                                               Colour::Magenta, Colour::Cyan};
      const std::size_t k = static_cast<std::size_t>(ta / 90) % kDomes.size();
      burst(kDomes[k], kColours[k], 0.6F, 100.0F, 40);
    }
    for (std::size_t i = 0; i < kLampCount; ++i) {
      levels_[i] = clamp01(attractLevel(i));
    }
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
  for (std::size_t k = 0; k < rolloverHit_.size(); ++k) {
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
  hits[lampIndex(LampId::ArrowTurbine)] =
      std::max(hits[lampIndex(LampId::ArrowTurbine)], spinnerLevel() * 0.7F);

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
  if (arrow >= kLampCount || !table::isArrowLamp(table::kLamps[arrow].id) || attract_) {
    return view;
  }
  const LampState state = states_[arrow];
  if (state != LampState::Blink && state != LampState::FastBlink) {
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
