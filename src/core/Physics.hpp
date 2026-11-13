#pragma once

#include <box2d/id.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/TableLayout.hpp"
#include "core/Vec2.hpp"

namespace pinball {

// Something that happened to a ball during a step.
struct PhysicsEvent {
  // Hit: touched a bumper, slingshot face or target.
  // Enter: rolled onto a rollover.
  // Capture: dropped into a hole. The ball is held until releaseBall().
  // BallHit: two balls touched. There is no element; ball is the lower slot
  // of the pair, otherBall the higher, and speed is their closing speed.
  enum class Type : std::uint8_t { Hit, Enter, Capture, BallHit };
  Type type = Type::Hit;
  table::ElementRef element;
  float speed = 0.0F;          // Ball speed when the event was recorded, pixels per second.
  std::uint8_t ball = 0;       // Slot of the ball it happened to.
  std::uint8_t otherBall = 0;  // BallHit only.
};

// Owns the Box2D world: the static table and the balls. All values crossing
// this interface are in playfield pixels; Box2D itself runs in metres.
//
// There are table::kMaxBalls ball slots, each with a body that exists for
// the life of the world. Slot 0 starts active on the plunger; the others
// start inactive: off the table, colliding with nothing, reporting nothing.
// Every ball method takes a slot; the forms without one mean slot 0. A slot
// out of range is ignored, and reads as an inactive ball at the origin.
class PhysicsWorld {
 public:
  static constexpr float kPixelsPerMeter = 100.0F;
  // In-plane gravity of the inclined table, in m/s^2.
  static constexpr float kGravity = 9.5F;
  // Hard cap on ball speed in m/s, enforced after every slice.
  static constexpr float kMaxBallSpeed = 30.0F;
  // Each step is simulated as this many equal slices. Box2D sweeps the fast
  // ball but not a rotating kinematic flipper, whose tip would otherwise move
  // most of a ball diameter per step and could pass through the ball. At four
  // slices the tip moves about 3.5 px per slice.
  static constexpr int kSlicesPerStep = 4;
  // Longest step accepted, in seconds. Larger values make the solver diverge.
  static constexpr float kMaxStepSeconds = 0.1F;
  // Shortest step accepted. Box2D divides by the slice length, and for a
  // vanishingly small one that quotient overflows and poisons the world.
  static constexpr float kMinStepSeconds = 1.0e-4F;

  // Throws std::runtime_error if the Box2D world cannot be created.
  PhysicsWorld();
  ~PhysicsWorld();
  PhysicsWorld(const PhysicsWorld&) = delete;
  PhysicsWorld& operator=(const PhysicsWorld&) = delete;

  // Flipper stroke speeds in radians per second. A real flipper completes its
  // stroke in 60 to 80 ms and falls back more slowly. The up speed sets how
  // hard shots are: at this value a ball struck near the pivot reaches the
  // middle of the table and one struck from mid-bat outward reaches the top.
  static constexpr float kFlipperUpSpeed = 13.5F;
  static constexpr float kFlipperDownSpeed = 12.0F;

  // Sets which flipper buttons are held. Applied on the next step.
  void setFlippers(bool leftHeld, bool rightHeld) noexcept;
  // Current angle of a flipper in radians. Index 0 is left, 1 is right.
  [[nodiscard]] float flipperAngle(std::size_t index) const noexcept;

  // Plunger. While held it draws back over kPlungerChargeSeconds; on release
  // it drives forward at a speed set by how far it was drawn back.
  static constexpr float kPlungerChargeSeconds = 1.0F;
  // These are plunger speeds in m/s. The ball leaves about 1.3 times faster,
  // because it rebounds off the moving face with its own restitution.
  static constexpr float kPlungerMinSpeed = 3.0F;   // Lightest tap.
  static constexpr float kPlungerMaxSpeed = 12.5F;  // Full draw.

  void setPlunger(bool held) noexcept;
  // How far the plunger is drawn back, in pixels: 0 at rest.
  [[nodiscard]] float plungerPull() const noexcept { return plungerPull_; }

  // Opens or closes the one-way gate at the top of the launch lane.
  void setLaneGateClosed(bool closed);
  [[nodiscard]] bool laneGateClosed() const noexcept { return laneGateClosed_; }

  // Speed added to a ball, in m/s, when it touches a bumper.
  static constexpr float kBumperKickSpeed = 5.5F;
  // Speed added along a slingshot's face normal, in m/s.
  static constexpr float kSlingshotKickSpeed = 4.5F;
  // Most events kept per ball and step. Further events for that ball in the
  // same step are dropped. A BallHit counts against the lower slot.
  static constexpr std::size_t kMaxEventsPerStep = 32;
  // Restitution between two balls: steel on steel is livelier than a ball
  // against the table.
  static constexpr float kBallBallRestitution = 0.85F;

  // Events from the most recent step, in the order they happened. Cleared at
  // the start of each step.
  [[nodiscard]] const std::vector<PhysicsEvent>& events() const noexcept { return events_; }

  // Advances by one fixed step. Ignored unless stepSeconds is within
  // [kMinStepSeconds, kMaxStepSeconds].
  void step(float stepSeconds);

  // Puts an inactive slot's ball on the table at position with velocity
  // (pixels, pixels per second; the velocity is held to the speed cap).
  // Returns false, changing nothing, if the slot is out of range or already
  // active or the input is not finite.
  bool activateBall(std::size_t ball, Vec2 position, Vec2 velocity);
  // Takes a slot's ball off the table, held or not. Ignored if inactive.
  void deactivateBall(std::size_t ball);
  [[nodiscard]] bool ballActive(std::size_t ball) const noexcept {
    return ball < balls_.size() && balls_[ball].active;
  }
  // Number of active balls, held ones included.
  [[nodiscard]] std::size_t activeBallCount() const noexcept;

  // Position and velocity are {0, 0} and the angle 0 for an inactive slot.
  [[nodiscard]] Vec2 ballPosition(std::size_t ball) const;
  [[nodiscard]] Vec2 ballVelocity(std::size_t ball) const;
  [[nodiscard]] float ballAngle(std::size_t ball) const;
  [[nodiscard]] Vec2 ballPosition() const { return ballPosition(0); }
  [[nodiscard]] Vec2 ballVelocity() const { return ballVelocity(0); }
  [[nodiscard]] float ballAngle() const { return ballAngle(0); }

  // True while a hole is holding the ball off the playfield.
  [[nodiscard]] bool ballHeld(std::size_t ball) const noexcept {
    return ball < balls_.size() && balls_[ball].held;
  }
  [[nodiscard]] bool ballHeld() const noexcept { return ballHeld(0); }
  // Index of the hole holding the ball. Meaningful only while ballHeld().
  [[nodiscard]] std::size_t heldHole(std::size_t ball) const noexcept {
    return ball < balls_.size() ? balls_[ball].heldHole : 0;
  }
  [[nodiscard]] std::size_t heldHole() const noexcept { return heldHole(0); }
  // Returns a held ball to play at position with velocity. The hole it was
  // in, and the hole at the exit, ignore that ball until it has moved clear;
  // they still take any other ball.
  void releaseBall(std::size_t ball, Vec2 position, Vec2 velocity);
  void releaseBall(Vec2 position, Vec2 velocity) { releaseBall(0, position, velocity); }

  // Teleports an active ball and sets its velocity (pixels and pixels per
  // second). Non-finite input is ignored. The velocity is held to the speed
  // cap. A held ball is returned to the table.
  void resetBall(std::size_t ball, Vec2 position, Vec2 velocity);
  void resetBall(Vec2 position, Vec2 velocity) { resetBall(0, position, velocity); }

  // Adds velocity (pixels per second) to a ball in play. Ignored while the
  // ball is held or if the value is not finite. The result is held to the
  // speed cap.
  void pushBall(std::size_t ball, Vec2 velocityChange);
  void pushBall(Vec2 velocityChange) { pushBall(0, velocityChange); }

  // False if the ball has left the table shell or its state is not finite.
  // The caller should reset the ball when this happens. An inactive slot has
  // nothing wrong with it.
  [[nodiscard]] bool ballIsSane(std::size_t ball) const;
  [[nodiscard]] bool ballIsSane() const { return ballIsSane(0); }

  // Lets a ball through the closed lane gate, or stops it again. For a ball
  // that must leave the launch lane while others are in play and the gate
  // is shut behind them. Ignored for an inactive slot.
  void setBallPassesGate(std::size_t ball, bool passes);
  [[nodiscard]] bool ballPassesGate(std::size_t ball) const noexcept {
    return ball < balls_.size() && balls_[ball].passesGate;
  }

 private:
  void buildTable();
  void buildBall(std::size_t slot, bool enabled);
  void buildFlippers();
  void driveFlippers(float stepSeconds);
  void buildPlunger();
  void buildBumpers();
  void collectEvents();
  void checkBumpers(std::size_t slot);
  void kickFromBumper(std::size_t slot, const table::Bumper& bumper);
  void buildTargets();
  void checkZones(std::size_t slot);
  void pushEvent(PhysicsEvent::Type type, table::ElementRef element, float speed, std::size_t slot,
                 std::size_t otherSlot = 0);
  // Kicks the ball if it is on the slingshot's kicker face. Returns whether it did.
  bool kickFromSlingshot(std::size_t slot, const table::Triangle& sling);
  void drivePlunger(float stepSeconds);
  void clampBallSpeed(std::size_t slot);
  // How many equal parts to take the next slice in: 1 unless two balls are
  // closing fast enough to need a finer look.
  [[nodiscard]] int partsForBallApproach(float sliceSeconds) const;
  // The slot whose body this is, or kMaxBalls if it is not a ball.
  [[nodiscard]] std::size_t slotOf(b2BodyId body) const noexcept;
  void clearLatches(std::size_t slot) noexcept;

  struct FlipperState {
    b2BodyId body{};
    float angle = 0.0F;
    bool held = false;
  };

  b2WorldId world_{};
  b2BodyId table_{};
  std::array<FlipperState, 2> flippers_{};

  b2BodyId plunger_{};
  float plungerPull_ = 0.0F;          // Pixels drawn back.
  float plungerReleaseSpeed_ = 0.0F;  // Pixels per second while driving forward.
  bool plungerHeld_ = false;

  // One per slot. The latches are per ball, so that a second ball on a
  // bumper or in a lane still counts and a hole that has just returned one
  // ball still takes another.
  struct BallSlot {
    b2BodyId body{};
    b2ShapeId shape{};
    bool active = false;
    bool held = false;
    std::size_t heldHole = 0;
    bool passesGate = false;
    // Bumpers the ball is touching, so each touch kicks once.
    std::array<bool, table::kBumpers.size()> touchingBumper{};
    // Rollovers the ball is currently on, so each pass reports once.
    std::array<bool, table::kRollovers.size()> onRollover{};
    // Holes that will take the ball. A hole disarms when it returns it.
    std::array<bool, table::kHoles.size()> holeArmed{};
  };
  std::array<BallSlot, table::kMaxBalls> balls_{};

  std::vector<PhysicsEvent> events_;
  // Events recorded for each ball this step, for the per-ball limit.
  std::array<std::size_t, table::kMaxBalls> eventCounts_{};

  b2BodyId laneGate_{};
  bool laneGateClosed_ = false;
};

}  // namespace pinball
