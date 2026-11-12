#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace pinball {

// Something a mission can ask the player to do.
enum class Goal : std::uint8_t {
  AttackBumpers,
  EngineBumpers,
  SatelliteBumper,
  AnyBumper,
  ReEntryLanes,
  ReturnLanes,
  Ramp,
  Hyperspace,
  Wormholes,
  BlackHole,
  BoosterTargets,
  MedalTargets,
  MultiplierTargets,
  AnyTarget,
  Slingshots,
};
inline constexpr std::size_t kGoalCount = 15;

// One table event can satisfy several goals at once (an attack bumper is
// also "any bumper"), so events are reported as a set of goals.
using GoalSet = std::uint32_t;
constexpr GoalSet goalBit(Goal goal) noexcept { return GoalSet{1} << static_cast<unsigned>(goal); }

// Short instruction for the display, such as "HIT ATTACK BUMPERS".
std::string_view goalText(Goal goal) noexcept;

struct MissionStage {
  Goal goal;
  std::uint8_t count;  // 0 marks an unused second stage.
};

struct Mission {
  std::string_view name;
  std::array<MissionStage, 2> stages;
  std::uint64_t award;
  std::uint8_t progress;  // Rank progress earned on completion.
};

inline constexpr std::size_t kRankCount = 9;
inline constexpr std::size_t kMissionsPerRank = 4;  // Three choices and a special.
inline constexpr std::size_t kSpecialMission = 3;
inline constexpr int kProgressPerRank = 18;

std::string_view rankName(std::size_t rank) noexcept;
// The mission at a rank; both indices are clamped into range.
const Mission& missionAt(std::size_t rank, std::size_t index) noexcept;

// Tracks rank, the selected or active mission, its progress and its fuel.
class MissionControl {
 public:
  enum class Phase : std::uint8_t { Idle, Selected, Active };
  static constexpr double kFuelSeconds = 90.0;
  static constexpr int kFuelLights = 6;

  // What a call changed, for the caller to score and announce.
  struct Outcome {
    bool selected = false;
    bool accepted = false;
    bool refueled = false;
    bool stageCompleted = false;
    bool completed = false;
    bool promoted = false;
    bool aborted = false;
    std::uint64_t award = 0;
  };

  explicit MissionControl(double stepSeconds) noexcept;

  // Back to Cadet with nothing selected.
  void reset() noexcept;

  // A mission target was hit: 0 top, 1 middle, 2 bottom. With the whole bank
  // lit, the rank's special mission is selected instead. Ignored while a
  // mission is active.
  Outcome onMissionTarget(std::size_t which, bool bankComplete) noexcept;
  // The launch ramp was shot: accepts a selected mission, or refuels an
  // active one (and then counts towards it like any other event).
  Outcome onRamp() noexcept;
  // Any other scoring event, as the set of goals it satisfies.
  Outcome onEvent(GoalSet goals) noexcept;
  // Once per simulation step: burns fuel while a mission is active.
  Outcome tick() noexcept;

  std::size_t rank() const noexcept { return rank_; }
  int progress() const noexcept { return progress_; }
  Phase phase() const noexcept { return phase_; }
  // The selected or active mission, or nullptr when idle.
  const Mission* mission() const noexcept;
  std::size_t missionIndex() const noexcept { return missionIndex_; }
  // The stage being worked on and how many more hits it needs.
  const MissionStage* stage() const noexcept;
  int remaining() const noexcept { return remaining_; }
  // Lit fuel lights, 0 to kFuelLights. Zero unless a mission is active.
  int fuelLights() const noexcept;
  std::uint32_t completedMissions() const noexcept { return completed_; }

 private:
  void beginStage(std::size_t stage) noexcept;
  Outcome advance() noexcept;

  std::uint32_t fullFuelSteps_;
  std::size_t rank_ = 0;
  int progress_ = 0;
  Phase phase_ = Phase::Idle;
  std::size_t missionIndex_ = 0;
  std::size_t stageIndex_ = 0;
  int remaining_ = 0;
  std::uint32_t fuelSteps_ = 0;
  std::uint32_t completed_ = 0;
};

}  // namespace pinball
