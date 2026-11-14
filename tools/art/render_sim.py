# A stand-in for the game's renderer, used for review mock frames and for the visibility test: it draws the
# table the way TableRenderer does (background, lamps, glows, targets, doors, spinner, kickback, bumpers,
# plunger, flippers, then balls with their shadows last) from the shipped sprites and layout.json.
import math
import os

import numpy as np
from PIL import Image, ImageFilter

import palette as P
from common import ROOT, hexrgb, layout

L = layout()
A = os.path.join(ROOT, "assets")
LAMP_NAMES = [n for n, _ in P.LAMPS]
LAMP_COL = dict(P.LAMPS)
LAMPS = {LAMP_NAMES[l["id"]]: (tuple(l["c"]), l["kind"]) for l in L["lamps"]}
_cache = {}


def load(rel):
    if rel not in _cache:
        _cache[rel] = Image.open(os.path.join(A, rel)).convert("RGBA")
    return _cache[rel]


def tint(im, colour, level=1.0):
    a = np.asarray(im).astype(np.float32)
    a[..., :3] *= np.array(hexrgb(colour), np.float32) / 255.0 * level
    return Image.fromarray(np.clip(a, 0, 255).astype(np.uint8), "RGBA")


class Sim:
    def __init__(self, k, background=None):
        self.k = float(k)
        bg = background or load("table/background.png")
        self.im = bg.convert("RGB").resize((int(round(768 * k)), int(round(960 * k))), Image.LANCZOS)

    # ---- drawing in units -----------------------------------------------------
    def over(self, sp, cx, cy, w, h=None, angle=0.0, alpha=1.0):
        """Draw sprite centred at (cx, cy), w x h units, rotated clockwise by angle degrees."""
        h = h if h is not None else w * sp.height / sp.width
        s = sp.resize((max(1, int(round(w * self.k))), max(1, int(round(h * self.k)))), Image.LANCZOS)
        if angle:
            s = s.rotate(-angle, resample=Image.BICUBIC, expand=True)
        if alpha != 1.0:
            a = np.asarray(s).astype(np.float32); a[..., 3] *= alpha; s = Image.fromarray(a.astype(np.uint8), "RGBA")
        self.im.paste(s, (int(round(cx * self.k - s.width / 2)), int(round(cy * self.k - s.height / 2))), s)

    def glow(self, cx, cy, radius, colour, strength):
        strength = min(strength, P.GLOW_STRENGTH_MAX)
        g = tint(load("sprites/glow.png"), colour)
        n = max(2, int(round(radius * 2 * self.k)))
        g = np.asarray(g.resize((n, n), Image.LANCZOS)).astype(np.float32)
        x, y = int(round(cx * self.k - n / 2)), int(round(cy * self.k - n / 2))
        x0, y0, x1, y1 = max(0, x), max(0, y), min(self.im.width, x + n), min(self.im.height, y + n)
        if x1 <= x0 or y1 <= y0:
            return
        d = np.asarray(self.im.crop((x0, y0, x1, y1))).astype(np.float32)
        part = g[y0 - y:y1 - y, x0 - x:x1 - x]
        d += part[..., :3] * part[..., 3:4] / 255.0 * strength
        self.im.paste(Image.fromarray(np.clip(d, 0, 255).astype(np.uint8), "RGB"), (x0, y0))

    def lamp(self, name, lit=True):
        (x, y), kind = LAMPS[name]
        col = LAMP_COL[name]
        if not lit:
            return
        if kind == "arrow":
            aim = P.LAMP_AIM[name]
            ang = math.degrees(math.atan2(aim[1] - y, aim[0] - x)) + 90.0
            size = P.INSERT_ARROW_UNITS * (P.INSERT_ARROW_BRIEFING if name == "ArrowBriefing" else 1.0)
            self.over(tint(load("sprites/insert_arrow.png"), col, P.lit_level(col)), x, y, size, angle=ang)
            self.glow(x, y, size * P.GLOW_RADIUS_ARROW, col, P.GLOW_STRENGTH_LAMP)
        else:
            dia = L["lampSizes"]["course"] if kind == "course" else L["lampSizes"]["status"]
            self.over(tint(load("sprites/insert_round.png"), col, P.lit_level(col)), x, y, dia * P.INSERT_ROUND_PER_DIAMETER)
            self.glow(x, y, dia * P.GLOW_RADIUS_ROUND, col, P.GLOW_STRENGTH_LAMP)

    def furniture(self, lit=(), flash=(), doors_up=True, marked=(), targets_lit=(0, 1), spinner_frame=0,
                  plunger_pull=0.0, flippers=(False, False), kickback=True):
        """Everything but the balls. lit: lamp names (or 'all'); flash: bumper indices; marked: door indices."""
        names = LAMP_NAMES if lit == "all" else lit
        for n in names:
            self.lamp(n)
        for i, t in enumerate(L["targets"]):
            a, b = t["shape"]["a"], t["shape"]["b"]
            ln = math.hypot(b[0] - a[0], b[1] - a[1])
            self.over(tint(load("sprites/bar.png"), P.TINT["turbo_target"], 1.0 if i in targets_lit else 0.45),
                      (a[0] + b[0]) / 2, (a[1] + b[1]) / 2, 48 * ln / 34.0, 14, angle=math.degrees(math.atan2(b[1] - a[1], b[0] - a[0])))
        if doors_up:
            for i, d in enumerate(L["bayDoors"]):
                x, y = (d["a"][0] + d["b"][0]) / 2, d["a"][1]
                self.over(tint(load("sprites/door.png"), P.TINT["door_marked" if i in marked else "door"]), x, y, 28, 16)
                if i in marked:
                    self.glow(x, y, 14, P.ORANGE, 0.30)
        sa, sb = L["spinner"]["a"], L["spinner"]["b"]
        self.over(load("sprites/spinner_%d.png" % spinner_frame), (sa[0] + sb[0]) / 2, sa[1], 80, 24)
        if kickback:
            self.over(load("sprites/kickback.png"), L["kickback"]["c"][0], L["kickback"]["c"][1] + 1, 28, 44)
        for i, b in enumerate(L["bumpers"]):
            size = 64.0 * b["r"] / 30.0 * (1.08 if i in flash else 1.0)
            self.over(tint(load("sprites/bumper.png"), P.TINT["bumper_flash" if i in flash else "bumper"]), b["c"][0], b["c"][1], size)
            self.over(load("sprites/bumper_cap.png"), b["c"][0], b["c"][1], size)
            if i in flash:
                self.glow(b["c"][0], b["c"][1], b["r"] * 2.2, P.YELLOW, 0.40)
        pl = L["plunger"]
        self.over(load("sprites/plunger.png"), L["lane"]["centerX"], pl["restTopY"] + plunger_pull + pl["height"] / 2, pl["width"], pl["height"])
        fl = load("sprites/flipper.png")
        for i, f in enumerate(L["flippers"]):
            ang = math.degrees(f["active"] if flippers[i] else f["rest"])
            sp = fl if i == 0 else fl.transpose(Image.FLIP_TOP_BOTTOM)
            # Rotate about the pivot (20, 20 units into the sprite): put the pivot at the centre of a square first.
            half = 4 * 118
            sq = Image.new("RGBA", (half * 2, half * 2), (0, 0, 0, 0)); sq.paste(sp, (half - 80, half - 80))
            self.over(sq, f["pivot"][0], f["pivot"][1], half * 2 / 4.0, angle=ang)

    def ball(self, x, y, scale=1.0):
        k = self.k
        r = L["ballRadius"] * scale
        # Contact shadow (the renderer draws glow.png with a darkening blend; this is the same shape).
        n = int(round(r * 4.2 * k))
        yy, xx = np.mgrid[0:n, 0:n].astype(np.float32)
        d = np.hypot(xx - n / 2, yy - n / 2) / (r * 1.5 * k)
        a = np.clip(1 - d, 0, 1) ** 1.2 * P.BALL_SHADOW_ALPHA
        sh = np.zeros((n, n, 4), np.uint8); sh[..., :3] = hexrgb("#05030F"); sh[..., 3] = (np.clip(a * 1.25, 0, P.BALL_SHADOW_ALPHA) * 255).astype(np.uint8)
        lift = (scale - 1.0) * 40.0
        ox, oy = P.BALL_SHADOW_OFFSET
        s = Image.fromarray(sh, "RGBA")
        self.im.paste(s, (int(round((x + ox + lift) * k - n / 2)), int(round((y + oy + lift) * k - n / 2))), s)
        self.over(load("sprites/ball.png"), x, y, P.BALL_SPRITE_UNITS * scale)


def rail_point(rail, t):
    """Point a fraction t along a ramp rail polyline (how the game moves a ball on its ride)."""
    seg = [math.hypot(rail[i + 1][0] - rail[i][0], rail[i + 1][1] - rail[i][1]) for i in range(len(rail) - 1)]
    d = t * sum(seg)
    for i, s in enumerate(seg):
        if d <= s:
            f = d / s
            return rail[i][0] + (rail[i + 1][0] - rail[i][0]) * f, rail[i][1] + (rail[i + 1][1] - rail[i][1]) * f
        d -= s
    return tuple(rail[-1])
