#pragma once

#include <box2d/id.h>

#include "core/Vec2.hpp"

namespace pinball {

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

  b2WorldId world_{};
  b2BodyId table_{};
  b2BodyId ball_{};
};

}  // namespace pinball
