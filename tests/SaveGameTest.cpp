// The saved game (core/Snapshot.hpp, core/GameSave.cpp, docs/RESUME.md):
// a game taken down and put back is the same game; every state that play
// reaches can be saved; and a file that is anything but a good save from
// this build is refused, whole, without a crash.
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "Check.hpp"
#include "GameTestAccess.hpp"
#include "Suites.hpp"
#include "bots/Bots.hpp"
#include "bots/TableHarness.hpp"
#include "core/Args.hpp"
#include "core/Game.hpp"
#include "core/Input.hpp"
#include "core/Rules.hpp"
#include "core/Scenes.hpp"
#include "core/Sha256.hpp"
#include "core/Snapshot.hpp"
#include "core/TableLayout.hpp"

namespace {

using pinball::Game;
using pinball::InputState;
using pinball::Rules;
using pinball::bots::ReflexBot;
using pinball::bots::Rng;
using pinball::bots::TableHarness;
namespace save = pinball::save;
namespace table = pinball::table;

std::span<const std::uint8_t> bytesOf(const std::string& text) {
  return {reinterpret_cast<const std::uint8_t*>(text.data()), text.size()};
}

// Recomputes a file's checksum after it has been changed by hand, so that
// what is tested is the check behind it.
void reseal(std::string& file) {
  if (file.size() < 32) {
    return;
  }
  const std::size_t body = file.size() - 32;
  const pinball::Sha256Digest sum =
      pinball::sha256(reinterpret_cast<const std::uint8_t*>(file.data()), body);
  file.replace(body, 32, reinterpret_cast<const char*>(sum.data()), sum.size());
}

// A player: the reflex bot at the flippers, the plunger pulled a while after
// a ball arrives on it, and a new game once one is over.
struct Player {
  explicit Player(std::uint64_t seed) : bot(seed, 4), rng(seed ^ 0xA5A5ULL) {}
  TableHarness harness;
  ReflexBot bot;
  Rng rng;
  int pull = 0;
  int waited = 0;

  void step() {
    Game& game = harness.game();
    waited = game.ballAwaitingLaunch() ? waited + 1 : 0;
    if (pull == 0 && waited > 20) {
      pull = rng.range(60, 125);
    }
    harness.plunger = pull > 1;
    pull = pull > 0 ? pull - 1 : 0;
    if (game.rules().ballInPlay()) {
      harness.act(bot);
    } else {
      harness.left = false;
      harness.right = false;
    }
    harness.restart = game.gameOver();
    harness.advance();
  }
};

// What a player sees and what the rules hold, for comparing two games.
bool sameOnTable(const Game& a, const Game& b) {
  const pinball::BallViews ballsA = a.balls();
  const pinball::BallViews ballsB = b.balls();
  if (ballsA.size() != ballsB.size()) {
    return false;
  }
  for (std::size_t i = 0; i < ballsA.size(); ++i) {
    const pinball::BallView& x = ballsA.items[i];
    const pinball::BallView& y = ballsB.items[i];
    if (x.id != y.id || x.position.x != y.position.x || x.position.y != y.position.y ||
        x.visible != y.visible || x.raised != y.raised || x.locked != y.locked ||
        x.scale != y.scale) {
      return false;
    }
  }
  const Rules& ra = a.rules();
  const Rules& rb = b.rules();
  if (ra.score() != rb.score() || ra.ball() != rb.ball() || ra.gameOver() != rb.gameOver() ||
      ra.tilted() != rb.tilted() || ra.multiball() != rb.multiball() ||
      ra.course() != rb.course() || ra.courseSecondsLeft() != rb.courseSecondsLeft() ||
      ra.locksHeld() != rb.locksHeld() || ra.lockLit() != rb.lockLit() ||
      ra.turboMeter() != rb.turboMeter() || ra.overdrive() != rb.overdrive() ||
      ra.comboLevel() != rb.comboLevel() || ra.bonusMultiplier() != rb.bonusMultiplier() ||
      ra.ballSaveActive() != rb.ballSaveActive() || ra.extraBalls() != rb.extraBalls() ||
      ra.rank() != rb.rank() || ra.jackpotValue() != rb.jackpotValue() ||
      ra.examShotsLeft() != rb.examShotsLeft() || ra.graduations() != rb.graduations()) {
    return false;
  }
  for (std::size_t lamp = 0; lamp < table::kLamps.size(); ++lamp) {
    if (ra.lampState(static_cast<table::LampId>(lamp)) !=
        rb.lampState(static_cast<table::LampId>(lamp))) {
      return false;
    }
  }
  for (std::size_t door = 0; door < table::kDropTargets.size(); ++door) {
    if (ra.doorLampState(door) != rb.doorLampState(door)) {
      return false;
    }
  }
  for (std::size_t i = 0; i < table::kFlippers.size(); ++i) {
    if (a.flipper(i).angle != b.flipper(i).angle) {
      return false;
    }
  }
  return a.doorBankUp() == b.doorBankUp() && a.kickbackArmed() == b.kickbackArmed() &&
         a.lockedBalls() == b.lockedBalls() && a.queuedBalls() == b.queuedBalls() &&
         a.plungerPull() == b.plungerPull() && a.stepCount() == b.stepCount() &&
         a.spinnerPhase() == b.spinnerPhase() && a.inProgress() == b.inProgress();
}

// Inputs for a stretch of play that are the same for two games.
InputState scripted(int i) {
  InputState input;
  input.plunger = (i % 900) < 100 + (i / 900) % 60;
  input.leftFlipper = (i / 29) % 2 == 0;
  input.rightFlipper = (i / 41) % 2 == 0;
  input.newGame = false;
  return input;
}

// What the states saved along the way covered.
struct Coverage {
  int saves = 0;
  int restored = 0;
  int multiball = 0;
  int locked = 0;
  int held = 0;
  int course = 0;
  int tilted = 0;
  int waiting = 0;  // A ball on the plunger.
  int queued = 0;
};

// Takes the game down and puts it back in a new Game: the file reads back
// to the same records, and the new game shows and holds what the old one
// did. Then the new one plays on for a while, which must go without harm.
void roundTrip(const Game& game, Coverage& seen, bool restore) {
  const save::Snapshot snapshot = game.snapshot();
  const bool valid = save::valid(snapshot);
  CHECK(valid);
  if (!valid) {
    std::fprintf(stderr, "  not valid at step %llu, ball %d, score %llu\n",
                 static_cast<unsigned long long>(game.stepCount()), game.rules().ball(),
                 static_cast<unsigned long long>(game.rules().score()));
    return;
  }
  const std::string file = save::encode(snapshot);
  CHECK(file.size() == save::fileSize());
  const std::optional<save::Snapshot> read = save::decode(bytesOf(file));
  CHECK(read.has_value() && *read == snapshot);
  ++seen.saves;
  seen.multiball += game.ballCount() - game.lockedBalls() >= 2 ? 1 : 0;
  seen.locked += game.lockedBalls() > 0 ? 1 : 0;
  seen.course += game.rules().courseRunning() ? 1 : 0;
  seen.tilted += game.rules().tilted() ? 1 : 0;
  seen.waiting += game.ballAwaitingLaunch() ? 1 : 0;
  seen.queued += game.queuedBalls() > 0 ? 1 : 0;
  for (const pinball::BallView& ball : game.balls()) {
    seen.held += !ball.visible || ball.raised ? 1 : 0;
  }
  if (!restore || !read) {
    return;
  }
  Game again;
  CHECK(again.restore(*read));
  ++seen.restored;
  CHECK(again.snapshot() == snapshot);
  CHECK(sameOnTable(game, again));
  // It plays on, scoring nothing back: the score never goes down.
  const std::uint64_t score = again.rules().score();
  InputState input;
  for (int i = 0; i < 240; ++i) {
    input.leftFlipper = (i / 23) % 2 == 0;
    input.rightFlipper = (i / 31) % 2 == 0;
    again.step(input);
    if (again.gameOver()) {
      break;
    }
  }
  CHECK(again.rules().score() >= score);
}

void testInProgress() {
  Game game;
  // A new table is not a game in progress until a ball is played.
  CHECK(!game.inProgress());
  CHECK(game.gamesBegun() == 0);
  InputState plunge;
  plunge.plunger = true;
  for (int i = 0; i < 90; ++i) {
    game.step(plunge);
  }
  const InputState idle;
  for (int i = 0; i < 240 && !game.inProgress(); ++i) {
    game.step(idle);
  }
  CHECK(game.inProgress());
  CHECK(game.gamesBegun() == 1);
  // A new game asked for: begun, and not in progress until played.
  InputState fresh;
  fresh.newGame = true;
  game.step(fresh);
  CHECK(game.gamesBegun() == 2);
  CHECK(!game.inProgress());
  game.step(idle);
  // Played to the end: over, and not in progress.
  for (int i = 0; i < 120 * 600 && !game.gameOver(); ++i) {
    InputState input;
    input.plunger = (i % 300) < 90;
    game.step(input);
  }
  CHECK(game.gameOver());
  CHECK(!game.inProgress());
  CHECK(game.gamesBegun() == 2);
}

// Every state play reaches can be saved and read back, and a good share of
// them restored, from games played out by the reflex bot and from the
// staged scenes played on.
void testEveryStateSaves() {
  Coverage seen;
  for (std::uint64_t seed = 1; seed <= 3; ++seed) {
    Player player(seed);
    for (int i = 0; i < 120 * 300; ++i) {
      player.step();
      const Game& game = player.harness.game();
      if (game.inProgress() && i % 53 == 0) {
        roundTrip(game, seen, i % (53 * 9) == 0);
      }
    }
  }
  for (const pinball::Scene scene :
       {pinball::Scene::Multiball, pinball::Scene::Hangar, pinball::Scene::Course,
        pinball::Scene::Graduation}) {
    Player player(static_cast<std::uint64_t>(scene) + 11U);
    CHECK(pinball::stageScene(player.harness.game(), scene));
    for (int i = 0; i < 120 * 40; ++i) {
      player.step();
      const Game& game = player.harness.game();
      if (game.inProgress() && i % 17 == 0) {
        roundTrip(game, seen, i % (17 * 5) == 0);
      }
    }
  }
  // States the bot meets too seldom to count on: a tilted table, and a
  // ball asked for and not yet served.
  {
    Player player(31);
    for (int i = 0; i < 120 * 20 && !player.harness.game().rules().ballInPlay(); ++i) {
      player.step();
    }
    for (int i = 0; i < 120; ++i) {
      player.step();
    }
    Game& game = player.harness.game();
    InputState shove;
    for (int i = 0; i < 8 && !game.rules().tilted(); ++i) {
      shove.nudgeLeft = i % 2 == 0;
      game.step(shove);
    }
    CHECK(game.rules().tilted());
    roundTrip(game, seen, true);
    Player other(32);
    for (int i = 0; i < 120 * 20 && !other.harness.game().inProgress(); ++i) {
      other.step();
    }
    CHECK(other.harness.game().requestBall());
    CHECK(other.harness.game().requestBall());
    roundTrip(other.harness.game(), seen, true);
  }
  std::fprintf(stderr,
               "saved games: %d saved, %d restored; multiball %d, locked %d, held %d, "
               "course %d, tilted %d, on the plunger %d, queued %d\n",
               seen.saves, seen.restored, seen.multiball, seen.locked, seen.held, seen.course,
               seen.tilted, seen.waiting, seen.queued);
  CHECK(seen.saves > 500);
  CHECK(seen.restored > 50);
  CHECK(seen.multiball > 0);
  CHECK(seen.locked > 0);
  CHECK(seen.held > 0);
  CHECK(seen.course > 0);
  CHECK(seen.waiting > 0);
  CHECK(seen.tilted > 0);
  CHECK(seen.queued > 0);
}

// The same save put on two new tables gives two games that never part: a
// restore is as deterministic as the game itself.
void testRestoreIsDeterministic() {
  Player player(8);
  for (int i = 0; i < 120 * 120; ++i) {
    player.step();
  }
  while (!player.harness.game().inProgress()) {
    player.step();
  }
  const save::Snapshot snapshot = player.harness.game().snapshot();
  Game a;
  Game b;
  CHECK(a.restore(snapshot));
  CHECK(b.restore(snapshot));
  bool same = true;
  for (int i = 0; i < 120 * 60 && same; ++i) {
    const InputState input = scripted(i);
    a.step(input);
    b.step(input);
    same = a.snapshot() == b.snapshot();
  }
  CHECK(same);
}


// The records with what Box2D alone decides left out: where the balls are,
// how they move, and the flippers and plunger (which the balls can push).
save::Snapshot withoutBodies(save::Snapshot s) {
  for (save::BallRecord& ball : s.balls) {
    ball.x = 0.0;
    ball.y = 0.0;
    ball.vx = 0.0F;
    ball.vy = 0.0F;
    ball.rotationCos = 1.0F;
    ball.rotationSin = 0.0F;
    ball.spin = 0.0F;
    ball.sensorFrom = {};
    ball.searchAnchor = {};
    ball.takenAt = {};
  }
  return s;
}

// A game saved while every ball is out of the physics (held in holes, on
// rails or in bays) is put back with no contact Box2D could have kept. It
// then goes on as the game it was taken from, step for step and bit for
// bit, every record equal, until the two physics worlds part by a rounding
// (Box2D's broad phase keeps a history of its own that a new world cannot
// share, so pairs of contacts can be met in another order). Until then not
// one field differs, which shows that nothing the game depends on is left
// out of the save; and the worlds must keep together for two seconds at
// least.
void testContinuesAsTheOriginal() {
  int compared = 0;
  int shortest = 1 << 30;
  for (std::uint64_t seed = 1; seed <= 8 && compared < 4; ++seed) {
    Player player(seed + 40U);
    for (int i = 0; i < 120 * 400 && compared < 4; ++i) {
      player.step();
      Game& game = player.harness.game();
      if (!game.inProgress()) {
        continue;
      }
      bool allHeld = true;
      for (const pinball::BallView& ball : game.balls()) {
        allHeld = allHeld && (!ball.visible || ball.raised || ball.locked);
      }
      if (!allHeld || game.queuedBalls() != 0 || game.plungerPull() != 0.0F) {
        continue;
      }
      Game again;
      CHECK(again.restore(game.snapshot()));
      CHECK(sameOnTable(game, again));
      int together = 0;
      for (; together < 120 * 30; ++together) {
        const InputState input = scripted(i + together);
        game.step(input);
        again.step(input);
        const save::Snapshot a = game.snapshot();
        const save::Snapshot b = again.snapshot();
        if (a == b) {
          continue;
        }
        // Only the bodies may have parted; anything else is a field the
        // save does not carry.
        const bool bodiesOnly = withoutBodies(a) == withoutBodies(b);
        if (!bodiesOnly) {
          std::fprintf(stderr, "restored game differs from its original in more than the "
                               "physics after %d steps\n", together);
        }
        CHECK(bodiesOnly);
        break;
      }
      shortest = std::min(shortest, together);
      ++compared;
      player.pull = 0;
      player.waited = 0;
    }
  }
  std::fprintf(stderr, "saved games: restored games kept with their originals for %d steps "
                       "at least\n", shortest);
  CHECK(compared >= 4);
  CHECK(shortest >= 240);
}

// A save taken while balls fly is put back where they were, moving as they
// were: the records match and the table shows the same picture.
void testInFlight() {
  Player player(77);
  int checked = 0;
  for (int i = 0; i < 120 * 200 && checked < 20; ++i) {
    player.step();
    const Game& game = player.harness.game();
    if (!game.inProgress() || i % 101 != 0) {
      continue;
    }
    bool moving = false;
    for (std::size_t slot = 0; slot < Game::kMaxBalls; ++slot) {
      const pinball::Vec2 v = pinball::GameTestAccess::velocity(game, slot);
      moving = moving || std::fabs(v.x) + std::fabs(v.y) > 100.0F;
    }
    if (!moving) {
      continue;
    }
    const save::Snapshot snapshot = game.snapshot();
    Game again;
    CHECK(again.restore(snapshot));
    CHECK(again.snapshot() == snapshot);
    for (std::size_t slot = 0; slot < Game::kMaxBalls; ++slot) {
      const pinball::Vec2 v0 = pinball::GameTestAccess::velocity(game, slot);
      const pinball::Vec2 v1 = pinball::GameTestAccess::velocity(again, slot);
      CHECK(v0.x == v1.x && v0.y == v1.y);
    }
    ++checked;
  }
  CHECK(checked >= 5);
}

// A good file for the corruption tests: a game in the middle of things.
std::string goodFile(save::Snapshot* out = nullptr) {
  Player player(5);
  for (int i = 0; i < 120 * 90; ++i) {
    player.step();
  }
  while (!player.harness.game().inProgress()) {
    player.step();
  }
  const save::Snapshot snapshot = player.harness.game().snapshot();
  if (out != nullptr) {
    *out = snapshot;
  }
  return save::encode(snapshot);
}

void testRejectsDamage() {
  save::Snapshot snapshot;
  const std::string file = goodFile(&snapshot);
  CHECK(file.size() == save::fileSize());
  CHECK(save::decode(bytesOf(file)).has_value());

  // Cut short anywhere, or nothing at all.
  int accepted = 0;
  for (std::size_t length = 0; length < file.size(); ++length) {
    accepted += save::decode(bytesOf(file.substr(0, length))).has_value() ? 1 : 0;
  }
  CHECK(accepted == 0);
  // Longer than it should be, by a byte or by a lot.
  CHECK(!save::decode(bytesOf(file + '\0')).has_value());
  CHECK(!save::decode(bytesOf(file + std::string(save::kMaxFileBytes, 'x'))).has_value());
  CHECK(!save::decode(bytesOf(std::string(save::kMaxFileBytes * 4, '\0'))).has_value());
  // Any byte changed: the checksum or the header gives it away.
  accepted = 0;
  for (std::size_t at = 0; at < file.size(); ++at) {
    for (const unsigned flip : {0x01U, 0x80U, 0xFFU}) {
      std::string bad = file;
      bad[at] = static_cast<char>(static_cast<unsigned char>(bad[at]) ^ flip);
      accepted += save::decode(bytesOf(bad)).has_value() ? 1 : 0;
    }
  }
  CHECK(accepted == 0);
  // Another build, version, layout or element count, even correctly sealed.
  CHECK(!save::decode(bytesOf(file), "9.9.9+9").has_value());
  CHECK(!save::decode(bytesOf(save::encode(snapshot, "2.0.0+0"))).has_value());
  CHECK(save::decode(bytesOf(save::encode(snapshot, "2.0.0+0")), "2.0.0+0").has_value());
  CHECK(save::encode(snapshot, std::string(save::kMaxBuildIdLength + 1, '1')).empty());
  const std::size_t idLength = static_cast<unsigned char>(file[10]);
  const std::size_t layoutAt = 11 + idLength;
  for (const std::size_t at : {std::size_t{0}, std::size_t{8}, std::size_t{9}, layoutAt,
                               layoutAt + 31, layoutAt + 32, layoutAt + 45, layoutAt + 46}) {
    std::string bad = file;
    bad[at] = static_cast<char>(static_cast<unsigned char>(bad[at]) ^ 0x04U);
    reseal(bad);
    CHECK(!save::decode(bytesOf(bad)).has_value());
  }
  // Random bytes of the right size.
  Rng rng(99);
  for (int i = 0; i < 200; ++i) {
    std::string junk(file.size(), '\0');
    for (char& c : junk) {
      c = static_cast<char>(rng.next() & 0xFFU);
    }
    CHECK(!save::decode(bytesOf(junk)).has_value());
    // Same with the good header in front, sealed: the payload's checks.
    std::string sealed = file.substr(0, file.size() - 32 - save::payloadSize()) +
                         junk.substr(0, save::payloadSize()) + std::string(32, '\0');
    reseal(sealed);
    CHECK(!save::decode(bytesOf(sealed)).has_value());
  }
}

// Fields out of range, or at odds with each other, in a file that is
// otherwise perfect: refused by decode and by Game::restore alike.
void testRejectsBadFields() {
  save::Snapshot good;
  (void)goodFile(&good);
  CHECK(save::valid(good));
  Game table;
  const std::uint64_t steps = table.stepCount();

  const auto refused = [&](const char* what, auto change) {
    save::Snapshot bad = good;
    change(bad);
    const bool rejected = !save::valid(bad) && !save::decode(bytesOf(save::encode(bad))) &&
                          !save::decodePayload(bytesOf(save::encodePayload(bad))) &&
                          !table.restore(bad);
    if (!rejected) {
      std::fprintf(stderr, "accepted a save with %s\n", what);
    }
    CHECK(rejected);
  };
  using S = save::Snapshot;
  refused("ball 4", [](S& s) { s.rules.ball = 4; });
  refused("ball 0", [](S& s) { s.rules.ball = 0; });
  refused("a score past the limit", [](S& s) { s.rules.score = pinball::points::kMaxScore + 1; });
  refused("an unknown course", [](S& s) { s.rules.course = 7; });
  refused("an unknown shot for the combo", [](S& s) { s.rules.comboShot = 9; });
  refused("an unknown navigation shot", [](S& s) { s.rules.navShot = 7; });
  refused("a rank past the top", [](S& s) { s.rules.rank = 7; });
  refused("overdrive 4", [](S& s) { s.rules.overdrive = 4; });
  refused("bonus x 0", [](S& s) { s.rules.bonusMultiplier = 0; });
  refused("a jackpot past the ladder", [](S& s) { s.rules.jackpotIndex = 9; });
  refused("graduation stage 4", [](S& s) { s.rules.gradStage = 4; });
  refused("a skill lane past three", [](S& s) { s.rules.skillLane = 3; });
  refused("a tilt past its limit", [](S& s) { s.rules.tiltHeat = 3.0; });
  refused("tilt heat NaN", [](S& s) { s.rules.tiltHeat = std::nan(""); });
  refused("a meter over its goal", [](S& s) { s.rules.turboMeter = s.rules.turboGoal + 1; });
  refused("a stuck random number", [](S& s) { s.rules.random = 0; });
  refused("four balls in play", [](S& s) {
    s.rules.statusLive = 3;
    s.rules.statusLocked = 1;
  });
  refused("a timer of a day", [](S& s) { s.rules.courseSteps = 120U * 86400U; });
  // A ball's own counters past what play reaches: a tilted table's ball set
  // to wait 414 days for its launch would leave the table stuck.
  refused("a launch wait of 414 days", [](S& s) {
    s.rules.tilted = true;
    s.balls[0].autoLaunch = true;
    s.balls[0].launched = false;
    s.balls[0].inPlay = false;
    s.balls[0].launchSteps = 0xFFFFFFFFU;
  });
  refused("a launch wait past the relaunch", [](S& s) {
    s.balls[0].launchSteps = save::kMaxLaunchSteps + 1;
  });
  refused("a ball still past the ball search", [](S& s) {
    s.balls[0].stillSteps = save::kMaxStillSteps + 1;
  });
  refused("a hold of a day", [](S& s) { s.balls[0].heldSteps = 120U * 86400U; });
  refused("a door command past Raise", [](S& s) { s.rules.doorCommand = 3; });
  refused("a game random of 0", [](S& s) { s.game.random = 0; });
  refused("too many spins owed", [](S& s) { s.game.spinsOwed = table::kMaxSpinsOwed + 1; });
  refused("a spinner direction of 2", [](S& s) { s.game.spinDirection = 2; });
  refused("a future release", [](S& s) { s.game.lastReleaseStep[0] = s.game.stepCount + 5; });
  refused("four balls queued", [](S& s) { s.game.queuedBalls = 4; });
  refused("a flipper past its stop", [](S& s) { s.table.flipperAngles[0] = 3.0F; });
  refused("a plunger drawn too far", [](S& s) { s.table.plungerPull = 100.0F; });
  refused("no ball at all", [](S& s) {
    for (save::BallRecord& ball : s.balls) {
      ball = {};
    }
  });
  refused("balls and queue past three", [](S& s) {
    s.game.queuedBalls = static_cast<std::uint8_t>(table::kMaxBalls);
  });
  refused("a ball off the table", [](S& s) { s.balls[0].x = 50.0; });
  refused("a ball at NaN", [](S& s) { s.balls[0].y = std::nan(""); });
  refused("a ball at infinity", [](S& s) { s.balls[0].vx = INFINITY; });
  refused("a ball faster than the cap", [](S& s) { s.balls[0].vy = 100.0F; });
  refused("a rotation that is not one", [](S& s) {
    s.balls[0].rotationCos = 0.5F;
    s.balls[0].rotationSin = 0.5F;
  });
  refused("a ball held by nothing", [](S& s) {
    s.balls[0].held = true;
    s.balls[0].heldBy = {};
  });
  refused("a ball held by a bumper", [](S& s) {
    s.balls[0].held = true;
    s.balls[0].heldBy = {table::ElementKind::Bumper, 0};
    s.balls[0].holder = s.balls[0].heldBy;
  });
  refused("a ball held by hole 9", [](S& s) {
    s.balls[0].held = true;
    s.balls[0].heldBy = {table::ElementKind::Hole, 9};
    s.balls[0].holder = s.balls[0].heldBy;
  });
  refused("a ball in bay 3", [](S& s) { s.balls[0].lockBay = 3; });
  refused("a ball in a bay outside the Hangar", [](S& s) {
    s.balls[0].lockBay = 1;
    s.balls[0].held = false;
    s.balls[0].heldBy = {};
  });
  refused("two balls in one bay", [](S& s) {
    const table::ElementRef hangar{table::ElementKind::Hole,
                                   static_cast<std::uint8_t>(table::kHangarHole)};
    for (std::size_t slot = 0; slot < 2; ++slot) {
      save::BallRecord& ball = s.balls[slot];
      ball = s.balls[0];
      ball.active = true;
      ball.held = true;
      ball.heldBy = hangar;
      ball.holder = hangar;
      ball.lockBay = 1;
    }
  });
  refused("an empty slot with something in it", [](S& s) {
    for (save::BallRecord& ball : s.balls) {
      if (!ball.active) {
        ball.stillSteps = 1;
        return;
      }
    }
    s.balls[2] = {};
    s.balls[2].inPlay = true;
  });
  // None of that touched the table it was offered to.
  CHECK(table.stepCount() == steps);
  CHECK(!table.inProgress());

  // Raw payload bytes past every range a field has: the reader stops.
  const std::string payload = save::encodePayload(good);
  CHECK(payload.size() == save::payloadSize());
  int accepted = 0;
  for (std::size_t at = 0; at < payload.size(); ++at) {
    std::string bad = payload;
    bad[at] = static_cast<char>(0xFF);
    if (const auto read = save::decodePayload(bytesOf(bad))) {
      // A change the ranges allow: whatever is accepted restores, and
      // plays on without harm (the sanitizers watch).
      Game other;
      CHECK(other.restore(*read));
      for (int i = 0; i < 60; ++i) {
        other.step(scripted(i));
      }
      ++accepted;
    }
  }
  std::fprintf(stderr, "saved games: %d of %zu single-byte payload changes still valid\n",
               accepted, payload.size());
}

// The save is written on the main thread as the application goes to the
// background: it must take next to nothing.
void testFast() {
  save::Snapshot snapshot;
  (void)goodFile(&snapshot);
  Game game;
  const auto start = std::chrono::steady_clock::now();
  constexpr int kRounds = 200;
  std::size_t bytes = 0;
  for (int i = 0; i < kRounds; ++i) {
    bytes += save::encode(snapshot).size();
  }
  const auto middle = std::chrono::steady_clock::now();
  for (int i = 0; i < kRounds; ++i) {
    const std::string file = save::encode(snapshot);
    const auto read = save::decode(bytesOf(file));
    if (read) {
      game.restore(*read);
    }
  }
  const auto end = std::chrono::steady_clock::now();
  const double encodeMs =
      std::chrono::duration<double, std::milli>(middle - start).count() / kRounds;
  const double restoreMs =
      std::chrono::duration<double, std::milli>(end - middle).count() / kRounds;
  std::fprintf(stderr, "saved games: %zu bytes; snapshot+encode %.3f ms, decode+restore %.3f ms\n",
               bytes / kRounds, encodeMs, restoreMs);
  // Generous for sanitizers and a slow machine; a real save is far below.
  CHECK(encodeMs < 20.0);
}

}  // namespace

void testSaveGame() {
  testInProgress();
  testRejectsDamage();
  testRejectsBadFields();
  testInFlight();
  testContinuesAsTheOriginal();
  testEveryStateSaves();
  testRestoreIsDeterministic();
  testFast();
}
