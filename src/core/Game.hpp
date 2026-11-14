#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "core/Input.hpp"
#include "core/Physics.hpp"
#include "core/Rules.hpp"
#include "core/Sound.hpp"
#include "core/TableLayout.hpp"
#include "core/Vec2.hpp"

namespace pinball {

namespace save {
struct Snapshot;
}  // namespace save

// What the renderer needs to draw a ball.
struct BallView {
  Vec2 position;        // Playfield pixels.
  float angle = 0.0F;   // Radians.
  bool visible = true;  // False while a hole holds it out of sight.
  float scale = 1.0F;   // Above 1 while it rides a ramp's rail over the table.
  // Which ball: its slot, 0 to kMaxBalls - 1. A ball keeps its id for as
  // long as it is on the table.
  std::uint8_t id = 0;
  // Riding a ramp's rail over the table, or waiting at its end for the exit
  // to clear: to be drawn above everything on the playfield.
  bool raised = false;
  // Locked in one of the Hangar's bays: in view, at rest, out of play.
  bool locked = false;
  // For the eye only, while the Briefing room or the Hangar takes the ball
  // in or lets it out (table::kHoleSinkSeconds): it is drawn at drawnAt(),
  // this far from position, and sunk this far into the hole, from 0 (on
  // the table) to 1 (down out of sight), drawn smaller and darker the
  // further down it is. position stays where the game has the ball.
  Vec2 drawOffset{};
  float sink = 0.0F;

  [[nodiscard]] constexpr Vec2 drawnAt() const noexcept { return position + drawOffset; }
};

// Something the table did during a step, beside what the balls touched
// (Game::events()): for the display, the sounds and the tests.
struct GameEvent {
  enum class Type : std::uint8_t {
    DoorBankDropped,
    DoorBankRaised,
    BallLocked,        // index: the bay.
    LockReleased,      // index: the bay it left.
    HoleEject,         // index: the hole. Every ball a hole returns, locked or not.
    Spin,              // The spinner turned once.
    KickbackFired,     // The kickback took the ball.
    KickbackReturned,  // And set it down again.
    EjectSave,         // A ball drained within the short save after an eject.
  };
  Type type = Type::Spin;
  std::uint8_t ball = 0;
  std::uint8_t index = 0;
};

// The balls on the table, lowest id first. Fixed capacity and returned by
// value, so there is no allocation. Loop over it directly:
//   for (const BallView& ball : game.balls(alpha))
struct BallViews {
  std::array<BallView, table::kMaxBalls> items{};
  std::size_t count = 0;

  // A view of the balls present. It points into this object, so it is not
  // offered on a temporary: keep the BallViews in a variable first.
  [[nodiscard]] std::span<const BallView> span() const& noexcept {
    return std::span<const BallView>(items).first(count);
  }
  std::span<const BallView> span() const&& = delete;
  // The same goes for these. A range-for over game.balls(alpha) is fine: it
  // keeps the object for the length of the loop.
  [[nodiscard]] auto begin() const& noexcept { return span().begin(); }
  [[nodiscard]] auto end() const& noexcept { return span().end(); }
  void begin() const&& = delete;
  void end() const&& = delete;
  [[nodiscard]] std::size_t size() const noexcept { return count; }
  [[nodiscard]] bool empty() const noexcept { return count == 0; }
};

// What the renderer needs to draw one flipper.
struct FlipperView {
  Vec2 pivot;          // Playfield pixels.
  float angle = 0.0F;  // Radians, clockwise on screen from +x.
};

struct GameTestAccess;

// All game state and rules. Has no dependency on SDL so it can be driven and
// verified headless. The platform layer feeds it input and draws its state.
class Game {
 public:
  // The simulation always advances by exactly this much. A fixed step keeps
  // physics deterministic, so the caller cannot choose it.
  static constexpr double kStepSeconds = 1.0 / 120.0;
  // Most balls on the table at once.
  static constexpr std::size_t kMaxBalls = table::kMaxBalls;

  Game();

  // Advances the simulation by one fixed step.
  void step(const InputState& input);

  // Derived from the step count so it cannot drift from it.
  [[nodiscard]] double simTime() const noexcept {
    return static_cast<double>(stepCount_) * kStepSeconds;
  }
  [[nodiscard]] std::uint64_t stepCount() const noexcept { return stepCount_; }

  // The primary ball: the one with the lowest id. There is always one.
  [[nodiscard]] BallView ball() const;
  // Every ball on the table, including any in a hole or in the launch
  // lane, as ball(alpha) would give each of them.
  [[nodiscard]] BallViews balls(float alpha = 1.0F) const;
  // How many balls are on the table: 1 to kMaxBalls.
  [[nodiscard]] std::size_t ballCount() const noexcept { return physics_.activeBallCount(); }
  // Index 0 is the left flipper, 1 the right.
  [[nodiscard]] FlipperView flipper(std::size_t index) const;

  // For drawing between steps. alpha is the fraction of a step that has
  // passed since the latest one (FixedTimestep::alpha()): 0 gives the state
  // one step ago, 1 the latest state, and anything else lies between them.
  // Values outside [0, 1] are clamped; a value that is not a number counts
  // as 1. A ball that was moved by hand during the latest step (served,
  // returned by a hole) is given at its new place for every alpha, so it is
  // never drawn part-way along the jump.
  [[nodiscard]] BallView ball(float alpha) const;
  [[nodiscard]] FlipperView flipper(std::size_t index, float alpha) const;
  // How far the plunger is drawn back, in pixels.
  [[nodiscard]] float plungerPull() const noexcept { return physics_.plungerPull(); }
  // 1 just after a bumper was hit, fading to 0. Drives its light.
  [[nodiscard]] float bumperFlash(std::size_t index) const noexcept {
    return index < bumperFlash_.size() ? bumperFlash_[index] : 0.0F;
  }
  // Ball search: if a ball in play has not moved for this long, the table
  // gives it a shove, as a real machine fires its coils to free a stuck ball.
  static constexpr double kBallSearchSeconds = 4.0;
  // Times the ball search has fired.
  [[nodiscard]] std::uint32_t ballSearchCount() const noexcept { return ballSearchCount_; }

  // How long a hole keeps the ball, in seconds: the scoop and the hangar
  // their hold, a ramp the ride along its rail. The scoop and the hangar
  // each hold one ball at a time: a ball that arrives while another is
  // inside is taken and scored on arrival, waits behind it, and its own
  // hold starts when the one ahead has left. Rails carry any number.
  [[nodiscard]] static constexpr double holdSeconds(table::HoleType type) noexcept {
    return table::holdSeconds(type);
  }
  // A ball on a rail is drawn this much larger.
  static constexpr float kRailScale = 1.15F;
  // With several balls a hole returns its ball only when no other ball is
  // within this distance of the exit, in pixels, and this long after the
  // last ball left by the same exit. A single ball never waits for either.
  static constexpr float kHoleExitClearance = 30.0F;
  static constexpr double kHoleReleaseSpacingSeconds = 0.3;

  // --- Bay doors -----------------------------------------------------------
  // Whether the bank stands. It drops at once when the rules say so, and
  // comes back up when they say so and no free ball's centre is inside the
  // mouth (table::kDoorMouthCenter): until then the raise is pending.
  [[nodiscard]] bool doorBankUp() const noexcept { return physics_.doorBankUp(); }
  [[nodiscard]] bool doorRaisePending() const noexcept { return doorRaisePending_; }

  // --- The lock ------------------------------------------------------------
  // Balls locked in the Hangar's bays. A locked ball keeps its slot and its
  // id, so it counts toward kMaxBalls, is in balls() (BallView::locked, at
  // its bay) and stays where it is when a ball ends or the table is tilted.
  // The bays are emptied when the game is over and when a new one starts.
  [[nodiscard]] std::size_t lockedBalls() const noexcept;
  [[nodiscard]] bool ballLocked(std::size_t slot) const noexcept {
    return slot < kMaxBalls && physics_.ballActive(slot) && balls_[slot].lockBay != 0;
  }
  // Whether a ball is in this bay (0 or 1). The bays empty in order but
  // need not be taken in order once balls have left, so a lamp for a bay
  // asks this, not how many are locked.
  [[nodiscard]] bool bayTaken(std::size_t bay) const noexcept;
  [[nodiscard]] std::uint32_t lockCount() const noexcept { return lockCount_; }
  [[nodiscard]] std::uint32_t lockReleaseCount() const noexcept { return lockReleaseCount_; }

  // --- Spinner -------------------------------------------------------------
  // Turns counted so far, and turns a passing ball has earned that are
  // still to be counted (one every table::kSpinPayoutSteps steps).
  [[nodiscard]] std::uint32_t spinCount() const noexcept { return spinCount_; }
  [[nodiscard]] int spinsOwed() const noexcept { return spinsOwed_; }
  // Where the spinner's blade is in its turn, 0 to 1 (not including 1). It
  // makes half a turn for each spin counted: 0 and 0.5 are flat, at rest.
  [[nodiscard]] float spinnerPhase() const noexcept {
    return static_cast<float>(spinnerTicks_) / static_cast<float>(2 * table::kSpinPayoutSteps);
  }

  // --- Kickback ------------------------------------------------------------
  // Armed: the next ball into the left outlane's pocket is taken and carried
  // back to the left inlane. The rules say when (Rules::kickbackLit()).
  [[nodiscard]] bool kickbackArmed() const noexcept { return physics_.kickbackArmed(); }
  [[nodiscard]] std::uint32_t kickbackCount() const noexcept { return kickbackCount_; }

  // What the table did during the latest step.
  [[nodiscard]] std::span<const GameEvent> gameEvents() const noexcept {
    return std::span<const GameEvent>(gameEvents_).first(gameEventCount_);
  }

  // Times the ball has rolled over each rollover.
  [[nodiscard]] std::uint32_t rolloverCount(std::size_t index) const noexcept {
    return index < rolloverCounts_.size() ? rolloverCounts_[index] : 0;
  }
  // Times the ball has dropped into each hole.
  [[nodiscard]] std::uint32_t holeCount(std::size_t index) const noexcept {
    return index < holeCounts_.size() ? holeCounts_[index] : 0;
  }
  // Whether a hole is holding the primary ball right now.
  [[nodiscard]] bool ballHeld() const noexcept { return physics_.ballHeld(primaryBall()); }

  // Nudge: speed given to every ball, in pixels per second, by a shove from
  // the side and from the bottom of the table.
  static constexpr float kNudgeSideSpeed = 130.0F;
  static constexpr float kNudgeUpSpeed = 170.0F;
  // How far the playfield is drawn displaced right now, in pixels. Decays to
  // zero after a nudge.
  [[nodiscard]] Vec2 shake() const noexcept { return shake_; }

  // Sounds started by the most recent step.
  [[nodiscard]] SoundSet sounds() const noexcept { return sounds_; }

  // Score, lights, balls and messages.
  [[nodiscard]] const Rules& rules() const noexcept { return rules_; }
  [[nodiscard]] bool gameOver() const noexcept { return rules_.gameOver(); }
  // 1 just after a slingshot fired, fading to 0.
  [[nodiscard]] float slingshotFlash(std::size_t index) const noexcept {
    return index < slingshotFlash_.size() ? slingshotFlash_[index] : 0.0F;
  }
  // Total bumper hits this session.
  [[nodiscard]] std::uint32_t bumperHits() const noexcept { return bumperHits_; }
  // Number of balls lost down the drain so far, every ball counted.
  [[nodiscard]] std::uint32_t drainCount() const noexcept { return drainCount_; }
  // What the balls did during the latest step, as the physics reported it.
  // For tests and for the players that test the table.
  [[nodiscard]] std::span<const PhysicsEvent> events() const noexcept { return physics_.events(); }
  // Whether a ball has left the launch channel since it was put on the
  // table. False for a slot with no ball.
  [[nodiscard]] bool ballInPlay(std::size_t slot) const noexcept {
    return slot < kMaxBalls && physics_.ballActive(slot) && balls_[slot].inPlay;
  }

  // Whether a ball is in the launch lane waiting for the player's plunge
  // (not one the table launches by itself).
  [[nodiscard]] bool ballAwaitingLaunch() const noexcept;

  // Asks for another ball. It is served to the plunger when the lane is
  // empty, kAutoServeSpacingSeconds after the last ball served this way, and
  // kAutoLaunchDelaySeconds later it is launched without the player at
  // table::kAutoLaunchSpeed. Refused (false) when the game is over or the
  // balls on the table and those still waiting would exceed kMaxBalls.
  bool requestBall() noexcept;
  // --- A game saved and resumed (core/Snapshot.hpp, docs/RESUME.md) ----------
  // A game the player has started (a ball has been in play since it began,
  // or it was resumed) and that is not over. Only such a game is saved.
  [[nodiscard]] bool inProgress() const noexcept { return started_ && !rules_.gameOver(); }
  // Games begun on this table: one for each new game asked for, one for
  // each game restored, and one for the game it was made with once a ball
  // of it has gone into play. For whoever must notice that the game is not
  // the one it was.
  [[nodiscard]] std::uint32_t gamesBegun() const noexcept { return gamesBegun_; }
  // Everything that carries the game on, between two steps. Only
  // meaningful while inProgress().
  [[nodiscard]] save::Snapshot snapshot() const;
  // Puts a saved game back exactly: the rules, the table and every ball,
  // where it was and moving as it was. Refused (false, nothing changed) if
  // the snapshot is not save::valid(). The controls are released: the next
  // step sees no button held.
  bool restore(const save::Snapshot& snapshot);

  // Balls asked for and not yet served.
  [[nodiscard]] std::size_t queuedBalls() const noexcept { return queuedBalls_; }
  static constexpr double kAutoServeSpacingSeconds = 0.75;
  static constexpr double kAutoLaunchDelaySeconds = 0.25;
  // An auto-launched ball found at rest on the plunger for this long is
  // launched again.
  static constexpr double kAutoRelaunchSeconds = 0.5;
  // Times an auto-launched ball has been launched, second launches included.
  [[nodiscard]] std::uint32_t autoLaunchCount() const noexcept { return autoLaunchCount_; }
  [[nodiscard]] std::uint32_t autoRelaunchCount() const noexcept { return autoRelaunchCount_; }

 private:
  // The tests force states that play cannot reach (a ball lost outside the
  // table) through this. It is defined only in the tests.
  friend struct GameTestAccess;
  // Stages the scenes of the store screenshots (core/Scenes.hpp) the same
  // way, for debug builds and the review tool.
  friend struct SceneStage;

  // Leaves exactly one ball on the table, on the plunger, and nothing waiting.
  void serveBall();
  void snapshotForDrawing();
  void handleEvents();
  // Tells the rules, once, that the served ball is in play.
  void enterPlay();
  void startNewGame();
  void handleNudges(const InputState& input);
  void handleDrain(std::size_t slot);
  // Deals with balls that have drained or been lost outside the table.
  void handleLostBalls();
  // Why a ball left the table while others stayed on it.
  enum class BallLoss : std::uint8_t { Drained, OutOfBounds };
  // The one way a ball leaves the table while others play on. The rules
  // will hear of every such ball from here.
  void loseBallWithOthersLive(std::size_t slot, BallLoss loss);
  // Takes a ball off the table and forgets everything kept about it.
  void removeBall(std::size_t slot);
  // Puts another ball on the table, in the lowest free slot. Returns the
  // slot, or kMaxBalls if every slot is in use or the input is not finite.
  std::size_t addBall(Vec2 position, Vec2 velocity);
  // Notes which balls have left the launch channel.
  void updateInPlay();
  // Serves and launches the balls asked for with requestBall().
  void updateAutoLaunch();
  // Puts a waiting ball on the plunger, in the given free slot.
  void serveQueuedBall(std::size_t slot);
  // Notes a ball that a hole or the kickback has just taken.
  void noteHeldBall(std::size_t slot);
  // Runs a held ball's hold and returns it when its time has come.
  void updateHeldBall(std::size_t slot);
  // Puts a ball on the plunger for the player, leaving locked balls alone.
  // Returns its slot.
  std::size_t serveForPlayer();
  // Whether a ball is free of the lock: on the table and not in a bay.
  [[nodiscard]] bool ballLive(std::size_t slot) const noexcept {
    return physics_.ballActive(slot) && balls_[slot].lockBay == 0;
  }
  // Live balls other than this one.
  [[nodiscard]] std::size_t otherLiveBalls(std::size_t slot) const noexcept;
  // Locks the ball the Hangar has just taken, if a bay is free.
  void lockBall(std::size_t slot);
  // Lets every locked ball go, one at a time, by the Hangar's exit.
  void releaseLocks() noexcept;
  // Takes the locked balls off the table.
  void clearLocks();
  // Whether this ball is the one its hole returns next.
  [[nodiscard]] bool firstInHole(std::size_t slot) const noexcept;
  // The way the next ball out of the Hangar goes.
  [[nodiscard]] Vec2 nextHangarExitVelocity() noexcept;
  // The Briefing room's kick, a little different each time.
  [[nodiscard]] Vec2 nextScoopExitVelocity() noexcept;
  // Returns a held ball to the table and notes it.
  void returnBall(std::size_t slot, std::size_t exitHole, Vec2 position, Vec2 velocity);
  // Does what the rules ask of the doors and the kickback.
  void applyRules();
  [[nodiscard]] bool doorMouthClear() const;
  // The balls, as the rules count them.
  [[nodiscard]] TableStatus tableStatus() const noexcept;
  // Every ball in play is in a timed hold: none free, none on the plunger.
  [[nodiscard]] bool allBallsHeld() const noexcept;
  // A ball has just been taken by a hole or the kickback: the rules hear of
  // it and the Hangar's answer is carried out.
  void tellRulesOfCapture(std::size_t slot);
  void updateSpinner();
  void addSpins(float speedAcross, int direction) noexcept;
  // How the ball in a slot is leaving.
  [[nodiscard]] Rules::DrainInfo drainInfo(std::size_t slot) const noexcept;
  void note(GameEvent::Type type, std::size_t ball, std::size_t index = 0) noexcept;
  // The next number from the game's generator.
  [[nodiscard]] std::uint64_t nextRandom() noexcept;
  // Whether a ball may leave by this hole's exit now: nothing in the way,
  // and long enough since the last ball left by it.
  [[nodiscard]] bool exitIsClear(std::size_t slot, std::size_t hole, double spacingSeconds) const;
  // The flags say which flippers are actually raised: the keys count only
  // while the controls are live.
  void updateBallSearch(std::size_t slot, bool leftRaised, bool rightRaised);
  // The lowest live slot.
  [[nodiscard]] std::size_t primaryBall() const noexcept;
  [[nodiscard]] BallView ballView(std::size_t slot) const;
  [[nodiscard]] BallView ballView(std::size_t slot, float alpha) const;
  // How far through a span of seconds that started at mark (one more than
  // a step) the game is, from 0 to 1. A mark of 0 is long past.
  [[nodiscard]] float progressSince(std::uint64_t mark, double seconds) const noexcept;

  PhysicsWorld physics_;
  Rules rules_{kStepSeconds};
  std::uint64_t stepCount_ = 0;
  std::uint32_t drainCount_ = 0;
  InputState previousInput_;
  std::array<float, table::kBumpers.size()> bumperFlash_{};
  std::uint32_t bumperHits_ = 0;
  std::array<float, table::kSlingshots.size()> slingshotFlash_{};
  std::array<std::uint32_t, table::kRollovers.size()> rolloverCounts_{};
  std::array<std::uint32_t, table::kHoles.size()> holeCounts_{};
  // What the game keeps for each ball slot, beside the physics state.
  struct BallState {
    // What is holding the ball (a hole or the kickback), and for how many
    // steps its hold has run so far.
    table::ElementRef holder{};
    std::uint32_t heldSteps = 0;
    bool wasHeld = false;
    // The order in which held balls were taken: lower came first.
    std::uint64_t arrival = 0;
    // One more than the bay it is locked in; 0 if it is not locked.
    std::uint8_t lockBay = 0;
    // Locked, and told to leave.
    bool releasing = false;
    // One more than the step at which the scoop or the hangar last returned
    // it; 0 if neither has.
    std::uint64_t ejectStep = 0;
    // Ball search state.
    Vec2 searchAnchor{};
    std::uint32_t stillSteps = 0;
    // State one step ago, for drawing between steps.
    BallView previous{};
    // The ball was placed somewhere during the latest step.
    bool placed = false;
    // Seen clear of the launch channel since it was put on the table.
    bool inPlay = false;
    // Where it was when a hole took it: a rail's ride starts there.
    Vec2 takenAt{};
    // One more than the step at which a hole last took it; 0 if none has.
    // For drawing it on its way in.
    std::uint64_t takenStep = 0;
    // Where the scoop or the hangar let it out from, for drawing it on its
    // way out: the hole's centre, or the bay it was locked in.
    Vec2 ejectFrom{};
    bool ejectFromBay = false;
    // Served by requestBall(): launched without the player.
    bool autoLaunch = false;
    // Steps until the launch, while waiting for it; then steps spent at
    // rest on the plunger since.
    std::uint32_t launchSteps = 0;
    bool launched = false;
  };
  std::array<BallState, kMaxBalls> balls_{};
  // For each hole, one more than the step at which it last returned a ball;
  // 0 if it never has.
  std::array<std::uint64_t, table::kHoles.size()> lastReleaseStep_{};
  std::uint32_t ballSearchCount_ = 0;
  std::uint64_t arrivals_ = 0;
  // How many balls have left the Hangar close behind one another.
  std::uint32_t hangarBurst_ = 0;
  bool doorRaisePending_ = false;
  std::uint32_t lockCount_ = 0;
  std::uint32_t lockReleaseCount_ = 0;
  std::uint32_t kickbackCount_ = 0;
  // Spinner: turns owed, steps until the next may be counted, turns counted,
  // the way it turns (+1 or -1) and where the blade is, in twelfths of a turn.
  int spinsOwed_ = 0;
  int spinCooldown_ = 0;
  std::uint32_t spinCount_ = 0;
  int spinDirection_ = 1;
  std::uint32_t spinnerTicks_ = 0;
  static constexpr std::size_t kMaxGameEvents = 24;
  std::array<GameEvent, kMaxGameEvents> gameEvents_{};
  std::size_t gameEventCount_ = 0;
  // The game's own source of chance: xorshift64 from a fixed seed, so the
  // same input always gives the same game. Advanced only by nextRandom().
  std::uint64_t random_ = 0x9E3779B97F4A7C15ULL;
  std::size_t queuedBalls_ = 0;
  // One more than the step of the last serve by requestBall(); 0 if none.
  std::uint64_t lastAutoServeStep_ = 0;
  std::uint32_t autoLaunchCount_ = 0;
  std::uint32_t autoRelaunchCount_ = 0;
  Vec2 shake_{};
  SoundSet sounds_ = 0;
  // Flipper angles one step ago, for drawing between steps.
  std::array<float, table::kFlippers.size()> previousFlipperAngles_{};
  // A ball has been in play in this game (or the game was resumed).
  bool started_ = false;
  std::uint32_t gamesBegun_ = 0;
};

}  // namespace pinball
