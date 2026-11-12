# Generates assets/ui/panel.png: the information panel beside the playfield,
# styled as a neon arcade backglass.
#
#   build/pinball_layout > build/layout.json
#   REPO = "/path/to/repo"; LAYOUT = REPO + "/build/layout.json"
#   exec(open(REPO + "/tools/gimp/make_panel.py").read())
#
# The display windows come from src/core/PanelLayout.hpp via the layout dump,
# so the art always lines up with the text drawn over it.
#
# The sunset illustration behind the title is not drawn here: it is composited
# from tools/gimp/sources/backglass.png, which was made in Canva (see the
# README, "Assets").
# Everything else on the panel is drawn by this script.
import json
import math
import random

exec(open(REPO + "/tools/gimp/common.py").read())

S = 2
REPLACE = Gimp.ChannelOps.REPLACE
ADD = Gimp.ChannelOps.ADD
SUBTRACT = Gimp.ChannelOps.SUBTRACT
INTERSECT = Gimp.ChannelOps.INTERSECT

PINK = (1.0, 0.16, 0.60)
MAGENTA = (0.95, 0.10, 0.90)
CYAN = (0.10, 0.92, 1.0)
PURPLE = (0.62, 0.25, 1.0)
YELLOW = (1.0, 0.92, 0.22)
ORANGE = (1.0, 0.55, 0.15)
MINT = (0.30, 1.0, 0.70)
WELL_COLORS = [CYAN, PINK, YELLOW, MAGENTA, MINT, PURPLE]
HEADER = 150.0  # Height of the title area above the first window.


def make_panel(repo, layout_path):
    with open(layout_path) as f:
        p = json.load(f)["panel"]
    left = p["left"]
    W, H = int(p["width"] * S), int(p["height"] * S)
    PW, PH = p["width"], p["height"]
    rng = random.Random(1984)
    img = Gimp.Image.new(W, H, Gimp.ImageBaseType.RGB)
    Gimp.context_set_antialias(True)
    Gimp.context_set_feather(False)
    Gimp.context_set_interpolation(Gimp.InterpolationType.CUBIC)

    def c3(rgb, a=1.0, k=1.0):
        return color(min(1.0, rgb[0] * k), min(1.0, rgb[1] * k), min(1.0, rgb[2] * k), a)

    def rect(x, y, w, h, op=REPLACE):
        img.select_rectangle(op, x * S, y * S, w * S, h * S)

    def rrect(x, y, w, h, r, op=REPLACE):
        img.select_round_rectangle(op, x * S, y * S, w * S, h * S, r * S, r * S)

    # --- Base: deep indigo -------------------------------------------------
    base = new_layer(img, "base")
    img.select_rectangle(REPLACE, 0, 0, W, H)
    linear_gradient(base, color(0.13, 0.03, 0.28), color(0.04, 0.01, 0.12), 0, 0, 0, H)
    select_none(img)

    # --- Backglass illustration (composited source) ------------------------
    art = load_layer(img, repo + "/tools/gimp/sources/backglass.png", "backglass")
    scale = W / float(art.get_width())
    art_h = int(round(art.get_height() * scale)) / S - 6  # Bottom edge, in panel pixels.
    art.scale(W, int(round(art.get_height() * scale)), False)
    art.set_offsets(0, -6 * S)
    art.resize_to_image_size()
    apply_filter(art, "gegl:hue-chroma", chroma=12.0, lightness=-2.0)
    # Fade the illustration into the base below the title area.
    fade = new_layer(img, "fade")
    rect(0, HEADER - 26, PW, art_h - (HEADER - 26) + 2)
    alpha_gradient(fade, color(0.115, 0.027, 0.25), 0, (HEADER + 60) * S, 0, (HEADER - 26) * S)
    # A soft shade behind the title keeps the lettering clear of the sun.
    shade = new_layer(img, "title-shade", opacity=50)
    Gimp.context_set_feather(True)
    Gimp.context_set_feather_radius(70.0, 70.0)
    img.select_ellipse(REPLACE, 30 * S, 22 * S, (PW - 60) * S, 112 * S)
    Gimp.context_set_feather(False)
    fill_selection(shade, color(0.03, 0.0, 0.10))
    select_none(img)

    # --- Lower panel: a faint grid running off to a horizon ----------------
    grid = new_layer(img, "grid", opacity=30)
    gy0 = 250.0
    vx = PW / 2.0
    op = REPLACE
    for i in range(-14, 15):
        bx = vx + i * 58.0
        select_polygon(img, [(vx + i * 6.0 - 0.3, gy0), (vx + i * 6.0 + 0.3, gy0),
                             (bx + 1.0, PH), (bx - 1.0, PH)], S, op)
        op = ADD
    for k in range(1, 22):
        y = gy0 + (PH - gy0) * (k / 21.0) ** 1.8
        rect(0, y - 0.4, PW, 0.8 + 0.04 * k, ADD)
    linear_gradient(grid, c3(MAGENTA), c3(CYAN), 0, gy0 * S, 0, H)
    select_none(img)
    bloom(img, grid, [(5.0, 30)])

    # --- Memphis accents in the margins and between the windows ------------
    accents = new_layer(img, "accents")

    def tri(c, r, rot):
        return [(c[0] + r * math.cos(rot + k * 2.0944), c[1] + r * math.sin(rot + k * 2.0944))
                for k in range(3)]

    wells = [(x - left, y, w, h) for x, y, w, h in p["wells"]]
    for i in range(len(wells) - 1):
        gap_y = wells[i][1] + wells[i][3] + (wells[i + 1][1] - wells[i][1] - wells[i][3]) / 2.0
        rgb = WELL_COLORS[(i + 2) % len(WELL_COLORS)]
        # A dotted rule with a triangle at each end.
        op = REPLACE
        x = 60.0
        while x < PW - 58:
            select_circle(img, (x, gap_y), 1.0, S, op)
            op = ADD
            x += 8.0
        fill_selection(accents, c3(rgb, 0.8))
        for cx_, rot in ((36.0, 0.0), (PW - 36.0, math.pi)):
            select_polygon(img, tri((cx_, gap_y), 5.5, rot), S)
            fill_selection(accents, c3(rgb, 0.95))
    # Zigzags and squiggles down the side margins.
    for x, rgb, phase in ((11.0, PINK, 0), (PW - 11.0, CYAN, 1)):
        y = 170.0
        k = phase
        while y < 860:
            pts = [(x + (3.5 if (j + k) % 2 else -3.5), y + j * 7.0) for j in range(6)]
            select_polyline(img, pts, 0.9, S)
            fill_selection(accents, c3(rgb, 0.85))
            select_circle(img, (x, y + 52), 2.4, S)
            select_circle(img, (x, y + 52), 1.3, S, SUBTRACT)
            fill_selection(accents, c3(YELLOW, 0.85))
            y += 76.0
            k += 1
    # A dark plate behind the controls legend, with a rule above and below.
    rrect(20, 877, PW - 40, 70, 6)
    fill_selection(accents, color(0.03, 0.0, 0.09, 0.82))
    for y, rgb in ((875.0, PINK), (949.0, CYAN)):
        rect(24, y - 0.6, PW - 48, 1.2)
        fill_selection(accents, c3(rgb, 0.9))
    select_none(img)
    bloom(img, accents, [(4.0, 80)])

    # --- Display windows: dark glass inside a neon tube --------------------
    glass = new_layer(img, "glass")
    tube = new_layer(img, "tube")
    for i, (x, y, w, h) in enumerate(wells):
        rgb = WELL_COLORS[i % len(WELL_COLORS)]
        rrect(x - 5, y - 5, w + 10, h + 10, 11)
        fill_selection(glass, color(0.02, 0.0, 0.06, 1.0))
        rrect(x, y, w, h, 7)
        linear_gradient(glass, color(0.04, 0.01, 0.11), color(0.10, 0.03, 0.22),
                        0, y * S, 0, (y + h) * S)
        # Raster lines in the glass.
        select_none(img)
        op = REPLACE
        yy = y + 1.0
        while yy < y + h:
            rect(x, yy, w, 0.5, op)
            op = ADD
            yy += 3.0
        rrect(x, y, w, h, 7, INTERSECT)
        fill_selection(glass, color(0.0, 0.0, 0.0, 0.35))
        # A reflection across the top edge of the glass.
        rrect(x + 3, y + 2, w - 6, 5, 3)
        alpha_gradient(glass, c3(rgb, 0.22), 0, (y + 2) * S, 0, (y + 7) * S)
        # The tube.
        rrect(x - 3.4, y - 3.4, w + 6.8, h + 6.8, 9.5)
        rrect(x - 0.9, y - 0.9, w + 1.8, h + 1.8, 7.6, SUBTRACT)
        fill_selection(tube, c3(rgb))
        rrect(x - 2.6, y - 2.6, w + 5.2, h + 5.2, 8.9)
        rrect(x - 1.7, y - 1.7, w + 3.4, h + 3.4, 8.2, SUBTRACT)
        fill_selection(tube, color(1.0, 0.97, 1.0, 0.9))
    select_none(img)
    bloom(img, tube, [(4.0, 100), (11.0, 70), (26.0, 35)])

    # Corner studs: small neon diamonds.
    studs = new_layer(img, "studs")
    for sx, sy, rgb in ((12, 12, YELLOW), (PW - 12, 12, YELLOW), (12, PH - 12, MINT),
                        (PW - 12, PH - 12, MINT)):
        select_polygon(img, [(sx, sy - 6), (sx + 6, sy), (sx, sy + 6), (sx - 6, sy)], S)
        fill_selection(studs, c3(rgb))
        select_polygon(img, [(sx, sy - 3), (sx + 3, sy), (sx, sy + 3), (sx - 3, sy)], S)
        fill_selection(studs, color(0.04, 0.0, 0.10))
    select_none(img)
    bloom(img, studs, [(4.0, 70)])

    # A dark seam and a magenta tube where the panel meets the playfield.
    seam = new_layer(img, "seam")
    img.select_rectangle(REPLACE, 0, 0, 6 * S, H)
    alpha_gradient(seam, color(0.0, 0.0, 0.0), 0, 0, 6 * S, 0)
    img.select_rectangle(REPLACE, 2, 0, 3, H)
    fill_selection(seam, c3(MAGENTA))
    select_none(img)
    bloom(img, seam, [(5.0, 60)])

    # --- Finish: raster lines and grain -------------------------------------
    raster = new_layer(img, "scanlines", opacity=11)
    scanlines(img, raster, 6, 2, color(0.0, 0.0, 0.0))
    grain = new_layer(img, "grain", opacity=6, mode=Gimp.LayerMode.OVERLAY)
    img.select_rectangle(REPLACE, 0, 0, W, H)
    fill_selection(grain, color(0.5, 0.5, 0.5))
    select_none(img)
    apply_filter(grain, "gegl:noise-hurl", pct_random=60.0, repeat=1, seed=85)
    apply_filter(grain, "gegl:saturation", scale=0.0)

    img.flatten()
    path = export_png(img, repo + "/assets/ui/panel.png")
    img.delete()
    return path


print(make_panel(REPO, LAYOUT))
