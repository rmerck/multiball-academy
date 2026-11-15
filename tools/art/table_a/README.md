# tools/art/table_a: Table A (Flight Academy) art masters

These are the masters for concept A's toys and finishes. `ship.py` moves the ones the game draws into
`assets/table_a/` at two pixels per unit (listed in `tools/art/SHA256SUMS`); `tools/art/gen_header.py` copies
their sheet layout and the lamp plan into `src/generated/ArtPalette.hpp` for the renderer. The style board is kept
outside the repository (`board/ios/art-A/STYLE.md`).

| Path | What |
|---|---|
| `blender/toys.py` | Models, lights and renders every toy (Cycles, CPU, fixed seed, no denoiser, 8 threads), then packs the sprite sheets. Each state is a keyframe, so a `.blend` alone holds every frame |
| `blender/*.blend` | The saved scenes (sources), one per sheet |
| `gimp/finish.py` | GIMP 3.2 finishing pass, headless: F-L-A-P-S inserts, Altitude ladder and plate, shot arrows, sling plastics, nameplates |
| `gimp/xcf/*.xcf` | The layered sources of the GIMP pass |
| `out/*.png` | The masters: 8-bit RGBA, straight alpha, IHDR/IDAT/IEND only, 4 pixels per playfield unit |
| `out/sheets.json`, `out/gimp_sheets.json` | Per sheet: cell size (units and pixels), columns, frame labels in order, anchor (`"centre"` = centred on the toy; `[x, y]` = the cell's top-left corner in playfield units) |
| `layout_a.json` | Snapshot of `pinball_layout` from `table/a-geometry` (see `_source`); the ramps, G-FORCE and plastics are drawn from it. Refresh it when the geometry moves |
| `make_floor.py` | Playfield base (skia, the pinned art venv, Linux): `floor_a.png` (flush only), `background_a.png` (plus raised static parts), `gi_a.png` (greyscale GI light map), `lamp_plan.json` (98 inserts: 58 from `kLamps`, 40 proposed). Needs the Blender/GIMP masters in `out/` |
| `check.py` | Hashes against `SHA256SUMS`, PNG chunks and sizes, and the floor-luminance budget for flush pieces and `floor_a.png` |
| `composite.py` | Full-table preview at iPhone 18 Pro width (1206 px), `fl100` or `fl500` sample state; not shipped. `render()` is also the table `tools/art/visibility_full.py --table-a` measures the ball against |
| `ship.py` | The shipped copies: `assets/table_a/` (sheets halved to 2 px per unit on premultiplied alpha; `background.png` and `gi.png` copied) |
| `contact.py` | Review contact sheet (every sheet, the floor images, a composite); not shipped |

## Regenerate (macOS arm64, where the reference was made)

```sh
B=/Applications/Blender.app/Contents/MacOS/Blender
$B --background --factory-startup --python tools/art/table_a/blender/toys.py -- --samples 128
# or one sheet from its committed .blend:
$B --background --factory-startup tools/art/table_a/blender/drone.blend \
   --python tools/art/table_a/blender/toys.py -- --from-blend --toy drone
R=$PWD; printf '(font-path "%s")\n' "$R/tools/art/sources/fonts" > /tmp/table_a.gimprc
GIMP3_DIRECTORY=$(mktemp -d) /Applications/GIMP.app/Contents/MacOS/gimp-console-3.2 -i -n \
   --gimprc=/tmp/table_a.gimprc --batch-interpreter=python-fu-eval \
   -b "TABLE_A_ROOT='$R'; exec(open('$R/tools/art/table_a/gimp/finish.py').read())" --quit
# then, on Linux with the pinned venv (tools/art/README.md):
build/art-venv/bin/python tools/art/table_a/make_floor.py
build/art-venv/bin/python tools/art/table_a/check.py --report
build/art-venv/bin/python tools/art/table_a/composite.py /tmp/composite_fl500.png fl500
build/art-venv/bin/python tools/art/table_a/contact.py /tmp/contact.png /tmp/composite_fl500.png
build/art-venv/bin/python tools/art/table_a/ship.py      # assets/table_a
python3 tools/art/gen_header.py                          # src/generated/ArtPalette.hpp
# then update tools/art/SHA256SUMS; gates:
python3 tools/art/check_sums.py
(cd tools/art && ../../build/art-venv/bin/python visibility_full.py build/art-review --fx --table-a)   # FL500
(cd tools/art && ../../build/art-venv/bin/python visibility_full.py build/art-review --fx --table-a --fl100)
```

Reproducibility, as verified on the reference Mac (Blender 5.3.0, GIMP 3.2):
- Building from the script and re-rendering from the saved `.blend` files give byte-identical sheets.
- Two GIMP runs give byte-identical PNGs.
- Two `make_floor.py` runs (Linux x86_64, pinned venv) give byte-identical files.
- Other platforms or versions are expected to differ in anti-aliased edges, as with skia (`docs/ASSETS.md`).
  The committed files are the reference.

`GIMP3_DIRECTORY` points GIMP at a throwaway profile, so the user's GIMP settings are never read or written.
The `.xcf` files and the `.blend` files are sources: their hashes are recorded, but re-saving them is not
expected to give the same bytes.

Fonts: only the SIL OFL fonts in `tools/art/sources/fonts`. No image, texture or element comes from outside
these scripts.
