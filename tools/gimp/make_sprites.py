# Generates the moving and lit sprites in assets/sprites.
#
#   build/pinball_layout > build/layout.json
#   REPO = "/path/to/repo"; LAYOUT = REPO + "/build/layout.json"
#   exec(open(REPO + "/tools/gimp/make_sprites.py").read())
#
# Sprites are drawn at 4x. Sizes come from the table geometry so they match
# the physics shapes. White sprites are tinted by the game at draw time.
import json

exec(open(REPO + "/tools/gimp/common.py").read())

S = 4
REPLACE = Gimp.ChannelOps.REPLACE
ADD = Gimp.ChannelOps.ADD
SUBTRACT = Gimp.ChannelOps.SUBTRACT


def new_image(width, height):
    img = Gimp.Image.new(int(width * S), int(height * S), Gimp.ImageBaseType.RGB)
    Gimp.context_set_antialias(True)
    Gimp.context_set_feather(False)
    return img


def finish(img, repo, name):
    select_none(img)
    img.merge_visible_layers(Gimp.MergeType.CLIP_TO_IMAGE)
    path = export_png(img, repo + "/assets/sprites/" + name)
    img.delete()
    return path


def make_flipper(repo, d):
    """Bat lying along +x. The pivot is at (MARGIN + base radius, height / 2)."""
    f = d["flipper"]
    margin = 3.0
    base, tip, length = f["baseRadius"], f["tipRadius"], f["length"]
    width = margin * 2 + base + length + tip
    height = margin * 2 + base * 2
    img = new_image(width, height)
    px, py = margin + base, height / 2

    def bat_selection(grow):
        select_polygon(img, [(px, py - base - grow), (px + length, py - tip - grow),
                             (px + length, py + tip + grow), (px, py + base + grow)], S)
        select_circle(img, (px, py), base + grow, S, ADD)
        select_circle(img, (px + length, py), tip + grow, S, ADD)

    shadow = new_layer(img, "outline")
    bat_selection(1.5)
    fill_selection(shadow, color(0.25, 0.04, 0.02))

    body = new_layer(img, "body")
    bat_selection(0.0)
    linear_gradient(body, color(1.0, 0.55, 0.25), color(0.62, 0.10, 0.05),
                    0, (py - base) * S, 0, (py + base) * S)

    shine = new_layer(img, "shine", opacity=55, mode=Gimp.LayerMode.SCREEN)
    Gimp.context_set_feather(True)
    Gimp.context_set_feather_radius(5.0, 5.0)
    select_polygon(img, [(px + 4, py - base * 0.62), (px + length - 2, py - tip * 0.62),
                         (px + length - 2, py - tip * 0.15), (px + 4, py - base * 0.15)], S)
    Gimp.context_set_feather(False)
    fill_selection(shine, color(1.0, 0.95, 0.85))

    hub = new_layer(img, "hub")
    select_circle(img, (px, py), base * 0.5, S)
    radial_gradient(hub, color(0.95, 0.96, 1.0), color(0.35, 0.38, 0.5),
                    (px - 2) * S, (py - 2) * S, (px + base * 0.6) * S, (py + base * 0.6) * S)
    return finish(img, repo, "flipper.png")


def make_bumper(repo):
    """White domed cap, tinted per bumper group by the game. 64 px logical."""
    size = 64.0
    c = (size / 2, size / 2)
    img = new_image(size, size)

    skirt = new_layer(img, "skirt")
    select_circle(img, c, 30, S)
    fill_selection(skirt, color(0.20, 0.20, 0.24))
    select_circle(img, c, 28, S)
    radial_gradient(skirt, color(1.0, 1.0, 1.0), color(0.50, 0.50, 0.56),
                    (c[0] - 8) * S, (c[1] - 9) * S, (c[0] + 28) * S, (c[1] + 28) * S)

    ring = new_layer(img, "ring")
    select_circle(img, c, 20, S)
    select_circle(img, c, 17.5, S, SUBTRACT)
    fill_selection(ring, color(0.30, 0.30, 0.36, 0.9))

    cap = new_layer(img, "cap")
    select_circle(img, c, 13, S)
    radial_gradient(cap, color(1.0, 1.0, 1.0), color(0.62, 0.62, 0.70),
                    (c[0] - 4) * S, (c[1] - 5) * S, (c[0] + 13) * S, (c[1] + 13) * S)

    spec = new_layer(img, "specular", opacity=85)
    Gimp.context_set_feather(True)
    Gimp.context_set_feather_radius(7.0, 7.0)
    img.select_ellipse(REPLACE, (c[0] - 15) * S, (c[1] - 19) * S, 14 * S, 9 * S)
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
    """White rounded bar for stand-up target faces, lying along +x."""
    length, radius, margin = 34.0, 5.0, 2.0
    width = length + 2 * radius + 2 * margin
    height = 2 * radius + 2 * margin
    img = new_image(width, height)
    a = (margin + radius, height / 2)
    b = (margin + radius + length, height / 2)
    edge = new_layer(img, "edge")
    select_capsule(img, a, b, radius + 1.0, S)
    fill_selection(edge, color(0.15, 0.15, 0.18))
    face = new_layer(img, "face")
    select_capsule(img, a, b, radius - 0.5, S)
    linear_gradient(face, color(1.0, 1.0, 1.0), color(0.55, 0.55, 0.60),
                    0, (height / 2 - radius) * S, 0, (height / 2 + radius) * S)
    return finish(img, repo, "bar.png")


def make_plunger(repo, d):
    p = d["plunger"]
    width, height = p["width"], p["height"]
    img = new_image(width, height)
    rod = new_layer(img, "rod")
    img.select_rectangle(REPLACE, (width / 2 - 6) * S, 10 * S, 12 * S, (height - 10) * S)
    linear_gradient(rod, color(0.85, 0.87, 0.95), color(0.30, 0.32, 0.42),
                    (width / 2 - 6) * S, 0, (width / 2 + 6) * S, 0)
    # Spring coils.
    coils = new_layer(img, "coils")
    first = True
    y = 20.0
    while y < height - 4:
        img.select_rectangle(REPLACE if first else ADD, (width / 2 - 10) * S, y * S, 20 * S,
                             3 * S)
        first = False
        y += 8.0
    fill_selection(coils, color(0.55, 0.58, 0.68))
    head = new_layer(img, "head")
    img.select_round_rectangle(REPLACE, 1 * S, 0, (width - 2) * S, 14 * S, 5 * S, 5 * S)
    linear_gradient(head, color(1.0, 0.45, 0.30), color(0.55, 0.10, 0.08), 0, 0, 0, 14 * S)
    return finish(img, repo, "plunger.png")


with open(LAYOUT) as _f:
    _d = json.load(_f)
print(make_flipper(REPO, _d))
print(make_bumper(REPO))
print(make_glow(REPO))
print(make_bar(REPO, _d))
print(make_plunger(REPO, _d))
