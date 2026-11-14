# Art and assets

The table's layout is new and its art is still to be drawn. Until it is, the game draws the playfield itself from `src/core/TableLayout.hpp`, in plain colours: the field inside the shell, every wall and guide as the physics has it (teal), the dead posts and slingshot vanes (grey), the slingshots (pink), the lane switches, the boxes of the Briefing room, the Hangar and the two ramp mouths with their rails and exits, the spinner line, the kickback zone, the lock bays, and every lamp at its place, unlit. Bumpers, targets, bay doors, flippers, plunger and balls are the sprites, as before. That drawing is used whenever the asset manifest has no `table/background.png`, so the picture always matches the table being played; putting a `table/background.png` into `assets` switches to the art (`TableRenderer`).

The panel and the sprites still wear the old 1980s neon look and are to be redrawn with the table. The art pipeline is being replaced with them: the script that drew the old table's playfield is gone (it could not run against the new layout), no script draws the new one yet, and the scripts below, which still make the ball, the sprites, the panel and the icon, stay only until the new art replaces what they make.

All art is original. It is generated in GIMP 3 by the scripts in `tools/gimp`, with one exception described below. The art is drawn from the same geometry the physics uses: `pinball_layout` prints `src/core/TableLayout.hpp` and `src/core/PanelLayout.hpp` as JSON and the scripts read it.

What `pinball_layout` prints for the table: `width`, `height`, `ballRadius`, `fieldCenterX`, `lane`, `plunger`, `flipper`, `arch` (centre and radii of the ellipse the arch follows), `ramp` (least speed, ride time), `spinner`, `kickback`, `lampSizes`; the chains `shell` and `laneIsland` (closed loops of points) and `innerArch`, `slingVaneLeft`, `slingVaneRight` (open and one-sided: the first and last point of each are ghosts, not wall); `walls` and `deadPosts` (capsules `a`, `b`, `r`), `slingshots`, `flippers`, `bumpers`, `targets` (with `id`), `bayDoors`, `lockBays`, `rollovers` (centre, radius, the half size of the switch strip, group `rollCall`, `inlane` or `outlane`), `holes` (centre, half size of the box, type `scoop`, `hangar`, `rampLeft` or `rampRight`, exit and exit velocity), `sensors` (the lines `a` to `b` of the two circuit mouths, the top of the arch and the spinner), `kickbackPath` (the way a ball taken by the kickback is carried), `runwayRail` and `skywayRail`, and `lamps` (id in `LampId` order, kind `arrow`, `course` or `status`, centre). The old keys `fuelLights`, `hole.radius`, `rampExit`, `rampMouth`, `laneGate`, `medalIsland` and the `group` of a bumper and `bank` of a target are gone. `tools/gimp/make_table.py` still draws the old table from the old keys and does not run against this output; it is to be rewritten with the new art. The other scripts read only the panel and sprite parts, which are unchanged. To regenerate those, build the project, then in GIMP's Python console run:

```python
REPO = "/path/to/multiball-academy"
LAYOUT = REPO + "/build/layout.json"   # from: build/pinball_layout > build/layout.json
for script in ("make_ball", "make_sprites", "make_panel"):
    exec(open(REPO + "/tools/gimp/" + script + ".py").read())
```

`tools/gimp/common.py` holds the shared helpers. Each script refuses to run without the layout file, sets every GIMP context setting it depends on, and puts your context back and deletes its working image when it ends, whether or not it succeeded. The export fails loudly if GIMP lacks any of the options that switch metadata off.

| Asset | Script | Notes |
|---|---|---|
| `table/background.png` | (to be drawn) | 1536x1920 (2x). Not in the tree: the game draws the table from its layout until the art for the new table exists |
| `ui/panel.png` | `make_panel.py` | 1024x1920 (2x). Information panel beside the playfield: backglass illustration behind the title, neon-framed display windows from `src/core/PanelLayout.hpp`, grid and accents |
| `sprites/ball.png` | `make_ball.py` | 4x. Chrome ball with a bright rim so it stays visible on any part of the art |
| `sprites/flipper.png` | `make_sprites.py` | Rotated about its pivot; mirrored for the right flipper. A dark halo under a cyan glow lifts it off the grid |
| `sprites/bumper.png` | `make_sprites.py` | White starburst on a dark skirt, tinted |
| `sprites/glow.png` | `make_sprites.py` | Drawn additively and tinted for every light; also the ball's shadow and halo |
| `sprites/bar.png` | `make_sprites.py` | Stand-up target and bay door face, tinted; its bright outline keeps an unlit one visible |
| `sprites/plunger.png` | `make_sprites.py` | |
| `packaging/macos/icon_1024.png` | `make_icon.py` | 1024x1024. The application icon, not a game asset: it is outside `assets` and the manifest. Needs no layout file |

## The application icon

The macOS icon is a chrome pinball over the slatted sunset and the neon grid floor, framed by a neon tube that runs from pink to cyan, on the standard macOS rounded square (824 pixels centred in 1024, transparent outside, with a soft drop shadow). It has no text. `make_icon.py` draws it with the same helpers and export settings as the assets; it reads no layout file, so it can be run on its own. `tools/make_icns.sh` then scales the PNG with `sips` to every standard size (16 to 512 points, at 1x and 2x) and packs them with `iconutil` into `packaging/macos/AppIcon.icns`. Both files are committed, so a build needs neither GIMP nor the script:

```python
REPO = "/path/to/space-cadet-pinball"
exec(open(REPO + "/tools/gimp/make_icon.py").read())
```

```sh
tools/make_icns.sh
```

The playfield (for now), bumper and slingshot flashes, all text and the About and initials dialogs, with their frames, zigzags and the small sunset, are drawn by the game (`src/platform/TableRenderer.cpp`, `Hud.cpp`, `Text.cpp`). Their colours match the rims and inserts in the art; if you change a palette in one place, change it in the other.

## The Canva source

The sunset illustration behind the title on the panel was generated with Canva's AI design tools and exported as a PNG. It is kept at `tools/gimp/sources/backglass.png` (1920x900: the export with the bottom rows cropped away, re-exported from GIMP with all metadata removed). `make_panel.py` scales it to the panel width, raises its chroma, fades it into the panel below the title and draws everything else over it. That file cannot be regenerated by a script, so `ui/panel.png` is reproducible only given that source. No other asset contains pixels from outside the scripts. The source must stay free of metadata too: `python3 tools/check_assets.py tools/gimp/sources`.

## Reproducibility

Regeneration is reproducible: two-colour gradients are drawn without dithering (GIMP's dither is randomly seeded), noise, stars and skyline use fixed seeds, and exports carry no timestamps. Running the scripts twice gives byte-identical files, so the hashes in the asset manifest only change when the art does.

Two GIMP behaviours the helpers in `tools/gimp/common.py` work around: a selection fill ignores the alpha of the foreground colour (`fill_selection` applies it as the fill opacity), and a two-colour gradient ignores alpha altogether (`alpha_gradient` fades to transparent instead).

To look at the result without opening a window: `cd build && ./pinball_snapshot 1200 frame.png` (the tool is at the top of the build directory on every system) plays 1200 steps (ten seconds) and writes the frame to the current directory, as a new file with ordinary permissions (0644 less the umask, unlike the owner-only score file); a link with that name is replaced, not followed. A third argument, `about` or `initials`, draws that screen instead.

## Integrity

Assets are external files, so the game checks them before it runs:

- At build time CMake records the size and SHA-256 of every PNG under `assets/`, applying the same name and size rules the game enforces, and compiles that manifest into the executable (`cmake/GenerateAssetManifest.cmake`).
- At startup every listed asset is read into memory and compared with the manifest. Any missing, resized or altered file, or anything that is not a regular file, stops the game with exit code 1 before a window opens.
- Each image is then decoded from the same bytes that were hashed, so a file cannot be swapped between the check and the use.
- The manifest is also the allowlist. A file that is not in it is never opened.
- `ctest` runs `tools/test_asset_integrity.py`, which flips a bit, appends, truncates, empties and deletes each asset, and replaces it with a FIFO, a symbolic link to a genuine copy and a directory, and confirms every case is refused.

What this does not cover: the hashes live in the executable, so someone able to replace the executable can replace them too. That is the job of code signing at release time (`codesign` and notarisation on macOS), which protects the executable and with it the manifest. A signed macOS bundle also seals the asset files themselves, but the system checks that seal only when Gatekeeper assesses the app, not at every start; the game's own check runs every time.

The game only opens files inside the `assets` directory next to the executable (`Contents/Resources/assets` in the macOS bundle: the fixed directory under SDL's base path). Files over 16 MB or images over 4096 pixels on an edge are refused before decoding.

Assets are exported with all metadata off. `tools/check_assets.py` (run by `ctest`) fails if a PNG carries EXIF, text, timestamp, profile or thumbnail chunks, is malformed, or if anything but a regular `.png` file is in `assets`.
