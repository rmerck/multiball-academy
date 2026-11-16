# Moves the chosen Table A masters into the game's assets: assets/table_a/, at the game's two pixels per unit.
#   build/art-venv/bin/python tools/art/table_a/ship.py [root]
# The sheets in out/ are drawn at four pixels per unit; each is halved by a 2 x 2 box on premultiplied alpha (no
# dark fringes, the same bytes on every platform: plain integer arithmetic), keeping its cell layout, so
# sheets.json and gimp_sheets.json still describe it with cellPixels halved. background_a.png (two pixels per unit
# already) and gi_a.png (half a pixel per unit, the light map) are copied as they are. Pieces that are baked into
# background_a.png (the G-FORCE housing, the sling plastics, the Prop Wash's ground ring, the altimeter's base,
# the unlit dome bases and inserts) do not ship as sprites; the altimeter's pointer cells are mirrored when the
# floor bakes its plate mirrored. Then: tools/art/gen_header.py (the renderer's copy of
# the sheet layout and lamp plan) and tools/art/SHA256SUMS.
import json
import os
import shutil
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.normpath(os.path.join(HERE, "..")))
import palette as P  # noqa: E402
from common import save_png  # noqa: E402

OUT = os.path.join(HERE, "out")
# out/<sheet>.png -> assets/table_a/<sheet>.png, halved
SHEETS = ["drone", "propwash", "propwash_blur", "pylon", "bay_door", "flight_deck", "ramp_runway", "ramp_skyway",
          "jet_caps", "flasher_domes", "inserts_arrows", "inserts_flaps", "inserts_altitude", "altitude_plate"]
COPIED = {"background_a.png": "background.png", "gi_a.png": "gi.png"}


def halve(path):
    a = np.asarray(Image.open(path).convert("RGBA")).astype(np.int64)
    h, w = a.shape[0] // 2 * 2, a.shape[1] // 2 * 2
    a = a[:h, :w]
    alpha = a[..., 3:4]
    pre = a[..., :3] * alpha                                   # premultiplied, scaled by 255
    q = lambda x: x[0::2, 0::2] + x[1::2, 0::2] + x[0::2, 1::2] + x[1::2, 1::2]  # noqa: E731
    sa = q(alpha)                                              # 4 x the mean alpha
    sp = q(pre)
    rgb = np.where(sa > 0, (sp + sa // 2) // np.maximum(sa, 1), 0)
    out = np.concatenate([np.clip(rgb, 0, 255), (sa + 2) // 4], axis=2).astype(np.uint8)
    return Image.fromarray(out, "RGBA")


def mirror_cells(im, cell_w):
    """Each cell mirrored left to right in place (the cell order stays)."""
    a = np.asarray(im).copy()
    for x in range(0, a.shape[1] - cell_w + 1, cell_w):
        a[:, x:x + cell_w] = a[:, x:x + cell_w][:, ::-1]
    return Image.fromarray(a, "RGBA")


def main(root):
    dst = os.path.join(root, "assets", "table_a")
    os.makedirs(dst, exist_ok=True)
    gimp = json.load(open(os.path.join(OUT, "gimp_sheets.json")))["sheets"]
    for name in SHEETS:
        im = halve(os.path.join(OUT, name + ".png"))
        if name == "altitude_plate" and P.TABLE_A_ALT_PLATE_MIRROR:
            # The floor bakes the plate mirrored (palette.py TABLE_A_ALT_PLATE_MIRROR): its pointers follow.
            im = mirror_cells(im, gimp[name]["cellPixels"][0] // 2)
        save_png(im, os.path.join(dst, name + ".png"))
    for src, name in COPIED.items():
        shutil.copyfile(os.path.join(OUT, src), os.path.join(dst, name))
    print("ship: %d files in %s" % (len(SHEETS) + len(COPIED), dst))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else os.path.normpath(os.path.join(HERE, "..", "..", "..")))
