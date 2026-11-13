# Design notes

How the game is put together: priorities, high score file, sound, physics and where things live in the tree.

## Design priorities

1. Secure software practices: RAII ownership with no owning raw pointers, standard-library hardening, validated input, no network access, sanitizer builds in CI.
2. Performance: fixed 120 Hz simulation step, rendering at display refresh rate with a manual pacing fallback, 60 FPS minimum target. The ball and the flippers are drawn part-way between the last two steps, so their motion is smooth on a display that does not refresh at a multiple of 60 Hz; everything else shows the latest step.
3. Faithfulness to the original feel.

The code is C++23 with compiler extensions off, kept to the parts of the language and library that GCC 13 with libstdc++, clang 18 with libstdc++ or libc++, and Xcode 16 all provide. That rules out `std::expected`, `std::print` and explicit object parameters for now. Every file includes the header for each standard name it uses, because libc++ in C++23 no longer brings them in through other headers. Warnings are errors in CI with all three compilers.

## High scores

The five best games are kept in a small text file in the per-user preferences directory (`~/Library/Application Support/multiball-academy/highscores.txt` on macOS, `~/.local/share/multiball-academy/highscores.txt` on Linux unless `XDG_DATA_HOME` says otherwise, and `Library/Application Support/multiball-academy/highscores.txt` inside the application's own container on iOS), with the score, the rank reached and the player's three initials. This is the only file the game ever writes. Version 1.0.0, released as Space Cadet Pinball, kept its scores in a directory under that name; they are not carried over, and that file is never read or removed.

A game that makes the table asks for initials: type letters, or use Up and Down to change one, Enter to confirm each, Left or Backspace to go back, Right to move on, Escape to accept as shown. The last initials entered are offered again.

- It is written to a new, uniquely named, owner-only temporary file, flushed to disk and renamed into place, so a crash cannot leave half a table and no existing file or link is ever written through. Where the file system has no flush to offer, the save goes ahead without one; a flush that fails stops it. The temporary file is named `highscores.txt.tmp-` and six more characters; one left by an interrupted save is removed at a later start, once it is a minute old. Only regular files with exactly such a name are removed. (These link and permission protections are POSIX; other platforms use plain SDL file calls with the same size limit.)
- On load it must be a regular file, not a link, of at most 512 bytes and every line is validated strictly (digits only, in range, in order, initials A to Z or dashes). Anything else is set aside as `highscores.txt.bad`, or `.bad.1` to `.bad.9` when earlier ones are still there (a file set aside is never replaced), and the table starts empty; it is never partly trusted. If all ten names are taken the file stays where it is and nothing is saved that run. A file that cannot be read at all is left where it is, and nothing is saved that run.
- There is one file format. The two that version 1.0.0 wrote, under the game's first name and in another directory, are not read.

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
- A one-way gate at the top of the lane is modelled as a wall that is switched on once the launched ball is clear, and off again for the next serve. The ball counts as in play (ball save, fuel, nudges) from the moment it is on the playfield: past the top of the arch, anywhere left of the lane wall below its top, or at its first hit or capture.
- Bumpers and slingshot faces add a fixed kick on top of the solver's bounce. Elements are tagged in Box2D shape user data with a packed integer, never a pointer, and validated on the way out.
- Rollovers and holes are detected by ball position every slice, not by Box2D sensors, so the result is exact and deterministic. A rollover is a switch across the whole width of its lane, wider than its drawn light, so a ball running down the lane's wall still trips it.
- Holes take the ball off the table (its body is disabled) for 0.9 s and return it: wormholes pass it round a ring of three, the black hole and hyperspace chute return it from themselves.
- The launch ramp's mouth is a strip 74 pixels wide in the left of the lower field, facing the right flipper. It takes only a ball travelling up the table at 11 m/s or more that is also heading left by at least a quarter of that speed: a firm shot off the right flipper, with a timing window of two to five steps (about six from a cradle), or a falling ball met late in the stroke by the outer half of the left flipper. A slower ball, or one rolling or cradled on the left flipper and driven straight up, rolls over it. A real ramp lifts the ball above the playfield; here the ball is drawn riding over the table from where it entered and is dropped into the top of the field.
- `src/core/TableLayout.hpp` is the single source of table geometry for physics, drawing and art.

## Layout

- `src/core` game state and rules as a static library with no SDL dependency, unit tested without a display. `ScreenLayout` says where everything goes on the screen, as a function of the window's size and safe area: the desktop's fixed 1280 x 960 canvas with the table left and the panel right, and for touch screens either that or a column of status strip, table and control deck, whichever shows the table larger. `PanelLayout` is the inside of the panel and `OverlayLayout` the rectangles of the dialogs. Drawing takes a layout; so far only the desktop's is drawn. `TouchControls` (touches to game input) is a placeholder.
- `src/platform` SDL window, input, asset loading and rendering, as a static library shared by the game and the snapshot tool. `TableRenderer` draws the game; `App` owns the window and runs the loop as `frame()` once per displayed frame, called by `run()` on a desktop, and `finish()` at the end. Its keyboard handling is in `AppKeyboard.cpp` and the pause on losing focus in `AppLifecycle.cpp`; `AppTouch.cpp`, `TouchOverlay` and `ios/IosSystem` are placeholders for the touch screen and iOS, and `src/main_ios.cpp` for the iOS entry point.
- `assets` PNG art, copied at build time next to the executable, or into `Contents/Resources` of the macOS bundle. Sprites are authored at 4x.
- `packaging/macos` the application icon: `icon_1024.png` as drawn, and `AppIcon.icns` made from it, which the build copies into the bundle.
- `tools/gimp` GIMP 3 Python scripts that generate every asset and the icon, so art is reproducible.
- `tools/snapshot` development tool (`pinball_snapshot`) that plays the game headless with scripted input and writes one rendered frame to a PNG, with no window. `ctest` uses it as a rendering smoke test.
- `tools/layout_dump` development tool (`pinball_layout`) that prints the table geometry as JSON for the art scripts. Both tools are built into the top of the build directory, outside the macOS bundle, where `pinball_snapshot` has an `assets` copy of its own.
- `tools/make_icns.sh` builds `AppIcon.icns` from the icon PNG with `sips` and `iconutil`. `tools/notarize.sh` notarises a Developer ID signed bundle.
- `tools/check_assets.py`, `tools/test_asset_integrity.py` asset checks run by `ctest`. `tools/make_font.py` regenerates the pixel font header. `tools/check_pins.sh` compares the pinned dependencies with upstream. `tools/check_hardening.py` and `tools/check_deps_cache.sh` are CI's checks of the compiler hardening and of what its cache restored.
- `tests` unit tests and headless smoke tests, run by `ctest`. `tests/fuzz` holds the fuzzer, its seed corpus and dictionary.
- `docs` the reference pages: building, rules, design notes, art.
- `cmake` hardening flags, the pinned dependencies, the asset manifest generator, and the templates for the macOS `Info.plist` and the generated version header. `Ios.cmake` and `LayoutTests.cmake` are included by the build and still empty. The game's name, its identifier, its data directory and its version are written once, in a block at the top of `CMakeLists.txt`.
- `.github/workflows` CI and the weekly dependency pin check.
