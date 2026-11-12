# Generates assets/sprites/ball.png. Run inside GIMP 3.x (Python console or MCP):
#   REPO = "/path/to/repo"; exec(open(REPO + "/tools/gimp/make_ball.py").read())
exec(open(REPO + "/tools/gimp/common.py").read())

SIZE = 96          # pixels; drawn at 24 logical pixels, so 4x for high-DPI
MARGIN = 4

def make_ball(repo):
    img = Gimp.Image.new(SIZE, SIZE, Gimp.ImageBaseType.RGB)
    layer = Gimp.Layer.new(img, "ball", SIZE, SIZE, Gimp.ImageType.RGBA_IMAGE, 100,
                           Gimp.LayerMode.NORMAL)
    img.insert_layer(layer, None, 0)
    layer.fill(Gimp.FillType.TRANSPARENT)

    d = SIZE - 2 * MARGIN
    Gimp.context_set_antialias(True)
    Gimp.context_set_feather(False)

    # Steel body: radial gradient, light upper-left to dark rim.
    img.select_ellipse(Gimp.ChannelOps.REPLACE, MARGIN, MARGIN, d, d)
    Gimp.context_set_foreground(color(0.93, 0.95, 1.0))
    Gimp.context_set_background(color(0.16, 0.18, 0.26))
    Gimp.context_set_gradient_fg_bg_rgb()
    cx, cy = SIZE * 0.38, SIZE * 0.34
    layer.edit_gradient_fill(Gimp.GradientType.RADIAL, 0, False, 1, 0, True,
                             cx, cy, SIZE * 0.95, SIZE * 0.95)

    # Soft reflected light along the lower-right rim.
    rim = Gimp.Layer.new(img, "rim", SIZE, SIZE, Gimp.ImageType.RGBA_IMAGE, 45,
                         Gimp.LayerMode.SCREEN)
    img.insert_layer(rim, None, 0)
    rim.fill(Gimp.FillType.TRANSPARENT)
    Gimp.context_set_foreground(color(0.45, 0.6, 1.0))
    Gimp.context_set_gradient_fg_transparent()
    rim.edit_gradient_fill(Gimp.GradientType.RADIAL, 0, False, 1, 0, True,
                           SIZE * 0.70, SIZE * 0.74, SIZE * 0.42, SIZE * 0.46)

    # Specular highlight.
    spec = Gimp.Layer.new(img, "specular", SIZE, SIZE, Gimp.ImageType.RGBA_IMAGE, 90,
                          Gimp.LayerMode.NORMAL)
    img.insert_layer(spec, None, 0)
    spec.fill(Gimp.FillType.TRANSPARENT)
    Gimp.context_set_feather(True)
    Gimp.context_set_feather_radius(6, 6)
    img.select_ellipse(Gimp.ChannelOps.REPLACE, SIZE * 0.24, SIZE * 0.18, SIZE * 0.26, SIZE * 0.2)
    Gimp.context_set_foreground(color(1.0, 1.0, 1.0))
    spec.edit_fill(Gimp.FillType.FOREGROUND)
    Gimp.context_set_feather(False)
    Gimp.Selection.none(img)

    img.merge_visible_layers(Gimp.MergeType.CLIP_TO_IMAGE)
    path = export_png(img, repo + "/assets/sprites/ball.png")
    img.delete()
    return path


print(make_ball(REPO))
