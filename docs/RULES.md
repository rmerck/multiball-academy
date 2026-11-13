# Rules

Three balls per game. Everything a table feature awards is multiplied by the field multiplier. Mission awards, and the jackpot when a mission collects it, are paid as stated. At most five extra balls are held at once.

| Feature | Award |
|---|---|
| Attack bumpers | 500, rising to 1,000, 1,500 and 2,000 as they are upgraded |
| Engine bumpers, satellite bumper | 1,500 and 1,000 |
| Slingshots | 500 |
| Any target | 750 |
| Re-entry lanes | 5,000 each and the lane lights. All three lit: 10,000 and the attack bumpers upgrade. Flipper buttons rotate the lit lanes |
| Booster bank (right wall) | 5,000 per completion. Then in turn: booster online, jackpot lit, bonus lanes lit, extra ball |
| Medal bank (centre) | 10,000, then 50,000, then 100,000 and an extra ball |
| Multiplier bank (upper left) | 5,000 per completion and the field multiplier steps 2x, 3x, 5x, 10x. At 10x a completion still pays 5,000 and the display reads FIELD MULTIPLIER AT MAXIMUM. Resets each ball |
| Wormholes | 2,000 and the ball leaves the next wormhole |
| Black hole | 20,000 |
| Hyperspace chute | 10,000, then the 500,000 jackpot if lit (else 20,000), then 30,000, then an extra ball (30,000 instead when five are already held) |
| Launch ramp | 5,000 |
| Reflex shot | 25,000 for repeating the ramp or the chute within 5 seconds |
| Return lanes | Inlane 5,000. Outlane 20,000 when the bonus lanes are lit |
| End of ball | 250 per bumper or target hit that ball (1,000,000 at most), times the multiplier |

## Nudge and tilt

X bumps the table from the left and sends the ball right, `.` does the opposite, and Up bumps from the bottom and sends the ball up the table. The drawn table jolts and settles.

Each nudge adds tilt heat, which cools over 2.5 seconds. Two quick nudges show a warning. Three inside about a second tilt the table: the flippers and plunger go dead, nothing scores, the bumpers, slingshots, targets, lanes and holes neither light nor sound (the ball still bounces off them and the holes still return it), the end-of-ball bonus is lost and no ball save applies, until the ball drains. The next ball starts clean.

## Missions and ranks

1. Hit a mission target on the left wall to select a mission: top, middle or bottom picks one of the rank's three missions. Lighting all three selects the rank's special mission, and it stays selected: the three lights stay on until the ramp accepts it, and further hits on the mission targets do not swap it for an ordinary mission. The selection is announced, with its sound, only when it changes: hitting the target of the mission already selected says nothing. The mission targets are ignored while a mission is active, and the hit that completes a mission does not select the next one.
2. Shoot the launch ramp to accept it. The fuel gauge fills and the mission targets' lights go out: until the mission ends they score without lighting, so the special mission always needs all three hit afresh. The ramp is a timed shot: a firm one off the right flipper, or a falling ball caught late by the left one.
3. Do what the display asks. Some missions have two stages.
4. Fuel lasts 90 seconds and burns only while a ball is in play. The ramp refuels. If it runs out the mission is aborted. On a tilted table the display keeps saying TILT, so the abort is reported with the message that follows the drain.
5. Completing a mission pays its award (500,000 at Cadet up to 10,000,000 for the last special mission), plus the jackpot if lit, and fills the rank gauge: 6 of 18 for a normal mission, 9 for a special. A full gauge is a promotion.

Ranks: Cadet, Ensign, Lieutenant, Captain, Lt Commander, Commander, Commodore, Admiral, Fleet Admiral. Each rank has its own four missions with larger counts and awards. Missions carry over from ball to ball. At game over the mission lights go out: a selection still waiting for the ramp, or a mission still running, is dropped, and the rank stays on show until the next game. The mission table is in `src/core/Missions.cpp`.

The mission structure follows the original (select with the mission targets, accept on the ramp, fuel, rank lights). The individual missions, their counts and awards are this project's own.

A ball that drains within 6 seconds of entering play is returned free, once per ball. The rules live in `src/core/Rules` with no dependency on physics, and are unit tested event by event.

Differences from the original are deliberate simplifications: awards do not time out, and there is no replay score.
