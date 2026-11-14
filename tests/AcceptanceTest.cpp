#include <cstddef>
#include <cstdint>

#include "Check.hpp"
#include "Suites.hpp"
#include "bots/Acceptance.hpp"
#include "bots/Bots.hpp"
#include "bots/TableHarness.hpp"
#include "core/TableLayout.hpp"

// The quick part of the acceptance tests of TABLE.md section 5, run with
// every build: the bots themselves, eject safety, stuck-ball freedom over a
// bounded number of steps with one, two and three balls, and determinism.
// The launch is in tests/LaunchTest.cpp. The statistical criteria (ball
// times, drain split, shot windows, the ten million step stress run) need
// far more play than a unit test should take: they are the separate
// program pinball_acceptance (tests/AcceptanceMain.cpp, docs/BUILDING.md).
namespace {

using namespace pinball::bots;
using namespace pinball::bots::acceptance;
using pinball::Vec2;
namespace table = pinball::table;

Sense oneBall(Vec2 position, Vec2 velocity) {
  Sense sense;
  sense.balls[0] = {position, velocity};
  sense.count = 1;
  return sense;
}

// A point on the resting left bat's upper side: foot along it from the
// pivot, and a ball's radius and a little above its line.
Vec2 onLeftBat(float foot) {
  const Vec2 pivot = table::kLeftFlipper.pivot;
  return {pivot.x + foot * 0.8660254F + 9.0F, pivot.y + foot * 0.5F - 16.0F};
}

}  // namespace

void testAcceptance() {
  {  // The generator: the same seed gives the same numbers, another seed
     // others, and the ranges are what they say.
    Rng a(7);
    Rng b(7);
    Rng c(8);
    bool same = true;
    bool differs = false;
    bool inRange = true;
    for (int i = 0; i < 1000; ++i) {
      const int x = a.range(-3, 3);
      same = same && x == b.range(-3, 3);
      differs = differs || x != c.range(-3, 3);
      inRange = inRange && x >= -3 && x <= 3;
      const float u = a.unit();
      inRange = inRange && u >= 0.0F && u < 1.0F;
      b.unit();
      c.unit();
    }
    CHECK(same && differs && inRange);
  }

  {  // P0 never presses; P1 presses each flipper about twice a second, for
     // 10 to 36 steps at a time, the same way for the same seed.
    IdleBot idle;
    RandomBot random(3);
    RandomBot again(3);
    const Sense nothing;
    int presses = 0;
    int longest = 0;
    int shortest = 1000;
    int run = 0;
    bool same = true;
    constexpr int kSteps = 120 * 600;
    for (int i = 0; i < kSteps; ++i) {
      const Flippers none = idle.act(nothing);
      CHECK(!none.left && !none.right);
      const Flippers f = random.act(nothing);
      const Flippers g = again.act(nothing);
      same = same && f.left == g.left && f.right == g.right;
      if (f.left) {
        ++run;
      } else if (run > 0) {
        ++presses;
        longest = std::max(longest, run);
        shortest = std::min(shortest, run);
        run = 0;
      }
    }
    CHECK(same);
    CHECK(shortest >= 10 && longest <= 36);
    const double perSecond = presses / 600.0;
    CHECK(perSecond > 1.6 && perSecond < 2.4);
  }

  {  // P2 with no timing error: nothing in its zone, no press. A ball
     // rolling out along the bat is flipped on the step its foot reaches
     // mid-bat, on that side only, for 10 to 18 steps, and then the flipper
     // rests for 10.
    ReflexBot bot(1, 0);
    CHECK(!bot.act(Sense{}).left && !bot.act(Sense{}).right);
    // Far up the field, and rising fast through the zone: not seen.
    CHECK(!bot.act(oneBall({300.0F, 400.0F}, {0.0F, 100.0F})).left);
    CHECK(!bot.act(oneBall(onLeftBat(60.0F), {0.0F, -900.0F})).left);
    int pressedAtFoot = -1;
    int held = 0;
    int rested = 0;
    for (int foot = 20; foot <= 110 && rested < ReflexBot::kRestSteps; ++foot) {
      const float speed = 120.0F;  // One unit of foot a step.
      const Flippers f =
          bot.act(oneBall(onLeftBat(static_cast<float>(foot)), {speed * 0.866F, speed * 0.5F}));
      CHECK(!f.right);
      if (f.left && pressedAtFoot < 0) {
        pressedAtFoot = foot;
      }
      held += f.left ? 1 : 0;
      rested += pressedAtFoot >= 0 && !f.left ? 1 : 0;
    }
    // The foot is measured to the ball's centre, which onLeftBat puts a
    // little further along than the foot asked for.
    CHECK(pressedAtFoot >= static_cast<int>(ReflexBot::kAim) - 2 &&
          pressedAtFoot <= static_cast<int>(ReflexBot::kAim) + 1);
    CHECK(held >= 10 && held <= 18);
    CHECK(rested == ReflexBot::kRestSteps);

    // A ball that sits in the zone without rolling is flipped anyway.
    ReflexBot patient(1, 0);
    int waited = 0;
    while (waited < 200 && !patient.act(oneBall(onLeftBat(30.0F), {})).left) {
      ++waited;
    }
    CHECK(waited == ReflexBot::kPatienceSteps);

    // With a timing error the press comes early or late by up to that many
    // steps, and over many approaches both ways.
    int earliest = 1000;
    int latest = -1000;
    ReflexBot sloppy(5, 6);
    for (int approach = 0; approach < 200; ++approach) {
      int pressed = 1000;
      for (int foot = 20; foot <= 100; ++foot) {
        const Flippers f =
            sloppy.act(oneBall(onLeftBat(static_cast<float>(foot)), {120.0F * 0.866F, 60.0F}));
        if (f.left && pressed == 1000) {
          pressed = foot;
        }
      }
      // Clear its state between approaches.
      for (int i = 0; i < 40; ++i) {
        sloppy.act(Sense{});
      }
      earliest = std::min(earliest, pressed);
      latest = std::max(latest, pressed);
    }
    const int aim = static_cast<int>(ReflexBot::kAim);
    CHECK(earliest >= aim - 9 && earliest <= aim - 4);
    CHECK(latest <= aim + 8 && latest >= aim + 4);
  }

  {  // P3 presses its one flipper at its step, for its time.
    ScriptedBot bot(table::kRightFlipperIndex, 5, 3);
    int rightSteps = 0;
    for (int i = 0; i < 20; ++i) {
      const Flippers f = bot.act(Sense{});
      CHECK(!f.left);
      CHECK(f.right == (i >= 5 && i < 8));
      rightSteps += f.right ? 1 : 0;
    }
    CHECK(rightSteps == 3);
    ScriptedBot never(table::kLeftFlipperIndex, -1);
    CHECK(!never.act(Sense{}).left && !never.act(Sense{}).right);
  }

  {  // The bench itself: it sees a launched ball's first touch, counts the
     // drain and which way out, and is handed the next ball by the game.
    TableHarness harness;
    CHECK(harness.sense().count == 1);
    CHECK(!harness.game().ballInPlay(0));
    harness.put(0, table::kLeftInlane + Vec2{0.0F, -30.0F}, {}, true);
    const FirstTouch touch = firstTouch(harness, 120 * 5);
    CHECK(touch.touch == Touch::InlaneLeft);
    CHECK(harness.rolloverHits[4] == 1);
    // With idle flippers it rolls off the bat and out through the middle.
    Touch way = Touch::None;
    for (int i = 0; i < 120 * 10 && way == Touch::None; ++i) {
      way = harness.advance();
    }
    CHECK(way == Touch::DrainCentre);
    CHECK(harness.drains[0] == 1 && harness.drains[1] == 0 && harness.drains[2] == 0);
    CHECK(harness.flipperTouches > 0 && harness.lostBalls == 0);
    CHECK(pinball::bots::touchName(Touch::Briefing)[0] == 'B');
    // An outlane drain is told from a centre one.
    harness.put(0, table::mirrored(table::kLeftOutlane) + Vec2{0.0F, -20.0F}, {}, true);
    way = Touch::None;
    for (int i = 0; i < 120 * 10 && way == Touch::None; ++i) {
      way = harness.advance();
    }
    CHECK(way == Touch::OutlaneRight && harness.drains[2] == 1);
  }

  {  // The bench opens the Hangar for the shot sweep, and it stays open:
     // the rules' raise at a new game must not shut it again.
    TableHarness harness;
    harness.openHangar();
    for (int i = 0; i < 120 * 3; ++i) {
      harness.game().step(pinball::InputState{});
    }
    CHECK(!harness.game().doorBankUp());
  }

  {  // B2a under the rules: every fresh game's first ball is played into
     // play and judged, and the same seed gives the same games.
    EarlyDrainOptions options;
    options.games = 12;
    const EarlyDrainResult first = earlyDrains(options);
    const EarlyDrainResult again = earlyDrains(options);
    CHECK(first.games == 12 && first.inPlay == 12);
    CHECK(first.lost == first.way[0] + first.way[1] + first.way[2]);
    CHECK(first.fingerprint == again.fingerprint && first.lost == again.lost);
    CHECK(first.percent() >= 0.0 && first.percent() <= 100.0);
  }

  {  // B5, eject safety: a ball let go at each exit (the Briefing room, the
     // Hangar, both rails), a little off the exact release each time, meets
     // a flipper or an inlane first. Never a slingshot, an outlane or the
     // drain.
    for (std::size_t hole = 0; hole < table::kHoles.size(); ++hole) {
      const ExitResult exit = exitOf(hole, 10);
      CHECK(exit.releases == 10);
      CHECK(exit.safe == 10);
    }
    // The rails cross the ball over: the Runway to the left inlane, the
    // Skyway to the right one.
    CHECK(exitOf(table::kRunwayHole, 3).first[static_cast<std::size_t>(Touch::InlaneLeft)] == 3);
    CHECK(exitOf(table::kSkywayHole, 3).first[static_cast<std::size_t>(Touch::InlaneRight)] == 3);
  }

  {  // The table's own elements, a few of each (the full counts are in the
     // acceptance program). The Hangar's kick to the right reaches a
     // flipper. Two balls locked and a third sent in: all three are let go
     // in order, 0.4 s apart, and each meets a flipper or an inlane first.
    CHECK(exitAt(table::kHoles[table::kHangarHole].exitPosition, table::kHangarEjectRight, 10)
              .safe == 10);
    // The Briefing room's kick, at the corners of its range and inside it.
    const ExitResult kicks = scoopKicks(12);
    CHECK(kicks.safe == 12);
    const LockBurstResult lock = lockBursts(6);
    CHECK(lock.bursts == 6 && lock.released == 18);
    CHECK(lock.outOfOrder == 0 && lock.badSpacing == 0 && lock.heldMoved == 0);
    CHECK(lock.safe == 18);
    // A second ball into the Briefing room while it holds one: both come
    // out, a full hold apart, to a flipper or an inlane.
    const ScoopPairResult pairs = scoopPairs(8);
    CHECK(pairs.pairs >= 6 && pairs.bothOut == pairs.pairs && pairs.tooClose == 0);
    CHECK(pairs.safe == pairs.returned && pairs.longestOverlap <= 4);
    // The kickback takes every ball that comes down the lit left outlane
    // and sets it down where it rolls over the inlane's switch.
    const KickbackResult kick = kickbackSweep(60);
    CHECK(kick.balls >= 30);
    CHECK(kick.fired == kick.balls && kick.returned == kick.balls && kick.safe == kick.balls);
    CHECK(kick.lostByOutlane == 0);
  }

  {  // The wedge: two balls arriving together at the mouth of an inlane
     // do not arch across it and stay. (With any grip between two balls
     // they did: PhysicsWorld::kBallBallFriction.)
    const WedgeResult wedge = inlaneWedge(41);
    CHECK(wedge.pairs >= 60);
    CHECK(wedge.wedged == 0);
  }

  {  // D1, D4, D5 over a bounded run with one, two and three balls kept in
     // the game: no ball stuck, none needing the ball search, none back in
     // the launch lane, lost outside the table or over the speed cap, no two
     // pressed into each other, and the count of balls on the table and
     // waiting never wrong.
    struct Run {
      std::size_t balls;
      BotKind bot;
    };
    for (const Run run : {Run{1, BotKind::Reflex}, Run{2, BotKind::Random}, Run{3, BotKind::Random},
                          Run{3, BotKind::Reflex}}) {
      StressOptions options;
      options.balls = run.balls;
      options.bot = run.bot;
      options.steps = 6000;
      options.seed = 40 + run.balls;
      const StressResult result = stress(options);
      CHECK(result.steps == options.steps);
      CHECK(result.stuck == 0 && result.searches == 0);
      CHECK(result.laneIntrusions == 0 && result.lostBalls == 0 && result.overSpeed == 0);
      CHECK(result.countBroken == 0);
      CHECK(result.longestOverlap <= 4);
      // No ball is left waiting in the lane.
      CHECK(result.longestInLane <= 10U * 120U);
      // The Hangar is never entered past a standing door, and a locked
      // ball is never anywhere but in its bay.
      CHECK(result.hangarEntriesDoorsUp == 0 && result.lockedAstray == 0);
      CHECK(result.kickbackReturns <= result.kickbacks &&
            result.kickbacks - result.kickbackReturns <= 1);
      if (run.balls > 1) {
        CHECK(result.autoLaunches >= run.balls - 1);
        CHECK(result.stepsWithAllFree > 0);
      }
    }
  }

  {  // Determinism: the same bot with the same seed plays the same game,
     // drain for drain, with three balls and with one; another seed does not.
    StressOptions options;
    options.balls = 3;
    options.bot = BotKind::Reflex;
    options.steps = 12000;  // Long enough for balls to be locked and to drain.
    options.seed = 9;
    const StressResult a = stress(options);
    const StressResult b = stress(options);
    CHECK(a.fingerprint == b.fingerprint && a.drains == b.drains && a.drains > 0);
    CHECK(a.stepsWithAllFree == b.stepsWithAllFree && a.autoLaunches == b.autoLaunches);
    CHECK(a.locks == b.locks && a.lockReleases == b.lockReleases && a.spins == b.spins);
    CHECK(a.bankDrops == b.bankDrops && a.hangarEntries == b.hangarEntries && a.spins > 0);
    options.seed = 10;
    CHECK(stress(options).fingerprint != a.fingerprint);

    // A whole game under the table's own rules, played twice: the same
    // game, sound from the first plunge to game over.
    GamesOptions whole;
    whole.games = 1;
    whole.seed = 3;
    const GamesResult one = playGames(whole);
    const GamesResult again = playGames(whole);
    CHECK(one.games == 1 && one.broken == nullptr && one.brokenSteps == 0 && one.timeouts == 0);
    CHECK(one.fingerprint == again.fingerprint && one.steps == again.steps);
    CHECK(one.scores == again.scores && one.scores.at(0) > 0);
    CHECK(one.ballSeconds.size() >= 3 && one.gameSeconds.size() == 1);
    CHECK(one.hangarEntriesDoorsUp == 0 && one.lockedAstray == 0);
    std::uint64_t parts = 0;
    for (const std::uint64_t part : one.featureScore) {
      parts += part;
    }
    CHECK(parts == one.totalScore && one.totalScore == one.scores.at(0));

    SoakOptions soakOptions;
    soakOptions.bot = BotKind::Random;
    soakOptions.balls = 4;
    soakOptions.seed = 5;
    const SoakResult first = soak(soakOptions);
    const SoakResult second = soak(soakOptions);
    CHECK(first.ballSeconds.size() == 4 && first.fingerprint == second.fingerprint);
    CHECK(first.steps == second.steps && first.ballSeconds == second.ballSeconds);
    CHECK(first.drains[0] + first.drains[1] + first.drains[2] >= 4);
    CHECK(first.timeouts == 0 && first.lostBalls == 0);
    // The saver puts an early drain back and lets the clock run on.
    soakOptions.saverSeconds = 15.0;
    const SoakResult saved = soak(soakOptions);
    CHECK(saved.ballSeconds.size() == 4);
    for (const double seconds : saved.ballSeconds) {
      CHECK(seconds >= 15.0);
    }
    CHECK(saved.percentile(0.0) <= saved.percentile(0.5) &&
          saved.percentile(0.5) <= saved.percentile(1.0));
    CHECK(saved.drainShare(0) + saved.drainShare(1) + saved.drainShare(2) > 99.9);
  }
}
