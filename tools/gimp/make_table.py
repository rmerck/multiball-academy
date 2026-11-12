# Generates assets/table/background.png from the table geometry: a neon,
# 1980s arcade playfield (striped sun, perspective grid, neon tube rails).
#
# 1. Build the tools and dump the geometry:
#      build/pinball_layout > build/layout.json
# 2. In GIMP 3's Python console:
#      REPO = "/path/to/repo"; LAYOUT = REPO + "/build/layout.json"
#      exec(open(REPO + "/tools/gimp/make_table.py").read())
#
# Everything is drawn from the same numbers the physics uses, at 2x.
import json
import math
import random

exec(open(REPO + "/tools/gimp/common.py").read())

S = 2  # Art pixels per playfield pixel.

ADD = Gimp.ChannelOps.ADD
SUBTRACT = Gimp.ChannelOps.SUBTRACT
INTERSECT = Gimp.ChannelOps.INTERSECT
REPLACE = Gimp.ChannelOps.REPLACE

PINK = (1.0, 0.16, 0.60)
MAGENTA = (0.95, 0.10, 0.90)
CYAN = (0.10, 0.92, 1.0)
PURPLE = (0.62, 0.25, 1.0)
YELLOW = (1.0, 0.92, 0.22)
ORANGE = (1.0, 0.55, 0.15)
MINT = (0.30, 1.0, 0.70)
WHITE = (1.0, 1.0, 1.0)

# These match the tints in src/platform/TableRenderer.cpp.
HOLE_COLORS = {
    "wormhole": [YELLOW, PINK, CYAN],
    "blackhole": [PURPLE],
    "hyperspace": [CYAN],
    "ramp": [ORANGE],
}
BUMPER_COLORS = {"attack": PINK, "engine": CYAN, "satellite": YELLOW}
BANK_COLORS = {"booster": ORANGE, "medal": CYAN, "mission": MAGENTA, "multiplier": MINT}
ROLLOVER_COLORS = {"reentry": MINT, "inlane": CYAN, "outlane": PINK}
HORIZON = 640.0


def feather(on, radius=0.0):
    Gimp.context_set_feather(on)
    if on:
        Gimp.context_set_feather_radius(radius, radius)


def mix(a, b, t):
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))


def col(rgb, a=1.0, k=1.0):
    return color(min(1.0, rgb[0] * k), min(1.0, rgb[1] * k), min(1.0, rgb[2] * k), a)


def make_table(repo, layout_path):
    with open(layout_path) as f:
        d = json.load(f)
    W, H = int(d["width"] * S), int(d["height"] * S)
    cx = d["fieldCenterX"]
    shell = [tuple(p) for p in d["shell"]]
    lane = d["lane"]
    rng = random.Random(19840612)

    img = Gimp.Image.new(W, H, Gimp.ImageBaseType.RGB)
    Gimp.context_set_antialias(True)
    feather(False)

    def clip_to_shell():
        select_polygon(img, shell, S, INTERSECT)

    def clip_field():
        """The shell, without the launch lane."""
        clip_to_shell()
        img.select_rectangle(SUBTRACT, (lane["wallX"] - 5) * S, 285 * S, 80 * S, H)

    def rect(x, y, w, h, op=REPLACE):
        img.select_rectangle(op, x * S, y * S, w * S, h * S)

    def line(layer, a, b, width, c):
        select_capsule(img, a, b, width / 2.0, S)
        fill_selection(layer, c)

    def polyline(layer, pts, width, c, closed=False):
        if closed:
            pts = list(pts) + [pts[0]]
        select_polyline(img, pts, width / 2.0, S)
        fill_selection(layer, c)

    def ring(layer, center, radius, width, c):
        select_ring(img, center, radius + width / 2.0, radius - width / 2.0, S)
        fill_selection(layer, c)

    def zigzag(x, y, step, amp, count, vertical=False):
        pts = []
        for i in range(count + 1):
            o = amp if i % 2 else -amp
            pts.append((x + o, y + i * step) if vertical else (x + i * step, y + o))
        return pts

    def squiggle(x, y, length, amp, waves, angle=0.0):
        pts = []
        ca, sa = math.cos(angle), math.sin(angle)
        n = waves * 10
        for i in range(n + 1):
            u = length * i / n
            v = amp * math.sin(2 * math.pi * waves * i / n)
            pts.append((x + u * ca - v * sa, y + u * sa + v * ca))
        return pts

    def tri(c, r, rot):
        return [(c[0] + r * math.cos(rot + k * 2.0944), c[1] + r * math.sin(rot + k * 2.0944))
                for k in range(3)]

    def ellipse_pts(c, rx, ry, rot, n=56, a0=0.0, a1=2 * math.pi):
        pts = []
        cr, sr = math.cos(rot), math.sin(rot)
        for i in range(n + 1):
            a = a0 + (a1 - a0) * i / n
            x, y = rx * math.cos(a), ry * math.sin(a)
            pts.append((c[0] + x * cr - y * sr, c[1] + x * sr + y * cr))
        return pts

    # --- Cabinet: everything outside the playfield shell -------------------
    cabinet = new_layer(img, "cabinet")
    img.select_rectangle(REPLACE, 0, 0, W, H)
    linear_gradient(cabinet, color(0.15, 0.04, 0.30), color(0.05, 0.02, 0.13), 0, 0, W, H)
    select_none(img)
    apply_filter(cabinet, "gegl:noise-hurl", pct_random=1.2, repeat=1, seed=7)
    blur(cabinet, 1.6)
    # Diagonal pinstripes.
    stripes = new_layer(img, "cabinet-stripes", opacity=16)
    op = REPLACE
    for k in range(-60, 110, 2):
        x0 = k * 14.0
        select_polygon(img, [(x0, 0), (x0 + 3, 0), (x0 + 3 - 300, 300), (x0 - 300, 300)], S, op)
        op = ADD
    fill_selection(stripes, col(PURPLE))
    select_none(img)

    # Memphis confetti in the corners above the arch.
    memphis = new_layer(img, "memphis")
    shapes = [
        ("tri", (60, 62), 26, 0.4, PINK), ("tri", (150, 36), 15, 1.2, CYAN),
        ("zig", (28, 132), YELLOW), ("ring", (112, 96), 13, MINT),
        ("sq", (20, 30), 60, 0.6, CYAN), ("tri", (36, 210), 13, 2.2, YELLOW),
        ("ring", (212, 26), 8, PINK), ("dots", (84, 150), PURPLE),
        ("tri", (706, 58), 26, 2.6, CYAN), ("tri", (612, 34), 15, 0.2, YELLOW),
        ("zig", (690, 128), PINK), ("ring", (650, 92), 13, ORANGE),
        ("sq", (690, 22), 60, 2.4, PINK), ("tri", (738, 208), 12, 1.0, MINT),
        ("ring", (552, 24), 8, CYAN), ("dots", (640, 150), YELLOW),
    ]
    for s in shapes:
        kind = s[0]
        if kind == "tri":
            pts = tri(s[1], s[2], s[3])
            select_polygon(img, pts, S)
            fill_selection(memphis, col(s[4], 0.22))
            polyline(memphis, pts, 2.4, col(s[4]), closed=True)
        elif kind == "zig":
            polyline(memphis, zigzag(s[1][0], s[1][1], 9, 5, 6), 2.6, col(s[2]))
        elif kind == "ring":
            ring(memphis, s[1], s[2], 2.6, col(s[3]))
        elif kind == "sq":
            polyline(memphis, squiggle(s[1][0], s[1][1], s[2], 5, 3, s[3]), 2.6, col(s[4]))
        elif kind == "dots":
            op = REPLACE
            for i in range(4):
                for j in range(3):
                    select_circle(img, (s[1][0] + i * 9, s[1][1] + j * 9), 1.8, S, op)
                    op = ADD
            fill_selection(memphis, col(s[2]))
    # Side strips: a column of small chevrons down each cabinet edge.
    op = REPLACE
    for y in range(330, 950, 34):
        for x, sgn in ((16, 1), (752, -1)):
            select_polyline(img, [(x - 6, y - 6 * sgn), (x, y), (x + 6, y - 6 * sgn)], 1.2, S, op)
            op = ADD
    fill_selection(memphis, col(PURPLE, 0.9))
    select_none(img)
    bloom(img, memphis, [(7.0, 70)])

    # --- Sky inside the shell ----------------------------------------------
    space = new_layer(img, "space")
    select_polygon(img, shell, S)
    rect(0, 0, 768, 400, INTERSECT)
    linear_gradient(space, color(0.02, 0.01, 0.09), color(0.09, 0.02, 0.24), 0, 40 * S, 0,
                    400 * S)
    select_polygon(img, shell, S)
    rect(0, 400, 768, HORIZON - 400, INTERSECT)
    linear_gradient(space, color(0.09, 0.02, 0.24), color(0.50, 0.06, 0.48), 0, 400 * S, 0,
                    HORIZON * S)
    select_polygon(img, shell, S)
    rect(0, HORIZON, 768, 400, INTERSECT)
    linear_gradient(space, color(0.11, 0.02, 0.25), color(0.03, 0.0, 0.10), 0, HORIZON * S, 0,
                    960 * S)

    nebula = new_layer(img, "nebula", opacity=44, mode=Gimp.LayerMode.SCREEN)
    clouds = [
        ((170, 200), 170, 110, (0.70, 0.10, 0.62)),
        ((540, 310), 180, 140, (0.08, 0.50, 0.70)),
        ((330, 300), 150, 130, (0.45, 0.12, 0.85)),
        ((600, 190), 110, 90, (0.75, 0.15, 0.45)),
        ((120, 420), 120, 100, (0.10, 0.42, 0.60)),
        ((400, 110), 160, 60, (0.30, 0.12, 0.70)),
    ]
    for (x, y), rx, ry, rgb in clouds:
        feather(True, 150.0)
        img.select_ellipse(REPLACE, (x - rx) * S, (y - ry) * S, 2 * rx * S, 2 * ry * S)
        feather(False)
        clip_to_shell()
        fill_selection(nebula, color(*rgb))
    select_none(img)

    stars = new_layer(img, "stars")
    tints = [(1.0, 1.0, 1.0), (0.75, 0.95, 1.0), (1.0, 0.75, 0.95), (1.0, 0.95, 0.7)]
    for brightness, count, size in ((0.45, 460, 1.0), (0.8, 170, 1.6), (1.0, 46, 2.4)):
        for tint in tints:
            select_none(img)
            first = True
            for _ in range(count // len(tints)):
                x = rng.uniform(40, 730)
                y = rng.uniform(40, HORIZON - 30)
                r = size * rng.uniform(0.6, 1.2)
                img.select_ellipse(REPLACE if first else ADD, x * S - r, y * S - r, 2 * r, 2 * r)
                first = False
            clip_to_shell()
            fill_selection(stars, color(tint[0], tint[1], tint[2], brightness))
    # A few four-point sparkles.
    op = REPLACE
    for _ in range(16):
        x, y = rng.uniform(60, 660), rng.uniform(60, 440)
        r = rng.uniform(4.0, 8.0)
        select_polygon(img, [(x - r, y), (x, y - 0.7), (x + r, y), (x, y + 0.7)], S, op)
        select_polygon(img, [(x, y - r), (x + 0.7, y), (x, y + r), (x - 0.7, y)], S, ADD)
        op = ADD
    clip_to_shell()
    fill_selection(stars, color(1.0, 1.0, 1.0, 0.95))
    # A comet above the top lanes.
    select_polygon(img, [(532.0, 84.0), (533.0, 86.4), (474.0, 104.0)], S, REPLACE)
    select_circle(img, (532.5, 85.2), 2.0, S, ADD)
    clip_to_shell()
    fill_selection(stars, color(0.85, 0.97, 1.0, 0.9))
    select_none(img)
    bloom(img, stars, [(3.0, 100)])

    # A small banded planet with a ring, high on the right.
    planet = new_layer(img, "planet")
    pc, pr = (604.0, 152.0), 15.0
    polyline(planet, ellipse_pts(pc, 27, 6, -0.35, 28, math.pi, 2 * math.pi), 1.4, col(CYAN, 0.8))
    select_circle(img, pc, pr, S)
    linear_gradient(planet, color(1.0, 0.45, 0.75), color(0.36, 0.10, 0.62), 0, (pc[1] - pr) * S,
                    0, (pc[1] + pr) * S)
    for off, hh in ((-7, 2.2), (-1, 3.0), (6, 2.2)):
        rect(pc[0] - pr, pc[1] + off, 2 * pr, hh)
        select_circle(img, pc, pr, S, INTERSECT)
        fill_selection(planet, color(0.25, 0.05, 0.45, 0.45))
    feather(True, 10.0)
    select_circle(img, (pc[0] + 7, pc[1] + 7), pr, S)
    feather(False)
    select_circle(img, pc, pr, S, INTERSECT)
    fill_selection(planet, color(0.03, 0.0, 0.10, 0.6))
    polyline(planet, ellipse_pts(pc, 27, 6, -0.35, 28, 0, math.pi), 1.4, col(CYAN, 0.9))
    select_none(img)
    bloom(img, planet, [(5.0, 40)])

    # --- The sun, sinking behind the horizon ------------------------------
    sun_c = (cx, 560.0)
    sun_r = 104.0
    halo = new_layer(img, "sun-halo", opacity=60, mode=Gimp.LayerMode.SCREEN)
    feather(True, 90.0)
    select_circle(img, sun_c, sun_r + 22, S)
    feather(False)
    rect(0, 0, 768, HORIZON, INTERSECT)
    clip_to_shell()
    fill_selection(halo, color(1.0, 0.15, 0.55))
    sun = new_layer(img, "sun")
    select_circle(img, sun_c, sun_r, S)
    rect(0, 0, 768, HORIZON, INTERSECT)
    # Slats: gaps that widen towards the horizon.
    y = sun_c[1] - 18.0
    gap = 2.0
    while y < HORIZON:
        rect(0, y, 768, gap, SUBTRACT)
        y += gap + 11.0
        gap += 1.5
    linear_gradient(sun, color(1.0, 0.80, 0.08), color(0.95, 0.03, 0.45), 0,
                    (sun_c[1] - sun_r) * S, 0, HORIZON * S)
    select_none(img)
    bloom(img, sun, [(12.0, 18)])

    # --- Mountains, skyline and palms along the horizon --------------------
    ridge = new_layer(img, "mountains")
    pts = [(30.0, HORIZON)]
    x = 30.0
    top = []
    while x < 690:
        centre = abs(x - cx) / 330.0
        hmax = 16 + 46 * min(1.0, centre * 1.3)
        top.append((x, HORIZON - rng.uniform(0.25, 1.0) * hmax))
        x += rng.uniform(13, 26)
    pts += top + [(690.0, HORIZON)]
    select_polygon(img, pts, S)
    clip_to_shell()
    linear_gradient(ridge, color(0.30, 0.07, 0.48), color(0.10, 0.02, 0.22), 0, 570 * S, 0,
                    HORIZON * S)
    # Facets: a darker flank on the right of each peak.
    op = REPLACE
    for i in range(1, len(top) - 1):
        if top[i][1] < top[i - 1][1] and top[i][1] < top[i + 1][1]:
            select_polygon(img, [top[i], top[i + 1], (top[i][0] + 3, HORIZON)], S, op)
            op = ADD
    clip_to_shell()
    fill_selection(ridge, color(0.04, 0.0, 0.12, 0.55))
    select_polyline(img, top, 0.6, S)
    clip_to_shell()
    fill_selection(ridge, col(CYAN, 0.75))

    city = new_layer(img, "skyline")
    lights = []
    x = 44.0
    op = REPLACE
    while x < 668:
        w = rng.uniform(9, 20)
        centre = abs(x - cx) / 330.0
        h = rng.uniform(6, 14) + 22 * min(1.0, centre * 1.6) * rng.uniform(0.3, 1.0)
        rect(x, HORIZON - h, w, h + 1, op)
        op = ADD
        if rng.random() < 0.25:
            rect(x + w / 2 - 0.4, HORIZON - h - 7, 0.8, 7, ADD)
        yy = HORIZON - h + 3
        while yy < HORIZON - 3:
            xx = x + 2.5
            while xx < x + w - 2:
                if rng.random() < 0.4:
                    lights.append((xx, yy, rng.choice([YELLOW, CYAN, PINK, YELLOW])))
                xx += 3.4
            yy += 4.2
        x += w + rng.uniform(0, 3)
    clip_to_shell()
    fill_selection(city, color(0.035, 0.005, 0.09))
    for tint in (YELLOW, CYAN, PINK):
        op = REPLACE
        for lx, ly, lc in lights:
            if lc == tint:
                rect(lx, ly, 1.4, 1.6, op)
                op = ADD
        clip_to_shell()
        fill_selection(city, col(tint, 0.95))

    palms = new_layer(img, "palms")

    def bez(p0, p1, p2, t):
        u = 1 - t
        return (u * u * p0[0] + 2 * u * t * p1[0] + t * t * p2[0],
                u * u * p0[1] + 2 * u * t * p1[1] + t * t * p2[1])

    def palm(base, height, lean):
        trunk = [(base[0] + lean * (i / 8.0) ** 2, base[1] - height * i / 8.0) for i in range(9)]
        op = REPLACE
        for i in range(8):
            select_capsule(img, trunk[i], trunk[i + 1], 2.4 - 0.15 * i, S, op)
            op = ADD
        t0 = trunk[-1]
        for ang, length in ((185, 30), (160, 25), (215, 24), (-5, 30), (20, 25), (-35, 24),
                            (250, 15), (290, 15), (130, 18), (50, 18)):
            a = math.radians(ang)
            tip = (t0[0] + length * math.cos(a), t0[1] - length * math.sin(a) * 0.5 + length * 0.5)
            c1 = (t0[0] + length * 0.55 * math.cos(a), t0[1] - length * 0.42)
            c2 = (c1[0], c1[1] + length * 0.34)
            upper = [bez(t0, c1, tip, i / 8.0) for i in range(9)]
            lower = [bez(t0, c2, tip, i / 8.0) for i in range(8, -1, -1)]
            select_polygon(img, upper + lower, S, ADD)
        select_circle(img, t0, 3.2, S, ADD)
        fill_selection(palms, color(0.03, 0.0, 0.08))

    palm((cx - 64, HORIZON + 1), 74, -9)
    palm((cx + 70, HORIZON + 1), 62, 8)
    palm((cx + 50, HORIZON + 1), 40, -5)
    select_none(img)

    # --- The grid floor -----------------------------------------------------
    grid = new_layer(img, "grid", opacity=62)
    op = REPLACE
    for i in range(-16, 17):
        bx = cx + i * 62.0
        select_polygon(img, [(cx + i * 1.2 - 0.25, HORIZON), (cx + i * 1.2 + 0.25, HORIZON),
                             (bx + 1.1, 965), (bx - 1.1, 965)], S, op)
        op = ADD
    for k in range(1, 17):
        y = HORIZON + 325.0 * (k / 16.0) ** 2.1
        rect(0, y - 0.3 - 0.05 * k, 768, 0.6 + 0.1 * k, ADD)
    clip_field()
    linear_gradient(grid, color(1.0, 0.25, 0.85), color(0.25, 0.75, 1.0), 0, HORIZON * S, 0,
                    960 * S)
    select_none(img)
    bloom(img, grid, [(6.0, 55)])
    # The sun's reflection and a bright seam along the horizon.
    shine = new_layer(img, "horizon", mode=Gimp.LayerMode.SCREEN)
    feather(True, 70.0)
    img.select_ellipse(REPLACE, (cx - 150) * S, (HORIZON - 14) * S, 300 * S, 90 * S)
    feather(False)
    rect(0, HORIZON, 768, 400, INTERSECT)
    clip_field()
    fill_selection(shine, color(1.0, 0.15, 0.55, 0.32))
    rect(0, HORIZON - 0.8, 768, 1.6)
    clip_field()
    fill_selection(shine, color(0.85, 0.95, 1.0, 0.95))
    # The sun's slats, mirrored in the floor and shrinking with distance.
    for k in range(7):
        half = 92.0 - k * 11.0
        rect(cx - half, HORIZON + 7 + k * 10.5, 2 * half, 2.4 + 0.3 * k)
        fill_selection(shine, color(1.0, 0.45 - 0.05 * k, 0.35 + 0.05 * k, 0.55 - 0.06 * k))
    select_none(img)
    bloom(img, shine, [(8.0, 40)])

    # --- Area graphics: each part of the table gets its own motif ----------
    deco = new_layer(img, "deco")
    bump = {g: [b for b in d["bumpers"] if b["group"] == g] for g in BUMPER_COLORS}

    # Attack bumpers: a hot pink diamond with a starburst.
    att = [tuple(b["c"]) for b in bump["attack"]]
    ac = (sum(p[0] for p in att) / 4.0, sum(p[1] for p in att) / 4.0)
    order = sorted(att, key=lambda p: math.atan2(p[1] - ac[1], p[0] - ac[0]))
    op = REPLACE
    for i in range(24):
        a = i * math.pi / 12
        r0, r1 = 20.0, (118.0 if i % 2 == 0 else 96.0)
        select_polygon(img, [(ac[0] + r0 * math.cos(a), ac[1] + r0 * math.sin(a)),
                             (ac[0] + r1 * math.cos(a - 0.012), ac[1] + r1 * math.sin(a - 0.012)),
                             (ac[0] + r1 * math.cos(a + 0.012), ac[1] + r1 * math.sin(a + 0.012))],
                       S, op)
        op = ADD
    fill_selection(deco, col(PINK, 0.30))
    polyline(deco, order, 1.6, col(PINK, 0.9), closed=True)
    for grow, alpha in ((1.55, 0.55), (1.95, 0.28)):
        polyline(deco, [(ac[0] + (p[0] - ac[0]) * grow, ac[1] + (p[1] - ac[1]) * grow)
                        for p in order], 1.2, col(MAGENTA, alpha), closed=True)
    ring(deco, ac, 14, 1.4, col(YELLOW, 0.8))
    select_polygon(img, tri(ac, 8, -math.pi / 2), S)
    fill_selection(deco, col(YELLOW, 0.8))

    # Engine bumpers: a cyan delta with thrust lines.
    eng = [tuple(b["c"]) for b in bump["engine"]]
    ec = (sum(p[0] for p in eng) / 3.0, sum(p[1] for p in eng) / 3.0)
    polyline(deco, eng, 1.6, col(CYAN, 0.9), closed=True)
    ring(deco, ec, 80, 1.2, col(CYAN, 0.30))
    op = REPLACE
    for i in range(40):
        a = i * math.pi / 20
        select_capsule(img, (ec[0] + 84 * math.cos(a), ec[1] + 84 * math.sin(a)),
                       (ec[0] + (90 if i % 5 else 96) * math.cos(a),
                        ec[1] + (90 if i % 5 else 96) * math.sin(a)), 0.6, S, op)
        op = ADD
    clip_to_shell()
    fill_selection(deco, col(CYAN, 0.55))
    select_polygon(img, tri(ec, 9, math.pi / 2), S)
    fill_selection(deco, col(CYAN, 0.7))

    # Satellite bumper: tilted orbits with moons.
    sat = tuple(bump["satellite"][0]["c"])
    for rx, ry, rot, alpha in ((62, 22, -0.5, 0.85), (58, 26, 0.75, 0.5), (46, 46, 0, 0.22)):
        polyline(deco, ellipse_pts(sat, rx, ry, rot), 1.3, col(YELLOW, alpha))
    for rx, ry, rot, a in ((62, 22, -0.5, 0.6), (62, 22, -0.5, 3.6), (58, 26, 0.75, 2.2)):
        m = ellipse_pts(sat, rx, ry, rot, 1, a, a)[0]
        select_circle(img, m, 3.2, S)
        fill_selection(deco, col(WHITE, 0.95))
        ring(deco, m, 5.5, 1.0, col(YELLOW, 0.8))

    # Re-entry lanes: mint chevrons dropping into each lane, and an arc.
    op = REPLACE
    for r in d["rollovers"]:
        if r["group"] == "reentry":
            x, y = r["c"]
            for k in range(2):
                yy = y + 38 + k * 9
                select_polyline(img, [(x - 8, yy), (x, yy + 7), (x + 8, yy)], 1.0, S, op)
                op = ADD
    fill_selection(deco, col(MINT, 0.8))
    polyline(deco, ellipse_pts((cx, 150), 118, 62, 0, 40, math.pi * 1.12, math.pi * 1.88), 1.2,
             col(MINT, 0.5))

    # Return lanes: arrows guiding down the inlanes and out of the outlanes.
    for r in d["rollovers"]:
        if r["group"] in ("inlane", "outlane"):
            x, y = r["c"]
            rgb = ROLLOVER_COLORS[r["group"]]
            op = REPLACE
            for k in range(3):
                yy = y - 46 + k * 9
                select_polyline(img, [(x - 7, yy), (x, yy + 6), (x + 7, yy)], 0.9, S, op)
                op = ADD
            fill_selection(deco, col(rgb, 0.75))

    # Mission bank (left wall): magenta pointers and a zigzag.
    # Booster bank (right wall): orange pointers and a squiggle.
    for t in d["targets"]:
        a, b = t["shape"]["a"], t["shape"]["b"]
        mid = ((a[0] + b[0]) / 2.0, (a[1] + b[1]) / 2.0)
        rgb = BANK_COLORS[t["bank"]]
        if t["bank"] in ("mission", "booster"):
            sgn = 1 if t["bank"] == "mission" else -1
            select_polygon(img, [(mid[0] + sgn * 12, mid[1]), (mid[0] + sgn * 24, mid[1] - 8),
                                 (mid[0] + sgn * 24, mid[1] + 8)], S)
            fill_selection(deco, col(rgb, 0.85))
            polyline(deco, [(mid[0] + sgn * 30, mid[1] - 8), (mid[0] + sgn * 30, mid[1] + 8)], 1.4,
                     col(rgb, 0.6))
        elif t["bank"] == "multiplier":
            nx, ny = (b[1] - a[1]), -(b[0] - a[0])
            n = math.hypot(nx, ny)
            nx, ny = nx / n, ny / n
            if nx < 0:
                nx, ny = -nx, -ny
            p = (mid[0] + nx * 20, mid[1] + ny * 20)
            polyline(deco, [(p[0] - 5, p[1] - 5), (p[0] + 5, p[1] + 5)], 1.6, col(rgb, 0.9))
            polyline(deco, [(p[0] - 5, p[1] + 5), (p[0] + 5, p[1] - 5)], 1.6, col(rgb, 0.9))
        else:
            select_polygon(img, tri((mid[0], mid[1] + 17), 5, math.pi / 2 + math.pi), S)
            fill_selection(deco, col(rgb, 0.85))
    polyline(deco, zigzag(84, 536, 10, 4, 9, True), 1.5, col(MAGENTA, 0.55))
    polyline(deco, squiggle(630, 532, 96, 4, 4, math.pi / 2), 1.5, col(ORANGE, 0.55))

    # Launch ramp: rails that sweep from the entry up around the arch to the
    # exit. They are drawn faint: the ball travels above the playfield there.
    ramp = [h for h in d["holes"] if h["type"] == "ramp"][0]["c"]
    rex = d["rampExit"]
    arch_c = (384.0, 300.0)
    path = [(ramp[0], ramp[1] - 20), (ramp[0] - 8, 360.0)]
    path += ellipse_pts(arch_c, 320, 236, 0, 30, math.pi * 1.0, math.pi * 1.385)[1:]
    path.append((rex[0] - 18, rex[1] - 14))
    for off, alpha in ((-9, 0.55), (9, 0.55)):
        rail = []
        for i, p in enumerate(path):
            q = path[min(i + 1, len(path) - 1)]
            o = path[max(i - 1, 0)]
            tx, ty = q[0] - o[0], q[1] - o[1]
            n = math.hypot(tx, ty) or 1.0
            rail.append((p[0] - ty / n * off, p[1] + tx / n * off))
        polyline(deco, rail, 1.3, col(ORANGE, alpha))
    op = REPLACE
    for i in range(1, len(path) - 1, 2):
        p, q, o = path[i], path[i + 1], path[i - 1]
        tx, ty = q[0] - o[0], q[1] - o[1]
        n = math.hypot(tx, ty) or 1.0
        select_capsule(img, (p[0] - ty / n * 9, p[1] + tx / n * 9),
                       (p[0] + ty / n * 9, p[1] - tx / n * 9), 0.5, S, op)
        op = ADD
    fill_selection(deco, col(YELLOW, 0.4))
    ring(deco, rex, 15, 1.6, col(ORANGE, 0.8))
    ring(deco, rex, 21, 1.0, col(YELLOW, 0.4))
    op = REPLACE
    for k in range(3):
        yy = ramp[1] + 28 + k * 11
        select_polygon(img, [(ramp[0], yy), (ramp[0] + 12, yy + 10), (ramp[0] + 12, yy + 15),
                             (ramp[0], yy + 5), (ramp[0] - 12, yy + 15), (ramp[0] - 12, yy + 10)],
                       S, op)
        op = ADD
    linear_gradient(deco, col(YELLOW), col(PINK), 0, (ramp[1] + 28) * S, 0, (ramp[1] + 66) * S)

    # Drain: a warning glow between the flippers.
    op = REPLACE
    for k in range(4):
        yy = 915 + k * 11
        select_polyline(img, [(cx - 30 + k * 4, yy), (cx, yy + 9), (cx + 30 - k * 4, yy)], 1.1, S,
                        op)
        op = ADD
    fill_selection(deco, col(PINK, 0.7))
    select_none(img)
    deco.set_opacity(85.0)
    bloom(img, deco, [(5.0, 70), (14.0, 40)])

    # --- Launch lane -------------------------------------------------------
    lane_layer = new_layer(img, "lane")
    rect(lane["wallX"], 150, lane["right"] - lane["wallX"], d["height"] - 150)
    clip_to_shell()
    linear_gradient(lane_layer, color(0.17, 0.04, 0.34), color(0.05, 0.01, 0.15),
                    lane["wallX"] * S, 0, lane["right"] * S, 0)
    first = True
    for y in range(330, 870, 44):
        x = lane["centerX"]
        select_polygon(img, [(x, y), (x + 14, y + 16), (x + 14, y + 23), (x, y + 7),
                             (x - 14, y + 23), (x - 14, y + 16)], S, REPLACE if first else ADD)
        first = False
    linear_gradient(lane_layer, col(YELLOW, 0.85), col(PINK, 0.85), 0, 330 * S, 0, 870 * S)
    for dx in (-19, 19):
        rect(lane["centerX"] + dx - 0.5, 310, 1.0, 580)
        fill_selection(lane_layer, col(PURPLE, 0.7))
    select_none(img)
    bloom(img, lane_layer, [(6.0, 45)])

    # --- Inserts: unlit lights and switch slots ----------------------------
    inserts = new_layer(img, "inserts")

    def insert_circle(center, radius, rgb):
        select_circle(img, center, radius + 2.5, S)
        fill_selection(inserts, col(rgb, 0.95))
        select_circle(img, center, radius + 0.6, S)
        fill_selection(inserts, color(0.02, 0.0, 0.05, 1.0))
        select_circle(img, center, radius - 1.0, S)
        radial_gradient(inserts, col(rgb, 1.0, 0.50), col(rgb, 1.0, 0.20),
                        (center[0] - 2) * S, (center[1] - 2) * S, (center[0] + radius) * S,
                        (center[1] + radius) * S)

    n_fuel = len(d["fuelLights"]["centers"])
    fc = d["fuelLights"]["centers"]
    # A dark spine behind the fuel column keeps it readable against the sun.
    select_capsule(img, (fc[0][0], fc[0][1]), (fc[-1][0], fc[-1][1]), 15, S)
    fill_selection(inserts, color(0.04, 0.0, 0.10, 0.80))
    select_capsule(img, (fc[0][0], fc[0][1]), (fc[-1][0], fc[-1][1]), 15, S)
    select_capsule(img, (fc[0][0], fc[0][1]), (fc[-1][0], fc[-1][1]), 13.6, S, SUBTRACT)
    fill_selection(inserts, col(YELLOW, 0.8))
    for i, center in enumerate(fc):
        insert_circle(center, d["fuelLights"]["radius"], mix(YELLOW, PINK, i / max(1, n_fuel - 1)))

    for r in d["rollovers"]:
        c = r["c"]
        rgb = ROLLOVER_COLORS[r["group"]]
        select_capsule(img, (c[0], c[1] - 9), (c[0], c[1] + 9), 5.5, S)
        fill_selection(inserts, col(rgb, 0.95))
        select_capsule(img, (c[0], c[1] - 9), (c[0], c[1] + 9), 3.8, S)
        fill_selection(inserts, col(rgb, 1.0, 0.24))

    # Bumper bases: a dark disc with a neon rim and tick marks; the cap
    # sprite sits on top.
    for b in d["bumpers"]:
        rgb = BUMPER_COLORS[b["group"]]
        c = b["c"]
        select_circle(img, c, b["r"] + 9, S)
        fill_selection(inserts, color(0.03, 0.0, 0.08, 0.9))
        ring(inserts, c, b["r"] + 7.5, 1.8, col(rgb, 1.0))
        op = REPLACE
        for i in range(16):
            a = i * math.pi / 8
            select_capsule(img, (c[0] + (b["r"] + 2) * math.cos(a), c[1] + (b["r"] + 2) * math.sin(a)),
                           (c[0] + (b["r"] + 5) * math.cos(a), c[1] + (b["r"] + 5) * math.sin(a)),
                           0.6, S, op)
            op = ADD
        fill_selection(inserts, col(rgb, 0.8))
    select_none(img)
    bloom(img, inserts, [(5.0, 55)])

    # --- Holes -------------------------------------------------------------
    wells = new_layer(img, "wells")
    seen = {}
    for h in d["holes"]:
        palette = HOLE_COLORS[h["type"]]
        rgb = palette[seen.get(h["type"], 0) % len(palette)]
        seen[h["type"]] = seen.get(h["type"], 0) + 1
        radius = d["hole"]["radius"]
        c = tuple(h["c"])
        feather(True, 18.0)
        select_circle(img, c, radius + 13, S)
        feather(False)
        clip_to_shell()
        fill_selection(wells, col(rgb, 0.42))
        if h["type"] == "wormhole":
            # Two spiral arms winding into the well.
            for phase in (0.0, math.pi):
                pts = []
                for i in range(40):
                    t = i / 39.0
                    a = phase + t * 3.6 * math.pi + seen[h["type"]]
                    rr = radius + 2 + 17 * (1 - t) ** 1.3
                    pts.append((c[0] + rr * math.cos(a), c[1] + rr * math.sin(a)))
                polyline(wells, pts, 1.3, col(rgb, 0.85))
        elif h["type"] == "blackhole":
            # An accretion disc seen edge on, with photon rings.
            for rr, alpha in ((24, 0.8), (30, 0.45), (37, 0.25)):
                ring(wells, c, rr, 1.3, col(PURPLE, alpha))
            select_polyline(img, ellipse_pts(c, 50, 11, -0.28), 2.2, S)
            fill_selection(wells, col(ORANGE, 0.9))
            polyline(wells, ellipse_pts(c, 44, 8, -0.28), 1.0, col(YELLOW, 0.8))
            polyline(wells, ellipse_pts(c, 57, 14, -0.28), 0.9, col(PINK, 0.6))
        elif h["type"] == "hyperspace":
            # Nested diamonds and warp streaks.
            for rr, alpha in ((22, 0.9), (29, 0.55), (36, 0.3)):
                select_polyline(img, [(c[0], c[1] - rr), (c[0] + rr, c[1]), (c[0], c[1] + rr),
                                      (c[0] - rr, c[1]), (c[0], c[1] - rr)], 0.7, S)
                rect(0, 0, lane["wallX"] - 6, 960, INTERSECT)
                fill_selection(wells, col(CYAN, alpha))
            op = REPLACE
            for i in range(12):
                a = i * math.pi / 6 + 0.26
                select_capsule(img, (c[0] + 40 * math.cos(a), c[1] + 40 * math.sin(a)),
                               (c[0] + 58 * math.cos(a), c[1] + 58 * math.sin(a)), 0.6, S, op)
                op = ADD
            rect(0, 0, lane["wallX"] - 6, 960, INTERSECT)
            fill_selection(wells, col(WHITE, 0.5))
        select_circle(img, c, radius + 3.5, S)
        fill_selection(wells, col(rgb, 1.0))
        select_circle(img, c, radius + 1.6, S)
        fill_selection(wells, col(WHITE, 0.85))
        select_circle(img, c, radius, S)
        radial_gradient(wells, color(0.0, 0.0, 0.0), col(rgb, 1.0, 0.30),
                        c[0] * S, c[1] * S, (c[0] + radius) * S, c[1] * S)
    select_none(img)
    bloom(img, wells, [(5.0, 45), (16.0, 22)])

    # --- Islands: slingshots and the medal bank roof -----------------------
    islands = new_layer(img, "islands")
    island_neon = new_layer(img, "island-neon")
    for idx, shape in enumerate(d["slingshots"] + [d["medalIsland"]]):
        pts = [tuple(p) for p in shape]
        rgb = CYAN if idx == 2 else PINK
        ys = [p[1] for p in pts]
        select_polygon(img, pts, S)
        Gimp.Selection.grow(img, 3 * S)
        fill_selection(islands, color(0.03, 0.0, 0.08, 1.0))
        select_polygon(img, pts, S)
        linear_gradient(islands, color(0.36, 0.06, 0.52), color(0.09, 0.02, 0.24),
                        0, min(ys) * S, 0, max(ys) * S)
        # Pinstripes across the face.
        select_none(img)
        op = REPLACE
        yy = min(ys) + 4
        while yy < max(ys):
            rect(0, yy, 768, 1.2, op)
            op = ADD
            yy += 6
        select_polygon(img, pts, S, INTERSECT)
        fill_selection(islands, col(rgb, 0.22))
        # An inset outline.
        select_polygon(img, pts, S)
        Gimp.Selection.shrink(img, 7 * S)
        if not Gimp.Selection.is_empty(img):
            Gimp.Selection.border(img, 2)
            fill_selection(islands, col(YELLOW, 0.85))
        # The neon tube around the edge.
        select_polygon(img, pts, S)
        Gimp.Selection.grow(img, 3 * S)
        select_polygon(img, pts, S, SUBTRACT)
        fill_selection(island_neon, col(rgb))
        select_polygon(img, pts, S)
        Gimp.Selection.grow(img, 2 * S)
        Gimp.Selection.border(img, 1)
        fill_selection(island_neon, col(mix(rgb, WHITE, 0.8)))
    select_none(img)
    drop_shadow(islands, 5, 7, 7, 0.6)
    bloom(img, island_neon, [(5.0, 90), (16.0, 60)])

    # Target backing plates in each bank's colour.
    plates = new_layer(img, "plates")
    for t in d["targets"]:
        rgb = BANK_COLORS[t["bank"]]
        sh = t["shape"]
        select_capsule(img, sh["a"], sh["b"], sh["r"] + 3.0, S)
        fill_selection(plates, col(rgb, 1.0, 0.75))
        select_capsule(img, sh["a"], sh["b"], sh["r"] + 1.6, S)
        fill_selection(plates, col(rgb, 1.0, 0.18))
    select_none(img)

    # --- Rails -------------------------------------------------------------
    # The outer rail is dark chrome with a neon tube along its inner edge.
    rail = new_layer(img, "rail")
    select_polygon(img, shell, S)
    Gimp.Selection.grow(img, 9 * S)
    select_polygon(img, shell, S, SUBTRACT)
    linear_gradient(rail, color(0.30, 0.20, 0.50), color(0.07, 0.03, 0.16), 0, 0, W, H)
    select_polygon(img, shell, S)
    Gimp.Selection.grow(img, 9 * S)
    Gimp.Selection.border(img, 2)
    fill_selection(rail, color(0.75, 0.65, 1.0, 0.8))
    select_none(img)
    drop_shadow(rail, 4, 6, 8, 0.7)

    wall_body = new_layer(img, "wall-body")
    neon = new_layer(img, "neon")
    select_polygon(img, shell, S)
    Gimp.Selection.grow(img, 5 * S)
    select_polygon(img, shell, S, SUBTRACT)
    fill_selection(neon, col(MAGENTA))
    select_polygon(img, shell, S)
    Gimp.Selection.grow(img, 3 * S)
    Gimp.Selection.border(img, 2)
    fill_selection(neon, col(mix(MAGENTA, WHITE, 0.85)))

    wall_colors = [PURPLE, YELLOW, MINT, MINT, MINT, MINT, ORANGE, ORANGE, CYAN, CYAN, CYAN, CYAN]
    for i, w in enumerate(d["walls"]):
        rgb = wall_colors[i % len(wall_colors)]
        select_capsule(img, w["a"], w["b"], w["r"], S)
        fill_selection(wall_body, col(rgb, 1.0, 0.22))
        select_capsule(img, w["a"], w["b"], w["r"] * 0.72, S)
        fill_selection(neon, col(rgb))
        select_capsule(img, w["a"], w["b"], w["r"] * 0.30, S)
        fill_selection(neon, col(mix(rgb, WHITE, 0.85)))
    select_none(img)
    drop_shadow(wall_body, 4, 6, 6, 0.65)
    bloom(img, neon, [(4.0, 100), (12.0, 80), (30.0, 45)])

    # --- Finish: raster lines, grain and a vignette ------------------------
    raster = new_layer(img, "scanlines", opacity=13)
    scanlines(img, raster, 6, 2, color(0.0, 0.0, 0.0))
    grain = new_layer(img, "grain", opacity=7, mode=Gimp.LayerMode.OVERLAY)
    img.select_rectangle(REPLACE, 0, 0, W, H)
    fill_selection(grain, color(0.5, 0.5, 0.5))
    select_none(img)
    apply_filter(grain, "gegl:noise-hurl", pct_random=60.0, repeat=1, seed=84)
    apply_filter(grain, "gegl:saturation", scale=0.0)

    vignette = new_layer(img, "vignette", opacity=50, mode=Gimp.LayerMode.MULTIPLY)
    img.select_rectangle(REPLACE, 0, 0, W, H)
    radial_gradient(vignette, color(1.0, 1.0, 1.0), color(0.62, 0.52, 0.78),
                    cx * S, 470 * S, cx * S, (470 + 660) * S)
    select_none(img)

    img.flatten()
    path = export_png(img, repo + "/assets/table/background.png")
    img.delete()
    return path


print(make_table(REPO, LAYOUT))
