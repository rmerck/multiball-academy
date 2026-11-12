# Space Cadet Pinball (C++ recreation)

A 2D recreation of the Space Cadet pinball table in C++23, SDL3 and Box2D, with original art made in GIMP.

This is an independent fan project. It contains no code or assets from the original game and is not affiliated with Microsoft, Maxis or Cinematronics.

## What is in it

- A full table: 8 bumpers, 2 slingshots, 4 target banks (11 targets), 7 rollovers, 3 wormholes, a black hole, a hyperspace chute and a launch ramp, with flippers, a plunger and launch lane, nudge and tilt.
- A rules engine with lit lanes and targets, 9 ranks, 36 missions and fuel, and an information panel drawn with a built-in pixel font.
- Synthesized sound effects and an original background tune. There are no audio files.
- A high score table with initials, an About screen, auto-pause, and an idle wait while paused.
- Art generated in GIMP from the same table geometry the physics uses, loaded through a verifying loader.
- A test suite that runs under sanitizers and in Release, including a 10 minute simulated soak and offscreen render checks of play, the About screen and the initials prompt.

Known approximations: the table geometry, art and sounds are original work in the style of the 1995 table, not copies of it. The music is an original tune, not the original soundtrack. Mission goals follow the original's structure with simplified conditions.

Platforms: macOS and Linux build and pass the tests in CI. Windows is untested.

## Build

Needs CMake 3.26 or newer and a C++23 compiler (GCC 13, Clang 18, Xcode 16 or newer). Python 3 is optional: without it CMake warns and three of the tests (`assets_clean`, `asset_integrity`, `layout_json`) are left out, unless `-DPINBALL_REQUIRE_ALL_TESTS=ON` makes that an error. In-source builds are refused.

macOS, with the libraries from Homebrew:

```sh
brew install cmake sdl3 box2d
cmake -S . -B build
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/pinball
```

Linux: the same `cmake` and `ctest` commands work where SDL3 3.4 or newer and Box2D 3.2 or newer are installed as CMake packages. Few distributions have both yet, so the usual way is to let the build fetch them (`-DPINBALL_FETCH_DEPS=ON`, below), which needs the headers SDL builds against. On Ubuntu 24.04, as CI does:

```sh
sudo apt-get install -y --no-install-recommends \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev \
  libxss-dev libxtst-dev libxkbcommon-dev libwayland-dev wayland-protocols libegl1-mesa-dev \
  libgl1-mesa-dev libasound2-dev libpulse-dev libudev-dev libdbus-1-dev
cmake -S . -B build -DPINBALL_FETCH_DEPS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/pinball
```

The default build type is RelWithDebInfo. Options:

- `-DPINBALL_SANITIZE=ON` builds with AddressSanitizer and UBSan.
- `-DPINBALL_WERROR=ON` treats warnings as errors. CI uses this.
- `-DPINBALL_FETCH_DEPS=ON` needs no installed SDL or Box2D: it downloads SDL3 3.4.20 (release archive, checked by SHA-256) and Box2D 3.2.0 (fixed by commit), builds them with the same protective compiler flags as the game, and links them statically. CI uses this. `tools/check_pins.sh` reports when either has a newer release; a weekly workflow runs it. FetchContent's own switches that would put other sources in their place unchecked (`FETCHCONTENT_SOURCE_DIR_SDL3`, `FETCHCONTENT_SOURCE_DIR_BOX2D`, `FETCHCONTENT_FULLY_DISCONNECTED`, `FETCHCONTENT_TRY_FIND_PACKAGE_MODE=ALWAYS`) stop the configuration.
- `-DPINBALL_ALLOW_UNVERIFIED_DEPS=ON` accepts those switches, with a warning: the dependencies are then whatever was supplied, not the pinned and verified sources.
- `-DPINBALL_HARDENED_RUNTIME=OFF` (macOS, only with `PINBALL_FETCH_DEPS` and without sanitizers) skips the ad hoc signing with the hardened runtime that a Release build otherwise gets.
- `-DPINBALL_BUILD_TOOLS=OFF` leaves out the development tools `pinball_snapshot` and `pinball_layout`, and with them the render and layout tests.
- `-DBUILD_TESTING=OFF` leaves out the tests.
- `-DPINBALL_REQUIRE_ALL_TESTS=ON` stops the configuration if a test cannot be registered: no Python 3, or the tools left out. CI uses this, and runs `ctest --no-tests=error`.
- `-DPINBALL_FUZZ=ON` (clang with libFuzzer, not Apple clang) builds only `pinball_fuzz`, which fuzzes the score file, PNG header, asset name and argument parsers.

There are no install rules: the game runs from the build directory, with its `assets` directory next to the executable.

`pinball --frames N` (1 to 1,000,000) exits after N frames and leaves the high score file alone. The tests use it; the game takes no other arguments.

### A build to give to someone else

```sh
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DPINBALL_FETCH_DEPS=ON
cmake --build build-release --parallel
```

On macOS that binary is optimised, links only system libraries, carries no path from the build machine, and runs under the hardened runtime, so the system ignores injected libraries. It is not signed by an identified developer or notarised, so macOS Gatekeeper will still warn about it on another machine.

On Linux the same build is a position-independent executable with full RELRO, stack protection, fortified library calls and control-flow protection. SDL still opens the system's display and audio libraries at run time, as every SDL program does.

### Tests and CI

`ctest` runs 16 tests: the core unit tests (`core_tests`, no display needed), the file handling tests, two asset checks, three offscreen render checks, a check that the snapshot tool replaces a symbolic link at its output name instead of writing through it, the layout dump check, a headless run of the real game, and six rejected command lines. They need no display or audio device, and scratch files stay in the build directory. A timed run (`--frames`) never opens the high score file, and on Linux the tests also point the per-user preferences directory into the build tree.

Every pull request must pass seven CI jobs, all with warnings as errors. The first six build SDL3 and Box2D from the pinned sources, which are fetched and verified on every run. Only SDL's compiled objects are cached between runs, never source code, and only runs on `main` write the cache.

- macOS, with and without sanitizers.
- macOS Release, checked for build paths in the binary, system-only libraries and the hardened runtime.
- Linux with GCC and with clang, both under sanitizers.
- Linux Release with GCC, with the hardening checked: `readelf` on the binary for what the linker decides, and `tools/check_hardening.py` for the compiler flags of the game's own files and the stack-protector and FORTIFY references in two of its objects (the binary cannot show those, because the statically linked SDL has them too).
- A two minute libFuzzer run over the parsers.

An eighth job, not required, builds and tests on Linux with clang and libc++ under sanitizers. libc++ is the standard library macOS uses, so a missing include shows up there before it breaks the macOS jobs.

## Controls

Keys are read by physical position (scancode), so they sit where Z and / are on a US keyboard whatever the layout. F1, F3, F4 and Escape are matched by name.

| Key | Action |
|---|---|
| Z | Left flipper |
| / | Right flipper |
| Space | Plunger, hold for power |
| X | Nudge left |
| . | Nudge right |
| Up | Nudge from bottom |
| F2 | New game |
| F1 | About and high scores. Escape or F1 closes it. On macOS the menu bar's About item opens the same screen |
| F3 | Pause. Does nothing while the About screen is open, where the change could not be seen. The game also pauses by itself when its window loses focus or is minimised |
| F4 | Fullscreen. Works at any time, including in the initials prompt |
| M | Mute all sound |
| N | Music on and off |

If the graphics device is reset, the textures are read and verified again from disk and the game carries on. If the device is lost for good, the game keeps a score that was waiting at the initials prompt and exits with code 1.

## Design priorities

1. Secure software practices: RAII ownership with no owning raw pointers, standard-library hardening, validated input, no network access, sanitizer builds in CI.
2. Performance: fixed 120 Hz simulation step, rendering at display refresh rate with a manual pacing fallback, 60 FPS minimum target. The ball and the flippers are drawn part-way between the last two steps, so their motion is smooth on a display that does not refresh at a multiple of 60 Hz; everything else shows the latest step.
3. Faithfulness to the original feel.

The code is C++23 with compiler extensions off, kept to the parts of the language and library that GCC 13 with libstdc++, clang 18 with libstdc++ or libc++, and Xcode 16 all provide. That rules out `std::expected`, `std::print` and explicit object parameters for now. Every file includes the header for each standard name it uses, because libc++ in C++23 no longer brings them in through other headers. Warnings are errors in CI with all three compilers.

## Rules

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

### Nudge and tilt

X bumps the table from the left and sends the ball right, `.` does the opposite, and Up bumps from the bottom and sends the ball up the table. The drawn table jolts and settles.

Each nudge adds tilt heat, which cools over 2.5 seconds. Two quick nudges show a warning. Three inside about a second tilt the table: the flippers and plunger go dead, nothing scores, the end-of-ball bonus is lost and no ball save applies, until the ball drains. The next ball starts clean.

### Missions and ranks

1. Hit a mission target on the left wall to select a mission: top, middle or bottom picks one of the rank's three missions. Lighting all three selects the rank's special mission. The mission targets are ignored while a mission is active, and the hit that completes a mission does not select the next one.
2. Shoot the launch ramp to accept it. The fuel gauge fills and the mission targets' lights go out: until the mission ends they score without lighting, so the special mission always needs all three hit afresh. The ramp is a timed shot: a firm one off the right flipper, or a falling ball caught late by the left one.
3. Do what the display asks. Some missions have two stages.
4. Fuel lasts 90 seconds and burns only while a ball is in play. The ramp refuels. If it runs out the mission is aborted.
5. Completing a mission pays its award (500,000 at Cadet up to 10,000,000 for the last special mission), plus the jackpot if lit, and fills the rank gauge: 6 of 18 for a normal mission, 9 for a special. A full gauge is a promotion.

Ranks: Cadet, Ensign, Lieutenant, Captain, Lt Commander, Commander, Commodore, Admiral, Fleet Admiral. Each rank has its own four missions with larger counts and awards. Missions carry over from ball to ball. The mission table is in `src/core/Missions.cpp`.

The mission structure follows the original (select with the mission targets, accept on the ramp, fuel, rank lights). The individual missions, their counts and awards are this project's own.

A ball that drains within 6 seconds of entering play is returned free, once per ball. The rules live in `src/core/Rules` with no dependency on physics, and are unit tested event by event.

Differences from the original are deliberate simplifications: awards do not time out, and there is no replay score.

## High scores

The five best games are kept in a small text file in the per-user preferences directory (`~/Library/Application Support/space-cadet-pinball/Space Cadet Pinball/highscores.txt` on macOS, `~/.local/share/space-cadet-pinball/Space Cadet Pinball/highscores.txt` on Linux unless `XDG_DATA_HOME` says otherwise), with the score, the rank reached and the player's three initials. This is the only file the game ever writes.

A game that makes the table asks for initials: type letters, or use Up and Down to change one, Enter to confirm each, Left or Backspace to go back, Right to move on, Escape to accept as shown. The last initials entered are offered again.

- It is written to a new, uniquely named, owner-only temporary file, flushed to disk and renamed into place, so a crash cannot leave half a table and no existing file or link is ever written through. Where the file system has no flush to offer, the save goes ahead without one; a flush that fails stops it. The temporary file is named `highscores.txt.tmp-` and six more characters; one left by an interrupted save is removed at a later start, once it is a minute old. Only regular files with exactly such a name are removed. (These link and permission protections are POSIX; other platforms use plain SDL file calls with the same size limit.)
- On load it must be a regular file, not a link, of at most 512 bytes and every line is validated strictly (digits only, in range, in order, initials A to Z or dashes). Anything else is set aside as `highscores.txt.bad`, or `.bad.1` to `.bad.9` when earlier ones are still there (a file set aside is never replaced), and the table starts empty; it is never partly trusted. If all ten names are taken the file stays where it is and nothing is saved that run. A file that cannot be read at all is left where it is, and nothing is saved that run.
- Files from before initials were added are still read; their entries show dashes.

## Sound

Every sound is synthesized at run time from sine, square and triangle oscillators and noise (`src/core/Sound`), so the game ships no audio files and has nothing extra to verify. Twenty effects cover flippers, launch, bumpers, targets, lanes, holes, drains, awards, missions, tilt and game over.

The mixer is plain arithmetic with no device access and is unit tested: output is always finite and within [-1, 1], every sound ends, and a flood of triggers cannot overrun the 24-voice pool. The platform layer pulls samples on the main thread and queues about 40 to 50 ms to the default device, so no locking is needed. If no audio device opens, the game runs silent.

The background tune (`src/core/Music`) is an original eight-bar loop for bass, arpeggio and lead, synthesized the same way and mixed under the effects. N turns it on and off. It rests while the game is paused and picks up where it stopped. It is not the original game's music.

## Physics notes

- Box2D 3.2 runs in metres at 100 playfield pixels per metre, stepped at a fixed 120 Hz. Each step is run as 4 slices (480 Hz) so a swinging flipper cannot pass through the ball.
- Playfield space has y pointing down, so table gravity is +9.5 m/s^2 in y. This stands in for the incline of a real table.
- The ball is a Box2D bullet body with continuous collision, and its speed is capped at 30 m/s.
- The outer shell is one closed chain shape, which avoids ghost collisions at segment joints.
- Flippers are kinematic bodies driven to an absolute target angle each step (13.5 rad/s up, 12 rad/s down, 60 degree stroke). They cannot overshoot or be pushed back by the ball.
- The plunger is a kinematic block. Holding Space draws it back over one second; on release it drives forward at 3 to 12.5 m/s depending on the draw, and the ball leaves about 1.3 times faster. A full draw clears the arch; a tap does not leave the lane. The launch sound plays on release when the plunger was drawn back by more than 4 pixels, and never on a tilted or finished table.
- A one-way gate at the top of the lane is modelled as a wall that is switched on once the launched ball is clear, and off again for the next serve.
- Bumpers and slingshot faces add a fixed kick on top of the solver's bounce. Elements are tagged in Box2D shape user data with a packed integer, never a pointer, and validated on the way out.
- Rollovers and holes are detected by ball position every slice, not by Box2D sensors, so the result is exact and deterministic. A rollover is a switch across the whole width of its lane, wider than its drawn light, so a ball running down the lane's wall still trips it.
- Holes take the ball off the table (its body is disabled) for 0.9 s and return it: wormholes pass it round a ring of three, the black hole and hyperspace chute return it from themselves.
- The launch ramp's mouth is a strip 74 pixels wide in the left of the lower field, facing the right flipper. It takes only a ball travelling up the table at 11 m/s or more that is also heading left by at least a quarter of that speed: a firm shot off the right flipper, with a timing window of two to five steps (about six from a cradle), or a falling ball met late in the stroke by the outer half of the left flipper. A slower ball, or one rolling or cradled on the left flipper and driven straight up, rolls over it. A real ramp lifts the ball above the playfield; here the ball is drawn riding over the table from where it entered and is dropped into the top of the field.
- `src/core/TableLayout.hpp` is the single source of table geometry for physics, drawing and art.

## Layout

- `src/core` game state and rules as a static library with no SDL dependency, unit tested without a display.
- `src/platform` SDL window, input, asset loading and rendering, as a static library shared by the game and the snapshot tool. `TableRenderer` draws the game; `App` owns the window and the loop.
- `assets` PNG art, copied next to the executable at build time. Sprites are authored at 4x.
- `tools/gimp` GIMP 3 Python scripts that generate every asset, so art is reproducible.
- `tools/snapshot` development tool (`pinball_snapshot`) that plays the game headless with scripted input and writes one rendered frame to a PNG, with no window. `ctest` uses it as a rendering smoke test.
- `tools/layout_dump` development tool (`pinball_layout`) that prints the table geometry as JSON for the art scripts.
- `tools/check_assets.py`, `tools/test_asset_integrity.py` asset checks run by `ctest`. `tools/make_font.py` regenerates the pixel font header. `tools/check_pins.sh` compares the pinned dependencies with upstream. `tools/check_hardening.py` and `tools/check_deps_cache.sh` are CI's checks of the compiler hardening and of what its cache restored.
- `tests` unit tests and headless smoke tests, run by `ctest`. `tests/fuzz` holds the fuzzer, its seed corpus and dictionary.
- `cmake` hardening flags, the pinned dependencies and the asset manifest generator.
- `.github/workflows` CI and the weekly dependency pin check.

## Assets

The game wears a 1980s neon arcade look: a slatted sun setting behind a mountain ridge, a city skyline and palm silhouettes, a perspective grid for a floor, neon tube rails with bloom, a starfield with nebula clouds, Memphis confetti on the cabinet, and faint CRT raster lines and grain over everything. Each part of the table has its own motif and colour: a hot pink starburst diamond under the attack bumpers, a cyan delta and dial round the engine bumpers, yellow orbits round the satellite bumper, spiral wormholes, an accretion disc for the black hole, nested diamonds for hyperspace, and an orange ramp: a flared scoop at the ramp's mouth with chevrons along the line of the shot from the right flipper, and rails that climb from it past the wormhole and round the arch to the exit. The mouth is drawn from `rampMouth` in the layout, so it is exactly as wide as the strip the physics tests.

All art is original. It is generated in GIMP 3 by the scripts in `tools/gimp`, with one exception described below. The table art is drawn from the same geometry the physics uses: `pinball_layout` prints `src/core/TableLayout.hpp` and `src/core/PanelLayout.hpp` as JSON and the scripts read it. To regenerate everything, build the project, then in GIMP's Python console run:

```python
REPO = "/path/to/space-cadet-pinball"
LAYOUT = REPO + "/build/layout.json"   # from: build/pinball_layout > build/layout.json
for script in ("make_ball", "make_table", "make_sprites", "make_panel"):
    exec(open(REPO + "/tools/gimp/" + script + ".py").read())
```

`make_table.py` takes a couple of minutes: it builds about forty layers and several blur passes at 1536x1920. `tools/gimp/common.py` holds the shared helpers. Each script refuses to run without the layout file, sets every GIMP context setting it depends on, and puts your context back and deletes its working image when it ends, whether or not it succeeded. The export fails loudly if GIMP lacks any of the options that switch metadata off.

| Asset | Script | Notes |
|---|---|---|
| `table/background.png` | `make_table.py` | 1536x1920 (2x). Sky, sun, horizon, grid floor, area motifs, neon rails, islands, hole wells, unlit inserts |
| `ui/panel.png` | `make_panel.py` | 1024x1920 (2x). Information panel beside the playfield: backglass illustration behind the title, neon-framed display windows from `src/core/PanelLayout.hpp`, grid and accents |
| `sprites/ball.png` | `make_ball.py` | 4x. Chrome ball with a bright rim so it stays visible on any part of the art |
| `sprites/flipper.png` | `make_sprites.py` | Rotated about its pivot; mirrored for the right flipper. A dark halo under a cyan glow lifts it off the grid |
| `sprites/bumper.png` | `make_sprites.py` | White starburst on a dark skirt, tinted per bumper group |
| `sprites/glow.png` | `make_sprites.py` | Drawn additively and tinted for every light; also the ball's shadow and halo |
| `sprites/bar.png` | `make_sprites.py` | Stand-up target face, tinted per bank; its bright outline keeps an unlit target visible |
| `sprites/plunger.png` | `make_sprites.py` | |

Lights, lit targets, bumper flashes, the lane gate, all text and the About and initials dialogs, with their frames, zigzags and the small sunset, are drawn by the game (`src/platform/TableRenderer.cpp`, `Hud.cpp`, `Text.cpp`). Their colours match the rims and inserts in the art; if you change a palette in one place, change it in the other.

### The Canva source

The sunset illustration behind the title on the panel was generated with Canva's AI design tools and exported as a PNG. It is kept at `tools/gimp/sources/backglass.png` (1920x900: the export with the bottom rows cropped away, re-exported from GIMP with all metadata removed). `make_panel.py` scales it to the panel width, raises its chroma, fades it into the panel below the title and draws everything else over it. That file cannot be regenerated by a script, so `ui/panel.png` is reproducible only given that source. No other asset contains pixels from outside the scripts. The source must stay free of metadata too: `python3 tools/check_assets.py tools/gimp/sources`.

### Reproducibility

Regeneration is reproducible: two-colour gradients are drawn without dithering (GIMP's dither is randomly seeded), noise, stars and skyline use fixed seeds, and exports carry no timestamps. Running the scripts twice gives byte-identical files, so the hashes in the asset manifest only change when the art does.

Two GIMP behaviours the helpers in `tools/gimp/common.py` work around: a selection fill ignores the alpha of the foreground colour (`fill_selection` applies it as the fill opacity), and a two-colour gradient ignores alpha altogether (`alpha_gradient` fades to transparent instead).

To look at the result without opening a window: `cd build && ./pinball_snapshot 1200 frame.png` plays 1200 steps (ten seconds) and writes the frame to the current directory, as a new file readable by its owner; a link with that name is replaced, not followed. A third argument, `about` or `initials`, draws that screen instead.

### Integrity

Assets are external files, so the game checks them before it runs:

- At build time CMake records the size and SHA-256 of every PNG under `assets/`, applying the same name and size rules the game enforces, and compiles that manifest into the executable (`cmake/GenerateAssetManifest.cmake`).
- At startup every listed asset is read into memory and compared with the manifest. Any missing, resized or altered file, or anything that is not a regular file, stops the game with exit code 1 before a window opens.
- Each image is then decoded from the same bytes that were hashed, so a file cannot be swapped between the check and the use.
- The manifest is also the allowlist. A file that is not in it is never opened.
- `ctest` runs `tools/test_asset_integrity.py`, which flips a bit, appends, truncates, empties and deletes each asset, and replaces it with a FIFO, a symbolic link to a genuine copy and a directory, and confirms every case is refused.

What this does not cover: the hashes live in the executable, so someone able to replace the executable can replace them too. That is the job of code signing at release time (`codesign` and notarisation on macOS), which protects the executable and with it the manifest.

The game only opens files inside the `assets` directory next to the executable. Files over 16 MB or images over 4096 pixels on an edge are refused before decoding.

Assets are exported with all metadata off. `tools/check_assets.py` (run by `ctest`) fails if a PNG carries EXIF, text, timestamp, profile or thumbnail chunks, is malformed, or if anything but a regular `.png` file is in `assets`.

## Known limitations

Minor findings from review that were looked at and left as they are:

- Only the ball and the flippers are drawn between steps. The plunger, the nudge jolt and the lights show the latest 120 Hz step, and the picture is up to one step (8 ms) behind the simulation.
- The flippers and plunger are commanded on every physics slice, even at rest (about a tenth of the 8 microseconds a step takes).
- HUD text is redrawn as rectangles every frame. On SDL's software renderer the About, initials and pause screens take 15 to 24 ms a frame; the loop sleeps while they are up.
- `clang-tidy` is not run in CI. A configuration is in `.clang-tidy`.
- Windows is untested, and the link and permission protections of the score file are POSIX only.

## How this was made

This is a learning project. It was built with Claude, Anthropic's AI coding assistant: I set the requirements, priorities and workflow, and Claude wrote the code, tests and art scripts through reviewed pull requests. Treat it as an experiment in AI-assisted development, not as hand-written reference code.

## License

MIT. See `LICENSE`.
