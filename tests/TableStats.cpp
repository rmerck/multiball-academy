// Table review instrument (TABLE_REVIEW_2): where the ball goes, per ball and
// per minute of play, and where Briefing room entries come from.
//
//   table_stats games <p0|p1|p2|hunt> <error> <games> <seed> [target]
//   table_stats fan
//
// games: whole games under the real rules (saver, kickback, multiballs), as
//   playGames does. hunt is a skilled player: it holds both flippers up,
//   lets the ball settle in a cradle, then drops the flipper and re-presses
//   at the centre of the target's timing window from that cradle, missing it
//   by a uniform error of +-error steps. target: briefing (default) or ramps
//   (Skyway from the left cradle, Runway from the right: the cross loop).
// fan: the press-step fan from each cradle (first thing met, by step).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "bots/Acceptance.hpp"
#include "bots/Bots.hpp"
#include "bots/TableHarness.hpp"
#include "core/Rules.hpp"
#include "core/TableLayout.hpp"

using namespace pinball;
using namespace pinball::bots;

namespace {

constexpr int kHz = 120;

// --- The cradle fan -------------------------------------------------------------------
std::string cradleFan(bool rightSide, int maxPress) {
  std::string fan;
  for (int press = 0; press < maxPress; ++press) {
    TableHarness h;
    h.setLocks(false);
    bool& flip = rightSide ? h.right : h.left;
    flip = true;
    for (int i = 0; i < 20; ++i) {
      h.advance();
    }
    const Vec2 pivot = table::kFlippers[rightSide ? 1 : 0].pivot;
    h.put(0, {pivot.x + (rightSide ? -45.0F : 45.0F), pivot.y - 60.0F}, {}, true);
    for (int i = 0; i < 240; ++i) {
      h.advance();
    }
    h.events.clear();
    Touch got = Touch::None;
    for (int i = 0; i < kHz * 5 && got == Touch::None; ++i) {
      flip = i >= press && i < press + 30;
      const std::size_t before = h.events.size();
      const Touch drain = h.advance();
      if (std::getenv("TRACE_FAN") != nullptr && i >= press && i % 6 == 0 &&
          std::atoi(std::getenv("TRACE_FAN")) == press && rightSide == (std::getenv("TRACE_R") != nullptr)) {
        std::printf("  k=%d i=%d p=(%.0f,%.0f) v=(%.0f,%.0f)\n", press, i, h.position(0).x, h.position(0).y,
                    h.velocity(0).x, h.velocity(0).y);
      }
      if (i < press) {
        continue;
      }
      for (const TouchEvent& e : std::span(h.events).subspan(before)) {
        if (e.touch != Touch::InlaneLeft && e.touch != Touch::InlaneRight && e.touch != Touch::Flaps) {
          got = e.touch;
          break;
        }
      }
      if (got == Touch::None && drain != Touch::None) {
        got = drain;
      }
    }
    static constexpr char kChars[] = ".OLwDHSPWRQbs^ijxyc*dTGeKkYf";
    fan += kChars[static_cast<std::size_t>(got)];
  }
  return fan;
}

// Centre of the widest run of c in fan, or -1.
int aimFor(const std::string& fan, char c) {
  int best = 0;
  int bestStart = -1;
  for (std::size_t i = 0; i < fan.size();) {
    if (fan[i] != c) {
      ++i;
      continue;
    }
    std::size_t j = i;
    while (j < fan.size() && fan[j] == c) {
      ++j;
    }
    if (static_cast<int>(j - i) > best) {
      best = static_cast<int>(j - i);
      bestStart = static_cast<int>(i);
    }
    i = j;
  }
  return bestStart < 0 ? -1 : bestStart + best / 2;
}
int widest(const std::string& fan, char c) {
  int best = 0;
  for (std::size_t i = 0; i < fan.size();) {
    std::size_t j = i;
    while (j < fan.size() && fan[j] == c) {
      ++j;
    }
    best = std::max(best, static_cast<int>(j - i));
    i = j == i ? i + 1 : j;
  }
  return best;
}

// --- The skilled player ---------------------------------------------------------------
class HunterBot final : public Bot {
 public:
  HunterBot(std::uint64_t seed, int error, std::array<int, 2> aim) noexcept
      : rng_(seed ^ 0xC0FFEEULL), error_(error), aim_(aim) {}
  Flippers act(const Sense& sense) override {
    std::array<bool, 2> up{true, true};
    if (shooting_ >= 0) {
      const auto s = static_cast<std::size_t>(shooting_);
      ++clock_;
      up[s] = clock_ >= pressAt_ && clock_ < pressAt_ + 30;
      if (clock_ >= pressAt_ + 30) {
        shooting_ = -1;
        ++shots;
      }
      return {up[0], up[1]};
    }
    // A ball at rest low on one side, with both flippers up, is cradled.
    bool any = false;
    for (std::size_t b = 0; b < sense.count; ++b) {
      const BallSense& ball = sense.balls[b];
      const float speed = std::sqrt(ball.velocity.x * ball.velocity.x +
                                    ball.velocity.y * ball.velocity.y);
      if (ball.position.y > 760.0F && speed < 25.0F) {
        any = true;
        if (++still_ > 30) {
          shooting_ = ball.position.x < table::kFieldCenterX ? 0 : 1;
          clock_ = 0;
          const auto s = static_cast<std::size_t>(shooting_);
          pressAt_ = aim_[s] + rng_.range(-error_, error_);
          still_ = 0;
          up[s] = false;
        }
        break;
      }
    }
    if (!any) {
      still_ = 0;
    }
    return {up[0], up[1]};
  }
  int shots = 0;

 private:
  Rng rng_;
  int error_;
  std::array<int, 2> aim_;
  int shooting_ = -1;
  int clock_ = 0;
  int pressAt_ = 0;
  int still_ = 0;
};

// --- Where a Briefing entry came from -------------------------------------------------
enum Source : std::uint8_t {
  FlipShot,       // An active flip (flipper pressed within 20 steps of contact) was the last thing.
  FlipPassive,    // Rolled or bounced off a flipper not being swung.
  Sling,
  UpperRebound,   // Bumper, turbo target, bay door, roll call.
  OwnKick,        // Straight from the Briefing room's own kick-out, nothing between.
  OtherEject,     // From the Hangar's kick or a ramp's set-down, nothing between.
  Other,          // A wall only, or launch.
  kSources
};
constexpr std::array<const char*, kSources> kSourceNames{
    "flipper shot", "off a still flipper", "slingshot", "bumper/target/door/lane",
    "own kick-out", "other eject", "other"};

struct BallTrack {
  bool active = false;
  std::uint64_t flipStep = 0;
  bool flipActive = false;
  int flipper = -1;
  std::uint64_t eventStep = 0;
  Touch eventTouch = Touch::None;
  std::uint64_t ejectStep = 0;
  int ejectHole = -1;
  std::uint64_t lastBriefingEject = 0;
  bool flippedSinceBriefing = false;
  std::uint64_t gateStep = 0;
};

struct Stats {
  double playSeconds = 0;
  int balls = 0;
  int games = 0;
  std::vector<double> gameSeconds;
  std::array<std::uint64_t, kTouchCount> count{};
  std::array<std::uint64_t, kSources> source{};
  std::array<std::uint64_t, 2> flipShotBy{};  // Left, right flipper.
  std::uint64_t briefing = 0;
  std::uint64_t briefingRepeat5 = 0;     // Within 5 s of this ball's last Briefing kick-out.
  std::uint64_t briefingRepeat10 = 0;
  std::uint64_t briefingRepeatNoFlip = 0;  // With no flipper contact since that kick-out.
  std::uint64_t coursesStarted = 0;
  std::uint64_t hunterShots = 0;
  std::array<std::uint64_t, 3> drains{};
  std::vector<float> entrySpeed;
  // Ball-steps by zone: top (y<300), shot band (300-480), mid-field (480-760),
  // flipper zone (>760), held in a hole or on a rail.
  std::array<std::uint64_t, 6> zone{};
  double speedSum = 0;
  std::uint64_t speedSamples = 0;
  // Gaps between scoring switches (any logged touch), in play.
  std::vector<double> gaps;
  std::uint64_t lastSwitch = 0;
  std::uint64_t cradledSteps = 0;
  std::array<std::uint64_t, 5> standup{};
  std::uint64_t gateBounces = 0;  // Turned back just below the Briefing room's mouth.
  std::uint64_t gateThroughGap = 0;  // Of those, drained down the centre with no flipper touch since.
};

std::unique_ptr<Bot> makeBot(const std::string& kind, std::uint64_t seed, int error,
                             std::array<int, 2> aim) {
  if (kind == "p0") return std::make_unique<IdleBot>();
  if (kind == "p1") return std::make_unique<RandomBot>(seed);
  if (kind == "hunt") return std::make_unique<HunterBot>(seed, error, aim);
  return std::make_unique<ReflexBot>(seed, error);
}

Stats playGames(const std::string& kind, int error, int games, std::uint64_t seed,
                std::array<int, 2> aim) {
  Stats st;
  TableHarness h;
  h.setSaver(true);
  h.setLocks(true);
  Game& game = h.game();
  const Rules& rules = game.rules();
  std::unique_ptr<Bot> bot = makeBot(kind, seed, error, aim);
  Rng rng(seed ^ 0x6A3E5ULL);
  std::array<BallTrack, table::kMaxBalls> track{};
  std::array<std::uint64_t, 2> pressStart{};
  std::array<bool, 2> wasDown{};
  std::array<float, table::kMaxBalls> lastVy{};
  constexpr std::uint64_t kCap = static_cast<std::uint64_t>(kHz) * 1800U;
  while (st.games < games) {
    std::uint64_t gameStart = 0;
    bool started = false;
    int pull = 0;
    int waited = 0;
    int ballNumber = rules.ball();
    while (true) {
      waited = game.ballAwaitingLaunch() ? waited + 1 : 0;
      if (pull == 0 && waited > 20) {
        pull = rng.range(60, 125);
        if (!started) {
          started = true;
          gameStart = h.step();
        }
      }
      h.plunger = pull > 1;
      pull = pull > 0 ? pull - 1 : 0;
      if (rules.ballInPlay()) {
        h.act(*bot);
        st.playSeconds += 1.0 / kHz;
      } else {
        h.left = false;
        h.right = false;
      }
      const std::array<bool, 2> down{h.left, h.right};
      for (std::size_t f = 0; f < 2; ++f) {
        if (down[f] && !wasDown[f]) pressStart[f] = h.step();
        wasDown[f] = down[f];
      }
      const std::size_t before = h.events.size();
      const std::uint64_t centreBefore = h.drains[0];
      h.advance();
      const std::uint64_t now = h.step();
      if (rules.ballInPlay()) {
        std::array<bool, table::kMaxBalls> locked{};
        std::array<bool, table::kMaxBalls> raised{};
        const BallViews views = game.balls(1.0F);
        for (const BallView& view : views) {
          if (view.id < table::kMaxBalls) {
            locked[view.id] = view.locked;
            raised[view.id] = view.raised;
          }
        }
        for (std::size_t s2 = 0; s2 < table::kMaxBalls; ++s2) {
          if (!h.live(s2) || !game.ballInPlay(s2)) continue;
          if (locked[s2]) continue;
          if (h.held(s2)) { ++st.zone[raised[s2] ? 5 : 4]; continue; }
          const Vec2 p = h.position(s2);
          const Vec2 v = h.velocity(s2);
          const float y = p.y;
          ++st.zone[y < 300 ? 0 : y < 480 ? 1 : y < 760 ? 2 : 3];
          const float sp = std::sqrt(v.x * v.x + v.y * v.y);
          st.speedSum += sp;
          ++st.speedSamples;
          if (sp < 25.0F && y > 760) ++st.cradledSteps;
          if (y > 430.0F && y < 446.0F && p.x > 316.0F && p.x < 396.0F && v.y > 0.0F &&
              lastVy[s2] < 0.0F) {
            ++st.gateBounces;
            track[s2].gateStep = now;
          }
          lastVy[s2] = v.y;
        }
        bool any = false;
        for (const TouchEvent& e : std::span(h.events).subspan(before)) {
          any = any || (e.touch != Touch::None);
        }
        if (any) {
          if (st.lastSwitch != 0) st.gaps.push_back(static_cast<double>(now - st.lastSwitch) / kHz);
          st.lastSwitch = now;
        }
      } else {
        st.lastSwitch = 0;
      }
      // Ball bookkeeping.
      const bool centreDrain = h.drains[0] > centreBefore;
      for (std::size_t s = 0; s < table::kMaxBalls; ++s) {
        BallTrack& t = track[s];
        if (centreDrain && t.active && t.gateStep != 0 && t.gateStep > t.flipStep &&
            (!h.live(s) || !game.ballInPlay(s) || h.position(s).y > 900.0F)) {
          ++st.gateThroughGap;
          t.gateStep = 0;
        }
        if (!h.live(s) || !game.ballInPlay(s)) {
          if (!h.live(s) || !h.held(s)) t = {};
          continue;
        }
        if (!t.active) {
          t = {};
          t.active = true;
        }
        if (h.held(s)) continue;
        const Vec2 p = h.position(s);
        for (std::size_t f = 0; f < 2; ++f) {
          if (h.nearFlipper(p, f, 1.0F)) {
            t.flipStep = now;
            t.flipper = static_cast<int>(f);
            t.flipActive = down[f] && now - pressStart[f] <= 20;
            t.flippedSinceBriefing = true;
          }
        }
      }
      for (const GameEvent& e : game.gameEvents()) {
        if (e.type == GameEvent::Type::HoleEject && e.ball < table::kMaxBalls) {
          BallTrack& t = track[e.ball];
          t.ejectStep = now;
          t.ejectHole = e.index;
          if (e.index == table::kScoopHole) {
            t.lastBriefingEject = now;
            t.flippedSinceBriefing = false;
          }
        }
      }
      for (const TouchEvent& e : std::span(h.events).subspan(before)) {
        if (e.ball >= table::kMaxBalls) continue;
        BallTrack& t = track[e.ball];
        if (e.touch == Touch::Briefing) {
          ++st.briefing;
          st.entrySpeed.push_back(e.speed);
          Source src = Other;
          const std::uint64_t last = std::max({t.flipStep, t.eventStep, t.ejectStep});
          if (last == 0) {
            src = Other;
          } else if (last == t.ejectStep && t.ejectStep >= t.flipStep &&
                     t.ejectStep >= t.eventStep) {
            src = t.ejectHole == static_cast<int>(table::kScoopHole) ? OwnKick : OtherEject;
          } else if (last == t.flipStep) {
            src = t.flipActive ? FlipShot : FlipPassive;
            if (t.flipActive) ++st.flipShotBy[static_cast<std::size_t>(t.flipper)];
          } else {
            switch (t.eventTouch) {
              case Touch::Slingshot: src = Sling; break;
              case Touch::Bumper:
              case Touch::TargetLeft:
              case Touch::TargetRight:
              case Touch::Door:
              case Touch::RollCall: src = UpperRebound; break;
              default: src = Other; break;
            }
          }
          ++st.source[src];
          if (t.lastBriefingEject != 0) {
            const double since = static_cast<double>(now - t.lastBriefingEject) / kHz;
            st.briefingRepeat5 += since <= 5.0 ? 1U : 0U;
            st.briefingRepeat10 += since <= 10.0 ? 1U : 0U;
            st.briefingRepeatNoFlip += t.flippedSinceBriefing ? 0U : 1U;
          }
        }
        if (e.touch != Touch::InlaneLeft && e.touch != Touch::InlaneRight && e.touch != Touch::Flaps &&
            e.touch != Touch::CircuitLeft && e.touch != Touch::CircuitRight &&
            e.touch != Touch::Turbine && e.touch != Touch::OutlaneLeft &&
            e.touch != Touch::OutlaneRight) {
          t.eventStep = now;
          t.eventTouch = e.touch;
        }
      }
      if (h.events.size() > 4096) h.events.clear();
      if (rules.gameOver() || rules.ball() != ballNumber) {
        ++st.balls;
        ballNumber = rules.ball();
      }
      if (rules.gameOver()) break;
      if (started && now - gameStart > kCap) {
        h.restart = true;
        break;
      }
    }
    ++st.games;
    st.gameSeconds.push_back(static_cast<double>(h.step() - gameStart) / kHz);
    st.coursesStarted += static_cast<std::uint64_t>(rules.coursesStarted());
    for (std::size_t i = 0; i < kTouchCount; ++i) st.count[i] += h.count[i];
    for (std::size_t i = 0; i < st.standup.size(); ++i) st.standup[i] += h.standupHits[i];
    h.standupHits = {};
    h.count = {};
    for (std::size_t i = 0; i < 3; ++i) st.drains[i] += h.drains[i];
    h.drains = {};
    h.events.clear();
    track = {};
    h.left = h.right = h.plunger = false;
    h.advance();
  }
  if (auto* hunter = dynamic_cast<HunterBot*>(bot.get())) st.hunterShots = hunter->shots;
  return st;
}

void report(const char* label, const Stats& st) {
  const double minutes = st.playSeconds / 60.0;
  std::vector<double> g = st.gameSeconds;
  std::ranges::sort(g);
  const double median = g.empty() ? 0 : g[g.size() / 2];
  std::printf("== %s: games %d, balls %d, ball-in-play %.1f min, median game %.0f s, courses/game %.2f\n",
              label, st.games, st.balls, minutes, median,
              static_cast<double>(st.coursesStarted) / st.games);
  const std::array<Touch, 18> shown{Touch::Drone, Touch::GForce, Touch::Standup, Touch::Briefing, Touch::Hangar,  Touch::Door,
                                    Touch::Runway,   Touch::Skyway,  Touch::Turbine,
                                    Touch::CircuitLeft, Touch::CircuitRight, Touch::Bumper,
                                    Touch::Slingshot, Touch::TargetLeft, Touch::TargetRight,
                                    Touch::RollCall, Touch::InlaneLeft, Touch::InlaneRight};
  std::printf("   %-14s %9s %8s %8s\n", "switch", "count", "/ball", "/min");
  for (Touch t : shown) {
    const auto c = static_cast<double>(st.count[static_cast<std::size_t>(t)]);
    std::printf("   %-14s %9.0f %8.2f %8.2f\n", touchName(t), c, c / st.balls, c / minutes);
  }
  std::printf("   stand-ups T/A/X/I/G: %llu/%llu/%llu/%llu/%llu\n", (unsigned long long)st.standup[0],
              (unsigned long long)st.standup[1], (unsigned long long)st.standup[2],
              (unsigned long long)st.standup[3], (unsigned long long)st.standup[4]);
  const double b = static_cast<double>(st.briefing);
  std::printf("   Briefing entries by source (%llu):", static_cast<unsigned long long>(st.briefing));
  for (std::size_t i = 0; i < kSources; ++i) {
    std::printf(" %s %.1f%%%s", kSourceNames[i], 100.0 * st.source[i] / std::max(1.0, b),
                i + 1 < kSources ? "," : "\n");
  }
  std::printf("   turned back just below the Briefing mouth: %llu (%.2f/min); then down the centre untouched %llu\n",
              static_cast<unsigned long long>(st.gateBounces), st.gateBounces / minutes,
              static_cast<unsigned long long>(st.gateThroughGap));
  std::printf("   flipper shots into Briefing: left %llu, right %llu\n",
              static_cast<unsigned long long>(st.flipShotBy[0]),
              static_cast<unsigned long long>(st.flipShotBy[1]));
  std::printf("   repeats: within 5 s of the same ball's last Briefing kick-out %.1f%%, within 10 s %.1f%%, "
              "no flipper touch since %.1f%%\n",
              100.0 * st.briefingRepeat5 / std::max(1.0, b),
              100.0 * st.briefingRepeat10 / std::max(1.0, b),
              100.0 * st.briefingRepeatNoFlip / std::max(1.0, b));
  std::vector<float> sp = st.entrySpeed;
  std::ranges::sort(sp);
  if (!sp.empty()) {
    std::printf("   entry speed px/s: p10 %.0f, median %.0f, p90 %.0f\n", sp[sp.size() / 10],
                sp[sp.size() / 2], sp[sp.size() * 9 / 10]);
  }
  const double drains = static_cast<double>(st.drains[0] + st.drains[1] + st.drains[2]);
  std::printf("   drains centre/left/right %.1f/%.1f/%.1f%%\n", 100 * st.drains[0] / drains,
              100 * st.drains[1] / drains, 100 * st.drains[2] / drains);
  {
    double total = 0;
    for (auto z : st.zone) total += static_cast<double>(z);
    std::printf("   ball time: top %.1f%%, shot band %.1f%%, mid-field %.1f%%, flipper zone %.1f%% (of it at rest %.1f%%), held in a hole %.1f%%, on a ramp rail %.1f%%; mean speed %.0f px/s\n",
                100 * st.zone[0] / total, 100 * st.zone[1] / total, 100 * st.zone[2] / total,
                100 * st.zone[3] / total, 100 * st.cradledSteps / total, 100 * st.zone[4] / total, 100 * st.zone[5] / total,
                st.speedSum / static_cast<double>(st.speedSamples));
    std::vector<double> gp = st.gaps;
    std::ranges::sort(gp);
    double over3 = 0;
    double sum = 0;
    for (double g2 : gp) { sum += g2; if (g2 > 3.0) over3 += g2; }
    if (!gp.empty())
      std::printf("   gaps between switches: median %.2f s, p90 %.2f s, share of play time in gaps over 3 s %.1f%%\n",
                  gp[gp.size() / 2], gp[gp.size() * 9 / 10], 100 * over3 / sum);
  }
  if (st.hunterShots > 0) {
    const auto shots = static_cast<double>(st.hunterShots);
    std::printf("   hunter: %llu aimed shots; per shot: Briefing %.2f, Runway %.2f, Skyway %.2f\n",
                static_cast<unsigned long long>(st.hunterShots),
                st.count[static_cast<std::size_t>(Touch::Briefing)] / shots,
                st.count[static_cast<std::size_t>(Touch::Runway)] / shots,
                st.count[static_cast<std::size_t>(Touch::Skyway)] / shots);
  }
}

}  // namespace

int main(int argc, char** argv) {
  const std::string mode = argc > 1 ? argv[1] : "fan";
  const std::string fanL = cradleFan(false, 110);
  const std::string fanR = cradleFan(true, 110);
  if (mode == "b2") {
    // The B2 soak (one ball, saver and kickback off, P2 at error E): balls
    // ending within 10 s, by the last thing touched before the drain, and
    // where balls got stuck.
    const int error = argc > 2 ? std::atoi(argv[2]) : 5;
    const int balls = argc > 3 ? std::atoi(argv[3]) : 2000;
    TableHarness h;
    h.setLocks(false);
    h.kickbackOff = true;
    ReflexBot bot(1, error);
    Rng rng(0xABCDEFULL);
    std::array<int, kTouchCount> lastBy5{}, lastBy10{};
    int under5 = 0, under10 = 0;
    std::array<int, 5> standup10{};
    for (int n = 0; n < balls; ++n) {
      h.left = h.right = false;
      for (int i = 0; i < 20; ++i) h.advance();
      h.plunger = true;
      const int hold = rng.range(60, 125);
      for (int i = 0; i < hold; ++i) h.advance();
      h.plunger = false;
      std::uint64_t start = 0;
      Touch last = Touch::None;
      int lastStandup = -1;
      for (int i = 0; i < kHz * 1200; ++i) {
        if (start == 0 && h.game().ballInPlay(0)) start = h.step();
        if (start != 0) h.act(bot); else { h.left = h.right = false; }
        const std::size_t before = h.events.size();
        const auto hitsBefore = h.standupHits;
        const Touch drain = h.advance();
        for (const TouchEvent& e : std::span(h.events).subspan(before)) {
          if (e.touch != Touch::OutlaneLeft && e.touch != Touch::OutlaneRight) last = e.touch;
        }
        for (std::size_t k = 0; k < hitsBefore.size(); ++k) {
          if (h.standupHits[k] != hitsBefore[k]) lastStandup = static_cast<int>(k);
        }
        if (h.events.size() > 4096) h.events.clear();
        if (drain != Touch::None) {
          const double t = start == 0 ? 0.0 : static_cast<double>(h.step() - start) / kHz;
          if (start != 0 && t <= 5.0) { ++under5; ++lastBy5[static_cast<std::size_t>(last)]; }
          if (start != 0 && t <= 10.0) {
            ++under10;
            ++lastBy10[static_cast<std::size_t>(last)];
            if (last == Touch::Standup) ++standup10[static_cast<std::size_t>(lastStandup)];
          }
          break;
        }
      }
    }
    std::printf("B2 E=%d: %d balls, under 5 s %.2f%%, under 10 s %.2f%%\n", error, balls,
                100.0 * under5 / balls, 100.0 * under10 / balls);
    std::printf("  last touch before a drain within 5 s / 10 s:\n");
    for (std::size_t t = 0; t < kTouchCount; ++t) {
      if (lastBy10[t] > 0) std::printf("    %-14s %4d %4d\n", touchName(static_cast<Touch>(t)), lastBy5[t], lastBy10[t]);
    }
    std::printf("  TAXI by letter T/A/X/I: %d/%d/%d/%d\n", standup10[0], standup10[1], standup10[2], standup10[3]);
    std::printf("  stuck %llu, searches %u\n", (unsigned long long)h.stuck, h.game().ballSearchCount());
    for (const Vec2 p : h.stuckAt) std::printf("    stuck at (%.0f, %.0f)\n", p.x, p.y);
    return 0;
  }
  if (mode == "rebound") {
    // Inlane feeds and drop feeds, every press step, flippers idle after the
    // press: balls turned back just below the Briefing mouth, and of those the
    // ones that then left between the flippers without touching one.
    int bounced = 0, gap = 0, outl = 0, saved = 0;
    for (int side = 0; side < 2; ++side) {
      for (int feed = 0; feed < 3; ++feed) {
        for (int k = 95; k < 150; ++k) {
          for (int jit = 0; jit < 8; ++jit) {
            TableHarness h;
            h.setLocks(false);
            Vec2 p0 = feed == 0 ? Vec2{107.0F, 690.0F} : feed == 1 ? Vec2{107.0F, 672.0F} : Vec2{112.0F, 600.0F};
            p0.x += static_cast<float>(jit) * 0.3F;
            const Vec2 v0 = feed == 1 ? Vec2{0.0F, 60.0F + 6.0F * static_cast<float>(jit)} : Vec2{0.0F, 0.0F};
            if (side == 1) p0 = table::mirrored(p0);
            for (int i = 0; i < 5; ++i) h.advance();
            h.put(0, p0, side == 1 ? Vec2{-v0.x, v0.y} : v0, true);
            bool hitGate = false;
            std::uint64_t touchesAt = 0;
            float lastVy = 0;
            Touch end = Touch::None;
            for (int i = 0; i < kHz * 6; ++i) {
              const bool press = i >= k && i < k + 30;
              h.left = side == 0 && press;
              h.right = side == 1 && press;
              const Touch drain = h.advance();
              const Vec2 p = h.position(0);
              const Vec2 v = h.velocity(0);
              if (!hitGate && i > k && p.y > 430.0F && p.y < 446.0F && p.x > 316.0F && p.x < 396.0F &&
                  v.y > 0.0F && lastVy < 0.0F) {
                hitGate = true;
                touchesAt = h.flipperTouches;
              }
              lastVy = v.y;
              if (drain != Touch::None) { end = drain; break; }
              if (hitGate && h.flipperTouches > touchesAt) { end = Touch::Flipper; break; }
            }
            if (hitGate) {
              ++bounced;
              if (end == Touch::DrainCentre) ++gap;
              else if (end == Touch::OutlaneLeft || end == Touch::OutlaneRight) ++outl;
              else if (end == Touch::Flipper) ++saved;
            }
          }
        }
      }
    }
    std::printf("door rebounds: %d; then a flipper first %d, down the centre untouched %d (%.1f%%), an outlane %d\n",
                bounced, saved, gap, 100.0 * gap / std::max(1, bounced), outl);
    return 0;
  }
  if (mode == "trace") {
    // CSV: side,k,first,x,y for inlane-feed flips; path until the first touch after the press.
    for (int side = 0; side < 2; ++side) {
      for (int k = 112; k < 146; ++k) {
        TableHarness h;
        h.setLocks(false);
        Vec2 p0{107.0F, 690.0F};
        if (side == 1) p0 = table::mirrored(p0);
        for (int i = 0; i < 5; ++i) h.advance();
        h.put(0, p0, {}, true);
        h.events.clear();
        std::vector<Vec2> path;
        Touch first = Touch::None;
        for (int i = 0; i < kHz * 2 && first == Touch::None; ++i) {
          const bool press = i >= k && i < k + 30;
          h.left = side == 0 && press;
          h.right = side == 1 && press;
          const std::size_t before = h.events.size();
          const Touch drain = h.advance();
          if (i >= k) path.push_back(h.position(0));
          for (const TouchEvent& e : std::span(h.events).subspan(before)) {
            if (i >= k && e.touch != Touch::InlaneLeft && e.touch != Touch::InlaneRight && e.touch != Touch::Flaps) { first = e.touch; break; }
          }
          if (drain != Touch::None && first == Touch::None) first = drain;
        }
        for (std::size_t j = 0; j < path.size(); j += 2) {
          std::printf("%d,%d,%s,%.1f,%.1f\n", side, k, touchName(first), path[j].x, path[j].y);
        }
      }
    }
    return 0;
  }
  if (mode == "spray") {
    // Inlane feed (as the sweep's), press at step k: where the ball crosses y 440 going up.
    for (int side = 0; side < 2; ++side) {
      for (int k = 95; k < 150; ++k) {
        TableHarness h;
        h.setLocks(false);
        Vec2 p0{107.0F, 690.0F};
        if (side == 1) p0 = table::mirrored(p0);
        for (int i = 0; i < 5; ++i) h.advance();
        h.put(0, p0, {}, true);
        h.events.clear();
        float cross = -1;
        float vx = 0, vy = 0;
        for (int i = 0; i < kHz * 3; ++i) {
          const bool press = i >= k && i < k + 30;
          h.left = side == 0 && press;
          h.right = side == 1 && press;
          const Vec2 before = h.position(0);
          h.advance();
          const Vec2 after = h.position(0);
          if (i >= k && before.y >= 440.0F && after.y < 440.0F && cross < 0) {
            cross = after.x;
            vx = h.velocity(0).x;
            vy = h.velocity(0).y;
          }
        }
        Touch first = Touch::None;
        for (const TouchEvent& e : h.events) {
          if (e.step > static_cast<std::uint64_t>(k + 5) && e.touch != Touch::InlaneLeft && e.touch != Touch::InlaneRight && e.touch != Touch::Flaps) { first = e.touch; break; }
        }
        std::printf("%c k=%2d x@440=%6.1f v=(%6.0f,%6.0f) first=%s\n", side ? 'R' : 'L', k, cross, vx, vy, touchName(first));
      }
    }
    return 0;
  }
  if (mode == "deckhold") {
    TableHarness h;
    h.put(0, table::kDeckSetDown, table::kDeckSetDownVelocity, true);
    h.right = true;
    for (int i = 0; i < kHz * 6; ++i) {
      h.advance();
      if (i % 30 == 0) {
        std::printf("t=%.2f p=(%.1f,%.1f) v=(%.1f,%.1f) held=%d flip=%.2f\n", i / 120.0,
                    static_cast<double>(h.position(0).x), static_cast<double>(h.position(0).y),
                    static_cast<double>(h.velocity(0).x), static_cast<double>(h.velocity(0).y),
                    h.held(0) ? 1 : 0, static_cast<double>(h.game().flipper(2).angle));
      }
    }
    return 0;
  }
  if (mode == "deck") {
    for (int v = 0; v < 5; ++v) {
      const std::string fan = acceptance::deckFan(v);
      std::printf("deck v%d: %s  widest A %d C %d E %d K %d\n", v, fan.c_str(), widest(fan, 'A'),
                  widest(fan, 'C'), widest(fan, 'E'), widest(fan, 'K'));
    }
    return 0;
  }
  if (mode == "fan") {
    std::printf("L cradle: %s\nR cradle: %s\n", fanL.c_str(), fanR.c_str());
    std::printf("widest windows from the cradles (L/R): Briefing %d/%d, Runway %d/%d, Skyway %d/%d, "
                "Turbine %d/%d, doors %d/%d, Hangar %d/%d, circuits L %d/%d R %d/%d\n",
                widest(fanL, 'S'), widest(fanR, 'S'), widest(fanL, 'L'), widest(fanR, 'L'),
                widest(fanL, 'R'), widest(fanR, 'R'), widest(fanL, 'P'), widest(fanR, 'P'),
                widest(fanL, 'D'), widest(fanR, 'D'), widest(fanL, 'H'), widest(fanR, 'H'),
                widest(fanL, 'O'), widest(fanR, 'O'), widest(fanL, 'Q'), widest(fanR, 'Q'));
    const auto all = [](const std::string& f, char c) { return std::ranges::count(f, c); };
    std::printf("all steps from the cradles (L/R): Briefing %ld/%ld, Runway %ld/%ld, Skyway %ld/%ld, Turbine %ld/%ld, doors %ld/%ld\n",
                all(fanL, 'S'), all(fanR, 'S'), all(fanL, 'L'), all(fanR, 'L'), all(fanL, 'R'), all(fanR, 'R'),
                all(fanL, 'P'), all(fanR, 'P'), all(fanL, 'D'), all(fanR, 'D'));
    return 0;
  }
  const std::string kind = argc > 2 ? argv[2] : "p2";
  const int error = argc > 3 ? std::atoi(argv[3]) : 5;
  const int games = argc > 4 ? std::atoi(argv[4]) : 20;
  const auto seed = static_cast<std::uint64_t>(argc > 5 ? std::atoll(argv[5]) : 1);
  const std::string target = argc > 6 ? argv[6] : "briefing";
  std::array<int, 2> aim{aimFor(fanL, 'S'), aimFor(fanR, 'S')};
  if (target == "ramps") aim = {aimFor(fanL, 'R'), aimFor(fanR, 'L')};
  // A side that cannot make the target at all just plays the ball on (a
  // firm shot from the cradle) rather than holding it for ever.
  for (int& a : aim) {
    if (a < 0) a = 60;
  }
  if (kind == "hunt") std::printf("aim steps L %d R %d (%s)\n", aim[0], aim[1], target.c_str());
  const Stats st = playGames(kind, error, games, seed, aim);
  const std::string label = kind + " E=" + std::to_string(error) + " " +
                            (kind == "hunt" ? target : std::string{});
  report(label.c_str(), st);
  return 0;
}
