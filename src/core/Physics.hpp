#pragma once

#include <box2d/id.h>

#include <array>
#include <cstddef>
#include <vector>

#include "core/TableLayout.hpp"
#include "core/Vec2.hpp"

namespace pinball {

// Something that happened to the ball during a step.
struct PhysicsEvent {
  // Hit: touched a bumper, slingshot face or target.
  // Enter: rolled onto a rollover.
  // Capture: dropped into a hole. The ball is held until releaseBall().
  enum class Type : std::uint8_t { Hit, Enter, Capture };
  Type type = Type::Hit;
  table::ElementRef element;
  float speed = 0.0F;  // Ball speed just before the event, pixels per second.
};

// Owns the Box2D world: the static table and the ball. All values crossing
// this interface are in playfield pixels; Box2D itself runs in metres.
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
  float flipperAngle(std::size_t index) const noexcept;

  // Plunger. While held it draws back over kPlungerChargeSeconds; on release
  // it drives forward at a speed set by how far it was drawn back.
  static constexpr float kPlungerChargeSeconds = 1.0F;
  // These are plunger speeds in m/s. The ball leaves about 1.3 times faster,
  // because it rebounds off the moving face with its own restitution.
  static constexpr float kPlungerMinSpeed = 3.0F;   // Lightest tap.
  static constexpr float kPlungerMaxSpeed = 12.5F;  // Full draw.

  void setPlunger(bool held) noexcept;
  // How far the plunger is drawn back, in pixels: 0 at rest.
  float plungerPull() const noexcept { return plungerPull_; }

  // Opens or closes the one-way gate at the top of the launch lane.
  void setLaneGateClosed(bool closed);
  bool laneGateClosed() const noexcept { return laneGateClosed_; }

  // Speed added to the ball, in m/s, when it touches a bumper.
  static constexpr float kBumperKickSpeed = 5.5F;
  // Speed added along a slingshot's face normal, in m/s.
  static constexpr float kSlingshotKickSpeed = 4.5F;
  // Most events kept per step. Further events in the same step are dropped.
  static constexpr std::size_t kMaxEventsPerStep = 32;

  // Events from the most recent step. Cleared at the start of each step.
  const std::vector<PhysicsEvent>& events() const noexcept { return events_; }

  // Advances by one fixed step. Ignored unless 0 < stepSeconds <= kMaxStepSeconds.
  void step(float stepSeconds);

  Vec2 ballPosition() const;
  Vec2 ballVelocity() const;
  float ballAngle() const;

  // True while a hole is holding the ball off the playfield.
  bool ballHeld() const noexcept { return ballHeld_; }
  // Returns a held ball to play at position with velocity. The hole it was
  // in, and the hole at the exit, ignore it until it has moved clear.
  void releaseBall(Vec2 position, Vec2 velocity);

  // Teleports the ball and sets its velocity (pixels and pixels per second).
  // Non-finite input is ignored.
  void resetBall(Vec2 position, Vec2 velocity);

  // Adds velocity (pixels per second) to a ball in play. Ignored while the
  // ball is held or if the value is not finite.
  void pushBall(Vec2 velocityChange);

  // False if the ball has left the table shell or its state is not finite.
  // The caller should reset the ball when this happens.
  bool ballIsSane() const;

 private:
  void buildTable();
  void buildBall();
  void buildFlippers();
  void driveFlippers(float stepSeconds);
  void buildPlunger();
  void buildBumpers();
  void collectEvents();
  void kickFromBumper(const table::Bumper& bumper);
  void buildTargets();
  void checkZones();
  void pushEvent(PhysicsEvent::Type type, table::ElementRef element, float speed);
  // Kicks the ball if it is on the slingshot's kicker face. Returns whether it did.
  bool kickFromSlingshot(const table::Triangle& sling);
  void drivePlunger(float stepSeconds);
  void clampBallSpeed();

  struct FlipperState {
    b2BodyId body{};
    float angle = 0.0F;
    bool held = false;
  };

  b2WorldId world_{};
  b2BodyId table_{};
  b2BodyId ball_{};
  std::array<FlipperState, 2> flippers_{};

  b2BodyId plunger_{};
  float plungerPull_ = 0.0F;          // Pixels drawn back.
  float plungerReleaseSpeed_ = 0.0F;  // Pixels per second while driving forward.
  bool plungerHeld_ = false;

  std::vector<PhysicsEvent> events_;

  bool ballHeld_ = false;
  // Rollovers the ball is currently on, so each pass reports once.
  std::array<bool, table::kRollovers.size()> onRollover_{};
  // Holes that will take the ball. A hole disarms when it returns a ball.
  std::array<bool, table::kHoles.size()> holeArmed_{};

  b2BodyId laneGate_{};
  bool laneGateClosed_ = false;
};

}  // namespace pinball
