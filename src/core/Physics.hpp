#pragma once

#include <box2d/id.h>

#include <array>
#include <cstddef>

#include "core/Vec2.hpp"

namespace pinball {

// Owns the Box2D world: the static table and the ball. All values crossing
// this interface are in playfield pixels; Box2D itself runs in metres.
class PhysicsWorld {
 public:
  static constexpr float kPixelsPerMeter = 100.0F;
  // In-plane gravity of the inclined table, in m/s^2.
  static constexpr float kGravity = 9.5F;
  // Hard cap on ball speed in m/s. Keeps continuous collision reliable.
  static constexpr float kMaxBallSpeed = 40.0F;

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
  static constexpr float kPlungerMinSpeed = 3.0F;   // m/s, lightest tap.
  static constexpr float kPlungerMaxSpeed = 17.0F;  // m/s, full draw.

  void setPlunger(bool held) noexcept;
  // How far the plunger is drawn back, in pixels: 0 at rest.
  float plungerPull() const noexcept { return plungerPull_; }

  // Opens or closes the one-way gate at the top of the launch lane.
  void setLaneGateClosed(bool closed);
  bool laneGateClosed() const noexcept { return laneGateClosed_; }

  // Advances by one fixed step of stepSeconds.
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

  b2BodyId laneGate_{};
  bool laneGateClosed_ = false;
};

}  // namespace pinball
