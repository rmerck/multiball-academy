#include "bots/Acceptance.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <span>

#include "GameTestAccess.hpp"
#include "bots/Bots.hpp"
#include "core/Game.hpp"
#include "core/TableLayout.hpp"

namespace pinball::bots::acceptance {

namespace {

constexpr int kStepsPerSecond = 120;
// Start positions across the plunger for a launch: sideways offsets.
constexpr std::array<float, 8> kLaunchOffsets{0.0F,  -1.5F, -0.75F, -0.25F,
                                              0.25F, 0.75F, 1.5F,   0.0F};

Vec2 mirrorPoint(Vec2 p, bool mirror) noexcept { return mirror ? table::mirrored(p) : p; }
Vec2 mirrorVelocity(Vec2 v, bool mirror) noexcept { return mirror ? Vec2{-v.x, v.y} : v; }

bool isSafeFirstTouch(Touch touch) noexcept {
  return touch == Touch::Flipper || touch == Touch::InlaneLeft || touch == Touch::InlaneRight ||
         touch == Touch::RollCall;
}

bool isPassedThrough(Touch touch) noexcept {
  return touch == Touch::CircuitLeft || touch == Touch::CircuitRight || touch == Touch::Turbine;
}

float speedOf(Vec2 v) noexcept { return std::sqrt(v.x * v.x + v.y * v.y); }

// Puts the ball on the plunger, lets it settle, and holds the plunger.
void plunge(TableHarness& harness, int offsetIndex, int holdSteps) {
  const auto index = static_cast<std::size_t>(offsetIndex) % kLaunchOffsets.size();
  harness.put(0, {table::kBallStart.x + kLaunchOffsets[index], table::kBallStart.y}, {}, false);
  for (int i = 0; i < (index == 7 ? 20 : 30); ++i) {
    harness.advance();
  }
  harness.plunger = true;
  for (int i = 0; i < holdSteps; ++i) {
    harness.advance();
  }
  harness.plunger = false;
}

bool atRestOnPlunger(const TableHarness& harness) {
  return !harness.game().ballInPlay(0) &&
         std::fabs(harness.position(0).y - table::kBallStart.y) < 3.0F &&
         std::fabs(harness.velocity(0).y) < 5.0F;
}

std::unique_ptr<Bot> makeBot(BotKind kind, std::uint64_t seed, int reflexError) {
  switch (kind) {
    case BotKind::Idle:
      return std::make_unique<IdleBot>();
    case BotKind::Random:
      return std::make_unique<RandomBot>(seed);
    case BotKind::Reflex:
      break;
  }
  return std::make_unique<ReflexBot>(seed, reflexError);
}

// Whether a ball can be put at p without starting inside something solid:
// clear of every wall, post, target, door, bumper and slingshot, and not in
// the Hangar, which is shut behind its doors.
bool clearOfSolids(Vec2 p) noexcept {
  constexpr float kClear = table::kBallRadius + 1.0F;
  const auto clearOf = [p](const table::Capsule& c) {
    return reachOf(p, c.a, c.b).distance > c.radius + kClear;
  };
  for (const table::Capsule& wall : table::kAllWalls) {
    if (!clearOf(wall)) {
      return false;
    }
  }
  for (const table::Capsule& post : table::kDeadPosts) {
    if (!clearOf(post)) {
      return false;
    }
  }
  for (const table::Target& target : table::kTargets) {
    if (!clearOf(target.shape)) {
      return false;
    }
  }
  for (const table::Capsule& door : table::kDropTargets) {
    if (!clearOf(door)) {
      return false;
    }
  }
  for (const table::Bumper& bumper : table::kBumpers) {
    const Vec2 d = p - bumper.shape.center;
    const float reach = bumper.shape.radius + kClear;
    if (d.x * d.x + d.y * d.y < reach * reach) {
      return false;
    }
  }
  for (const table::Triangle& sling : table::kSlingshots) {
    // Near an edge, or inside.
    bool inside = true;
    for (std::size_t i = 0; i < 3; ++i) {
      const Vec2 a = sling.points[i];
      const Vec2 b = sling.points[(i + 1) % 3];
      if (reachOf(p, a, b).distance < sling.cornerRadius + kClear) {
        return false;
      }
      const Vec2 c = sling.points[(i + 2) % 3];
      const float side = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
      const float inner = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
      inside = inside && side * inner > 0.0F;
    }
    if (inside) {
      return false;
    }
  }
  // The Hangar's pocket, from its roof down to the doors.
  const bool inHangar = p.x > table::kHangarLeft.a.x && p.x < table::kHangarRight.a.x &&
                        p.y > table::kHangarRoofRight.a.y && p.y < table::kDoorY;
  return !inHangar;
}

void mix(std::uint64_t& hash, std::uint64_t value) noexcept {
  hash = (hash ^ value) * 0x100000001B3ULL;
}

}  // namespace

LaunchResult launchSweep(int holdStride, int offsets) {
  LaunchResult result;
  for (int hold = 1; hold <= 125; hold += std::max(holdStride, 1)) {
    for (int j = 0; j < std::clamp(offsets, 1, 8); ++j) {
      TableHarness harness;
      plunge(harness, j, hold);
      Touch first = Touch::None;
      bool back = false;
      float before = speedOf(harness.velocity(0));
      for (int i = 0; i < kStepsPerSecond * 25; ++i) {
        const std::size_t eventsBefore = harness.events.size();
        const std::uint64_t touchesBefore = harness.flipperTouches;
        const bool wasInPlay = harness.game().ballInPlay(0);
        const bool clearOfPlunger = harness.position(0).y < table::kPlungerRestTopY - 120.0F;
        const Touch drain = harness.advance();
        const float now = speedOf(harness.velocity(0));
        if (!wasInPlay && clearOfPlunger && drain == Touch::None) {
          result.largestKick = std::max(result.largestKick, now - before);
        }
        before = now;
        for (const TouchEvent& event : std::span(harness.events).subspan(eventsBefore)) {
          if (!isPassedThrough(event.touch)) {
            first = event.touch;
            break;
          }
        }
        if (first == Touch::None && harness.flipperTouches > touchesBefore) {
          first = Touch::Flipper;
        }
        if (first == Touch::None && drain != Touch::None) {
          first = drain;
        }
        if (first != Touch::None) {
          break;
        }
        if (i > 240 && atRestOnPlunger(harness)) {
          back = true;
          break;
        }
      }
      ++result.launches;
      if (back) {
        ++result.backOnPlunger;
        continue;
      }
      ++result.first[static_cast<std::size_t>(first)];
      if (!isSafeFirstTouch(first)) {
        ++result.unsafe;
      }
    }
  }
  return result;
}

DescentResult slowDescents(int stride) {
  DescentResult result;
  int n = 0;
  for (int side = 0; side < 2; ++side) {
    for (int ix = 0; ix < 6; ++ix) {
      for (int iy = 0; iy < 5; ++iy) {
        for (int iv = 0; iv < 4; ++iv) {
          for (int il = 0; il < 3; ++il) {
            if (n++ % std::max(stride, 1) != 0) {
              continue;
            }
            TableHarness harness;
            // Across the lane, down its length, at 0 to 450 px/s down.
            const Vec2 p{101.0F + static_cast<float>(ix) * 7.4F,
                         300.0F + static_cast<float>(iy) * 45.0F};
            const Vec2 v{(static_cast<float>(il) - 1.0F) * 70.0F, static_cast<float>(iv) * 150.0F};
            harness.put(0, mirrorPoint(p, side == 1), mirrorVelocity(v, side == 1), true);
            Touch first = Touch::None;
            bool touched = false;
            for (int i = 0; i < kStepsPerSecond * 8; ++i) {
              const std::size_t eventsBefore = harness.events.size();
              const std::uint64_t touchesBefore = harness.flipperTouches;
              const Touch drain = harness.advance();
              if (first == Touch::None && harness.events.size() > eventsBefore) {
                first = harness.events[eventsBefore].touch;
              }
              if (harness.flipperTouches > touchesBefore) {
                touched = true;
                if (first == Touch::None) {
                  first = Touch::Flipper;
                }
              }
              if (drain != Touch::None) {
                if (drain == Touch::DrainCentre && !touched) {
                  ++result.throughGapUntouched;
                }
                if (first == Touch::None) {
                  first = drain;
                }
                break;
              }
            }
            ++result.descents;
            ++result.first[static_cast<std::size_t>(first)];
          }
        }
      }
    }
  }
  return result;
}

SkillShotResult skillShotDrains(int holdStride, int offsets) {
  SkillShotResult result;
  for (int hold = 6; hold <= 125; hold += std::max(holdStride, 1)) {
    for (int j = 0; j < std::clamp(offsets, 1, 8); ++j) {
      TableHarness harness;
      plunge(harness, j, hold);
      bool first = true;
      bool lane = false;
      bool touched = false;
      Touch drain = Touch::None;
      for (int i = 0; i < kStepsPerSecond * 25 && drain == Touch::None; ++i) {
        const std::size_t eventsBefore = harness.events.size();
        const std::uint64_t touchesBefore = harness.flipperTouches;
        drain = harness.advance();
        if (first && harness.events.size() > eventsBefore) {
          lane = harness.events[eventsBefore].touch == Touch::RollCall;
          first = false;
        }
        touched = touched || harness.flipperTouches > touchesBefore;
      }
      if (!lane) {
        continue;
      }
      ++result.reachedRollCall;
      if (drain == Touch::DrainCentre && !touched) {
        ++result.throughGapUntouched;
      }
    }
  }
  return result;
}

AutoLaunchRow autoLaunchAt(int speed, int launches) {
  AutoLaunchRow row;
  row.speed = speed;
  for (int j = 0; j < launches; ++j) {
    TableHarness harness;
    harness.put(
        0, {table::kBallStart.x + (static_cast<float>(j % 8) - 3.5F) * 0.4F, table::kBallStart.y},
        {}, false);
    for (int i = 0; i < 20 + j / 8; ++i) {
      harness.advance();
    }
    harness.put(0, harness.position(0), {0.0F, -static_cast<float>(speed)}, false);
    Touch first = Touch::None;
    bool back = false;
    for (int i = 0; i < kStepsPerSecond * 12; ++i) {
      const FirstTouch touch = firstTouch(harness, 1);
      if (touch.touch != Touch::None) {
        first = touch.touch;
        break;
      }
      if (i > 300 && !harness.game().ballInPlay(0) && harness.position(0).y > 870.0F &&
          std::fabs(harness.velocity(0).y) < 5.0F) {
        back = true;
        break;
      }
    }
    ++row.launches;
    if (back) {
      ++row.backOnPlunger;
    } else if (first == Touch::InlaneLeft) {
      ++row.leftInlane;
    } else if (first == Touch::RollCall) {
      ++row.rollCall;
    } else if (first == Touch::InlaneRight) {
      ++row.rightInlane;
    } else {
      ++row.other;
    }
  }
  return row;
}

GameAutoLaunchResult gameAutoLaunch(int launches) {
  GameAutoLaunchResult result;
  for (int j = 0; j < launches; ++j) {
    TableHarness harness;
    // The only ball is dropped out of the bottom with another asked for:
    // the game serves that one and launches it by itself.
    harness.put(0, {300.0F + static_cast<float>(j), table::kHeight - 20.0F}, {}, true);
    if (!harness.game().requestBall()) {
      continue;
    }
    for (int i = 0; i < kStepsPerSecond && harness.advance() == Touch::None; ++i) {
    }
    harness.events.clear();
    const FirstTouch touch = firstTouch(harness, kStepsPerSecond * 12);
    ++result.launches;
    result.safeFirstTouch += isSafeFirstTouch(touch.touch) ? 1 : 0;
    result.leftInlane += touch.touch == Touch::InlaneLeft ? 1 : 0;
    result.relaunches += harness.game().autoRelaunchCount();
  }
  return result;
}

OneWayResult oneWayArch(int balls) {
  OneWayResult result;
  // The arch part of the chain, ghosts and the straight wall aside.
  constexpr std::size_t kFirst = 3;
  constexpr std::size_t kLast = table::kInnerArch.size() - 2;
  for (int k = 0; k < balls; ++k) {
    TableHarness harness;
    const std::size_t index = kFirst + static_cast<std::size_t>(k) * 7U % (kLast - kFirst + 1U);
    const Vec2 target = table::kInnerArch[index];
    // From 70 units inside, straight out at the arch (the line from the
    // arch's centre is near enough its normal), a little off to one side.
    const Vec2 inward = table::kArchCenter - target;
    const float length = speedOf(inward);
    const Vec2 unit = inward * (1.0F / length);
    const Vec2 start = target + unit * 70.0F;
    const float speed = 300.0F + 150.0F * static_cast<float>(k % 15);
    const float skew = (static_cast<float>(k % 5) - 2.0F) * 0.15F;
    const Vec2 velocity{(-unit.x + skew * unit.y) * speed, (-unit.y - skew * unit.x) * speed};
    harness.put(0, start, velocity, true);
    bool crossed = false;
    for (int i = 0; i < kStepsPerSecond * 3 && !crossed; ++i) {
      if (harness.advance() != Touch::None) {
        break;
      }
      const Vec2 p = harness.position(0);
      if (p.y < table::kArchCenter.y) {
        const float ex = (p.x - table::kArchCenter.x) / table::kArchRadiusX;
        const float ey = (p.y - table::kArchCenter.y) / table::kArchRadiusY;
        crossed = p.x > table::kArchCenter.x && ex * ex + ey * ey > 1.0F;
      } else {
        crossed = p.x > table::kLaneWallX + table::kWallRadius;
      }
    }
    ++result.fired;
    result.crossed += crossed || harness.laneIntrusions > 0 ? 1 : 0;
  }
  return result;
}

// --- The shot sweep ------------------------------------------------------------------

namespace {

struct Feed {
  const char* name;
  Vec2 position;  // For the left flipper; mirrored for the right.
  Vec2 velocity;
  bool cradle;
};
// TABLE.md 5.1: the standard feeds, and a ball crossing the field.
constexpr std::array<Feed, 7> kFeeds{{
    {"inlane", {107.0F, 690.0F}, {0.0F, 0.0F}, false},
    {"ramp exit", {107.0F, 672.0F}, {0.0F, 80.0F}, false},
    {"circuit return", {112.0F, 470.0F}, {0.0F, 700.0F}, false},
    {"cradle", {0.0F, 0.0F}, {0.0F, 0.0F}, true},
    {"drop mid", {300.0F, 640.0F}, {0.0F, 0.0F}, false},
    {"drop tip", {322.0F, 640.0F}, {0.0F, 0.0F}, false},
    {"cross", {470.0F, 560.0F}, {-330.0F, 0.0F}, false},
}};
constexpr std::size_t kInlaneFeed = 0;
constexpr std::size_t kRampExitFeed = 1;

struct Attempt {
  Touch shot = Touch::None;
  bool reachedArch = false;
  bool completed = false;
  bool throughGapUntouched = false;
  // Dry run only: the first and last step the ball was on the flipper.
  int contactFirst = -1;
  int contactLast = -1;
};

void setUpFeed(TableHarness& harness, const Feed& feed, bool rightSide, int variant) {
  // Variant 0 is the feed as written. The others move it a little, to give
  // more first hits; they are not used for the timing windows.
  const float dx = static_cast<float>((variant * 3) % 7 - (variant == 0 ? 0 : 3)) * 0.6F;
  const float dv = static_cast<float>((variant * 5) % 9 - (variant == 0 ? 0 : 4)) * 6.0F;
  if (feed.cradle) {
    (rightSide ? harness.right : harness.left) = true;
    for (int i = 0; i < 20; ++i) {
      harness.advance();
    }
    const Vec2 pivot = table::kFlippers[rightSide ? 1 : 0].pivot;
    harness.put(0, {pivot.x + (rightSide ? -45.0F : 45.0F) + dx, pivot.y - 60.0F}, {}, true);
    for (int i = 0; i < 240; ++i) {
      harness.advance();
    }
    (rightSide ? harness.right : harness.left) = false;
  } else {
    const Vec2 position{feed.position.x + dx, feed.position.y};
    const Vec2 velocity{feed.velocity.x, feed.velocity.y + (feed.velocity.y > 0.0F ? dv : 0.0F)};
    harness.put(0, mirrorPoint(position, rightSide), mirrorVelocity(velocity, rightSide), true);
  }
  harness.events.clear();
}

// One go: the scripted bot presses at step pressStep after the feed is set
// up (never, for a dry run).
Attempt attempt(const Feed& feed, bool rightSide, int variant, int pressStep, bool bankDown) {
  const bool dry = pressStep < 0;
  TableHarness harness;
  if (bankDown) {
    harness.setLocks(false);
    GameTestAccess::dropDoorBank(harness.game());
  }
  setUpFeed(harness, feed, rightSide, variant);
  const std::size_t flipper = rightSide ? table::kRightFlipperIndex : table::kLeftFlipperIndex;
  ScriptedBot bot(flipper, pressStep);
  Attempt result;
  bool decided = false;
  std::uint64_t touchesAtHit = 0;
  std::uint64_t topsAtHit = 0;
  std::uint64_t doneAtHit = 0;
  for (int i = 0; i < kStepsPerSecond * 7; ++i) {
    if (dry && harness.nearFlipper(harness.position(0), flipper, 1.0F)) {
      if (result.contactFirst < 0) {
        result.contactFirst = i;
      }
      result.contactLast = i;
    }
    harness.act(bot);
    const std::size_t eventsBefore = harness.events.size();
    const Touch drain = harness.advance();
    const bool isCircuit = result.shot == Touch::CircuitLeft || result.shot == Touch::CircuitRight;
    if (i >= pressStep && !dry && !decided) {
      for (const TouchEvent& event : std::span(harness.events).subspan(eventsBefore)) {
        if (event.touch == Touch::InlaneLeft || event.touch == Touch::InlaneRight) {
          continue;
        }
        result.shot = event.touch;
        decided = true;
        touchesAtHit = harness.flipperTouches;
        topsAtHit = harness.circuitTop[0] + harness.circuitTop[1];
        doneAtHit = harness.circuitDone[0] + harness.circuitDone[1];
        break;
      }
    } else if (decided && isCircuit) {
      result.reachedArch =
          result.reachedArch || harness.circuitTop[0] + harness.circuitTop[1] > topsAtHit;
      result.completed =
          result.completed || harness.circuitDone[0] + harness.circuitDone[1] > doneAtHit;
    }
    if (drain != Touch::None) {
      result.throughGapUntouched =
          decided && drain == Touch::DrainCentre && harness.flipperTouches == touchesAtHit;
      break;
    }
    if (decided && !isCircuit && i - pressStep > 300) {
      break;
    }
    if (decided && i - pressStep > 600) {
      break;
    }
  }
  return result;
}

}  // namespace

char touchChar(Touch touch) noexcept {
  static constexpr char kChars[] = ".OLwDHSPWRQbs^ijxyc*";
  const auto index = static_cast<std::size_t>(touch);
  return index < sizeof(kChars) - 1 ? kChars[index] : '?';
}

ShotMap shotSweep(int variants, bool bankDown) {
  ShotMap map;
  for (int variant = 0; variant < std::max(variants, 1); ++variant) {
    for (int side = 0; side < 2; ++side) {
      for (std::size_t f = 0; f < kFeeds.size(); ++f) {
        const Feed& feed = kFeeds[f];
        const Attempt dry = attempt(feed, side == 1, variant, -1, bankDown);
        if (dry.contactFirst < 0) {
          continue;  // Never reaches the flipper.
        }
        const int first = std::max(0, dry.contactFirst - 2);
        const int last = std::min(dry.contactLast, dry.contactFirst + 140) + 2;
        std::string line = std::string(side == 1 ? "R " : "L ") + feed.name + ": ";
        std::array<int, kTouchCount> bestRun{};
        Touch previous = Touch::Count;
        int run = 0;
        for (int k = first; k <= last; ++k) {
          const Attempt made = attempt(feed, side == 1, variant, k, bankDown);
          Touch shot = made.shot;
          ShotWindow& window = map.shots[static_cast<std::size_t>(shot)];
          ++window.firstHits;
          window.throughGapUntouched += made.throughGapUntouched ? 1 : 0;
          window.reachedArch += made.reachedArch ? 1 : 0;
          window.completed += made.completed ? 1 : 0;
          // A circuit that did not reach the arch is not a made circuit.
          if ((shot == Touch::CircuitLeft || shot == Touch::CircuitRight) && !made.reachedArch) {
            shot = Touch::None;
          }
          line.push_back(touchChar(shot));
          run = shot == previous ? run + 1 : 1;
          previous = shot;
          bestRun[static_cast<std::size_t>(shot)] =
              std::max(bestRun[static_cast<std::size_t>(shot)], run);
        }
        if (variant != 0) {
          continue;
        }
        map.fans.push_back(line);
        for (std::size_t s = 1; s < kTouchCount; ++s) {
          ShotWindow& window = map.shots[s];
          window.best = std::max(window.best, bestRun[s]);
          if (feed.cradle) {
            window.cradle = std::max(window.cradle, bestRun[s]);
          } else if (f == kInlaneFeed) {
            window.inlane = std::max(window.inlane, bestRun[s]);
          } else if (f == kRampExitFeed) {
            window.rampExit = std::max(window.rampExit, bestRun[s]);
          }
        }
      }
    }
  }
  return map;
}

LaneResult circuitAt(bool rightSide, int speed) {
  LaneResult result;
  double seconds = 0.0;
  const Touch farInlane = rightSide ? Touch::InlaneLeft : Touch::InlaneRight;
  const Touch nearInlane = rightSide ? Touch::InlaneRight : Touch::InlaneLeft;
  for (int j = 0; j < 40; ++j) {
    TableHarness harness;
    const Vec2 p{102.0F + static_cast<float>(j % 10) * 2.6F, 470.0F};
    const Vec2 v{(static_cast<float>(j / 10) - 1.5F) * 60.0F, -static_cast<float>(speed)};
    harness.put(0, mirrorPoint(p, rightSide), mirrorVelocity(v, rightSide), true);
    ++result.balls;
    for (int i = 0; i < kStepsPerSecond * 6; ++i) {
      const std::size_t eventsBefore = harness.events.size();
      const Touch drain = harness.advance();
      bool over = drain != Touch::None;
      for (const TouchEvent& event : std::span(harness.events).subspan(eventsBefore)) {
        if (isPassedThrough(event.touch)) {
          continue;
        }
        if (event.touch == farInlane) {
          ++result.completed;
          const double t = static_cast<double>(i) / kStepsPerSecond;
          seconds += t;
          result.worstSeconds = std::max(result.worstSeconds, t);
        } else if (event.touch == Touch::RollCall || event.touch == Touch::Bumper) {
          ++result.rollCallOrNest;
        } else if (event.touch != nearInlane && event.touch != Touch::Slingshot) {
          // Anything else: neither made nor fallen back.
        }
        over = true;
        break;
      }
      if (over) {
        break;
      }
    }
  }
  result.meanSeconds = result.completed > 0 ? seconds / result.completed : 0.0;
  return result;
}

LaneResult rampAt(bool rightSide, int speed) {
  LaneResult result;
  const table::Hole& hole = table::kHoles[rightSide ? table::kSkywayHole : table::kRunwayHole];
  for (int j = 0; j < 40; ++j) {
    TableHarness harness;
    const float x = hole.center.x - hole.halfSize.x + 2.0F +
                    static_cast<float>(j % 10) * (2.0F * hole.halfSize.x - 4.0F) / 9.0F;
    const float vx = (static_cast<float>(j / 10) - 1.5F) * 40.0F;
    harness.put(0, {x - vx * 0.03F, 470.0F}, {vx, -static_cast<float>(speed)}, true);
    ++result.balls;
    for (int i = 0; i < kStepsPerSecond && !harness.held(0); ++i) {
      harness.advance();
    }
    result.completed += harness.held(0) ? 1 : 0;
  }
  return result;
}

ExitResult exitAt(Vec2 position, Vec2 velocity, int releases) {
  ExitResult result;
  for (int j = 0; j < releases; ++j) {
    TableHarness harness;
    const float jx = (static_cast<float>(j % 20) - 9.5F) * 0.05F;
    const float jv = (static_cast<float>(j / 20 % 10) - 4.5F) * 2.0F;
    harness.put(0, {position.x + jx, position.y}, {velocity.x + jv, velocity.y}, true);
    const FirstTouch touch = firstTouch(harness, kStepsPerSecond * 10);
    ++result.releases;
    ++result.first[static_cast<std::size_t>(touch.touch)];
    result.safe += touch.touch == Touch::Flipper || touch.touch == Touch::InlaneLeft ||
                           touch.touch == Touch::InlaneRight
                       ? 1
                       : 0;
  }
  return result;
}

ExitResult scoopKicks(int releases) {
  ExitResult result;
  const table::Hole& scoop = table::kHoles[table::kScoopHole];
  const int across = table::kScoopExitJitterLeft + table::kScoopExitJitterRight + 1;
  const int along = 2 * table::kScoopExitJitterDown + 1;
  for (int j = 0; j < releases; ++j) {
    TableHarness harness;
    // Every corner of the range, then through it both ways at once.
    const int ix = j < 4 ? (j % 2) * (across - 1) : (j * 7) % across;
    const int iy = j < 4 ? (j / 2) * (along - 1) : (j * 11) % along;
    harness.put(0, scoop.exitPosition,
                {scoop.exitVelocity.x + static_cast<float>(ix - table::kScoopExitJitterLeft),
                 scoop.exitVelocity.y + static_cast<float>(iy - table::kScoopExitJitterDown)},
                true);
    const FirstTouch touch = firstTouch(harness, kStepsPerSecond * 10);
    ++result.releases;
    ++result.first[static_cast<std::size_t>(touch.touch)];
    result.safe += touch.touch == Touch::Flipper || touch.touch == Touch::InlaneLeft ||
                           touch.touch == Touch::InlaneRight
                       ? 1
                       : 0;
  }
  return result;
}

ExitResult exitOf(std::size_t hole, int releases) {
  if (hole >= table::kHoles.size()) {
    return {};
  }
  const table::Hole& from = table::kHoles[hole];
  return exitAt(from.exitPosition, from.exitVelocity, releases);
}

namespace {

// Follows every ball that the game returns from a hole to the first thing
// it meets: per ball, since several are on the table.
struct Followed {
  bool following = false;
  bool decided = false;
  Touch first = Touch::None;
  std::uint64_t flipperStepAtStart = 0;
};

// Sends the ball in a slot up the middle of the open Hangar.
void shootHangar(TableHarness& harness, std::size_t slot, float dx, float speed) {
  harness.put(slot, {279.0F + dx, 440.0F}, {0.0F, -speed}, true);
}

std::size_t freeSlot(const TableHarness& harness) {
  for (std::size_t slot = 0; slot < table::kMaxBalls; ++slot) {
    if (harness.live(slot) && !harness.held(slot)) {
      return slot;
    }
  }
  return table::kMaxBalls;
}

}  // namespace

LockBurstResult lockBursts(int bursts) {
  LockBurstResult result;
  const int spacing = static_cast<int>(table::kLockReleaseSpacingSeconds * kStepsPerSecond + 0.5);
  for (int j = 0; j < bursts; ++j) {
    TableHarness harness;
    Game& game = harness.game();
    harness.kickbackOff = true;
    harness.advance();
    // Open the Hangar with the lock lit, as three door hits do.
    for (std::size_t door = 0; door < table::kDropTargets.size(); ++door) {
      GameTestAccess::rules(game).onDropTarget(door);
    }
    harness.advance();
    const float dx = (static_cast<float>(j % 9) - 4.0F) * 3.0F;
    const float speed = 700.0F + static_cast<float>(j % 7) * 250.0F;
    // Two locks, then the third ball in.
    bool ready = true;
    for (int n = 0; n < 3 && ready; ++n) {
      const std::size_t slot = freeSlot(harness);
      ready = slot < table::kMaxBalls;
      if (!ready) {
        break;
      }
      shootHangar(harness, slot, dx, speed);
      for (int i = 0; i < kStepsPerSecond && !harness.held(slot); ++i) {
        harness.advance();
      }
      ready = harness.held(slot);
      // A different wait before the next each time.
      for (int i = 0; i < (n < 2 ? 10 + j % 50 : 0); ++i) {
        harness.advance();
      }
    }
    if (!ready || game.lockedBalls() != 2) {
      continue;
    }
    ++result.bursts;
    harness.events.clear();
    std::array<Followed, table::kMaxBalls> followed{};
    std::array<std::size_t, 3> order{table::kMaxBalls, table::kMaxBalls, table::kMaxBalls};
    std::array<int, 3> leftAt{};
    std::size_t left = 0;
    // The bays' balls, by bay, before they go.
    std::array<std::size_t, 2> inBay{table::kMaxBalls, table::kMaxBalls};
    for (const BallView& view : game.balls()) {
      for (std::size_t bay = 0; bay < table::kLockBays.size(); ++bay) {
        if (view.locked && view.position.x == table::kLockBays[bay].x) {
          inBay[bay] = view.id;
        }
      }
    }
    const std::size_t arriving = 3U - inBay[0] - inBay[1];
    const std::uint64_t astrayBefore = harness.lockedAstray;
    for (int i = 0; i < kStepsPerSecond * 12; ++i) {
      const std::size_t eventsBefore = harness.events.size();
      harness.advance();
      for (const GameEvent& event : game.gameEvents()) {
        if (event.type != GameEvent::Type::HoleEject || left >= order.size()) {
          continue;
        }
        const std::size_t slot = event.ball;
        order[left] = slot;
        leftAt[left] = i;
        ++left;
        // A little off the exact release, as a real kick is.
        const int k = j * 3 + static_cast<int>(left);
        const Vec2 jitterPosition{(static_cast<float>(k % 20) - 9.5F) * 0.05F, 0.0F};
        const Vec2 jitterVelocity{(static_cast<float>(k / 20 % 10) - 4.5F) * 2.0F, 0.0F};
        GameTestAccess::placeBall(game, slot, harness.position(slot) + jitterPosition,
                                  harness.velocity(slot) + jitterVelocity);
        followed[slot] = {true, false, Touch::None, harness.lastFlipperStep(slot)};
      }
      for (const PhysicsEvent& event : game.events()) {
        if (event.type == PhysicsEvent::Type::BallHit &&
            (!followed[event.ball].decided || !followed[event.otherBall].decided)) {
          ++result.ballHits;
        }
      }
      for (const TouchEvent& event : std::span(harness.events).subspan(eventsBefore)) {
        Followed& ball = followed[event.ball];
        if (ball.following && !ball.decided && !isPassedThrough(event.touch)) {
          ball.decided = true;
          ball.first = event.touch;
        }
      }
      for (std::size_t slot = 0; slot < table::kMaxBalls; ++slot) {
        Followed& ball = followed[slot];
        if (ball.following && !ball.decided &&
            harness.lastFlipperStep(slot) != ball.flipperStepAtStart) {
          ball.decided = true;
          ball.first = Touch::Flipper;
        }
        if (ball.following && !ball.decided && !harness.live(slot)) {
          ball.decided = true;
          ball.first = Touch::DrainCentre;
        }
      }
      if (harness.events.size() > 4096) {
        harness.events.clear();
      }
    }
    result.heldMoved += static_cast<int>(harness.lockedAstray - astrayBefore);
    result.released += static_cast<int>(left);
    if (left != 3 || order[0] != arriving || order[1] != inBay[0] || order[2] != inBay[1]) {
      ++result.outOfOrder;
    }
    for (std::size_t n = 1; n < left; ++n) {
      result.badSpacing += leftAt[n] - leftAt[n - 1] == spacing ? 0 : 1;
    }
    for (std::size_t n = 0; n < left; ++n) {
      const Touch first = followed[order[n]].first;
      ++result.first[static_cast<std::size_t>(first)];
      result.safe +=
          first == Touch::Flipper || first == Touch::InlaneLeft || first == Touch::InlaneRight ? 1
                                                                                               : 0;
    }
  }
  return result;
}

ScoopPairResult scoopPairs(int pairs) {
  ScoopPairResult result;
  const table::Hole& scoop = table::kHoles[table::kScoopHole];
  const int hold = static_cast<int>(table::kScoopHoldSeconds * kStepsPerSecond + 0.5);
  for (int j = 0; j < pairs; ++j) {
    TableHarness harness;
    Game& game = harness.game();
    harness.kickbackOff = true;
    harness.advance();
    const float dx = (static_cast<float>(j % 5) - 2.0F) * 3.0F;
    harness.put(0, {scoop.center.x + dx, 440.0F},
                {0.0F, -700.0F - static_cast<float>(j % 4) * 300.0F}, true);
    // The second follows the first by anything from nothing to past the
    // moment the first comes out.
    const int gap = (j * 7) % (hold + 30);
    std::size_t second = table::kMaxBalls;
    std::array<Followed, table::kMaxBalls> followed{};
    std::array<int, 2> outAt{-1, -1};
    std::size_t out = 0;
    int overlapRun = 0;
    for (int i = 0; i < kStepsPerSecond * 10; ++i) {
      if (i == gap) {
        second = harness.add({scoop.center.x - dx, 470.0F}, {0.0F, -900.0F});
      }
      const std::size_t eventsBefore = harness.events.size();
      harness.advance();
      for (const GameEvent& event : game.gameEvents()) {
        if (event.type == GameEvent::Type::HoleEject && event.index == table::kScoopHole &&
            out < outAt.size()) {
          outAt[out++] = i;
          followed[event.ball] = {true, false, Touch::None, harness.lastFlipperStep(event.ball)};
        }
      }
      for (const TouchEvent& event : std::span(harness.events).subspan(eventsBefore)) {
        Followed& ball = followed[event.ball];
        if (ball.following && !ball.decided && !isPassedThrough(event.touch)) {
          ball.decided = true;
          ball.first = event.touch;
        }
      }
      for (std::size_t slot = 0; slot < table::kMaxBalls; ++slot) {
        Followed& ball = followed[slot];
        if (ball.following && !ball.decided &&
            harness.lastFlipperStep(slot) != ball.flipperStepAtStart) {
          ball.decided = true;
          ball.first = Touch::Flipper;
        }
      }
      bool overlap = false;
      if (second < table::kMaxBalls && harness.live(0) && harness.live(second) &&
          !harness.held(0) && !harness.held(second)) {
        const Vec2 d = harness.position(0) - harness.position(second);
        constexpr float kDeepest = 2.0F * table::kBallRadius - 2.0F;
        overlap = d.x * d.x + d.y * d.y < kDeepest * kDeepest;
      }
      overlapRun = overlap ? overlapRun + 1 : 0;
      result.longestOverlap = std::max(result.longestOverlap, overlapRun);
      if (out == 2 && followed[0].decided && followed[1].decided) {
        break;
      }
      if (out == 1 && harness.holeHits[table::kScoopHole] == 1 && i > gap + 3 * kStepsPerSecond) {
        break;  // The second never got in.
      }
    }
    if (harness.holeHits[table::kScoopHole] < 2) {
      // It met the first ball coming out, as any ball in play may.
      ++result.turnedAway;
    } else {
      ++result.pairs;
      result.bothOut += out == 2 ? 1 : 0;
      result.tooClose += out == 2 && outAt[1] - outAt[0] < hold ? 1 : 0;
    }
    for (const Followed& ball : followed) {
      result.returned += ball.following ? 1 : 0;
      result.safe += ball.decided && (ball.first == Touch::Flipper ||
                                      ball.first == Touch::InlaneLeft ||
                                      ball.first == Touch::InlaneRight)
                         ? 1
                         : 0;
    }
  }
  return result;
}

KickbackResult kickbackSweep(int balls) {
  KickbackResult result;
  for (int j = 0; j < balls; ++j) {
    TableHarness harness;
    harness.lightKickback();
    harness.advance();
    // Down the outlane from above its switch, or thrown in under the
    // pocket's roof from the circuit lane's foot, at every speed.
    const float x = 52.0F + static_cast<float>(j % 8) * 2.0F;
    const float speed = static_cast<float>(j / 8 % 15) * 200.0F;
    const float vx = (static_cast<float>(j / 120 % 5) - 2.0F) * 50.0F;
    if (j % 3 == 2) {
      harness.put(0, {92.0F, 585.0F}, {-180.0F - vx, 60.0F + speed * 0.3F}, true);
    } else {
      harness.put(0, {x, 660.0F}, {vx, speed}, true);
    }
    // Followed until the kickback has set it down and it has met something,
    // or it has left the table.
    Touch drain = Touch::None;
    for (int i = 0; i < kStepsPerSecond * 8 && drain == Touch::None; ++i) {
      drain = harness.advance();
      if (harness.kickbackReturns > 0 && harness.kickbackSafe > 0) {
        break;
      }
    }
    // Only a ball that went down the left outlane is part of this.
    if (harness.count[static_cast<std::size_t>(Touch::OutlaneLeft)] == 0) {
      continue;
    }
    ++result.balls;
    result.fired += harness.kickbacks > 0 ? 1 : 0;
    result.returned += harness.kickbackReturns > 0 ? 1 : 0;
    result.safe += harness.kickbackSafe > 0 ? 1 : 0;
    result.lostByOutlane += harness.kickbacks == 0 ? 1 : 0;
  }
  return result;
}

KickbackPlayResult kickbackUnderBot(int reflexError, int balls) {
  KickbackPlayResult result;
  for (int j = 0; j < balls; ++j) {
    TableHarness harness;
    harness.setLocks(false);
    harness.lightKickback();
    harness.advance();
    ReflexBot bot(4000U + static_cast<std::uint64_t>(j), reflexError);
    harness.put(0, {53.0F + static_cast<float>(j % 13), 660.0F},
                {0.0F, 100.0F + static_cast<float>(j % 7) * 100.0F}, true);
    // Until it is set down, then three seconds of play.
    int since = -1;
    for (int i = 0; i < kStepsPerSecond * 6 && since < kStepsPerSecond * 3; ++i) {
      harness.act(bot);
      const Touch drain = harness.advance();
      if (since < 0 && harness.kickbackReturns > 0) {
        since = 0;
        ++result.kicks;
      } else if (since >= 0) {
        ++since;
      }
      if (drain != Touch::None) {
        result.lostAgain += since >= 0 ? 1 : 0;
        break;
      }
    }
  }
  return result;
}

RestResult restingPlaces(int balls, std::uint64_t seed) {
  RestResult result;
  Rng rng(seed);
  while (result.placed < balls) {
    const Vec2 p{100.0F + rng.unit() * 510.0F, 90.0F + rng.unit() * 760.0F};
    // Inside the arch, not in the launch channel or the shell.
    const float ex = (p.x - table::kArchCenter.x) / (table::kArchRadiusX - 16.0F);
    const float ey = (p.y - table::kArchCenter.y) / (table::kArchRadiusY - 16.0F);
    if ((p.y < table::kArchCenter.y && ex * ex + ey * ey > 1.0F) || !clearOfSolids(p)) {
      continue;
    }
    const Vec2 v{rng.unit() * 1600.0F - 800.0F, rng.unit() * 1600.0F - 800.0F};
    TableHarness harness;
    harness.put(0, p, v, true);
    ++result.placed;
    Touch drain = Touch::None;
    for (int i = 0; i < kStepsPerSecond * 30 && drain == Touch::None; ++i) {
      const Vec2 before = harness.position(0);
      const std::uint32_t searches = harness.game().ballSearchCount();
      drain = harness.advance();
      if (harness.game().ballSearchCount() > searches && result.restingAt.size() < 16) {
        result.restingAt.push_back(before);
      }
    }
    result.searches += harness.game().ballSearchCount();
    if (drain == Touch::None) {
      ++result.notDrained;
      if (result.restingAt.size() < 16) {
        result.restingAt.push_back(harness.position(0));
      }
    }
  }
  return result;
}

WedgeResult inlaneWedge(int stride) {
  WedgeResult result;
  int n = 0;
  // From the foot of the circuit lane, and from just above the funnel.
  for (int side = 0; side < 4; ++side) {
    const float startY = side < 2 ? 540.0F : 640.0F;
    for (int ix = 0; ix < 6; ++ix) {
      for (int gap = 0; gap < 5; ++gap) {
        for (int iy = 0; iy < 7; ++iy) {
          for (int iv = 0; iv < 3; ++iv) {
            if (n++ % std::max(stride, 1) != 0) {
              continue;
            }
            TableHarness harness;
            // The lanes as they are, with no kickback to take a ball away.
            harness.kickbackOff = true;
            // The outer ball against the divider's side of the lane and the
            // inner one beside it, a little apart, level or one a little ahead.
            const Vec2 outer{100.0F + static_cast<float>(ix) * 1.5F, startY};
            const Vec2 inner{outer.x + 24.2F + static_cast<float>(gap) * 1.5F,
                             startY + (static_cast<float>(iy) - 3.0F) * 4.0F};
            const Vec2 v{0.0F, static_cast<float>(iv) * 120.0F};
            const bool rightSide = side % 2 == 1;
            harness.put(0, mirrorPoint(outer, rightSide), v, true);
            const std::size_t second = harness.add(mirrorPoint(inner, rightSide), v);
            bool cleared = false;
            Vec2 searchedAt{};
            for (int i = 0; i < kStepsPerSecond * 12 && !cleared; ++i) {
              const Vec2 before = harness.position(0);
              const std::uint32_t searches = harness.game().ballSearchCount();
              harness.advance();
              if (searches == 0 && harness.game().ballSearchCount() > 0) {
                searchedAt = before;
              }
              // Cleared once neither is anywhere near the inlane mouth: the
              // game has served a new ball to slot 0 by then.
              cleared = !harness.live(second) &&
                        harness.drains[0] + harness.drains[1] + harness.drains[2] >= 2;
            }
            ++result.pairs;
            if (!cleared || harness.stuck > 0 || harness.game().ballSearchCount() > 0) {
              ++result.wedged;
              if (result.wedgedAt.size() < 8) {
                result.wedgedAt.push_back(
                    harness.game().ballSearchCount() > 0 ? searchedAt : harness.position(0));
              }
            }
          }
        }
      }
    }
  }
  return result;
}

double SoakResult::percentile(double q) const {
  if (ballSeconds.empty()) {
    return 0.0;
  }
  std::vector<double> sorted = ballSeconds;
  std::ranges::sort(sorted);
  return sorted[static_cast<std::size_t>(q * static_cast<double>(sorted.size() - 1))];
}

double SoakResult::drainShare(std::size_t way) const {
  const double total = static_cast<double>(drains[0] + drains[1] + drains[2]);
  return way < drains.size() && total > 0.0 ? 100.0 * static_cast<double>(drains[way]) / total
                                            : 0.0;
}

SoakResult soak(const SoakOptions& options) {
  SoakResult result;
  TableHarness harness;
  // One ball at a time: the Hangar returns the ball, it does not lock it.
  harness.setLocks(false);
  harness.kickbackOff = !options.kickback;
  const std::unique_ptr<Bot> bot = makeBot(options.bot, options.seed, options.reflexError);
  Rng rng(options.seed ^ 0xABCDEFULL);
  const auto advance = [&harness, &result] {
    ++result.steps;
    return harness.advance();
  };
  // Holds the plunger for a number of steps and lets go.
  const auto pull = [&harness, &advance](int steps) {
    harness.plunger = true;
    for (int i = 0; i < steps; ++i) {
      advance();
    }
    harness.plunger = false;
  };
  constexpr std::uint64_t kNotStarted = ~std::uint64_t{0};
  for (int n = 0; n < options.balls; ++n) {
    // The game has a ball on the plunger: at the start, and after every drain.
    harness.left = false;
    harness.right = false;
    if (options.kickback) {
      harness.lightKickback();
    }
    for (int i = 0; i < 20; ++i) {
      advance();
    }
    pull(rng.range(60, 125));
    std::uint64_t start = kNotStarted;
    Touch way = Touch::None;
    int waiting = 0;
    while (true) {
      if (start == kNotStarted && harness.game().ballInPlay(0)) {
        start = harness.step();
      }
      if (start != kNotStarted) {
        harness.act(*bot);
      } else {
        harness.left = false;
        harness.right = false;
      }
      const Touch drain = advance();
      const double played = start == kNotStarted
                                ? 0.0
                                : static_cast<double>(harness.step() - start) / kStepsPerSecond;
      if (drain != Touch::None && start != kNotStarted && played < options.saverSeconds) {
        // Saved: back on the plunger, launched at full power, the clock runs on.
        ++result.saves;
        harness.left = false;
        harness.right = false;
        pull(125);
        for (int i = 0; i < kStepsPerSecond * 6 && !harness.game().ballInPlay(0); ++i) {
          advance();
        }
        continue;
      }
      if (drain != Touch::None) {
        way = drain;
        break;
      }
      if (start == kNotStarted && ++waiting > kStepsPerSecond * 20) {
        // A weak plunge came back: again, at full power.
        pull(125);
        waiting = 0;
        ++result.replunges;
      }
      if (start != kNotStarted && harness.step() - start > kStepsPerSecond * 60U * 20U) {
        // Twenty minutes: end the ball by hand so that the run goes on.
        ++result.timeouts;
        harness.put(0, {table::kFieldCenterX, table::kDrainY + 4.0F}, {}, true);
        advance();
        break;
      }
    }
    harness.events.clear();
    if (start == kNotStarted) {
      continue;
    }
    const double seconds = static_cast<double>(harness.step() - start) / kStepsPerSecond;
    result.ballSeconds.push_back(seconds);
    result.under5 += seconds < 5.0 ? 1 : 0;
    result.under10 += seconds < 10.0 ? 1 : 0;
    mix(result.fingerprint, harness.step());
    mix(result.fingerprint, static_cast<std::uint64_t>(way));
  }
  // Only the drains that ended a ball: a saved ball's drain is not one.
  result.drains = harness.drains;
  result.stuck = harness.stuck;
  result.stuckAt = harness.stuckAt;
  result.searches = harness.game().ballSearchCount();
  result.laneIntrusions = harness.laneIntrusions;
  result.lostBalls = harness.lostBalls;
  result.overSpeed = harness.overSpeed;
  result.count = harness.count;
  result.bumperHits = harness.bumperHits;
  result.targetHits = harness.targetHits;
  result.doorHits = harness.doorHits;
  result.rolloverHits = harness.rolloverHits;
  result.holeHits = harness.holeHits;
  result.slingHits = harness.slingHits;
  result.circuitEntered = harness.circuitEntered;
  result.circuitTop = harness.circuitTop;
  result.circuitDone = harness.circuitDone;
  result.hangarEntries = harness.hangarEntries;
  result.hangarEntriesDoorsUp = harness.hangarEntriesDoorsUp;
  result.bankDrops = harness.bankDrops;
  result.bankRaises = harness.bankRaises;
  result.spins = harness.spins;
  result.kickbacks = harness.kickbacks;
  result.kickbackReturns = harness.kickbackReturns;
  result.kickbackSafe = harness.kickbackSafe;
  result.kickbackLostAgain = harness.kickbackLostAgain;
  result.ejectSaves = harness.ejectSaves;
  return result;
}

StressResult stress(const StressOptions& options) {
  StressResult result;
  TableHarness harness;
  Game& game = harness.game();
  const std::unique_ptr<Bot> bot = makeBot(options.bot, options.seed, options.reflexError);
  const std::size_t wanted = std::clamp<std::size_t>(options.balls, 1, table::kMaxBalls);
  int inLane = 0;
  int pullLeft = 0;
  std::uint64_t overlapRun = 0;
  std::array<std::uint64_t, table::kMaxBalls> laneSteps{};
  for (std::uint64_t n = 0; n < options.steps; ++n) {
    // Keep the table full: every ball that leaves is asked for again and
    // the game serves and launches it by itself.
    while (game.ballCount() + game.queuedBalls() < wanted && game.requestBall()) {
    }
    // A ball served for the player to launch (the first of a game) is
    // plunged at full power once it has waited a moment.
    bool waiting = false;
    for (std::size_t slot = 0; slot < table::kMaxBalls; ++slot) {
      waiting = waiting ||
                (harness.live(slot) && !harness.held(slot) && !game.ballInPlay(slot) &&
                 harness.position(slot).y > 860.0F && std::fabs(harness.velocity(slot).y) < 8.0F);
    }
    inLane = waiting ? inLane + 1 : 0;
    if (pullLeft == 0 && inLane > kStepsPerSecond) {
      pullLeft = 125;
    }
    harness.plunger = pullLeft > 0;
    pullLeft = std::max(pullLeft - 1, 0);

    harness.act(*bot);
    const std::array<std::uint64_t, 3> drainsBefore = harness.drains;
    harness.advance();
    for (std::size_t way = 0; way < 3; ++way) {
      if (harness.drains[way] != drainsBefore[way]) {
        mix(result.fingerprint, n * 4U + way);
      }
    }
    if (game.ballCount() + game.queuedBalls() > wanted || game.ballCount() == 0) {
      ++result.countBroken;
    }
    std::array<Vec2, table::kMaxBalls> free{};
    std::size_t freeCount = 0;
    for (std::size_t slot = 0; slot < table::kMaxBalls; ++slot) {
      if (harness.live(slot) && !harness.held(slot)) {
        free[freeCount++] = harness.position(slot);
      }
      laneSteps[slot] = harness.live(slot) && !game.ballInPlay(slot) ? laneSteps[slot] + 1 : 0;
      result.longestInLane = std::max(result.longestInLane, laneSteps[slot]);
    }
    result.stepsWithAllFree += freeCount == wanted ? 1U : 0U;
    bool overlap = false;
    for (std::size_t a = 0; a < freeCount; ++a) {
      for (std::size_t b = a + 1; b < freeCount; ++b) {
        const Vec2 d = free[a] - free[b];
        constexpr float kDeepest = 2.0F * table::kBallRadius - 2.0F;
        overlap = overlap || d.x * d.x + d.y * d.y < kDeepest * kDeepest;
      }
    }
    overlapRun = overlap ? overlapRun + 1 : 0;
    result.overlapSteps += overlap ? 1U : 0U;
    result.longestOverlap = std::max(result.longestOverlap, overlapRun);
    if (harness.events.size() > 4096) {
      harness.events.clear();
    }
  }
  result.steps = options.steps;
  result.drains = harness.drains[0] + harness.drains[1] + harness.drains[2];
  result.autoLaunches = game.autoLaunchCount();
  result.relaunches = game.autoRelaunchCount();
  result.stuck = harness.stuck;
  result.stuckAt = harness.stuckAt;
  result.searches = game.ballSearchCount();
  result.laneIntrusions = harness.laneIntrusions;
  result.lostBalls = harness.lostBalls;
  result.overSpeed = harness.overSpeed;
  result.hangarEntries = harness.hangarEntries;
  result.hangarEntriesDoorsUp = harness.hangarEntriesDoorsUp;
  result.bankDrops = harness.bankDrops;
  result.bankRaises = harness.bankRaises;
  result.locks = harness.locks;
  result.lockReleases = harness.lockReleases;
  result.locksEmptied = harness.locksEmptied;
  result.lockedAstray = harness.lockedAstray;
  result.spins = harness.spins;
  result.kickbacks = harness.kickbacks;
  result.kickbackReturns = harness.kickbackReturns;
  result.kickbackSafe = harness.kickbackSafe;
  result.kickbackLostAgain = harness.kickbackLostAgain;
  return result;
}

}  // namespace pinball::bots::acceptance
