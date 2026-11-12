#include <cmath>
#include <cstdint>
#include <string_view>

#include "Check.hpp"
#include "Suites.hpp"
#include "core/Game.hpp"
#include "core/Rules.hpp"
#include "core/TableLayout.hpp"

namespace {

using pinball::Game;
using pinball::InputState;
using pinball::Rules;
namespace table = pinball::table;

constexpr double kStep = 1.0 / 120.0;

void wait(Rules& rules, double seconds) {
  for (int i = 0; i < static_cast<int>(seconds / kStep) + 2; ++i) {
    rules.tick();
  }
}

// Launches the ball with a full draw and steps until it is in play.
void launch(Game& game) {
  InputState input;
  input.plunger = true;
  for (int i = 0; i < 150; ++i) {
    game.step(input);
  }
  input.plunger = false;
  for (int i = 0; i < 600 && !game.laneGateClosed(); ++i) {
    game.step(input);
  }
}

}  // namespace

void testNudge() {
  {  // Tilt heat: two quick nudges warn, the third tilts; spaced out they do not.
    Rules rules(kStep);
    rules.onBallInPlay();
    CHECK(rules.onNudge());
    CHECK(!rules.tilted());
    CHECK(rules.onNudge());
    CHECK(rules.message() == std::string_view("DANGER  TILT WARNING"));
    CHECK(rules.tiltWarning() > 0.6);
    CHECK(!rules.onNudge());
    CHECK(rules.tilted());
    CHECK(rules.message() == std::string_view("TILT"));
    // Further nudges do nothing more.
    CHECK(!rules.onNudge());
    CHECK(rules.tiltWarning() <= 1.0);
  }
  {
    Rules rules(kStep);
    rules.onBallInPlay();
    for (int i = 0; i < 20; ++i) {
      CHECK(rules.onNudge());
      wait(rules, Rules::kTiltCoolSeconds);
    }
    CHECK(!rules.tilted());
    CHECK(rules.tiltWarning() < 0.05);
  }

  {  // While tilted nothing scores, the end-of-ball bonus is lost, no ball
     // save applies, and the next ball starts clean.
    Rules rules(kStep);
    rules.onBumper(0);
    const std::uint64_t before = rules.score();
    CHECK(rules.bonus() > 0);
    rules.onBallInPlay();
    rules.onNudge();
    rules.onNudge();
    rules.onNudge();
    CHECK(rules.tilted());
    CHECK(!rules.ballSaveActive());
    rules.onBumper(0);
    rules.onTarget(0);
    rules.onRollover(0);
    rules.onHole(0);
    rules.onSlingshot(0);
    CHECK(rules.score() == before);
    wait(rules, 10.0);
    CHECK(rules.tilted());
    CHECK(rules.message() == std::string_view("TILT"));  // Stays up.
    CHECK(rules.onDrain() == Rules::DrainResult::NextBall);
    CHECK(rules.score() == before);  // Bonus forfeited.
    CHECK(!rules.tilted());
    CHECK(rules.tiltWarning() == 0.0);
    rules.onBumper(0);
    CHECK(rules.score() > before);
  }

  {  // In the game: each nudge key moves the ball the documented way and
     // shifts the drawn table, which then settles back.
    struct Case {
      bool InputState::* key;
      float wantX;  // Sign of the expected change in x; 0 for no fixed direction.
    };
    const Case cases[] = {
        {&InputState::nudgeLeft, 1.0F},
        {&InputState::nudgeRight, -1.0F},
        {&InputState::nudgeUp, 0.0F},
    };
    for (const Case& c : cases) {
      Game nudged;
      Game control;
      launch(nudged);
      launch(control);
      InputState press;
      press.*(c.key) = true;
      nudged.step(press);
      control.step(InputState{});
      CHECK(std::fabs(nudged.shake().x) + std::fabs(nudged.shake().y) > 1.0F);
      // Compare with an identical game that was not nudged, a few steps on.
      for (int i = 0; i < 3; ++i) {
        nudged.step(InputState{});
        control.step(InputState{});
      }
      const float dx = nudged.ball().position.x - control.ball().position.x;
      const float dy = nudged.ball().position.y - control.ball().position.y;
      // The ball is riding the arch at this point, which redirects part of
      // any push, so only the side nudges have a dependable direction.
      if (c.wantX != 0.0F) {
        CHECK(dx * c.wantX > 1.0F);
      }
      CHECK(std::fabs(dx) + std::fabs(dy) > 0.5F);
      for (int i = 0; i < 120; ++i) {
        nudged.step(InputState{});
      }
      CHECK(nudged.shake().x == 0.0F && nudged.shake().y == 0.0F);
    }
  }

  {  // Holding a nudge key is one nudge, not one per step.
    Game game;
    launch(game);
    InputState hold;
    hold.nudgeUp = true;
    for (int i = 0; i < 240; ++i) {
      game.step(hold);
    }
    CHECK(!game.rules().tilted());
  }

  {  // Three quick nudges tilt: the flippers go dead until the ball drains,
     // then the next ball plays normally.
    Game game;
    launch(game);
    for (int i = 0; i < 3; ++i) {
      InputState press;
      press.nudgeLeft = true;
      game.step(press);
      game.step(InputState{});
    }
    CHECK(game.rules().tilted());

    InputState flip;
    flip.leftFlipper = true;
    flip.rightFlipper = true;
    const std::uint32_t drainsBefore = game.drainCount();
    bool flipperMoved = false;
    for (int i = 0; i < 120 * 60 && game.drainCount() == drainsBefore; ++i) {
      game.step(flip);
      if (std::fabs(game.flipper(table::kLeftFlipperIndex).angle -
                    table::kLeftFlipper.restAngle) > 0.01F) {
        flipperMoved = true;
      }
    }
    CHECK(!flipperMoved);
    CHECK(game.drainCount() == drainsBefore + 1);
    CHECK(!game.rules().tilted());
    CHECK(game.rules().ball() == 2);

    for (int i = 0; i < 30; ++i) {
      game.step(flip);
    }
    CHECK(std::fabs(game.flipper(table::kLeftFlipperIndex).angle -
                    table::kLeftFlipper.activeAngle) < 0.01F);
  }

  {  // A ball waiting on the plunger cannot be nudged or tilted, so the table
     // can never be locked with a dead plunger and a ball that cannot drain.
    Rules rules(kStep);
    for (int i = 0; i < 10; ++i) {
      CHECK(!rules.onNudge());
    }
    CHECK(!rules.tilted());
    CHECK(rules.tiltWarning() == 0.0);

    Game game;
    for (int i = 0; i < 10; ++i) {
      InputState press;
      press.nudgeLeft = true;
      game.step(press);
      game.step(InputState{});
    }
    CHECK(!game.rules().tilted());
    launch(game);
    CHECK(game.laneGateClosed());
  }

  {  // A tilt never outlives its ball, and a tilted ball is not saved: tilt
     // inside the ball-save window, drain, and the next ball is clean.
    Rules rules(kStep);
    rules.onBallInPlay();
    CHECK(rules.ballSaveActive());
    rules.onNudge();
    rules.onNudge();
    rules.onNudge();
    CHECK(rules.tilted());
    CHECK(rules.onDrain() == Rules::DrainResult::NextBall);
    CHECK(!rules.tilted());
    CHECK(rules.ball() == 2);
  }

  {  // While tilted the display keeps saying so.
    Rules rules(kStep);
    rules.onBallInPlay();
    rules.onNudge();
    rules.onNudge();
    rules.onNudge();
    rules.onHole(0);
    wait(rules, 5.0);
    CHECK(rules.message() == std::string_view("TILT"));
  }

  {  // Nudging with the game over does nothing.
    Rules rules(kStep);
    for (int ball = 0; ball < Rules::kBallsPerGame; ++ball) {
      rules.onDrain();
    }
    CHECK(rules.gameOver());
    CHECK(!rules.onNudge());
    CHECK(!rules.tilted());
  }
}
