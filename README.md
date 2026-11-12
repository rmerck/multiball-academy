# Space Cadet Pinball (C++ recreation)

A 2D recreation of the Space Cadet pinball table in C++17, SDL3 and Box2D, with original art made in GIMP.

This is an independent fan project. It contains no code or assets from the original game and is not affiliated with Microsoft, Maxis or Cinematronics.

## Status

| Phase | Feature | State |
|---|---|---|
| 1 | Project skeleton, window, fixed-step loop, CI | Done |
| 2 | Asset loading and rendering | Done: verified loader, table art and sprites generated in GIMP from the table geometry |
| 3 | Physics foundation | Done: Box2D world, table shell, ball, drain |
| 4 | Flippers | Done |
| 5 | Plunger and launch lane | Done |
| 6 | Bumpers, targets, ramps, table geometry | Done: 8 bumpers, slingshots, 4 target banks, 7 rollovers, wormholes, black hole, hyperspace, launch ramp |
| 7 | Scoring, lights, UI | Done: rules engine, lit lanes and targets, information panel with a built-in pixel font |
| 8 | Missions and ranks | Done: 9 ranks, 36 missions, fuel |
| 9 | Nudge and tilt | Done |
| 10 | Audio | Done: synthesized sound effects, no audio files. No music |
| 11 | High scores, polish, performance | Done: high score table, About screen, auto-pause, idle wait while paused |
| 12 | Final playability pass | Planned |

## Build (macOS)

```sh
brew install cmake sdl3 box2d
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/pinball
```

Options:

- `-DPINBALL_SANITIZE=ON` builds with AddressSanitizer and UBSan.
- `-DPINBALL_WERROR=ON` treats warnings as errors. CI uses this.

## Controls

Keys are read by physical position (scancode), so they sit where Z and / are on a US keyboard whatever the layout. The function keys and Escape are matched by name. Escape or F1 closes the About screen.

| Key | Action | State |
|---|---|---|
| Z | Left flipper | Done |
| / | Right flipper | Done |
| Space | Plunger, hold for power | Done |
| X | Nudge left | Done |
| . | Nudge right | Done |
| Up | Nudge from bottom | Done |
| F2 | New game | Done |
| F1 | About and high scores | Done |
| F3 | Pause. The game also pauses by itself when its window loses focus or is minimised | Done |
| F4 | Fullscreen | Done |
| M | Mute | Done |

## Design priorities

1. Secure software practices: RAII ownership with no owning raw pointers, standard-library hardening, validated input, no network access, sanitizer builds in CI.
2. Performance: fixed 120 Hz simulation step, rendering at display refresh rate with a manual pacing fallback, 60 FPS minimum target.
3. Faithfulness to the original feel.

## Rules

Three balls per game. Everything a feature awards is multiplied by the field multiplier.

| Feature | Award |
|---|---|
| Attack bumpers | 500, rising to 1,000, 1,500 and 2,000 as they are upgraded |
| Engine bumpers, satellite bumper | 1,500 and 1,000 |
| Slingshots | 500 |
| Any target | 750 |
| Re-entry lanes | 5,000 each and the lane lights. All three lit: 10,000 and the attack bumpers upgrade. Flipper buttons rotate the lit lanes |
| Booster bank (right wall) | 5,000 per completion. Then in turn: booster online, jackpot lit, bonus lanes lit, extra ball |
| Medal bank (centre) | 10,000, then 50,000, then 100,000 and an extra ball |
| Multiplier bank (upper left) | Field multiplier steps 2x, 3x, 5x, 10x. Resets each ball |
| Wormholes | 2,000 and the ball leaves the next wormhole |
| Black hole | 20,000 |
| Hyperspace chute | 10,000, then the 500,000 jackpot if lit (else 20,000), then 30,000, then an extra ball |
| Launch ramp | 5,000 |
| Reflex shot | 25,000 for repeating the ramp or the chute within 5 seconds |
| Return lanes | Inlane 5,000. Outlane 20,000 when the bonus lanes are lit |
| End of ball | 250 per bumper or target hit that ball, times the multiplier |

### Nudge and tilt

X bumps the table from the left and sends the ball right, `.` does the opposite, and Up bumps from the bottom and sends the ball up the table. The drawn table jolts and settles.

Each nudge adds tilt heat, which cools over 2.5 seconds. Two quick nudges show a warning. Three inside about a second tilt the table: the flippers and plunger go dead, nothing scores, the end-of-ball bonus is lost and no ball save applies, until the ball drains. The next ball starts clean.

### Missions and ranks

1. Hit a mission target on the left wall to select a mission: top, middle or bottom picks one of the rank's three missions. Lighting all three selects the rank's special mission.
2. Shoot the launch ramp to accept it. The fuel gauge fills.
3. Do what the display asks. Some missions have two stages.
4. Fuel lasts 90 seconds. The ramp refuels. If it runs out the mission is aborted.
5. Completing a mission pays its award (500,000 at Cadet up to 10,000,000 for the last special mission), plus the jackpot if lit, and fills the rank gauge: 6 of 18 for a normal mission, 9 for a special. A full gauge is a promotion.

Ranks: Cadet, Ensign, Lieutenant, Captain, Lt Commander, Commander, Commodore, Admiral, Fleet Admiral. Each rank has its own four missions with larger counts and awards. Missions carry over from ball to ball. The mission table is in `src/core/Missions.cpp`.

The mission structure follows the original (select with the mission targets, accept on the ramp, fuel, rank lights). The individual missions, their counts and awards are this project's own.

A ball that drains within 6 seconds of entering play is returned free. The rules live in `src/core/Rules` with no dependency on physics, and are unit tested event by event.

Differences from the original are deliberate simplifications: awards do not time out, and there is no replay score.

## High scores

The five best games are kept in a small text file in the per-user preferences directory (on macOS, `~/Library/Application Support/space-cadet-pinball/Space Cadet Pinball/highscores.txt`), with the score and the rank reached. This is the only file the game ever writes.

- It is written to a temporary file and renamed into place, so a crash cannot leave half a table.
- On load it must be a regular file of at most 512 bytes and every line is validated strictly (digits only, in range, in order). Anything else is ignored and the table starts empty; it is never partly trusted.

## Sound

Every sound is synthesized at run time from sine, square and triangle oscillators and noise (`src/core/Sound`), so the game ships no audio files and has nothing extra to verify. Twenty effects cover flippers, launch, bumpers, targets, lanes, holes, drains, awards, missions, tilt and game over.

The mixer is plain arithmetic with no device access and is unit tested: output is always finite and within [-1, 1], every sound ends, and a flood of triggers cannot overrun the 24-voice pool. The platform layer pulls samples on the main thread and queues about 60 ms to the default device, so no locking is needed. If no audio device opens, the game runs silent.

There is no music.

## Physics notes

- Box2D 3.2 runs in metres at 100 playfield pixels per metre, stepped at a fixed 120 Hz. Each step is run as 4 slices (480 Hz) so a swinging flipper cannot pass through the ball.
- Playfield space has y pointing down, so table gravity is +9.5 m/s^2 in y. This stands in for the incline of a real table.
- The ball is a Box2D bullet body with continuous collision, and its speed is capped at 30 m/s.
- The outer shell is one closed chain shape, which avoids ghost collisions at segment joints.
- Flippers are kinematic bodies driven to an absolute target angle each step (13.5 rad/s up, 12 rad/s down, 60 degree stroke). They cannot overshoot or be pushed back by the ball.
- The plunger is a kinematic block. Holding Space draws it back over one second; on release it drives forward at 3 to 12.5 m/s depending on the draw, and the ball leaves about 1.3 times faster. A full draw clears the arch; a tap does not leave the lane.
- A one-way gate at the top of the lane is modelled as a wall that is switched on once the launched ball is clear, and off again for the next serve.
- Bumpers and slingshot faces add a fixed kick on top of the solver's bounce. Elements are tagged in Box2D shape user data with a packed integer, never a pointer, and validated on the way out.
- Rollovers and holes are detected by ball position every slice, not by Box2D sensors, so the result is exact and deterministic.
- Holes take the ball off the table (its body is disabled) for 0.9 s and return it: wormholes pass it round a ring of three, the black hole and hyperspace chute return it from themselves.
- The launch ramp is modelled as a hole that only takes a ball travelling up the table at 3 m/s or more. A real ramp lifts the ball above the playfield; here the ball is drawn riding over the table and is dropped into the top of the field.
- `src/core/TableLayout.hpp` is the single source of table geometry for physics, drawing and art.

## Layout

- `src/core` game state and rules as a static library with no SDL dependency, unit tested without a display.
- `src/platform` SDL window, input, asset loading and rendering, as a static library shared by the game and the snapshot tool. `TableRenderer` draws the game; `App` owns the window and the loop.
- `assets` PNG art, copied next to the executable at build time. Sprites are authored at 4x.
- `tools/gimp` GIMP 3 Python scripts that generate every asset, so art is reproducible.
- `tools/snapshot` development tool that plays the game headless with scripted input and writes one rendered frame to a PNG, with no window. `ctest` uses it as a rendering smoke test.
- `tests` unit tests and headless smoke tests, run by `ctest`.
- `cmake` shared build settings.

## Assets

All art is original and generated in GIMP 3 by the scripts in `tools/gimp`. The table art is drawn from the same geometry the physics uses: `pinball_layout` prints `src/core/TableLayout.hpp` as JSON and the scripts read it. To regenerate everything, build the project, then in GIMP's Python console run:

```python
REPO = "/path/to/space-cadet-pinball"
LAYOUT = "/tmp/layout.json"   # from: build/pinball_layout > /tmp/layout.json
for script in ("make_ball", "make_table", "make_sprites"):
    exec(open(REPO + "/tools/gimp/" + script + ".py").read())
```

| Asset | Script | Notes |
|---|---|---|
| `table/background.png` | `make_table.py` | 1536x1920 (2x). Space backdrop, steel rails, islands, hole wells, unlit inserts |
| `sprites/ball.png` | `make_ball.py` | 4x |
| `sprites/flipper.png` | `make_sprites.py` | Rotated about its pivot; mirrored for the right flipper |
| `sprites/bumper.png` | `make_sprites.py` | White cap, tinted per bumper group |
| `sprites/glow.png` | `make_sprites.py` | Drawn additively and tinted for every light; also the ball's shadow |
| `sprites/bar.png` | `make_sprites.py` | Stand-up target face, tinted per bank |
| `sprites/plunger.png` | `make_sprites.py` | |

Regeneration is reproducible: gradients are drawn without dithering (GIMP's dither is randomly seeded), noise and stars use fixed seeds, and exports carry no timestamps. Running the scripts twice gives byte-identical files, so the hashes in the asset manifest only change when the art does.

To look at the result without opening a window: `cd build && ./pinball_snapshot 1200 frame.png`.

### Integrity

Assets are external files, so the game checks them before it runs:

- At build time CMake records the size and SHA-256 of every PNG under `assets/`, applying the same name and size rules the game enforces, and compiles that manifest into the executable (`cmake/GenerateAssetManifest.cmake`).
- At startup every listed asset is read into memory and compared with the manifest. Any missing, resized or altered file, or anything that is not a regular file, stops the game with exit code 1 before a window opens.
- Each image is then decoded from the same bytes that were hashed, so a file cannot be swapped between the check and the use.
- The manifest is also the allowlist. A file that is not in it is never opened.
- `ctest` runs `tools/test_asset_integrity.py`, which flips a bit, appends, truncates, empties and deletes each asset and confirms every case is refused.

What this does not cover: the hashes live in the executable, so someone able to replace the executable can replace them too. That is the job of code signing at release time (`codesign` and notarisation on macOS), which protects the executable and with it the manifest.

The game only opens files inside the `assets` directory next to the executable. Files over 16 MB or images over 4096 pixels on an edge are refused before decoding.

Assets are exported with all metadata off. `tools/check_assets.py` (run by `ctest`) fails the build if a PNG carries EXIF, text, timestamp, profile or thumbnail chunks.

## License

MIT. See `LICENSE`.
