#include <cstddef>
#include <cstdint>
#include <string_view>

#include "Check.hpp"
#include "Suites.hpp"
#include "core/Missions.hpp"
#include "core/Rules.hpp"
#include "core/TableLayout.hpp"

namespace {

using pinball::Goal;
using pinball::goalBit;
using pinball::MissionControl;
using pinball::Rules;
namespace table = pinball::table;
namespace points = pinball::points;

constexpr double kStep = 1.0 / 120.0;

// Plays a selected mission through to the end by feeding exactly the events
// it asks for. Returns the outcome of the completing event.
MissionControl::Outcome playThrough(MissionControl& control) {
  MissionControl::Outcome last;
  for (int guard = 0; guard < 500 && control.phase() == MissionControl::Phase::Active; ++guard) {
    const pinball::MissionStage* stage = control.stage();
    last = stage->goal == Goal::Ramp ? control.onRamp() : control.onEvent(goalBit(stage->goal));
  }
  return last;
}

std::size_t missionTarget(std::size_t which) {
  std::size_t seen = 0;
  for (std::size_t i = 0; i < table::kTargets.size(); ++i) {
    if (table::kTargets[i].bank == table::TargetBank::Mission) {
      if (seen == which) {
        return i;
      }
      ++seen;
    }
  }
  return 0;
}

std::size_t hole(table::HoleType type) {
  for (std::size_t i = 0; i < table::kHoles.size(); ++i) {
    if (table::kHoles[i].type == type) {
      return i;
    }
  }
  return 0;
}

}  // namespace

void testMissions() {
  {  // The hit that completes a mission does not also select the next one,
     // so the completion is what the display shows.
    Rules rules(kStep);
    for (std::size_t i = 0; i < 3; ++i) {
      rules.onTarget(missionTarget(i));
    }
    // Science Mission: nine targets of any kind.
    CHECK(rules.missions().missionIndex() == pinball::kSpecialMission);
    rules.onHole(hole(table::HoleType::Ramp));
    CHECK(rules.missions().phase() == MissionControl::Phase::Active);
    CHECK(rules.missions().stage()->goal == Goal::AnyTarget);
    while (rules.missions().remaining() > 1) {
      rules.onTarget(missionTarget(0));
    }
    rules.takeSounds();
    rules.onTarget(missionTarget(1));
    CHECK(rules.missions().completedMissions() == 1);
    CHECK(rules.missions().phase() == MissionControl::Phase::Idle);
    CHECK(rules.message() == std::string_view("MISSION COMPLETE"));
    const pinball::SoundSet sounds = rules.takeSounds();
    CHECK((sounds & pinball::soundBit(pinball::Sound::MissionComplete)) != 0U);
    CHECK((sounds & pinball::soundBit(pinball::Sound::MissionSelect)) == 0U);
    // The next mission target selects as usual.
    rules.onTarget(missionTarget(2));
    CHECK(rules.missions().phase() == MissionControl::Phase::Selected);
  }

  {  // The special mission stays selected until the ramp accepts it: the
     // three lights stay on, and further mission-target hits, drains and
     // waits change nothing. It is then played as the special.
    Rules rules(kStep);
    rules.onBallInPlay();
    for (std::size_t i = 0; i < 3; ++i) {
      rules.onTarget(missionTarget(i));
    }
    const pinball::Mission* special = &pinball::missionAt(0, pinball::kSpecialMission);
    CHECK(rules.missions().mission() == special);
    for (std::size_t round = 0; round < 2; ++round) {
      for (std::size_t i = 0; i < 3; ++i) {
        const std::uint64_t before = rules.score();
        rules.onTarget(missionTarget((i + 1) % 3));
        CHECK(rules.score() == before + points::kTarget);  // No bank award.
        CHECK(rules.missions().phase() == MissionControl::Phase::Selected);
        CHECK(rules.missions().mission() == special);
        for (std::size_t lit = 0; lit < 3; ++lit) {
          CHECK(rules.targetLit(missionTarget(lit)));
        }
      }
      // Across a lost ball too.
      for (int i = 0; i < 1200; ++i) {
        rules.tick();
      }
      rules.onDrain();
      rules.onBallInPlay();
    }
    CHECK(rules.missions().mission() == special);
    rules.onHole(hole(table::HoleType::Ramp));
    CHECK(rules.missions().phase() == MissionControl::Phase::Active);
    CHECK(rules.missions().mission() == special);
    for (std::size_t i = 0; i < 3; ++i) {
      CHECK(!rules.targetLit(missionTarget(i)));
    }
    while (rules.missions().phase() == MissionControl::Phase::Active) {
      rules.onTarget(missionTarget(0));
    }
    CHECK(rules.missions().completedMissions() == 1);
    CHECK(rules.missions().progress() == 9);
  }

  {  // Fuel that runs out behind the tilt message is reported when the tilt
     // ends, with the drain, and only once.
    for (int extraBall = 0; extraBall < 2; ++extraBall) {
      Rules rules(kStep);
      rules.onBallInPlay();
      if (extraBall == 1) {
        // Four medal banks: two commendations, then an extra ball.
        for (int bank = 0; bank < 3; ++bank) {
          for (std::size_t i = 0; i < table::kTargets.size(); ++i) {
            if (table::kTargets[i].bank == table::TargetBank::Medal) {
              rules.onTarget(i);
            }
          }
        }
        CHECK(rules.extraBalls() == 1);
      }
      rules.onTarget(missionTarget(0));
      rules.onHole(hole(table::HoleType::Ramp));
      CHECK(rules.missions().phase() == MissionControl::Phase::Active);
      rules.onNudge();
      rules.onNudge();
      rules.onNudge();
      CHECK(rules.tilted());
      for (int i = 0; i < static_cast<int>(MissionControl::kFuelSeconds / kStep) + 2; ++i) {
        rules.tick();
      }
      CHECK(rules.missions().phase() == MissionControl::Phase::Idle);
      CHECK(rules.message() == std::string_view("TILT"));
      rules.onDrain();
      CHECK(rules.message() == std::string_view(extraBall == 1 ? "SHOOT AGAIN  MISSION ABORTED"
                                                               : "NEXT BALL  MISSION ABORTED"));
      rules.onBallInPlay();
      for (int i = 0; i < 1200; ++i) {
        rules.tick();
      }
      rules.onDrain();
      CHECK(rules.message() == std::string_view("NEXT BALL"));
    }
    // Seen at the time, it is not repeated at the drain.
    Rules rules(kStep);
    rules.onBallInPlay();
    rules.onTarget(missionTarget(0));
    rules.onHole(hole(table::HoleType::Ramp));
    for (int i = 0; i < static_cast<int>(MissionControl::kFuelSeconds / kStep) + 2; ++i) {
      rules.tick();
    }
    CHECK(rules.message() == std::string_view("OUT OF FUEL  MISSION ABORTED"));
    rules.onNudge();
    rules.onNudge();
    rules.onNudge();
    CHECK(rules.tilted());
    rules.onDrain();
    CHECK(rules.message() == std::string_view("NEXT BALL"));
    // The fail-safe return ends a tilt as well and carries the news.
    Rules returned(kStep);
    returned.onBallInPlay();
    returned.onTarget(missionTarget(0));
    returned.onHole(hole(table::HoleType::Ramp));
    returned.onNudge();
    returned.onNudge();
    returned.onNudge();
    for (int i = 0; i < static_cast<int>(MissionControl::kFuelSeconds / kStep) + 2; ++i) {
      returned.tick();
    }
    returned.onBallReturned();
    CHECK(returned.message() == std::string_view("RE-DEPLOY  MISSION ABORTED"));
  }

  {  // The mission bank cannot be loaded during a mission. Accepting one
     // puts its lights out and they stay out until the mission is over, so
     // the next special mission needs all three targets again.
    Rules rules(kStep);
    rules.onBallInPlay();
    rules.onTarget(missionTarget(0));
    rules.onTarget(missionTarget(1));
    CHECK(rules.targetLit(missionTarget(0)) && rules.targetLit(missionTarget(1)));
    CHECK(rules.missions().missionIndex() == 1);
    rules.onHole(hole(table::HoleType::Ramp));
    CHECK(rules.missions().phase() == MissionControl::Phase::Active);
    for (std::size_t i = 0; i < 3; ++i) {
      CHECK(!rules.targetLit(missionTarget(i)));
    }
    // Mission targets still score during the mission but do not light.
    const std::uint64_t before = rules.score();
    rules.onTarget(missionTarget(0));
    rules.onTarget(missionTarget(2));
    CHECK(rules.score() == before + 2 * points::kTarget);
    for (std::size_t i = 0; i < 3; ++i) {
      CHECK(!rules.targetLit(missionTarget(i)));
    }
    // Other banks light as usual.
    for (std::size_t i = 0; i < table::kTargets.size(); ++i) {
      if (table::kTargets[i].bank == table::TargetBank::Medal) {
        rules.onTarget(i);
        CHECK(rules.targetLit(i));
        break;
      }
    }
    // Out of fuel: the mission ends with the bank still empty.
    for (int i = 0; i < static_cast<int>(MissionControl::kFuelSeconds / kStep) + 2; ++i) {
      rules.tick();
    }
    CHECK(rules.missions().phase() == MissionControl::Phase::Idle);
    // One target now selects its own mission, not the special.
    rules.onTarget(missionTarget(1));
    CHECK(rules.missions().phase() == MissionControl::Phase::Selected);
    CHECK(rules.missions().missionIndex() == 1);
    CHECK(rules.targetLit(missionTarget(1)) && !rules.targetLit(missionTarget(0)));
    rules.onTarget(missionTarget(0));
    CHECK(rules.missions().missionIndex() == 0);
    rules.onTarget(missionTarget(2));
    CHECK(rules.missions().missionIndex() == pinball::kSpecialMission);
  }

  {  // A tilt or a drain in the same step as a mission announcement still
     // gets its own message.
    Rules rules(kStep);
    rules.onBallInPlay();
    rules.onTarget(missionTarget(0));
    CHECK(rules.message() == std::string_view("MISSION SELECTED  SHOOT THE RAMP"));
    rules.onNudge();
    rules.onNudge();
    rules.onTarget(missionTarget(1));
    rules.onNudge();
    CHECK(rules.tilted());
    CHECK(rules.message() == std::string_view("TILT"));

    Rules last(kStep);
    for (int ball = 1; ball < Rules::kBallsPerGame; ++ball) {
      last.onDrain();
    }
    last.onTarget(missionTarget(0));
    CHECK(last.onDrain() == Rules::DrainResult::GameOver);
    CHECK(last.message() == std::string_view("GAME OVER   PRESS F2"));
    for (int i = 0; i < 120 * 10; ++i) {
      last.tick();
    }
    CHECK(last.message() == std::string_view("GAME OVER   PRESS F2"));
  }

  {  // The mission table is well formed: every mission has a name that fits
     // the display, a first stage, a positive award, and awards never fall
     // as rank rises.
    std::uint64_t previousNormal = 0;
    for (std::size_t rank = 0; rank < pinball::kRankCount; ++rank) {
      CHECK(!pinball::rankName(rank).empty());
      for (std::size_t m = 0; m < pinball::kMissionsPerRank; ++m) {
        const pinball::Mission& mission = pinball::missionAt(rank, m);
        CHECK(!mission.name.empty() && mission.name.size() <= 22);
        CHECK(mission.stages[0].count > 0);
        CHECK(mission.award > 0);
        CHECK(mission.progress > 0 && mission.progress <= pinball::kProgressPerRank);
        for (const pinball::MissionStage& stage : mission.stages) {
          CHECK(static_cast<std::size_t>(stage.goal) < pinball::kGoalCount);
          CHECK(!pinball::goalText(stage.goal).empty());
        }
      }
      CHECK(pinball::missionAt(rank, 0).award >= previousNormal);
      CHECK(pinball::missionAt(rank, pinball::kSpecialMission).award >
            pinball::missionAt(rank, 0).award);
      previousNormal = pinball::missionAt(rank, 0).award;
    }
    // Lookups clamp instead of reading out of range.
    CHECK(pinball::rankName(999) == pinball::rankName(pinball::kRankCount - 1));
    CHECK(&pinball::missionAt(999, 999) ==
          &pinball::missionAt(pinball::kRankCount - 1, pinball::kMissionsPerRank - 1));
  }

  {  // Select, accept, progress, complete.
    MissionControl control(kStep);
    CHECK(control.phase() == MissionControl::Phase::Idle);
    CHECK(control.mission() == nullptr && control.stage() == nullptr);
    CHECK(control.fuelLights() == 0);

    // Nothing counts before a mission is active.
    CHECK(!control.onEvent(goalBit(Goal::AttackBumpers)).completed);
    CHECK(!control.onRamp().accepted);

    CHECK(control.onMissionTarget(0, false).selected);
    CHECK(control.phase() == MissionControl::Phase::Selected);
    CHECK(control.mission() == &pinball::missionAt(0, 0));
    // Changing your mind before accepting is allowed.
    CHECK(control.onMissionTarget(2, false).selected);
    CHECK(control.mission() == &pinball::missionAt(0, 2));
    CHECK(control.onMissionTarget(0, false).selected);
    CHECK(!control.onMissionTarget(7, false).selected);

    CHECK(control.onRamp().accepted);
    CHECK(control.phase() == MissionControl::Phase::Active);
    CHECK(control.fuelLights() == MissionControl::kFuelLights);
    const int needed = pinball::missionAt(0, 0).stages[0].count;
    CHECK(control.remaining() == needed);

    // Once active, the mission targets no longer change the mission.
    CHECK(!control.onMissionTarget(1, false).selected);
    // Unrelated events do nothing.
    CHECK(!control.onEvent(goalBit(Goal::Wormholes)).completed);
    CHECK(control.remaining() == needed);

    for (int i = 0; i < needed - 1; ++i) {
      CHECK(!control.onEvent(goalBit(Goal::AttackBumpers) | goalBit(Goal::AnyBumper)).completed);
    }
    const MissionControl::Outcome done = control.onEvent(goalBit(Goal::AttackBumpers));
    CHECK(done.completed && !done.promoted);
    CHECK(done.award == pinball::missionAt(0, 0).award);
    CHECK(control.phase() == MissionControl::Phase::Idle);
    CHECK(control.progress() == pinball::missionAt(0, 0).progress);
    CHECK(control.completedMissions() == 1);
  }

  {  // A selected special mission is not traded down by a single target.
    MissionControl control(kStep);
    CHECK(control.onMissionTarget(2, true).selected);
    CHECK(!control.onMissionTarget(1, false).selected);
    CHECK(control.phase() == MissionControl::Phase::Selected);
    CHECK(control.missionIndex() == pinball::kSpecialMission);
    CHECK(control.onMissionTarget(1, true).selected);
    CHECK(control.missionIndex() == pinball::kSpecialMission);
    // An ordinary selection can still be changed, up to the special too.
    MissionControl other(kStep);
    other.onMissionTarget(0, false);
    CHECK(other.onMissionTarget(2, false).selected);
    CHECK(other.missionIndex() == 2);
  }

  {  // The special mission, and a two-stage mission.
    MissionControl control(kStep);
    CHECK(control.onMissionTarget(1, true).selected);
    CHECK(control.mission() == &pinball::missionAt(0, pinball::kSpecialMission));

    // Find a two-stage mission and check the stage hand-over.
    bool checked = false;
    for (std::size_t rank = 0; rank < pinball::kRankCount && !checked; ++rank) {
      for (std::size_t m = 0; m < 3 && !checked; ++m) {
        if (pinball::missionAt(rank, m).stages[1].count == 0 || rank != 1) {
          continue;
        }
        MissionControl c(kStep);
        // Reach Ensign by completing three Cadet missions.
        for (int i = 0; i < 3; ++i) {
          c.onMissionTarget(0, false);
          c.onRamp();
          playThrough(c);
        }
        CHECK(c.rank() == 1);
        c.onMissionTarget(m, false);
        c.onRamp();
        const pinball::Mission& mission = pinball::missionAt(1, m);
        MissionControl::Outcome o;
        for (int i = 0; i < mission.stages[0].count; ++i) {
          o = mission.stages[0].goal == Goal::Ramp ? c.onRamp()
                                                   : c.onEvent(goalBit(mission.stages[0].goal));
        }
        CHECK(o.stageCompleted && !o.completed);
        CHECK(c.stage() == &mission.stages[1]);
        CHECK(c.remaining() == mission.stages[1].count);
        checked = true;
      }
    }
    CHECK(checked);
  }

  {  // Fuel: burns down light by light, the ramp refills it, and running dry
     // aborts the mission with no award and no progress.
    MissionControl control(kStep);
    control.onMissionTarget(1, false);
    control.onRamp();
    const int stepsPerLight =
        static_cast<int>(MissionControl::kFuelSeconds / kStep) / MissionControl::kFuelLights;
    int previous = control.fuelLights();
    for (int i = 0; i < stepsPerLight * 3 + 5; ++i) {
      CHECK(!control.tick().aborted);
      CHECK(control.fuelLights() <= previous);
      previous = control.fuelLights();
    }
    CHECK(control.fuelLights() == 3);

    const MissionControl::Outcome refuel = control.onRamp();
    CHECK(refuel.refueled && !refuel.accepted);
    CHECK(control.fuelLights() == MissionControl::kFuelLights);

    bool aborted = false;
    for (int i = 0; i < static_cast<int>(MissionControl::kFuelSeconds / kStep) + 5; ++i) {
      if (control.tick().aborted) {
        aborted = true;
        break;
      }
      CHECK(control.fuelLights() >= 1);
    }
    CHECK(aborted);
    CHECK(control.phase() == MissionControl::Phase::Idle);
    CHECK(control.progress() == 0 && control.completedMissions() == 0);
    CHECK(control.fuelLights() == 0);
    CHECK(!control.tick().aborted);
  }

  {  // A whole career: play every rank's missions to Fleet Admiral. Each
     // rank takes three normal missions; the top rank does not promote.
    MissionControl control(kStep);
    int promotions = 0;
    for (int played = 0; played < 200 && control.rank() + 1 < pinball::kRankCount; ++played) {
      control.onMissionTarget(static_cast<std::size_t>(played % 3), false);
      CHECK(control.onRamp().accepted);
      const MissionControl::Outcome o = playThrough(control);
      CHECK(o.completed);
      promotions += o.promoted ? 1 : 0;
      CHECK(control.progress() >= 0 && control.progress() < pinball::kProgressPerRank);
    }
    CHECK(control.rank() == pinball::kRankCount - 1);
    CHECK(promotions == static_cast<int>(pinball::kRankCount) - 1);
    CHECK(control.completedMissions() == 3U * (pinball::kRankCount - 1));

    for (int i = 0; i < 10; ++i) {
      control.onMissionTarget(0, true);
      control.onRamp();
      CHECK(!playThrough(control).promoted);
    }
    CHECK(control.rank() == pinball::kRankCount - 1);
    CHECK(control.progress() == pinball::kProgressPerRank);

    control.reset();
    CHECK(control.rank() == 0 && control.progress() == 0);
  }

  {  // Through the rules: a mission target selects, the ramp accepts, attack
     // bumpers complete it, and the award (plus a lit jackpot) is paid as
     // stated, not multiplied.
    Rules rules(kStep);
    const std::size_t ramp = hole(table::HoleType::Ramp);
    rules.onTarget(missionTarget(0));
    CHECK(rules.missions().phase() == MissionControl::Phase::Selected);
    CHECK(rules.message() == std::string_view("MISSION SELECTED  SHOOT THE RAMP"));
    rules.onHole(ramp);
    CHECK(rules.missions().phase() == MissionControl::Phase::Active);
    CHECK(rules.message() == std::string_view("MISSION ACCEPTED"));

    std::size_t attack = 0;
    for (std::size_t i = 0; i < table::kBumpers.size(); ++i) {
      if (table::kBumpers[i].group == table::BumperGroup::Attack) {
        attack = i;
        break;
      }
    }
    const int needed = rules.missions().remaining();
    for (int i = 0; i < needed - 1; ++i) {
      rules.onBumper(attack);
    }
    const std::uint64_t before = rules.score();
    rules.onBumper(attack);
    CHECK(rules.missions().phase() == MissionControl::Phase::Idle);
    CHECK(rules.score() == before + points::kAttackBumper[0] + pinball::missionAt(0, 0).award);
    CHECK(rules.message() == std::string_view("MISSION COMPLETE"));

    // Lighting all three mission targets selects the special mission.
    for (std::size_t t = 0; t < 3; ++t) {
      rules.onTarget(missionTarget(t));
    }
    CHECK(rules.missions().mission() == &pinball::missionAt(0, pinball::kSpecialMission));
    CHECK(rules.targetLit(missionTarget(0)));

    // A new game returns to Cadet with nothing selected.
    rules.startGame();
    CHECK(rules.missions().rank() == 0);
    CHECK(rules.missions().phase() == MissionControl::Phase::Idle);
  }

  {  // Fuel through the rules: it burns only while a ball is in play, so a
     // mission survives any wait on the plunger, and runs dry in play.
    Rules rules(kStep);
    const int fullTank = static_cast<int>(MissionControl::kFuelSeconds / kStep) + 5;
    rules.onBallInPlay();
    rules.onTarget(missionTarget(2));
    rules.onHole(hole(table::HoleType::Ramp));
    for (int i = 0; i < 600; ++i) {
      rules.tick();
    }
    const int lightsBefore = rules.missions().fuelLights();
    rules.onDrain();
    for (int i = 0; i < fullTank * 2; ++i) {
      rules.tick();
    }
    CHECK(rules.missions().phase() == MissionControl::Phase::Active);
    CHECK(rules.missions().fuelLights() == lightsBefore);

    rules.onBallInPlay();
    for (int i = 0; i < fullTank && rules.missions().phase() == MissionControl::Phase::Active;
         ++i) {
      rules.tick();
    }
    CHECK(rules.missions().phase() == MissionControl::Phase::Idle);
    CHECK(rules.message() == std::string_view("OUT OF FUEL  MISSION ABORTED"));
  }

  {  // The mission announcement wins over a lesser message from the same
     // hit: completing the Science Mission on the target that also completes
     // a bank still says the mission is complete.
    Rules rules(kStep);
    rules.onBallInPlay();
    for (std::size_t t = 0; t < 3; ++t) {
      rules.onTarget(missionTarget(t));  // Selects the special mission.
    }
    rules.onHole(hole(table::HoleType::Ramp));
    CHECK(rules.missions().mission() == &pinball::missionAt(0, pinball::kSpecialMission));
    // Nine target hits, the last of which also completes the multiplier bank.
    std::size_t multiplier[2] = {0, 0};
    std::size_t found = 0;
    for (std::size_t i = 0; i < table::kTargets.size() && found < 2; ++i) {
      if (table::kTargets[i].bank == table::TargetBank::Multiplier) {
        multiplier[found++] = i;
      }
    }
    for (int i = 0; i < 8; ++i) {
      rules.onTarget(multiplier[0]);
    }
    CHECK(rules.missions().remaining() == 1);
    rules.onTarget(multiplier[1]);
    CHECK(rules.missions().phase() == MissionControl::Phase::Idle);
    CHECK(rules.fieldMultiplier() == 2);
    CHECK(rules.message() == std::string_view("MISSION COMPLETE"));
    // The lock lasts only for that step.
    rules.tick();
    rules.onHole(hole(table::HoleType::BlackHole));
    CHECK(rules.message() == std::string_view("BLACK HOLE"));
  }
}
