# Rules

The table is new (two circuits, the Runway and Skyway ramps, the Hangar, the Briefing room, the Turbine lane, three roll call lanes over three thruster bumpers), and its own rule set, with courses, two multiballs, combos and ranks, is still to come. Until it does the game plays under provisional rules: everything the ball can do pays a flat value, and nothing lights.

Three balls per game.

| Feature | Award |
|---|---|
| Thruster bumper | 1,000 |
| Slingshot | 250 |
| Turbo target (the two in the nest) | 2,500 |
| Bay door (the three across the Hangar) | 2,000 |
| Roll call lane | 3,000 |
| Return lane (inlane or outlane) | 1,000 |
| Briefing room | 5,000 |
| Runway, Skyway | 6,000 |
| Hangar | 10,000 |

The bay doors stay up for now, so the Hangar behind them cannot be entered; the spinner in the Turbine lane and the kickback in the left outlane are not there yet. The rank shown is Rookie throughout: rank will be earned by passing courses.

A ball that drains within 6 seconds of entering play is returned free, once per ball. The score stops at 999,999,999,999.

## Nudge and tilt

X bumps the table from the left and sends the ball right, `.` does the opposite, and Up bumps from the bottom and sends the ball up the table. The drawn table jolts and settles.

Each nudge adds tilt heat, which cools over 2.5 seconds. Two quick nudges show a warning. Three inside about a second tilt the table: the flippers and plunger go dead, nothing scores, the bumpers, slingshots, targets, lanes and holes neither light nor sound (the ball still bounces off them and the holes still return it) and no ball save applies, until the ball drains. The next ball starts clean.

The rules live in `src/core/Rules` with no dependency on physics, and are unit tested event by event.
