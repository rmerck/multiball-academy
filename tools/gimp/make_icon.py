# Generates packaging/macos/icon_1024.png, the application icon: a chrome
# pinball over a slatted sunset and a neon grid floor, framed by a neon tube,
# inside the macOS rounded-square icon shape. Run inside GIMP 3.x (Python
# console or MCP):
#   REPO = "/path/to/repo"; exec(open(REPO + "/tools/gimp/make_icon.py").read())
# Then tools/make_icns.sh turns the PNG into packaging/macos/AppIcon.icns.
#
# The canvas follows the macOS icon grid: an 824 pixel rounded square centred
# in 1024, transparent outside, with a soft drop shadow below it.
import random

with open(REPO + "/tools/gimp/common.py", encoding="utf-8") as _f:
    exec(_f.read())

CANVAS = 1024
PLATE = 824                      # the rounded square
ORIGIN = (CANVAS - PLATE) // 2   # 100
CORNER = 185.0                   # about 22.5 % of the plate
HORIZON = 632
SUN = (512, 420, 262)            # centre x, centre y, radius
BALL = (512, 640, 176)


def make_icon(repo):
    img = work_image(CANVAS, CANVAS)
    REPLACE = Gimp.ChannelOps.REPLACE
    ADD = Gimp.ChannelOps.ADD
    INTERSECT = Gimp.ChannelOps.INTERSECT
    SUBTRACT = Gimp.ChannelOps.SUBTRACT
    left, top = ORIGIN, ORIGIN
    right, bottom = ORIGIN + PLATE, ORIGIN + PLATE

    def plate(op=REPLACE, inset=0.0):
        r = max(CORNER - inset, 1.0)
        img.select_round_rectangle(op, left + inset, top + inset, PLATE - 2 * inset,
                                   PLATE - 2 * inset, r, r)

    # ---- Sky: deep space falling to a magenta dusk at the horizon ----
    sky = new_layer(img, "sky")
    img.select_rectangle(REPLACE, left, top, PLATE, 330)
    linear_gradient(sky, color(0.03, 0.01, 0.12), color(0.16, 0.03, 0.34), 0, top, 0, top + 330)
    img.select_rectangle(REPLACE, left, top + 330, PLATE, HORIZON - top - 330)
    linear_gradient(sky, color(0.16, 0.03, 0.34), color(0.78, 0.10, 0.56), 0, top + 330, 0,
                    HORIZON)

    stars = new_layer(img, "stars")
    rng = random.Random(1986)
    for _ in range(70):
        x = rng.uniform(left + 20, right - 20)
        y = rng.uniform(top + 20, HORIZON - 190)
        r = rng.uniform(1.4, 3.6)
        a = rng.uniform(0.45, 1.0)
        tint = rng.choice(((1.0, 1.0, 1.0), (0.80, 0.95, 1.0), (1.0, 0.80, 0.95)))
        select_circle(img, (x, y), r, 1)
        fill_selection(stars, color(tint[0], tint[1], tint[2], a))
    select_none(img)
    bloom(img, stars, [(4.0, 60)])

    # ---- Sun: yellow to hot pink, cut into slats that widen downwards ----
    sx, sy, sr = SUN
    halo = new_layer(img, "sun-halo", opacity=70, mode=Gimp.LayerMode.ADDITION)
    select_circle(img, (sx, sy), sr + 14, 1)
    fill_selection(halo, color(1.0, 0.15, 0.55))
    select_none(img)
    blur(halo, 46.0)

    sun = new_layer(img, "sun")
    select_circle(img, (sx, sy), sr, 1)
    y = sy - 26.0
    gap = 9.0
    while y < sy + sr:
        img.select_rectangle(SUBTRACT, sx - sr - 2, y, 2 * sr + 4, gap)
        y += gap + 30.0
        gap += 4.5
    img.select_rectangle(INTERSECT, left, top, PLATE, HORIZON - top)
    linear_gradient(sun, color(1.0, 0.86, 0.10), color(0.98, 0.04, 0.46), 0, sy - sr, 0,
                    sy + sr * 0.82)
    select_none(img)
    bloom(img, sun, [(10.0, 35)])

    # ---- Floor: dark, with a neon grid running to the horizon ----
    floor = new_layer(img, "floor")
    img.select_rectangle(REPLACE, left, HORIZON, PLATE, bottom - HORIZON)
    linear_gradient(floor, color(0.13, 0.02, 0.28), color(0.02, 0.0, 0.08), 0, HORIZON, 0,
                    bottom)
    select_none(img)

    grid = new_layer(img, "grid")
    depth = bottom - HORIZON
    op = REPLACE
    # Lines that run away from the viewer meet at the middle of the horizon.
    for k in range(-9, 10):
        x0 = 512 + k * 34.0
        x1 = 512 + k * 172.0
        img.select_polygon(op, [x0 - 1.2, HORIZON, x0 + 1.2, HORIZON,
                                x1 + 4.5, bottom, x1 - 4.5, bottom])
        op = ADD
    # Lines across, closer together towards the horizon.
    for i in range(1, 9):
        t = (i / 8.0) ** 2.1
        yy = HORIZON + depth * t
        thick = 2.0 + 6.0 * t
        img.select_rectangle(ADD, left, yy - thick / 2, PLATE, thick)
    linear_gradient(grid, color(1.0, 0.25, 0.85), color(0.25, 0.80, 1.0), 0, HORIZON, 0, bottom)
    select_none(img)
    bloom(img, grid, [(5.0, 80), (16.0, 45)])

    # The horizon itself: a white-hot line with a pink glow.
    shine = new_layer(img, "horizon")
    img.select_rectangle(REPLACE, left, HORIZON - 3, PLATE, 6)
    fill_selection(shine, color(1.0, 0.92, 0.98))
    select_none(img)
    bloom(img, shine, [(8.0, 90), (26.0, 60)])

    # ---- The ball's shadow and its glow on the floor ----
    bx, by, br = BALL
    shadow = new_layer(img, "ball-shadow")
    img.select_ellipse(REPLACE, bx - br * 1.05, by + br * 0.80, br * 2.1, br * 0.42)
    fill_selection(shadow, color(0.01, 0.0, 0.05, 0.9))
    select_none(img)
    blur(shadow, 16.0)
    under = new_layer(img, "ball-glow", opacity=55, mode=Gimp.LayerMode.ADDITION)
    select_circle(img, (bx, by), br + 16, 1)
    fill_selection(under, color(0.30, 0.75, 1.0))
    select_none(img)
    blur(under, 30.0)

    # ---- Chrome ball: a neon sky above a dark horizon, as on the table ----
    def ball(op=REPLACE, inset=0.0):
        select_circle(img, (bx, by), br - inset, 1, op)

    def band(op, y_top):
        # The curved horizon inside the ball is the top of a wide ellipse.
        img.select_ellipse(op, bx - br * 1.9, by + br * y_top, br * 3.8, br * 2.4)

    edge = new_layer(img, "ball-edge")
    ball()
    fill_selection(edge, color(0.05, 0.02, 0.14))

    body = new_layer(img, "ball-body")
    ball(inset=7.0)
    linear_gradient(body, color(1.0, 1.0, 1.0), color(0.42, 0.78, 1.0), 0, by - br, 0,
                    by + br * 0.12)
    band(REPLACE, 0.04)
    ball(INTERSECT, 7.0)
    linear_gradient(body, color(0.14, 0.04, 0.34), color(1.0, 0.50, 0.84), 0, by + br * 0.04, 0,
                    by + br * 0.62)
    band(REPLACE, 0.52)
    ball(INTERSECT, 7.0)
    linear_gradient(body, color(1.0, 0.60, 0.86), color(1.0, 0.95, 0.80), 0, by + br * 0.52, 0,
                    by + br)
    band(REPLACE, -0.02)
    band(SUBTRACT, 0.04)
    ball(INTERSECT, 7.0)
    fill_selection(body, color(1.0, 1.0, 1.0))

    shade = new_layer(img, "ball-shade", opacity=55, mode=Gimp.LayerMode.MULTIPLY)
    ball(inset=7.0)
    radial_gradient(shade, color(1.0, 1.0, 1.0), color(0.70, 0.68, 0.86), bx - br * 0.16,
                    by - br * 0.24, bx + br * 1.02, by + br * 1.02)

    rim = new_layer(img, "ball-rim", opacity=80)
    ball(inset=7.0)
    ball(SUBTRACT, 17.0)
    fill_selection(rim, color(0.85, 0.97, 1.0))
    select_none(img)
    blur(rim, 4.0)

    spec = new_layer(img, "ball-specular", opacity=95)
    Gimp.context_set_feather(True)
    Gimp.context_set_feather_radius(18, 18)
    img.select_ellipse(REPLACE, bx - br * 0.56, by - br * 0.74, br * 0.62, br * 0.40)
    Gimp.context_set_feather(False)
    fill_selection(spec, color(1.0, 1.0, 1.0))
    select_circle(img, (bx + br * 0.36, by - br * 0.42), br * 0.075, 1)
    fill_selection(spec, color(1.0, 1.0, 1.0))
    select_none(img)

    # ---- Neon tube round the inside of the plate: pink above, cyan below ----
    tube = new_layer(img, "tube")
    plate(REPLACE, 26.0)
    plate(SUBTRACT, 38.0)
    linear_gradient(tube, color(1.0, 0.20, 0.70), color(0.15, 0.85, 1.0), 0, top + 160, 0,
                    bottom - 160)
    plate(REPLACE, 30.0)
    plate(SUBTRACT, 34.0)
    fill_selection(tube, color(1.0, 1.0, 1.0, 0.85))
    select_none(img)
    bloom(img, tube, [(6.0, 85), (20.0, 50)])

    # ---- Cut everything to the plate, then lay the shadow underneath ----
    art = img.merge_visible_layers(Gimp.MergeType.CLIP_TO_IMAGE)
    plate()
    Gimp.Selection.invert(img)
    art.edit_clear()
    select_none(img)

    drop = new_layer(img, "drop-shadow")
    img.lower_item_to_bottom(drop)
    plate()
    fill_selection(drop, color(0.0, 0.0, 0.0, 0.5))
    select_none(img)
    drop.set_offsets(0, 12)
    blur(drop, 14.0)

    img.merge_visible_layers(Gimp.MergeType.CLIP_TO_IMAGE)
    return export_png(img, repo + "/packaging/macos/icon_1024.png")


print(run_script(lambda: make_icon(REPO)))
