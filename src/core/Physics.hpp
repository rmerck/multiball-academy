#pragma once

#include <box2d/id.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "core/TableLayout.hpp"
#include "core/Vec2.hpp"

namespace pinball {

namespace save {
struct BallRecord;
struct TableRecord;
}  // namespace save

// Something that happened to a ball during a step.
struct PhysicsEvent {
  // Hit: touched a bumper, slingshot face, target or bay door.
  // Enter: rolled onto a rollover.
  // Capture: dropped into a hole, or taken by the kickback. The ball is held
  // until releaseBall().
  // BallHit: two balls touched. There is no element; ball is the lower slot
  // of the pair, otherBall the higher, and speed is their closing speed.
  // Cross: passed over a sensor line. speed is the ball's speed across the
  // line, and direction says which way (table::SensorLine).
  enum class Type : std::uint8_t { Hit, Enter, Capture, BallHit, Cross };
  Type type = Type::Hit;
  table::ElementRef element;
  float speed = 0.0F;          // Ball speed when the event was recorded, pixels per second.
  std::uint8_t ball = 0;       // Slot of the ball it happened to.
  std::uint8_t otherBall = 0;  // BallHit only.
  std::int8_t direction = 0;   // Cross only: +1 or -1.
};

// The events of one step, in the order they happened. Each ball has its own
// allowance, so a busy ball cannot crowd out another's events, and touches
// between balls have one apart from those, so that a pile-up cannot cost a
// ball the capture or the hit it scores with. Room for all of it is taken
// once, at construction.
class PhysicsEventList {
 public:
  // Most events kept for one ball in a step, touches between balls aside.
  static constexpr std::size_t kMaxPerBall = 32;
  // Most BallHit events kept in a step, whichever balls they are between.
  static constexpr std::size_t kMaxBallHits = 16;

  PhysicsEventList();
  void clear() noexcept;
  // Keeps the event if its allowance is not used up. Returns whether it was
  // kept. An event for a slot that does not exist is never kept.
  bool push(const PhysicsEvent& event);
  [[nodiscard]] const std::vector<PhysicsEvent>& events() const noexcept { return events_; }

 private:
  std::vector<PhysicsEvent> events_;
  std::array<std::size_t, table::kMaxBalls> perBall_{};
  std::size_t ballHits_ = 0;
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
  // The lightest tap must still send the ball out of the lane: below about
  // 9.5 m/s it cannot climb the launch curve and comes back to the plunger.
  static constexpr float kPlungerMinSpeed = 9.6F;   // Lightest tap.
  static constexpr float kPlungerMaxSpeed = 12.5F;  // Full draw.

  void setPlunger(bool held) noexcept;
  // How far the plunger is drawn back, in pixels: 0 at rest.
  [[nodiscard]] float plungerPull() const noexcept { return plungerPull_; }

  // Speed added to a ball, in m/s, when it touches a bumper.
  static constexpr float kBumperKickSpeed = 5.5F;
  // Speed added along a slingshot's face normal, in m/s.
  static constexpr float kSlingshotKickSpeed = 4.5F;
  // Most events kept per ball and step. Further events for that ball in the
  // same step are dropped. Touches between balls are counted apart.
  static constexpr std::size_t kMaxEventsPerStep = PhysicsEventList::kMaxPerBall;
  static constexpr std::size_t kMaxBallHitsPerStep = PhysicsEventList::kMaxBallHits;
  // Restitution between two balls: steel on steel is livelier than a ball
  // against the table.
  static constexpr float kBallBallRestitution = 0.85F;
  // Friction between two balls: none. Polished steel slides on polished
  // steel, and with any grip between them two balls arriving together at a
  // narrowing lane (the mouth of an inlane, the gap between the flippers)
  // lean on each other and on the walls and stay there: the grip between
  // the balls is what holds the arch up. Each ball keeps its friction
  // against the table.
  static constexpr float kBallBallFriction = 0.0F;

  // Events from the most recent step, in the order they happened. Cleared at
  // the start of each step.
  [[nodiscard]] const std::vector<PhysicsEvent>& events() const noexcept {
    return events_.events();
  }

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

  // True while a hole or the kickback is holding the ball off the playfield.
  [[nodiscard]] bool ballHeld(std::size_t ball) const noexcept {
    return ball < balls_.size() && balls_[ball].held;
  }
  [[nodiscard]] bool ballHeld() const noexcept { return ballHeld(0); }
  // What is holding the ball: a hole, or the kickback. No element (kind
  // None) unless ballHeld().
  [[nodiscard]] table::ElementRef heldBy(std::size_t ball) const noexcept {
    return ballHeld(ball) ? balls_[ball].heldBy : table::ElementRef{};
  }
  // Index of the hole holding the ball. Meaningful only while a hole holds it.
  [[nodiscard]] std::size_t heldHole(std::size_t ball) const noexcept {
    return ball < balls_.size() ? balls_[ball].heldBy.index : 0;
  }
  [[nodiscard]] std::size_t heldHole() const noexcept { return heldHole(0); }

  // The bay doors, the Hangar's drop targets, stand or lie down as one
  // bank. Standing, each reports its hits and together they shut the Hangar;
  // down, they are not there at all. The caller keeps the bank from coming
  // up onto a ball (Game waits for table::kDoorMouthCenter's box to clear).
  void setDoorBank(bool up);
  [[nodiscard]] bool doorBankUp() const noexcept { return doorBankUp_; }

  // The kickback in the left outlane. While it is armed, the first ball
  // whose centre is inside table::kKickbackZone is taken (a Capture event
  // for the kickback; the ball is held until releaseBall()) and the kickback
  // disarms itself. Two balls arriving in the same slice: the lower slot is
  // taken, the other falls through.
  void setKickbackArmed(bool armed) noexcept { kickbackArmed_ = armed; }
  [[nodiscard]] bool kickbackArmed() const noexcept { return kickbackArmed_; }
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

  // --- A game saved and resumed (core/GameSave.cpp) ---------------------------
  // The body, hold and latches of a slot, and the flippers, plunger, bay
  // doors and kickback; the Game fields of the records are left alone.
  void saveBall(std::size_t ball, save::BallRecord& record) const;
  void saveTable(save::TableRecord& record) const;
  // Puts them back exactly: place, velocity, angle and spin, hold and
  // latches. Every slot is first taken off the table. Box2D keeps contacts
  // of its own that cannot be put back, so the touches already under way
  // are reported again by the first slice after a restore; that slice's
  // touches are therefore not reported (nor do they kick).
  void restoreBalls(std::span<const save::BallRecord> records);
  void restoreTable(const save::TableRecord& record);
  // A restore is waiting for its first slice.
  [[nodiscard]] bool restorePending() const noexcept { return skipTouches_; }

  // False if the ball has left the table shell or its state is not finite.
  // The caller should reset the ball when this happens. An inactive slot has
  // nothing wrong with it.
  [[nodiscard]] bool ballIsSane(std::size_t ball) const;
  [[nodiscard]] bool ballIsSane() const { return ballIsSane(0); }

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
  void buildDoorBank();
  void checkSensors(std::size_t slot, Vec2 position, Vec2 velocity);
  // Takes a free ball off the table for whatever element it is.
  void holdBall(std::size_t slot, table::ElementRef by, float speed);
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
    table::ElementRef heldBy{};
    // Where the ball was a slice ago, for the sensor lines. Not known for a
    // ball that has just been put somewhere by hand.
    Vec2 sensorFrom{};
    bool sensorFromKnown = false;
    // Bumpers the ball is touching, so each touch kicks once.
    std::array<bool, table::kBumpers.size()> touchingBumper{};
    // Rollovers the ball is currently on, so each pass reports once.
    std::array<bool, table::kRollovers.size()> onRollover{};
    // Holes that will take the ball. A hole disarms when it returns it.
    std::array<bool, table::kHoles.size()> holeArmed{};
  };
  std::array<BallSlot, table::kMaxBalls> balls_{};

  PhysicsEventList events_;

  // The bay doors are a body of their own, so that all three leave and
  // return to the table as one.
  b2BodyId doorBank_{};
  bool doorBankUp_ = true;
  bool kickbackArmed_ = false;
  // The next slice's touches are the restored world finding contacts that
  // were already there (restoreBalls).
  bool skipTouches_ = false;
};

}  // namespace pinball
