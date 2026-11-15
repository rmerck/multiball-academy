# Ball visibility over the whole playfield, on the shipped background and sprites, at real device scales.
# Worst case: every lamp lit, every thruster flashing, doors up and marked, both targets lit.
#   python3 visibility_full.py [out_dir]    writes visibility_full.md (and worst-case crops in visibility_full.png)
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

# --table-a [--fl100]: Table A (concept A), drawn as the game draws it from the masters (tools/art/table_a/
# composite.py render(), worst case: every insert lit, jets and domes on, the deck open, at FL500 unless --fl100),
# on its own layout (tools/art/table_a/layout_a.json unless PINBALL_LAYOUT says otherwise). A ball under a ramp is
# measured under the ramp's plastic, as the game draws it; a ball riding a ramp over it. Needs the art venv (skia).
TABLE_A = "--table-a" in sys.argv[1:]
if TABLE_A:
    os.environ.setdefault("PINBALL_LAYOUT", os.path.join(os.path.dirname(os.path.abspath(__file__)), "table_a",
                                                         "layout_a.json"))

import palette as P  # noqa: E402
import render_sim as R  # noqa: E402
from common import ROOT, lum, save_png  # noqa: E402

# --fx: with the light show, every lamp lit in neon at full level, the general illumination on, the bumpers
# flashing as struck.
# --bare (with --fx): the ball measured against the table as lit, without the dark pool round it.
ARGS = [a for a in sys.argv[1:] if a not in ("--fx", "--bare", "--table-a", "--fl100")]
R.FX = "--fx" in sys.argv[1:]
WITH_POOL = R.FX and "--bare" not in sys.argv[1:]
OUT = ARGS[0] if ARGS else os.path.join(ROOT, "build", "art-review")
L = R.L
K = 4
SCALES = [("iPhone SE class, 375 pt @2x", 0.488281 * 2), ("iPhone 17e, 393 pt @3x", 0.511719 * 3),
          ("iPhone 18 Pro, 402 pt @3x", 0.523438 * 3), ("iPad 11 in portrait, 834 pt @2x", 0.876524 * 2),
          ("iPad 13 in portrait, 1024 pt @2x", 1.007622 * 2)]
HALF = 28


def seg_dist(px, py, a, b):
    dx, dy = b[0] - a[0], b[1] - a[1]
    t = max(0.0, min(1.0, ((px - a[0]) * dx + (py - a[1]) * dy) / max(1e-9, dx * dx + dy * dy)))
    return math.hypot(px - a[0] - t * dx, py - a[1] - t * dy)


def inside(poly, x, y):
    n = False
    for i in range(len(poly)):
        x1, y1 = poly[i]; x2, y2 = poly[i - 1]
        if (y1 > y) != (y2 > y) and x < (x2 - x1) * (y - y1) / (y2 - y1) + x1:
            n = not n
    return n


def poly_dist(poly, x, y):
    return min(seg_dist(x, y, poly[i], poly[i - 1]) for i in range(len(poly)))


def reachable(x, y):
    """Can a ball's centre be here? Inside the shell, outside every solid, a radius clear of each."""
    if not inside(L["shell"], x, y) or inside(L["laneIsland"], x, y):
        return False
    if poly_dist(L["shell"], x, y) < 12.5 or poly_dist(L["laneIsland"], x, y) < 12.5:
        return False
    arch = L["innerArch"][1:-1]
    if min(seg_dist(x, y, arch[i], arch[i + 1]) for i in range(len(arch) - 1)) < 12.5:
        return False
    gate = L["laneGate"][1:-1]
    if min(seg_dist(x, y, gate[i], gate[i + 1]) for i in range(len(gate) - 1)) < 12.5:
        return False
    if math.hypot(x - R.DRONE_AT[0], y - R.DRONE_AT[1]) < L["drone"]["radius"] + 12.0:   # drawn there; it patrols
        return False
    cb = L["captiveBall"]   # the captive ball at rest (its channel walls are among the walls)
    if math.hypot(x - cb["home"][0], y - cb["home"][1]) < cb["r"] + 12.3:
        return False
    for w in L["walls"] + L["deadPosts"] + [t["shape"] for t in L["targets"]] + [u["shape"] for u in L["standups"]]:
        if seg_dist(x, y, w["a"], w["b"]) < w["r"] + 12.3:
            return False
    for b in L["bumpers"]:
        if math.hypot(x - b["c"][0], y - b["c"][1]) < b["r"] + 12.3:
            return False
    for tri in L["slingshots"]:
        if inside(tri, x, y) or poly_dist(tri, x, y) < 12.3:
            return False
    return True


def ball_layer(scale):
    """Shadow and ball as the renderer draws them, as one straight-alpha layer of 2*HALF units at K px per unit."""
    n = HALF * 2 * K
    sim = R.Sim.__new__(R.Sim); sim.k = float(K); sim.im = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    # Sim pastes with the sprite as its own mask; onto a clear canvas that needs alpha compositing instead.
    canvas = Image.new("RGBA", (n, n), (0, 0, 0, 0))

    class Layer(R.Sim):
        pass
    lay = Layer.__new__(Layer); lay.k = float(K)

    class Catch:
        def __init__(self):
            self.im = canvas
            self.width, self.height = n, n

        def paste(self, s, xy, mask=None):
            tmp = Image.new("RGBA", (n, n), (0, 0, 0, 0)); tmp.paste(s, xy)
            self.im = Image.alpha_composite(self.im, tmp)
    catch = Catch(); lay.im = catch
    lay.ball(HALF, HALF, scale)
    return np.asarray(catch.im).astype(np.float32)


def ball_only(scale, alpha):
    """The ball sprite alone at an alpha, as one straight-alpha layer like ball_layer's."""
    n = HALF * 2 * K
    sp = R.load("sprites/ball.png")
    size = max(1, int(round(P.BALL_SPRITE_UNITS * scale * K)))
    s = np.asarray(sp.resize((size, size), Image.LANCZOS)).astype(np.float32)
    out = np.zeros((n, n, 4), np.float32)
    o = (n - size) // 2
    out[o:o + size, o:o + size] = s
    out[..., 3] *= alpha
    return out


def nearest(x, y):
    every = [(n, v[0]) for n, v in R.LAMPS.items()] + [(n, v[2]) for n, v in R.PLANNED.items()]
    best = min(every, key=lambda kv: math.hypot(kv[1][0] - x, kv[1][1] - y))
    return "%s %.0f units away" % (best[0], math.hypot(best[1][0] - x, best[1][1] - y))


def over(top, base):
    """Straight-alpha RGBA `top` over RGB `base` (float arrays of one size)."""
    a = top[..., 3:4] / 255.0
    return top[..., :3] * a + base * (1 - a)


def measure(empty, layer, pos, scale, bs=1.0, overlay=None, through=None):
    x0 = int(round((pos[0] - HALF) * K)); y0 = int(round((pos[1] - HALF) * K))
    n = HALF * 2 * K
    Hh, Ww = empty.shape[:2]
    patch = np.zeros((n, n, 3), np.float32); valid = np.zeros((n, n), bool)
    xs0, ys0, xs1, ys1 = max(0, x0), max(0, y0), min(Ww, x0 + n), min(Hh, y0 + n)
    patch[ys0 - y0:ys1 - y0, xs0 - x0:xs1 - x0] = empty[ys0:ys1, xs0:xs1]
    valid[ys0 - y0:ys1 - y0, xs0 - x0:xs1 - x0] = True
    a = layer[..., 3:4] / 255.0
    comp = layer[..., :3] * a + patch * (1 - a)
    if overlay is not None:
        # Table A: the ramps' plastic over the ball and the table round it.
        ov = np.zeros((n, n, 4), np.float32)
        ov[ys0 - y0:ys1 - y0, xs0 - x0:xs1 - x0] = overlay[ys0:ys1, xs0:xs1]
        comp, patch = over(ov, comp), over(ov, patch)
        if through is not None:
            # The game draws the ball again over the ramps at P.TABLE_A_BALL_THROUGH (TableRenderer).
            comp = over(through, comp)
    m = max(8, int(round(HALF * 2 * scale)))

    def small(arr):
        return np.asarray(Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8)).resize((m, m), Image.BOX))
    # Around the ball: the bare table, or with --fx what the game draws there, the dark pool round the ball
    # (pool.png) included, since that is what the eye sees beside it.
    lb, le = lum(small(comp)), lum(small(comp if WITH_POOL else patch))
    v = np.asarray(Image.fromarray((valid * 255).astype(np.uint8)).resize((m, m), Image.BOX)) > 250
    yy, xx = np.mgrid[0:m, 0:m]
    dx, dy = (xx + 0.5) / scale - HALF, (yy + 0.5) / scale - HALF
    r = np.hypot(dx, dy) / bs; ang = np.arctan2(dy, dx)   # radii in ball radii of 12, whatever the ball's scale
    disc = (r < 10.5) & v; around = (r > 13.0) & (r < 24.0) & v
    if disc.sum() < 4 or around.sum() < 8:
        return None
    l_ball, l_around = float(lb[disc].mean()), float(le[around].mean())
    cr = (max(l_ball, l_around) + 0.05) / (min(l_ball, l_around) + 0.05)
    worst = 99.0
    for s in range(12):
        a0 = -math.pi + s * math.pi / 6
        sec = (ang >= a0) & (ang < a0 + math.pi / 6) & v
        inner = sec & (r > 7.0) & (r < 11.0); edge = sec & (r > 12.0) & (r < 14.4)
        if inner.sum() < 1 or edge.sum() < 1:
            continue
        p, q = float(lb[inner].mean()), float(lb[edge].mean())
        worst = min(worst, (max(p, q) + 0.05) / (min(p, q) + 0.05))
    near = (r > 13.0) & (r < 40.0) & v
    return l_ball, l_around, cr, worst, float((le[near] > l_ball).mean())


def main():
    os.makedirs(OUT, exist_ok=True)
    overlay = None
    if TABLE_A:
        sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "table_a"))
        import composite as A
        level = "fl100" if "--fl100" in sys.argv[1:] else "fl500"
        empty = np.asarray(A.render(level, K, worst=True, balls=False, ramps=False).convert("RGB")).astype(np.float32)
        overlay = np.asarray(A.ramp_layer(level, K)).astype(np.float32)
    else:
        sim = R.Sim(K)
        sim.furniture(lit="all", flash=(0, 1, 2), doors_up=True, marked=(0, 1, 2))
        empty = np.asarray(sim.im).astype(np.float32)
    layers = {1.0: ball_layer(1.0), 1.15: ball_layer(1.15)}
    throughs = {1.0: ball_only(1.0, P.TABLE_A_BALL_THROUGH), 1.15: ball_only(1.15, P.TABLE_A_BALL_THROUGH)}
    pts = [(x, y, 1.0, "field") for y in range(70, 950, 9) for x in range(46, 724, 9) if reachable(x, y)]
    pts += [(b[0], b[1], 1.0, "lock bay") for b in L["lockBays"]]
    pts += [(L[r][-1][0], L[r][-1][1], 1.0, "rail landing") for r in ("runwayRail", "skywayRail")]
    for r in ("runwayRail", "skywayRail"):
        pts += [R.rail_point(L[r], t) + (1.15, "on rail") for t in np.linspace(0.0, 1.0, 60)]
    md = ["# Ball visibility: the whole playfield", "",
          "Shipped background and sprites, drawn as the game draws them (tools/art/render_sim.py). Every lamp lit (revision 3's",
          "new inserts, stand-ups and flasher domes too), all three thrusters flashing, doors up and marked, the drone mid-patrol. %d ball positions: a 9-unit grid of every place a ball's centre can be" % len(pts),
          "(%d), both lock bays, both rail landings, and 60 points along each ramp rail at the raised scale 1.15." % sum(1 for p in pts if p[3] == "field"),
          "Luminance is relative (0 to 1); contrast is (lighter + 0.05) / (darker + 0.05). Columns as in visibility.md.", ""]
    allres = {}
    for sname, scale in SCALES:
        res = []
        for (x, y, sc, kind) in pts:
            under = kind != "on rail" and overlay is not None
            mres = measure(empty, layers[sc], (x, y), scale, sc, overlay if under else None,
                           throughs[sc] if under else None)
            if mres:
                res.append(mres + (x, y, sc))
        a = np.array(res); allres[sname] = a
        md += ["## %s: %.2f px per unit" % (sname, scale), "",
               "| ball | deck median / max | mean contrast min / 1% / 5% / median | weakest edge min / 1% / 5% / median | edge under 3:1 | mean under 3:1 | mean under 2.5:1 |",
               "|---|---|---|---|---|---|---|",
               "| %.2f | %.3f / %.3f | %.1f / %.1f / %.1f / %.1f | %.1f / %.1f / %.1f / %.1f | %d of %d | %d | %d |" % (
                   np.median(a[:, 0]), np.median(a[:, 1]), a[:, 1].max(), a[:, 2].min(), np.percentile(a[:, 2], 1), np.percentile(a[:, 2], 5), np.median(a[:, 2]),
                   a[:, 3].min(), np.percentile(a[:, 3], 1), np.percentile(a[:, 3], 5), np.median(a[:, 3]),
                   int((a[:, 3] < 3.0).sum()), len(a), int((a[:, 2] < 3.0).sum()), int((a[:, 2] < 2.5).sum())), ""]
    ref = allres[SCALES[1][0]]
    with open(os.path.join(OUT, "visibility_full.csv"), "w") as f:
        f.write("x,y,scale,deck,mean,edge\n")
        for r in ref:
            f.write("%.0f,%.0f,%.2f,%.3f,%.2f,%.2f\n" % (r[5], r[6], r[7], r[1], r[2], r[3]))
    order = np.argsort(ref[:, 2])[:14]
    md += ["## Worst places at iPhone 17e scale, by mean contrast", "", "| x, y | scale | deck | mean contrast | weakest edge | nearest lamp |", "|---|---|---|---|---|---|"]
    for i in order:
        r = ref[i]
        md.append("| %.0f, %.0f | %.2f | %.3f | %.1f | %.1f | %s |" % (r[5], r[6], r[7], r[1], r[2], r[3], nearest(r[5], r[6])))
    order2 = np.argsort(ref[:, 3])[:8]
    md += ["", "## Worst places at iPhone 17e scale, by weakest edge", "", "| x, y | scale | deck | mean contrast | weakest edge | nearest lamp |", "|---|---|---|---|---|---|"]
    for i in order2:
        r = ref[i]
        md.append("| %.0f, %.0f | %.2f | %.3f | %.1f | %.1f | %s |" % (r[5], r[6], r[7], r[1], r[2], r[3], nearest(r[5], r[6])))
    open(os.path.join(OUT, "visibility_full.md"), "w").write("\n".join(md) + "\n")
    # A picture of the worst twelve, each a 56-unit crop at iPhone 17e scale enlarged 4 times.
    scale = SCALES[1][1]
    tiles = []
    for i in order[:12]:
        r = ref[i]
        x0 = int(round((r[5] - HALF) * K)); y0 = int(round((r[6] - HALF) * K)); n = HALF * 2 * K
        patch = np.zeros((n, n, 3), np.float32)
        xs0, ys0, xs1, ys1 = max(0, x0), max(0, y0), min(empty.shape[1], x0 + n), min(empty.shape[0], y0 + n)
        patch[ys0 - y0:ys1 - y0, xs0 - x0:xs1 - x0] = empty[ys0:ys1, xs0:xs1]
        lay = layers[1.15 if r[7] > 1.0 else 1.0]
        al = lay[..., 3:4] / 255.0
        comp = Image.fromarray(np.clip(lay[..., :3] * al + patch * (1 - al), 0, 255).astype(np.uint8))
        m = int(round(HALF * 2 * scale))
        tiles.append((comp.resize((m, m), Image.BOX).resize((m * 4, m * 4), Image.NEAREST), "%.0f, %.0f  mean %.1f  edge %.1f" % (r[5], r[6], r[2], r[3])))
    tw = tiles[0][0].width
    sheet = Image.new("RGB", (6 * (tw + 12) + 12, 2 * (tw + 34) + 50), "#17161d")
    d = ImageDraw.Draw(sheet)
    try:
        f = ImageFont.truetype("/usr/share/fonts/opentype/inter/Inter-Regular.otf", 14)
    except OSError:
        f = ImageFont.load_default()
    d.text((12, 10), "The twelve lowest-contrast ball positions on the whole table, everything lit, at iPhone 17e pixel size (enlarged 4x)", font=f, fill="#f2efe6")
    for i, (t, cap) in enumerate(tiles):
        x = 12 + (i % 6) * (tw + 12); y = 40 + (i // 6) * (tw + 34)
        sheet.paste(t, (x, y)); d.text((x, y + tw + 4), cap, font=f, fill="#b9b5c6")
    save_png(sheet, os.path.join(OUT, "visibility_full.png"))
    print("\n".join(md))


main()
