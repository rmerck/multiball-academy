# Shared helpers for the asset scripts. Loaded by each make_*.py with exec().
import json
import os

import gi
gi.require_version("Gimp", "3.0")
from gi.repository import Gegl, Gimp, Gio

# Images made by the running script. run_script deletes whatever is left.
_WORK_IMAGES = []


def work_image(width, height):
    """A new RGB image that run_script deletes when the script ends, however
    it ends."""
    img = Gimp.Image.new(int(width), int(height), Gimp.ImageBaseType.RGB)
    _WORK_IMAGES.append(img)
    return img


def load_layout(path):
    """Reads the table geometry. The scripts refuse to run without it."""
    if not os.path.isfile(path):
        raise RuntimeError("Layout file not found: " + str(path) +
                           " (run build/pinball_layout > build/layout.json first)")
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def run_script(work):
    """Runs work() with a known GIMP context and puts the caller's context
    back afterwards. Every setting the drawing depends on is set here, so the
    result does not depend on what the user last did in GIMP."""
    Gimp.context_push()
    try:
        Gimp.context_set_defaults()
        Gimp.context_set_antialias(True)
        Gimp.context_set_feather(False)
        Gimp.context_set_feather_radius(0.0, 0.0)
        Gimp.context_set_opacity(100.0)
        Gimp.context_set_paint_mode(Gimp.LayerMode.NORMAL)
        Gimp.context_set_interpolation(Gimp.InterpolationType.CUBIC)
        Gimp.context_set_gradient_blend_color_space(
            Gimp.GradientBlendColorSpace.RGB_PERCEPTUAL)
        Gimp.context_set_gradient_reverse(False)
        Gimp.context_set_gradient_repeat_mode(Gimp.RepeatMode.NONE)
        Gimp.context_set_sample_merged(False)
        return work()
    finally:
        while _WORK_IMAGES:
            img = _WORK_IMAGES.pop()
            if img.is_valid():
                img.delete()
        Gimp.context_pop()


def _linear(v):
    return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4


def color(r, g, b, a=1.0):
    """A colour from sRGB components in 0..1, as a colour picker shows them.
    Gegl.Color.set_rgba takes linear light, so the components are converted;
    passing sRGB numbers straight in gives washed-out pastels."""
    c = Gegl.Color.new("black")
    c.set_rgba(_linear(r), _linear(g), _linear(b), a)
    return c


def new_layer(img, name, opacity=100, mode=Gimp.LayerMode.NORMAL):
    layer = Gimp.Layer.new(img, name, img.get_width(), img.get_height(),
                           Gimp.ImageType.RGBA_IMAGE, opacity, mode)
    img.insert_layer(layer, None, 0)
    layer.fill(Gimp.FillType.TRANSPARENT)
    return layer


def export_png(img, path):
    """Exports with every metadata option off, so output carries no EXIF,
    XMP, thumbnail, colour profile, comment or timestamp and is reproducible.
    tools/check_assets.py enforces this."""
    proc = Gimp.get_pdb().lookup_procedure("file-png-export")
    cfg = proc.create_config()
    cfg.set_property("run-mode", Gimp.RunMode.NONINTERACTIVE)
    cfg.set_property("image", img)
    cfg.set_property("file", Gio.File.new_for_path(path))
    names = {p.get_name() for p in cfg.list_properties()}
    off = ("include-exif", "include-iptc", "include-xmp", "include-color-profile",
           "include-thumbnail", "include-comment", "time", "bkgd", "offs", "phys",
           "interlaced")
    # A missing option would leave some metadata switched on without a word.
    missing = [name for name in off + ("compression",) if name not in names]
    if missing:
        raise RuntimeError("file-png-export has no option(s) " + ", ".join(missing) +
                           "; cannot guarantee a metadata-free export of " + path)
    for name in off:
        cfg.set_property(name, False)
    cfg.set_property("compression", 9)
    result = proc.run(cfg)
    if result.index(0) != Gimp.PDBStatusType.SUCCESS:
        raise RuntimeError("PNG export failed for " + path)
    return path


# ---- Drawing helpers shared by the table and sprite scripts ----

def set_fg(c):
    Gimp.context_set_foreground(c)


def select_none(img):
    Gimp.Selection.none(img)


def select_polygon(img, points, scale, op=Gimp.ChannelOps.REPLACE):
    flat = []
    for x, y in points:
        flat.extend([x * scale, y * scale])
    img.select_polygon(op, flat)


def select_circle(img, center, radius, scale, op=Gimp.ChannelOps.REPLACE):
    x, y = center
    img.select_ellipse(op, (x - radius) * scale, (y - radius) * scale,
                       2 * radius * scale, 2 * radius * scale)


def select_capsule(img, a, b, radius, scale, op=Gimp.ChannelOps.REPLACE):
    """A thick line with round ends: a quad plus a circle at each end."""
    import math
    dx, dy = b[0] - a[0], b[1] - a[1]
    length = math.hypot(dx, dy) or 1.0
    nx, ny = -dy / length * radius, dx / length * radius
    quad = [(a[0] + nx, a[1] + ny), (b[0] + nx, b[1] + ny),
            (b[0] - nx, b[1] - ny), (a[0] - nx, a[1] - ny)]
    select_polygon(img, quad, scale, op)
    select_circle(img, a, radius, scale, Gimp.ChannelOps.ADD)
    select_circle(img, b, radius, scale, Gimp.ChannelOps.ADD)


def fill_selection(layer, c):
    """Fills the selection with c. A fill ignores the alpha of the foreground
    colour, so the colour's alpha is applied as the fill opacity instead."""
    set_fg(c)
    Gimp.context_set_opacity(c.get_rgba()[3] * 100.0)
    layer.edit_fill(Gimp.FillType.FOREGROUND)
    Gimp.context_set_opacity(100.0)


# Gradients are drawn without dithering. GIMP seeds its dither from an
# unseeded random source, which would make every regeneration differ.
def linear_gradient(layer, c1, c2, x1, y1, x2, y2):
    Gimp.context_set_foreground(c1)
    Gimp.context_set_background(c2)
    Gimp.context_set_gradient_fg_bg_rgb()
    layer.edit_gradient_fill(Gimp.GradientType.LINEAR, 0, False, 1, 0, False, x1, y1, x2, y2)


def alpha_gradient(layer, c, x1, y1, x2, y2):
    """Linear fade from c at (x1, y1) to fully transparent at (x2, y2). The
    two-colour gradients above ignore alpha; this is the way to fade out."""
    Gimp.context_set_foreground(c)
    Gimp.context_set_gradient_fg_transparent()
    Gimp.context_set_opacity(c.get_rgba()[3] * 100.0)
    layer.edit_gradient_fill(Gimp.GradientType.LINEAR, 0, False, 1, 0, False, x1, y1, x2, y2)
    Gimp.context_set_opacity(100.0)


def radial_gradient(layer, c1, c2, cx, cy, ex, ey):
    Gimp.context_set_foreground(c1)
    Gimp.context_set_background(c2)
    Gimp.context_set_gradient_fg_bg_rgb()
    layer.edit_gradient_fill(Gimp.GradientType.RADIAL, 0, False, 1, 0, False, cx, cy, ex, ey)


def apply_filter(layer, operation, **props):
    """Applies a GEGL filter destructively. Property names use dashes."""
    f = Gimp.DrawableFilter.new(layer, operation, operation)
    cfg = f.get_config()
    for key, value in props.items():
        cfg.set_property(key.replace("_", "-"), value)
    layer.merge_filter(f)


def blur(layer, radius):
    apply_filter(layer, "gegl:gaussian-blur", std_dev_x=float(radius), std_dev_y=float(radius))


def drop_shadow(layer, x, y, radius, opacity):
    apply_filter(layer, "gegl:dropshadow", x=float(x), y=float(y), radius=float(radius),
                 opacity=float(opacity))


# ---- Neon helpers ----

def bloom(img, layer, passes):
    """Adds blurred additive copies of a layer directly above it, which reads
    as the glow around a neon tube. passes is a list of (radius, opacity)."""
    made = []
    for radius, opacity in passes:
        g = layer.copy()
        img.insert_layer(g, None, img.get_item_position(layer))
        blur(g, radius)
        g.set_mode(Gimp.LayerMode.ADDITION)
        g.set_opacity(float(opacity))
        made.append(g)
    return made


def select_ring(img, center, outer, inner, scale, op=Gimp.ChannelOps.REPLACE):
    select_circle(img, center, outer, scale, op)
    select_circle(img, center, inner, scale, Gimp.ChannelOps.SUBTRACT)


def select_polyline(img, points, radius, scale, op=Gimp.ChannelOps.REPLACE):
    """A run of capsules through the points. Leaves the union selected."""
    first = op
    for i in range(len(points) - 1):
        select_capsule(img, points[i], points[i + 1], radius, scale, first)
        first = Gimp.ChannelOps.ADD


def scanlines(img, layer, period, thickness, c):
    """Horizontal lines across the whole image: a CRT raster."""
    w, h = img.get_width(), img.get_height()
    op = Gimp.ChannelOps.REPLACE
    y = 0
    while y < h:
        img.select_rectangle(op, 0, y, w, thickness)
        op = Gimp.ChannelOps.ADD
        y += period
    fill_selection(layer, c)
    select_none(img)


def load_layer(img, path, name):
    """Loads an image file as a new top layer of img (at its own size)."""
    layer = Gimp.file_load_layer(Gimp.RunMode.NONINTERACTIVE, img, Gio.File.new_for_path(path))
    layer.set_name(name)
    img.insert_layer(layer, None, 0)
    return layer
