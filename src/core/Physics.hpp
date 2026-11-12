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
  enum class Type : std::uint8_t { Hit };
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
  // Hard cap on ball speed in m/s. Keeps continuous collision reliable: at
  // 120 Hz this is 25 px per step, about one ball diameter.
  static constexpr float kMaxBallSpeed = 30.0F;
  // Longest step accepted, in seconds. Larger values make the solver diverge.
  static constexpr float kMaxStepSeconds = 0.1F;

  // Throws std::runtime_error if the Box2D world cannot be created.
  PhysicsWorld();
  ~PhysicsWorld();
  PhysicsWorld(const PhysicsWorld&) = delete;
  PhysicsWorld& operator=(const PhysicsWorld&) = delete;

  // Flipper stroke speeds in radians per second. A real flipper completes its
  // stroke in roughly 50 ms and falls back more slowly.
  static constexpr float kFlipperUpSpeed = 22.0F;
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
  // Most events kept per step. Further events in the same step are dropped.
  static constexpr std::size_t kMaxEventsPerStep = 32;

  // Events from the most recent step. Cleared at the start of each step.
  const std::vector<PhysicsEvent>& events() const noexcept { return events_; }

  // Advances by one fixed step. Ignored unless 0 < stepSeconds <= kMaxStepSeconds.
  void step(float stepSeconds);

  Vec2 ballPosition() const;
  Vec2 ballVelocity() const;
  float ballAngle() const;

  // Teleports the ball and sets its velocity (pixels and pixels per second).
  // Non-finite input is ignored.
  void resetBall(Vec2 position, Vec2 velocity);

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
  void drivePlunger(float stepSeconds);

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

  b2BodyId laneGate_{};
  bool laneGateClosed_ = false;
};

}  // namespace pinball
