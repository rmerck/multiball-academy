# tools/art: how the art is made

All game art is drawn by these Python scripts (skia-python, numpy, Pillow), from the same geometry the
physics uses. No image editor, no generated painting, no pixels from outside the scripts. The full account
(what each file is, how to regenerate, reproducibility, verification) is in `docs/ASSETS.md`.

    python3 -m venv build/art-venv
    build/art-venv/bin/pip install -r tools/art/requirements.txt
    build/pinball_layout > build/layout.json
    build/art-venv/bin/python tools/art/make_all.py      # assets/, both icons, src/generated/ArtPalette.hpp
    python3 tools/check_assets.py assets

- `palette.py`: every colour and art constant. `gen_header.py` writes the part the renderer needs to
  `src/generated/ArtPalette.hpp`; `gen_header.py --check` (ctest `art_palette`) fails if the two disagree.
  It needs only Python.
- `common.py`: canvas, gradients, deck plates, ordered dither, luminance, metadata-free PNG writer.
- `make_sprites.py`, `make_table.py`, `make_ui.py`, `make_icons.py`: the generators. Each can be run alone
  with an output path. `make_table.py` bakes the unlit lamp inserts from `assets/sprites`, so run
  `make_sprites.py` first (`make_all.py` does).
- `hud_layout.py`: the display windows PROPOSED for the HUD rework. Nothing uses it yet: `make_ui.py`
  draws the windows where the game has them (`strip` and `panel` in `layout.json`).
- `render_sim.py`: draws the table as `TableRenderer` does, from the shipped files.
- `visibility_full.py`: the ball-visibility test over the whole playfield at five device scales
  (writes `build/art-review/visibility_full.md` and `.png`).
- `sources/fonts`: Russo One, Audiowide, Saira Stencil One and Silkscreen, each with its SIL Open Font
  Licence text (`OFL-*.txt`). Only outlines drawn from them end up in images; no font file ships in the
  game.

Geometry and sizes (ball radius, flipper, plunger, every wall, lamp and window, the app name on the title)
are read from `layout.json`, so art and physics cannot disagree. Set `PINBALL_LAYOUT` to use another file.
Lamp inserts are baked into the table background: a lamp that moves in `src/core/TableLayout.hpp` needs
`make_all.py` run again.

Reproducible: fixed seeds, no timestamps, PNGs hold IHDR, IDAT and IEND only, and the versions in
`requirements.txt` are pinned. The committed files are the reference; regeneration is a check, not a build
step (`docs/ASSETS.md`, "Reproducibility").
