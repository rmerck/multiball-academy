#include "core/Missions.hpp"

#include <algorithm>
#include <cmath>

namespace pinball {

namespace {

constexpr std::array<std::string_view, kRankCount> kRankNames{
    "CADET",     "ENSIGN",    "LIEUTENANT", "CAPTAIN",       "LT COMMANDER",
    "COMMANDER", "COMMODORE", "ADMIRAL",    "FLEET ADMIRAL"};

constexpr std::array<std::string_view, kGoalCount> kGoalTexts{
    "HIT ATTACK BUMPERS",   "HIT ENGINE BUMPERS",   "HIT THE SATELLITE BUMPER",
    "HIT ANY BUMPERS",      "PASS RE-ENTRY LANES",  "PASS RETURN LANES",
    "SHOOT THE LAUNCH RAMP", "SHOOT HYPERSPACE",    "ENTER WORMHOLES",
    "ENTER THE BLACK HOLE", "HIT BOOSTER TARGETS",  "HIT MEDAL TARGETS",
    "HIT MULTIPLIER TARGETS", "HIT ANY TARGETS",    "HIT SLINGSHOTS"};

constexpr MissionStage kNone{Goal::AnyBumper, 0};
constexpr std::uint8_t kNormalProgress = 6;
constexpr std::uint8_t kSpecialProgress = 9;

constexpr Mission normal(std::string_view name, MissionStage a, MissionStage b,
                         std::uint64_t award) noexcept {
  return {name, {{a, b}}, award, kNormalProgress};
}
constexpr Mission special(std::string_view name, MissionStage a, MissionStage b,
                          std::uint64_t award) noexcept {
  return {name, {{a, b}}, award, kSpecialProgress};
}

using G = Goal;

// Four missions per rank: three chosen by the mission targets (top, middle,
// bottom) and a special one for lighting the whole bank. Counts and awards
// climb with rank.
constexpr std::array<std::array<Mission, kMissionsPerRank>, kRankCount> kMissions{{
    {{  // Cadet
        normal("TARGET PRACTICE", {G::AttackBumpers, 8}, kNone, 500'000),
        normal("RE-ENTRY TRAINING", {G::ReEntryLanes, 3}, kNone, 500'000),
        normal("LAUNCH TRAINING", {G::Ramp, 3}, kNone, 500'000),
        special("SCIENCE MISSION", {G::AnyTarget, 9}, kNone, 750'000),
    }},
    {{  // Ensign
        normal("BUG HUNT", {G::AnyTarget, 15}, kNone, 750'000),
        normal("ORBITAL RESCUE", {G::Ramp, 1}, {G::Hyperspace, 1}, 750'000),
        normal("ALIEN MENACE", {G::AttackBumpers, 8}, {G::EngineBumpers, 6}, 750'000),
        special("SECRET MISSION", {G::Wormholes, 3}, kNone, 1'000'000),
    }},
    {{  // Lieutenant
        normal("STRAY COMET", {G::ReEntryLanes, 2}, {G::Hyperspace, 1}, 1'000'000),
        normal("SPACE RADIATION", {G::Wormholes, 1}, {G::ReEntryLanes, 3}, 1'000'000),
        normal("BLACK HOLE THREAT", {G::EngineBumpers, 6}, {G::BlackHole, 1}, 1'000'000),
        special("COSMIC PLAGUE", {G::AnyTarget, 20}, {G::Ramp, 1}, 1'500'000),
    }},
    {{  // Captain
        normal("SATELLITE RETRIEVAL", {G::SatelliteBumper, 3}, {G::Ramp, 1}, 1'250'000),
        normal("RECON PATROL", {G::ReturnLanes, 4}, {G::ReEntryLanes, 3}, 1'250'000),
        normal("DOOMSDAY DEVICE", {G::Slingshots, 6}, {G::Hyperspace, 1}, 1'250'000),
        special("TIME WARP", {G::Wormholes, 2}, {G::BlackHole, 1}, 2'000'000),
    }},
    {{  // Lt Commander
        normal("ASTEROID SWEEP", {G::AnyBumper, 25}, kNone, 1'500'000),
        normal("SUPPLY RUN", {G::Ramp, 3}, {G::MedalTargets, 3}, 1'500'000),
        normal("DEEP SCAN", {G::BoosterTargets, 3}, {G::MultiplierTargets, 2}, 1'500'000),
        special("MAELSTROM", {G::AnyTarget, 9}, {G::Wormholes, 2}, 3'000'000),
    }},
    {{  // Commander
        normal("FLEET DRILL", {G::AttackBumpers, 15}, {G::ReEntryLanes, 3}, 1'750'000),
        normal("CONVOY ESCORT", {G::ReturnLanes, 4}, {G::Ramp, 2}, 1'750'000),
        normal("SIGNAL TRACE", {G::Wormholes, 3}, {G::Hyperspace, 1}, 1'750'000),
        special("STARFALL", {G::AnyBumper, 30}, {G::BlackHole, 1}, 4'000'000),
    }},
    {{  // Commodore
        normal("BLOCKADE RUN", {G::Ramp, 3}, {G::Hyperspace, 2}, 2'000'000),
        normal("NEBULA SURVEY", {G::AnyTarget, 25}, kNone, 2'000'000),
        normal("GRAVITY SLING", {G::BlackHole, 1}, {G::Wormholes, 3}, 2'000'000),
        special("DARK FRONTIER", {G::EngineBumpers, 12}, {G::MedalTargets, 6}, 5'000'000),
    }},
    {{  // Admiral
        normal("ARMADA STRIKE", {G::AttackBumpers, 20}, {G::SatelliteBumper, 5}, 2'500'000),
        normal("LONG PATROL", {G::ReEntryLanes, 6}, {G::ReturnLanes, 6}, 2'500'000),
        normal("QUANTUM LEAP", {G::Hyperspace, 3}, kNone, 2'500'000),
        special("SUPERNOVA", {G::AnyTarget, 30}, {G::Ramp, 3}, 6'000'000),
    }},
    {{  // Fleet Admiral
        normal("LAST STAND", {G::AnyBumper, 40}, kNone, 3'000'000),
        normal("GRAND TOUR", {G::Ramp, 2}, {G::Wormholes, 3}, 3'000'000),
        normal("FLAGSHIP DUTY", {G::BoosterTargets, 6}, {G::MedalTargets, 6}, 3'000'000),
        special("FINAL FRONTIER", {G::Hyperspace, 3}, {G::BlackHole, 2}, 10'000'000),
    }},
}};

}  // namespace

std::string_view goalText(Goal goal) noexcept {
  const auto index = static_cast<std::size_t>(goal);
  return index < kGoalTexts.size() ? kGoalTexts[index] : std::string_view{};
}

std::string_view rankName(std::size_t rank) noexcept {
  return kRankNames[std::min(rank, kRankCount - 1)];
}

const Mission& missionAt(std::size_t rank, std::size_t index) noexcept {
  return kMissions[std::min(rank, kRankCount - 1)][std::min(index, kMissionsPerRank - 1)];
}

MissionControl::MissionControl(double stepSeconds) noexcept
    : fullFuelSteps_(static_cast<std::uint32_t>(std::lround(
          kFuelSeconds /
          (std::isfinite(stepSeconds) && stepSeconds > 0.0 ? stepSeconds : 1.0 / 120.0)))) {}

void MissionControl::reset() noexcept {
  rank_ = 0;
  progress_ = 0;
  phase_ = Phase::Idle;
  missionIndex_ = 0;
  stageIndex_ = 0;
  remaining_ = 0;
  fuelSteps_ = 0;
  completed_ = 0;
}

const Mission* MissionControl::mission() const noexcept {
  return phase_ == Phase::Idle ? nullptr : &missionAt(rank_, missionIndex_);
}

const MissionStage* MissionControl::stage() const noexcept {
  return phase_ == Phase::Active ? &missionAt(rank_, missionIndex_).stages[stageIndex_] : nullptr;
}

int MissionControl::fuelLights() const noexcept {
  if (phase_ != Phase::Active || fullFuelSteps_ == 0) {
    return 0;
  }
  // Rounded up, so the last light stays on until the tank is truly empty.
  const std::uint64_t lit = (static_cast<std::uint64_t>(fuelSteps_) * kFuelLights +
                             fullFuelSteps_ - 1U) /
                            fullFuelSteps_;
  return static_cast<int>(std::min<std::uint64_t>(lit, kFuelLights));
}

void MissionControl::beginStage(std::size_t stage) noexcept {
  stageIndex_ = stage;
  remaining_ = missionAt(rank_, missionIndex_).stages[stage].count;
}

MissionControl::Outcome MissionControl::onMissionTarget(std::size_t which,
                                                        bool bankComplete) noexcept {
  Outcome outcome;
  if (phase_ == Phase::Active || which >= kSpecialMission) {
    return outcome;
  }
  missionIndex_ = bankComplete ? kSpecialMission : which;
  phase_ = Phase::Selected;
  outcome.selected = true;
  return outcome;
}

MissionControl::Outcome MissionControl::onRamp() noexcept {
  Outcome outcome;
  if (phase_ == Phase::Selected) {
    phase_ = Phase::Active;
    fuelSteps_ = fullFuelSteps_;
    beginStage(0);
    outcome.accepted = true;
    return outcome;  // The accepting shot does not also count towards the mission.
  }
  if (phase_ == Phase::Active) {
    fuelSteps_ = fullFuelSteps_;
    outcome = onEvent(goalBit(Goal::Ramp));
    outcome.refueled = true;
  }
  return outcome;
}

MissionControl::Outcome MissionControl::onEvent(GoalSet goals) noexcept {
  Outcome outcome;
  if (phase_ != Phase::Active) {
    return outcome;
  }
  const MissionStage& current = missionAt(rank_, missionIndex_).stages[stageIndex_];
  if ((goals & goalBit(current.goal)) == 0U || remaining_ <= 0) {
    return outcome;
  }
  if (--remaining_ > 0) {
    return outcome;
  }
  return advance();
}

MissionControl::Outcome MissionControl::advance() noexcept {
  Outcome outcome;
  const Mission& m = missionAt(rank_, missionIndex_);
  const std::size_t next = stageIndex_ + 1;
  if (next < m.stages.size() && m.stages[next].count > 0) {
    beginStage(next);
    outcome.stageCompleted = true;
    return outcome;
  }

  // Mission complete.
  outcome.completed = true;
  outcome.award = m.award;
  ++completed_;
  phase_ = Phase::Idle;
  fuelSteps_ = 0;
  remaining_ = 0;
  progress_ += m.progress;
  if (progress_ >= kProgressPerRank) {
    if (rank_ + 1 < kRankCount) {
      ++rank_;
      progress_ -= kProgressPerRank;
      outcome.promoted = true;
    } else {
      progress_ = kProgressPerRank;  // Top rank: the gauge stays full.
    }
  }
  return outcome;
}

MissionControl::Outcome MissionControl::tick() noexcept {
  Outcome outcome;
  if (phase_ != Phase::Active) {
    return outcome;
  }
  if (fuelSteps_ > 0) {
    --fuelSteps_;
  }
  if (fuelSteps_ == 0) {
    phase_ = Phase::Idle;
    remaining_ = 0;
    outcome.aborted = true;
  }
  return outcome;
}

}  // namespace pinball
