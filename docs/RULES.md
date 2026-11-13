# Rules

Three balls per game. Everything a table feature awards is multiplied by overdrive, the table multiplier raised at the overdrive bank. Course awards, and the jackpot when a course collects it, are paid as stated. At most five extra balls are held at once.

| Feature | Award |
|---|---|
| Meteor bumpers | 500, rising to 1,000, 1,500 and 2,000 as they are charged |
| Thruster bumpers, beacon bumper | 1,500 and 1,000 |
| Slingshots | 500 |
| Any target | 750 |
| Roll call lanes | 5,000 each and the lane lights. All three lit: 10,000 and the meteor bumpers charge. Flipper buttons rotate the lit lanes |
| Turbo bank (right wall) | 5,000 per completion. Then in turn: turbo online, jackpot lit, outlane bonus lit, extra ball |
| Merit bank (centre) | 10,000, then 50,000, then 100,000 and an extra ball |
| Overdrive bank (upper left) | 5,000 per completion and overdrive steps 2x, 3x, 5x, 10x. At 10x a completion still pays 5,000 and the display reads OVERDRIVE AT MAXIMUM. Resets each ball |
| Jump gates | 2,000 and the ball leaves the next jump gate |
| Singularity | 20,000 |
| Quasar chute | 10,000, then the 500,000 jackpot if lit (else 20,000), then 30,000, then an extra ball (30,000 instead when five are already held) |
| Runway ramp | 5,000 |
| Combo shot | 25,000 for repeating the runway or the chute within 5 seconds |
| Return lanes | Inlane 5,000. Outlane 20,000 when the outlane bonus is lit |
| End of ball | 250 per bumper or target hit that ball (1,000,000 at most), times overdrive |

## Nudge and tilt

X bumps the table from the left and sends the ball right, `.` does the opposite, and Up bumps from the bottom and sends the ball up the table. The drawn table jolts and settles.

Each nudge adds tilt heat, which cools over 2.5 seconds. Two quick nudges show a warning. Three inside about a second tilt the table: the flippers and plunger go dead, nothing scores, the bumpers, slingshots, targets, lanes and holes neither light nor sound (the ball still bounces off them and the holes still return it), the end-of-ball bonus is lost and no ball save applies, until the ball drains. The next ball starts clean.

## Courses and ranks

The table is a flight academy: its missions are courses, and passing them raises your rank.

1. Hit a course target on the left wall to select a course: top, middle or bottom picks one of the rank's three courses. Lighting all three selects the rank's special course, and it stays selected: the three lights stay on until the runway accepts it, and further hits on the course targets do not swap it for an ordinary course. The selection is announced, with its sound, only when it changes: hitting the target of the course already selected says nothing. The course targets are ignored while a course is in session, and the hit that passes a course does not select the next one.
2. Shoot the runway ramp to accept it (CLASS IN SESSION). The energy gauge fills and the course targets' lights go out: until the course ends they score without lighting, so the special course always needs all three hit afresh. The runway is a timed shot: a firm one off the right flipper, or a falling ball caught late by the left one.
3. Do what the display asks. Some courses have two parts.
4. Energy lasts 90 seconds and burns only while a ball is in play. The runway recharges it. If it runs out the course is failed. On a tilted table the display keeps saying TILT, so the failure is reported with the message that follows the drain.
5. Passing a course pays its award (500,000 at Rookie up to 10,000,000 for the last special course), plus the jackpot if lit, and fills the rank gauge: 6 of 18 for a normal course, 9 for a special. A full gauge is a rank up.

Ranks: Rookie, Trainee, Wingmate, Pilot, Navigator, Ace, Instructor, Flight Dean, Chancellor. Each rank has its own four courses with larger counts and awards. An ordinary course is named by subject and number, the rank times 100 plus 1, 2 or 3 for the top, middle or bottom target (BUMPER BASICS 101 up to SYSTEMS MASTERY 903); a special course is an exam, from POP QUIZ: TARGETS to GRADUATION DAY. Courses carry over from ball to ball. At game over the course lights go out: a selection still waiting for the runway, or a course still in session, is dropped, and the rank stays on show until the next game. The course table is in `src/core/Missions.cpp`, where the code calls a course a mission.

In short: select with the course targets, accept on the runway, finish before the energy gauge runs down, and watch the rank gauge fill.

A ball that drains within 6 seconds of entering play is returned free, once per ball. The rules live in `src/core/Rules` with no dependency on physics, and are unit tested event by event.

Awards do not time out, and there is no replay score.
