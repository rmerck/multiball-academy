#include "core/Physics.hpp"

#include <box2d/box2d.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "core/TableLayout.hpp"

namespace pinball {

namespace {

// Solver sub-steps within each slice.
constexpr int kSubSteps = 2;

constexpr float kWallFriction = 0.1F;
constexpr float kWallRestitution = 0.45F;
constexpr float kBallFriction = 0.1F;
constexpr float kBallRestitution = 0.3F;
constexpr float kBallDensity = 8.0F;

// Flipper rubber: grippy and a little lively.
constexpr float kFlipperFriction = 0.6F;
constexpr float kFlipperRestitution = 0.35F;

// Bumper skirts are rubber rings.
constexpr float kBumperRestitution = 0.6F;

// Element references ride in Box2D's shape user data as an integer packed
// into the pointer value. No real pointer is ever stored or dereferenced.
void* packElement(table::ElementRef ref) noexcept {
  const std::uintptr_t value = (static_cast<std::uintptr_t>(ref.kind) << 8U) |
                               static_cast<std::uintptr_t>(ref.index);
  return reinterpret_cast<void*>(value);  // NOLINT: integer tag, not a pointer.
}

table::ElementRef unpackElement(const void* data) noexcept {
  const auto value = reinterpret_cast<std::uintptr_t>(data);  // NOLINT: integer tag.
  const auto kindValue = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
  const auto index = static_cast<std::uint8_t>(value & 0xFFU);
  // Validate before trusting: only known kinds with in-range indices pass.
  // Rollovers and holes are detected by position and are never shape tags.
  for (const table::ElementKind kind :
       {table::ElementKind::Bumper, table::ElementKind::Slingshot, table::ElementKind::Target}) {
    if (kindValue == static_cast<std::uint8_t>(kind)) {
      const table::ElementRef ref{kind, index};
      return table::isValid(ref) ? ref : table::ElementRef{};
    }
  }
  return {};
}

// Slingshot and target faces are rubber.
constexpr float kRubberRestitution = 0.55F;

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
  // Contacts closing slower than this do not bounce. Keeps a ball rolling
  // from the inlane guide onto the flipper from hopping off the pivot.
  def.restitutionThreshold = 1.5F;
  world_ = b2CreateWorld(&def);
  // Box2D returns a null id when its world table is full. Every later call
  // would dereference it, so stop here.
  if (!b2World_IsValid(world_)) {
    throw std::runtime_error("could not create the physics world");
  }

  buildTable();
  buildBall();
  buildFlippers();
  buildPlunger();
  buildBumpers();
  buildTargets();
  holeArmed_.fill(true);
  events_.reserve(kMaxEventsPerStep);
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
  const auto addTriangle = [this](const table::Triangle& triangle, const b2ShapeDef& def) {
    b2Vec2 corners[3];
    for (std::size_t i = 0; i < 3; ++i) {
      corners[i] = toMeters(triangle.points[i]);
    }
    const b2Hull hull = b2ComputeHull(corners, 3);
    if (hull.count == 3) {
      const b2Polygon polygon = b2MakePolygon(&hull, toMeters(triangle.cornerRadius));
      b2CreatePolygonShape(table_, &def, &polygon);
    }
  };

  b2ShapeDef slingDef = wallShapeDef();
  slingDef.material.restitution = kRubberRestitution;
  for (std::size_t i = 0; i < table::kSlingshots.size(); ++i) {
    slingDef.userData =
        packElement({table::ElementKind::Slingshot, static_cast<std::uint8_t>(i)});
    addTriangle(table::kSlingshots[i], slingDef);
  }
  addTriangle(table::kMedalIsland, shapeDef);
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
  // Off by default in Box2D. Only one shape of a pair needs it.
  shapeDef.enableContactEvents = true;
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
    // slice, so error cannot accumulate and the limits cannot be overshot.
    // Box2D derives the velocity that carries the ball from the target; the
    // body follows to within about a thousandth of a radian.
    const b2WorldTransform target{b2ToPos(toMeters(layout.pivot)), b2MakeRot(flipper.angle)};
    b2Body_SetTargetTransform(flipper.body, target, stepSeconds, true);
  }
}

void PhysicsWorld::buildPlunger() {
  // Kinematic block whose top face carries the ball. Its origin is the centre
  // of that top face.
  b2BodyDef bodyDef = b2DefaultBodyDef();
  bodyDef.type = b2_kinematicBody;
  bodyDef.position = b2ToPos(toMeters(Vec2{table::kLaneCenterX, table::kPlungerRestTopY}));
  bodyDef.enableSleep = false;
  plunger_ = b2CreateBody(world_, &bodyDef);

  b2ShapeDef shapeDef = b2DefaultShapeDef();
  shapeDef.material.friction = 0.4F;
  // Box2D takes the larger restitution of the two shapes, so the ball's own
  // value governs the rebound whatever is set here.
  shapeDef.material.restitution = 0.0F;
  const float halfWidth = toMeters(table::kPlungerWidth) * 0.5F;
  const float halfHeight = toMeters(table::kPlungerHeight) * 0.5F;
  const b2Polygon block =
      b2MakeOffsetBox(halfWidth, halfHeight, {0.0F, halfHeight}, b2MakeRot(0.0F));
  b2CreatePolygonShape(plunger_, &shapeDef, &block);

  // Lane gate on its own body so it can be switched on and off.
  b2BodyDef gateDef = b2DefaultBodyDef();
  gateDef.type = b2_staticBody;
  laneGate_ = b2CreateBody(world_, &gateDef);
  const b2ShapeDef gateShape = wallShapeDef();
  const b2Capsule gate{toMeters(table::kLaneGate.a), toMeters(table::kLaneGate.b),
                       toMeters(table::kLaneGate.radius)};
  b2CreateCapsuleShape(laneGate_, &gateShape, &gate);
  b2Body_Disable(laneGate_);
  laneGateClosed_ = false;
}

void PhysicsWorld::buildBumpers() {
  b2ShapeDef shapeDef = b2DefaultShapeDef();
  shapeDef.material.friction = kWallFriction;
  shapeDef.material.restitution = kBumperRestitution;
  for (std::size_t i = 0; i < table::kBumpers.size(); ++i) {
    const table::Circle& c = table::kBumpers[i].shape;
    shapeDef.userData =
        packElement({table::ElementKind::Bumper, static_cast<std::uint8_t>(i)});
    const b2Circle circle{toMeters(c.center), toMeters(c.radius)};
    b2CreateCircleShape(table_, &shapeDef, &circle);
  }
}

void PhysicsWorld::buildTargets() {
  b2ShapeDef shapeDef = wallShapeDef();
  shapeDef.material.restitution = kRubberRestitution;
  for (std::size_t i = 0; i < table::kTargets.size(); ++i) {
    const table::Capsule& c = table::kTargets[i].shape;
    shapeDef.userData = packElement({table::ElementKind::Target, static_cast<std::uint8_t>(i)});
    const b2Capsule capsule{toMeters(c.a), toMeters(c.b), toMeters(c.radius)};
    b2CreateCapsuleShape(table_, &shapeDef, &capsule);
  }
}

bool PhysicsWorld::kickFromSlingshot(const table::Triangle& sling) {
  // The kicker is the long face from the top corner to the inner bottom
  // corner (points 0 and 2). Its normal points away from the third corner.
  const b2Vec2 top = toMeters(sling.points[0]);
  const b2Vec2 bottom = toMeters(sling.points[2]);
  const b2Vec2 back = toMeters(sling.points[1]);
  const b2Vec2 face{bottom.x - top.x, bottom.y - top.y};
  const float faceLength = std::sqrt(face.x * face.x + face.y * face.y);
  if (!(faceLength > 1e-6F)) {
    return false;
  }
  b2Vec2 normal{face.y / faceLength, -face.x / faceLength};
  if ((back.x - top.x) * normal.x + (back.y - top.y) * normal.y > 0.0F) {
    normal = {-normal.x, -normal.y};
  }

  const b2Pos ballPos = b2Body_GetPosition(ball_);
  const b2Vec2 rel{static_cast<float>(ballPos.x) - top.x, static_cast<float>(ballPos.y) - top.y};
  const float along = (rel.x * face.x + rel.y * face.y) / (faceLength * faceLength);
  const float outward = rel.x * normal.x + rel.y * normal.y;
  // Only the middle of the face kicks, and only a ball in front of it. The
  // back and bottom edges are plain rubber.
  if (along < 0.12F || along > 0.88F || outward <= 0.0F) {
    return false;
  }

  b2Vec2 v = b2Body_GetLinearVelocity(ball_);
  const float inward = std::min(v.x * normal.x + v.y * normal.y, 0.0F);
  v.x += normal.x * (kSlingshotKickSpeed - inward);
  v.y += normal.y * (kSlingshotKickSpeed - inward);
  b2Body_SetLinearVelocity(ball_, v);
  return true;
}

void PhysicsWorld::kickFromBumper(const table::Bumper& bumper) {
  // Push the ball straight away from the bumper's centre, on top of the
  // bounce the solver already gave it.
  const b2Vec2 center = toMeters(bumper.shape.center);
  const b2Pos ball = b2Body_GetPosition(ball_);
  b2Vec2 away{static_cast<float>(ball.x) - center.x, static_cast<float>(ball.y) - center.y};
  const float length = std::sqrt(away.x * away.x + away.y * away.y);
  if (!(length > 1e-6F)) {
    return;
  }
  away.x /= length;
  away.y /= length;

  b2Vec2 v = b2Body_GetLinearVelocity(ball_);
  // Drop any remaining motion into the bumper, then add the kick.
  const float inward = std::min(v.x * away.x + v.y * away.y, 0.0F);
  v.x += away.x * (kBumperKickSpeed - inward);
  v.y += away.y * (kBumperKickSpeed - inward);
  b2Body_SetLinearVelocity(ball_, v);
}

void PhysicsWorld::collectEvents() {
  const b2ContactEvents contacts = b2World_GetContactEvents(world_);
  for (int i = 0; i < contacts.beginCount; ++i) {
    const b2ContactBeginTouchEvent& touch = contacts.beginEvents[i];
    if (!b2Shape_IsValid(touch.shapeIdA) || !b2Shape_IsValid(touch.shapeIdB)) {
      continue;
    }
    // One of the two shapes is the ball; the other may be a tagged element.
    const bool ballIsA = B2_ID_EQUALS(b2Shape_GetBody(touch.shapeIdA), ball_);
    const bool ballIsB = B2_ID_EQUALS(b2Shape_GetBody(touch.shapeIdB), ball_);
    if (ballIsA == ballIsB) {
      continue;
    }
    const b2ShapeId other = ballIsA ? touch.shapeIdB : touch.shapeIdA;
    const table::ElementRef element = unpackElement(b2Shape_GetUserData(other));
    if (element.kind == table::ElementKind::None) {
      continue;
    }

    const b2Vec2 v = b2Body_GetLinearVelocity(ball_);
    const float speed = toPixels(std::sqrt(v.x * v.x + v.y * v.y));
    if (element.kind == table::ElementKind::Bumper) {
      kickFromBumper(table::kBumpers[element.index]);
    } else if (element.kind == table::ElementKind::Slingshot) {
      // A touch on a passive edge of the slingshot is not an event.
      if (!kickFromSlingshot(table::kSlingshots[element.index])) {
        continue;
      }
    }
    pushEvent(PhysicsEvent::Type::Hit, element, speed);
  }
}

void PhysicsWorld::pushEvent(PhysicsEvent::Type type, table::ElementRef element, float speed) {
  if (events_.size() < kMaxEventsPerStep) {
    events_.push_back({type, element, speed});
  }
}

void PhysicsWorld::checkZones() {
  if (ballHeld_) {
    return;
  }
  const Vec2 p = ballPosition();
  const Vec2 v = ballVelocity();
  const float speed = std::sqrt(v.x * v.x + v.y * v.y);
  const auto distanceTo = [p](Vec2 c) {
    const float dx = p.x - c.x;
    const float dy = p.y - c.y;
    return std::sqrt(dx * dx + dy * dy);
  };

  for (std::size_t i = 0; i < table::kRollovers.size(); ++i) {
    const bool inside = distanceTo(table::kRollovers[i].zone.center) <
                        table::kRollovers[i].zone.radius;
    if (inside && !onRollover_[i]) {
      pushEvent(PhysicsEvent::Type::Enter,
                {table::ElementKind::Rollover, static_cast<std::uint8_t>(i)}, speed);
    }
    onRollover_[i] = inside;
  }

  for (std::size_t i = 0; i < table::kHoles.size(); ++i) {
    const table::Hole& hole = table::kHoles[i];
    const float distance = distanceTo(hole.center);
    if (!holeArmed_[i]) {
      holeArmed_[i] = distance > table::kHoleRearmDistance;
      continue;
    }
    if (distance >= table::kHoleCaptureRadius) {
      continue;
    }
    if (hole.type == table::HoleType::Ramp && v.y > -table::kRampMinUpwardSpeed) {
      continue;
    }
    // Take the ball off the table. Disabling the body removes it from
    // collision until it is released.
    ballHeld_ = true;
    b2Body_SetLinearVelocity(ball_, {0.0F, 0.0F});
    b2Body_SetAngularVelocity(ball_, 0.0F);
    b2Body_Disable(ball_);
    pushEvent(PhysicsEvent::Type::Capture,
              {table::ElementKind::Hole, static_cast<std::uint8_t>(i)}, speed);
    return;
  }
}

void PhysicsWorld::releaseBall(Vec2 position, Vec2 velocity) {
  if (!ballHeld_ || !isFinite(position) || !isFinite(velocity)) {
    return;
  }
  b2Body_Enable(ball_);
  ballHeld_ = false;
  resetBall(position, velocity);
  // Any hole the ball now sits in must not swallow it again at once.
  for (std::size_t i = 0; i < table::kHoles.size(); ++i) {
    const float dx = position.x - table::kHoles[i].center.x;
    const float dy = position.y - table::kHoles[i].center.y;
    if (std::sqrt(dx * dx + dy * dy) <= table::kHoleRearmDistance) {
      holeArmed_[i] = false;
    }
  }
}

void PhysicsWorld::setPlunger(bool held) noexcept { plungerHeld_ = held; }

void PhysicsWorld::setLaneGateClosed(bool closed) {
  if (closed == laneGateClosed_) {
    return;
  }
  if (closed) {
    b2Body_Enable(laneGate_);
  } else {
    b2Body_Disable(laneGate_);
  }
  laneGateClosed_ = closed;
}

void PhysicsWorld::drivePlunger(float stepSeconds) {
  if (plungerHeld_) {
    // Drawing back. The release speed tracks how far it has been drawn.
    const float drawSpeed = table::kPlungerTravel / kPlungerChargeSeconds;
    plungerPull_ = std::min(plungerPull_ + drawSpeed * stepSeconds, table::kPlungerTravel);
    const float charge = plungerPull_ / table::kPlungerTravel;
    plungerReleaseSpeed_ =
        toPixels(kPlungerMinSpeed + (kPlungerMaxSpeed - kPlungerMinSpeed) * charge);
  } else if (plungerPull_ > 0.0F) {
    plungerPull_ = std::max(plungerPull_ - plungerReleaseSpeed_ * stepSeconds, 0.0F);
  }

  const Vec2 top{table::kLaneCenterX, table::kPlungerRestTopY + plungerPull_};
  const b2WorldTransform target{b2ToPos(toMeters(top)), b2MakeRot(0.0F)};
  b2Body_SetTargetTransform(plunger_, target, stepSeconds, true);
}

void PhysicsWorld::step(float stepSeconds) {
  if (!std::isfinite(stepSeconds) || stepSeconds <= 0.0F || stepSeconds > kMaxStepSeconds) {
    return;
  }
  events_.clear();
  const float slice = stepSeconds / static_cast<float>(kSlicesPerStep);
  for (int i = 0; i < kSlicesPerStep; ++i) {
    driveFlippers(slice);
    drivePlunger(slice);
    b2World_Step(world_, slice, kSubSteps);
    collectEvents();
    clampBallSpeed();
    checkZones();
  }
}

void PhysicsWorld::clampBallSpeed() {
  // Box2D's own limit is applied inside the solver and a contact with a fast
  // kinematic body can still leave the ball above it, so enforce it here.
  const b2Vec2 v = b2Body_GetLinearVelocity(ball_);
  const float speedSquared = v.x * v.x + v.y * v.y;
  if (speedSquared > kMaxBallSpeed * kMaxBallSpeed) {
    const float scale = kMaxBallSpeed / std::sqrt(speedSquared);
    b2Body_SetLinearVelocity(ball_, {v.x * scale, v.y * scale});
  }
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
  if (ballHeld_) {
    // A reset always leaves a live ball on the table.
    b2Body_Enable(ball_);
    ballHeld_ = false;
  }
  b2Body_SetTransform(ball_, b2ToPos(toMeters(position)), b2MakeRot(0.0F));
  b2Body_SetLinearVelocity(ball_, toMeters(velocity));
  b2Body_SetAngularVelocity(ball_, 0.0F);
  b2Body_SetAwake(ball_, true);
}

void PhysicsWorld::pushBall(Vec2 velocityChange) {
  if (ballHeld_ || !isFinite(velocityChange)) {
    return;
  }
  const b2Vec2 v = b2Body_GetLinearVelocity(ball_);
  const b2Vec2 dv = toMeters(velocityChange);
  b2Body_SetLinearVelocity(ball_, {v.x + dv.x, v.y + dv.y});
  clampBallSpeed();
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
