# Shared helpers for the asset scripts. Loaded by each make_*.py with exec().
import gi
gi.require_version("Gimp", "3.0")
from gi.repository import Gegl, Gimp, Gio


def color(r, g, b, a=1.0):
    c = Gegl.Color.new("black")
    c.set_rgba(r, g, b, a)
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
    for name in ("include-exif", "include-iptc", "include-xmp", "include-color-profile",
                 "include-thumbnail", "include-comment", "time", "bkgd", "offs", "phys",
                 "interlaced"):
        if name in names:
            cfg.set_property(name, False)
    if "compression" in names:
        cfg.set_property("compression", 9)
    result = proc.run(cfg)
    if result.index(0) != Gimp.PDBStatusType.SUCCESS:
        raise RuntimeError("PNG export failed for " + path)
    return path
