#include "core/Snapshot.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "core/Version.hpp"

namespace pinball::save {

namespace {

constexpr std::array<char, 8> kMagic{'M', 'B', 'A', '-', 'S', 'A', 'V', 'E'};

// --- Bytes ---------------------------------------------------------------------------

// Appends fields, little-endian. Ranges are not checked here: decode does.
class Writer {
 public:
  explicit Writer(std::string& out) noexcept : out_(out) {}

  void raw(const void* data, std::size_t size) {
    out_.append(static_cast<const char*>(data), size);
  }
  void unsignedValue(std::uint64_t value, std::size_t bytes) {
    for (std::size_t i = 0; i < bytes; ++i) {
      out_.push_back(static_cast<char>((value >> (8U * i)) & 0xFFU));
    }
  }

  // The visitor's side: one call per field, with the range decode wants.
  template <class T>
  void integer(T& value, T /*low*/, T /*high*/) {
    using U = std::make_unsigned_t<T>;
    unsignedValue(static_cast<std::uint64_t>(static_cast<U>(value)), sizeof(T));
  }
  void flag(bool& value) { unsignedValue(value ? 1U : 0U, 1); }
  void real(float& value, float /*low*/, float /*high*/) {
    unsignedValue(std::bit_cast<std::uint32_t>(value), 4);
  }
  void real(double& value, double /*low*/, double /*high*/) {
    unsignedValue(std::bit_cast<std::uint64_t>(value), 8);
  }
  void element(table::ElementRef& ref) {
    unsignedValue(std::to_underlying(ref.kind), 1);
    unsignedValue(ref.index, 1);
  }

 private:
  std::string& out_;
};

// Reads fields, checking each against its range. Once anything is wrong it
// stays wrong and reads nothing more.
class Reader {
 public:
  explicit Reader(std::span<const std::uint8_t> bytes) noexcept : bytes_(bytes) {}

  [[nodiscard]] bool ok() const noexcept { return ok_; }
  [[nodiscard]] bool atEnd() const noexcept { return at_ == bytes_.size(); }
  void fail() noexcept { ok_ = false; }

  bool raw(void* data, std::size_t size) noexcept {
    if (!ok_ || bytes_.size() - at_ < size) {
      ok_ = false;
      return false;
    }
    std::memcpy(data, bytes_.data() + at_, size);
    at_ += size;
    return true;
  }
  std::uint64_t unsignedValue(std::size_t bytes) noexcept {
    if (!ok_ || bytes_.size() - at_ < bytes) {
      ok_ = false;
      return 0;
    }
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < bytes; ++i) {
      value |= static_cast<std::uint64_t>(bytes_[at_ + i]) << (8U * i);
    }
    at_ += bytes;
    return value;
  }

  template <class T>
  void integer(T& value, T low, T high) noexcept {
    using U = std::make_unsigned_t<T>;
    const auto read = static_cast<T>(static_cast<U>(unsignedValue(sizeof(T))));
    if (read < low || read > high) {
      ok_ = false;
    }
    value = ok_ ? read : T{};
  }
  void flag(bool& value) noexcept {
    const std::uint64_t read = unsignedValue(1);
    if (read > 1U) {
      ok_ = false;
    }
    value = ok_ && read == 1U;
  }
  // Finite and within the range: no NaN, no infinity.
  void real(float& value, float low, float high) noexcept {
    const float read = std::bit_cast<float>(static_cast<std::uint32_t>(unsignedValue(4)));
    if (!(read >= low && read <= high)) {
      ok_ = false;
    }
    value = ok_ ? read : 0.0F;
  }
  void real(double& value, double low, double high) noexcept {
    const double read = std::bit_cast<double>(unsignedValue(8));
    if (!(read >= low && read <= high)) {
      ok_ = false;
    }
    value = ok_ ? read : 0.0;
  }
  // A real element of the table, or none.
  void element(table::ElementRef& ref) noexcept {
    const std::uint64_t kind = unsignedValue(1);
    const std::uint64_t index = unsignedValue(1);
    constexpr auto kLastKind = std::to_underlying(table::ElementKind::Kickback);
    ref = {};
    if (!ok_ || kind > kLastKind) {
      ok_ = false;
      return;
    }
    const table::ElementRef read{static_cast<table::ElementKind>(kind),
                                 static_cast<std::uint8_t>(index)};
    if (read.kind == table::ElementKind::None) {
      if (index != 0) {
        ok_ = false;
      }
      return;
    }
    if (!table::isValid(read)) {
      ok_ = false;
      return;
    }
    ref = read;
  }

 private:
  std::span<const std::uint8_t> bytes_;
  std::size_t at_ = 0;
  bool ok_ = true;
};

// --- Ranges -------------------------------------------------------------------------

constexpr std::int32_t kMaxBalls = static_cast<std::int32_t>(table::kMaxBalls);
constexpr std::uint64_t kMaxScore = points::kMaxScore;
constexpr std::uint32_t kAnyU32 = std::numeric_limits<std::uint32_t>::max();
// A ball on the table: within the shell, with the margin PhysicsWorld allows
// before it calls a ball lost.
constexpr float kMargin = 2.0F * table::kBallRadius;
constexpr float kMinX = table::kWallLeft - kMargin;
constexpr float kMaxX = table::kWallRight + kMargin;
constexpr float kMinY = -kMargin;
constexpr float kMaxY = table::kFloorY + kMargin;
// Somewhere a ball was or will be; an untouched slot's default is 0, 0.
constexpr float kAnyPlace = 4096.0F;
// The physics caps the ball's speed (with a little room for rounding).
constexpr float kMaxSpeed = kMaxBallSpeedPixels * 1.01F;
constexpr float kMaxSpin = 10'000.0F;  // Radians per second.
template <class V>
void visit(V& v, Vec2& p, float low, float high) {
  v.real(p.x, low, high);
  v.real(p.y, low, high);
}

template <class V, std::size_t N>
void visit(V& v, std::array<bool, N>& flags) {
  for (bool& flag : flags) {
    v.flag(flag);
  }
}

template <class V>
void visit(V& v, RulesRecord& r) {
  v.integer(r.score, std::uint64_t{0}, kMaxScore);
  for (std::uint64_t& points : r.featureScore) {
    v.integer(points, std::uint64_t{0}, kMaxScore);
  }
  v.integer(r.ball, 1, Rules::kBallsPerGame);
  v.flag(r.tilted);
  v.real(r.tiltHeat, 0.0, Rules::kTiltLimit);
  v.flag(r.ballInPlay);
  v.integer(r.rank, std::uint8_t{0}, static_cast<std::uint8_t>(kRankCount - 1));
  v.integer(r.statusLive, 0, kMaxBalls);
  v.integer(r.statusFree, 0, kMaxBalls);
  v.integer(r.statusQueued, 0, kMaxBalls);
  v.integer(r.statusLocked, 0, kMaxBalls);
  v.integer(r.ballRequests, 0, 2 * kMaxBalls);
  v.integer(r.requestsInFlight, 0, 2 * kMaxBalls);
  v.integer(r.turboRequests, 0, 2 * kMaxBalls);
  v.integer(r.turboRequestsInFlight, 0, 2 * kMaxBalls);
  v.integer(r.lockCommand, std::uint8_t{0}, std::to_underlying(Rules::LockCommand::Release));
  v.flag(r.multiballEnabled);
  v.flag(r.ballSaveEnabled);

  v.flag(r.saverArmable);
  v.integer(r.saverSteps, 0U, kMaxTimerSteps);
  v.integer(r.graceSteps, 0U, kMaxTimerSteps);
  v.integer(r.extraBallsLit, 0, Rules::kMaxExtraBalls);
  v.integer(r.extraBallsHeld, 0, Rules::kMaxExtraBallsHeld);
  v.integer(r.extraBallsAwarded, 0, Rules::kMaxExtraBalls);

  v.flag(r.skillOffered);
  v.integer(r.skillLane, std::uint8_t{0}, std::uint8_t{2});
  v.integer(r.superSkillSteps, 0U, kMaxTimerSteps);

  visit(v, r.rollCallLit);
  visit(v, r.wingLit);
  visit(v, r.turboTargetLit);
  v.flag(r.kickbackLit);
  v.integer(r.kickbackArmSteps, 0U, kMaxTimerSteps);
  v.integer(r.kickbackRides, 0, kMaxBalls);
  v.flag(r.extraCreditLit);

  // The meter is never fuller than its goal when the step ends; the goal
  // stops rising at 10,000 (Rules::turboFull).
  v.integer(r.turboGoal, tuning::kTurboMeterFirst, 10'000);
  v.integer(r.turboMeter, 0, 10'000);

  visit(v, r.doorMarked);
  v.flag(r.doorsOpen);
  v.flag(r.firstBank);
  v.flag(r.doorsDown);
  v.integer(r.doorCommand, std::uint8_t{0}, std::to_underlying(Rules::DoorCommand::Raise));
  v.integer(r.doorRaiseSteps, 0U, kMaxTimerSteps);
  v.integer(r.hangarHolding, 0, kMaxBalls);
  v.integer(r.locksHeld, 0, Rules::kLocksNeeded);
  visit(v, r.bayFull);
  v.integer(r.releasesPending, 0, Rules::kLocksNeeded);

  v.flag(r.turboMultiball);
  v.flag(r.academyMultiball);
  v.integer(r.jackpotIndex, std::uint8_t{0}, static_cast<std::uint8_t>(points::kJackpot.size()));
  v.integer(r.jackpotsTowardSuper, 0, tuning::kJackpotsForSuper);
  v.flag(r.superLit);
  v.integer(r.turboMultiballs, 0, kMaxCounter);
  v.integer(r.academyMultiballs, 0, kMaxCounter);
  v.integer(r.jackpotsCollected, 0, kMaxCounter);

  v.integer(r.comboShot, std::uint8_t{0}, std::uint8_t{0xFF});
  v.integer(r.comboChain, 0, Rules::kMaxCombo);
  v.integer(r.comboSteps, 0U, kMaxTimerSteps);
  v.flag(r.fiveComboMade);

  v.flag(r.overdriveLit);
  v.integer(r.overdrive, 1, Rules::kMaxOverdrive);
  v.integer(r.overdriveSteps, 0U, kMaxTimerSteps);

  for (CircuitRecord& circuit : r.circuits) {
    v.integer(circuit.side, std::uint8_t{0}, std::uint8_t{1});
    v.integer(circuit.steps, 0U, kMaxTimerSteps);
  }

  visit(v, r.coursePassed);
  v.integer(r.course, std::uint8_t{0}, static_cast<std::uint8_t>(kCourseCount));
  v.integer(r.courseBlink, std::uint8_t{0}, static_cast<std::uint8_t>(kCourseCount - 1));
  v.flag(r.courseLit);
  v.integer(r.rampsNeeded, 0, 100);
  v.integer(r.coursesEnded, 0, kMaxCounter);
  v.integer(r.coursesStarted, 0, kMaxCounter);
  v.integer(r.coursePasses, 0, kMaxCounter);
  v.integer(r.courseSteps, 0U, kMaxTimerSteps);
  v.integer(r.courseElapsedSteps, 0U, kAnyU32);
  v.integer(r.courseProgress, 0, 255);
  v.flag(r.courseSecondPart);
  v.integer(r.navShot, std::uint8_t{0}, static_cast<std::uint8_t>(kShotCount - 1));
  v.integer(r.navSteps, 0U, kMaxTimerSteps);

  v.integer(r.gradStage, std::uint8_t{0}, std::uint8_t{3});
  v.flag(r.gradLit);
  v.integer(r.gradRampsNeeded, 0, 100);
  visit(v, r.examMade);
  v.integer(r.graduations, 0, kMaxCounter);
  v.flag(r.commencementThisTime);

  v.flag(r.briefingVisited);
  v.integer(r.combosThisBall, 0, kMaxCounter);
  v.integer(r.laneSetsThisBall, 0, kMaxCounter);
  v.integer(r.switchesThisBall, 0, points::kBonusSwitchCap);
  v.integer(r.bonusMultiplier, 1, Rules::kMaxBonusMultiplier);
  v.integer(r.bonusTally.courses, std::uint64_t{0}, kMaxScore);
  v.integer(r.bonusTally.locks, std::uint64_t{0}, kMaxScore);
  v.integer(r.bonusTally.combos, std::uint64_t{0}, kMaxScore);
  v.integer(r.bonusTally.lanes, std::uint64_t{0}, kMaxScore);
  v.integer(r.bonusTally.switches, std::uint64_t{0}, kMaxScore);
  v.integer(r.bonusTally.multiplier, 1, Rules::kMaxBonusMultiplier);
  v.integer(r.bonusTally.total, std::uint64_t{0}, kMaxScore);

  // xorshift never leaves 0, and never reaches it.
  v.integer(r.random, std::uint64_t{1}, std::numeric_limits<std::uint64_t>::max());
}

template <class V>
void visit(V& v, GameRecord& g) {
  v.integer(g.stepCount, std::uint64_t{0}, kMaxStepCount);
  v.integer(g.drainCount, 0U, kAnyU32);
  v.integer(g.bumperHits, 0U, kAnyU32);
  for (std::uint32_t& count : g.rolloverCounts) {
    v.integer(count, 0U, kAnyU32);
  }
  for (std::uint32_t& count : g.holeCounts) {
    v.integer(count, 0U, kAnyU32);
  }
  for (std::uint64_t& step : g.lastReleaseStep) {
    v.integer(step, std::uint64_t{0}, kMaxStepCount + 1U);
  }
  v.integer(g.ballSearchCount, 0U, kAnyU32);
  v.integer(g.arrivals, std::uint64_t{0}, kMaxStepCount);
  v.integer(g.hangarBurst, 0U, kAnyU32);
  v.flag(g.doorRaisePending);
  v.integer(g.lockCount, 0U, kAnyU32);
  v.integer(g.lockReleaseCount, 0U, kAnyU32);
  v.integer(g.kickbackCount, 0U, kAnyU32);
  v.integer(g.spinsOwed, 0, table::kMaxSpinsOwed);
  v.integer(g.spinCooldown, 0, table::kSpinPayoutSteps);
  v.integer(g.spinCount, 0U, kAnyU32);
  v.integer(g.spinDirection, -1, 1);
  v.integer(g.spinnerTicks, 0U, 2U * static_cast<std::uint32_t>(table::kSpinPayoutSteps) - 1U);
  v.integer(g.random, std::uint64_t{1}, std::numeric_limits<std::uint64_t>::max());
  v.integer(g.queuedBalls, std::uint8_t{0}, static_cast<std::uint8_t>(table::kMaxBalls));
  v.integer(g.lastAutoServeStep, std::uint64_t{0}, kMaxStepCount + 1U);
  v.integer(g.autoLaunchCount, 0U, kAnyU32);
  v.integer(g.autoRelaunchCount, 0U, kAnyU32);
}

template <class V>
void visit(V& v, TableRecord& t) {
  for (std::size_t i = 0; i < t.flipperAngles.size(); ++i) {
    const table::Flipper& flipper = table::kFlippers[i];
    v.real(t.flipperAngles[i], std::min(flipper.restAngle, flipper.activeAngle),
           std::max(flipper.restAngle, flipper.activeAngle));
  }
  v.real(t.plungerPull, 0.0F, table::kPlungerTravel);
  v.real(t.plungerReleaseSpeed, 0.0F, kMaxPlungerSpeedPixels * 1.01F);
  v.flag(t.doorBankUp);
  v.flag(t.kickbackArmed);
}

template <class V>
void visit(V& v, BallRecord& b) {
  v.flag(b.active);
  // (0 for an empty slot; a ball's own place is checked in ballConsistent.)
  v.real(b.x, static_cast<double>(std::min(kMinX, 0.0F) / kPixelsPerMeter),
         static_cast<double>(kMaxX / kPixelsPerMeter));
  v.real(b.y, static_cast<double>(kMinY / kPixelsPerMeter),
         static_cast<double>(kMaxY / kPixelsPerMeter));
  v.real(b.vx, -kMaxSpeed / kPixelsPerMeter, kMaxSpeed / kPixelsPerMeter);
  v.real(b.vy, -kMaxSpeed / kPixelsPerMeter, kMaxSpeed / kPixelsPerMeter);
  v.real(b.rotationCos, -1.001F, 1.001F);
  v.real(b.rotationSin, -1.001F, 1.001F);
  v.real(b.spin, -kMaxSpin, kMaxSpin);
  v.flag(b.held);
  v.element(b.heldBy);
  v.flag(b.sensorFromKnown);
  visit(v, b.sensorFrom, -kAnyPlace, kAnyPlace);
  visit(v, b.touchingBumper);
  visit(v, b.onRollover);
  visit(v, b.holeArmed);
  v.element(b.holder);
  v.integer(b.heldSteps, 0U, kMaxHeldSteps);
  v.flag(b.wasHeld);
  v.integer(b.arrival, std::uint64_t{0}, kMaxStepCount);
  v.integer(b.lockBay, std::uint8_t{0}, static_cast<std::uint8_t>(table::kLockBays.size()));
  v.flag(b.releasing);
  v.integer(b.ejectStep, std::uint64_t{0}, kMaxStepCount + 1U);
  visit(v, b.searchAnchor, -kAnyPlace, kAnyPlace);
  v.integer(b.stillSteps, 0U, kMaxStillSteps);
  v.flag(b.inPlay);
  visit(v, b.takenAt, -kAnyPlace, kAnyPlace);
  v.integer(b.takenStep, std::uint64_t{0}, kMaxStepCount + 1U);
  visit(v, b.ejectFrom, -kAnyPlace, kAnyPlace);
  v.flag(b.ejectFromBay);
  v.flag(b.autoLaunch);
  v.integer(b.launchSteps, 0U, kMaxLaunchSteps);
  v.flag(b.launched);
}

template <class V>
void visit(V& v, Snapshot& s) {
  visit(v, s.rules);
  visit(v, s.game);
  visit(v, s.table);
  for (BallRecord& ball : s.balls) {
    visit(v, ball);
  }
}

// --- What the ranges cannot say -------------------------------------------------------

bool holderValid(table::ElementRef ref) noexcept {
  return (ref.kind == table::ElementKind::Hole || ref.kind == table::ElementKind::Kickback) &&
         table::isValid(ref);
}

bool ballConsistent(const BallRecord& b, const GameRecord& g) {
  if (!b.active) {
    // An empty slot carries nothing: it is the default record, byte for byte.
    BallRecord copy = b;
    BallRecord empty;
    std::string mine;
    std::string blank;
    Writer w1(mine);
    Writer w2(blank);
    visit(w1, copy);
    visit(w2, empty);
    return mine == blank;
  }
  // On the table, as PhysicsWorld::ballIsSane has it.
  if (!(b.x >= static_cast<double>(kMinX / kPixelsPerMeter) &&
        b.y >= static_cast<double>(kMinY / kPixelsPerMeter))) {
    return false;
  }
  // A rotation is a unit vector (Box2D refuses anything else).
  const float length = b.rotationCos * b.rotationCos + b.rotationSin * b.rotationSin;
  if (!(length > 0.998F && length < 1.002F)) {
    return false;
  }
  if (b.held != (b.heldBy.kind != table::ElementKind::None)) {
    return false;
  }
  if (b.held && (!holderValid(b.heldBy) || b.holder != b.heldBy)) {
    return false;
  }
  if (b.holder.kind != table::ElementKind::None && !holderValid(b.holder)) {
    return false;
  }
  if (b.lockBay != 0) {
    // Only the Hangar keeps a ball in a bay, and only while it holds it.
    const table::ElementRef hangar{table::ElementKind::Hole,
                                   static_cast<std::uint8_t>(table::kHangarHole)};
    if (!b.held || b.holder != hangar) {
      return false;
    }
  } else if (b.releasing) {
    return false;
  }
  if (b.arrival > g.arrivals || b.ejectStep > g.stepCount + 1U ||
      b.takenStep > g.stepCount + 1U) {
    return false;
  }
  return true;
}

}  // namespace

std::string currentBuildId() {
  std::string id(kGameVersion);
  id += '+';
  id += kBuildNumber;
  return id;
}

Sha256Digest layoutFingerprint() noexcept {
  Sha256 hash;
  const auto add = [&hash](float value) {
    const auto bits = std::bit_cast<std::uint32_t>(value);
    const std::array<std::uint8_t, 4> bytes{
        static_cast<std::uint8_t>(bits), static_cast<std::uint8_t>(bits >> 8U),
        static_cast<std::uint8_t>(bits >> 16U), static_cast<std::uint8_t>(bits >> 24U)};
    hash.update(bytes);
  };
  const auto addPoint = [&add](Vec2 p) {
    add(p.x);
    add(p.y);
  };
  const auto addCapsule = [&](const table::Capsule& c) {
    addPoint(c.a);
    addPoint(c.b);
    add(c.radius);
  };
  for (const Vec2 p : table::kShell) {
    addPoint(p);
  }
  for (const Vec2 p : table::kLaneIsland) {
    addPoint(p);
  }
  for (const Vec2 p : table::kInnerArch) {
    addPoint(p);
  }
  for (const table::Capsule& c : table::kAllWalls) {
    addCapsule(c);
  }
  for (const table::Capsule& c : table::kDeadPosts) {
    addCapsule(c);
  }
  for (const table::Triangle& t : table::kSlingshots) {
    for (const Vec2 p : t.points) {
      addPoint(p);
    }
    add(t.cornerRadius);
  }
  for (const table::Flipper& f : table::kFlippers) {
    addPoint(f.pivot);
    add(f.restAngle);
    add(f.activeAngle);
  }
  add(table::kFlipperLength);
  add(table::kFlipperBaseRadius);
  add(table::kFlipperTipRadius);
  for (const table::Bumper& b : table::kBumpers) {
    addPoint(b.shape.center);
    add(b.shape.radius);
  }
  for (const table::Target& t : table::kTargets) {
    addCapsule(t.shape);
  }
  for (const table::Capsule& c : table::kDropTargets) {
    addCapsule(c);
  }
  for (const table::Rollover& r : table::kRollovers) {
    addPoint(r.zone.center);
    add(r.zone.radius);
    add(static_cast<float>(std::to_underlying(r.group)));
  }
  for (const table::Hole& h : table::kHoles) {
    addPoint(h.center);
    addPoint(h.halfSize);
    add(static_cast<float>(std::to_underlying(h.type)));
    addPoint(h.exitPosition);
    addPoint(h.exitVelocity);
  }
  for (const Vec2 p : table::kLockBays) {
    addPoint(p);
  }
  for (const table::SensorLine& s : table::kSensors) {
    addPoint(s.a);
    addPoint(s.b);
  }
  addPoint(table::kKickbackZone.center);
  add(table::kKickbackZone.radius);
  for (const Vec2 p : table::kKickbackPath) {
    addPoint(p);
  }
  addPoint(table::kBallStart);
  add(table::kBallRadius);
  add(table::kPlungerTravel);
  add(table::kLaneCenterX);
  addPoint(table::kDoorMouthCenter);
  addPoint(table::kDoorMouthHalfSize);
  return hash.finish();
}

namespace {

// The counts of every kind of element and record the payload is made of,
// so that a table with one more hole is refused even before the payload.
constexpr std::array<std::uint8_t, 14> kShape{
    static_cast<std::uint8_t>(table::kMaxBalls),
    static_cast<std::uint8_t>(table::kBumpers.size()),
    static_cast<std::uint8_t>(table::kSlingshots.size()),
    static_cast<std::uint8_t>(table::kTargets.size()),
    static_cast<std::uint8_t>(table::kDropTargets.size()),
    static_cast<std::uint8_t>(table::kRollovers.size()),
    static_cast<std::uint8_t>(table::kHoles.size()),
    static_cast<std::uint8_t>(table::kSensors.size()),
    static_cast<std::uint8_t>(table::kLockBays.size()),
    static_cast<std::uint8_t>(table::kFlippers.size()),
    static_cast<std::uint8_t>(kCourseCount),
    static_cast<std::uint8_t>(kShotCount),
    static_cast<std::uint8_t>(kFeatureCount),
    static_cast<std::uint8_t>(kRankCount),
};

std::string header(std::string_view buildId) {
  std::string out;
  Writer w(out);
  w.raw(kMagic.data(), kMagic.size());
  w.unsignedValue(kFormatVersion, 2);
  w.unsignedValue(buildId.size(), 1);
  w.raw(buildId.data(), buildId.size());
  const Sha256Digest layout = layoutFingerprint();
  w.raw(layout.data(), layout.size());
  w.raw(kShape.data(), kShape.size());
  w.unsignedValue(payloadSize(), 4);
  return out;
}

}  // namespace

std::size_t payloadSize() noexcept {
  static const std::size_t size = encodePayload(Snapshot{}).size();
  return size;
}

std::size_t fileSize() noexcept {
  static const std::size_t size = header(currentBuildId()).size() + payloadSize() + 32U;
  return size;
}

std::string encodePayload(const Snapshot& snapshot) {
  Snapshot copy = snapshot;
  std::string out;
  out.reserve(1024);
  Writer w(out);
  visit(w, copy);
  return out;
}

std::optional<Snapshot> decodePayload(std::span<const std::uint8_t> bytes) {
  Snapshot snapshot;
  Reader r(bytes);
  visit(r, snapshot);
  if (!r.ok() || !r.atEnd() || !valid(snapshot)) {
    return std::nullopt;
  }
  return snapshot;
}

std::string encode(const Snapshot& snapshot) { return encode(snapshot, currentBuildId()); }

std::string encode(const Snapshot& snapshot, std::string_view buildId) {
  if (buildId.size() > kMaxBuildIdLength) {
    return {};
  }
  std::string out = header(buildId);
  out += encodePayload(snapshot);
  const Sha256Digest sum =
      sha256(reinterpret_cast<const std::uint8_t*>(out.data()), out.size());
  out.append(reinterpret_cast<const char*>(sum.data()), sum.size());
  return out;
}

std::optional<Snapshot> decode(std::span<const std::uint8_t> bytes) {
  return decode(bytes, currentBuildId());
}

std::optional<Snapshot> decode(std::span<const std::uint8_t> bytes, std::string_view buildId) {
  // Checked before anything is read: every file of this build is the same
  // size, so nothing else needs reading.
  if (buildId.size() > kMaxBuildIdLength || bytes.size() > kMaxFileBytes ||
      bytes.size() < 32U) {
    return std::nullopt;
  }
  const std::string expected = header(buildId);
  if (bytes.size() != expected.size() + payloadSize() + 32U) {
    return std::nullopt;
  }
  // The header is fixed for this build, so it is compared whole: magic,
  // version, build id, layout, element counts and payload size.
  if (std::memcmp(bytes.data(), expected.data(), expected.size()) != 0) {
    return std::nullopt;
  }
  const std::span<const std::uint8_t> sealed = bytes.first(bytes.size() - 32U);
  const Sha256Digest sum = sha256(sealed);
  if (std::memcmp(sum.data(), bytes.data() + sealed.size(), sum.size()) != 0) {
    return std::nullopt;
  }
  return decodePayload(sealed.subspan(expected.size()));
}

bool valid(const Snapshot& snapshot) {
  // Every field within its range: read it back.
  {
    Snapshot copy = snapshot;
    std::string bytes;
    Writer w(bytes);
    visit(w, copy);
    Snapshot read;
    Reader r(std::span(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()));
    visit(r, read);
    if (!r.ok() || !r.atEnd()) {
      return false;
    }
  }
  const RulesRecord& rules = snapshot.rules;
  const GameRecord& game = snapshot.game;
  // The rules' view of the balls never passes the table's three.
  if (rules.statusFree > rules.statusLive ||
      rules.statusLive + rules.statusQueued + rules.statusLocked > kMaxBalls) {
    return false;
  }
  if (rules.turboMeter > rules.turboGoal ||
      (rules.comboShot >= kShotCount && rules.comboShot != 0xFF)) {
    return false;
  }
  if (rules.bonusTally.total > kMaxScore) {
    return false;
  }
  // Steps the game has seen bound every step it remembers.
  for (const std::uint64_t step : game.lastReleaseStep) {
    if (step > game.stepCount + 1U) {
      return false;
    }
  }
  if (game.lastAutoServeStep > game.stepCount + 1U || game.arrivals > game.stepCount + 1U) {
    return false;
  }
  // At least one ball on the table, and never more than three with the
  // ones waiting to be served; each bay holds one ball at most.
  std::size_t active = 0;
  std::array<bool, table::kLockBays.size()> bays{};
  for (const BallRecord& ball : snapshot.balls) {
    if (!ballConsistent(ball, game)) {
      return false;
    }
    if (!ball.active) {
      continue;
    }
    ++active;
    if (ball.lockBay != 0) {
      bool& bay = bays[ball.lockBay - 1U];
      if (bay) {
        return false;
      }
      bay = true;
    }
  }
  if (active == 0 || active + game.queuedBalls > table::kMaxBalls) {
    return false;
  }
  return true;
}

bool operator==(const Snapshot& a, const Snapshot& b) {
  return encodePayload(a) == encodePayload(b);
}

}  // namespace pinball::save
