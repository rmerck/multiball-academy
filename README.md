# Space Cadet Pinball (C++ recreation)

A 2D recreation of the Space Cadet pinball table in C++17, SDL3 and Box2D, with original art made in GIMP.

This is an independent fan project. It contains no code or assets from the original game and is not affiliated with Microsoft, Maxis or Cinematronics.

## Status

| Phase | Feature | State |
|---|---|---|
| 1 | Project skeleton, window, fixed-step loop, CI | Done |
| 2 | Asset loading and rendering | In progress: loader and ball sprite done, table art pending |
| 3 | Physics foundation | Done: Box2D world, table shell, ball, drain |
| 4 | Flippers | Done |
| 5 | Plunger and launch lane | Done |
| 6 | Bumpers, targets, ramps, table geometry | Done: 8 bumpers, slingshots, 4 target banks, 7 rollovers, wormholes, black hole, hyperspace, launch ramp |
| 7 | Scoring, lights, UI | Planned |
| 8 | Missions and ranks | Planned |
| 9 | Nudge and tilt | Planned |
| 10 | Audio | Planned |
| 11 | High scores, polish, performance | Planned |
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

Keys are read by physical position (scancode), so they sit where Z and / are on a US keyboard whatever the layout.

| Key | Action | State |
|---|---|---|
| Z | Left flipper | Done |
| / | Right flipper | Done |
| Space | Plunger, hold for power | Done |
| X | Nudge left | Planned |
| . | Nudge right | Planned |
| Up | Nudge from bottom | Planned |
| F2 | New game | Planned |
| F3 | Pause | Done |
| F4 | Fullscreen | Done |

## Design priorities

1. Secure software practices: RAII ownership with no owning raw pointers, standard-library hardening, validated input, no network access, sanitizer builds in CI.
2. Performance: fixed 120 Hz simulation step, rendering at display refresh rate with a manual pacing fallback, 60 FPS minimum target.
3. Faithfulness to the original feel.

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
- `src/platform` SDL window, input and rendering. Feeds input to `core` and draws its state.
- `assets` PNG art, copied next to the executable at build time. Sprites are authored at 4x.
- `tools/gimp` GIMP 3 Python scripts that generate every asset, so art is reproducible.
- `tests` unit tests and headless smoke tests, run by `ctest`.
- `cmake` shared build settings.

## Assets

All art is original and generated in GIMP 3 by the scripts in `tools/gimp`. To regenerate one, open GIMP's Python console and run:

```python
REPO = "/path/to/space-cadet-pinball"
exec(open(REPO + "/tools/gimp/make_ball.py").read())
```

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
