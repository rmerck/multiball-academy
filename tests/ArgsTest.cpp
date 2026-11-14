#include <string>

#include "Check.hpp"
#include "Suites.hpp"
#include "core/Args.hpp"
#include "core/Game.hpp"
#include "core/Rules.hpp"
#include "core/Scenes.hpp"

void testArgs() {
  using pinball::parseFrameLimit;

  CHECK(parseFrameLimit("1") == 1U);
  CHECK(parseFrameLimit("120") == 120U);
  CHECK(parseFrameLimit("1000000") == pinball::kMaxFrameLimit);
  // Leading zeros are plain decimal and harmless.
  CHECK(parseFrameLimit("007") == 7U);
  CHECK(parseFrameLimit(std::string(64, '0') + "9") == 9U);

  CHECK(!parseFrameLimit(""));
  CHECK(!parseFrameLimit("0"));
  CHECK(!parseFrameLimit("1000001"));
  CHECK(!parseFrameLimit("-5"));
  CHECK(!parseFrameLimit("+5"));
  CHECK(!parseFrameLimit(" 5"));
  CHECK(!parseFrameLimit("5 "));
  CHECK(!parseFrameLimit("5x"));
  CHECK(!parseFrameLimit("0x10"));
  CHECK(!parseFrameLimit("1e3"));
  CHECK(!parseFrameLimit("99999999999999999999"));

  // Embedded NUL is trailing junk, not a terminator.
  CHECK(!parseFrameLimit(std::string("5\0", 2)));

  // Scenes by name: exactly the names listed, nothing like them.
  for (const std::string_view name : pinball::kSceneNames) {
    const auto scene = pinball::parseScene(name);
    CHECK(scene.has_value() && pinball::sceneName(*scene) == name);
  }
  CHECK(!pinball::parseScene(""));
  CHECK(!pinball::parseScene("Multiball"));
  CHECK(!pinball::parseScene("multiball "));
  CHECK(!pinball::parseScene(std::string("table\0", 6)));
  CHECK(pinball::sceneName(static_cast<pinball::Scene>(99)).empty());

  // Each scene is reached, shows what it is for, and comes out the same
  // every time.
  using pinball::Scene;
  for (const std::string_view name : pinball::kSceneNames) {
    const Scene scene = *pinball::parseScene(name);
    pinball::Game game;
    pinball::Game again;
    CHECK(pinball::stageScene(game, scene));
    CHECK(pinball::stageScene(again, scene));
    const pinball::Rules& rules = game.rules();
    CHECK(rules.score() == again.rules().score() && game.stepCount() == again.stepCount());
    for (std::size_t i = 0; i < game.ballCount(); ++i) {
      CHECK(game.balls().items[i].position.x == again.balls().items[i].position.x &&
            game.balls().items[i].position.y == again.balls().items[i].position.y);
    }
    CHECK(!rules.gameOver() && rules.ball() == 1);
    switch (scene) {
      case Scene::Table:
        CHECK(rules.score() == 0 && game.ballCount() == 1);
        break;
      case Scene::Multiball:
        CHECK(game.ballCount() == 3 && rules.multiball() == pinball::Multiball::Academy);
        CHECK(rules.multiballJackpotLit());
        break;
      case Scene::Hangar:
        CHECK(game.lockedBalls() == 2 && !game.doorBankUp() && rules.lockLit());
        break;
      case Scene::Course:
        CHECK(rules.courseRunning() && rules.courseSecondsLeft() > 0.0);
        CHECK(rules.goalRemaining() > 0);
        break;
      case Scene::Graduation:
        CHECK(rules.graduationRunning() && rules.coursesPassed() == 6);
        CHECK(game.ballCount() == 3);
        break;
    }
  }
}
