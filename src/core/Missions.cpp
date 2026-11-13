#include "core/Missions.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include "core/DisplayText.hpp"

namespace pinball {

namespace {

constexpr std::array<std::string_view, kRankCount> kRankNames{
    "ROOKIE", "TRAINEE",    "WINGMATE",    "PILOT",     "NAVIGATOR",
    "ACE",    "INSTRUCTOR", "FLIGHT DEAN", "CHANCELLOR"};

constexpr std::array<std::string_view, kGoalCount> kGoalTexts{
    "HIT METEOR BUMPERS",     "HIT THRUSTER BUMPERS",   "HIT THE BEACON BUMPER",
    "HIT ANY BUMPERS",        "PASS ROLL CALL LANES",   "PASS RETURN LANES",
    "SHOOT THE RUNWAY",       "SHOOT THE QUASAR CHUTE", "ENTER JUMP GATES",
    "ENTER THE SINGULARITY",  "HIT TURBO TARGETS",      "HIT MERIT TARGETS",
    "HIT OVERDRIVE TARGETS",  "HIT ANY TARGETS",        "HIT SLINGSHOTS"};

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
//
// The display calls a mission a course, and names it like an academy
// catalogue. An ordinary course is a subject and a number: the rank (1 to 9)
// times 100, plus 1, 2 or 3 for the top, middle or bottom target. A special
// course is an exam and its subject, with no number.
constexpr std::array<std::array<Mission, kMissionsPerRank>, kRankCount> kMissions{{
    {{  // Rookie
        normal("BUMPER BASICS 101", {G::AttackBumpers, 8}, kNone, 500'000),
        normal("ROLL CALL 102", {G::ReEntryLanes, 3}, kNone, 500'000),
        normal("RUNWAY DRILLS 103", {G::Ramp, 3}, kNone, 500'000),
        special("POP QUIZ: TARGETS", {G::AnyTarget, 9}, kNone, 750'000),
    }},
    {{  // Trainee
        normal("TARGET LAB 201", {G::AnyTarget, 15}, kNone, 750'000),
        normal("QUASAR NAV 202", {G::Ramp, 1}, {G::Hyperspace, 1}, 750'000),
        normal("THRUSTER PRIMER 203", {G::AttackBumpers, 8}, {G::EngineBumpers, 6}, 750'000),
        special("FIELD TRIP: JUMP GATES", {G::Wormholes, 3}, kNone, 1'000'000),
    }},
    {{  // Wingmate
        normal("QUASAR APPROACH 301", {G::ReEntryLanes, 2}, {G::Hyperspace, 1}, 1'000'000),
        normal("GATE TRANSIT 302", {G::Wormholes, 1}, {G::ReEntryLanes, 3}, 1'000'000),
        normal("GRAVITY STUDIES 303", {G::EngineBumpers, 6}, {G::BlackHole, 1}, 1'000'000),
        special("MIDTERM: PRECISION", {G::AnyTarget, 20}, {G::Ramp, 1}, 1'500'000),
    }},
    {{  // Pilot
        normal("BEACON SIGNALS 401", {G::SatelliteBumper, 3}, {G::Ramp, 1}, 1'250'000),
        normal("LANE DISCIPLINE 402", {G::ReturnLanes, 4}, {G::ReEntryLanes, 3}, 1'250'000),
        normal("SLINGSHOT PHYSICS 403", {G::Slingshots, 6}, {G::Hyperspace, 1}, 1'250'000),
        special("LAB FINAL: GRAVITY", {G::Wormholes, 2}, {G::BlackHole, 1}, 2'000'000),
    }},
    {{  // Navigator
        normal("BUMPER MARATHON 501", {G::AnyBumper, 25}, kNone, 1'500'000),
        normal("MERIT WORKSHOP 502", {G::Ramp, 3}, {G::MedalTargets, 3}, 1'500'000),
        normal("TURBO TUNING 503", {G::BoosterTargets, 3}, {G::MultiplierTargets, 2}, 1'500'000),
        special("CHECK RIDE: GATE RUN", {G::AnyTarget, 9}, {G::Wormholes, 2}, 3'000'000),
    }},
    {{  // Ace
        normal("BUMPER TACTICS 601", {G::AttackBumpers, 15}, {G::ReEntryLanes, 3}, 1'750'000),
        normal("APPROACH PATTERNS 602", {G::ReturnLanes, 4}, {G::Ramp, 2}, 1'750'000),
        normal("LONG RANGE NAV 603", {G::Wormholes, 3}, {G::Hyperspace, 1}, 1'750'000),
        special("FIRST SOLO: BUMPERS", {G::AnyBumper, 30}, {G::BlackHole, 1}, 4'000'000),
    }},
    {{  // Instructor
        normal("ADVANCED RUNWAY 701", {G::Ramp, 3}, {G::Hyperspace, 2}, 2'000'000),
        normal("TARGET SEMINAR 702", {G::AnyTarget, 25}, kNone, 2'000'000),
        normal("ORBITAL MECHANICS 703", {G::BlackHole, 1}, {G::Wormholes, 3}, 2'000'000),
        special("FINALS: PROPULSION", {G::EngineBumpers, 12}, {G::MedalTargets, 6}, 5'000'000),
    }},
    {{  // Flight Dean
        normal("BUMPER COMMAND 801", {G::AttackBumpers, 20}, {G::SatelliteBumper, 5}, 2'500'000),
        normal("LANE ENDURANCE 802", {G::ReEntryLanes, 6}, {G::ReturnLanes, 6}, 2'500'000),
        normal("QUASAR THEORY 803", {G::Hyperspace, 3}, kNone, 2'500'000),
        special("THESIS: RUNWAY OPS", {G::AnyTarget, 30}, {G::Ramp, 3}, 6'000'000),
    }},
    {{  // Chancellor
        normal("BUMPER DOCTORATE 901", {G::AnyBumper, 40}, kNone, 3'000'000),
        normal("FLIGHT PLANNING 902", {G::Ramp, 2}, {G::Wormholes, 3}, 3'000'000),
        normal("SYSTEMS MASTERY 903", {G::BoosterTargets, 6}, {G::MedalTargets, 6}, 3'000'000),
        special("GRADUATION DAY", {G::Hyperspace, 3}, {G::BlackHole, 2}, 10'000'000),
    }},
}};

// The table is checked when it is compiled: a mission that could not be
// played, paid nothing or did not fit the display never reaches a player.
constexpr bool stageIsValid(const MissionStage& stage) noexcept {
  return std::to_underlying(stage.goal) < kGoalCount;
}

constexpr bool missionIsValid(const Mission& m) noexcept {
  return display::fits(m.name, display::kMaxMissionNameLength) && m.stages[0].count > 0 &&
         std::ranges::all_of(m.stages, stageIsValid) && m.award > 0 && m.progress > 0 &&
         m.progress <= kProgressPerRank;
}

constexpr bool missionTableIsValid() noexcept {
  std::uint64_t previousNormal = 0;
  for (const auto& rank : kMissions) {
    if (!std::ranges::all_of(rank, missionIsValid)) {
      return false;
    }
    // Awards never fall as rank rises, and the special pays more.
    if (rank[0].award < previousNormal || rank[kSpecialMission].award <= rank[0].award) {
      return false;
    }
    previousNormal = rank[0].award;
  }
  return true;
}

constexpr auto rankFits = [](std::string_view text) {
  return display::fits(text, display::kMaxRankNameLength);
};
constexpr auto goalFits = [](std::string_view text) {
  return display::fits(text, display::kMaxGoalTextLength);
};

static_assert(missionTableIsValid());
static_assert(std::ranges::all_of(kRankNames, rankFits));
static_assert(std::ranges::all_of(kGoalTexts, goalFits));
static_assert(kNormalProgress < kSpecialProgress && kSpecialProgress <= kProgressPerRank);

}  // namespace

std::string_view goalText(Goal goal) noexcept {
  const std::size_t index = std::to_underlying(goal);
  return index < kGoalTexts.size() ? kGoalTexts[index] : std::string_view{};
}

std::string_view rankName(std::size_t rank) noexcept {
  return kRankNames[std::min(rank, kRankCount - 1)];
}

const Mission& missionAt(std::size_t rank, std::size_t index) noexcept {
  return kMissions[std::min(rank, kRankCount - 1)][std::min(index, kMissionsPerRank - 1)];
}

MissionControl::MissionControl(double stepSeconds) noexcept
    // Clamped before converting, so an extreme step length cannot ask for a
    // count outside the integer's range or a tank that holds nothing.
    : fullFuelSteps_(static_cast<std::uint32_t>(std::clamp(
          std::round(kFuelSeconds /
                     (std::isfinite(stepSeconds) && stepSeconds > 0.0 ? stepSeconds : 1.0 / 120.0)),
          1.0, 4.0e9))) {}

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

void MissionControl::standDown() noexcept {
  phase_ = Phase::Idle;
  missionIndex_ = 0;
  stageIndex_ = 0;
  remaining_ = 0;
  fuelSteps_ = 0;
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
  // A selected special mission is kept until the ramp accepts it: a single
  // target cannot trade it down to an ordinary mission.
  if (phase_ == Phase::Selected && missionIndex_ == kSpecialMission && !bankComplete) {
    return outcome;
  }
  const std::size_t chosen = bankComplete ? kSpecialMission : which;
  // Hitting the target of the mission already selected changes nothing, so
  // there is nothing to announce again.
  outcome.selected = phase_ != Phase::Selected || missionIndex_ != chosen;
  missionIndex_ = chosen;
  phase_ = Phase::Selected;
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
