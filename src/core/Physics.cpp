#include "core/Physics.hpp"

#include <box2d/box2d.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>
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
  const std::uintptr_t value =
      (std::uintptr_t{std::to_underlying(ref.kind)} << 8U) | std::uintptr_t{ref.index};
  return reinterpret_cast<void*>(value);  // NOLINT: integer tag, not a pointer.
}

table::ElementRef unpackElement(const void* data) noexcept {
  const auto value = reinterpret_cast<std::uintptr_t>(data);  // NOLINT: integer tag.
  if ((value >> 16U) != 0U) {
    return {};  // Not one of our tags.
  }
  const auto kindValue = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
  const auto index = static_cast<std::uint8_t>(value & 0xFFU);
  // Validate before trusting: only known kinds with in-range indices pass.
  // Rollovers and holes are detected by position and are never shape tags.
  for (const table::ElementKind kind :
       {table::ElementKind::Bumper, table::ElementKind::Slingshot, table::ElementKind::Target,
        table::ElementKind::DropTarget, table::ElementKind::Drone, table::ElementKind::Standup,
        table::ElementKind::CaptiveBall}) {
    if (kindValue == std::to_underlying(kind)) {
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

// Marks ball shapes for the restitution rule below.
constexpr std::uint64_t kBallMaterial = 1U;
// Marks dead posts and vanes: metal that takes the bounce out of a ball.
constexpr std::uint64_t kDeadMaterial = 2U;

// Collision categories. Everything on the table keeps Box2D's default
// category (1) and collides with everything; the playfield balls have a
// category of their own, so that the captive ball can collide with them and
// with nothing else: its joint, not the channel's walls, keeps it in the
// channel, and at rest it touches nothing.
constexpr std::uint64_t kBallCategory = 0x2U;
constexpr std::uint64_t kCaptiveCategory = 0x4U;

// Box2D's own rule is the larger of the two values. Two balls get a value of
// their own, and anything against a dead post gets the post's. A pure
// function: Box2D may call it from any step.
float mixRestitution(float restitutionA, std::uint64_t materialA, float restitutionB,
                     std::uint64_t materialB) {
  if (materialA == kBallMaterial && materialB == kBallMaterial) {
    return PhysicsWorld::kBallBallRestitution;
  }
  if (materialA == kDeadMaterial || materialB == kDeadMaterial) {
    return table::kDeadPostRestitution;
  }
  return restitutionA > restitutionB ? restitutionA : restitutionB;
}

// Box2D's own rule is the geometric mean of the two values.
float mixFriction(float frictionA, std::uint64_t materialA, float frictionB,
                  std::uint64_t materialB) {
  if (materialA == kBallMaterial && materialB == kBallMaterial) {
    return PhysicsWorld::kBallBallFriction;
  }
  return std::sqrt(frictionA * frictionB);
}

b2ShapeDef wallShapeDef() {
  b2ShapeDef def = b2DefaultShapeDef();
  def.material.friction = kWallFriction;
  def.material.restitution = kWallRestitution;
  return def;
}

// Length of a vector, worked out in double so that the squares of even the
// largest float cannot overflow. Only exactly rounded operations, so every
// platform gets the same answer; the library's hypot is not held to that.
float lengthOf(float x, float y) noexcept {
  const double dx = static_cast<double>(x);
  const double dy = static_cast<double>(y);
  return static_cast<float>(std::sqrt(dx * dx + dy * dy));
}

}  // namespace

PhysicsEventList::PhysicsEventList() {
  events_.reserve(kMaxPerBall * table::kMaxBalls + kMaxBallHits);
}

void PhysicsEventList::clear() noexcept {
  events_.clear();
  perBall_.fill(0);
  ballHits_ = 0;
}

bool PhysicsEventList::push(const PhysicsEvent& event) {
  if (event.ball >= table::kMaxBalls || event.otherBall >= table::kMaxBalls) {
    return false;
  }
  std::size_t& used =
      event.type == PhysicsEvent::Type::BallHit ? ballHits_ : perBall_[event.ball];
  const std::size_t allowed =
      event.type == PhysicsEvent::Type::BallHit ? kMaxBallHits : kMaxPerBall;
  if (used >= allowed) {
    return false;
  }
  ++used;
  events_.push_back(event);
  return true;
}

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

  b2World_SetRestitutionCallback(world_, mixRestitution);
  b2World_SetFrictionCallback(world_, mixFriction);

  // The order of creation fixes Box2D's internal ids, and the order in which
  // it solves contacts follows from them. The first ball stays where it
  // always was in that order and the spare balls come last, so that adding
  // them changed nothing about a game with one ball.
  buildTable();
  buildBall(0, true);
  buildFlippers();
  buildPlunger();
  buildBumpers();
  buildTargets();
  for (std::size_t slot = 1; slot < balls_.size(); ++slot) {
    buildBall(slot, false);
  }
  buildDoorBank();
  buildStandups();
  buildDrone();
  buildPylon();
  buildCaptive();
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

  // Chains are one-sided and have no joints for a ball to catch on. Each is
  // wound so that the ball's side is the solid one.
  b2SurfaceMaterial material = b2DefaultSurfaceMaterial();
  material.friction = kWallFriction;
  material.restitution = kWallRestitution;
  const auto addLoop = [this, &material](std::span<const Vec2> outline) {
    std::vector<b2Vec2> points;
    points.reserve(outline.size());
    for (const Vec2 p : outline) {
      points.push_back(toMeters(p));
    }
    b2ChainDef chainDef = b2DefaultChainDef();
    chainDef.points = points.data();
    chainDef.pointCount = static_cast<int>(points.size());
    chainDef.materials = &material;
    chainDef.materialCount = 1;
    chainDef.isLoop = true;
    b2CreateChain(table_, &chainDef);
  };
  // An open chain: the first and last points are ghosts, which shape the
  // end normals and are never collided with. A ball whose centre is behind
  // a segment is ignored by it, so an open chain is a wall from one side
  // and nothing from the other.
  const auto addOneSided = [this](std::span<const Vec2> outline,
                                  const b2SurfaceMaterial& surface) {
    std::vector<b2Vec2> points;
    points.reserve(outline.size());
    for (const Vec2 p : outline) {
      points.push_back(toMeters(p));
    }
    b2ChainDef chainDef = b2DefaultChainDef();
    chainDef.points = points.data() + 1;
    chainDef.pointCount = static_cast<int>(points.size()) - 2;
    chainDef.ghost1 = points.front();
    chainDef.ghost2 = points.back();
    chainDef.materials = &surface;
    chainDef.materialCount = 1;
    chainDef.isLoop = false;
    b2CreateChain(table_, &chainDef);
  };
  addLoop(table::kShell);
  addLoop(table::kLaneIsland);
  // The inner arch is the launch lane's one-way gate: a launched ball comes
  // up behind it and passes through; a ball in play cannot pass back.
  addOneSided(table::kInnerArch, material);

  const b2ShapeDef shapeDef = wallShapeDef();
  for (const table::Capsule& wall : table::kAllWalls) {
    const b2Capsule capsule{toMeters(wall.a), toMeters(wall.b), toMeters(wall.radius)};
    b2CreateCapsuleShape(table_, &shapeDef, &capsule);
  }
  b2ShapeDef deadDef = wallShapeDef();
  deadDef.material.restitution = table::kDeadPostRestitution;
  deadDef.material.userMaterialId = kDeadMaterial;
  for (const table::Capsule& post : table::kDeadPosts) {
    const b2Capsule capsule{toMeters(post.a), toMeters(post.b), toMeters(post.radius)};
    b2CreateCapsuleShape(table_, &deadDef, &capsule);
  }
  addOneSided(table::kLaneGate, material);
  addOneSided(table::kSlingVaneLeft, deadDef.material);
  addOneSided(table::kSlingVaneRight, deadDef.material);

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
}

void PhysicsWorld::buildBall(std::size_t slot, bool enabled) {
  BallSlot& ball = balls_[slot];
  ball.active = enabled;
  ball.holeArmed.fill(true);

  b2BodyDef bodyDef = b2DefaultBodyDef();
  bodyDef.type = b2_dynamicBody;
  bodyDef.position = b2ToPos(toMeters(table::kBallStart));
  // The ball is free to spin. A rolling disc accelerates down a slope at 2/3
  // of the sliding rate, close to the 5/7 of a real rolling sphere.
  // Continuous collision against everything, so a fast ball cannot tunnel.
  bodyDef.isBullet = true;
  bodyDef.enableSleep = false;
  // A spare ball is in no collision and no solver set until it is activated.
  bodyDef.isEnabled = enabled;
  ball.body = b2CreateBody(world_, &bodyDef);

  b2ShapeDef shapeDef = b2DefaultShapeDef();
  shapeDef.density = kBallDensity;
  shapeDef.material.friction = kBallFriction;
  shapeDef.material.restitution = kBallRestitution;
  // Off by default in Box2D. Only one shape of a pair needs it.
  shapeDef.enableContactEvents = true;
  shapeDef.material.userMaterialId = kBallMaterial;
  shapeDef.filter.categoryBits = kBallCategory;
  const b2Circle circle{{0.0F, 0.0F}, toMeters(table::kBallRadius)};
  ball.shape = b2CreateCircleShape(ball.body, &shapeDef, &circle);
}

void PhysicsWorld::buildFlippers() {
  b2ShapeDef shapeDef = b2DefaultShapeDef();
  shapeDef.material.friction = kFlipperFriction;
  shapeDef.material.restitution = kFlipperRestitution;

  // In the flipper's own frame the pivot is the origin and the bat lies along
  // +x: a large circle at the pivot, a small one at the tip, and a tapered
  // quad joining them.
  for (std::size_t i = 0; i < flippers_.size(); ++i) {
    const table::Flipper& layout = table::kFlippers[i];
    FlipperState& flipper = flippers_[i];
    const float baseRadius = toMeters(layout.baseRadius);
    const float tipRadius = toMeters(layout.tipRadius);
    const b2Circle base{{0.0F, 0.0F}, baseRadius};
    const float length = toMeters(layout.length);
    const b2Circle tip{{length, 0.0F}, tipRadius};
    const b2Vec2 quad[4] = {
        {0.0F, -baseRadius}, {length, -tipRadius}, {length, tipRadius}, {0.0F, baseRadius}};
    const b2Hull hull = b2ComputeHull(quad, 4);
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

void PhysicsWorld::setFlippers(bool leftHeld, bool rightHeld, bool deckAllowed) noexcept {
  flippers_[table::kLeftFlipperIndex].held = leftHeld;
  flippers_[table::kRightFlipperIndex].held = rightHeld;
  flippers_[table::kDeckFlipperIndex].held = rightHeld && deckAllowed;
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

void PhysicsWorld::buildDoorBank() {
  // The three bay doors on one static body: disabling it takes all three
  // off the table at once and enabling it puts them back, so the Hangar's
  // mouth is shut or wholly open and never open by one door.
  b2BodyDef bodyDef = b2DefaultBodyDef();
  bodyDef.type = b2_staticBody;
  doorBank_ = b2CreateBody(world_, &bodyDef);
  b2ShapeDef shapeDef = wallShapeDef();
  shapeDef.material.restitution = kRubberRestitution;
  for (std::size_t i = 0; i < table::kDropTargets.size(); ++i) {
    const table::Capsule& c = table::kDropTargets[i];
    shapeDef.userData =
        packElement({table::ElementKind::DropTarget, static_cast<std::uint8_t>(i)});
    const b2Capsule capsule{toMeters(c.a), toMeters(c.b), toMeters(c.radius)};
    b2CreateCapsuleShape(doorBank_, &shapeDef, &capsule);
  }
}

void PhysicsWorld::buildStandups() {
  // T-A-X-I keep the dead posts' bounce.
  b2ShapeDef deadDef = wallShapeDef();
  deadDef.material.restitution = table::kDeadPostRestitution;
  deadDef.material.userMaterialId = kDeadMaterial;
  for (std::size_t i = 0; i < table::kStandups.size(); ++i) {
    const table::Capsule& c = table::kStandups[i];
    b2ShapeDef def = deadDef;
    def.userData = packElement({table::ElementKind::Standup, static_cast<std::uint8_t>(i)});
    const b2Capsule capsule{toMeters(c.a), toMeters(c.b), toMeters(c.radius)};
    b2CreateCapsuleShape(table_, &def, &capsule);
  }
}

void PhysicsWorld::buildDrone() {
  // A kinematic hexagon, a corner down the table, moved by its target
  // transform each step like the flippers.
  b2BodyDef bodyDef = b2DefaultBodyDef();
  bodyDef.type = b2_kinematicBody;
  bodyDef.position = b2ToPos(toMeters(dronePosition_));
  bodyDef.enableSleep = false;
  drone_ = b2CreateBody(world_, &bodyDef);
  b2ShapeDef shapeDef = wallShapeDef();
  shapeDef.material.restitution = kRubberRestitution;
  shapeDef.userData = packElement({table::ElementKind::Drone, 0});
  // Corners at 90 + 60k degrees, written out (cos 30 = 0.8660254).
  const float r = toMeters(table::kDroneRadius);
  const float c = r * 0.8660254F;
  const b2Vec2 corners[6] = {{0.0F, r},  {c, r * 0.5F},  {c, -r * 0.5F},
                             {0.0F, -r}, {-c, -r * 0.5F}, {-c, r * 0.5F}};
  const b2Hull hull = b2ComputeHull(corners, 6);
  if (hull.count == 6) {
    const b2Polygon hexagon = b2MakePolygon(&hull, 0.0F);
    b2CreatePolygonShape(drone_, &shapeDef, &hexagon);
  }
}

bool PhysicsWorld::droneSpotClear(Vec2 at) const {
  const float reach = table::kDroneRadius + table::kBallRadius + 4.0F;
  for (std::size_t slot = 0; slot < balls_.size(); ++slot) {
    if (!balls_[slot].active || balls_[slot].held) {
      continue;
    }
    const Vec2 p = ballPosition(slot);
    const float dx = p.x - at.x;
    const float dy = p.y - at.y;
    if (dx * dx + dy * dy < reach * reach) {
      return false;
    }
  }
  return true;
}

void PhysicsWorld::buildPylon() {
  // A static body of its own, so that disabling it makes it flush.
  b2BodyDef bodyDef = b2DefaultBodyDef();
  bodyDef.type = b2_staticBody;
  bodyDef.isEnabled = false;
  pylon_ = b2CreateBody(world_, &bodyDef);
  b2ShapeDef shapeDef = wallShapeDef();
  shapeDef.material.restitution = table::kDeadPostRestitution;
  shapeDef.material.userMaterialId = kDeadMaterial;
  shapeDef.userData = packElement({table::ElementKind::Pylon, 0});
  const b2Circle circle{toMeters(table::kPylon.center), toMeters(table::kPylon.radius)};
  b2CreateCircleShape(pylon_, &shapeDef, &circle);
  pylonUp_ = false;
}

void PhysicsWorld::setPylon(bool up) {
  if (up == pylonUp_) {
    return;
  }
  pylonUp_ = up;
  if (up) {
    b2Body_Enable(pylon_);
  } else {
    b2Body_Disable(pylon_);
  }
}

bool PhysicsWorld::pylonSpotClear() const {
  const float reach = table::kPylon.radius + table::kBallRadius + 2.0F;
  for (std::size_t slot = 0; slot < balls_.size(); ++slot) {
    if (!balls_[slot].active || balls_[slot].held) {
      continue;
    }
    const Vec2 d = ballPosition(slot) - table::kPylon.center;
    if (d.x * d.x + d.y * d.y < reach * reach) {
      return false;
    }
  }
  return true;
}

void PhysicsWorld::driveDevices(float seconds, bool endOfStep) {
  // The Prop Wash drags a ball on it towards the disc's surface speed, by at
  // most the grip times the time (friction); the Air Pocket holds balls in
  // its reach while it pulls and kicks them away when it lets go.
  const bool still = propWashSpin_ == 0.0F;
  if (still && airPocket_ == AirPocket::Off) {
    return;
  }
  for (std::size_t slot = 0; slot < balls_.size(); ++slot) {
    BallSlot& ball = balls_[slot];
    if (!ball.active || ball.held) {
      continue;
    }
    const Vec2 p = ballPosition(slot);
    Vec2 v = ballVelocity(slot);
    bool changed = false;
    if (!still) {
      const Vec2 r = p - table::kPropWash.center;
      const float rr = table::kPropWash.radius;
      if (r.x * r.x + r.y * r.y < rr * rr) {
        // Clockwise on screen (y down): surface velocity = spin x (-r.y, r.x).
        const Vec2 surface{-propWashSpin_ * r.y, propWashSpin_ * r.x};
        const Vec2 slip = surface - v;
        const float slipSpeed = std::sqrt(slip.x * slip.x + slip.y * slip.y);
        const float most = table::kPropWashGrip * seconds;
        if (slipSpeed > 1.0e-3F) {
          const float scale = slipSpeed <= most ? 1.0F : most / slipSpeed;
          v = v + slip * scale;
          changed = true;
        }
      }
    }
    if (airPocket_ != AirPocket::Off) {
      const Vec2 r = table::kAirPocketCenter - p;
      const float reach = table::kAirPocketRadius;
      if (r.x * r.x + r.y * r.y < reach * reach) {
        if (airPocket_ == AirPocket::Pull && v.y > table::kAirPocketRisingLimit) {
          v = r * table::kAirPocketPull;
          changed = true;
        } else if (airPocket_ == AirPocket::Release && endOfStep) {
          v = airPocketKick_;
          ++airPocketCatches_;
          changed = true;
        }
      }
    }
    if (changed && isFinite(v)) {
      b2Body_SetLinearVelocity(ball.body, toMeters(v));
    }
  }
}

void PhysicsWorld::knockDroneDown(std::uint32_t steps) noexcept {
  droneDownSteps_ = steps;
  droneClock_ = 0;
  // Up again when the time is out (and its place is clear), not at the end
  // of the patrol cycle after it.
  droneKnocked_ = true;
  if (droneUp_) {
    droneUp_ = false;
    b2Body_Disable(drone_);
  }
}

void PhysicsWorld::driveDrone(float stepSeconds) {
  const auto steps = [stepSeconds](double seconds) {
    return static_cast<std::uint32_t>(seconds / static_cast<double>(stepSeconds) + 0.5);
  };
  const std::uint32_t patrol = steps(table::kDronePatrolSeconds);
  const std::uint32_t cycle = patrol + steps(table::kDroneDiveSeconds);
  const Vec2 start{table::kDroneLeftX, table::kDroneY};
  if (droneDownSteps_ > 0) {
    --droneDownSteps_;
    return;
  }
  if (!droneUp_) {
    // Down: rise at the start of the next patrol, or at once after being
    // knocked down, if the spot is clear.
    if (!droneKnocked_ && droneClock_ + 1U < cycle) {
      ++droneClock_;
      return;
    }
    if (!droneSpotClear(start)) {
      return;
    }
    droneClock_ = 0;
    dronePosition_ = start;
    b2Body_SetTransform(drone_, b2ToPos(toMeters(start)), b2Rot{1.0F, 0.0F});
    b2Body_SetLinearVelocity(drone_, {0.0F, 0.0F});
    b2Body_Enable(drone_);
    droneUp_ = true;
    droneKnocked_ = false;
    return;
  }
  ++droneClock_;
  if (droneClock_ >= patrol) {
    droneUp_ = false;
    b2Body_SetLinearVelocity(drone_, {0.0F, 0.0F});
    b2Body_Disable(drone_);
    return;
  }
  // Out and back, eased at both ends (smoothstep).
  const float t = static_cast<float>(droneClock_) / static_cast<float>(patrol);
  const float u = t < 0.5F ? t * 2.0F : 2.0F - t * 2.0F;
  const float eased = u * u * (3.0F - 2.0F * u);
  dronePosition_ = {table::kDroneLeftX + (table::kDroneRightX - table::kDroneLeftX) * eased,
                    table::kDroneY};
  const b2WorldTransform target{b2ToPos(toMeters(dronePosition_)), b2Rot{1.0F, 0.0F}};
  b2Body_SetTargetTransform(drone_, target, stepSeconds, true);
}

// --- The captive ball -------------------------------------------------------------
namespace {

// The channel's direction as a rotation: the joint's x axis runs up the
// channel. (0.8, 0.6) is exact, so no trigonometry is needed.
b2Rot captiveAxisRotation() noexcept {
  return b2NormalizeRot(b2Rot{table::kCaptiveAxis.x, table::kCaptiveAxis.y});
}

}  // namespace

void PhysicsWorld::buildCaptive() {
  // The joint's anchor: a static body with no shapes, so that making the
  // joint (which removes contacts between the bodies it joins) never
  // touches a contact of the table's.
  b2BodyDef anchorDef = b2DefaultBodyDef();
  anchorDef.type = b2_staticBody;
  captiveAnchor_ = b2CreateBody(world_, &anchorDef);

  // A dynamic ball of steel like the others, not a bullet: the playfield
  // balls are bullets and sweep against it, so neither passes through the
  // other. Its joint holds its rotation; it is never asleep.
  b2BodyDef bodyDef = b2DefaultBodyDef();
  bodyDef.type = b2_dynamicBody;
  bodyDef.position = b2ToPos(toMeters(table::kCaptiveHome));
  bodyDef.enableSleep = false;
  captive_ = b2CreateBody(world_, &bodyDef);

  b2ShapeDef shapeDef = b2DefaultShapeDef();
  shapeDef.density = kBallDensity;
  shapeDef.material.friction = kBallFriction;
  shapeDef.material.restitution = kBallRestitution;
  shapeDef.material.userMaterialId = kBallMaterial;
  shapeDef.enableContactEvents = true;
  shapeDef.filter.categoryBits = kCaptiveCategory;
  shapeDef.filter.maskBits = kBallCategory;
  shapeDef.userData = packElement({table::ElementKind::CaptiveBall, table::kCaptiveBallStruck});
  const b2Circle circle{{0.0F, 0.0F}, toMeters(table::kCaptiveBallRadius)};
  b2CreateCircleShape(captive_, &shapeDef, &circle);
  renewCaptiveJoint();
}

void PhysicsWorld::renewCaptiveJoint() {
  // Box2D keeps the impulses a joint applied in one step to start the next
  // with (warm starting), and gives no way to read or set them. A saved
  // game could therefore not carry the joint exactly. So the joint is made
  // afresh at the start of every step, in play and after a restore alike:
  // its state is then the captive body's state alone, which the save does
  // carry. (Within the step the four slices warm-start each other.) The
  // joint's id is freed and taken again, so it is the same every step.
  if (b2Joint_IsValid(captiveJoint_)) {
    b2DestroyJoint(captiveJoint_);
  }
  const b2Rot axis = captiveAxisRotation();
  b2PrismaticJointDef def = b2DefaultPrismaticJointDef();
  def.base.bodyIdA = captiveAnchor_;
  def.base.bodyIdB = captive_;
  def.base.localFrameA = b2Transform{toMeters(table::kCaptiveHome), axis};
  def.base.localFrameB = b2Transform{{0.0F, 0.0F}, axis};
  def.base.collideConnected = false;
  def.enableLimit = true;
  def.lowerTranslation = 0.0F;
  def.upperTranslation = toMeters(table::kCaptiveTravel);
  def.enableSpring = true;
  def.hertz = table::kCaptiveSpringHertz;
  def.dampingRatio = 0.0F;
  def.targetTranslation = 0.0F;
  captiveJoint_ = b2CreatePrismaticJoint(world_, &def);
}

void PhysicsWorld::putCaptiveHome() {
  b2Body_SetTransform(captive_, b2ToPos(toMeters(table::kCaptiveHome)), b2Rot{1.0F, 0.0F});
  b2Body_SetLinearVelocity(captive_, {0.0F, 0.0F});
  b2Body_SetAngularVelocity(captive_, 0.0F);
  captiveSwitchClosed_ = false;
  captivePeakSpeed_ = 0.0F;
}

Vec2 PhysicsWorld::captivePosition() const {
  const b2Pos p = b2Body_GetPosition(captive_);
  return {toPixels(static_cast<float>(p.x)), toPixels(static_cast<float>(p.y))};
}

Vec2 PhysicsWorld::captiveVelocity() const {
  const b2Vec2 v = b2Body_GetLinearVelocity(captive_);
  return {toPixels(v.x), toPixels(v.y)};
}

float PhysicsWorld::captiveTranslation() const {
  return table::captiveTranslationAt(captivePosition());
}

void PhysicsWorld::checkCaptive() {
  // Held to the playfield balls' speed cap, like them.
  const b2Vec2 v = b2Body_GetLinearVelocity(captive_);
  if (v.x * v.x + v.y * v.y > kMaxBallSpeed * kMaxBallSpeed) {
    const float speed = lengthOf(v.x, v.y);
    const float scale = std::isfinite(speed) ? kMaxBallSpeed / speed : 0.0F;
    b2Body_SetLinearVelocity(captive_, {v.x * scale, v.y * scale});
  }
  const Vec2 p = captivePosition();
  const float along = table::captiveTranslationAt(p);
  const float aside = table::captiveOffsetAt(p);
  // Written so that NaN fails it. Never seen in play: the tests count it.
  const bool inChannel = std::fabs(aside) <= table::kCaptiveSlack &&
                         along >= -table::kCaptiveSlack &&
                         along <= table::kCaptiveTravel + table::kCaptiveSlack &&
                         isFinite(captiveVelocity());
  if (!inChannel) {
    ++captiveResets_;
    putCaptiveHome();
    return;
  }
  if (along <= table::kCaptiveSwitchRearm) {
    // At the bottom of its stroke: the switch is open and the next stroke's
    // speed starts from nothing.
    captiveSwitchClosed_ = false;
    captivePeakSpeed_ = 0.0F;
    return;
  }
  const Vec2 velocity = captiveVelocity();
  captivePeakSpeed_ = std::max(captivePeakSpeed_, lengthOf(velocity.x, velocity.y));
  if (!captiveSwitchClosed_ && along >= table::kCaptiveSwitchAt) {
    captiveSwitchClosed_ = true;
    pushEvent(PhysicsEvent::Type::Hit, {table::ElementKind::CaptiveBall, table::kCaptiveBallSwitch},
              captivePeakSpeed_, captiveStriker_);
  }
}

void PhysicsWorld::setDoorBank(bool up) {
  if (up == doorBankUp_) {
    return;
  }
  doorBankUp_ = up;
  if (up) {
    b2Body_Enable(doorBank_);
  } else {
    b2Body_Disable(doorBank_);
  }
}

bool PhysicsWorld::kickFromSlingshot(std::size_t slot, const table::Triangle& sling) {
  const b2BodyId body = balls_[slot].body;
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

  const b2Pos ballPos = b2Body_GetPosition(body);
  const b2Vec2 rel{static_cast<float>(ballPos.x) - top.x, static_cast<float>(ballPos.y) - top.y};
  const float along = (rel.x * face.x + rel.y * face.y) / (faceLength * faceLength);
  const float outward = rel.x * normal.x + rel.y * normal.y;
  // Only the middle of the face kicks, and only a ball in front of it. The
  // back and bottom edges are plain rubber.
  if (along < 0.12F || along > 0.88F || outward <= 0.0F) {
    return false;
  }

  b2Vec2 v = b2Body_GetLinearVelocity(body);
  const float inward = std::min(v.x * normal.x + v.y * normal.y, 0.0F);
  v.x += normal.x * (kSlingshotKickSpeed - inward);
  v.y += normal.y * (kSlingshotKickSpeed - inward);
  b2Body_SetLinearVelocity(body, v);
  return true;
}

void PhysicsWorld::kickFromBumper(std::size_t slot, const table::Bumper& bumper) {
  const b2BodyId body = balls_[slot].body;
  // Push the ball straight away from the bumper's centre, on top of the
  // bounce the solver already gave it.
  const b2Vec2 center = toMeters(bumper.shape.center);
  const b2Pos ball = b2Body_GetPosition(body);
  b2Vec2 away{static_cast<float>(ball.x) - center.x, static_cast<float>(ball.y) - center.y};
  const float length = std::sqrt(away.x * away.x + away.y * away.y);
  if (!(length > 1e-6F)) {
    return;
  }
  away.x /= length;
  away.y /= length;

  b2Vec2 v = b2Body_GetLinearVelocity(body);
  // Drop any remaining motion into the bumper, then add the kick.
  const float inward = std::min(v.x * away.x + v.y * away.y, 0.0F);
  v.x += away.x * (kBumperKickSpeed - inward);
  v.y += away.y * (kBumperKickSpeed - inward);
  b2Body_SetLinearVelocity(body, v);
}

void PhysicsWorld::checkBumpers(std::size_t slot) {
  // A bumper fires when the ball actually reaches it, and not again until
  // that ball has moved clear.
  constexpr float kContactSlack = 0.75F;  // Pixels.
  constexpr float kClearDistance = 4.0F;
  auto& touching = balls_[slot].touchingBumper;
  const b2BodyId body = balls_[slot].body;
  const Vec2 p = ballPosition(slot);
  for (std::size_t i = 0; i < table::kBumpers.size(); ++i) {
    const table::Circle& c = table::kBumpers[i].shape;
    const float dx = p.x - c.center.x;
    const float dy = p.y - c.center.y;
    const float gap = std::sqrt(dx * dx + dy * dy) - c.radius - table::kBallRadius;
    if (touching[i]) {
      touching[i] = gap < kClearDistance;
      continue;
    }
    if (gap > kContactSlack) {
      continue;
    }
    touching[i] = true;
    const b2Vec2 v = b2Body_GetLinearVelocity(body);
    const float speed = toPixels(std::sqrt(v.x * v.x + v.y * v.y));
    kickFromBumper(slot, table::kBumpers[i]);
    pushEvent(PhysicsEvent::Type::Hit, {table::ElementKind::Bumper, static_cast<std::uint8_t>(i)},
              speed, slot);
  }
}

void PhysicsWorld::collectEvents() {
  const b2ContactEvents contacts = b2World_GetContactEvents(world_);
  for (int i = 0; i < contacts.beginCount; ++i) {
    const b2ContactBeginTouchEvent& touch = contacts.beginEvents[i];
    if (!b2Shape_IsValid(touch.shapeIdA) || !b2Shape_IsValid(touch.shapeIdB)) {
      continue;
    }
    // One of the two shapes is a ball; the other may be a tagged element,
    // or another ball.
    const std::size_t slotA = slotOf(b2Shape_GetBody(touch.shapeIdA));
    const std::size_t slotB = slotOf(b2Shape_GetBody(touch.shapeIdB));
    const bool ballIsA = slotA < balls_.size();
    const bool ballIsB = slotB < balls_.size();
    if (ballIsA && ballIsB) {
      if (slotA != slotB) {
        const b2Vec2 va = b2Body_GetLinearVelocity(balls_[slotA].body);
        const b2Vec2 vb = b2Body_GetLinearVelocity(balls_[slotB].body);
        pushEvent(PhysicsEvent::Type::BallHit, {}, toPixels(lengthOf(va.x - vb.x, va.y - vb.y)),
                  std::min(slotA, slotB), std::max(slotA, slotB));
      }
      continue;
    }
    if (!ballIsA && !ballIsB) {
      continue;
    }
    const std::size_t slot = ballIsA ? slotA : slotB;
    const b2BodyId body = balls_[slot].body;
    const b2ShapeId other = ballIsA ? touch.shapeIdB : touch.shapeIdA;
    const table::ElementRef element = unpackElement(b2Shape_GetUserData(other));
    if (element.kind == table::ElementKind::None) {
      continue;
    }

    const b2Vec2 v = b2Body_GetLinearVelocity(body);
    const float speed = toPixels(std::sqrt(v.x * v.x + v.y * v.y));
    if (element.kind == table::ElementKind::Bumper) {
      // Bumpers are handled by checkBumpers, on real contact. Box2D reports
      // a touch up to two pixels early, which would kick a ball that was
      // only passing close by.
      continue;
    }
    if (element.kind == table::ElementKind::Standup && element.index < 2) {
      // T and A are the rim feet: a stand-up face towards the field only. A
      // ball coming down the circuit lane behind them brushes the foot
      // without closing the switch.
      const table::Capsule& foot = table::kStandups[element.index];
      const Vec2 p = ballPosition(slot);
      const float cross = (foot.b.x - foot.a.x) * (p.y - foot.a.y) - (foot.b.y - foot.a.y) * (p.x - foot.a.x);
      // The field is to the right of the left foot (a below b: cross < 0)
      // and to the left of the right foot.
      const bool fieldSide = element.index == 0 ? cross < 0.0F : cross > 0.0F;
      if (!fieldSide) {
        continue;
      }
    }
    if (element.kind == table::ElementKind::CaptiveBall) {
      // The switch, when the captive ball reaches it, is this ball's.
      captiveStriker_ = static_cast<std::uint8_t>(slot);
    }
    if (element.kind == table::ElementKind::Slingshot) {
      // A touch on a passive edge of the slingshot is not an event.
      if (!kickFromSlingshot(slot, table::kSlingshots[element.index])) {
        continue;
      }
    }
    pushEvent(PhysicsEvent::Type::Hit, element, speed, slot);
  }
}

std::size_t PhysicsWorld::slotOf(b2BodyId body) const noexcept {
  for (std::size_t slot = 0; slot < balls_.size(); ++slot) {
    if (B2_ID_EQUALS(body, balls_[slot].body)) {
      return slot;
    }
  }
  return balls_.size();
}

void PhysicsWorld::pushEvent(PhysicsEvent::Type type, table::ElementRef element, float speed,
                             std::size_t slot, std::size_t otherSlot) {
  events_.push({type, element, speed, static_cast<std::uint8_t>(slot),
                static_cast<std::uint8_t>(otherSlot), 0});
}

void PhysicsWorld::holdBall(std::size_t slot, table::ElementRef by, float speed) {
  // Disabling the body removes it from collision until it is released.
  BallSlot& ball = balls_[slot];
  ball.held = true;
  ball.heldBy = by;
  ball.sensorFromKnown = false;
  b2Body_SetLinearVelocity(ball.body, {0.0F, 0.0F});
  b2Body_SetAngularVelocity(ball.body, 0.0F);
  b2Body_Disable(ball.body);
  pushEvent(PhysicsEvent::Type::Capture, by, speed, slot);
}

void PhysicsWorld::checkSensors(std::size_t slot, Vec2 position, Vec2 velocity) {
  BallSlot& ball = balls_[slot];
  const Vec2 from = ball.sensorFrom;
  const bool known = ball.sensorFromKnown;
  ball.sensorFrom = position;
  ball.sensorFromKnown = true;
  if (!known) {
    return;
  }
  for (std::size_t i = 0; i < table::kSensors.size(); ++i) {
    const table::SensorLine& line = table::kSensors[i];
    const Vec2 along = line.b - line.a;
    // Which side of the line each end of the ball's move is on.
    const float before = along.x * (from.y - line.a.y) - along.y * (from.x - line.a.x);
    const float after = along.x * (position.y - line.a.y) - along.y * (position.x - line.a.x);
    if ((before > 0.0F) == (after > 0.0F)) {
      continue;
    }
    // Where the move meets the line, as a fraction of the line's length.
    // The two sides differ, so the divisor is not zero.
    const float part = before / (before - after);
    const Vec2 meet = from + (position - from) * part;
    const float lengthSquared = along.x * along.x + along.y * along.y;
    const float place =
        ((meet.x - line.a.x) * along.x + (meet.y - line.a.y) * along.y) / lengthSquared;
    if (!(place >= 0.0F && place <= 1.0F)) {
      continue;
    }
    const float across =
        std::fabs(velocity.x * along.y - velocity.y * along.x) / std::sqrt(lengthSquared);
    events_.push({PhysicsEvent::Type::Cross,
                  {table::ElementKind::Sensor, static_cast<std::uint8_t>(i)},
                  across,
                  static_cast<std::uint8_t>(slot),
                  0,
                  static_cast<std::int8_t>(before > 0.0F ? 1 : -1)});
  }
}

void PhysicsWorld::checkZones(std::size_t slot) {
  BallSlot& ball = balls_[slot];
  if (ball.held) {
    return;
  }
  const Vec2 p = ballPosition(slot);
  const Vec2 v = ballVelocity(slot);
  const float speed = std::sqrt(v.x * v.x + v.y * v.y);
  const auto distanceTo = [p](Vec2 c) {
    const float dx = p.x - c.x;
    const float dy = p.y - c.y;
    return std::sqrt(dx * dx + dy * dy);
  };
  // Where the ball was at the last check (the sensor lines keep it).
  const Vec2 from = ball.sensorFrom;
  const bool fromKnown = ball.sensorFromKnown;

  checkSensors(slot, p, v);

  for (std::size_t i = 0; i < table::kRollovers.size(); ++i) {
    // A strip across the lane, not the drawn circle: the circle is narrower
    // than the lane and missed a ball running down the lane's wall.
    const table::Circle& zone = table::kRollovers[i].zone;
    const Vec2 half = table::rolloverHalfSize(table::kRollovers[i]);
    const bool inside =
        std::fabs(p.x - zone.center.x) < half.x && std::fabs(p.y - zone.center.y) < half.y;
    if (inside && !ball.onRollover[i]) {
      pushEvent(PhysicsEvent::Type::Enter,
                {table::ElementKind::Rollover, static_cast<std::uint8_t>(i)}, speed, slot);
    }
    ball.onRollover[i] = inside;
  }

  // Put somewhere by hand since the last check: the holes take it as it is.
  const bool placed = ball.placed;
  ball.placed = false;
  for (std::size_t i = 0; i < table::kHoles.size(); ++i) {
    const table::Hole& hole = table::kHoles[i];
    const float distance = distanceTo(hole.center);
    if (!ball.holeArmed[i]) {
      ball.holeArmed[i] = distance > table::kHoleRearmDistance;
      continue;
    }
    if (table::hasCup(hole)) {
      // The Briefing room (the Mission scoop) judges a ball as it comes into
      // its pocket: one that came in the wrong way is neither funnelled nor
      // taken while it stays there (it rises to the roof and falls back
      // out); one put there by hand is taken.
      if (hole.type == table::HoleType::Subway) {
        const bool inPocket = std::fabs(p.x - hole.center.x) < hole.halfSize.x &&
                              std::fabs(p.y - hole.center.y) < hole.halfSize.y;
        if (!inPocket) {
          ball.inBox[i] = false;
          ball.boxWelcome[i] = false;
          continue;
        }
        if (!ball.inBox[i] || placed) {
          ball.inBox[i] = true;
          ball.boxWelcome[i] = placed || (v.y < 0.0F && v.x >= -v.y * table::kScoopMinRightward);
        }
        if (!ball.boxWelcome[i]) {
          continue;
        }
      }
      // Taken at the cup, wherever the ball's path since the last check
      // came nearest the cup's centre, if that is within the cup's radius
      // (so that a fast ball cannot step over it), and held there.
      Vec2 nearest = p;
      if (fromKnown) {
        const Vec2 run = p - from;
        const float runSquared = run.x * run.x + run.y * run.y;
        if (runSquared > 0.0F) {
          const float along =
              ((hole.cup.x - from.x) * run.x + (hole.cup.y - from.y) * run.y) / runSquared;
          nearest = from + run * std::clamp(along, 0.0F, 1.0F);
        }
      }
      const Vec2 off = nearest - hole.cup;
      if (off.x * off.x + off.y * off.y > hole.cupRadius * hole.cupRadius) {
        continue;  // Not there yet (or not in the pocket at all).
      }
      b2Body_SetTransform(ball.body, b2ToPos(toMeters(nearest)), b2Body_GetRotation(ball.body));
      holdBall(slot, {table::ElementKind::Hole, static_cast<std::uint8_t>(i)}, speed);
      return;
    }
    // No cup: a box across its mouth, deep enough that the fastest ball
    // (about 6 px per slice) cannot step over it. A ramp takes only a ball
    // shot up the table, written so that a speed that is not a number is
    // not taken.
    const bool inBox = std::fabs(p.x - hole.center.x) < hole.halfSize.x &&
                       std::fabs(p.y - hole.center.y) < hole.halfSize.y;
    if (!inBox || (table::isRamp(hole.type) && !(v.y <= -table::kRampMinUpwardSpeed))) {
      continue;
    }
    holdBall(slot, {table::ElementKind::Hole, static_cast<std::uint8_t>(i)}, speed);
    return;
  }

  // The kickback takes the first ball to come into its zone while it is
  // armed, at any speed, and is then spent. The zone is as wide as the
  // outlane and deeper than the fastest ball moves in a slice.
  if (kickbackArmed_ && distanceTo(table::kKickbackZone.center) < table::kKickbackZone.radius) {
    kickbackArmed_ = false;
    holdBall(slot, {table::ElementKind::Kickback, 0}, speed);
  }
}

void PhysicsWorld::releaseBall(std::size_t slot, Vec2 position, Vec2 velocity) {
  if (!ballHeld(slot) || !isFinite(position) || !isFinite(velocity)) {
    return;
  }
  BallSlot& ball = balls_[slot];
  b2Body_Enable(ball.body);
  ball.held = false;
  resetBall(slot, position, velocity);
  // Any hole the ball now sits in must not swallow it again at once.
  disarmHolesNear(slot, position);
}

void PhysicsWorld::disarmHolesNear(std::size_t slot, Vec2 position) noexcept {
  if (slot >= balls_.size()) {
    return;
  }
  BallSlot& ball = balls_[slot];
  for (std::size_t i = 0; i < table::kHoles.size(); ++i) {
    const float dx = position.x - table::kHoles[i].center.x;
    const float dy = position.y - table::kHoles[i].center.y;
    if (std::sqrt(dx * dx + dy * dy) <= table::kHoleRearmDistance) {
      ball.holeArmed[i] = false;
    }
  }
}

void PhysicsWorld::guideIntoCups(float seconds) {
  // A dished floor: towards the cup, and losing speed, for any ball in the
  // pocket of a hole that will take it.
  for (std::size_t slot = 0; slot < balls_.size(); ++slot) {
    const BallSlot& ball = balls_[slot];
    if (!ball.active || ball.held) {
      continue;
    }
    const Vec2 p = ballPosition(slot);
    for (std::size_t i = 0; i < table::kHoles.size(); ++i) {
      const table::Hole& hole = table::kHoles[i];
      // A ball the Briefing room turned away is not drawn to its cup.
      if (hole.type == table::HoleType::Subway && ball.inBox[i] && !ball.boxWelcome[i]) {
        continue;
      }
      if (!table::hasCup(hole) || !ball.holeArmed[i] ||
          !(std::fabs(p.x - hole.center.x) < hole.halfSize.x &&
            std::fabs(p.y - hole.center.y) < hole.halfSize.y)) {
        continue;
      }
      const Vec2 v = ballVelocity(slot);
      const Vec2 change =
          ((hole.cup - p) * table::kHoleFunnelPull - v * table::kHoleFunnelDamping) * seconds;
      b2Body_SetLinearVelocity(ball.body, toMeters(v + change));
      break;
    }
  }
}

void PhysicsWorld::setPlunger(bool held) noexcept { plungerHeld_ = held; }

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
  // Cleared first, so a rejected step never leaves old events behind.
  events_.clear();
  // Written so that NaN fails the test.
  if (!(stepSeconds >= kMinStepSeconds && stepSeconds <= kMaxStepSeconds)) {
    return;
  }
  driveDrone(stepSeconds);
  renewCaptiveJoint();
  const float slice = stepSeconds / static_cast<float>(kSlicesPerStep);
  for (int i = 0; i < kSlicesPerStep; ++i) {
    // One part, the whole slice, unless two balls are about to meet fast.
    const int parts = partsForBallApproach(slice);
    const float part = slice / static_cast<float>(parts);
    for (int j = 0; j < parts; ++j) {
      driveFlippers(part);
      drivePlunger(part);
      // The Air Pocket's kick is given once, at the start of its step.
      driveDevices(part, i == 0 && j == 0);
      guideIntoCups(part);
      b2World_Step(world_, part, kSubSteps);
      if (skipTouches_) {
        // Contacts the restored world found already touching (restoreBalls).
        skipTouches_ = false;
      } else {
        collectEvents();
      }
      // Always in slot order, so a step has one outcome.
      for (std::size_t slot = 0; slot < balls_.size(); ++slot) {
        if (!balls_[slot].active) {
          continue;
        }
        checkBumpers(slot);
        clampBallSpeed(slot);
        checkZones(slot);
      }
      checkCaptive();
    }
  }
}

int PhysicsWorld::partsForBallApproach(float sliceSeconds) const {
  // Box2D sweeps a fast ball against the table but not against another
  // ball, and it starts a contact only about 2 px ahead. Two balls closing
  // at full speed cover 12 px in a slice and would be found half inside
  // each other. So a slice in which a pair is about to meet, or is still
  // driving together, is taken in parts short enough that they close no
  // more than kFinePixels in each. With one ball there is no pair and the
  // slice is never divided.
  //
  // What this bounds: the closing is judged from the velocities at the
  // start of the slice. A pair that keeps those is found no more than about
  // kFinePixels inside each other (1.05 px measured head on at twice the
  // top speed). A ball that is struck during the slice, by a flipper or
  // by a third ball, towards a neighbour more than Box2D's 2 px away was
  // not foreseen and can go in by up to what it then travels in the rest
  // of the slice: at most 6.25 px at the top speed, for that one slice. The
  // next slice sees it and the contact pushes the pair apart.
  constexpr float kFinePixels = 1.5F;
  constexpr int kMostParts = 10;
  // A part may not be shorter than the shortest slice step() accepts.
  const float shortest = kMinStepSeconds / static_cast<float>(kSlicesPerStep);
  int parts = 1;
  for (std::size_t a = 0; a < balls_.size(); ++a) {
    if (!balls_[a].active || balls_[a].held) {
      continue;
    }
    for (std::size_t b = a + 1; b < balls_.size(); ++b) {
      if (!balls_[b].active || balls_[b].held) {
        continue;
      }
      const Vec2 apart = ballPosition(b) - ballPosition(a);
      const Vec2 relative = ballVelocity(b) - ballVelocity(a);
      const float distance = std::sqrt(apart.x * apart.x + apart.y * apart.y);
      if (!(distance > 1.0e-3F)) {
        continue;  // On top of each other, or not a number: no direction to judge by.
      }
      // Pixels by which the pair closes during the slice, if nothing intervenes.
      const float closing =
          -(apart.x * relative.x + apart.y * relative.y) / distance * sliceSeconds;
      const float gap = distance - 2.0F * table::kBallRadius;
      if (!(closing > kFinePixels) || gap > closing + kFinePixels) {
        continue;
      }
      const int wanted = static_cast<int>(
          std::min(std::ceil(closing / kFinePixels), static_cast<float>(kMostParts)));
      parts = std::max(parts, wanted);
    }
  }
  while (parts > 1 && sliceSeconds / static_cast<float>(parts) < shortest) {
    --parts;
  }
  return parts;
}

void PhysicsWorld::clampBallSpeed(std::size_t slot) {
  const b2BodyId body = balls_[slot].body;
  // Box2D's own limit is applied inside the solver and a contact with a fast
  // kinematic body can still leave the ball above it, so enforce it here.
  const b2Vec2 v = b2Body_GetLinearVelocity(body);
  const float speedSquared = v.x * v.x + v.y * v.y;
  if (speedSquared <= kMaxBallSpeed * kMaxBallSpeed) {
    return;
  }
  // The squares overflow for a huge velocity; taken in double they do not,
  // so the direction survives. Anything that is still not a number stops
  // the ball.
  const float speed = lengthOf(v.x, v.y);
  if (!std::isfinite(speed)) {
    b2Body_SetLinearVelocity(body, {0.0F, 0.0F});
    return;
  }
  const float scale = kMaxBallSpeed / speed;
  b2Body_SetLinearVelocity(body, {v.x * scale, v.y * scale});
}

std::size_t PhysicsWorld::activeBallCount() const noexcept {
  std::size_t count = 0;
  for (const BallSlot& ball : balls_) {
    count += ball.active ? 1U : 0U;
  }
  return count;
}

Vec2 PhysicsWorld::ballPosition(std::size_t slot) const {
  if (!ballActive(slot)) {
    return {};
  }
  const b2Pos p = b2Body_GetPosition(balls_[slot].body);
  return {toPixels(static_cast<float>(p.x)), toPixels(static_cast<float>(p.y))};
}

Vec2 PhysicsWorld::ballVelocity(std::size_t slot) const {
  if (!ballActive(slot)) {
    return {};
  }
  const b2Vec2 v = b2Body_GetLinearVelocity(balls_[slot].body);
  return {toPixels(v.x), toPixels(v.y)};
}

float PhysicsWorld::ballAngle(std::size_t slot) const {
  if (!ballActive(slot)) {
    return 0.0F;
  }
  return b2Rot_GetAngle(b2Body_GetRotation(balls_[slot].body));
}

void PhysicsWorld::clearLatches(std::size_t slot) noexcept {
  BallSlot& ball = balls_[slot];
  ball.touchingBumper.fill(false);
  ball.onRollover.fill(false);
  ball.holeArmed.fill(true);
  ball.inBox.fill(false);
  ball.boxWelcome.fill(false);
}

bool PhysicsWorld::activateBall(std::size_t slot, Vec2 position, Vec2 velocity) {
  if (slot >= balls_.size() || balls_[slot].active || !isFinite(position) || !isFinite(velocity)) {
    return false;
  }
  BallSlot& ball = balls_[slot];
  b2Body_Enable(ball.body);
  ball.active = true;
  ball.held = false;
  clearLatches(slot);
  resetBall(slot, position, velocity);
  return true;
}

void PhysicsWorld::deactivateBall(std::size_t slot) {
  if (!ballActive(slot)) {
    return;
  }
  BallSlot& ball = balls_[slot];
  if (!ball.held) {
    b2Body_SetLinearVelocity(ball.body, {0.0F, 0.0F});
    b2Body_SetAngularVelocity(ball.body, 0.0F);
    b2Body_Disable(ball.body);
  }
  ball.active = false;
  ball.held = false;
}

void PhysicsWorld::resetBall(std::size_t slot, Vec2 position, Vec2 velocity) {
  if (!ballActive(slot) || !isFinite(position) || !isFinite(velocity)) {
    return;
  }
  BallSlot& ball = balls_[slot];
  if (ball.held) {
    // A reset always leaves a live ball on the table.
    b2Body_Enable(ball.body);
    ball.held = false;
  }
  // A ball moved by hand has crossed nothing on the way.
  ball.sensorFromKnown = false;
  ball.placed = true;
  b2Body_SetTransform(ball.body, b2ToPos(toMeters(position)), b2MakeRot(0.0F));
  b2Body_SetLinearVelocity(ball.body, toMeters(velocity));
  clampBallSpeed(slot);
  b2Body_SetAngularVelocity(ball.body, 0.0F);
  b2Body_SetAwake(ball.body, true);
}

void PhysicsWorld::pushBall(std::size_t slot, Vec2 velocityChange) {
  if (!ballActive(slot) || balls_[slot].held || !isFinite(velocityChange)) {
    return;
  }
  const b2BodyId body = balls_[slot].body;
  const b2Vec2 v = b2Body_GetLinearVelocity(body);
  const b2Vec2 dv = toMeters(velocityChange);
  b2Body_SetLinearVelocity(body, {v.x + dv.x, v.y + dv.y});
  clampBallSpeed(slot);
}

bool PhysicsWorld::ballIsSane(std::size_t slot) const {
  if (!ballActive(slot)) {
    return true;
  }
  const Vec2 p = ballPosition(slot);
  const Vec2 v = ballVelocity(slot);
  if (!isFinite(p) || !isFinite(v)) {
    return false;
  }
  constexpr float kMargin = 2.0F * table::kBallRadius;
  return p.x > table::kWallLeft - kMargin && p.x < table::kWallRight + kMargin &&
         p.y > -kMargin && p.y < table::kFloorY + kMargin;
}

}  // namespace pinball
