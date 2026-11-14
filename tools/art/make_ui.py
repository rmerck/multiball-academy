# Interface art: title lockup, Classic panel, Stack strip and deck, backdrop tile. 2 pixels per unit.
#   python3 make_ui.py [out_dir]      (default: ../../assets/ui)
import math
import os
import sys

import numpy as np
import skia
from PIL import Image

import palette as P
from common import (BAYER4, C, ROOT, Stage, blur, chrome_sweep, deck_plates, fill, fit_font, font, hexrgb, layout, lin,
                    mix, ordered_dither, plus, poly_path, rad, ramp, rrect, save_png, shade, soft_noise, stroke, text_path)

L = layout()
APP = L["screen"]["appName"].upper().split(" ", 1)
SCR = L["screen"]
# The display windows are drawn where the game has them today (strip:: and panel:: in the C++ headers, as
# pinball_layout prints them), so the art and the text in it cannot disagree. hud_layout.py holds the
# windows proposed for the HUD rework; when the headers take them, this art follows on regeneration.
STRIP_WELLS = [SCR["strip"][k] for k in ("scoreWell", "statusWell", "rankWell", "missionWell", "messageWell")]
STRIP_PAUSE = SCR["strip"]["pauseButton"]
PANEL_WELLS = L["panel"]["wells"]
PANEL_TITLE = SCR["panelAreas"]["title"]
PANEL_CONTROLS = SCR["panelAreas"]["controls"]


# ---- shared pieces ------------------------------------------------------------
def body(st, seed, spot):
    """The panel surface: the deck plates, a little lighter and warmer in hue than the playfield's."""
    xs, ys = st.grid()
    base = deck_plates(xs, ys, tile=32.0, origin=(0.0, 0.0), spot=spot, seed=seed)
    base = base * np.array([1.28, 1.02, 1.18], np.float32) + np.array([4, 0, 10], np.float32)
    st.put(base)
    return st.reopen()


def well(c, r, radius=7.0):
    """A display window: chrome lip, dark glass, shade under the top lip."""
    x, y, w, h = r
    c.drawRRect(rrect(x - 5.5, y - 4.5, w + 11, h + 11, radius + 5), blur(fill(C(P.INK, 200)), 2.5))
    c.drawRRect(rrect(x - 4, y - 4, w + 8, h + 8, radius + 4), shade(lin((x, y - 4), (x, y + h + 4), [C(P.CH_HI), C(P.CH_MID), C(P.CH_LO)], [0, 0.35, 1])))
    c.drawRRect(rrect(x - 4, y - 4, w + 8, h + 8, radius + 4), stroke(C(P.INK, 220), 0.8))
    c.drawRRect(rrect(x - 1.2, y - 1.2, w + 2.4, h + 2.4, radius + 1.2), fill(C(P.INK)))
    c.drawRRect(rrect(x, y, w, h, radius), fill(C(P.WELL)))
    c.save(); c.clipRRect(rrect(x, y, w, h, radius), skia.ClipOp.kIntersect, True)
    c.drawRect(skia.Rect.MakeXYWH(x, y, w, 9), shade(lin((x, y), (x, y + 9), [C("#000000", 150), C("#000000", 0)])))
    c.drawPath(poly_path([(x + w * 0.62, y), (x + w * 0.74, y), (x + w * 0.66, y + h), (x + w * 0.54, y + h)]), fill(C(P.WHITE, 7)))
    c.restore()


def socket(c, r, radius=20.0):
    """Where a code-drawn button sits: a shallow dark recess."""
    x, y, w, h = r
    c.drawRRect(rrect(x - 3, y - 3, w + 6, h + 6, radius + 3), shade(lin((x, y - 3), (x, y + h + 3), [C(P.INK, 235), C("#2A2F66", 235)])))
    c.drawRRect(rrect(x - 3, y - 3, w + 6, h + 6, radius + 3), stroke(C("#4A55A0", 160), 0.8))


def screw(c, x, y, r=3.2):
    c.drawCircle(x + 0.5, y + 0.8, r + 0.8, fill(C(P.INK, 200)))
    c.drawCircle(x, y, r, shade(chrome_sweep(x, y)))
    c.drawLine(x - r * 0.6, y + r * 0.2, x + r * 0.6, y - r * 0.2, stroke(C(P.INK, 210), 0.7, cap=skia.Paint.kButt_Cap))


def trim(c, x0, y, x1, col):
    """A glossy plastic rule with a chrome edge: the one colour accent a panel gets."""
    c.drawLine(x0, y + 0.8, x1, y + 0.8, stroke(C(P.INK, 220), 4.6, cap=skia.Paint.kButt_Cap))
    c.drawLine(x0, y, x1, y, stroke(C(col), 3.0, cap=skia.Paint.kButt_Cap))
    c.drawLine(x0, y - 0.8, x1, y - 0.8, stroke(C(P.WHITE, 150), 0.7, cap=skia.Paint.kButt_Cap))


def checker_band(c, x, y, w, h, n, a="#0E0F30", b="#2B3A8C"):
    cw = w / n
    rows = max(1, int(round(h / cw)))
    for j in range(rows):
        for i in range(n):
            c.drawRect(skia.Rect.MakeXYWH(x + i * cw, y + j * h / rows, cw + 0.05, h / rows + 0.05), fill(C(a if (i + j) % 2 else b)))


def confetti(c, items):
    """Graphic-design accents, used sparingly: (x, y, kind, colour, rotation)."""
    for (x, y, kind, col, rot) in items:
        c.save(); c.translate(x, y); c.rotate(rot)
        if kind == "tri":
            c.drawPath(poly_path([(0, -5), (5, 4), (-5, 4)]), fill(C(col)))
        elif kind == "zig":
            z = skia.Path(); z.moveTo(-9, 0)
            for j in range(4):
                z.lineTo(-9 + 4.5 * j + 2.25, -3 if j % 2 == 0 else 3)
            c.drawPath(z, stroke(C(col), 2.0, join=skia.Paint.kMiter_Join, cap=skia.Paint.kSquare_Cap))
        else:
            c.drawCircle(0, 0, 3, stroke(C(col), 1.8))
        c.restore()


def finish(st, wells, levels=24):
    """Ordered dither over the opaque art, then the display wells' own two-colour pixel fade."""
    im = ordered_dither(st.image(), levels=levels, cell=2, strength=1.0)
    a = np.asarray(im).copy()
    k = st.scale
    lo, hi = np.array(hexrgb(P.WELL), np.uint8), np.array(hexrgb(P.WELL_HI), np.uint8)
    for (x, y, w, h) in wells:
        x0, y0, x1, y1 = int((x - st.origin[0] + 3) * k), int((y - st.origin[1] + 3) * k), int((x - st.origin[0] + w - 3) * k), int((y - st.origin[1] + h - 3) * k)
        yy, xx = np.mgrid[y0:y1, x0:x1]
        t = (yy - y0) / max(1, (y1 - y0 - 1))
        prob = np.clip((t - 0.45) / 0.55, 0, 1) * 0.62
        th = BAYER4[(yy // 2) % 4, (xx // 2) % 4] + 0.5
        region = a[y0:y1, x0:x1]
        glass = np.abs(region.astype(np.int16) - lo.astype(np.int16)).sum(axis=2) < 60
        on = (prob > th) & glass
        region[on] = hi
    return Image.fromarray(a, "RGB")


# ---- title ----------------------------------------------------------------------
def title():
    """ui/title.png, 480 x 120 units, transparent: the only art that holds the name (from layout.json)."""
    st = Stage(960, 240, 1, 2, transparent=True)
    c = st.c
    f = fit_font("RussoOne-Regular.ttf", APP[0], 868, 7, 170)
    p, _ = text_path(APP[0], f, 470, 134, 7, "center")
    b = p.getBounds()
    depth = 15
    c.save(); c.translate(depth * 0.7 + 5, depth + 9); c.drawPath(p, blur(fill(C("#02010A", 170)), 6)); c.restore()
    for dd in range(depth, 0, -1):
        c.save(); c.translate(dd * 0.7, dd * 1.0)
        c.drawPath(p, stroke(C("#1A0803"), 2.0)); c.drawPath(p, fill(C(mix("#2A0A04", "#C2601A", 1 - dd / depth)))); c.restore()
    c.drawPath(p, stroke(C("#1A0803"), 2.4))
    c.drawPath(p, shade(lin((0, b.top()), (0, b.bottom()),
                            [C(P.GOLD_HI), C(P.GOLD), C("#D9861A"), C("#FFF3B0"), C("#FFC93A"), C(P.GOLD_LO)], [0, 0.3, 0.5, 0.53, 0.75, 1])))
    c.save(); c.clipPath(p, skia.ClipOp.kIntersect, True)
    c.save(); c.translate(2.0, 2.4); c.drawPath(p, stroke(C(P.WHITE, 235), 3.0)); c.restore()
    c.save(); c.translate(-2.0, -2.4); c.drawPath(p, stroke(C("#6A2A06", 210), 3.0)); c.restore()
    c.restore()
    if len(APP) > 1:
        f2 = fit_font("Audiowide-Regular.ttf", APP[1], 560, 24, 46)
        p2, _ = text_path(APP[1], f2, 474, 218, 24, "center")
        b2 = p2.getBounds()
        c.save(); c.translate(6, 9); c.drawPath(p2, blur(stroke(C("#02010A", 220), 10), 3.5)); c.restore()
        for dd in range(8, 0, -1):
            c.save(); c.translate(dd * 0.7, dd); c.drawPath(p2, stroke(C("#02010A"), 3)); c.drawPath(p2, fill(C(mix("#04142A", "#0E6A80", 1 - dd / 8)))); c.restore()
        c.drawPath(p2, stroke(C("#02010A"), 3))
        c.drawPath(p2, shade(lin((0, b2.top()), (0, b2.bottom()), [C(P.WHITE), C("#9FE8FF"), C("#2A7FB0"), C("#E8FBFF"), C(P.AQUA)], [0, 0.3, 0.5, 0.54, 1])))
        # Stripe wings either side of the second word.
        for sgn in (-1, 1):
            for i in range(3):
                x0 = 474 + sgn * (b2.width() / 2 + 22); y = 184 + i * 12
                wing = poly_path([(x0, y), (x0 + sgn * (96 - i * 24), y - 2), (x0 + sgn * (84 - i * 24), y + 7), (x0, y + 7)])
                c.drawPath(wing, stroke(C("#02010A"), 3.4)); c.drawPath(wing, fill(C([P.YELLOW, P.ORANGE, P.PINK][i])))
    # One glint on the first letter: a static highlight, so a flare is allowed here.
    fx, fy = b.left() + 26, b.top() + 7
    for ang, Ln, wd in ((0, 70, 2.0), (90, 30, 1.8), (45, 14, 1.2), (135, 14, 1.2)):
        a = math.radians(ang)
        c.drawLine(fx - Ln * math.cos(a), fy - Ln * math.sin(a), fx + Ln * math.cos(a), fy + Ln * math.sin(a), blur(stroke(C(P.WHITE, 235), wd), 1.0))
    c.drawCircle(fx, fy, 12, shade(rad((fx, fy), 12, [C(P.WHITE, 235), C(P.WHITE, 0)])))
    return st.image()


# ---- backdrop tile ------------------------------------------------------------
def backdrop():
    """ui/backdrop.png, 256 units, seamless. Dark enough to sit behind the status bar and the home indicator."""
    st = Stage(256, 256, 2, 2)
    xs, ys = st.grid()
    chk = (np.floor(xs / 32.0) + np.floor(ys / 32.0)) % 2
    base = ramp([(0, P.NIGHT), (1, mix(P.NIGHT, "#1E1E5A", 0.55))], chk)
    fx, fy = (xs / 32.0) % 1.0, (ys / 32.0) % 1.0
    base += ((fx < 0.04) | (fy < 0.04))[..., None] * np.array([5, 6, 12], np.float32)
    st.put(base)
    c = st.reopen()
    items = [(40, 52, "tri", 18), (168, 30, "zig", -12), (214, 120, "ring", 0), (96, 150, "zig", 28), (150, 214, "tri", 200),
             (22, 206, "ring", 0), (232, 232, "tri", 70), (120, 86, "ring", 0)]
    cols = [P.TEAL, P.VIOLET, P.PINK, P.VIOLET, P.TEAL, P.PINK, P.VIOLET, P.TEAL]
    for (x, y, kind, rot), col in zip(items, cols):
        for ox in (-256, 0, 256):
            for oy in (-256, 0, 256):
                confetti(c, [(x + ox, y + oy, kind, mix(col, P.NIGHT, 0.62), rot)])
    return ordered_dither(st.image(), levels=28, cell=2)


# ---- strip (Stack) ----------------------------------------------------------------
def strip():
    w, h = SCR["stack"]["width"], SCR["stack"]["stripHeight"]
    st = Stage(w, h, 2, 2)
    c = body(st, 3, (300, 60, 520, 200))
    c.drawRect(skia.Rect.MakeXYWH(0, h - 5, w, 5), shade(lin((0, h - 5), (0, h), [C(P.CH_HI), C(P.CH_MID), C(P.CH_LO)])))
    c.drawLine(0, h - 5.4, w, h - 5.4, stroke(C(P.INK), 1.0, cap=skia.Paint.kButt_Cap))
    wells = STRIP_WELLS
    for r in wells:
        well(c, r, 7.0 if r[3] > 50 else 5.0)
    socket(c, STRIP_PAUSE, 20)
    return finish(st, wells)


# ---- deck (Stack) -------------------------------------------------------------------
def deck():
    w, h = SCR["stack"]["width"], SCR["stack"]["deckMaxHeight"]
    st = Stage(w, h, 2, 2)
    xs, ys = st.grid()
    base = deck_plates(xs, ys, tile=32.0, origin=(0.0, 0.0), spot=(384, 40, 520, 160), seed=4)
    base = base * np.array([1.28, 1.02, 1.18], np.float32) + np.array([4, 0, 10], np.float32)
    # Below the button row the plates sink into the flat backdrop colour, so the deck can be cut at any height.
    t = np.clip((ys - 124.0) / 70.0, 0, 1)[..., None]
    base = base * (1 - t) + np.array(hexrgb(P.NIGHT), np.float32) * t
    st.put(base)
    c = st.reopen()
    c.drawRect(skia.Rect.MakeXYWH(0, 0, w, 5), shade(lin((0, 0), (0, 5), [C(P.CH_HI), C(P.CH_MID), C(P.CH_LO)])))
    c.drawLine(0, 5.4, w, 5.4, stroke(C(P.INK), 1.0, cap=skia.Paint.kButt_Cap))
    d = SCR["deck"]
    for key, rad_ in (("nudgeLeftButton", 20), ("centreButton", 20), ("nudgeRightButton", 20), ("leftCorner", 20), ("rightCorner", 20)):
        socket(c, d[key], rad_)
    checker_band(c, 0, 114, w, 6, 128)
    c.drawRect(skia.Rect.MakeXYWH(0, 114, w, 6), shade(lin((0, 114), (0, 120), [C(P.INK, 0), C(P.INK, 120)])))
    return finish(st, [], levels=28)


# ---- panel (Classic) ----------------------------------------------------------------
def panel():
    left = L["panel"]["left"]
    w, h = L["panel"]["width"], L["panel"]["height"]
    st = Stage(w, h, 2, 2, origin=(left, 0.0))
    c = body(st, 5, (left + 256, 300, 420, 620))
    # Chrome rail between table and panel.
    c.drawRect(skia.Rect.MakeXYWH(left, 0, 6, h), shade(lin((left, 0), (left + 6, 0), [C(P.CH_LO), C(P.CH_HI), C(P.CH_MID), C(P.CH_LO)], [0, 0.3, 0.6, 1])))
    c.drawLine(left + 6.4, 0, left + 6.4, h, stroke(C(P.INK), 1.0, cap=skia.Paint.kButt_Cap))
    # Title bay: plain dark glass so the lockup reads; a chequer foot and one glossy rule.
    tx, ty, tw, th = PANEL_TITLE
    c.drawRRect(rrect(tx - 4, ty - 4, tw + 8, th + 8, 12), shade(lin((0, ty - 4), (0, ty + th + 4), [C(P.CH_HI), C(P.CH_MID), C(P.CH_LO)], [0, 0.35, 1])))
    c.drawRRect(rrect(tx - 1, ty - 1, tw + 2, th + 2, 9), fill(C(P.INK)))
    c.save(); c.clipRRect(rrect(tx, ty, tw, th, 8), skia.ClipOp.kIntersect, True)
    c.drawRect(skia.Rect.MakeXYWH(tx, ty, tw, th), shade(lin((0, ty), (0, ty + th), [C("#05041A"), C("#141048"), C("#3A1C8A")], [0, 0.6, 1])))
    checker_band(c, tx, ty + th - 10, tw, 10, 58, "#0A0824", "#19B7B0")
    c.drawRect(skia.Rect.MakeXYWH(tx, ty + th - 10, tw, 10), shade(lin((0, ty + th - 10), (0, ty + th), [C("#141048", 200), C("#141048", 0)])))
    rng = np.random.default_rng(6)
    for _ in range(40):
        c.drawCircle(tx + rng.uniform(0, tw), ty + rng.uniform(0, th * 0.6), rng.uniform(0.3, 0.8), fill(C(P.CREAM, int(rng.integers(90, 220)))))
    c.restore()
    wells = PANEL_WELLS
    for r in wells:
        well(c, r, 8.0)
    # Glossy rules in the gaps above the first window and below the last, where there is room for one
    # beside the windows' chrome lips (about 5 units each side).
    x0, x1 = wells[0][0], wells[0][0] + wells[0][2]
    for top, bottom, col in ((ty + th, wells[0][1], P.TEAL), (wells[-1][1] + wells[-1][3], PANEL_CONTROLS[1], P.PINK)):
        if bottom - top >= 12:
            trim(c, x0, (top + bottom) / 2 + 0.5, x1, col)
    for (x, y) in ((779, 12), (1269, 12), (779, 948), (1269, 948), (779, 480), (1269, 480)):
        screw(c, x, y)
    pa = SCR["panelAreas"]
    for key in ("nudgeLeftButton", "centreButton", "nudgeRightButton", "flipperCorner"):
        socket(c, pa[key], 18)
    confetti(c, [(1262, 868, "tri", P.YELLOW, 20)])
    return finish(st, wells)


def dialog():
    """ui/dialog.png: the card behind the pause menu, About and initials screens. 192 units square, drawn
    nine-part with 48-unit corners (96 px), so it fits any box. Dark glass in a chrome lip, one teal rule
    under where the heading goes, a screw in each corner."""
    st = Stage(192, 192, 2, 2, transparent=True)
    c = st.c
    c.drawRRect(rrect(4, 6, 186, 184, 22), blur(fill(C("#02010A", 170)), 3.5))
    c.drawRRect(rrect(2, 2, 186, 186, 22), fill(C(P.INK)))
    c.drawRRect(rrect(3.2, 3.2, 183.6, 183.6, 21), shade(lin((0, 3), (0, 187), [C(P.CH_HI), C(P.CH_MID), C(P.CH_LO)], [0, 0.3, 1])))
    c.drawRRect(rrect(8, 8, 174, 174, 16.5), fill(C(P.INK)))
    c.drawRRect(rrect(9.2, 9.2, 171.6, 171.6, 15.5), shade(lin((0, 9), (0, 181), [C("#1A1F5C"), C("#0E1138"), C("#0A0C2A")], [0, 0.25, 1])))
    c.save(); c.clipRRect(rrect(9.2, 9.2, 171.6, 171.6, 15.5), skia.ClipOp.kIntersect, True)
    c.drawRect(skia.Rect.MakeXYWH(9, 9, 174, 10), shade(lin((0, 9), (0, 19), [C("#000000", 140), C("#000000", 0)])))
    c.restore()
    for (x, y) in ((17, 17), (173, 17), (17, 173), (173, 173)):
        screw(c, x, y, 2.8)
    return st.image()


ASSETS = {"dialog.png": dialog, "title.png": title, "backdrop.png": backdrop, "strip.png": strip, "deck.png": deck, "panel.png": panel}


def main(out):
    for name, fn in ASSETS.items():
        save_png(fn(), os.path.join(out, name))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "assets", "ui"))
