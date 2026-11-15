# Checks the Table A masters in tools/art/table_a/out:
#  1. every file listed in tools/art/table_a/SHA256SUMS has that hash, and every PNG in out/ is listed;
#  2. every PNG holds IHDR, IDAT and IEND only (no text, time, profile or EXIF), 8-bit RGBA, no edge over
#     4096 pixels (the game's own limits, tools/check_assets.py);
#  3. sheet sizes match out/sheets.json and out/gimp_sheets.json;
#  4. floor pieces (flush with the playfield, under ball paths) keep to the deck's luminance budget
#     (tools/art/palette.py LUM_DECK_MAX for paint, LUM_STATIC_MAX for baked highlights, LUM_UNLIT_LENS
#     for unlit lenses): reported per sheet. The check fails, in an unlit frame, if the 75th percentile of
#     opaque pixels (the paint) is over LUM_DECK_MAX, or the 99.5th (chrome rims, the hub) over LUM_STATIC_MAX.
#     Specular glints on chrome, under half a percent of a frame, are the only pixels allowed above it;
#  5. palette.py TABLE_A (and ALTITUDE_GI) match blender/toys.py HEX.
# Needs Python 3 with numpy and Pillow (the art venv).   python3 tools/art/table_a/check.py [--report]
import hashlib
import json
import os
import struct
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
OUT = os.path.join(HERE, "out")
LUM_DECK_MAX = 0.060
LUM_STATIC_MAX = 0.45
# sheet -> frames that are flush with the playfield and unlit
FLOOR = {"propwash": None, "propwash_blur": None, "propwash_ring": None, "flight_deck": ["off"],
         "altitude_plate": ["base"]}
# whole images flush with the playfield, checked inside the shell (layout_a.json)
FLOOR_IMAGES = {"floor_a.png": 2}
OTHER_IMAGES = ["background_a.png", "gi_a.png"]


def chunks(path):
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        return None, None
    pos, names, ihdr = 8, [], None
    while pos < len(data):
        n, typ = struct.unpack(">I4s", data[pos:pos + 8])
        if typ == b"IHDR":
            ihdr = struct.unpack(">IIBBBBB", data[pos + 8:pos + 8 + 13])
        names.append(typ.decode("latin-1"))
        pos += 12 + n
    return names, ihdr


def luminance(rgb):
    c = rgb.astype(np.float64) / 255.0
    c = np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)
    return 0.2126 * c[..., 0] + 0.7152 * c[..., 1] + 0.0722 * c[..., 2]


def frames_of(meta, name, arr):
    sh = meta["sheets"][name]
    cw, ch = sh["cellPixels"]
    for i, label in enumerate(sh["frames"]):
        r, c = divmod(i, sh["columns"])
        yield label, arr[r * ch:(r + 1) * ch, c * cw:(c + 1) * cw]


def main():
    report = "--report" in sys.argv
    problems = []
    listed = {}
    with open(os.path.join(HERE, "SHA256SUMS")) as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            digest, name = line.rstrip("\n").split("  ", 1)
            listed[name] = digest
    for name, digest in sorted(listed.items()):
        path = os.path.join(ROOT, name)
        if not os.path.isfile(path):
            problems.append("missing: " + name)
        elif hashlib.sha256(open(path, "rb").read()).hexdigest() != digest:
            problems.append("differs: " + name)
    for file in sorted(os.listdir(OUT)):
        rel = "tools/art/table_a/out/" + file
        if file.endswith(".png") and rel not in listed:
            problems.append("not in SHA256SUMS: " + rel)
    meta = {"sheets": {}}
    for name in ("sheets.json", "gimp_sheets.json"):
        meta["sheets"].update(json.load(open(os.path.join(OUT, name)))["sheets"])
    for name, sh in sorted(meta["sheets"].items()):
        path = os.path.join(OUT, sh["file"])
        names, ihdr = chunks(path)
        if names is None:
            problems.append("not a PNG: " + sh["file"])
            continue
        if sorted(set(names)) != ["IDAT", "IEND", "IHDR"]:
            problems.append("%s: chunks %s" % (sh["file"], sorted(set(names))))
        w, h, depth, ctype = ihdr[0], ihdr[1], ihdr[2], ihdr[3]
        if depth != 8 or ctype != 6:
            problems.append("%s: not 8-bit RGBA" % sh["file"])
        if max(w, h) > 4096:
            problems.append("%s: %dx%d over 4096" % (sh["file"], w, h))
        n = len(sh["frames"])
        rows = (n + sh["columns"] - 1) // sh["columns"]
        want = (sh["columns"] * sh["cellPixels"][0], rows * sh["cellPixels"][1])
        if (w, h) != want:
            problems.append("%s: %dx%d, sheets.json says %dx%d" % (sh["file"], w, h, want[0], want[1]))
        if name in FLOOR:
            arr = np.asarray(Image.open(path).convert("RGBA"))
            for label, cell in frames_of(meta, name, arr):
                if FLOOR[name] is not None and label not in FLOOR[name]:
                    continue
                opaque = cell[..., 3] >= 250
                lum = luminance(cell[..., :3])[opaque]
                if lum.size == 0:
                    continue
                p50, p75, p995 = np.percentile(lum, [50, 75, 99.5])
                if report:
                    print("%-12s %-8s lum p50 %.3f p75 %.3f p99.5 %.3f" % (name, label, p50, p75, p995))
                if p75 > LUM_DECK_MAX:
                    problems.append("%s %s: p75 luminance %.3f over %.3f" % (name, label, p75, LUM_DECK_MAX))
                if p995 > LUM_STATIC_MAX:
                    problems.append("%s %s: p99.5 luminance %.3f over %.3f" % (name, label, p995, LUM_STATIC_MAX))
    shell = json.load(open(os.path.join(HERE, "layout_a.json")))["shell"]
    for file in list(FLOOR_IMAGES) + OTHER_IMAGES:
        path = os.path.join(OUT, file)
        names, ihdr = chunks(path)
        if names is None or sorted(set(names)) != ["IDAT", "IEND", "IHDR"]:
            problems.append("%s: not a clean PNG" % file)
            continue
        if max(ihdr[0], ihdr[1]) > 4096:
            problems.append("%s: over 4096" % file)
        if file in FLOOR_IMAGES:
            from PIL import ImageDraw
            k = FLOOR_IMAGES[file]
            arr = np.asarray(Image.open(path).convert("RGB"))
            mask = Image.new("L", (arr.shape[1], arr.shape[0]), 0)
            ImageDraw.Draw(mask).polygon([(x * k, y * k) for x, y in shell], fill=255)
            lum = luminance(arr)[np.asarray(mask) > 0]
            p50, p75, p995 = np.percentile(lum, [50, 75, 99.5])
            if report:
                print("%-12s %-8s lum p50 %.3f p75 %.3f p99.5 %.3f" % (file, "shell", p50, p75, p995))
            if p75 > LUM_DECK_MAX or p995 > LUM_STATIC_MAX:
                problems.append("%s: p75 %.3f / p99.5 %.3f over the floor budget" % (file, p75, p995))
    # 5. the Table A colours in palette.py (what the game and the floor use) are the ones Blender rendered with
    sys.path.insert(0, os.path.join(ROOT, "tools", "art"))
    import palette as P
    toys = open(os.path.join(HERE, "blender", "toys.py")).read()
    for name, colour in list(P.TABLE_A.items()) + list(zip(P.TABLE_A_GI_NAMES, P.ALTITUDE_GI)):
        if '"%s": "%s"' % (name, colour) not in toys:
            problems.append("palette.py TABLE_A %s %s differs from blender/toys.py HEX" % (name, colour))
    if problems:
        sys.exit("\n".join(problems))
    print("table_a check: %d file(s) ok" % len(listed))


if __name__ == "__main__":
    main()
