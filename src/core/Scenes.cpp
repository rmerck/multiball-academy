#include "core/Scenes.hpp"

#include <algorithm>
#include <cstddef>

#include "core/Courses.hpp"
#include "core/Game.hpp"
#include "core/Input.hpp"
#include "core/Rules.hpp"
#include "core/TableLayout.hpp"

namespace pinball {

// Game's friend: puts balls where the shots would, and tells the rules what
// the shots did, through their own events.
struct SceneStage {
  Game& game;

  Rules& rules() noexcept { return game.rules_; }

  void run(int steps) {
    for (int i = 0; i < steps; ++i) {
      game.step(InputState{});
    }
  }
  // Steps until done() or the limit; returns done().
  template <typename Done>
  bool runUntil(int limit, Done done) {
    for (int i = 0; i < limit && !done(); ++i) {
      game.step(InputState{});
    }
    return done();
  }

  // The ball the player has, put where a shot sends it, moving.
  void send(std::size_t slot, Vec2 position, Vec2 velocity) {
    game.physics_.resetBall(slot, position, velocity);
    game.balls_[slot].inPlay = true;
    game.enterPlay();
  }

  // The three bay doors hit, and the bank down.
  bool openDoors() {
    if (!runUntil(600, [this] { return game.doorBankUp() && !game.doorRaisePending(); })) {
      return false;
    }
    for (std::size_t door = 0; door < table::kDropTargets.size(); ++door) {
      rules().onDropTarget(door);
    }
    return runUntil(10, [this] { return !game.doorBankUp(); });
  }

  // The player's ball shot into the open Hangar and kept there.
  bool lockOne() {
    const std::size_t locked = game.lockedBalls();
    send(game.ball().id, kHangarShot, kHangarShotSpeed);
    return runUntil(240, [this, locked] { return game.lockedBalls() == locked + 1; });
  }

  // The steps a scoop shot makes, without the ball: taken and returned.
  void briefing() {
    rules().onHole(table::kScoopHole, game.ball().id);
    run(1);
    rules().onEject(table::kScoopHole);
  }
  void hangarEntry() {
    // Never a lock while a course or the exam asks for the Hangar here; if
    // it were, the bays are full: the ball is returned.
    if (rules().onHole(table::kHangarHole, game.ball().id) == Rules::HoleResult::Lock) {
      rules().onLockRefused();
    }
    run(1);
    rules().onEject(table::kHangarHole);
  }
  void circuit(bool right) {
    const std::size_t ball = game.ball().id;
    rules().onSensor(right ? 1 : 0, 1, 1200.0F, ball);
    rules().onSensor(2, right ? -1 : 1, 900.0F, ball);
  }
  void shoot(Shot shot) {
    switch (shot) {
      case Shot::CircuitLeft:
        circuit(false);
        break;
      case Shot::Runway:
        rules().onHole(table::kRunwayHole, game.ball().id);
        break;
      case Shot::Hangar:
        hangarEntry();
        break;
      case Shot::Briefing:
        briefing();
        break;
      case Shot::Turbine:
        rules().onSensor(table::kSpinnerSensor, 1, 800.0F, game.ball().id);
        break;
      case Shot::Skyway:
        rules().onHole(table::kSkywayHole, game.ball().id);
        break;
      case Shot::CircuitRight:
        circuit(true);
        break;
    }
    run(1);
  }

  // Lights the Briefing room with ramps if it is not, moves the blinking
  // course with the thrusters to this one, and starts it.
  bool startCourse(CourseId id) {
    const auto index = static_cast<std::size_t>(id);
    for (int i = 0; i < 4 && !rules().briefingLit(); ++i) {
      shoot(Shot::Runway);
    }
    for (int i = 0; i < 6 && rules().courseOffered() != index; ++i) {
      rules().onBumper(static_cast<std::size_t>(i) % table::kBumpers.size());
    }
    if (!rules().briefingLit() || rules().courseOffered() != index) {
      return false;
    }
    shoot(Shot::Briefing);
    return rules().course() == index;
  }

  // What the running course asks for, until it is passed.
  bool passCourse() {
    const std::size_t course = rules().course();
    for (int i = 0; i < 64 && rules().course() == course; ++i) {
      switch (static_cast<CourseId>(course)) {
        case CourseId::BumperBasics:
          rules().onBumper(static_cast<std::size_t>(i) % table::kBumpers.size());
          run(1);
          break;
        case CourseId::RunwayDrills:
          shoot(i % 2 == 0 ? Shot::Runway : Shot::Skyway);
          break;
        case CourseId::CircuitTraining:
          shoot(i % 2 == 0 ? Shot::CircuitLeft : Shot::CircuitRight);
          break;
        case CourseId::HangarSafety:
          if (rules().goalText() == kGoalEnterHangar) {
            if (!runUntil(10, [this] { return !game.doorBankUp(); })) {
              return false;
            }
            hangarEntry();
          } else {
            for (std::size_t door = 0; door < table::kDropTargets.size(); ++door) {
              rules().onDropTarget(door);
            }
            run(1);
          }
          break;
        case CourseId::TurbineTheory:
          rules().onSpin();
          run(1);
          break;
        case CourseId::Navigation:
          shoot(rules().navigationShot());
          break;
      }
    }
    return !rules().courseRunning() && rules().coursePassed(course);
  }

  // Waits for the extra balls asked for to be served and launched.
  bool ballsOnTable(std::size_t count) {
    return runUntil(1200, [this, count] {
      return rules().ballsLive() == static_cast<int>(count) && game.lockedBalls() == 0 &&
             game.queuedBalls() == 0 && game.ballCount() == count;
    });
  }

  bool stage(Scene scene) {
    switch (scene) {
      case Scene::Table:
        return true;
      case Scene::Hangar:
        return hangar();
      case Scene::Multiball:
        if (!hangar()) {
          return false;
        }
        send(game.ball().id, kHangarShot, kHangarShotSpeed);
        // The third ball starts it: all three leave the Hangar and spread out.
        if (!ballsOnTable(3)) {
          return false;
        }
        run(120);
        return rules().multiball() == Multiball::Academy && rules().multiballJackpotLit() &&
               game.ballCount() == 3;
      case Scene::Course: {
        if (!startCourse(CourseId::RunwayDrills)) {
          return false;
        }
        shoot(Shot::Skyway);
        // The ball is fed to the left flipper, as the Briefing room does.
        send(game.ball().id, table::kHoles[table::kScoopHole].exitPosition, {-60.0F, 40.0F});
        run(60);
        return rules().courseRunning() && rules().goalRemaining() > 0;
      }
      case Scene::Graduation: {
        for (std::size_t course = 0; course < kCourseCount; ++course) {
          if (!startCourse(static_cast<CourseId>(course)) || !passCourse()) {
            return false;
          }
          run(2);
        }
        if (!rules().graduationLit()) {
          return false;
        }
        // The ball comes up to the Briefing room, which starts it, and every
        // ball there can be is launched.
        send(game.ball().id, table::kHoles[table::kScoopHole].exitPosition, {-60.0F, 40.0F});
        run(2);
        shoot(Shot::Briefing);
        return rules().graduationRunning() && ballsOnTable(3);
      }
    }
    return false;
  }

  // Two balls locked and the doors down again, the third ball on the plunger.
  bool hangar() {
    if (!openDoors() || !lockOne()) {
      return false;
    }
    // The first multiball of a game keeps the Hangar open after one lock.
    if (game.doorBankUp() && !openDoors()) {
      return false;
    }
    if (!lockOne()) {
      return false;
    }
    if (!openDoors()) {
      return false;
    }
    run(30);
    return game.lockedBalls() == 2 && rules().lockLit() && !game.doorBankUp();
  }

  // Straight up the Hangar's mouth, from below the doors.
  static constexpr Vec2 kHangarShot{279.0F, 440.0F};
  static constexpr Vec2 kHangarShotSpeed{0.0F, -900.0F};
};

bool stageScene(Game& game, Scene scene) {
  SceneStage stage{game};
  return stage.stage(scene);
}

}  // namespace pinball
