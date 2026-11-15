# Table A playfield base art (concept A, Flight Academy), drawn from tools/art/table_a/layout_a.json with the
# repository's skia pipeline (tools/art/common.py; hash-pinned tools/art/requirements.txt).
#
#   build/art-venv/bin/python tools/art/table_a/make_floor.py
#
# Writes to tools/art/table_a/out/:
#   floor_a.png       1536 x 1920 (2 px per unit), opaque: everything flush with the playfield: the floor
#                     (dark at the flippers, lighter toward the top), grid, runway, compass rose at the Air
#                     Pocket, radar rings under the jets, taxi lines, hold bars, the hangar apron, the drone
#                     track, cups and rollovers, the altimeter plate, the Prop Wash ground ring, and an unlit
#                     insert pocket for every lamp of lamp_plan.json. Under the floor-luminance budget.
#   background_a.png  floor_a plus every raised static part: walls, posts, the lane island, the shell rail,
#                     slingshot rubbers with their plastics, stand-ups, the G-FORCE housing and the flasher-dome
#                     bases. The game's table background for concept A.
#   gi_a.png          384 x 480 (half a pixel per unit), greyscale: the general illumination as a light map,
#                     drawn additively by the game in the Altitude GI colour (palette.py ALTITUDE_GI).
#   lamp_plan.json    every insert: id, name, shape, colour, centre, size, aim, and source: "layout" (a lamp of
#                     table/a-geometry's kLamps) or "proposed" (a concept A lamp the geometry does not have yet;
#                     a proposal for the geometry owner, CONCEPT.md section 7).
import json
import math
import os
import sys

import numpy as np
import skia
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.normpath(os.path.join(HERE, "..")))
import palette as P  # noqa: E402
from common import (C, Stage, blur, chrome_sweep, fill, font, hexrgb, lin, mix, ordered_dither, plus,  # noqa: E402
                    poly_path, rad, ramp, save_png, shade, stroke, text_path)

OUT = os.path.join(HERE, "out")
L = json.load(open(os.path.join(HERE, "layout_a.json")))
W, H = L["width"], L["height"]
CX = L["fieldCenterX"]
SHELL = [tuple(p) for p in L["shell"]]
SHADOW = "#04030E"
SPRITES = {}
META = {}
for name in ("sheets.json", "gimp_sheets.json"):
    META.update(json.load(open(os.path.join(OUT, name)))["sheets"])

# ---- floor tones (all under palette.LUM_DECK_MAX) -------------------------------------------------------
FLOOR_TOP, FLOOR_BOTTOM = "#1A2258", "#0B0E2C"
GRID, GRID_MAJOR = "#1C2560", "#232E70"
RUNWAY = "#141B4A"
PAINT_W, PAINT_Y, PAINT_A = "#3A4270", "#57491E", "#173E50"


# ---- the lamp plan ------------------------------------------------------------------------------------------
LAYOUT_LAMPS = [n for n, _ in P.LAMPS] + (
    ["DroneArmour%d" % i for i in range(1, 6)] + ["GForce%d" % i for i in range(1, 6)] +
    ["TaxiT", "TaxiA", "TaxiX", "TaxiI"] + ["Rank%d" % i for i in range(1, 8)] +
    ["Combo%dX" % i for i in range(2, 6)] + ["Bonus%dX" % i for i in range(2, 6)])
COLOUR = dict(P.LAMPS)
COLOUR.update({"DroneArmour%d" % i: P.BLUE for i in range(1, 6)})
COLOUR.update(dict(zip(["GForce%d" % i for i in range(1, 6)], [P.GREEN, P.GREEN, P.YELLOW, P.ORANGE, P.PINK])))
COLOUR.update({n: P.YELLOW for n in ("TaxiT", "TaxiA", "TaxiX", "TaxiI")})
COLOUR.update({"Rank%d" % i: P.ORCHID for i in range(1, 8)})
COLOUR.update({"Combo%dX" % i: P.AQUA for i in range(2, 6)})
COLOUR.update({"Bonus%dX" % i: P.PINK for i in range(2, 6)})
AIM = {"ArrowCircuitLeft": (118.0, 440.0), "ArrowRunway": (192.0, 425.0), "ArrowHangar": (279.0, 392.0),
       "ArrowBriefing": (441.0, 400.0), "ArrowTurbine": (356.0, 415.0), "ArrowSkyway": (519.0, 425.0),
       "ArrowCircuitRight": (593.0, 440.0)}
ALT_PLATE_AT = (173.0, 584.0)       # top-left of the altimeter plate (units)


def lamp_plan():
    plan = []
    for l in L["lamps"]:
        name = LAYOUT_LAMPS[l["id"]]
        kind = l["kind"]
        e = {"id": l["id"], "name": name, "c": l["c"], "colour": COLOUR[name], "source": "layout"}
        if kind == "arrow":
            e.update(shape="arrow_mission" if name == "ArrowBriefing" else "arrow", aim=AIM[name])
        elif kind == "course":
            e.update(shape="round", d=L["lampSizes"]["course"])
        elif kind == "status":
            e.update(shape="round", d=L["lampSizes"]["status"])
        elif name.startswith("DroneArmour"):
            e.update(shape="hex", d=P_HEX)
        elif name.startswith("Rank"):
            e.update(shape="chevron", d=18.0)
        else:
            e.update(shape="round", d=12.0)
        plan.append(e)
    nid = 100
    # proposals (CONCEPT.md section 7): a 3-lamp progress ladder behind each shot arrow
    for e in [p for p in plan if p.get("shape", "").startswith("arrow")]:
        (lx, ly), (ax, ay) = e["c"], e["aim"]
        dx, dy = ax - lx, ay - ly
        n = math.hypot(dx, dy)
        dx, dy = dx / n, dy / n
        for k in range(3):
            d = 23.0 + 13.0 * k
            plan.append({"id": nid, "name": e["name"].replace("Arrow", "Ladder") + str(k + 1),
                         "c": [round(lx - dx * d, 1), round(ly - dy * d, 1)], "colour": e["colour"], "shape": "pip",
                         "d": 10.0, "source": "proposed"})
            nid += 1
    # Altitude FL100..FL500: the altimeter's sockets
    sock = META["altitude_plate"]["sockets"]
    for k, gi in enumerate(P.ALTITUDE_GI):
        plan.append({"id": nid, "name": "AltitudeFL%d00" % (k + 1),
                     "c": [ALT_PLATE_AT[0] + sock["centreXUnits"], ALT_PLATE_AT[1] + sock["centreYUnits"][k]],
                     "colour": gi, "shape": "rung", "source": "proposed"})
        nid += 1
    # F-L-A-P-S: the letter insert round each flush rollover
    for r, ch in zip([r for r in L["rollovers"] if r["group"] == "flaps"], "FLAPS"):
        plan.append({"id": nid, "name": "Flaps" + ch, "c": r["c"], "colour": P.CREAM, "shape": "flaps",
                     "letter": ch, "source": "proposed"})
        nid += 1
    # BOGEY (beside the left drop bank of CONCEPT G2) and RADAR (mirror), Safety Post, Multiball, Deck lit
    for k, (x, y) in enumerate(((188.0, 505.0), (190.0, 525.0), (192.0, 545.0))):
        plan.append({"id": nid, "name": "Bogey%d" % (k + 1), "c": [x, y], "colour": P.ORANGE, "shape": "round",
                     "d": 12.0, "source": "proposed"})
        plan.append({"id": nid + 1, "name": "Radar%d" % (k + 1), "c": [round(2 * CX - x, 1), y], "colour": P.AQUA,
                     "shape": "round", "d": 12.0, "source": "proposed"})
        nid += 2
    for name, c, col in (("SafetyPost", (CX, 904.0), P.GREEN), ("Multiball", (312.0, 530.0), P.YELLOW),
                         ("DeckLit", (540.0, 498.0), P.PINK)):
        plan.append({"id": nid, "name": name, "c": list(c), "colour": col, "shape": "round", "d": 14.0,
                     "source": "proposed"})
        nid += 1
    return plan


P_HEX = 11.0


# ---- helpers ----------------------------------------------------------------------------------------------
def frame_image(sheet, label):
    key = (sheet, label)
    if key not in SPRITES:
        sh = META[sheet]
        im = Image.open(os.path.join(OUT, sh["file"])).convert("RGBA")
        i = sh["frames"].index(label)
        r, c = divmod(i, sh["columns"])
        cw, ch = sh["cellPixels"]
        crop = np.ascontiguousarray(np.asarray(im.crop((c * cw, r * ch, (c + 1) * cw, (r + 1) * ch))))
        SPRITES[key] = skia.Image.fromarray(crop, colorType=skia.kRGBA_8888_ColorType,
                                            alphaType=skia.kUnpremul_AlphaType)
    return SPRITES[key]


def draw_frame(c, sheet, label, x, y, centre=True, angle=0.0, tint=None, level=1.0):
    sh = META[sheet]
    img = frame_image(sheet, label)
    w, h = sh["cellUnits"]
    paint = skia.Paint(AntiAlias=True)
    if tint is not None:
        r, g, b = hexrgb(tint)
        paint.setColorFilter(skia.ColorFilters.Blend(skia.Color(int(r * level), int(g * level), int(b * level)),
                                                     skia.BlendMode.kModulate))
    c.save()
    c.translate(x, y)
    c.rotate(angle)
    rect = skia.Rect.MakeXYWH(-w / 2, -h / 2, w, h) if centre else skia.Rect.MakeXYWH(0, 0, w, h)
    c.drawImageRect(img, rect, skia.SamplingOptions(skia.FilterMode.kLinear, skia.MipmapMode.kNone), paint)
    c.restore()


def lens(c, path, colour, level):
    """An unlit insert lens: ink bezel, a domed tint of its colour, a small highlight."""
    b = path.getBounds()
    cx, cy, r = b.centerX(), b.centerY(), max(b.width(), b.height()) / 2
    c.drawPath(path, stroke(C(P.INK), 1.6))
    lo = mix(P.INK, colour, level * 0.55)
    hi = mix(P.INK, colour, level * 1.15)
    c.drawPath(path, shade(rad((cx - r * 0.25, cy - r * 0.3), r * 1.3, [C(hi), C(lo)])))
    c.save()
    c.clipPath(path, skia.ClipOp.kIntersect, True)
    c.drawCircle(cx - r * 0.35, cy - r * 0.4, r * 0.32, blur(fill(C("#FFFFFF", 38)), r * 0.15))
    c.restore()


def hexagon(cx, cy, r):
    return poly_path([(cx + r * math.cos(math.radians(-90 + 60 * k)), cy + r * math.sin(math.radians(-90 + 60 * k)))
                      for k in range(6)])


def chevron_path(cx, cy, w, h):
    return poly_path([(cx - w / 2, cy + h / 2), (cx, cy - h / 2), (cx + w / 2, cy + h / 2), (cx + w / 2 - 4, cy + h / 2),
                      (cx, cy - h / 2 + 5), (cx - w / 2 + 4, cy + h / 2)])


def tube(c, path, r, base=P.CH_MID, hi=P.CH_HI, lo=P.CH_LO, shadow=(4.5, 6.0), sblur=2.6):
    """Raised chrome-capped rail, as make_table.py draws walls (highlights held near LUM_STATIC_MAX)."""
    if shadow:
        c.save(); c.translate(*shadow); c.drawPath(path, blur(stroke(C(SHADOW, 165), 2 * r + 1), sblur)); c.restore()
    c.drawPath(path, stroke(C(P.INK), 2 * r + 1.6))
    c.drawPath(path, stroke(C(lo), 2 * r))
    c.save(); c.translate(-r * 0.12, -r * 0.14); c.drawPath(path, stroke(C(base), 2 * r * 0.72)); c.restore()
    c.save(); c.translate(-r * 0.34, -r * 0.36); c.drawPath(path, stroke(C(hi), 2 * r * 0.26)); c.restore()


def seg(a, b):
    return poly_path([tuple(a), tuple(b)], close=False)


def stencil(c, txt, x, y, colour, size, angle=0.0, name="SairaStencilOne-Regular.ttf", tracking=1.0):
    f = font(name, size)
    c.save(); c.translate(x, y); c.rotate(angle)
    p, _ = text_path(txt, f, 0, 0, tracking, "center")
    c.drawPath(p, fill(C(colour)))
    c.restore()


# ---- the floor ------------------------------------------------------------------------------------------------
class Floor:
    def __init__(self):
        self.st = Stage(W, H, 2, 2)
        self.c = self.st.c
        self.shell = poly_path(SHELL)
        self.plan = lamp_plan()

    def outside(self):
        st = self.st
        xs, ys = st.grid()
        chk = (np.floor(xs / 16.0) + np.floor(ys / 16.0)) % 2
        fade = np.clip(1.0 - ys / 260.0, 0, 1) ** 1.2
        st.put(ramp([(0, "#07061A"), (1, "#2C2270")], chk * fade * 0.8))
        self.c = st.reopen()

    def base(self):
        """Dark at the flippers, lighter toward the top, a soft spot over the mid-field, a panel grid."""
        st = self.st
        hw, hh = st.w // st.ss, st.h // st.ss
        ys, xs = np.mgrid[0:hh, 0:hw].astype(np.float32)
        xs = (xs + 0.5) / st.scale
        ys = (ys + 0.5) / st.scale
        t = np.clip((ys - 80.0) / 860.0, 0, 1)
        col = ramp([(0, FLOOR_TOP), (0.55, mix(FLOOR_TOP, FLOOR_BOTTOM, 0.45)), (1, FLOOR_BOTTOM)], t)
        spot = np.exp(-(((xs - CX) / 300.0) ** 2 + ((ys - 470.0) / 380.0) ** 2))
        col = col + spot[..., None] * np.array([3, 5, 12], np.float32)
        gx = np.abs(((xs - CX) / 24.0 + 0.5) % 1.0 - 0.5) * 24.0
        gy = np.abs(((ys - 300.0) / 24.0 + 0.5) % 1.0 - 0.5) * 24.0
        line = np.clip(1.0 - np.minimum(gx, gy) / 0.7, 0, 1)
        gxm = np.abs(((xs - CX) / 96.0 + 0.5) % 1.0 - 0.5) * 96.0
        gym = np.abs(((ys - 300.0) / 96.0 + 0.5) % 1.0 - 0.5) * 96.0
        major = np.clip(1.0 - np.minimum(gxm, gym) / 0.9, 0, 1)
        col = col + line[..., None] * (np.array(hexrgb(GRID), np.float32) - col) * 0.5
        col = col + major[..., None] * (np.array(hexrgb(GRID_MAJOR), np.float32) - col) * 0.6
        rng = np.random.default_rng(1994)
        col = col + rng.normal(0, 0.9, (hh, 1, 1)).astype(np.float32)
        im = Image.fromarray(np.clip(col, 0, 255).astype(np.uint8), "RGB").resize((st.w, st.h), Image.BILINEAR)
        st.put(np.asarray(im), clip=self.shell)
        self.c = st.c

    def runway(self):
        c = self.c
        x0, x1, y0, y1 = CX - 58, CX + 58, 455.0, 945.0
        c.drawRect(skia.Rect.MakeLTRB(x0, y0, x1, y1), fill(C(RUNWAY, 150)))
        for x in (x0 + 3, x1 - 3):
            c.drawLine(x, y0, x, y1, stroke(C(PAINT_W), 2.0, cap=skia.Paint.kButt_Cap))
        y = y0 + 10
        while y < y1 - 30:
            c.drawLine(CX, y, CX, y + 20, stroke(C(PAINT_W), 3.0, cap=skia.Paint.kButt_Cap))
            y += 42
        for k in range(8):                       # threshold piano keys above the flipper gap
            x = x0 + 12 + k * 13.3
            if abs(x + 3 - CX) < 8:
                continue
            c.drawRect(skia.Rect.MakeXYWH(x, 792, 6, 34), fill(C(PAINT_W)))
        stencil(c, "36", CX, 778, PAINT_W, 22)
        for k in range(3):                       # aiming-point blocks
            for sd in (-1, 1):
                c.drawRect(skia.Rect.MakeXYWH(CX + sd * 30 - 5, 560 + k * 60, 10, 22), fill(C(RUNWAY)))
                c.drawRect(skia.Rect.MakeXYWH(CX + sd * 30 - 4, 561 + k * 60, 8, 20), stroke(C(PAINT_W, 120), 1.0))

    def compass(self):
        c = self.c
        x, y = L["airPocket"]["c"]
        R = 50.0
        c.drawCircle(x, y, R, stroke(C(PAINT_A), 2.0))
        c.drawCircle(x, y, R - 7, stroke(C(PAINT_A), 1.0))
        for k in range(72):
            a = math.radians(k * 5)
            r0 = R - (6 if k % 9 == 0 else 3)
            c.drawLine(x + r0 * math.cos(a), y + r0 * math.sin(a), x + R * math.cos(a), y + R * math.sin(a),
                       stroke(C(PAINT_A), 1.0, cap=skia.Paint.kButt_Cap))
        for k in range(8):
            a = math.radians(-90 + 45 * k)
            r = R - 9 if k % 2 == 0 else R - 22
            w = 7.0 if k % 2 == 0 else 5.0
            tip = (x + r * math.cos(a), y + r * math.sin(a))
            l = (x + w * math.cos(a - math.pi / 2), y + w * math.sin(a - math.pi / 2))
            rr = (x + w * math.cos(a + math.pi / 2), y + w * math.sin(a + math.pi / 2))
            c.drawPath(poly_path([tip, l, (x, y)]), fill(C(mix(PAINT_A, P.INK, 0.2))))
            c.drawPath(poly_path([tip, rr, (x, y)]), fill(C(PAINT_A)))
        stencil(c, "N", x, y - R - 4, PAINT_A, 11)

    def radar(self):
        c = self.c
        x, y = CX, 222.0
        c.save(); c.clipPath(self.shell, skia.ClipOp.kIntersect, True)
        for r in (52.0, 92.0, 132.0):
            c.drawCircle(x, y, r, stroke(C(PAINT_A), 1.6, PathEffect=skia.DashPathEffect.Make([10, 6], 0)))
        wedge = skia.Path()
        wedge.moveTo(x, y)
        wedge.arcTo(skia.Rect.MakeLTRB(x - 132, y - 132, x + 132, y + 132), -150, 40, False)
        wedge.close()
        c.drawPath(wedge, shade(skia.GradientShader.MakeSweep(x, y, [C(PAINT_A, 0), C(PAINT_A, 0), C(PAINT_A, 120)],
                                                              [0.0, 0.58, 0.69])))
        c.restore()

    def taxi_lines(self):
        c = self.c
        for x in (114.0, 2 * CX - 114.0):
            c.drawLine(x, 300, x, 610, stroke(C(PAINT_Y), 2.0, cap=skia.Paint.kButt_Cap,
                                               PathEffect=skia.DashPathEffect.Make([14, 8], 0)))
        for (mx0, mx1) in ((165.0, 219.0), (492.0, 546.0)):          # hold-short bars at the ramp mouths
            c.drawLine(mx0, 441, mx1, 441, stroke(C(PAINT_Y), 2.0, cap=skia.Paint.kButt_Cap))
            c.drawLine(mx0, 446, mx1, 446, stroke(C(PAINT_Y), 2.0, cap=skia.Paint.kButt_Cap,
                                                  PathEffect=skia.DashPathEffect.Make([6, 4], 0)))
        # hangar apron: hazard hatching in front of the bay doors, bay boxes inside
        c.save()
        c.clipRect(skia.Rect.MakeLTRB(244, 399, 314, 407))
        for k in range(-2, 14):
            x = 244 + k * 7
            c.drawLine(x, 407, x + 8, 399, stroke(C(PAINT_Y), 2.4, cap=skia.Paint.kButt_Cap))
        c.restore()
        for (bx, by), n in zip(L["lockBays"], ("1", "2")):
            c.drawRect(skia.Rect.MakeXYWH(bx - 17, by - 17, 34, 34), stroke(C(PAINT_W), 1.4))
            stencil(c, n, bx, by + 24, PAINT_W, 9)
        # drone track
        d = L["drone"]
        y = d["y"]
        c.drawLine(d["leftX"] - 16, y, d["rightX"] + 16, y, stroke(C(PAINT_Y), 2.0, cap=skia.Paint.kButt_Cap,
                                                                 PathEffect=skia.DashPathEffect.Make([5, 4], 0)))
        for x in (d["leftX"] - 18, d["rightX"] + 18):
            c.drawLine(x, y - 6, x, y + 6, stroke(C(PAINT_Y), 2.4, cap=skia.Paint.kButt_Cap))
        # plunger lane: flight-line chevrons
        lx = L["lane"]["centerX"]
        for k in range(6):
            yy = 860 - k * 70
            ch = poly_path([(lx - 9, yy + 5), (lx, yy - 4), (lx + 9, yy + 5)], close=False)
            c.drawPath(ch, stroke(C(PAINT_Y), 2.4, join=skia.Paint.kMiter_Join))

    def cups(self):
        c = self.c
        for h in L["holes"]:
            if h["type"] in ("subway", "hangar"):
                (x, y), (ax, ay) = h["c"], h["halfSize"]
                rr = skia.RRect.MakeRectXY(skia.Rect.MakeLTRB(x - ax, y - ay, x + ax, y + ay), 9, 9)
                c.drawRRect(rr, fill(C("#05051A")))
                c.drawRRect(rr, stroke(C(mix(P.CH_LO, P.INK, 0.3)), 2.0))
                c.drawRRect(rr, shade(rad((x, y), max(ax, ay), [C(P.INK, 0), C(P.INK, 200)])))
        for r in L["rollovers"]:
            (x, y), g = r["c"], r["group"]
            if g == "flaps":
                continue
            hw, hh = r["halfSize"]
            c.drawRRect(skia.RRect.MakeRectXY(skia.Rect.MakeLTRB(x - 3.2, y - hh + 2, x + 3.2, y + hh - 2), 3, 3),
                        fill(C("#06061C")))
            c.drawCircle(x, y, 3.0, fill(C(mix(P.CH_MID, P.INK, 0.55))))

    def inserts(self):
        c = self.c
        lvl = P.UNLIT_LEVEL
        for e in self.plan:
            (x, y), col, shape = e["c"], e["colour"], e["shape"]
            if shape in ("arrow", "arrow_mission"):
                ax, ay = e["aim"]
                ang = math.degrees(math.atan2(ay - y, ax - x)) + 90.0
                draw_frame(c, "inserts_arrows", shape, x, y, angle=ang, tint=col, level=lvl)
            elif shape == "pip":
                draw_frame(c, "inserts_arrows", "pip", x, y, tint=col, level=lvl)
            elif shape == "rung":
                draw_frame(c, "inserts_altitude", e["name"][-5:], x, y, tint=col, level=lvl)
            elif shape == "flaps":
                draw_frame(c, "inserts_flaps", e["letter"], x, y, tint=col, level=lvl)
                c.drawCircle(x, y - 8.0, 2.6, fill(C(P.INK)))      # the flush rollover's button, above the letter
                c.drawCircle(x, y - 8.0, 1.9, fill(C(mix(P.CH_MID, P.INK, 0.45))))
            elif shape == "hex":
                lens(c, hexagon(x, y, e["d"] / 2 + 1.5), col, lvl)
            elif shape == "chevron":
                lens(c, chevron_path(x, y, e["d"], 9.0), col, lvl)
            else:
                lens(c, poly_path([(x + e["d"] / 2 * math.cos(a), y + e["d"] / 2 * math.sin(a))
                                   for a in np.linspace(0, 2 * math.pi, 40, endpoint=False)]), col, lvl)

    def plates(self):
        c = self.c
        draw_frame(c, "altitude_plate", "base", ALT_PLATE_AT[0], ALT_PLATE_AT[1], centre=False)
        draw_frame(c, "propwash_ring", "ring", *L["propWash"]["c"])

    # ---- raised static parts ---------------------------------------------------------------------------
    def raised(self):
        c = self.c
        island = poly_path([tuple(p) for p in L["laneIsland"]])
        c.save(); c.translate(5, 7); c.drawPath(island, blur(fill(C(SHADOW, 170)), 3)); c.restore()
        c.drawPath(island, shade(lin((600, 200), (640, 540), [C("#2A2468"), C("#120E34")])))
        c.drawPath(island, stroke(C(P.INK), 1.6))
        tube(c, poly_path([tuple(p) for p in L["innerArch"]][1:-1], close=False), 2.4)
        tube(c, poly_path([tuple(p) for p in L["laneGate"]], close=False), 1.6, shadow=(3, 4), sblur=1.6)
        for w in L["walls"]:
            tube(c, seg(w["a"], w["b"]), w["r"])
        for d in L["deadPosts"]:
            tube(c, seg(d["a"], d["b"]), d["r"], base="#B9C6DA")
        for v in (L["slingVaneLeft"], L["slingVaneRight"]):
            tube(c, seg(v[1], v[2]), 1.4, shadow=(3, 4), sblur=1.6)
        for t in [s["shape"] for s in L["standups"][:4]] + [t["shape"] for t in L["targets"]]:
            c.save(); c.translate(4, 6); c.drawPath(seg(t["a"], t["b"]), blur(stroke(C(SHADOW, 180), 2 * t["r"] + 2), 2.4)); c.restore()
            c.drawPath(seg(t["a"], t["b"]), stroke(C(P.INK), 2 * t["r"] + 1.4))
            c.drawPath(seg(t["a"], t["b"]), stroke(C(mix(P.YELLOW, P.INK, 0.25)), 2 * t["r"] - 1.0))
            c.save(); c.translate(-0.8, -0.8); c.drawPath(seg(t["a"], t["b"]), stroke(C(mix(P.YELLOW, P.WHITE, 0.3)), 1.4)); c.restore()
        for s in L["spinners"]:
            for p in (s["a"], s["b"]):
                c.drawCircle(p[0], p[1], 3.6, fill(C(P.INK))); c.drawCircle(p[0], p[1], 2.6, shade(chrome_sweep(*p)))
        for tri in L["slingshots"]:
            path = poly_path([tuple(p) for p in tri])
            c.save(); c.translate(5, 7); c.drawPath(path, blur(stroke(C(SHADOW, 170), 9), 3)); c.restore()
            c.drawPath(path, stroke(C(P.INK), 9.6)); c.drawPath(path, stroke(C("#7C7258"), 8.0))
            c.save(); c.translate(-0.7, -0.8); c.drawPath(path, stroke(C("#C4B994"), 4.4)); c.restore()
            c.drawPath(path, fill(C("#0E1030")))
            for p in tri:
                c.drawCircle(p[0], p[1], 4.4, fill(C(P.INK))); c.drawCircle(p[0], p[1], 3.4, shade(chrome_sweep(*p)))
        for name in ("plastic_sling_left", "plastic_sling_right"):
            draw_frame(c, name, "plastic", *META[name]["anchor"], centre=False)
        draw_frame(c, "gforce", "housing", *META["gforce"]["anchor"], centre=False)
        domes = [(279.0, 296.0), (473.0, 357.0), (391.0, 360.0), (145.0, 392.0), (566.0, 392.0), (324.0, 168.0),
                 (150.0, 692.0), (561.0, 692.0)]
        names = ["hangar", "mission", "afterburner", "runway", "skyway", "nest", "sling_left", "sling_right"]
        for (x, y), n in zip(domes, names):
            draw_frame(c, "flasher_domes", n + "_off", x, y)
        sh = self.shell
        c.drawPath(sh, stroke(C(P.INK), 9.0)); c.drawPath(sh, stroke(C(P.CH_LO), 6.6)); c.drawPath(sh, stroke(C(P.CH_MID), 4.2))

    def render(self):
        self.outside()
        self.base()
        c = self.c
        c.save(); c.clipPath(self.shell, skia.ClipOp.kIntersect, True)
        self.runway()
        self.compass()
        self.radar()
        self.taxi_lines()
        self.cups()
        self.plates()
        self.inserts()
        c.restore()
        floor = ordered_dither(self.st.image(), levels=24, cell=2)
        self.st.reopen()
        self.c = self.st.c
        self.raised()
        return floor, ordered_dither(self.st.image(), levels=24, cell=2)


def gi_map():
    """Greyscale light map, half a pixel per unit: 40 rim bulbs, warm pools at the slingshots and the lower
    guides, nothing over the open middle. The game tints it with the Altitude GI colour, additively."""
    s = 0.5
    w, h = int(W * s), int(H * s)
    ys, xs = np.mgrid[0:h, 0:w].astype(np.float64)
    xs = (xs + 0.5) / s
    ys = (ys + 0.5) / s
    acc = np.zeros((h, w))
    pts = np.array(SHELL)
    seglen = np.hypot(*np.diff(pts, axis=0).T)
    cum = np.concatenate([[0], np.cumsum(seglen)])
    inner = [p for p in SHELL]
    for k in range(P.GI_BULBS):
        t = (k + 0.5) / P.GI_BULBS * cum[-1]
        i = min(np.searchsorted(cum, t) - 1, len(pts) - 2)
        f = (t - cum[i]) / max(seglen[i], 1e-6)
        x, y = pts[i] + (pts[i + 1] - pts[i]) * f
        dx, dy = pts[i + 1] - pts[i]
        n = math.hypot(dx, dy) or 1.0
        nx, ny = -dy / n, dx / n
        if not inside(inner, x + nx * 5, y + ny * 5):
            nx, ny = -nx, -ny
        bx, by = x + nx * 14, y + ny * 14
        if by > 940 or bx > L["lane"]["wallX"]:
            continue
        acc += np.exp(-(((xs - bx) ** 2 + (ys - by) ** 2) / (2 * 22.0 ** 2)))
    for tri in L["slingshots"]:
        gx = sum(p[0] for p in tri) / 3
        gy = sum(p[1] for p in tri) / 3
        acc += 0.8 * np.exp(-(((xs - gx) ** 2 + (ys - gy) ** 2) / (2 * 34.0 ** 2)))
    acc = np.clip(acc / 1.15, 0, 1)
    mask = np.array([[inside(SHELL, x, y) for x in xs[0, :]] for y in ys[:, 0]], dtype=np.float64)
    out = (np.clip(acc * mask, 0, 1) * 255.0).round().astype(np.uint8)
    return Image.fromarray(np.stack([out] * 3, axis=2), "RGB")


def inside(poly, x, y):
    n = False
    for i in range(len(poly)):
        x1, y1 = poly[i]
        x2, y2 = poly[i - 1]
        if (y1 > y) != (y2 > y) and x < (x2 - x1) * (y - y1) / (y2 - y1) + x1:
            n = not n
    return n


def main():
    fl = Floor()
    floor, background = fl.render()
    save_png(floor, os.path.join(OUT, "floor_a.png"))
    save_png(background, os.path.join(OUT, "background_a.png"))
    save_png(gi_map(), os.path.join(OUT, "gi_a.png"))
    with open(os.path.join(OUT, "lamp_plan.json"), "w") as f:
        json.dump({"note": "every insert of concept A; source 'proposed' = not yet in table/a-geometry kLamps",
                   "lamps": fl.plan,
                   "counts": {"layout": sum(e["source"] == "layout" for e in fl.plan),
                              "proposed": sum(e["source"] == "proposed" for e in fl.plan)}},
                  f, indent=1, sort_keys=True)
        f.write("\n")
    print("floor: %d inserts (%d layout, %d proposed)" % (
        len(fl.plan), sum(e["source"] == "layout" for e in fl.plan), sum(e["source"] == "proposed" for e in fl.plan)))


if __name__ == "__main__":
    main()
