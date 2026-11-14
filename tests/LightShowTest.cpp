#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "Check.hpp"
#include "GameTestAccess.hpp"
#include "Suites.hpp"
#include "core/Game.hpp"
#include "core/Input.hpp"
#include "core/Physics.hpp"
#include "core/LightShow.hpp"
#include "core/Rules.hpp"
#include "core/Scenes.hpp"
#include "core/TableLayout.hpp"

// The light show (core/LightShow.hpp): lamp states and blink phases, the
// flashes each event starts, the 3 Hz limiter, reduce motion, attract, and
// that watching the game never changes it.
namespace {

using pinball::Game;
using pinball::InputState;
using pinball::LampState;
using pinball::LightShow;
using pinball::Vec2;
using Seam = pinball::GameTestAccess;
namespace fx = pinball::fx;
namespace table = pinball::table;
using LampId = table::LampId;

InputState flipping(std::uint32_t step) noexcept {
  InputState input;
  input.plunger = (step % 1200U) >= 20U && (step % 1200U) < 140U;
  input.leftFlipper = (step / 29U) % 2U == 0U;
  input.rightFlipper = (step / 41U) % 2U == 0U;
  return input;
}

void step(Game& game, LightShow& show, const InputState& input = {}) {
  game.step(input);
  show.observe(game);
}

// A ball in play, the show watching from the start.
void inPlay(Game& game, LightShow& show) {
  for (std::uint32_t i = 0; i < 600 && !game.rules().ballInPlay(); ++i) {
    step(game, show, flipping(i));
  }
}

void testBlinkPhases() {
  // Square blinks with a three-step rise and a four-step fall, from the step
  // count alone: the same step always gives the same level.
  CHECK(fx::lampLevel(LampState::Off, 7, false) == 0.0F);
  CHECK(fx::lampLevel(LampState::On, 7, false) == 1.0F);
  CHECK(std::fabs(fx::lampLevel(LampState::Blink, 0, false) - 1.0F / 3.0F) < 1e-6F);
  CHECK(fx::lampLevel(LampState::Blink, 2, false) == 1.0F);
  CHECK(fx::lampLevel(LampState::Blink, 29, false) == 1.0F);
  CHECK(fx::lampLevel(LampState::Blink, 30, false) == 0.75F);
  CHECK(fx::lampLevel(LampState::Blink, 33, false) == 0.0F);
  CHECK(fx::lampLevel(LampState::Blink, 59, false) == 0.0F);
  CHECK(fx::lampLevel(LampState::FastBlink, 11, false) == 1.0F);
  CHECK(fx::lampLevel(LampState::FastBlink, 15, false) == 0.0F);
  bool periodic = true;
  for (std::uint64_t s = 0; s < 2000; ++s) {
    periodic = periodic &&
               fx::lampLevel(LampState::Blink, s, false) ==
                   fx::lampLevel(LampState::Blink, s + fx::kBlinkSteps, false) &&
               fx::lampLevel(LampState::FastBlink, s, false) ==
                   fx::lampLevel(LampState::FastBlink, s + fx::kFastBlinkSteps, false);
  }
  CHECK(periodic);
  // Blink rates under the photosensitive limits for small lamps: 2 and 5 a second.
  CHECK(fx::kBlinkSteps == 60 && fx::kFastBlinkSteps == 24);
}

void testReduceMotionLamps() {
  // No strobe: a blinking lamp breathes, never jumping more than a little a
  // step, and never going dark.
  float worst = 0.0F;
  float lowest = 1.0F;
  for (const LampState state : {LampState::Blink, LampState::FastBlink}) {
    for (std::uint64_t s = 1; s < 3000; ++s) {
      worst = std::max(worst, std::fabs(fx::lampLevel(state, s, true) -
                                        fx::lampLevel(state, s - 1, true)));
      lowest = std::min(lowest, fx::lampLevel(state, s, true));
    }
  }
  CHECK(worst < 0.05F);
  CHECK(lowest >= 0.44F);
}

void testLampsFollowRules() {
  Game game;
  LightShow show;
  inPlay(game, show);
  for (int i = 0; i < 400; ++i) {
    step(game, show, flipping(static_cast<std::uint32_t>(i)));
  }
  // Every lamp the rules light is lit: On full, blinking by the step's phase.
  bool agree = true;
  for (const table::Lamp& lamp : table::kLamps) {
    const LampState state = show.lampState(lamp.id);
    const float level = show.lampLevel(lamp.id);
    if (state == LampState::On) {
      agree = agree && level == 1.0F;
    }
    if (state == LampState::Off) {
      // Lit by nothing but a flash, a sweep or the quiet table's glimmer.
      agree = agree && level >= 0.0F && level <= 1.0F;
    }
    if (lamp.id != LampId::Lock1 && lamp.id != LampId::Lock2) {
      agree = agree && state == game.rules().lampState(lamp.id);
    }
  }
  CHECK(agree);
}

void testAmbientIsFarBelowLit() {
  // The quiet table is alive: no unlit insert is dark, and at every moment
  // many lamps are running their chases. And the chases stay well under a
  // lit lamp: lit against unlit is never in doubt.
  Game game;
  LightShow show;
  // On the plunger, nothing struck: only the chases light the unlit lamps.
  float most = 0.0F;
  for (int i = 0; i < 1200; ++i) {
    step(game, show);
    for (const table::Lamp& lamp : table::kLamps) {
      if (show.lampState(lamp.id) == LampState::Off && show.show() == fx::Show::None) {
        most = std::max(most, show.lampLevel(lamp.id));
      }
    }
  }
  inPlay(game, show);
  float least = 1.0F;
  int quietWindows = 0;
  std::array<float, LightShow::kLampCount> before{};
  for (int i = 0; i < 2400; ++i) {
    step(game, show, flipping(static_cast<std::uint32_t>(i)));
    for (const table::Lamp& lamp : table::kLamps) {
      if (show.lampState(lamp.id) == LampState::Off && show.show() == fx::Show::None) {
        least = std::min(least, show.lampLevel(lamp.id));
      }
    }
    if (i % 30 == 0) {
      int moved = 0;
      for (std::size_t k = 0; k < before.size(); ++k) {
        moved += std::fabs(show.lampLevels()[k] - before[k]) > 0.15F ? 1 : 0;
      }
      if (i > 0 && moved < 6) {
        ++quietWindows;
      }
      before = show.lampLevels();
    }
  }
  CHECK(least >= fx::kUnlitFloor - 1e-6F);
  // A chase's head is plain to see, but never as bright as a lamp the rules
  // light (1, with its core and star).
  CHECK(most > fx::kChaseHead - 0.01F);
  CHECK(most <= fx::kChaseHead + 1e-6F);
  CHECK(fx::kChaseHead + 0.15F <= 1.0F);
  CHECK(quietWindows == 0);  // Six or more lamps change in every quarter second.
  // Lit by a chase is not lit by the rules: no star.
  bool chaseNotLit = true;
  for (const table::Lamp& lamp : table::kLamps) {
    if (show.lampState(lamp.id) == LampState::Off) {
      chaseNotLit = chaseNotLit && !show.lampLit(lamp.id);
    }
  }
  CHECK(chaseNotLit);
}

void testSameGameSameLights() {
  // Two shows watching two copies of the same game agree at every step.
  Game a;
  Game b;
  LightShow sa;
  LightShow sb;
  bool same = true;
  for (std::uint32_t i = 0; i < 4000; ++i) {
    step(a, sa, flipping(i));
    step(b, sb, flipping(i));
    same = same && sa.lampLevels() == sb.lampLevels() && sa.giLevel() == sb.giLevel() &&
           sa.spinnerLevel() == sb.spinnerLevel() && sa.burstsStarted() == sb.burstsStarted();
  }
  CHECK(same);
}

void testWatchingChangesNothing() {
  // A game watched by the show, with everything asked of it every step,
  // plays exactly as one that is not.
  Game watched;
  Game alone;
  LightShow show;
  bool same = true;
  for (std::uint32_t i = 0; i < 20000; ++i) {
    watched.step(flipping(i));
    show.observe(watched);
    (void)show.bursts();
    (void)show.trail(0);
    (void)show.chase(0);
    alone.step(flipping(i));
    const Vec2 p = watched.ball().position;
    const Vec2 q = alone.ball().position;
    same = same && p.x == q.x && p.y == q.y && watched.rules().score() == alone.rules().score() &&
           watched.ballCount() == alone.ballCount();
  }
  CHECK(same);
  CHECK(watched.stepCount() == alone.stepCount());
}

void testHitFlashes() {
  Game game;
  LightShow show;
  inPlay(game, show);
  // Into the left turbo target.
  const std::size_t slot = game.ball().id;
  // (Sent again until the nest's gate is open: a hit there shortly before
  // lets the flash already lit run on instead.)
  bool target = false;
  for (int i = 0; i < 1200 && !target; ++i) {
    if (i % 120 == 0) {
      Seam::placeBall(game, slot, {235.0F, 226.0F}, {-800.0F, 0.0F});
    }
    step(game, show);
    target = show.targetFlash(0) == 1.0F;
  }
  CHECK(target);
  // It fades.
  for (int i = 0; i < 10; ++i) {
    step(game, show);
  }
  CHECK(show.targetFlash(0) < 1.0F);
  // Down a lane: the rollover and its lamp flash.
  Seam::placeBall(game, slot, {table::kRollovers[1].zone.center.x, 120.0F}, {0.0F, 300.0F});
  bool lane = false;
  for (int i = 0; i < 90 && !lane; ++i) {
    step(game, show);
    lane = show.rolloverFlash(1) > 0.9F && show.lampLevel(LampId::RollCall2) > 0.9F;
  }
  CHECK(lane);
}

void testMultiballShow() {
  Game game;
  LightShow show;
  CHECK(pinball::stageScene(game, pinball::Scene::Hangar));
  show.observe(game);
  const std::size_t slot = game.ball().id;
  Seam::placeBall(game, slot, {279.0F, 440.0F}, {0.0F, -900.0F});
  Seam::putInPlay(game, slot);
  bool started = false;
  float darkest = 1.0F;
  bool swept = false;
  bool bayFlashed = false;
  bool lampsOut = false;
  for (int i = 0; i < 900; ++i) {
    step(game, show);
    bayFlashed = bayFlashed || show.holeFlash(table::kHangarHole) > 0.0F;
    if (show.show() == fx::Show::MultiballStart) {
      if (!started) {
        // Blackout: every lamp out at the start.
        lampsOut = std::ranges::all_of(show.lampLevels(), [](float l) { return l == 0.0F; });
      }
      started = true;
      darkest = std::min(darkest, show.giLevel());
      swept = swept || show.sweepY() >= 0.0F;
    }
  }
  CHECK(started);
  CHECK(lampsOut);
  CHECK(bayFlashed);
  CHECK(darkest < 0.3F);
  CHECK(swept);
  CHECK(show.burstsStarted() >= 4);
  CHECK(show.largeFlashes() >= 2);
  CHECK(show.giLevel() > 0.95F);  // The lights come back.
}

void testJackpotShow() {
  Game game;
  LightShow show;
  CHECK(pinball::stageScene(game, pinball::Scene::Multiball));
  show.observe(game);
  pinball::Rules& rules = Seam::rules(game);
  bool made = false;
  for (const pinball::Shot shot : {pinball::Shot::Runway, pinball::Shot::Skyway}) {
    if (!made && rules.jackpotLit(shot)) {
      rules.onHole(shot == pinball::Shot::Runway ? table::kRunwayHole : table::kSkywayHole,
                   game.ball().id);
      made = true;
    }
  }
  CHECK(made);
  step(game, show);
  CHECK(show.show() == fx::Show::Jackpot || show.show() == fx::Show::SuperJackpot);
  step(game, show);
  // A jackpot is its ring alone, growing out from the ball (one flash for
  // each part of the table); a super jackpot is a wash and the domes.
  if (show.show() == fx::Show::Jackpot) {
    const float first = show.ringRadius();
    CHECK(first >= 0.0F);
    for (int i = 0; i < 20; ++i) {
      step(game, show);
    }
    CHECK(show.ringRadius() > first + 100.0F);
  } else {
    bool washed = false;
    for (int i = 0; i < 40; ++i) {
      step(game, show);
      const fx::BurstViews bursts = show.bursts();
      for (const fx::BurstView& b : bursts.span()) {
        washed = washed || (b.where == fx::Flasher::Table && b.level > 0.0F);
      }
    }
    CHECK(washed);
  }
}

void testDrainShow() {
  Game game;
  LightShow show;
  inPlay(game, show);
  Seam::rules(game).setBallSaveEnabled(false);
  const int ball = game.rules().ball();
  Seam::placeBall(game, game.ball().id, {table::kWidth * 0.5F - 8.0F, 860.0F}, {0.0F, 600.0F});
  bool drained = false;
  for (int i = 0; i < 400 && !drained; ++i) {
    step(game, show);
    drained = game.rules().ball() != ball;
  }
  CHECK(drained);
  CHECK(show.show() == fx::Show::Drain);
  float darkest = 1.0F;
  float bonusLit = 0.0F;
  for (int i = 0; i < 60; ++i) {
    step(game, show);
    darkest = std::min(darkest, show.giLevel());
    bonusLit = std::max(bonusLit, show.lampLevel(LampId::Course1));
  }
  CHECK(darkest < 0.6F);
  CHECK(bonusLit == 1.0F);  // The count-down lights the bonus lamps.
}

void testTiltShow() {
  Game game;
  LightShow show;
  inPlay(game, show);
  for (int i = 0; i < 400 && !game.rules().tilted(); ++i) {
    InputState input;
    input.nudgeLeft = i % 12 == 0;
    step(game, show, input);
  }
  CHECK(game.rules().tilted());
  for (int i = 0; i < 90; ++i) {
    step(game, show);
  }
  if (game.rules().tilted()) {
    CHECK(show.show() == fx::Show::Tilt);
    CHECK(show.giLevel() < 0.35F);
    CHECK(std::ranges::all_of(show.lampLevels(), [](float l) { return l == 0.0F; }));
  }
}

// The most large flashes started in any one second.
std::uint32_t mostPerSecond(const std::vector<std::uint64_t>& onsets) {
  std::uint32_t most = 0;
  for (std::size_t i = 0; i < onsets.size(); ++i) {
    std::uint32_t n = 0;
    for (std::size_t j = i; j < onsets.size() && onsets[j] < onsets[i] + 120; ++j) {
      ++n;
    }
    most = std::max(most, n);
  }
  return most;
}

// Rising edges of the large-flash level through a threshold, in any second.
std::uint32_t mostEdgesPerSecond(const std::vector<float>& level, float threshold) {
  std::vector<std::size_t> edges;
  for (std::size_t i = 1; i < level.size(); ++i) {
    if (level[i - 1] < threshold && level[i] >= threshold) {
      edges.push_back(i);
    }
  }
  std::uint32_t most = 0;
  for (std::size_t i = 0; i < edges.size(); ++i) {
    std::uint32_t n = 0;
    for (std::size_t j = i; j < edges.size() && edges[j] < edges[i] + 120; ++j) {
      ++n;
    }
    most = std::max(most, n);
  }
  return most;
}

void stormOnce(bool reduceMotion) {
  // Washes, dips, large domes and super jackpots asked for every few steps:
  // the limiter lets no more than three large flashes start in any second
  // (one with reduce motion), and the level the eye sees agrees.
  Game game;
  LightShow show;
  show.setReduceMotion(reduceMotion);
  std::vector<std::uint64_t> onsets;
  std::vector<float> level;
  std::uint32_t seen = 0;
  for (std::uint32_t i = 0; i < 2400; ++i) {
    if (i % 3 == 0) {
      show.burst(fx::Flasher::Table, fx::Colour::White, 1.0F, 0.0F, 30);
    }
    if (i % 7 == 0) {
      show.burst(fx::Flasher::Lights, fx::Colour::Warm, 0.8F, 0.0F, 30);
    }
    if (i % 5 == 0) {
      show.burst(fx::Flasher::Hangar, fx::Colour::Orange, 1.0F, 200.0F, 30);
    }
    if (i % 50 == 0) {
      show.startShow(fx::Show::SuperJackpot, {300.0F, 400.0F});
    }
    step(game, show);
    if (show.largeFlashes() != seen) {
      seen = show.largeFlashes();
      onsets.push_back(i);
    }
    level.push_back(show.largeFlashLevel());
  }
  const std::uint32_t limit = reduceMotion ? 1U : 3U;
  CHECK(!onsets.empty());
  CHECK(mostPerSecond(onsets) <= limit);
  CHECK(mostEdgesPerSecond(level, 0.25F) <= limit);
  CHECK(show.largeFlashesHeld() > 0);
}

void testLimiter() {
  stormOnce(false);
  stormOnce(true);
  // Small flashes are not held: a small dome starts at once whatever ran.
  Game game;
  LightShow show;
  show.burst(fx::Flasher::Table, fx::Colour::White, 1.0F, 0.0F, 30);
  step(game, show);
  const std::uint32_t before = show.burstsStarted();
  show.burst(fx::Flasher::Nest, fx::Colour::Cyan, 1.0F, 60.0F, 30);
  show.burst(fx::Flasher::Table, fx::Colour::White, 1.0F, 0.0F, 30);
  step(game, show);
  CHECK(show.burstsStarted() == before + 1);
  CHECK(show.largeFlashesHeld() == 1);
  // Domes lit in the same step are one flash, not four.
  Game g2;
  LightShow s2;
  for (const fx::Flasher dome : {fx::Flasher::Hangar, fx::Flasher::Briefing,
                                 fx::Flasher::RampLeft, fx::Flasher::RampRight}) {
    s2.burst(dome, fx::Colour::Magenta, 1.0F, 150.0F, 30);
  }
  step(g2, s2);
  CHECK(s2.largeFlashes() == 1 && s2.burstsStarted() == 4);
}

void testReduceMotionShows() {
  // With reduce motion the multiball show has no sweep and no flicker: the
  // lights fade down and up without a jump.
  Game game;
  LightShow show;
  show.setReduceMotion(true);
  CHECK(pinball::stageScene(game, pinball::Scene::Hangar));
  show.observe(game);
  const std::size_t slot = game.ball().id;
  Seam::placeBall(game, slot, {279.0F, 440.0F}, {0.0F, -900.0F});
  Seam::putInPlay(game, slot);
  bool started = false;
  bool swept = false;
  float worstGi = 0.0F;
  float previous = 1.0F;
  for (int i = 0; i < 900; ++i) {
    step(game, show);
    started = started || show.show() == fx::Show::MultiballStart;
    swept = swept || show.sweepY() >= 0.0F || show.ringRadius() >= 0.0F;
    worstGi = std::max(worstGi, std::fabs(show.giLevel() - previous));
    previous = show.giLevel();
    for (std::size_t a = 0; a < 7; ++a) {
      const fx::ChaseView chase = show.chase(a);
      CHECK(!chase.active || chase.steady);
    }
  }
  CHECK(started);
  CHECK(!swept);
  CHECK(worstGi < 0.05F);
}

void testAttract() {
  Game game;
  LightShow show;
  show.setAttract(true);
  std::array<bool, LightShow::kAttractPatterns> seen{};
  int samples = 0;
  int varied = 0;
  for (std::uint32_t i = 0; i < LightShow::kAttractPatternSteps * 5; ++i) {
    step(game, show, flipping(i));
    seen[static_cast<std::size_t>(show.attractPattern())] = true;
    if (i % 97 == 50) {
      const auto [lo, hi] = std::ranges::minmax(show.lampLevels());
      ++samples;
      varied += hi - lo > 0.3F ? 1 : 0;
    }
  }
  CHECK(std::ranges::all_of(seen, [](bool s) { return s; }));
  CHECK(varied * 10 >= samples * 9);  // Nine looks in ten see a pattern.
  CHECK(show.burstsStarted() > 10);  // The domes fire now and then.
  CHECK(show.giLevel() == 1.0F);
  // The same steps give the same attract lights.
  Game again;
  LightShow other;
  other.setAttract(true);
  for (std::uint32_t i = 0; i < LightShow::kAttractPatternSteps * 5; ++i) {
    step(again, other, flipping(i));
  }
  CHECK(other.lampLevels() == show.lampLevels());
  show.setAttract(false);
  step(game, show);
  CHECK(show.attractPattern() == 0);
}

void testTrails() {
  Game game;
  LightShow show;
  inPlay(game, show);
  const std::size_t slot = game.ball().id;
  Seam::placeBall(game, slot, {380.0F, 700.0F}, {0.0F, -1500.0F});
  for (int i = 0; i < 6; ++i) {
    step(game, show);
  }
  const fx::TrailView& trail = show.trail(slot);
  CHECK(trail.count == 6);
  CHECK(trail.speed > 8.0F);
  // Moved by hand: the trail starts again rather than streak across.
  Seam::placeBall(game, slot, {380.0F, 200.0F}, {0.0F, 0.0F});
  step(game, show);
  CHECK(show.trail(slot).count == 1);
}

void testHitGates() {
  // A ball rattling in the bumper nest: a new flash starts there at most
  // once in kMinHitSteps, however often it is struck; a hit sooner lets the
  // lit flash run on.
  Game game;
  LightShow show;
  inPlay(game, show);
  const std::size_t slot = game.ball().id;
  std::vector<std::uint64_t> onsets;
  std::array<float, 3> was{};
  int hits = 0;
  for (std::uint32_t i = 0; i < 1200; ++i) {
    if (i % 15 == 0) {
      Seam::placeBall(game, slot, {262.0F, 165.0F}, {20.0F, 700.0F});
    }
    step(game, show);
    for (const pinball::PhysicsEvent& e : game.events()) {
      hits += e.type == pinball::PhysicsEvent::Type::Hit &&
                      e.element.kind == table::ElementKind::Bumper
                  ? 1
                  : 0;
    }
    for (std::size_t b = 0; b < 3; ++b) {
      const float f = show.bumperFlash(b);
      if (f > was[b] + 0.05F) {
        onsets.push_back(show.step());
      }
      was[b] = f;
    }
  }
  CHECK(hits > static_cast<int>(onsets.size()));  // Some hits started nothing.
  CHECK(onsets.size() >= 5);
  bool spaced = true;
  for (std::size_t k = 1; k < onsets.size(); ++k) {
    spaced = spaced && onsets[k] - onsets[k - 1] >= fx::kMinHitSteps;
  }
  CHECK(spaced);
}

void testReduceMotionHits() {
  // With reduce motion a hit is a soft glow: it rises over several steps,
  // never jumps, never reaches full, and the nest's gate is a second and a
  // quarter.
  Game game;
  LightShow show;
  show.setReduceMotion(true);
  inPlay(game, show);
  const std::size_t slot = game.ball().id;
  float previous = 0.0F;
  float worstJump = 0.0F;
  float peak = 0.0F;
  bool rose = false;
  for (int i = 0; i < 1500; ++i) {
    if (i % 200 == 0) {
      Seam::placeBall(game, slot, {235.0F, 226.0F}, {-800.0F, 0.0F});
    }
    step(game, show);
    const float f = show.targetFlash(0);
    worstJump = std::max(worstJump, std::fabs(f - previous));
    peak = std::max(peak, f);
    rose = rose || f > 0.0F;
    previous = f;
  }
  CHECK(rose);
  CHECK(worstJump < 0.1F);
  CHECK(peak <= 0.6F + 1e-6F);
  // No running lights: on a quiet table the unlit lamps hold still.
  Game quiet;
  LightShow still;
  still.setReduceMotion(true);
  step(quiet, still);
  const std::array<float, LightShow::kLampCount> before = still.lampLevels();
  int moved = 0;
  for (int i = 0; i < 300; ++i) {
    step(quiet, still);
  }
  for (std::size_t k = 0; k < before.size(); ++k) {
    moved += still.lampState(table::kLamps[k].id) == LampState::Off &&
                     std::fabs(still.lampLevels()[k] - before[k]) > 0.01F
                 ? 1
                 : 0;
  }
  CHECK(moved == 0);
}

void testReduceMotionReshapes() {
  // A burst already queued takes the reduce motion shape as soon as the
  // setting changes, and the setting can be given before the first look.
  Game game;
  LightShow show;
  show.setReduceMotion(true);
  CHECK(show.reduceMotion());
  show.observe(game);
  show.setReduceMotion(false);
  show.burst(fx::Flasher::Nest, fx::Colour::Cyan, 1.0F, 50.0F, 40, 1);
  show.setReduceMotion(true);
  step(game, show);
  step(game, show);
  const fx::BurstViews soft = show.bursts();
  CHECK(soft.count == 1 && soft.items[0].level < 0.1F);  // A slow rise, not a flash.
  show.setReduceMotion(false);
  const fx::BurstViews sharp = show.bursts();
  CHECK(sharp.count == 1 && sharp.items[0].level > 0.5F);
}

void testShowStartsLimited() {
  // Jackpots asked for every ten steps: the ring and wash start again at
  // most once in kMinLargeFlashSteps; a multiball show told twice starts once.
  Game game;
  LightShow show;
  step(game, show);
  std::vector<std::uint64_t> starts;
  for (std::uint32_t i = 0; i < 600; ++i) {
    if (i % 10 == 0) {
      show.startShow(fx::Show::Jackpot, {380.0F, 600.0F});
    }
    step(game, show);
    const std::uint64_t began = show.step() - show.showSteps();
    if (show.show() == fx::Show::Jackpot && (starts.empty() || starts.back() != began)) {
      starts.push_back(began);
    }
  }
  CHECK(starts.size() >= 5);
  bool spaced = true;
  for (std::size_t k = 1; k < starts.size(); ++k) {
    spaced = spaced && starts[k] - starts[k - 1] >= fx::kMinLargeFlashSteps;
  }
  CHECK(spaced);
  Game g2;
  LightShow s2;
  step(g2, s2);
  s2.startShow(fx::Show::Graduation);
  for (int i = 0; i < 20; ++i) {
    step(g2, s2);
  }
  s2.startShow(fx::Show::Graduation);
  step(g2, s2);
  CHECK(s2.show() == fx::Show::Graduation && s2.showSteps() > 15);
}

void testSmallDomesAddUp() {
  // Small domes lit together count as large once they cover enough of the
  // table between them: the limiter then holds the rest.
  Game game;
  LightShow show;
  step(game, show);
  for (int k = 0; k < 8; ++k) {
    show.burst(static_cast<fx::Flasher>(k % 8), fx::Colour::Lime, 0.8F, 70.0F, 30);
  }
  step(game, show);
  CHECK(show.largeFlashes() >= 1);
  CHECK(show.burstsStarted() < 8 || show.largeFlashes() >= 1);
}

void testDrainKeepsLitLamps() {
  // During the bonus count a lamp the rules light keeps its full level, so
  // it is never dimmer than a chase.
  Game game;
  LightShow show;
  inPlay(game, show);
  Seam::rules(game).setBallSaveEnabled(false);
  const int ball = game.rules().ball();
  Seam::placeBall(game, game.ball().id, {table::kWidth * 0.5F - 8.0F, 860.0F}, {0.0F, 600.0F});
  for (int i = 0; i < 400 && game.rules().ball() == ball; ++i) {
    step(game, show);
  }
  bool kept = true;
  for (int i = 0; i < 120; ++i) {
    step(game, show);
    for (const table::Lamp& lamp : table::kLamps) {
      if (show.lampState(lamp.id) == LampState::On) {
        kept = kept && show.lampLevel(lamp.id) == 1.0F;
      }
    }
  }
  CHECK(show.show() == fx::Show::Drain);
  CHECK(kept);
}

void testTrailsFollowTheDrawnBall() {
  // A ball drawn into the Hangar: no trail while it sinks, and otherwise
  // the trail starts where the ball is drawn.
  Game game;
  LightShow show;
  CHECK(pinball::stageScene(game, pinball::Scene::Hangar));
  show.observe(game);
  const std::size_t slot = game.ball().id;
  Seam::placeBall(game, slot, {279.0F, 440.0F}, {0.0F, -900.0F});
  Seam::putInPlay(game, slot);
  bool sank = false;
  bool follows = true;
  for (int i = 0; i < 300; ++i) {
    step(game, show);
    for (const pinball::BallView& b : game.balls(1.0F)) {
      const fx::TrailView& t = show.trail(b.id);
      if (b.sink > 0.0F) {
        sank = true;
        follows = follows && t.count == 0;
      } else if (b.visible && !b.locked && t.count > 0) {
        const Vec2 at = b.drawnAt();
        follows = follows && t.points[0].x == at.x && t.points[0].y == at.y;
      }
    }
  }
  CHECK(sank);
  CHECK(follows);
}

}  // namespace

void testLightShow() {
  testBlinkPhases();
  testReduceMotionLamps();
  testLampsFollowRules();
  testAmbientIsFarBelowLit();
  testSameGameSameLights();
  testWatchingChangesNothing();
  testHitFlashes();
  testMultiballShow();
  testJackpotShow();
  testDrainShow();
  testTiltShow();
  testLimiter();
  testReduceMotionShows();
  testAttract();
  testTrails();
  testHitGates();
  testReduceMotionHits();
  testReduceMotionReshapes();
  testShowStartsLimited();
  testSmallDomesAddUp();
  testDrainKeepsLitLamps();
  testTrailsFollowTheDrawnBall();
}
