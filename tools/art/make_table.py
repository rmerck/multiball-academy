# The table background: assets/table/background.png, 1536 x 1920 (2 px per playfield unit), opaque.
# Every wall, post and guide the ball can hit is drawn from the same layout.json the physics is built from;
# nothing is drawn that looks like a wall and is not one. Unlit lamp inserts are baked in from the insert
# sprites, so the renderer's lit sprites land exactly on them.
#   python3 make_table.py [out_file]
import math
import os
import sys

import numpy as np
import skia
from PIL import Image

import palette as P
from common import (C, ROOT, Stage, blur, chrome_sweep, deck_plates, fill, font, hexrgb, layout, lin, mix,
                    ordered_dither, plus, poly_path, rad, ramp, rrect, save_png, shade, stroke, text_path)

L = layout()
W, H = L["width"], L["height"]
CX = L["fieldCenterX"]
LAMP_NAMES = [n for n, _ in P.LAMPS]
LAMP = {LAMP_NAMES[l["id"]]: tuple(l["c"]) for l in L["lamps"]}
LAMP_COL = dict(P.LAMPS)
SHELL = [tuple(p) for p in L["shell"]]
ISLAND = [tuple(p) for p in L["laneIsland"]]
ARCH = [tuple(p) for p in L["innerArch"]]
WALLS = [(w["a"], w["b"], w["r"]) for w in L["walls"]]
DEAD = [(w["a"], w["b"], w["r"]) for w in L["deadPosts"]]
BUMPERS = [(b["c"][0], b["c"][1], b["r"]) for b in L["bumpers"]]
TARGETS = [(t["shape"]["a"], t["shape"]["b"], t["shape"]["r"]) for t in L["targets"]]
DOORS = [(d["a"], d["b"], d["r"]) for d in L["bayDoors"]]
BAYS = [tuple(b) for b in L["lockBays"]]
HOLES = {h["type"]: h for h in L["holes"]}
SPIN = (tuple(L["spinner"]["a"]), tuple(L["spinner"]["b"]))
ORIGIN_L, ORIGIN_R = (318.0, 843.0), (393.0, 843.0)   # where shots leave the left and right bat
SPRITES = [os.path.join(ROOT, "assets", "sprites")]   # make_all points this at the tree being written
SHADOW = "#04030E"
LABEL_FONT = "Silkscreen-Regular.ttf"


def unit(v):
    n = math.hypot(v[0], v[1]) or 1.0
    return v[0] / n, v[1] / n


def mirror(p):
    return (2 * CX - p[0], p[1])


def inside(poly, x, y):
    n = False
    for i in range(len(poly)):
        x1, y1 = poly[i]; x2, y2 = poly[i - 1]
        if (y1 > y) != (y2 > y) and x < (x2 - x1) * (y - y1) / (y2 - y1) + x1:
            n = not n
    return n


def spline(pts, n=14):
    """Catmull-Rom through the points, as a polyline."""
    out = []
    q = [pts[0]] + list(pts) + [pts[-1]]
    for i in range(1, len(q) - 2):
        p0, p1, p2, p3 = q[i - 1], q[i], q[i + 1], q[i + 2]
        for t in np.linspace(0, 1, n, endpoint=False):
            t2, t3 = t * t, t * t * t
            out.append(tuple(0.5 * ((2 * p1[k]) + (-p0[k] + p2[k]) * t + (2 * p0[k] - 5 * p1[k] + 4 * p2[k] - p3[k]) * t2 +
                                    (-p0[k] + 3 * p1[k] - 3 * p2[k] + p3[k]) * t3) for k in (0, 1)))
    out.append(tuple(pts[-1]))
    return out


def offset(poly, d):
    out = []
    for i, p in enumerate(poly):
        a, b = poly[max(0, i - 1)], poly[min(len(poly) - 1, i + 1)]
        dx, dy = unit((b[0] - a[0], b[1] - a[1]))
        out.append((p[0] - dy * d, p[1] + dx * d))
    return out


class Table:
    def __init__(self):
        self.st = Stage(W, H, 2, 2)
        self.c = self.st.c
        self.shell = poly_path(SHELL)
        self.island = poly_path(ISLAND)

    # ---- primitives -------------------------------------------------------------
    def tube(self, path, r, base=P.CH_MID, hi=P.CH_HI, lo=P.CH_LO, shadow=(4.5, 6.0), sblur=2.6):
        c = self.c
        if shadow:
            c.save(); c.translate(*shadow); c.drawPath(path, blur(stroke(C(SHADOW, 165), 2 * r + 1), sblur)); c.restore()
        c.drawPath(path, stroke(C(P.INK), 2 * r + 1.6))
        c.drawPath(path, stroke(C(lo), 2 * r))
        c.save(); c.translate(-r * 0.12, -r * 0.14); c.drawPath(path, stroke(C(base), 2 * r * 0.72)); c.restore()
        c.save(); c.translate(-r * 0.34, -r * 0.36); c.drawPath(path, stroke(C(hi), 2 * r * 0.26)); c.restore()
        c.save(); c.translate(r * 0.52, r * 0.55); c.drawPath(path, stroke(C(mix(base, P.WHITE, 0.25), 150), 2 * r * 0.12)); c.restore()

    def seg(self, a, b):
        return poly_path([tuple(a), tuple(b)], close=False)

    def bolt(self, x, y, r=2.2):
        self.c.drawCircle(x, y, r, fill(C(P.INK)))
        self.c.drawCircle(x - r * 0.18, y - r * 0.18, r * 0.58, fill(C(P.CH_HI)))

    def label(self, text, x, y, colour, size=8.0, tracking=0.4, angle=0.0, alpha=255):
        c = self.c
        f = font(LABEL_FONT, size)
        c.save(); c.translate(x, y); c.rotate(angle)
        p, _ = text_path(text, f, 0, 0, tracking, "center")
        c.drawPath(p, stroke(C(P.INK, alpha), 2.2))
        c.drawPath(p, fill(C(colour, alpha)))
        c.restore()

    def stencil(self, text, x, y, colour, size, tracking=1.0, angle=0.0, alpha=255, name="SairaStencilOne-Regular.ttf"):
        c = self.c
        f = font(name, size)
        c.save(); c.translate(x, y); c.rotate(angle)
        p, _ = text_path(text, f, 0, 0, tracking, "center")
        c.drawPath(p, fill(C(colour, alpha)))
        c.restore()

    def sprite(self, name, cx, cy, size_units, tint=None, level=1.0, angle=0.0, h_units=None):
        """Bake a sprite as the renderer would draw it (tint = colour multiply)."""
        img = skia.Image.open(os.path.join(SPRITES[0], name))
        w = size_units; h = h_units if h_units is not None else size_units * img.height() / img.width()
        paint = skia.Paint(AntiAlias=True)
        if tint is not None:
            r, g, b = hexrgb(tint)
            paint.setColorFilter(skia.ColorFilters.Blend(skia.Color(int(r * level), int(g * level), int(b * level)), skia.BlendMode.kModulate))
        c = self.c
        c.save(); c.translate(cx, cy); c.rotate(angle)
        c.drawImageRect(img, skia.Rect.MakeXYWH(-w / 2, -h / 2, w, h), skia.SamplingOptions(skia.FilterMode.kLinear, skia.MipmapMode.kLinear), paint)
        c.restore()

    # ---- 0. outside the shell ---------------------------------------------------
    def outside(self):
        st = self.st
        xs, ys = st.grid()
        chk = (np.floor(xs / 16.0) + np.floor(ys / 16.0)) % 2
        fade = np.clip(1.0 - ys / 210.0, 0, 1) ** 1.2
        base = ramp([(0, "#07061A"), (1, "#3A2A8A")], chk * fade * 0.85)
        base += (np.clip(1.0 - np.abs(ys - 480) / 520.0, 0, 1) * 6)[..., None]
        st.put(base)
        c = self.c = st.reopen()
        # Academy crest in the upper left corner: a ring, stripe wings, a delta. Original; no lettering but the class year.
        x, y = 64.0, 66.0
        for sgn in (-1, 1):
            for i in range(3):
                wing = poly_path([(x + sgn * 22, y - 10 + i * 8), (x + sgn * (58 - i * 11), y - 13 + i * 8), (x + sgn * (52 - i * 11), y - 6 + i * 8), (x + sgn * 22, y - 4 + i * 8)])
                c.drawPath(wing, stroke(C(P.INK), 2.4)); c.drawPath(wing, fill(C(mix([P.YELLOW, P.ORANGE, P.PINK][i], P.INK, 0.25))))
        c.drawCircle(x + 2, y + 3, 26, blur(fill(C(SHADOW, 180)), 3))
        c.drawCircle(x, y, 25, shade(chrome_sweep(x, y)))
        c.drawCircle(x, y, 21, shade(rad((x - 6, y - 8), 34, [C("#3B4AA8"), C("#161A54"), C("#0A0920")], [0, 0.6, 1])))
        jet = [(0, -15), (2, -9), (2.8, -3), (14, 8), (13.2, 10.5), (3.2, 7.5), (2.6, 10), (6.4, 14), (5.8, 15.5), (1.6, 13.4), (0, 14.6)]
        jp = poly_path([(x + a, y + b) for a, b in jet] + [(x - a, y + b) for a, b in jet[::-1]])
        c.drawPath(jp, stroke(C(P.INK), 1.6)); c.drawPath(jp, shade(lin((x - 14, y - 12), (x + 12, y + 14), [C(P.WHITE), C(P.CH_MID), C(P.CH_LO)])))
        self.label("CLASS OF 94", x, y + 44, mix(P.CREAM, P.INK, 0.2), 8)
        # Upper right: the flight line the launch follows.
        self.label("FLIGHT LINE", 668, 60, mix(P.AQUA, P.INK, 0.15), 8)
        for i in range(3):
            ch = poly_path([(640 + i * 14, 84), (648 + i * 14, 76), (656 + i * 14, 84)], close=False)
            c.save(); c.rotate(-140, 662, 80)
            c.drawPath(ch, stroke(C(P.INK), 4.6, join=skia.Paint.kMiter_Join)); c.drawPath(ch, stroke(C(mix(P.AQUA, P.INK, 0.2)), 2.4, join=skia.Paint.kMiter_Join))
            c.restore()

    # ---- 1. the deck ------------------------------------------------------------
    def deck(self):
        st = self.st
        # Computed at the output size and enlarged: the plates have no detail finer than a pixel.
        hw, hh = st.w // st.ss, st.h // st.ss
        ys, xs = np.mgrid[0:hh, 0:hw].astype(np.float32)
        xs = (xs + 0.5) / st.scale; ys = (ys + 0.5) / st.scale
        base = deck_plates(xs, ys, tile=37.0, origin=(CX, 300.0), spot=(CX, 430.0, 340.0, 470.0), seed=1)
        im = Image.fromarray(np.clip(base, 0, 255).astype(np.uint8), "RGB").resize((st.w, st.h), Image.BILINEAR)
        st.put(np.asarray(im), clip=self.shell)
        self.c = st.c

    def zones(self):
        """Colour zones on the deck, all under the luminance ceiling: circuits, lanes, the launch lane."""
        c = self.c
        c.save(); c.clipPath(self.shell, skia.ClipOp.kIntersect, True)
        rx, ry = 239.0, 205.0
        pts = [(CX + rx * math.cos(t), 300 - ry * math.sin(t)) for t in np.linspace(0, math.pi, 100)]
        circuit = poly_path([(594.5, 500), (594.5, 300)] + pts[1:] + [(116.5, 300), (116.5, 500)], close=False)
        c.drawPath(circuit, stroke(C("#0A2A52", 225), 36, cap=skia.Paint.kButt_Cap))
        c.drawPath(circuit, stroke(C(mix(P.AQUA, P.INK, 0.55), 200), 1.4, PathEffect=skia.DashPathEffect.Make([8, 8], 0)))
        for deg in (150, 132, 114, 96, 84, 66, 48, 30):
            t = math.radians(deg)
            x = CX + rx * math.cos(t); y = 300 - ry * math.sin(t)
            tx, ty = -rx * math.sin(t), -ry * math.cos(t)
            sign = 1 if x < CX else -1
            self.chevron(x, y, math.degrees(math.atan2(ty * sign, tx * sign)), mix(P.AQUA, P.INK, 0.35))
        for x in (116.5, 594.5):
            for y in (330, 380, 430):
                self.chevron(x, y, -90, mix(P.AQUA, P.INK, 0.35))
        # Return lanes and outlanes: darker beds.
        for (x0, x1, y0, y1) in ((45, 78, 604, 960), (88, 141, 606, 742), (633, 671, 588, 960), (570, 623, 606, 742)):
            c.drawRect(skia.Rect.MakeLTRB(x0, y0, x1, y1), shade(lin((0, y0), (0, y0 + 40), [C("#0A0E30", 0), C("#0A0E30", 150)])))
        # Launch lane.
        c.drawRect(skia.Rect.MakeLTRB(681, 330, 728, 960), fill(C("#0A0E30", 150)))
        for y in range(380, 880, 44):
            self.chevron(704.5, y, -90, mix(P.AQUA, P.INK, 0.55), 5.0)
        c.restore()

    def chevron(self, x, y, angle, colour, s=6.0):
        c = self.c
        c.save(); c.translate(x, y); c.rotate(angle)
        ch = poly_path([(-s, -s * 1.3), (s * 0.66, 0), (-s, s * 1.3)], close=False)
        c.drawPath(ch, stroke(C(P.INK), s, join=skia.Paint.kMiter_Join))
        c.drawPath(ch, stroke(C(colour), s * 0.5, join=skia.Paint.kMiter_Join))
        c.restore()

    # ---- 2. printed floor art ---------------------------------------------------
    def taxi_lines(self):
        c = self.c
        shots = [(ORIGIN_R, (118, 530), "ArrowCircuitLeft"), (ORIGIN_R, (192, 438), "ArrowRunway"), (ORIGIN_R, (279, 432), "ArrowHangar"),
                 (ORIGIN_R, (356, 500), "ArrowBriefing"), (ORIGIN_L, (356, 500), "ArrowBriefing"), (ORIGIN_L, (432, 432), "ArrowTurbine"),
                 (ORIGIN_L, (519, 438), "ArrowSkyway"), (ORIGIN_L, (593, 530), "ArrowCircuitRight")]
        c.save(); c.clipPath(self.shell, skia.ClipOp.kIntersect, True)
        for o, m, key in shots:
            col = LAMP_COL[key]
            # Start a little way up the table so the fan does not knot between the flippers.
            dx, dy = m[0] - o[0], m[1] - o[1]
            k = (o[1] - 632.0) / (o[1] - m[1])      # every line starts on the same level, above the emblem
            s = (o[0] + dx * k, o[1] + dy * k)
            p = self.seg(s, m)
            c.drawPath(p, stroke(C("#080A24", 150), 4.6, cap=skia.Paint.kButt_Cap))
            c.drawPath(p, stroke(C(mix(col, P.INK, 0.45), 215), 1.8, cap=skia.Paint.kButt_Cap, PathEffect=skia.DashPathEffect.Make([10, 6], 0)))
        c.restore()

    def emblem(self):
        """The lower field: an academy crest (no product name: ui/title.png stays the only art that holds it)
        and airfield paint, all in low-mid tones under the luminance ceiling, all flat."""
        c = self.c
        x, y = CX, 700.0
        tone, tone2, tone3 = "#2A3470", "#38448E", "#222A60"
        # Radar arcs centred between the flippers.
        for rr in (150, 205, 260):
            arc = skia.Path(); arc.addArc(skia.Rect.MakeLTRB(CX - rr, 852 - rr, CX + rr, 852 + rr), 205, 130)
            c.drawPath(arc, stroke(C(tone, 200), 1.0, PathEffect=skia.DashPathEffect.Make([2, 5], 0)))
        # Taxiway: a double centre line from the lamp arc to the flippers, with hold bars across it.
        for dx in (-3.0, 3.0):
            c.drawLine(x + dx, 628, x + dx, 660, stroke(C(tone2), 1.4, cap=skia.Paint.kButt_Cap))
            c.drawLine(x + dx, 790, x + dx, 900, stroke(C(tone2), 1.4, cap=skia.Paint.kButt_Cap, PathEffect=skia.DashPathEffect.Make([9, 6], 0)))
        for i in range(4):
            yy = 800 + i * 7
            c.drawLine(x - 26, yy, x + 26, yy, stroke(C(tone2 if i % 2 == 0 else tone), 2.0, cap=skia.Paint.kButt_Cap,
                                                     PathEffect=None if i % 2 == 0 else skia.DashPathEffect.Make([5, 4], 0)))
        # Lead-in lines from each return lane to its flipper, with stand marks beside them.
        for sgn in (-1, 1):
            lead = skia.Path(); lead.moveTo(x + sgn * 250, 760); lead.quadTo(x + sgn * 190, 800, x + sgn * 120, 846)
            c.drawPath(lead, stroke(C(tone2), 1.4, PathEffect=skia.DashPathEffect.Make([8, 5], 0)))
            for i, (sx, sy) in enumerate(((132, 640), (118, 760))):
                px = x + sgn * sx
                c.drawLine(px - 9, sy, px + 9, sy, stroke(C(tone2), 1.6, cap=skia.Paint.kButt_Cap))
                c.drawLine(px, sy, px, sy + 18, stroke(C(tone2), 1.6, cap=skia.Paint.kButt_Cap))
                self.stencil("%s%d" % ("A" if sgn < 0 else "B", i + 1), px, sy - 5, tone2, 10, 0.5)
            # Inspection plates: a riveted outline, part of the deck.
            plate = rrect(x + sgn * 196 - 22, 868, 44, 30, 4)
            c.drawRRect(plate, stroke(C(tone3), 2.2)); c.drawRRect(plate, stroke(C(tone2, 200), 0.8))
            for (ax, ay) in ((-17, -10), (17, -10), (-17, 10), (17, 10)):
                c.drawCircle(x + sgn * 196 + ax, 883 + ay, 1.3, fill(C(tone2)))
        # Apron edge along the foot of the table.
        for xx in np.arange(120, 600, 14.0):
            c.drawPath(poly_path([(xx, 958), (xx + 7, 958), (xx + 13, 946), (xx + 6, 946)]), fill(C(tone, 210)))
        # The crest: wings, ring, star, laurel ticks, a ribbon with three stars.
        for sgn in (-1, 1):
            for i in range(5):
                wing = poly_path([(x + sgn * 26, y - 20 + i * 8.5), (x + sgn * (134 - i * 21), y - 26 + i * 8.5),
                                  (x + sgn * (123 - i * 21), y - 15.5 + i * 8.5), (x + sgn * 26, y - 14 + i * 8.5)])
                c.drawPath(wing, fill(C(tone2 if i % 2 == 0 else tone)))
                c.drawPath(wing, stroke(C(tone3), 0.8))
        c.drawCircle(x, y, 31, fill(C("#141A44"))); c.drawCircle(x, y, 31, stroke(C(tone2), 2.4)); c.drawCircle(x, y, 26, stroke(C(tone), 1.0))
        for k in range(24):
            a = k * math.pi / 12
            c.drawLine(x + 27.5 * math.cos(a), y + 27.5 * math.sin(a), x + 29.8 * math.cos(a), y + 29.8 * math.sin(a), stroke(C(tone2), 1.0, cap=skia.Paint.kButt_Cap))
        star = []
        for k in range(10):
            a = -math.pi / 2 + k * math.pi / 5
            rr = 21 if k % 2 == 0 else 8.8
            star.append((x + rr * math.cos(a), y + rr * math.sin(a)))
        c.drawPath(poly_path(star), fill(C(tone2))); c.drawPath(poly_path(star), stroke(C(tone3), 0.8))
        rib = poly_path([(x - 62, y + 42), (x + 62, y + 42), (x + 54, y + 51), (x + 62, y + 60), (x - 62, y + 60), (x - 54, y + 51)])
        c.drawPath(rib, fill(C(tone))); c.drawPath(rib, stroke(C(tone2), 1.0))
        for dx in (-26, 0, 26):
            s5 = []
            for k in range(10):
                a = -math.pi / 2 + k * math.pi / 5
                rr = 5.4 if k % 2 == 0 else 2.3
                s5.append((x + dx + rr * math.cos(a), y + 51 + rr * math.sin(a)))
            c.drawPath(poly_path(s5), fill(C(tone2)))
        self.stencil("EST 1994", x, y + 76, tone2, 10, 3.0)

    def nest_art(self):
        c = self.c
        bx, by, _ = BUMPERS[2]
        ring = mix(P.AQUA, P.INK, 0.50)
        c.drawCircle(bx, by, 43, stroke(C(ring), 1.0)); c.drawCircle(bx, by, 33, stroke(C(ring, 150), 0.8))
        for d in range(0, 360, 10):
            a = math.radians(d); r0 = 36 if d % 30 else 33
            c.drawLine(bx + r0 * math.cos(a), by + r0 * math.sin(a), bx + 43 * math.cos(a), by + 43 * math.sin(a),
                       stroke(C(ring), 0.9 if d % 30 else 1.7, cap=skia.Paint.kButt_Cap))
        f = font("RussoOne-Regular.ttf", 8)
        for ch, d in (("E", 0), ("S", 90), ("W", 180)):
            a = math.radians(d)
            p, _ = text_path(ch, f, bx + 50.5 * math.cos(a), by + 50.5 * math.sin(a) + 2.9, 0, "center")
            c.drawPath(p, fill(C(mix(P.AQUA, P.INK, 0.35))))
        # The trainer, printed dark between the roll call lanes and the thrusters, nose to the north.
        jx, jy = CX, 199.0
        half = [(0, -21), (2.6, -13), (3.6, -4), (19.5, 11), (18.5, 14.5), (4.2, 10.5), (3.4, 14), (8.8, 19.5), (8.0, 21.5), (2.2, 18.5), (0, 20.5)]
        jet = poly_path([(jx + a, jy + b) for a, b in half] + [(jx - a, jy + b) for a, b in half[::-1]])
        c.drawPath(jet, shade(lin((jx - 20, jy - 16), (jx + 16, jy + 20), [C("#55649A"), C("#39467C"), C("#262E5E"), C("#161A44")], [0, 0.3, 0.75, 1])))
        c.drawPath(jet, stroke(C(P.INK), 0.8))
        c.drawOval(skia.Rect.MakeLTRB(jx - 2, jy - 14.5, jx + 2, jy - 5.5), fill(C(P.INK)))
        c.drawOval(skia.Rect.MakeLTRB(jx - 1.3, jy - 13.8, jx + 1.0, jy - 7.5), fill(C("#3E7FA0")))
        # Seats under the thrusters: a dark well, a ring of exhaust scorch.
        for (x, y, r) in BUMPERS:
            c.drawCircle(x + 5, y + 7, r + 2, blur(fill(C(SHADOW, 190)), 4.5))
            c.drawCircle(x, y, r + 5, stroke(C(mix(P.ORANGE, P.INK, 0.72), 200), 1.4, PathEffect=skia.DashPathEffect.Make([3, 3], 0)))
        self.label("ROLL", 262, 190, mix(P.CREAM, P.INK, 0.35)); self.label("CALL", 449, 190, mix(P.CREAM, P.INK, 0.35))

    def roll_call(self):
        c = self.c
        for key in ("RollCall1", "RollCall2", "RollCall3"):
            x, y = LAMP[key]
            board = rrect(x - 15, y - 21, 30, 40, 3)
            c.drawRRect(board, shade(lin((x - 15, y - 21), (x + 15, y + 19), [C("#303A80"), C("#141A4A")])))
            c.drawRRect(board, stroke(C("#5560B0"), 0.9))
            c.drawRect(skia.Rect.MakeLTRB(x - 6, y - 24, x + 6, y - 18), shade(lin((0, y - 24), (0, y - 18), [C(P.CH_HI), C(P.CH_LO)])))
            c.drawRect(skia.Rect.MakeLTRB(x - 6, y - 24, x + 6, y - 18), stroke(C(P.INK), 0.8))
            for i in range(2):
                c.drawLine(x - 10, y + 13 + i * 3, x + 10 - i * 6, y + 13 + i * 3, stroke(C("#5560B0", 200), 0.8, cap=skia.Paint.kButt_Cap))

    def turbine(self):
        c = self.c
        cx, cy, R = 432.0, 372.0, 30.0
        col = P.SHOT["turbine"]
        c.drawCircle(cx, cy, R, shade(rad((cx - 9, cy - 12), R * 1.6, [C("#2C3A7C"), C("#141A4A"), C("#0A0920")], [0, 0.55, 1])))
        for k in range(12):
            a = math.radians(k * 30)
            c.drawPath(poly_path([(cx + 6 * math.cos(a), cy + 6 * math.sin(a)), (cx + (R - 4) * math.cos(a + 0.10), cy + (R - 4) * math.sin(a + 0.10)),
                                  (cx + (R - 4) * math.cos(a + 0.40), cy + (R - 4) * math.sin(a + 0.40))]), fill(C("#3E4C8E", 210)))
        c.drawCircle(cx, cy, R, stroke(C(P.INK), 3.4)); c.drawCircle(cx, cy, R, stroke(C("#4A5AA0"), 1.6))
        c.drawCircle(cx, cy, 6.5, fill(C(P.INK))); c.drawCircle(cx, cy, 5, fill(C("#4A5AA0")))
        for x in (410, 432, 454):
            ch = poly_path([(x - 6, 430), (x, 424), (x + 6, 430)], close=False)
            c.drawPath(ch, stroke(C(P.INK), 4.4, join=skia.Paint.kMiter_Join)); c.drawPath(ch, stroke(C(mix(col, P.INK, 0.3)), 2.2, join=skia.Paint.kMiter_Join))
        self.label("TURBINE", 432, 338.5, mix(col, P.CREAM, 0.35))
        # Spinner seat: the line the blade turns on.
        c.drawLine(SPIN[0][0], SPIN[0][1] + 3.5, SPIN[1][0], SPIN[1][1] + 3.5, blur(stroke(C(SHADOW, 170), 4), 1.6))

    def ramp_lip(self, mouth, col, mark):
        c = self.c
        x0, x1 = mouth
        mx = (x0 + x1) / 2
        lip = poly_path([(x0 + 4, 436), (x1 - 4, 436), (mx + 12, 382), (mx - 12, 382)])
        c.save(); c.translate(4, 6); c.drawPath(lip, blur(fill(C(SHADOW, 150)), 3)); c.restore()
        c.drawPath(lip, shade(lin((x0, 436), (x1, 372), [C(mix(col, P.INK, 0.80), 248), C(mix(col, P.INK, 0.55), 248), C(mix(col, P.INK, 0.72), 248)], [0, 0.45, 1])))
        c.drawPath(lip, stroke(C(mix(col, P.INK, 0.15)), 1.2, join=skia.Paint.kMiter_Join))
        c.save(); c.clipPath(lip, skia.ClipOp.kIntersect, True)
        key = C(P.CREAM, 120)
        for i in range(6):
            c.drawRect(skia.Rect.MakeXYWH(mx - 27.5 + i * 10.0, 421, 5, 12), fill(key))
        f = font("SairaStencilOne-Regular.ttf", 13)
        p, _ = text_path(mark, f, mx, 414, 1.0, "center"); c.drawPath(p, fill(key))
        c.drawLine(mx, 402, mx, 382, stroke(key, 1.6, cap=skia.Paint.kButt_Cap, PathEffect=skia.DashPathEffect.Make([5, 4], 0)))
        c.restore()

    def hangar(self):
        c = self.c
        floor = poly_path([(237, 318), (279, 296), (321, 318), (321, 397), (237, 397)])
        c.save(); c.clipPath(floor, skia.ClipOp.kIntersect, True)
        c.drawPaint(fill(C("#2E3A78")))
        for i in range(-1, 9):
            for j in range(0, 11):
                if (i + j) % 2 == 0:
                    c.drawRect(skia.Rect.MakeXYWH(237 + i * 10.5, 292 + j * 10.5, 10.5, 10.5), fill(C("#1B2254")))
        c.drawPaint(shade(lin((279, 296), (279, 397), [C(P.INK, 190), C(P.INK, 0)], [0, 0.8])))
        line = mix(P.YELLOW, P.INK, 0.22)
        for i, (bx, by) in enumerate(BAYS):
            c.drawCircle(bx, by, 13.5, stroke(C(line), 1.5))
            c.drawLine(bx, by + 13.5, bx, by + 40, stroke(C(line, 210), 1.3, cap=skia.Paint.kButt_Cap, PathEffect=skia.DashPathEffect.Make([5, 4], 0)))
            c.drawLine(bx - 5, by + 20, bx + 5, by + 20, stroke(C(line), 1.5, cap=skia.Paint.kButt_Cap))
            f = font("SairaStencilOne-Regular.ttf", 9)
            p, _ = text_path("B%d" % (i + 1), f, bx + (11 if i == 0 else -11), by + 36, 0, "center"); c.drawPath(p, fill(C(line)))
        c.drawLine(279, 312, 279, 378, stroke(C(line), 1.5, cap=skia.Paint.kButt_Cap))
        roof = poly_path([(240, 320), (279, 299), (318, 320), (318, 326), (279, 306.5), (240, 326)])
        c.drawPath(roof, fill(C("#8793B8")))
        c.save(); c.clipPath(roof, skia.ClipOp.kIntersect, True)
        for x in np.arange(240, 320, 3.0):
            c.drawLine(x, 296, x, 330, stroke(C("#3E4874"), 1.0, cap=skia.Paint.kButt_Cap))
        c.restore()
        c.drawPath(roof, stroke(C(P.INK), 0.8))
        c.drawPath(floor, blur(stroke(C(P.INK, 160), 7), 3))
        c.restore()
        # Door track and the numerals printed behind each door.
        c.drawLine(243, 392, 315, 392, stroke(C(P.INK, 235), 9.0, cap=skia.Paint.kButt_Cap))
        c.drawLine(243, 392, 315, 392, stroke(C("#2A3470"), 1.0, cap=skia.Paint.kButt_Cap, PathEffect=skia.DashPathEffect.Make([2, 3], 0)))
        f = font("RussoOne-Regular.ttf", 7.5)
        for i, d in enumerate(DOORS):
            p, _ = text_path(str(i + 1), f, (d[0][0] + d[1][0]) / 2, 382.5, 0, "center"); c.drawPath(p, fill(C(line)))
        # Apron with hazard edges; the lock lamps sit on it.
        box = skia.Rect.MakeLTRB(243, 402, 315, 430)
        c.save(); c.clipRect(box, skia.ClipOp.kIntersect, True)
        for x in np.arange(230, 330, 9.0):
            c.drawLine(x, 432, x + 6, 426.5, stroke(C(line, 215), 2.4, cap=skia.Paint.kButt_Cap))
            c.drawLine(x, 405.5, x + 6, 400, stroke(C(line, 215), 2.4, cap=skia.Paint.kButt_Cap))
        c.restore()
        c.drawRect(box, stroke(C(line), 1.5, join=skia.Paint.kMiter_Join))

    def hangar_walls(self):
        path = poly_path([(237, 397), (237, 318), (279, 296), (321, 318), (321, 400)], close=False)
        self.tube(path, 5)
        segs = [((237, 397), (237, 318)), ((237, 318), (279, 296)), ((279, 296), (321, 318)), ((321, 318), (321, 400))]
        for a, b in segs:
            Ln = math.hypot(b[0] - a[0], b[1] - a[1]); dx, dy = unit((b[0] - a[0], b[1] - a[1]))
            n = int(Ln // 4)
            for i in range(1, n):
                px = a[0] + dx * i * Ln / n; py = a[1] + dy * i * Ln / n
                self.c.drawLine(px - dy * 3.2, py + dx * 3.2, px + dy * 3.2, py - dx * 3.2, stroke(C(P.CH_LO, 220), 0.9, cap=skia.Paint.kButt_Cap))

    def briefing(self):
        c = self.c
        room = poly_path([(326, 322), (386, 351), (386, 400), (394, 428), (318, 428), (326, 400)])
        c.save(); c.clipPath(room, skia.ClipOp.kIntersect, True)
        c.drawPaint(shade(lin((326, 322), (386, 428), [C("#0B2F33"), C("#1B5A58")])))
        dx, dy = unit((70, 35)); nx, ny = -dy, dx
        a = (326, 325); b = (386, 354.5)
        board = poly_path([(a[0] + nx * 2, a[1] + ny * 2), (b[0] + nx * 2, b[1] + ny * 2), (b[0] + nx * 15, b[1] + ny * 15), (a[0] + nx * 15, a[1] + ny * 15)])
        c.drawPath(board, fill(C("#1E7A62"))); c.drawPath(board, stroke(C("#D9C9A0"), 1.3, join=skia.Paint.kMiter_Join))
        c.save(); c.clipPath(board, skia.ClipOp.kIntersect, True)
        chalk = stroke(C(P.CREAM, 230), 0.9)
        z = skia.Path(); z.moveTo(336, 337); z.cubicTo(344, 334, 346, 348, 356, 345); z.rLineTo(-2.5, -2.5); z.rMoveTo(2.5, 2.5); z.rLineTo(-3.3, 1.2)
        c.drawPath(z, chalk); c.drawPath(poly_path([(365, 352), (371, 349), (370.5, 356)]), chalk)
        c.drawLine(374, 358, 383, 362.5, chalk); c.drawLine(372.5, 361, 380, 364.7, chalk)
        c.restore()
        for i in range(4):
            c.save(); c.translate(337 + i * 12.5, 366 + i * 5.0); c.rotate(26.5)
            c.drawRRect(rrect(-4.6, -2.4, 9.2, 4.8, 1), fill(C("#C8A56A"))); c.drawRRect(rrect(-4.6, -2.4, 9.2, 4.8, 1), stroke(C(P.INK), 0.8))
            c.restore()
        hx, hy, hr = 356.0, 400.0, 15.0
        c.drawCircle(hx, hy, hr + 1.5, fill(C(P.CH_LO)))
        c.drawCircle(hx, hy, hr, shade(rad((hx + 3, hy + 5), hr * 1.3, [C("#2A2140"), C("#05030F")])))
        c.drawCircle(hx, hy, hr + 0.6, stroke(C(mix(P.YELLOW, P.INK, 0.15), 230), 1.2))
        c.drawPath(room, blur(stroke(C(P.INK, 170), 7), 3))
        c.restore()
        c.drawPath(poly_path([(322, 430), (390, 430), (410, 462), (302, 462)]), shade(lin((356, 430), (356, 462), [C(P.YELLOW, 40), C(P.YELLOW, 0)])))
        c.drawLine(321, 430.5, 391, 430.5, stroke(C(mix(P.YELLOW, P.INK, 0.15), 240), 2.2, cap=skia.Paint.kButt_Cap, PathEffect=skia.DashPathEffect.Make([6, 3], 0)))

    def lower_field(self):
        """Lanes, pockets, slingshot seats, kickback, launch lane scale, labels."""
        c = self.c
        # W-I-N-G letters above their lamps.
        for key, ch in (("WingW", "W"), ("WingI", "I"), ("WingN", "N"), ("WingG", "G")):
            x, y = LAMP[key]
            self.stencil(ch, x, y - 11.5, mix(P.AQUA, P.INK, 0.15), 11, 0, name="RussoOne-Regular.ttf")
        self.landings()
        # Kickback: its well, and the way it throws.
        kx, ky = L["kickback"]["c"]
        c.drawRRect(rrect(kx - 15, ky - 23, 30, 48, 4), fill(C(P.INK, 235))); c.drawRRect(rrect(kx - 15, ky - 23, 30, 48, 4), stroke(C("#2A3470"), 1.0))
        for y in (632, 644):
            self.chevron(kx, y, -90, mix(P.YELLOW, P.INK, 0.3), 4.5)
        # Right outlane: no kickback, so it says so.
        for y in (770, 800, 830):
            self.chevron(652, y, 90, mix(P.PINK, P.INK, 0.45), 4.5)
        for y in (800, 830):
            self.chevron(59, y, 90, mix(P.PINK, P.INK, 0.45), 4.5)
        # Plunger well at the foot of the launch lane.
        pl = L["plunger"]; lx = L["lane"]["centerX"]
        c.drawRect(skia.Rect.MakeLTRB(lx - pl["width"] / 2 - 2, pl["restTopY"] - 4, lx + pl["width"] / 2 + 2, 960), fill(C(P.INK, 200)))
        # Shoot again, extra awards, labels.
        self.label("SHOOT AGAIN", LAMP["ShootAgain"][0], LAMP["ShootAgain"][1] - 15, mix(P.CREAM, P.INK, 0.25))
        self.label("CREDIT", 331, 517, mix(P.AQUA, P.INK, 0.25)); self.label("X BALL", 381, 517, mix(P.PINK, P.CREAM, 0.2))
        self.label("OVERDRIVE", 442, 506, mix(P.YELLOW, P.INK, 0.2))
        self.label("RUNWAY", 204, 495, mix(P.SHOT["runway"], P.CREAM, 0.3)); self.label("HANGAR", 283, 466, mix(P.SHOT["hangar"], P.CREAM, 0.3))
        self.label("BRIEFING", 356, 447.5, mix(P.SHOT["briefing"], P.CREAM, 0.3)); self.label("SKYWAY", 507, 495, mix(P.SHOT["skyway"], P.CREAM, 0.3))
        self.label("CIRCUIT", 118, 537, mix(P.AQUA, P.INK, 0.15), 7.0, 0.2); self.label("CIRCUIT", 593, 537, mix(P.AQUA, P.INK, 0.15), 7.0, 0.2)
        # Course numbers under the lamp arc, and the flight-plan arc that joins them.
        arc = poly_path([LAMP["Course%d" % i] for i in range(1, 7)], close=False)
        c.drawPath(arc, stroke(C(mix(P.ORCHID, P.INK, 0.6)), 1.4, PathEffect=skia.DashPathEffect.Make([3, 4], 0)))
        for i, code in enumerate(("101", "103", "201", "202", "301", "302")):
            x, y = LAMP["Course%d" % (i + 1)]
            self.label(code, x, y + 21, mix(P.ORCHID, P.CREAM, 0.25), 8, 0.2)
        gx, gy = LAMP["Graduation"]
        self.label("GRADUATION", gx, gy - 16, mix(P.YELLOW, P.INK, 0.15), 8, 0.2)
        self.label("LAUNCH", 704.5, 622, mix(P.AQUA, P.INK, 0.45), 8, 1.0, angle=-90)

    def pockets(self):
        """Hazard stripes on the roofs of the two outlane pockets (outside the playfield: solid there)."""
        c = self.c
        for a, b, out in (((88, 556), (40, 600), (-1, -1)), ((625, 536), (671, 584), (1, -1))):
            dx, dy = unit((b[0] - a[0], b[1] - a[1])); nx, ny = (-dy, dx)
            if nx * out[0] + ny * out[1] < 0:
                nx, ny = -nx, -ny
            band = poly_path([a, b, (b[0] + nx * 9, b[1] + ny * 9), (a[0] + nx * 9, a[1] + ny * 9)])
            c.save(); c.clipPath(band, skia.ClipOp.kIntersect, True)
            c.drawPaint(fill(C(mix(P.YELLOW, P.INK, 0.15))))
            Ln = math.hypot(b[0] - a[0], b[1] - a[1])
            for i in range(-2, int(Ln // 8) + 2):
                px, py = a[0] + dx * i * 8, a[1] + dy * i * 8
                c.drawPath(poly_path([(px, py), (px + dx * 4, py + dy * 4), (px + dx * 10 + nx * 10, py + dy * 10 + ny * 10), (px + dx * 6 + nx * 10, py + dy * 6 + ny * 10)]), fill(C(P.INK)))
            c.restore()
            c.drawPath(band, stroke(C(P.INK), 1.0))

    # ---- 3. raised parts ---------------------------------------------------------
    def island_body(self):
        c = self.c
        c.save(); c.translate(5, 7); c.drawPath(self.island, blur(fill(C(SHADOW, 150)), 4)); c.restore()
        c.drawPath(self.island, shade(lin((625, 300), (681, 300), [C("#5562C4"), C("#2C3688")])))
        c.save(); c.clipPath(self.island, skia.ClipOp.kIntersect, True)
        for y in np.arange(300, 590, 9.0):
            c.drawLine(620, y + 12, 690, y - 12, stroke(C("#7C8AE6", 150), 1.3, cap=skia.Paint.kButt_Cap))
        # Plunger scale on the lane wall: how far the plunger is drawn back.
        pl = L["plunger"]
        for i in range(0, 11):
            y = pl["restTopY"] + i * pl["travel"] / 10.0
            colr = P.GREEN if i < 4 else P.YELLOW if i < 8 else P.PINK
            c.drawLine(681 - (7 if i % 5 == 0 else 4.5), y, 681, y, stroke(C(mix(colr, P.INK, 0.1)), 1.2, cap=skia.Paint.kButt_Cap))
        c.restore()
        c.drawPath(self.island, stroke(C(P.INK), 8.4)); c.drawPath(self.island, stroke(C(P.CH_LO), 6.2)); c.drawPath(self.island, stroke(C(P.CH_MID), 4.0))
        c.save(); c.translate(-0.8, -0.9); c.drawPath(self.island, stroke(C(P.CH_HI), 1.3)); c.restore()
        for (x, y) in ((630, 310), (630, 520), (676, 360), (676, 620), (676, 860)):
            self.bolt(x, y, 1.8)

    def shell_rail(self):
        c = self.c
        c.drawPath(self.shell, stroke(C(P.INK), 9.0))
        c.drawPath(self.shell, stroke(C(P.CH_LO), 6.6))
        c.drawPath(self.shell, stroke(C(P.CH_MID), 4.2))
        c.save(); c.translate(-0.8, -0.9); c.drawPath(self.shell, stroke(C(P.CH_HI), 1.3)); c.restore()

    def gate(self):
        """The inner arch: the right orbit wall, then the hinged one-way wire the launch passes through."""
        pts = ARCH[1:-1]
        self.tube(poly_path(pts, close=False), 2.4)
        for p in [q for q in pts if q[1] < 190][::4]:
            self.c.drawCircle(p[0], p[1], 3.0, fill(C(P.INK))); self.c.drawCircle(p[0], p[1], 2.0, fill(C(mix(P.ORANGE, P.INK, 0.1))))

    def walls(self):
        left = [(145, 420), (145, 300)] + [tuple(w[1]) for w in WALLS[15::2]]
        right = [(566, 420), (566, 300)] + [tuple(w[1]) for w in WALLS[16::2]]
        for pts in (left, right):
            self.tube(poly_path(pts, close=False), 5)
        for w in WALLS[13:15]:
            self.tube(self.seg(w[0], w[1]), w[2])
        # Lane dividers and the guides that run down to the flippers.
        for a, b in ((0, 1), (2, 3)):
            self.tube(poly_path([tuple(WALLS[a][0]), tuple(WALLS[a][1]), tuple(WALLS[b][1])], close=False), 5)
            self.bolt(*WALLS[a][0]); self.bolt(*WALLS[b][1])
        self.tube(poly_path([(321, 318), (356, 335), (391, 352), (391, 400)], close=False), 5)
        # Dead posts: bare metal with a bolt at each end.
        for d in DEAD:
            self.tube(self.seg(d[0], d[1]), d[2], base="#B9C6DA")
            self.bolt(*d[0]); self.bolt(*d[1])
        # Vanes: thin wire gates above the slingshots.
        for v in (L["slingVaneLeft"], L["slingVaneRight"]):
            self.tube(self.seg(v[1], v[2]), 1.4, shadow=(3, 4), sblur=1.6)
            self.bolt(v[1][0], v[1][1], 2.0); self.bolt(v[2][0], v[2][1], 2.0)

    def slingshots(self):
        c = self.c
        for tri in L["slingshots"]:
            pts = [tuple(p) for p in tri]
            gx = sum(p[0] for p in pts) / 3; gy = sum(p[1] for p in pts) / 3
            path = poly_path(pts)
            c.save(); c.translate(5, 7); c.drawPath(path, blur(stroke(C(SHADOW, 170), 9), 3)); c.drawPath(path, blur(fill(C(SHADOW, 170)), 3)); c.restore()
            # Rubber round three posts.
            c.drawPath(path, stroke(C(P.INK), 9.6)); c.drawPath(path, stroke(C("#7C7258"), 8.0))
            c.save(); c.translate(-0.7, -0.8); c.drawPath(path, stroke(C("#C4B994"), 4.4)); c.restore()
            # Glossy plastic over it, with wing stripes.
            inner = poly_path([(p[0] + (gx - p[0]) * 0.22, p[1] + (gy - p[1]) * 0.22) for p in pts])
            c.drawPath(inner, shade(lin((gx - 30, gy - 40), (gx + 30, gy + 40), [C("#19B7B0"), C("#0B6F78"), C("#063A48")])))
            c.save(); c.clipPath(inner, skia.ClipOp.kIntersect, True)
            sgn = 1 if gx < CX else -1
            for i, colr in enumerate((P.YELLOW, P.ORANGE, P.PINK)):
                c.drawLine(gx - sgn * 40, gy - 8 + i * 9, gx + sgn * 40, gy + 34 + i * 9, stroke(C(colr), 3.2, cap=skia.Paint.kButt_Cap))
            c.drawLine(pts[0][0] + sgn * 6, pts[0][1] + 16, gx - sgn * 6, gy + 8, blur(stroke(C(P.WHITE, 150), 2.2), 1.0))
            c.restore()
            c.drawPath(inner, stroke(C(P.INK), 1.2))
            for p in pts:
                c.drawCircle(p[0], p[1], 4.4, fill(C(P.INK))); c.drawCircle(p[0], p[1], 3.4, shade(chrome_sweep(p[0], p[1])))

    def targets(self):
        c = self.c
        for i, (a, b, r) in enumerate(TARGETS):
            c.save(); c.translate(4, 6); c.drawPath(self.seg(a, b), blur(stroke(C(SHADOW, 180), 2 * r + 2), 2.4)); c.restore()
            mx, my = (a[0] + b[0]) / 2, (a[1] + b[1]) / 2
            sgn = 1 if i == 0 else -1
            bx, by = mx + sgn * 20, my + 14
            bolt = poly_path([(bx + sgn * -5, by - 11), (bx + sgn * 4, by - 11), (bx, by - 2), (bx + sgn * 6, by - 2), (bx + sgn * -5, by + 12), (bx + sgn * -1.5, by + 2), (bx + sgn * -7, by + 2)])
            c.drawPath(bolt, stroke(C(P.INK), 2.4, join=skia.Paint.kMiter_Join)); c.drawPath(bolt, fill(C(mix(P.YELLOW, P.INK, 0.3))))

    def rails(self):
        """The two ramp rides: tinted wire-forms carried above the table on posts. Where a ride passes over the
        field it is drawn half clear with its shadow well off to the lower right, so that it cannot be taken
        for a wall; its posts stand only outside the playfield."""
        c = self.c
        for rail, colr in ((L["runwayRail"], P.SHOT["runway"]), (L["skywayRail"], P.SHOT["skyway"])):
            wire = dict(base=mix(colr, P.CH_MID, 0.45), hi=mix(colr, P.WHITE, 0.7), lo=mix(colr, P.INK, 0.6))
            pts = [(rail[0][0], 392.0)] + [tuple(p) for p in rail[1:]]
            cl = spline(pts, 16)
            a, b = offset(cl, 8.5), offset(cl, -8.5)
            over = [inside(SHELL, p[0], p[1]) and not inside(ISLAND, p[0], p[1]) for p in cl]
            # Shadow of the whole ride on the deck: twice as far off as a wall's, and soft.
            c.save(); c.translate(11, 15)
            c.drawPath(poly_path(cl, close=False), blur(stroke(C(SHADOW, 105), 19, cap=skia.Paint.kButt_Cap), 5.5)); c.restore()
            # Posts, outside the playfield only: base plate, shadow, column top.
            step = 0.0
            for i in range(1, len(cl)):
                step += math.hypot(cl[i][0] - cl[i - 1][0], cl[i][1] - cl[i - 1][1])
                if step >= 62.0 and not over[i] and not over[max(0, i - 3)] and not over[min(len(cl) - 1, i + 3)]:
                    step = 0.0
                    px, py = cl[i]
                    c.drawLine(px, py, px + 9, py + 12.5, blur(stroke(C(SHADOW, 190), 5.0), 1.6))
                    c.drawCircle(px, py, 6.2, fill(C(P.INK))); c.drawCircle(px, py, 5.2, shade(chrome_sweep(px, py)))
                    c.drawCircle(px, py, 2.4, fill(C(P.INK))); c.drawCircle(px - 0.4, py - 0.5, 1.4, fill(C(P.CH_HI)))

            def run(lo, hi, alpha):
                if hi - lo < 1:
                    return
                if alpha < 255:
                    c.saveLayerAlpha(None, alpha)
                step2 = 13.0
                for i in range(lo + 1, hi + 1):
                    step2 += math.hypot(cl[i][0] - cl[i - 1][0], cl[i][1] - cl[i - 1][1])
                    if step2 >= 26.0:
                        step2 = 0.0
                        self.tube(self.seg(a[i], b[i]), 1.0, shadow=None, **wire)
                for side in (a, b):
                    self.tube(poly_path(side[lo:hi + 1], close=False), 1.6, shadow=None, **wire)
                if alpha < 255:
                    c.restore()
            i = 0
            while i < len(cl) - 1:
                j = i
                while j < len(cl) - 1 and over[j + 1] == over[i]:
                    j += 1
                run(i, min(j + 1, len(cl) - 1), 150 if over[i] else 255)
                i = j + 1
            # The set-down: the wires end in an open cradle above the return lane, and the lane is painted for it.
            ex, ey = rail[-1]
            c.saveLayerAlpha(None, 170)
            cradle = skia.Path(); cradle.addArc(skia.Rect.MakeLTRB(ex - 11, ey - 11, ex + 11, ey + 11), 20, 140)
            self.tube(cradle, 1.6, shadow=None, **wire)
            c.restore()

    def landings(self):
        """Painted set-down marks at the foot of each ride (flat, under the ball)."""
        c = self.c
        for rail, colr in ((L["runwayRail"], P.SHOT["runway"]), (L["skywayRail"], P.SHOT["skyway"])):
            ex, ey = rail[-1]
            col = mix(colr, P.INK, 0.35)
            c.drawCircle(ex, ey, 15.5, fill(C(mix(colr, P.INK, 0.86))))
            c.drawCircle(ex, ey, 15.5, stroke(C(col), 1.6, PathEffect=skia.DashPathEffect.Make([4, 3], 0)))
            c.drawLine(ex - 7, ey, ex + 7, ey, stroke(C(col), 1.4, cap=skia.Paint.kButt_Cap))
            c.drawLine(ex, ey - 7, ex, ey + 7, stroke(C(col), 1.4, cap=skia.Paint.kButt_Cap))

    # ---- 4. inserts, baked unlit ----------------------------------------------------
    def inserts(self):
        for name, pos in LAMP.items():
            col = LAMP_COL[name]
            kind = L["lamps"][LAMP_NAMES.index(name)]["kind"]
            if kind == "arrow":
                aim = P.LAMP_AIM[name]
                ang = math.degrees(math.atan2(aim[1] - pos[1], aim[0] - pos[0])) + 90.0
                size = P.INSERT_ARROW_UNITS * (P.INSERT_ARROW_BRIEFING if name == "ArrowBriefing" else 1.0)
                self.sprite("insert_arrow.png", pos[0], pos[1], size, col, P.UNLIT_LEVEL, ang)
            else:
                dia = L["lampSizes"]["course"] if kind == "course" else L["lampSizes"]["status"]
                self.sprite("insert_round.png", pos[0], pos[1], dia * P.INSERT_ROUND_PER_DIAMETER, col, P.UNLIT_LEVEL)

    # ---- everything ------------------------------------------------------------------
    def render(self, dither=True):
        self.outside()
        self.deck()
        c = self.c
        self.zones()
        c.save(); c.clipPath(self.shell, skia.ClipOp.kIntersect, True)
        self.taxi_lines()
        self.emblem()
        self.nest_art()
        self.turbine()
        self.ramp_lip((151, 232), P.SHOT["runway"], "27 L")
        self.ramp_lip((478, 560), P.SHOT["skyway"], "09 R")
        self.hangar()
        self.briefing()
        self.roll_call()
        self.lower_field()
        self.inserts()
        c.restore()
        self.island_body()
        self.shell_rail()
        self.pockets()
        self.gate()
        self.walls()
        self.targets()
        self.hangar_walls()
        self.slingshots()
        self.rails()
        im = self.st.image()
        return ordered_dither(im, levels=20, cell=2) if dither else im


def main(path, sprites=None):
    if sprites:
        SPRITES[0] = sprites
    save_png(Table().render(), path)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "assets", "table", "background.png"))
