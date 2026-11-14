# Rules

The table is new (two circuits, the Runway and Skyway ramps, the Hangar behind its three bay doors, the Briefing room, the Turbine lane and its spinner, three roll call lanes over three thruster bumpers, a kickback in the left outlane), and its own rule set, with courses, two multiballs, combos and ranks, is still to come. Until it does the game plays under provisional rules: everything the ball can do pays a flat value, and the table's moving parts are worked just enough for a game to use them.

Three balls per game.

| Feature | Award |
|---|---|
| Thruster bumper | 1,000 |
| Slingshot | 250 |
| Spinner, each turn | 500 |
| Turbo target (the two in the nest) | 2,500 |
| Bay door (the three across the Hangar) | 2,000 the first time, 500 once it is marked |
| All three bay doors marked | 10,000, and the doors drop |
| Roll call lane | 3,000 |
| Return lane (inlane or outlane) | 1,000 |
| Briefing room | 5,000 |
| Kickback fired | 5,000 |
| Runway, Skyway | 6,000 |
| Hangar | 10,000 |

Provisional, and replaced by the table's own rules:

- Bay doors and lock. A door that is hit is marked; marks carry over between balls. When all three are marked the doors drop together and the lock is lit (`LOCK IS LIT`). The first two balls into the Hangar are locked in its bays, in view, and each time a new ball is served to the plunger. The third ball in lets all three go, one after another, and the doors come back up. Locked balls stay locked when a ball drains; the bays are emptied when the game is over.
- Spinner. A ball through the Turbine lane spins it: one turn for every 150 px/s of its speed, at least one and at most twelve, counted one at a time.
- Kickback. Lit at the start of a game. A ball that falls into the left outlane while it is lit is carried back over the divider to the left inlane, and the kickback goes out. Rolling over all four return lanes lights it again.
- A ball that drains within 2.5 seconds of being returned by the Briefing room or the Hangar is given back (`BALL SAVED`).

The rank shown is Rookie throughout: rank will be earned by passing courses.

A ball that drains within 6 seconds of entering play is returned free, once per ball. The score stops at 999,999,999,999.

## Nudge and tilt

X bumps the table from the left and sends the ball right, `.` does the opposite, and Up bumps from the bottom and sends the ball up the table. The drawn table jolts and settles.

Each nudge adds tilt heat, which cools over 2.5 seconds. Two quick nudges show a warning. Three inside about a second tilt the table: the flippers and plunger go dead, nothing scores, the bumpers, slingshots, targets, lanes and holes neither light nor sound (the ball still bounces off them and the holes still return it) and no ball save applies, until the ball drains. The next ball starts clean.

The rules live in `src/core/Rules` with no dependency on physics, and are unit tested event by event.
