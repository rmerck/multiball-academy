# Generates the moving and lit sprites in assets/sprites.
#
#   build/pinball_layout > build/layout.json
#   REPO = "/path/to/repo"; LAYOUT = REPO + "/build/layout.json"
#   exec(open(REPO + "/tools/gimp/make_sprites.py").read())
#
# Sprites are drawn at 4x. Sizes come from the table geometry so they match
# the physics shapes. White sprites are tinted by the game at draw time.
import math

with open(REPO + "/tools/gimp/common.py", encoding="utf-8") as _f:
    exec(_f.read())

S = 4
REPLACE = Gimp.ChannelOps.REPLACE
ADD = Gimp.ChannelOps.ADD
SUBTRACT = Gimp.ChannelOps.SUBTRACT


def new_image(width, height):
    return work_image(width * S, height * S)


def finish(img, repo, name):
    select_none(img)
    img.merge_visible_layers(Gimp.MergeType.CLIP_TO_IMAGE)
    return export_png(img, repo + "/assets/sprites/" + name)


def make_flipper(repo, d):
    """Bat lying along +x. The pivot is at (MARGIN + base radius, height / 2).
    Hot pink to violet body inside a white-hot cyan tube, with a chrome hub.
    A dark halo under a cyan glow lifts it off the bright grid. MARGIN must
    match kFlipperSpriteMargin in src/platform/TableRenderer.cpp."""
    f = d["flipper"]
    margin = 9.0
    base, tip, length = f["baseRadius"], f["tipRadius"], f["length"]
    width = margin * 2 + base + length + tip
    height = margin * 2 + base * 2
    img = new_image(width, height)
    px, py = margin + base, height / 2

    def bat_selection(grow, op=REPLACE):
        select_polygon(img, [(px, py - base - grow), (px + length, py - tip - grow),
                             (px + length, py + tip + grow), (px, py + base + grow)], S, op)
        select_circle(img, (px, py), base + grow, S, ADD)
        select_circle(img, (px + length, py), tip + grow, S, ADD)

    dark = new_layer(img, "dark-halo")
    bat_selection(4.0)
    fill_selection(dark, color(0.02, 0.0, 0.07, 0.85))
    select_none(img)
    blur(dark, 9.0)

    halo = new_layer(img, "halo")
    bat_selection(2.6)
    fill_selection(halo, color(0.10, 0.85, 1.0, 0.95))
    select_none(img)
    blur(halo, 7.0)

    tube = new_layer(img, "tube")
    bat_selection(1.2)
    fill_selection(tube, color(0.25, 0.92, 1.0))
    bat_selection(0.5)
    fill_selection(tube, color(0.88, 1.0, 1.0))

    body = new_layer(img, "body")
    bat_selection(-1.3)
    linear_gradient(body, color(1.0, 0.30, 0.72), color(0.42, 0.06, 0.70),
                    0, (py - base) * S, 0, (py + base) * S)

    # A laser yellow flash down the middle, tapering with the bat.
    flash = new_layer(img, "flash")
    select_polygon(img, [(px + 14, py - 2.2), (px + length - 2, py - 0.9),
                         (px + length - 2, py + 0.9), (px + 14, py + 2.2)], S)
    fill_selection(flash, color(1.0, 0.93, 0.25))

    shine = new_layer(img, "shine", opacity=70, mode=Gimp.LayerMode.SCREEN)
    Gimp.context_set_feather(True)
    Gimp.context_set_feather_radius(4.0, 4.0)
    select_polygon(img, [(px + 4, py - base * 0.70), (px + length - 2, py - tip * 0.70),
                         (px + length - 2, py - tip * 0.30), (px + 4, py - base * 0.30)], S)
    Gimp.context_set_feather(False)
    fill_selection(shine, color(1.0, 0.90, 1.0))

    hub = new_layer(img, "hub")
    select_circle(img, (px, py), base * 0.62, S)
    fill_selection(hub, color(0.08, 0.02, 0.18))
    select_circle(img, (px, py), base * 0.50, S)
    radial_gradient(hub, color(1.0, 1.0, 1.0), color(0.35, 0.45, 0.75),
                    (px - 2) * S, (py - 2) * S, (px + base * 0.6) * S, (py + base * 0.6) * S)
    select_circle(img, (px, py), base * 0.16, S)
    fill_selection(hub, color(0.08, 0.02, 0.18))
    return finish(img, repo, "flipper.png")


def make_bumper(repo):
    """White starburst cap, tinted per bumper group by the game. 64 px
    logical. Greys only: the tint multiplies them."""
    size = 64.0
    c = (size / 2, size / 2)
    img = new_image(size, size)

    skirt = new_layer(img, "skirt")
    select_circle(img, c, 30, S)
    fill_selection(skirt, color(0.16, 0.16, 0.20))
    select_circle(img, c, 28.5, S)
    fill_selection(skirt, color(1.0, 1.0, 1.0))
    select_circle(img, c, 26.5, S)
    # A dark skirt: the bright wedges, rim and dome stand out against it even
    # at the idle tint.
    radial_gradient(skirt, color(0.42, 0.42, 0.48), color(0.20, 0.20, 0.26),
                    (c[0] - 8) * S, (c[1] - 9) * S, (c[0] + 28) * S, (c[1] + 28) * S)

    # Sixteen alternating wedges: the classic pop bumper starburst.
    burst = new_layer(img, "burst")
    op = REPLACE
    for i in range(0, 16, 2):
        a0, a1 = i * math.pi / 8, (i + 1) * math.pi / 8
        select_polygon(img, [c, (c[0] + 40 * math.cos(a0), c[1] + 40 * math.sin(a0)),
                             (c[0] + 40 * math.cos(a1), c[1] + 40 * math.sin(a1))], S, op)
        op = ADD
    select_circle(img, c, 25.5, S, Gimp.ChannelOps.INTERSECT)
    select_circle(img, c, 15.5, S, SUBTRACT)
    fill_selection(burst, color(1.0, 1.0, 1.0, 1.0))

    ring = new_layer(img, "ring")
    select_circle(img, c, 16.5, S)
    select_circle(img, c, 14.5, S, SUBTRACT)
    fill_selection(ring, color(0.08, 0.08, 0.12))

    cap = new_layer(img, "cap")
    select_circle(img, c, 14.5, S)
    radial_gradient(cap, color(1.0, 1.0, 1.0), color(0.82, 0.82, 0.88),
                    (c[0] - 4) * S, (c[1] - 5) * S, (c[0] + 14) * S, (c[1] + 14) * S)
    # A four-point star on the dome.
    select_polygon(img, [(c[0] - 9, c[1]), (c[0] - 1.6, c[1] - 1.6), (c[0], c[1] - 9),
                         (c[0] + 1.6, c[1] - 1.6), (c[0] + 9, c[1]), (c[0] + 1.6, c[1] + 1.6),
                         (c[0], c[1] + 9), (c[0] - 1.6, c[1] + 1.6)], S)
    fill_selection(cap, color(0.14, 0.14, 0.20, 0.9))

    spec = new_layer(img, "specular", opacity=90)
    Gimp.context_set_feather(True)
    Gimp.context_set_feather_radius(6.0, 6.0)
    img.select_ellipse(REPLACE, (c[0] - 12) * S, (c[1] - 13) * S, 10 * S, 6 * S)
    Gimp.context_set_feather(False)
    fill_selection(spec, color(1.0, 1.0, 1.0))
    return finish(img, repo, "bumper.png")


def make_glow(repo):
    """Soft white disc fading to transparent. Drawn additively, tinted."""
    size = 64.0
    img = new_image(size, size)
    layer = new_layer(img, "glow")
    Gimp.context_set_foreground(color(1.0, 1.0, 1.0))
    Gimp.context_set_gradient_fg_transparent()
    layer.edit_gradient_fill(Gimp.GradientType.RADIAL, 0, False, 1, 0, False,
                             size / 2 * S, size / 2 * S, size * S * 0.98, size / 2 * S)
    return finish(img, repo, "glow.png")


def make_bar(repo, d):
    """White neon bar for stand-up target faces, lying along +x. Tinted by
    the game: a dark edge, a bright outline tube, a quieter face and a
    white-hot core. The outline keeps an unlit (dimmed) bar visible."""
    length, radius, margin = 34.0, 5.0, 2.0
    width = length + 2 * radius + 2 * margin
    height = 2 * radius + 2 * margin
    img = new_image(width, height)
    a = (margin + radius, height / 2)
    b = (margin + radius + length, height / 2)
    edge = new_layer(img, "edge")
    select_capsule(img, a, b, radius + 1.0, S)
    fill_selection(edge, color(0.06, 0.04, 0.12))
    face = new_layer(img, "face")
    select_capsule(img, a, b, radius - 0.3, S)
    fill_selection(face, color(1.0, 1.0, 1.0))
    select_capsule(img, a, b, radius - 1.9, S)
    linear_gradient(face, color(0.66, 0.66, 0.70), color(0.40, 0.40, 0.46),
                    0, (height / 2 - radius) * S, 0, (height / 2 + radius) * S)
    core = new_layer(img, "core")
    select_capsule(img, (a[0] + 1, a[1] - 1.0), (b[0] - 1, b[1] - 1.0), 1.5, S)
    fill_selection(core, color(1.0, 1.0, 1.0))
    return finish(img, repo, "bar.png")


def make_plunger(repo, d):
    """Chrome rod, hot pink spring and a yellow-to-pink striker."""
    p = d["plunger"]
    width, height = p["width"], p["height"]
    img = new_image(width, height)
    rod = new_layer(img, "rod")
    img.select_rectangle(REPLACE, (width / 2 - 6) * S, 10 * S, 12 * S, (height - 10) * S)
    linear_gradient(rod, color(0.20, 0.25, 0.50), color(0.95, 0.98, 1.0),
                    (width / 2 - 6) * S, 0, (width / 2 - 1) * S, 0)
    img.select_rectangle(REPLACE, (width / 2 - 1) * S, 10 * S, 7 * S, (height - 10) * S)
    linear_gradient(rod, color(0.95, 0.98, 1.0), color(0.25, 0.10, 0.45),
                    (width / 2 - 1) * S, 0, (width / 2 + 6) * S, 0)
    # Spring coils.
    coils = new_layer(img, "coils")
    first = True
    y = 20.0
    while y < height - 4:
        select_polygon(img, [(width / 2 - 11, y + 2), (width / 2 + 11, y - 1),
                             (width / 2 + 11, y + 2), (width / 2 - 11, y + 5)], S,
                       REPLACE if first else ADD)
        first = False
        y += 8.0
    linear_gradient(coils, color(1.0, 0.45, 0.80), color(0.55, 0.05, 0.40),
                    (width / 2 - 11) * S, 0, (width / 2 + 11) * S, 0)
    head = new_layer(img, "head")
    img.select_round_rectangle(REPLACE, 1 * S, 0, (width - 2) * S, 14 * S, 5 * S, 5 * S)
    fill_selection(head, color(0.06, 0.02, 0.14))
    img.select_round_rectangle(REPLACE, 2.2 * S, 1.2 * S, (width - 4.4) * S, 11.6 * S, 4 * S, 4 * S)
    linear_gradient(head, color(1.0, 0.95, 0.30), color(1.0, 0.20, 0.60), 0, 0, 0, 14 * S)
    img.select_round_rectangle(REPLACE, 5 * S, 2.4 * S, (width - 10) * S, 2.2 * S, 1 * S, 1 * S)
    fill_selection(head, color(1.0, 1.0, 0.85, 0.9))
    return finish(img, repo, "plunger.png")


def make_sprites(repo, layout_path):
    d = load_layout(layout_path)
    return [make_flipper(repo, d), make_bumper(repo), make_glow(repo), make_bar(repo, d),
            make_plunger(repo, d)]


for _path in run_script(lambda: make_sprites(REPO, LAYOUT)):
    print(_path)
