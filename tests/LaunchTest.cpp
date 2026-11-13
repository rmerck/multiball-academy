#include <cstddef>

#include "Check.hpp"
#include "Suites.hpp"
#include "bots/Acceptance.hpp"
#include "bots/TableHarness.hpp"
#include "core/TableLayout.hpp"

// The launch, as part of every build: a reduced sweep of the plunger's
// range with a strict first touch, slow balls down the circuit lanes, the
// automatic launch and the one-way arch. The full sweeps are in the
// acceptance suite (tests/AcceptanceMain.cpp); this is the part of them
// quick enough to run every time, and any failure here is a fairness
// failure of the table.
namespace {

using namespace pinball::bots;
using namespace pinball::bots::acceptance;
namespace table = pinball::table;

int count(const TouchCounts& counts, Touch touch) {
  return counts[static_cast<std::size_t>(touch)];
}

}  // namespace

void testLaunch() {
  {  // B1: no launch drains. Whatever the plunger does, the first thing the
     // ball meets is a flipper, an inlane or a roll call lane; a ball that
     // meets a slingshot, a bumper or an outlane first could be lost with
     // nothing the player did. Every eleventh hold, two start positions.
    const LaunchResult launch = launchSweep(11, 2);
    CHECK(launch.launches == 24);
    CHECK(launch.unsafe == 0);
    // Only the very shortest press fails to leave the lane.
    CHECK(launch.backOnPlunger <= 2);
    // Both kinds of launch happen: into the roll call lanes, and right
    // round the arch to the left inlane.
    CHECK(count(launch.first, Touch::RollCall) > 0);
    CHECK(count(launch.first, Touch::InlaneLeft) > 0);
    CHECK(count(launch.first, Touch::Slingshot) == 0 && count(launch.first, Touch::Bumper) == 0);
    CHECK(count(launch.first, Touch::OutlaneLeft) == 0 &&
          count(launch.first, Touch::OutlaneRight) == 0 &&
          count(launch.first, Touch::DrainCentre) == 0);
    // The one-way arch lets the ball out without throwing it.
    CHECK(launch.largestKick < 400.0F);
  }

  {  // A ball coming slowly down a circuit lane, as after a weak launch or a
     // failed circuit, does not leave between the flippers untouched.
    const DescentResult descents = slowDescents(24);
    CHECK(descents.descents == 30);
    CHECK(descents.throughGapUntouched == 0);
    CHECK(count(descents.first, Touch::OutlaneLeft) == 0 &&
          count(descents.first, Touch::OutlaneRight) == 0);
  }

  {  // A ball launched without the player, at the speed the game uses, laps
     // the arch to the left inlane every time; slower, it need not.
    const AutoLaunchRow chosen = autoLaunchAt(static_cast<int>(table::kAutoLaunchSpeed), 6);
    CHECK(chosen.launches == 6 && chosen.leftInlane == 6);
    const AutoLaunchRow tooSlow = autoLaunchAt(1200, 2);
    CHECK(tooSlow.backOnPlunger == 2);
    // The game's own: asked for, served, launched once, safe.
    const GameAutoLaunchResult served = gameAutoLaunch(1);
    CHECK(served.launches == 1 && served.safeFirstTouch == 1 && served.leftInlane == 1);
    CHECK(served.relaunches == 0);
  }

  {  // The one-way arch from the field side: balls fired straight at it, at
     // every part of it and from slow to fast, stay on the field.
    const OneWayResult arch = oneWayArch(48);
    CHECK(arch.fired == 48 && arch.crossed == 0);
  }
}
