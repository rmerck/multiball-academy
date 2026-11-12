# Generates assets/sprites/ball.png: a bright chrome ball reflecting a neon
# sky above a dark horizon. Run inside GIMP 3.x (Python console or MCP):
#   REPO = "/path/to/repo"; exec(open(REPO + "/tools/gimp/make_ball.py").read())
exec(open(REPO + "/tools/gimp/common.py").read())

SIZE = 96          # pixels; drawn at 24 logical pixels, so 4x for high-DPI
MARGIN = 4


def make_ball(repo):
    img = Gimp.Image.new(SIZE, SIZE, Gimp.ImageBaseType.RGB)
    Gimp.context_set_antialias(True)
    Gimp.context_set_feather(False)
    REPLACE = Gimp.ChannelOps.REPLACE
    INTERSECT = Gimp.ChannelOps.INTERSECT
    SUBTRACT = Gimp.ChannelOps.SUBTRACT
    d = SIZE - 2 * MARGIN

    def ball(op=REPLACE, inset=0.0):
        img.select_ellipse(op, MARGIN + inset, MARGIN + inset, d - 2 * inset, d - 2 * inset)

    # A dark edge so the ball separates from bright art.
    edge = new_layer(img, "edge")
    ball()
    fill_selection(edge, color(0.05, 0.02, 0.14))

    # Sky half: white at the top, falling to electric blue.
    body = new_layer(img, "body")
    ball(inset=2.0)
    linear_gradient(body, color(1.0, 1.0, 1.0), color(0.45, 0.80, 1.0), 0, MARGIN, 0, SIZE * 0.56)
    # Ground half: the curved horizon is the top of a wide ellipse.
    img.select_ellipse(REPLACE, -SIZE * 0.35, SIZE * 0.52, SIZE * 1.7, SIZE * 1.1)
    ball(INTERSECT, 2.0)
    linear_gradient(body, color(0.16, 0.05, 0.36), color(1.0, 0.55, 0.85), 0, SIZE * 0.52, 0,
                    SIZE * 0.80)
    img.select_ellipse(REPLACE, -SIZE * 0.35, SIZE * 0.74, SIZE * 1.7, SIZE * 1.1)
    ball(INTERSECT, 2.0)
    linear_gradient(body, color(1.0, 0.62, 0.86), color(1.0, 0.95, 0.80), 0, SIZE * 0.74, 0,
                    SIZE - MARGIN)
    # A thin bright line just above the horizon.
    img.select_ellipse(REPLACE, -SIZE * 0.35, SIZE * 0.495, SIZE * 1.7, SIZE * 1.1)
    img.select_ellipse(SUBTRACT, -SIZE * 0.35, SIZE * 0.52, SIZE * 1.7, SIZE * 1.1)
    ball(INTERSECT, 2.0)
    fill_selection(body, color(1.0, 1.0, 1.0))

    # Sphere shading: darker towards the lower right rim.
    shade = new_layer(img, "shade", opacity=55, mode=Gimp.LayerMode.MULTIPLY)
    ball(inset=2.0)
    radial_gradient(shade, color(1.0, 1.0, 1.0), color(0.72, 0.70, 0.86), SIZE * 0.42,
                    SIZE * 0.38, SIZE * 0.98, SIZE * 0.98)

    # A rim light all the way round keeps the outline readable on dark art.
    rim = new_layer(img, "rim", opacity=80)
    ball(inset=2.0)
    ball(SUBTRACT, 5.0)
    fill_selection(rim, color(0.85, 0.97, 1.0))
    select_none(img)
    blur(rim, 1.2)

    # Specular highlights.
    spec = new_layer(img, "specular", opacity=95)
    Gimp.context_set_feather(True)
    Gimp.context_set_feather_radius(5, 5)
    img.select_ellipse(REPLACE, SIZE * 0.24, SIZE * 0.16, SIZE * 0.28, SIZE * 0.18)
    Gimp.context_set_feather(False)
    fill_selection(spec, color(1.0, 1.0, 1.0))
    img.select_ellipse(REPLACE, SIZE * 0.62, SIZE * 0.26, SIZE * 0.07, SIZE * 0.07)
    fill_selection(spec, color(1.0, 1.0, 1.0))
    select_none(img)

    img.merge_visible_layers(Gimp.MergeType.CLIP_TO_IMAGE)
    path = export_png(img, repo + "/assets/sprites/ball.png")
    img.delete()
    return path


print(make_ball(REPO))
