# Space Cadet Pinball (C++ recreation)

A 2D recreation of the Space Cadet pinball table in C++17, SDL3 and Box2D, with original art made in GIMP.

This is an independent fan project. It contains no code or assets from the original game and is not affiliated with Microsoft, Maxis or Cinematronics.

## Status

| Phase | Feature | State |
|---|---|---|
| 1 | Project skeleton, window, fixed-step loop, CI | Done |
| 2 | Asset loading and rendering | In progress: loader and ball sprite done, table art pending |
| 3 | Physics foundation | Planned |
| 4 | Flippers | Planned |
| 5 | Plunger and launch lane | Planned |
| 6 | Bumpers, targets, ramps, table geometry | Planned |
| 7 | Scoring, lights, UI | Planned |
| 8 | Missions and ranks | Planned |
| 9 | Nudge and tilt | Planned |
| 10 | Audio | Planned |
| 11 | High scores, polish, performance | Planned |
| 12 | Final playability pass | Planned |

## Build (macOS)

```sh
brew install cmake sdl3
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/pinball
```

Options:

- `-DPINBALL_SANITIZE=ON` builds with AddressSanitizer and UBSan.
- `-DPINBALL_WERROR=ON` treats warnings as errors. CI uses this.

## Controls

| Key | Action | State |
|---|---|---|
| Z | Left flipper | Planned |
| / | Right flipper | Planned |
| Space | Plunger, hold for power | Planned |
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

The game only opens files inside the `assets` directory next to the executable. Asset names are validated, and files over 16 MB or images over 4096 pixels on an edge are refused before decoding.

Assets are exported with all metadata off. `tools/check_assets.py` (run by `ctest`) fails the build if a PNG carries EXIF, text, timestamp, profile or thumbnail chunks.

## License

MIT. See `LICENSE`.
