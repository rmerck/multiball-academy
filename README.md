# Space Cadet Pinball (C++ recreation)

A 2D recreation of the Space Cadet pinball table in C++17, SDL3 and Box2D, with original art made in GIMP.

This is an independent fan project. It contains no code or assets from the original game and is not affiliated with Microsoft, Maxis or Cinematronics.

## Status

| Phase | Feature | State |
|---|---|---|
| 1 | Project skeleton, window, fixed-step loop, CI | Done |
| 2 | Asset loading and rendering | Planned |
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

1. Secure software practices: RAII ownership, no raw `new`/`delete` in game code, validated input, no network access, sanitizer builds in CI.
2. Performance: fixed 120 Hz simulation step, vsynced rendering, 60 FPS target.
3. Faithfulness to the original feel.

## Layout

- `src/core` platform-independent logic, unit tested without a display.
- `src/platform` SDL window, input and rendering.
- `tests` unit tests and headless smoke tests, run by `ctest`.
- `cmake` shared build settings.

## License

MIT. See `LICENSE`.
