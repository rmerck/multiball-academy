# tools/art/table_a: Table A (Flight Academy) art masters, phase 1

These are the masters for concept A's toys and finishes. They are not shipped yet: nothing here is under
`assets/` or in the game's asset manifest. Renderer integration comes after the geometry gate G1, and moves the
chosen files into `assets/` and `tools/art/SHA256SUMS`. The style board is kept outside the repository
(`board/ios/art-A/STYLE.md`).

| Path | What |
|---|---|
| `blender/toys.py` | Models, lights and renders every toy (Cycles, CPU, fixed seed, no denoiser, 8 threads), then packs the sprite sheets. Each state is a keyframe, so a `.blend` alone holds every frame |
| `blender/*.blend` | The saved scenes (sources), one per sheet |
| `gimp/finish.py` | GIMP 3.2 finishing pass, headless: F-L-A-P-S inserts, Altitude ladder and plate, shot arrows, sling plastics, nameplates |
| `gimp/xcf/*.xcf` | The layered sources of the GIMP pass |
| `out/*.png` | The masters: 8-bit RGBA, straight alpha, IHDR/IDAT/IEND only, 4 pixels per playfield unit |
| `out/sheets.json`, `out/gimp_sheets.json` | Per sheet: cell size (units and pixels), columns, frame labels in order, anchor (`"centre"` = centred on the toy; `[x, y]` = the cell's top-left corner in playfield units) |
| `layout_a.json` | Snapshot of `pinball_layout` from `table/a-geometry` (see `_source`); the ramps, G-FORCE and plastics are drawn from it. Refresh it when the geometry moves |
| `check.py` | Hashes against `SHA256SUMS`, PNG chunks and sizes, and the floor-luminance budget for flush pieces |
| `contact.py` | Review contact sheet (every sheet plus an in-place preview); not shipped |

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
python3 tools/art/table_a/check.py --report
```

Reproducibility, as verified on the reference Mac (Blender 5.3.0, GIMP 3.2):
- Building from the script and re-rendering from the saved `.blend` files give byte-identical sheets.
- Two GIMP runs give byte-identical PNGs.
- Other platforms or versions are expected to differ in anti-aliased edges, as with skia (`docs/ASSETS.md`).
  The committed files are the reference.

`GIMP3_DIRECTORY` points GIMP at a throwaway profile, so the user's GIMP settings are never read or written.
The `.xcf` files and the `.blend` files are sources: their hashes are recorded, but re-saving them is not
expected to give the same bytes.

Fonts: only the SIL OFL fonts in `tools/art/sources/fonts`. No image, texture or element comes from outside
these scripts.
