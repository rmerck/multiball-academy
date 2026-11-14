#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "bots/Bots.hpp"
#include "core/Game.hpp"
#include "core/Input.hpp"
#include "core/TableLayout.hpp"
#include "core/Vec2.hpp"

// A game on the bench: one Game, driven a step at a time, with everything
// the acceptance tests of TABLE.md section 5 need to see written down as it
// happens. It drives the real Game, rules and all, through its public
// interface and the test seam; nothing here knows what the rules are.
namespace pinball::bots {

// What a ball touched, or how it left. The order is the shots from left to
// right, then the rest.
enum class Touch : std::uint8_t {
  None,
  CircuitLeft,  // Crossed the left circuit's mouth going up.
  Runway,       // Taken by the left ramp.
  TargetLeft,   // Turbo target, left.
  Door,         // A bay door.
  Hangar,       // Taken by the hangar.
  Briefing,     // Taken by the scoop.
  Turbine,      // Crossed the spinner going up.
  TargetRight,
  Skyway,  // Taken by the right ramp.
  CircuitRight,
  Bumper,
  Slingshot,
  RollCall,  // A roll call lane.
  InlaneLeft,
  InlaneRight,
  OutlaneLeft,
  OutlaneRight,
  DrainCentre,  // Left between the flippers.
  Flipper,      // Only as a first touch: see firstTouch().
  Count,
};
inline constexpr std::size_t kTouchCount = static_cast<std::size_t>(Touch::Count);
[[nodiscard]] const char* touchName(Touch touch) noexcept;

struct TouchEvent {
  std::uint64_t step = 0;
  Touch touch = Touch::None;
  float speed = 0.0F;
  std::uint8_t ball = 0;
};

class TableHarness {
 public:
  TableHarness();

  [[nodiscard]] Game& game() noexcept { return game_; }
  [[nodiscard]] const Game& game() const noexcept { return game_; }

  // The controls, applied by the next advance().
  bool left = false;
  bool right = false;
  bool plunger = false;
  // A shove of the table on the next advance(): 0 none, 1 from the left,
  // 2 from the right, 3 from below. Cleared when used.
  int nudge = 0;
  // Starts a new game on the next advance(), whatever state this one is in.
  bool restart = false;
  // Lets a bot set the flippers for the next step.
  void act(Bot& bot) {
    const Flippers flippers = bot.act(sense());
    left = flippers.left;
    right = flippers.right;
  }
  // What a bot may see: the balls on the table that are not in a hole.
  [[nodiscard]] Sense sense() const;

  // Puts the ball of a live slot somewhere, moving. inPlay says whether it
  // counts as having left the launch channel.
  void put(std::size_t slot, Vec2 position, Vec2 velocity, bool inPlay);
  // Puts another ball on the table, in play. Returns its slot or kMaxBalls.
  std::size_t add(Vec2 position, Vec2 velocity);

  // One step of the game. Returns how the ball in slot 0 left the table
  // during it (an outlane or DrainCentre), or None.
  Touch advance();
  [[nodiscard]] std::uint64_t step() const noexcept { return step_; }

  // Everything touched, in order. A long run may clear it.
  std::vector<TouchEvent> events;
  // How many of each.
  std::array<std::uint64_t, kTouchCount> count{};
  std::array<std::uint64_t, table::kBumpers.size()> bumperHits{};
  std::array<std::uint64_t, table::kTargets.size()> targetHits{};
  std::array<std::uint64_t, table::kDropTargets.size()> doorHits{};
  std::array<std::uint64_t, table::kRollovers.size()> rolloverHits{};
  std::array<std::uint64_t, table::kHoles.size()> holeHits{};
  std::array<std::uint64_t, table::kSlingshots.size()> slingHits{};
  // Circuits, left then right: mouth crossed going up; top of the arch
  // crossed the same way round; far inlane reached.
  std::array<std::uint64_t, 2> circuitEntered{};
  std::array<std::uint64_t, 2> circuitTop{};
  std::array<std::uint64_t, 2> circuitDone{};
  // Hangar: entries, and of those the ones made while the door bank stood
  // (there must be none); times the bank dropped and came back up.
  std::uint64_t hangarEntries = 0;
  std::uint64_t hangarEntriesDoorsUp = 0;
  std::uint64_t bankDrops = 0;
  std::uint64_t bankRaises = 0;
  // Balls locked and let go again, and steps on which a locked ball was not
  // exactly in its bay.
  std::uint64_t locks = 0;
  std::uint64_t lockReleases = 0;
  // Locked balls taken out of their bays when a game ended.
  std::uint64_t locksEmptied = 0;
  std::uint64_t lockedAstray = 0;
  // Spinner turns counted.
  std::uint64_t spins = 0;
  // Kickback: times it took a ball; times it set one down; of those, the
  // ones whose next touch was the left inlane's switch or a flipper, and
  // the ones that drained within three seconds of being set down.
  std::uint64_t kickbacks = 0;
  std::uint64_t kickbackReturns = 0;
  std::uint64_t kickbackSafe = 0;
  std::uint64_t kickbackLostAgain = 0;
  // Balls that drained within the short save after an eject.
  std::uint64_t ejectSaves = 0;
  // Keeps the kickback out, whatever the rules say (the table "before the
  // kickback"). Applied before every step.
  bool kickbackOff = false;
  // Lights the kickback now.
  void lightKickback();
  // Whether the rules may put more than one ball in play: lock balls in
  // the Hangar, launch a second from a full turbo meter, start Graduation
  // Day. Off for the measurements made with one ball at a time.
  void setLocks(bool enabled);
  // Whether the rules give drained balls back (ball saver and multiball
  // grace). Off on the bench unless asked for: the measurements of the table
  // itself count every ball that leaves it. On for whole games.
  void setSaver(bool enabled);
  // Steps on which a ball was against a flipper.
  std::uint64_t flipperTouches = 0;
  // The longest any free ball in play went without being against a flipper,
  // in steps, counted from the later of its entering play and its last
  // flipper contact, and while it was on the table (not as it left).
  std::uint64_t longestNoFlipper = 0;
  // Drains of every ball, by way out: centre, left outlane, right outlane.
  std::array<std::uint64_t, 3> drains{};
  // Balls that left the table any other way (the game's fail-safe).
  std::uint64_t lostBalls = 0;
  // Stuck balls: a free ball in play that stayed within kStuckRadius for
  // kStuckSteps, not cradled on a raised flipper. Each is given a shove.
  static constexpr float kStuckRadius = 12.0F;
  static constexpr int kStuckSteps = 600;
  std::uint64_t stuck = 0;
  std::vector<Vec2> stuckAt;
  // Steps on which a ball in play was inside the launch lane.
  std::uint64_t laneIntrusions = 0;
  // Steps on which a ball was faster than the speed cap allows.
  std::uint64_t overSpeed = 0;

  // Whether p is against flipper i as it stands now, to within slack.
  [[nodiscard]] bool nearFlipper(Vec2 p, std::size_t i, float slack = 3.0F) const;
  // Whether the ball in a slot has touched a flipper since it was put there.
  [[nodiscard]] std::uint64_t lastFlipperStep(std::size_t slot) const noexcept {
    return slot < balls_.size() ? balls_[slot].flipperStep : 0;
  }
  [[nodiscard]] Vec2 position(std::size_t slot) const;
  [[nodiscard]] Vec2 velocity(std::size_t slot) const;
  [[nodiscard]] bool live(std::size_t slot) const;
  [[nodiscard]] bool held(std::size_t slot) const;

 private:
  void log(Touch touch, float speed, std::size_t ball);
  void watchBall(std::size_t slot);

  struct Ball {
    bool tracked = false;
    Vec2 previous{};
    Vec2 anchor{};
    int still = 0;
    bool circuitLeft = false;  // Entered that mouth going up, not yet resolved.
    bool circuitRight = false;
    std::uint64_t outlaneStep = 0;  // One more than the step; 0 if never.
    Touch outlane = Touch::None;
    std::uint64_t flipperStep = 0;  // One more than the step; 0 if never.
    std::uint64_t playStep = 0;     // One more than the step it was first seen in play.
    // One more than the step at which the kickback set it down; 0 if never.
    std::uint64_t kickStep = 0;
    bool kickTouchPending = false;
  };
  void settleKick(std::size_t slot, bool safe);

  Game game_;
  std::array<Ball, table::kMaxBalls> balls_{};
  std::uint64_t step_ = 0;
};

// The first thing a ball meets, for the launch and exit tests: the first
// touch event, or a flipper, or a drain, whichever comes first. Circuit
// mouths and the spinner are passed through, not met, and are skipped.
struct FirstTouch {
  Touch touch = Touch::None;  // None: nothing within the time allowed.
  int steps = 0;
};
[[nodiscard]] FirstTouch firstTouch(TableHarness& harness, int maxSteps);

}  // namespace pinball::bots
