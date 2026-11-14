# Resuming a game

A game in progress is written to a small file when the application leaves
the foreground (iOS), is ended by the system, or is quit (the Mac's Quit,
any desktop quit). On the next launch the title menu offers CONTINUE, which
puts that game back on the table, paused, exactly as it was left.

Code: `src/core/Snapshot.*` (the records and the file format, no Box2D),
`src/core/GameSave.cpp` (Rules, Game and PhysicsWorld to and from the
records), `src/platform/SaveStore.*` (the file), `src/platform/AppLifecycle.cpp`
(when it is written and deleted). Tests: `tests/SaveGameTest.cpp`,
`testSavedGame` in `tests/AppEventTest.cpp`, branches `7` and `8` of
`tests/fuzz/FuzzParsers.cpp`.

## For the title menu

```cpp
bool App::hasSavedGame() const;   // show CONTINUE above PLAY
bool App::resumeSavedGame();      // CONTINUE: the saved game, paused; false if it cannot be
void App::discardSavedGame();     // PLAY, if PLAY does not itself start a new game
```

Nothing else is needed. Starting a new game (F2, NEW GAME, or simply playing
the table's own game) and finishing a game delete the save by themselves.

## What is saved, and the choice made

Box2D cannot write its world out, so the save is an explicit snapshot,
taken between two steps:

- **Rules**: every field that carries the game on: score and the score of
  each feature, ball number, tilt heat, ball saver and grace clocks, extra
  balls, skill shot, lanes and targets lit, kickback, turbo meter and goal,
  doors marked, lock state and bays, multiball and jackpot ladder, combos,
  overdrive, circuits under way, courses passed and running (clock, elapsed,
  progress, NAVIGATION's shot), Graduation Day, the bonus counts and
  multiplier, the last bonus tally, the requests in flight, and the rules'
  random number generator. Timers are kept in steps.
- **Game**: step count, counters, the last release step of each hole, the
  Hangar's burst, door raise pending, the spinner (owed, cooldown, phase,
  direction), the queue of balls asked for, auto-launch state, the game's
  random number generator.
- **Table**: flipper angles, plunger pull and release speed, bay doors up or
  down, kickback armed.
- **Each ball slot**: the Box2D body exactly as Box2D holds it (position in
  metres, linear velocity, rotation as cosine and sine, angular velocity),
  whether a hole or the kickback holds it and which, the per-ball latches
  (bumpers being touched, rollovers being crossed, holes armed, the last
  sensor position), and what Game keeps (holder, hold time, arrival order,
  lock bay, releasing, eject step, ball-search anchor and count, in play,
  where and when a hole took it, where it was let out from (for drawing it
  going in and out), auto-launch).

Not kept: the message showing and its queue, sounds of the step, flashes and
the table's shake (all of the moment), and the controls (released: nothing
is held when the game comes back).

**Balls are restored exactly, mid-flight included.** The alternatives were
to save at ball granularity or to return the live balls to the plunger.
Both are unfair one way or the other: a ball rolling into the drain would be
saved by backgrounding the application (a gift the player could use at
will), and a ball on its way to a jackpot would be lost. Exact restoration
gives neither. What makes it safe:

- Every slot's body is set where it was, with its velocity, rotation and
  spin; a held ball is put back disabled, as `holdBall` leaves it, so it is
  in no contact at all.
- The latches are restored with it, so a ball sitting on a rollover or
  against a bumper does not score it again.
- **The one safe-state rule:** a new Box2D world does not know the contacts
  the old one had, and reports every touch already under way as a new one on
  its first slice. The first slice (one quarter of a step, about 2 ms) after
  a restore therefore reports no touches and gives no slingshot kick. Its
  cost: a ball that genuinely meets a target, door or slingshot in those
  2 ms is not scored for it (at most one hit). Rollovers, bumpers, holes,
  sensors and the kickback do not depend on Box2D's touches and are not
  affected.
- A file is checked in full before anything of it is used (below), and
  `Game::restore` refuses a snapshot that is not `save::valid`.

Determinism: nothing in normal play changes. The only new branch in a step
is the skipped slice, which only a restore arms; the golden soak trajectory
in `tests/HoleTest.cpp` is unchanged. A restored game is deterministic (the
same save on two tables gives identical games, tested for 60 s), and a game
saved while every ball is held goes on bit for bit as its original, every
record equal, until the two physics worlds part by a rounding (Box2D's broad
phase has a history a new world cannot share, so two contacts can be met in
another order); measured at three seconds or more, the test asks for two.
That nothing but the bodies differs at the moment they part shows that no
state the game depends on is missing from the save.

## When the file exists

The file is there only while the game it holds is not in memory, so there
is never a second copy of a game to go back to:

| Event | Action |
| --- | --- |
| will / did enter background, terminating, quit (`App::finish`) | write, if a game is in progress (once per state) |
| did enter foreground | delete the file this run wrote: the game is in memory again |
| CONTINUE (`resumeSavedGame`) | delete first; resume only if this call deleted it (not if another copy of the game, such as a second window on a Mac, took it first) |
| a new game begun (F2, NEW GAME, the table's own game played) | delete |
| game over | delete; a game that is over is never written |
| launch, file damaged or from another build or table | delete, offer nothing |

A game is "in progress" once a ball of it has been in play and until it is
over (`Game::inProgress`). A table that has only been shown is not saved.
Timed runs, staged scenes (`--scene`) and runs with `keepScores` off never
read, write or delete the file.

High scores: a resumed game is the same game, so it enters the table once,
when it ends, with the score it earned. A save cannot be resumed twice (it
is deleted before it is restored), it is not left behind a game that goes
on in memory, and it cannot be taken from a game that is over. The checksum
guards against damage, not against a person editing their own files: that
person can edit the high score file directly, which is plain text, so
nothing is gained by signing the save.

## The file

`savedgame.bin` in the SDL preferences directory, beside `highscores.txt`.
Written to a uniquely named owner-only temporary file, flushed and renamed
into place (`replaceFile`, as for the scores); a crash leaves the old file
or the new one. The flush is a plain `fsync` (`Durability::System`): the
file must outlive the application being ended, not a power cut, and on
Apple systems `F_FULLFSYNC`, which the score file keeps, can take far
longer than the moment before suspension. Little-endian:

| Bytes | Field |
| --- | --- |
| 8 | `MBA-SAVE` |
| 2 | format version (`save::kFormatVersion`, now 2) |
| 1 + n | build id: version and build number, `2.0.0+1` |
| 32 | layout fingerprint: SHA-256 of the geometry a saved ball depends on (walls, flippers, bumpers, targets, doors, rollovers, holes and their exits, bays, sensors, kickback, plunger) |
| 14 | the count of each kind of element (balls, bumpers, slingshots, targets, doors, rollovers, holes, sensors, bays, flippers, courses, shots, features, ranks) |
| 4 | payload size |
| 1117 | the records, fixed size |
| 32 | SHA-256 of everything before it |

1,217 bytes in all for build `2.0.0+1`. Everything is keyed to element ids
of `core/TableLayout.hpp` (a hole is its index in `kHoles`, a bay its index
in `kLockBays`), never to a shape or a body.

Reading treats the file as untrusted. It reads at most 8 KiB, and only a
file of exactly the expected size goes further; nothing is allocated from a
size in the file. The header is compared whole with the one this build
writes, so another magic, version, build, layout or element count is
refused. Then the checksum, then every field against its range (integers,
enums and indices strictly; floats must be finite and within range: places
on the table, speeds under the physics' cap, a unit rotation, flipper angles
between their stops; a ball's launch wait, stillness count and hold no
longer than play reaches: 60 steps, 480 steps, two minutes), then the records against each other (at least one
ball and never more than three with those waiting, one ball per bay, only
the Hangar holds a locked ball, a held ball names a hole or the kickback,
empty slots carry nothing, remembered steps not in the future). Anything
wrong: the file is deleted and nothing is offered.

**Bump `kFormatVersion` whenever a record changes**, and add the field to
the records whenever Game, Rules or PhysicsWorld gains state: the
round-trip tests compare what the table shows, so a field left out fails
them. A change of the table's
geometry needs nothing: the fingerprint changes with it and old saves are
refused. A new build refuses an old build's save by its build id, so an
update from the App Store starts fresh.

## Timing

Measured on the Linux test machine under the sanitizers: snapshot and encode
0.12 to 0.23 ms; going to the background with a game in progress, file
written and flushed, about 7 ms. iOS gives an application about five seconds
after it enters the background.
