#include "core/Physics.hpp"

#include <box2d/box2d.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

#include "core/TableLayout.hpp"

namespace pinball {

namespace {

constexpr int kSubSteps = 4;

constexpr float kWallFriction = 0.1F;
constexpr float kWallRestitution = 0.45F;
constexpr float kBallFriction = 0.1F;
constexpr float kBallRestitution = 0.3F;
constexpr float kBallDensity = 8.0F;

// Flipper rubber: grippy and a little lively.
constexpr float kFlipperFriction = 0.6F;
constexpr float kFlipperRestitution = 0.35F;

constexpr float toMeters(float pixels) noexcept {
  return pixels / PhysicsWorld::kPixelsPerMeter;
}
constexpr float toPixels(float meters) noexcept {
  return meters * PhysicsWorld::kPixelsPerMeter;
}
constexpr b2Vec2 toMeters(Vec2 p) noexcept { return {toMeters(p.x), toMeters(p.y)}; }

bool isFinite(Vec2 v) noexcept { return std::isfinite(v.x) && std::isfinite(v.y); }

b2ShapeDef wallShapeDef() {
  b2ShapeDef def = b2DefaultShapeDef();
  def.material.friction = kWallFriction;
  def.material.restitution = kWallRestitution;
  return def;
}

}  // namespace

PhysicsWorld::PhysicsWorld() {
  b2WorldDef def = b2DefaultWorldDef();
  // y grows downward in playfield space, so gravity is positive y.
  def.gravity = {0.0F, kGravity};
  def.maximumLinearSpeed = kMaxBallSpeed;
  def.enableContinuous = true;
  world_ = b2CreateWorld(&def);
  // Box2D returns a null id when its world table is full. Every later call
  // would dereference it, so stop here.
  if (!b2World_IsValid(world_)) {
    throw std::runtime_error("could not create the physics world");
  }

  buildTable();
  buildBall();
  buildFlippers();
}

PhysicsWorld::~PhysicsWorld() {
  if (b2World_IsValid(world_)) {
    // Destroys every body, shape and joint in the world.
    b2DestroyWorld(world_);
  }
}

void PhysicsWorld::buildTable() {
  b2BodyDef bodyDef = b2DefaultBodyDef();
  bodyDef.type = b2_staticBody;
  table_ = b2CreateBody(world_, &bodyDef);

  // Outer shell: one closed chain, which avoids ghost collisions at the joints.
  const std::vector<Vec2> outline = table::outerBoundary();
  std::vector<b2Vec2> points;
  points.reserve(outline.size());
  for (const Vec2 p : outline) {
    points.push_back(toMeters(p));
  }
  b2SurfaceMaterial material = b2DefaultSurfaceMaterial();
  material.friction = kWallFriction;
  material.restitution = kWallRestitution;
  b2ChainDef chainDef = b2DefaultChainDef();
  chainDef.points = points.data();
  chainDef.pointCount = static_cast<int>(points.size());
  chainDef.materials = &material;
  chainDef.materialCount = 1;
  chainDef.isLoop = true;
  b2CreateChain(table_, &chainDef);

  const b2ShapeDef shapeDef = wallShapeDef();
  for (const table::Capsule& wall : table::kWalls) {
    const b2Capsule capsule{toMeters(wall.a), toMeters(wall.b), toMeters(wall.radius)};
    b2CreateCapsuleShape(table_, &shapeDef, &capsule);
  }
  for (const table::Triangle& sling : table::kSlingshots) {
    b2Vec2 corners[3];
    for (int i = 0; i < 3; ++i) {
      corners[i] = toMeters(sling.points[static_cast<std::size_t>(i)]);
    }
    const b2Hull hull = b2ComputeHull(corners, 3);
    if (hull.count == 3) {
      const b2Polygon polygon = b2MakePolygon(&hull, toMeters(sling.cornerRadius));
      b2CreatePolygonShape(table_, &shapeDef, &polygon);
    }
  }
}

void PhysicsWorld::buildBall() {
  b2BodyDef bodyDef = b2DefaultBodyDef();
  bodyDef.type = b2_dynamicBody;
  bodyDef.position = b2ToPos(toMeters(table::kBallStart));
  // The ball is free to spin. A rolling disc accelerates down a slope at 2/3
  // of the sliding rate, close to the 5/7 of a real rolling sphere.
  // Continuous collision against everything, so a fast ball cannot tunnel.
  bodyDef.isBullet = true;
  bodyDef.enableSleep = false;
  ball_ = b2CreateBody(world_, &bodyDef);

  b2ShapeDef shapeDef = b2DefaultShapeDef();
  shapeDef.density = kBallDensity;
  shapeDef.material.friction = kBallFriction;
  shapeDef.material.restitution = kBallRestitution;
  const b2Circle circle{{0.0F, 0.0F}, toMeters(table::kBallRadius)};
  b2CreateCircleShape(ball_, &shapeDef, &circle);
}

void PhysicsWorld::buildFlippers() {
  b2ShapeDef shapeDef = b2DefaultShapeDef();
  shapeDef.material.friction = kFlipperFriction;
  shapeDef.material.restitution = kFlipperRestitution;

  // In the flipper's own frame the pivot is the origin and the bat lies along
  // +x: a large circle at the pivot, a small one at the tip, and a tapered
  // quad joining them.
  const float length = toMeters(table::kFlipperLength);
  const float baseRadius = toMeters(table::kFlipperBaseRadius);
  const float tipRadius = toMeters(table::kFlipperTipRadius);
  const b2Circle base{{0.0F, 0.0F}, baseRadius};
  const b2Circle tip{{length, 0.0F}, tipRadius};
  const b2Vec2 quad[4] = {
      {0.0F, -baseRadius}, {length, -tipRadius}, {length, tipRadius}, {0.0F, baseRadius}};
  const b2Hull hull = b2ComputeHull(quad, 4);

  for (std::size_t i = 0; i < flippers_.size(); ++i) {
    const table::Flipper& layout = table::kFlippers[i];
    FlipperState& flipper = flippers_[i];
    flipper.angle = layout.restAngle;

    // Kinematic: the bat follows its commanded angle exactly and is never
    // pushed back by the ball, like a solenoid-driven flipper.
    b2BodyDef bodyDef = b2DefaultBodyDef();
    bodyDef.type = b2_kinematicBody;
    bodyDef.position = b2ToPos(toMeters(layout.pivot));
    bodyDef.rotation = b2MakeRot(flipper.angle);
    bodyDef.enableSleep = false;
    flipper.body = b2CreateBody(world_, &bodyDef);

    b2CreateCircleShape(flipper.body, &shapeDef, &base);
    b2CreateCircleShape(flipper.body, &shapeDef, &tip);
    if (hull.count == 4) {
      const b2Polygon bat = b2MakePolygon(&hull, 0.0F);
      b2CreatePolygonShape(flipper.body, &shapeDef, &bat);
    }
  }
}

void PhysicsWorld::setFlippers(bool leftHeld, bool rightHeld) noexcept {
  flippers_[table::kLeftFlipperIndex].held = leftHeld;
  flippers_[table::kRightFlipperIndex].held = rightHeld;
}

float PhysicsWorld::flipperAngle(std::size_t index) const noexcept {
  return index < flippers_.size() ? flippers_[index].angle : 0.0F;
}

void PhysicsWorld::driveFlippers(float stepSeconds) {
  for (std::size_t i = 0; i < flippers_.size(); ++i) {
    const table::Flipper& layout = table::kFlippers[i];
    FlipperState& flipper = flippers_[i];

    const float goal = flipper.held ? layout.activeAngle : layout.restAngle;
    const float speed = flipper.held ? kFlipperUpSpeed : kFlipperDownSpeed;
    const float maxDelta = speed * stepSeconds;
    const float delta = std::clamp(goal - flipper.angle, -maxDelta, maxDelta);
    flipper.angle += delta;

    // The angle is tracked here and commanded as an absolute target every
    // step, so it cannot drift or overshoot its limits. Box2D derives the
    // velocity that carries the ball from the target.
    const b2WorldTransform target{b2ToPos(toMeters(layout.pivot)), b2MakeRot(flipper.angle)};
    b2Body_SetTargetTransform(flipper.body, target, stepSeconds, true);
  }
}

void PhysicsWorld::step(float stepSeconds) {
  if (!std::isfinite(stepSeconds) || stepSeconds <= 0.0F || stepSeconds > kMaxStepSeconds) {
    return;
  }
  driveFlippers(stepSeconds);
  b2World_Step(world_, stepSeconds, kSubSteps);
}

Vec2 PhysicsWorld::ballPosition() const {
  const b2Pos p = b2Body_GetPosition(ball_);
  return {toPixels(static_cast<float>(p.x)), toPixels(static_cast<float>(p.y))};
}

Vec2 PhysicsWorld::ballVelocity() const {
  const b2Vec2 v = b2Body_GetLinearVelocity(ball_);
  return {toPixels(v.x), toPixels(v.y)};
}

float PhysicsWorld::ballAngle() const {
  return b2Rot_GetAngle(b2Body_GetRotation(ball_));
}

void PhysicsWorld::resetBall(Vec2 position, Vec2 velocity) {
  if (!isFinite(position) || !isFinite(velocity)) {
    return;
  }
  b2Body_SetTransform(ball_, b2ToPos(toMeters(position)), b2MakeRot(0.0F));
  b2Body_SetLinearVelocity(ball_, toMeters(velocity));
  b2Body_SetAngularVelocity(ball_, 0.0F);
  b2Body_SetAwake(ball_, true);
}

bool PhysicsWorld::ballIsSane() const {
  const Vec2 p = ballPosition();
  const Vec2 v = ballVelocity();
  if (!isFinite(p) || !isFinite(v)) {
    return false;
  }
  constexpr float kMargin = 2.0F * table::kBallRadius;
  return p.x > table::kWallLeft - kMargin && p.x < table::kWallRight + kMargin &&
         p.y > -kMargin && p.y < table::kFloorY + kMargin;
}

}  // namespace pinball
