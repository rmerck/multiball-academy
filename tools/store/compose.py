#!/usr/bin/env python3
"""Frames raw simulator screenshots for the App Store (ART_DIRECTION section 12).

Each output is the real screen, unchanged apart from scaling, placed on the
game's backdrop tile under a slanted caption bar in the title's lettering,
with a halftone corner. The output has exactly one of the pixel sizes App
Store Connect accepts for its set, is RGB (no alpha) and is written as a PNG
with the IHDR, IDAT and IEND chunks only: no text, time, gamma, profile or
EXIF. The same inputs give the same bytes (no randomness, no clock).

    compose.py one --set iphone-6.3 --caption "THREE BALLS AT ONCE" --bar pink \
        --input raw.png --output framed.png
    compose.py batch --raw <dir> --out <dir> [--shots tools/store/shots.txt]
        <dir>/<set>/<name>.png for each set and each shot in shots.txt that
        has a raw frame; missing frames are reported and skipped.
    compose.py check <png>...
        size, mode and chunks of finished files.

Needs skia-python, numpy and Pillow (the versions pinned for the art
scripts, tools/art/requirements.txt), the backdrop tile assets/ui/backdrop.png
and the title face tools/art/sources/fonts/RussoOne-Regular.ttf; both paths
can be given instead.
"""
import argparse
import math
import os
import struct
import sys
import zlib

import numpy as np
import skia
from PIL import Image

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
DEFAULT_BACKDROP = os.path.join(ROOT, "assets", "ui", "backdrop.png")
DEFAULT_FONT = os.path.join(ROOT, "tools", "art", "sources", "fonts", "RussoOne-Regular.ttf")
DEFAULT_SHOTS = os.path.join(ROOT, "tools", "store", "shots.txt")

# App Store Connect screenshot sets (portrait width, height), from
# https://developer.apple.com/help/app-store-connect/reference/app-information/screenshot-specifications/
# A landscape raw frame gives the same set in landscape. trim: rows of the raw
# capture (top, bottom), in points, that hold only the backdrop band outside
# the safe area; the point-to-pixel scale is taken from the raw frame's width.
SETS = {
    "iphone-6.3": {"size": (1206, 2622), "points": 402, "trim": (56, 30), "what": "iPhone, Dynamic Island, medium (required)"},
    "iphone-6.9": {"size": (1320, 2868), "points": 440, "trim": (56, 30), "what": "iPhone, Dynamic Island, large"},
    "ipad-13": {"size": (2064, 2752), "points": 1032, "trim": (0, 0), "what": "iPad 13-inch (required)"},
    "duo-outer": {"size": (1398, 2034), "points": None, "trim": (0, 0), "what": "iPhone Duo, outer display"},
    "duo-inner": {"size": (2007, 2853), "points": None, "trim": (0, 0), "what": "iPhone Duo, inner display"},
}

# Palette (art: tools/art/palette.py).
INK = "#0A0920"
NIGHT = "#12123A"
PINK = "#FF2E88"
YELLOW = "#FFD23C"
TEAL = "#00A7A5"
VIOLET = "#6A35C9"
GOLD_HI = "#FFFBE0"
GOLD = "#FFD23C"
GOLD_LO = "#A85A10"
CH_HI, CH_MID, CH_LO = "#F4FAFF", "#9FB4CC", "#2B3550"
WHITE = "#FFFFFF"

BARS = {
    # bar gradient top to bottom, letter fill, letter extrusion
    "pink": (["#FF6FB0", PINK, "#B0105C"],
             ([GOLD_HI, GOLD, "#D9861A", "#FFF3B0", GOLD_LO], [0, 0.35, 0.5, 0.54, 1]), "#3A0A24"),
    "yellow": (["#FFE98A", YELLOW, "#D99A10"],
               ([WHITE, CH_HI, CH_MID, "#E8F4FF", CH_LO], [0, 0.35, 0.5, 0.54, 1]), "#1A1450"),
}

TILT_DEGREES = 4.0


def rgb(h):
    h = h.lstrip("#")
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def colour(h, a=255):
    r, g, b = rgb(h)
    return skia.Color(r, g, b, a)


def fill(c):
    p = skia.Paint(AntiAlias=True)
    p.setColor(c)
    return p


def stroke(c, w):
    p = skia.Paint(AntiAlias=True, Style=skia.Paint.kStroke_Style, StrokeWidth=w)
    p.setStrokeJoin(skia.Paint.kRound_Join)
    p.setColor(c)
    return p


def vertical(y0, y1, cols, pos=None):
    p = skia.Paint(AntiAlias=True)
    p.setShader(skia.GradientShader.MakeLinear([(0, y0), (0, y1)], [colour(c) for c in cols], pos))
    return p


def text_path(text, typeface, size, cx, baseline, tracking):
    f = skia.Font(typeface, size)
    f.setSubpixel(True)
    f.setEdging(skia.Font.Edging.kAntiAlias)
    f.setHinting(skia.FontHinting.kNone)
    glyphs = f.textToGlyphs(text)
    widths = f.getWidths(glyphs)
    total = sum(widths) + tracking * (len(glyphs) - 1)
    x = cx - total / 2
    out = skia.Path()
    for g, w in zip(glyphs, widths):
        p = f.getPath(g)
        if p is not None:
            p.offset(x, baseline)
            out.addPath(p)
        x += w + tracking
    return out, total


def fitted_caption(text, typeface, width, height, tracking):
    size = height
    while size > 8:
        path, total = text_path(text, typeface, size, 0, 0, tracking)
        b = path.getBounds()
        if total <= width and b.height() <= height:
            return size
        size -= 1.0
    raise SystemExit("caption does not fit: " + text)


def to_image(surface):
    arr = surface.toarray(colorType=skia.kRGBA_8888_ColorType)
    return Image.fromarray(np.ascontiguousarray(arr[..., :4]), "RGBA")


def tile_backdrop(tile, w, h, scale):
    tw = max(1, int(round(tile.width * scale)))
    th = max(1, int(round(tile.height * scale)))
    t = tile.convert("RGB").resize((tw, th), Image.LANCZOS) if (tw, th) != tile.size else tile.convert("RGB")
    out = Image.new("RGB", (w, h))
    for y in range(0, h, th):
        for x in range(0, w, tw):
            out.paste(t, (x, y))
    return out


def halftone(w, h, k, corner):
    """Dots fading from one corner: teal over the backdrop, under the screen."""
    s = skia.Surface(w, h)
    c = s.getCanvas()
    c.clear(skia.Color(0, 0, 0, 0))
    step = 26.0 * k
    reach = 0.42 * min(w, h)
    cx, cy = corner
    n = int(reach / step) + 2
    for j in range(-n, n + 1):
        for i in range(-n, n + 1):
            x, y = cx + i * step, cy + j * step
            d = math.hypot(x - cx, y - cy)
            if d >= reach or not (0 <= x <= w and 0 <= y <= h):
                continue
            r = (1.0 - d / reach) * step * 0.42
            if r > 0.6:
                c.drawCircle(x, y, r, fill(colour(TEAL, 120)))
    return to_image(s)


def caption_band(w, k, caption, bar, typeface):
    """The slanted bar and its lettering on a transparent band, w wide."""
    band_h = int(round(600 * k))
    s = skia.Surface(w, band_h)
    c = s.getCanvas()
    c.clear(skia.Color(0, 0, 0, 0))
    bar_cols, (fill_cols, fill_pos), extrude = BARS[bar]
    mid = 280 * k
    thick = 270 * k
    rise = (w + 40 * k) * math.tan(math.radians(TILT_DEGREES))
    left_top = mid - thick / 2 + rise / 2
    right_top = mid - thick / 2 - rise / 2
    poly = skia.Path()
    poly.moveTo(-20 * k, left_top)
    poly.lineTo(w + 20 * k, right_top)
    poly.lineTo(w + 20 * k, right_top + thick)
    poly.lineTo(-20 * k, left_top + thick)
    poly.close()
    c.save()
    c.translate(14 * k, 16 * k)
    c.drawPath(poly, fill(colour(INK, 230)))
    c.restore()
    c.drawPath(poly, vertical(right_top, left_top + thick, bar_cols))
    c.drawPath(poly, stroke(colour(INK), 6 * k))

    tracking = 4 * k
    size = fitted_caption(caption, typeface, w - 220 * k, 150 * k, tracking)
    path, _ = text_path(caption, typeface, size, w / 2, 0, tracking)
    b = path.getBounds()
    path.offset(0, mid - (b.top() + b.bottom()) / 2)
    b = path.getBounds()
    c.save()
    c.rotate(-TILT_DEGREES, w / 2, mid)
    depth = max(2, int(round(10 * k)))
    for dd in range(depth, 0, -1):
        c.save()
        c.translate(dd * 0.7, dd * 1.0)
        c.drawPath(path, fill(colour(extrude)))
        c.restore()
    c.drawPath(path, stroke(colour(INK), 5 * k))
    c.drawPath(path, vertical(b.top(), b.bottom(), fill_cols, fill_pos))
    c.restore()
    return to_image(s), band_h


def compose(raw, set_name, caption, bar, backdrop, typeface, trim=None):
    spec = SETS[set_name]
    pw, ph = spec["size"]
    landscape = raw.width > raw.height
    w, h = (ph, pw) if landscape else (pw, ph)
    # One measure for everything drawn: the 6.9-inch portrait frame is 1.
    k = min(min(w, h) / 1290.0, max(w, h) / 2796.0)

    screen = raw.convert("RGB")
    top, bottom = trim if trim is not None else spec["trim"]
    if spec["points"] and not landscape and (top or bottom):
        ppp = screen.width / float(spec["points"])
        t, bt = int(round(top * ppp)), int(round(bottom * ppp))
        if t + bt < screen.height // 3:
            screen = screen.crop((0, t, screen.width, screen.height - bt))

    out = tile_backdrop(backdrop, w, h, k)
    corner = halftone(w, h, k, (w, h))
    out.paste(corner, (0, 0), corner)

    band, band_h = caption_band(w, k, caption, bar, typeface)
    top_y = int(round(30 * k))

    margin = int(round(70 * k))
    box_top = top_y + band_h - int(round(40 * k))
    box_w = w - 2 * margin
    box_h = h - box_top - margin
    border = max(2, int(round(14 * k)))
    inner_w, inner_h = box_w - 2 * border, box_h - 2 * border
    scale = min(inner_w / screen.width, inner_h / screen.height)
    sw, sh = int(round(screen.width * scale)), int(round(screen.height * scale))
    shown = screen.resize((sw, sh), Image.LANCZOS) if (sw, sh) != screen.size else screen
    fx = (w - sw) // 2
    fy = box_top + border + (inner_h - sh) // 2
    edge = Image.new("RGB", (sw + 2 * border, sh + 2 * border), rgb(CH_MID))
    ink = max(1, int(round(6 * k)))
    edge.paste(Image.new("RGB", (sw + 2 * (border - ink), sh + 2 * (border - ink)), rgb(INK)), (ink, ink))
    shadow = Image.new("RGBA", (edge.width, edge.height), rgb(INK) + (200,))
    out.paste(shadow, (fx - border + int(14 * k), fy - border + int(16 * k)), shadow)
    out.paste(edge, (fx - border, fy - border))
    out.paste(shown, (fx, fy))
    # The bar goes over the top of the frame, as a sticker would.
    out.paste(band, (0, top_y), band)
    return out


def png_chunks(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit("not a PNG: " + path)
    i, names = 8, []
    while i < len(data):
        n = struct.unpack(">I", data[i:i + 4])[0]
        names.append(data[i + 4:i + 8].decode("ascii"))
        i += 12 + n
    return names


def save_clean(im, path):
    """RGB PNG with IHDR, IDAT and IEND only."""
    clean = Image.new("RGB", im.size)
    clean.paste(im.convert("RGB"))
    tmp = path + ".tmp"
    clean.save(tmp, "PNG", optimize=False, compress_level=9)
    os.replace(tmp, path)
    check(path)


def accepted_sizes():
    sizes = {}
    for name, spec in SETS.items():
        w, h = spec["size"]
        sizes[(w, h)] = name
        sizes[(h, w)] = name + " (landscape)"
    return sizes


def check(path):
    with Image.open(path) as im:
        size, mode = im.size, im.mode
    chunks = [c for c in png_chunks(path) if c != "IDAT"]
    problems = []
    if chunks != ["IHDR", "IEND"]:
        problems.append("chunks other than IHDR, IDAT, IEND: %s" % chunks)
    if mode != "RGB":
        problems.append("mode %s, not RGB" % mode)
    sizes = accepted_sizes()
    if size not in sizes:
        problems.append("size %dx%d is not an App Store screenshot size" % size)
    if problems:
        raise SystemExit("%s: %s" % (path, "; ".join(problems)))
    return "%s: %dx%d %s, RGB, IHDR/IDAT/IEND only" % (path, size[0], size[1], sizes[size])


def read_shots(path):
    shots = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = [p.strip() for p in line.split("|")]
            if len(parts) < 3 or parts[2] not in BARS:
                raise SystemExit("bad line in %s: %s" % (path, line))
            shots.append((parts[0], parts[1], parts[2]))
    return shots


def load_inputs(args):
    if not os.path.isfile(args.backdrop):
        raise SystemExit("backdrop tile not found: %s (give --backdrop)" % args.backdrop)
    if not os.path.isfile(args.font):
        raise SystemExit("title face not found: %s (give --font)" % args.font)
    typeface = skia.Typeface.MakeFromFile(args.font)
    if typeface is None:
        raise SystemExit("cannot read the face: " + args.font)
    return Image.open(args.backdrop).convert("RGB"), typeface


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("one", "batch"):
        p = sub.add_parser(name)
        p.add_argument("--backdrop", default=DEFAULT_BACKDROP)
        p.add_argument("--font", default=DEFAULT_FONT)
        p.add_argument("--trim", type=float, nargs=2, metavar=("TOP", "BOTTOM"),
                       help="points to cut from the raw frame (default: per set)")
    one = sub.choices["one"]
    one.add_argument("--set", required=True, choices=sorted(SETS))
    one.add_argument("--caption", required=True)
    one.add_argument("--bar", required=True, choices=sorted(BARS))
    one.add_argument("--input", required=True)
    one.add_argument("--output", required=True)
    batch = sub.choices["batch"]
    batch.add_argument("--raw", required=True)
    batch.add_argument("--out", required=True)
    batch.add_argument("--shots", default=DEFAULT_SHOTS)
    chk = sub.add_parser("check")
    chk.add_argument("png", nargs="+")
    args = ap.parse_args(argv)

    if args.cmd == "check":
        for p in args.png:
            print(check(p))
        return 0
    backdrop, typeface = load_inputs(args)
    trim = tuple(args.trim) if args.trim else None
    if args.cmd == "one":
        with Image.open(args.input) as raw:
            im = compose(raw, args.set, args.caption, args.bar, backdrop, typeface, trim)
        save_clean(im, args.output)
        print(check(args.output))
        return 0
    shots = read_shots(args.shots)
    done = missing = 0
    for set_name in sorted(SETS):
        for index, (name, caption, bar) in enumerate(shots, 1):
            raw_path = os.path.join(args.raw, set_name, name + ".png")
            if not os.path.isfile(raw_path):
                missing += 1
                continue
            os.makedirs(os.path.join(args.out, set_name), exist_ok=True)
            out_path = os.path.join(args.out, set_name, "%02d-%s.png" % (index, name))
            with Image.open(raw_path) as raw:
                im = compose(raw, set_name, caption, bar, backdrop, typeface, trim)
            save_clean(im, out_path)
            print(check(out_path))
            done += 1
    print("compose: %d framed, %d set/shot pairs without a raw frame" % (done, missing))
    return 0 if done else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
