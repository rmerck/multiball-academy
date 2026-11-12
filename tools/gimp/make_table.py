# Generates assets/table/background.png from the table geometry.
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

HOLE_COLORS = {
    "wormhole": [(1.0, 0.85, 0.2), (1.0, 0.3, 0.25), (0.3, 1.0, 0.45)],
    "blackhole": [(0.7, 0.4, 1.0)],
    "hyperspace": [(0.3, 0.9, 1.0)],
    "ramp": [(1.0, 0.6, 0.2)],
}
BUMPER_COLORS = {"attack": (1.0, 0.35, 0.3), "engine": (0.35, 0.75, 1.0),
                 "satellite": (0.6, 1.0, 0.5)}
BANK_COLORS = {"booster": (1.0, 0.7, 0.2), "medal": (0.4, 0.8, 1.0),
               "mission": (1.0, 0.35, 0.35), "multiplier": (0.5, 1.0, 0.5)}


def feather(on, radius=0.0):
    Gimp.context_set_feather(on)
    if on:
        Gimp.context_set_feather_radius(radius, radius)


def make_table(repo, layout_path):
    with open(layout_path) as f:
        d = json.load(f)
    W, H = int(d["width"] * S), int(d["height"] * S)
    cx = d["fieldCenterX"]
    shell = [tuple(p) for p in d["shell"]]
    rng = random.Random(19950824)

    img = Gimp.Image.new(W, H, Gimp.ImageBaseType.RGB)
    Gimp.context_set_antialias(True)
    feather(False)

    def clip_to_shell():
        select_polygon(img, shell, S, INTERSECT)

    # --- Cabinet: everything outside the playfield shell -------------------
    cabinet = new_layer(img, "cabinet")
    img.select_rectangle(REPLACE, 0, 0, W, H)
    linear_gradient(cabinet, color(0.09, 0.10, 0.15), color(0.03, 0.03, 0.06), 0, 0, W, H)
    select_none(img)
    apply_filter(cabinet, "gegl:noise-hurl", pct_random=6.0, repeat=1, seed=7)
    blur(cabinet, 1.2)

    # --- Space backdrop inside the shell -----------------------------------
    space = new_layer(img, "space")
    select_polygon(img, shell, S)
    linear_gradient(space, color(0.02, 0.04, 0.14), color(0.07, 0.03, 0.15), 0, 0, 0, H)

    nebula = new_layer(img, "nebula", opacity=34, mode=Gimp.LayerMode.SCREEN)
    clouds = [
        ((190, 210), 170, 120, (0.55, 0.15, 0.60)),
        ((520, 330), 190, 150, (0.10, 0.45, 0.60)),
        ((300, 560), 220, 130, (0.20, 0.20, 0.65)),
        ((560, 700), 150, 170, (0.50, 0.12, 0.35)),
        ((150, 760), 130, 110, (0.08, 0.40, 0.45)),
    ]
    for (x, y), rx, ry, rgb in clouds:
        feather(True, 150.0)
        img.select_ellipse(REPLACE, (x - rx) * S, (y - ry) * S, 2 * rx * S, 2 * ry * S)
        feather(False)
        clip_to_shell()
        fill_selection(nebula, color(*rgb))

    stars = new_layer(img, "stars")
    for brightness, count, size in ((0.45, 420, 1.0), (0.8, 150, 1.6), (1.0, 40, 2.4)):
        select_none(img)
        first = True
        for _ in range(count):
            x = rng.uniform(40, 730)
            y = rng.uniform(40, 950)
            r = size * rng.uniform(0.6, 1.2)
            img.select_ellipse(REPLACE if first else ADD, x * S - r, y * S - r, 2 * r, 2 * r)
            first = False
        clip_to_shell()
        tint = rng.choice([(1.0, 1.0, 1.0), (0.8, 0.9, 1.0), (1.0, 0.92, 0.8)])
        fill_selection(stars, color(tint[0], tint[1], tint[2], brightness))
    select_none(img)
    glow = stars.copy()
    img.insert_layer(glow, None, 0)
    blur(glow, 3.0)
    glow.set_mode(Gimp.LayerMode.SCREEN)

    # --- Decorative orbits and a ringed planet between the slingshots ------
    orbits = new_layer(img, "orbits", opacity=40)
    for radius in (150, 235, 320):
        select_circle(img, (cx, 430), radius, S)
        Gimp.Selection.border(img, 2)
        clip_to_shell()
        fill_selection(orbits, color(0.35, 0.8, 1.0))

    planet_c = (cx, 705)
    planet_r = 62

    def ring_selection(op):
        img.select_ellipse(op, (planet_c[0] - 118) * S, (planet_c[1] - 22) * S, 236 * S, 44 * S)
        img.select_ellipse(SUBTRACT, (planet_c[0] - 92) * S, (planet_c[1] - 13) * S,
                           184 * S, 26 * S)

    ring_back = new_layer(img, "ring-back")
    ring_selection(REPLACE)
    fill_selection(ring_back, color(0.85, 0.75, 0.55, 0.75))

    planet = new_layer(img, "planet")
    select_circle(img, planet_c, planet_r, S)
    radial_gradient(planet, color(1.0, 0.72, 0.38), color(0.30, 0.10, 0.08),
                    (planet_c[0] - 22) * S, (planet_c[1] - 24) * S,
                    (planet_c[0] + 62) * S, (planet_c[1] + 62) * S)
    # Cloud bands.
    for offset, height, alpha in ((-30, 9, 0.18), (-8, 13, 0.22), (20, 8, 0.16)):
        img.select_rectangle(REPLACE, (planet_c[0] - planet_r) * S, (planet_c[1] + offset) * S,
                             2 * planet_r * S, height * S)
        select_circle(img, planet_c, planet_r, S, INTERSECT)
        fill_selection(planet, color(0.45, 0.16, 0.10, alpha))

    ring_front = new_layer(img, "ring-front")
    ring_selection(REPLACE)
    img.select_rectangle(INTERSECT, 0, planet_c[1] * S, W, 60 * S)
    fill_selection(ring_front, color(0.95, 0.85, 0.62, 0.9))
    select_none(img)

    # --- Launch lane -------------------------------------------------------
    lane = d["lane"]
    lane_layer = new_layer(img, "lane")
    img.select_rectangle(REPLACE, lane["wallX"] * S, 150 * S, (lane["right"] - lane["wallX"]) * S,
                         (d["height"] - 150) * S)
    clip_to_shell()
    linear_gradient(lane_layer, color(0.16, 0.19, 0.28, 0.9), color(0.07, 0.08, 0.13, 0.9),
                    lane["wallX"] * S, 0, lane["right"] * S, 0)
    first = True
    for y in range(340, 860, 56):
        x = lane["centerX"]
        select_polygon(img, [(x, y), (x + 13, y + 16), (x + 6, y + 16), (x, y + 8),
                             (x - 6, y + 16), (x - 13, y + 16)], S, REPLACE if first else ADD)
        first = False
    fill_selection(lane_layer, color(1.0, 0.7, 0.2, 0.55))
    select_none(img)

    # --- Inserts: unlit lights and switch slots ----------------------------
    inserts = new_layer(img, "inserts")

    def insert_circle(center, radius, rgb):
        select_circle(img, center, radius + 2.5, S)
        fill_selection(inserts, color(rgb[0], rgb[1], rgb[2], 0.9))
        select_circle(img, center, radius, S)
        fill_selection(inserts, color(rgb[0] * 0.16, rgb[1] * 0.16, rgb[2] * 0.16, 1.0))

    for i, center in enumerate(d["fuelLights"]["centers"]):
        t = i / max(1, len(d["fuelLights"]["centers"]) - 1)
        insert_circle(center, d["fuelLights"]["radius"], (1.0, 0.85 - 0.6 * t, 0.15))

    for r in d["rollovers"]:
        c = r["c"]
        rgb = {"reentry": (0.4, 1.0, 0.6), "inlane": (0.4, 0.8, 1.0),
               "outlane": (1.0, 0.4, 0.35)}[r["group"]]
        select_capsule(img, (c[0], c[1] - 9), (c[0], c[1] + 9), 5.5, S)
        fill_selection(inserts, color(rgb[0], rgb[1], rgb[2], 0.9))
        select_capsule(img, (c[0], c[1] - 9), (c[0], c[1] + 9), 3.5, S)
        fill_selection(inserts, color(rgb[0] * 0.16, rgb[1] * 0.16, rgb[2] * 0.16, 1.0))

    # Bumper bases: a dark disc with a coloured rim; the cap sprite sits on top.
    for b in d["bumpers"]:
        rgb = BUMPER_COLORS[b["group"]]
        select_circle(img, b["c"], b["r"] + 7, S)
        fill_selection(inserts, color(rgb[0], rgb[1], rgb[2], 0.55))
        select_circle(img, b["c"], b["r"] + 4, S)
        fill_selection(inserts, color(0.04, 0.05, 0.09, 1.0))

    # Holes: a coloured rim around a dark well.
    wells = new_layer(img, "wells")
    seen = {}
    for h in d["holes"]:
        palette = HOLE_COLORS[h["type"]]
        rgb = palette[seen.get(h["type"], 0) % len(palette)]
        seen[h["type"]] = seen.get(h["type"], 0) + 1
        radius = d["hole"]["radius"]
        c = h["c"]
        feather(True, 14.0)
        select_circle(img, c, radius + 9, S)
        feather(False)
        fill_selection(wells, color(rgb[0], rgb[1], rgb[2], 0.55))
        select_circle(img, c, radius + 3, S)
        fill_selection(wells, color(rgb[0], rgb[1], rgb[2], 1.0))
        select_circle(img, c, radius, S)
        radial_gradient(wells, color(0.0, 0.0, 0.0), color(rgb[0] * 0.25, rgb[1] * 0.25,
                                                           rgb[2] * 0.25),
                        c[0] * S, c[1] * S, (c[0] + radius) * S, c[1] * S)
        if h["type"] == "ramp":
            # An arrow pointing up the table into the ramp.
            select_polygon(img, [(c[0], c[1] + 26), (c[0] + 13, c[1] + 44), (c[0] + 5, c[1] + 44),
                                 (c[0] + 5, c[1] + 62), (c[0] - 5, c[1] + 62),
                                 (c[0] - 5, c[1] + 44), (c[0] - 13, c[1] + 44)], S)
            fill_selection(wells, color(rgb[0], rgb[1], rgb[2], 0.8))
    select_none(img)

    # --- Islands: slingshots and the medal bank roof -----------------------
    islands = new_layer(img, "islands")
    for tri in d["slingshots"] + [d["medalIsland"]]:
        pts = [tuple(p) for p in tri]
        select_polygon(img, pts, S)
        Gimp.Selection.grow(img, 3 * S)
        fill_selection(islands, color(0.35, 0.85, 1.0, 1.0))
        select_polygon(img, pts, S)
        ys = [p[1] for p in pts]
        linear_gradient(islands, color(0.10, 0.16, 0.34), color(0.04, 0.06, 0.16),
                        0, min(ys) * S, 0, max(ys) * S)
    select_none(img)
    drop_shadow(islands, 5, 7, 7, 0.6)

    # Target backing plates in each bank's colour.
    plates = new_layer(img, "plates")
    for t in d["targets"]:
        rgb = BANK_COLORS[t["bank"]]
        sh = t["shape"]
        select_capsule(img, sh["a"], sh["b"], sh["r"] + 2.5, S)
        fill_selection(plates, color(rgb[0] * 0.35, rgb[1] * 0.35, rgb[2] * 0.35, 1.0))
    select_none(img)

    # --- Walls: shell rail and interior guides, in brushed steel -----------
    walls = new_layer(img, "walls")
    select_polygon(img, shell, S)
    Gimp.Selection.grow(img, 9 * S)
    select_polygon(img, shell, S, SUBTRACT)
    for w in d["walls"]:
        select_capsule(img, w["a"], w["b"], w["r"], S, ADD)
    linear_gradient(walls, color(0.80, 0.85, 0.95), color(0.38, 0.43, 0.56), 0, 0, W, H)
    select_none(img)
    apply_filter(walls, "gegl:emboss", azimuth=135.0, elevation=40.0, depth=3)
    # Emboss leaves the layer grey; tint it back towards cool steel.
    tint = new_layer(img, "steel-tint", opacity=70, mode=Gimp.LayerMode.OVERLAY)
    select_polygon(img, shell, S)
    Gimp.Selection.grow(img, 9 * S)
    select_polygon(img, shell, S, SUBTRACT)
    for w in d["walls"]:
        select_capsule(img, w["a"], w["b"], w["r"], S, ADD)
    fill_selection(tint, color(0.55, 0.68, 0.95))
    select_none(img)
    drop_shadow(walls, 5, 7, 7, 0.65)

    # --- Vignette over the playfield ---------------------------------------
    vignette = new_layer(img, "vignette", opacity=55, mode=Gimp.LayerMode.MULTIPLY)
    img.select_rectangle(REPLACE, 0, 0, W, H)
    radial_gradient(vignette, color(1.0, 1.0, 1.0), color(0.25, 0.25, 0.35),
                    cx * S, 470 * S, cx * S, (470 + 640) * S)
    select_none(img)

    img.flatten()
    path = export_png(img, repo + "/assets/table/background.png")
    img.delete()
    return path


print(make_table(REPO, LAYOUT))
