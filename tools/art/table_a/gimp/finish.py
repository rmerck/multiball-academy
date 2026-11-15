# Table A finishing pass in GIMP 3.2 (headless): playfield insert art, the Altitude ladder, shot arrows,
# slingshot plastics and nameplate decals. Every piece is built from layers by this script, saved as a layered
# .xcf (tools/art/table_a/gimp/xcf/) and exported flattened to tools/art/table_a/out/ as a PNG holding IHDR,
# IDAT and IEND only. Fonts: the OFL fonts in tools/art/sources/fonts (font-path in the gimprc below).
#
#   R=$PWD; printf '(font-path "%s")\n' "$R/tools/art/sources/fonts" > /tmp/table_a.gimprc
#   GIMP3_DIRECTORY=$(mktemp -d) gimp-console-3.2 -i -n --gimprc=/tmp/table_a.gimprc \
#       --batch-interpreter=python-fu-eval -b "TABLE_A_ROOT='$R'; exec(open('$R/tools/art/table_a/gimp/finish.py').read())" --quit
#
# Scale: 4 pixels per playfield unit. Inserts and arrows are drawn in greys, so the game tints them (lit and
# unlit) as it does insert_round.png and insert_arrow.png; plastics and decals carry their own colours.
import json
import math
import os
import struct
import zlib

import gi

gi.require_version("Gimp", "3.0")
gi.require_version("Gegl", "0.4")
from gi.repository import Gegl, Gimp, Gio  # noqa: E402

ROOT = globals().get("TABLE_A_ROOT") or os.environ["TABLE_A_ROOT"]
TA = os.path.join(ROOT, "tools", "art", "table_a")
OUT = os.path.join(TA, "out")
XCF = os.path.join(TA, "gimp", "xcf")
PPU = 4

INK = "#0A0920"
CREAM = "#FFF4D6"
PAL = {"VIOLET": "#6A35C9", "ORCHID": "#B98CFF", "TEAL": "#00A7A5", "AQUA": "#63E6D8", "PINK": "#FF2E88",
       "ORANGE": "#FF6B1A", "YELLOW": "#FFD23C", "DAY": "#6FB4FF", "NIGHTV": "#9A6BFF", "CORAL": "#FF5A4E",
       "NIGHT": "#12123A", "DECK_B": "#1A2152"}

Gimp.context_push()
Gimp.context_set_antialias(True)
Gimp.context_set_feather(False)
Gimp.context_set_opacity(100.0)
Gimp.context_set_paint_mode(Gimp.LayerMode.NORMAL)


# ---- helpers ------------------------------------------------------------------------------------------
def colour(hexstr):
    return Gegl.Color.new(hexstr)


def new_image(w, h):
    img = Gimp.Image.new(w, h, Gimp.ImageBaseType.RGB)
    return img


def layer(img, name, opacity=100.0, mode=Gimp.LayerMode.NORMAL):
    lyr = Gimp.Layer.new(img, name, img.get_width(), img.get_height(), Gimp.ImageType.RGBA_IMAGE, opacity, mode)
    img.insert_layer(lyr, None, 0)
    lyr.fill(Gimp.FillType.TRANSPARENT)
    return lyr


def fill_sel(lyr, hexstr):
    # an empty selection would fill the whole layer: refuse instead
    if Gimp.Selection.is_empty(lyr.get_image()):
        raise RuntimeError("empty selection while filling " + lyr.get_name())
    Gimp.context_set_foreground(colour(hexstr))
    lyr.edit_fill(Gimp.FillType.FOREGROUND)


def gradient_sel(lyr, c0, c1, x0, y0, x1, y1, kind=Gimp.GradientType.LINEAR):
    Gimp.context_set_foreground(colour(c0))
    Gimp.context_set_background(colour(c1))
    Gimp.context_set_gradient_fg_bg_rgb()
    lyr.edit_gradient_fill(kind, 0.0, False, 1, 0.0, False, x0, y0, x1, y1)


def none(img):
    Gimp.Selection.none(img)


def poly(img, pts, op=Gimp.ChannelOps.REPLACE):
    flat = []
    for x, y in pts:
        flat += [float(x), float(y)]
    img.select_polygon(op, flat)


def ring(img, select_fn, width):
    """Select the band 'width' pixels wide just inside the current outline made by select_fn()."""
    select_fn(Gimp.ChannelOps.REPLACE)
    inner = Gimp.Selection.save(img)
    Gimp.Selection.shrink(img, width)
    shrunk = Gimp.Selection.save(img)
    img.select_item(Gimp.ChannelOps.REPLACE, inner)
    img.select_item(Gimp.ChannelOps.SUBTRACT, shrunk)
    img.remove_channel(inner)
    img.remove_channel(shrunk)


def text_layer(img, txt, font, size_px, hexstr, cx, cy, name=None, max_w=None):
    f = Gimp.Font.get_by_name(font)
    t = Gimp.TextLayer.new(img, txt, f, size_px, Gimp.Unit.pixel())
    img.insert_layer(t, None, 0)
    t.set_color(colour(hexstr))
    t.set_justification(Gimp.TextJustification.CENTER)
    t.set_antialias(True)
    try:
        t.set_hint_style(Gimp.TextHintStyle.NONE)
    except AttributeError:
        pass
    if max_w and t.get_width() > max_w:
        t.set_font_size(size_px * max_w / t.get_width(), Gimp.Unit.pixel())
    t.set_offsets(int(round(cx - t.get_width() / 2)), int(round(cy - t.get_height() / 2)))
    if name:
        t.set_name(name)
    return t


def shadow_of(img, src, dx, dy, hexstr, name):
    """A flat drop shadow: a copy of src's alpha in one colour, offset (the 90s chunky-shadow lettering)."""
    lyr = layer(img, name)
    img.select_item(Gimp.ChannelOps.REPLACE, src)
    fill_sel(lyr, hexstr)
    none(img)
    lyr.set_offsets(dx, dy)
    lyr.resize_to_image_size()
    img.reorder_item(lyr, None, img.get_item_position(src) + 1)
    return lyr


def blur(lyr, radius):
    f = Gimp.DrawableFilter.new(lyr, "gegl:gaussian-blur", "blur")
    cfg = f.get_config()
    cfg.set_property("std-dev-x", float(radius))
    cfg.set_property("std-dev-y", float(radius))
    lyr.merge_filter(f)


def strip_png(path):
    """Keep IHDR, IDAT and IEND only (no gamma, profile, time, text or EXIF)."""
    data = open(path, "rb").read()
    pos, keep = 8, []
    while pos < len(data):
        n, typ = struct.unpack(">I4s", data[pos:pos + 8])
        if typ in (b"IHDR", b"IDAT", b"IEND"):
            keep.append(data[pos:pos + 12 + n])
        pos += 12 + n
    # re-pack the image data as one IDAT so the bytes do not depend on the encoder's chunking
    ihdr = keep[0]
    idat = zlib.decompress(b"".join(c[8:-4] for c in keep if c[4:8] == b"IDAT"))
    body = zlib.compress(idat, 9)
    idat_chunk = struct.pack(">I", len(body)) + b"IDAT" + body + struct.pack(">I", zlib.crc32(b"IDAT" + body) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(data[:8] + ihdr + idat_chunk + keep[-1])


def save(img, name):
    os.makedirs(XCF, exist_ok=True)
    os.makedirs(OUT, exist_ok=True)
    Gimp.file_save(Gimp.RunMode.NONINTERACTIVE, img, Gio.File.new_for_path(os.path.join(XCF, name + ".xcf")), None)
    flat = img.duplicate()
    flat.merge_visible_layers(Gimp.MergeType.CLIP_TO_IMAGE)
    png = os.path.join(OUT, name + ".png")
    Gimp.file_save(Gimp.RunMode.NONINTERACTIVE, flat, Gio.File.new_for_path(png), None)
    flat.delete()
    strip_png(png)


SHEETS = {}


def sheet(name, cell_units, cols, frames, anchor="centre", **extra):
    SHEETS[name] = dict({"file": name + ".png", "cellUnits": list(cell_units),
                         "cellPixels": [cell_units[0] * PPU, cell_units[1] * PPU], "columns": cols,
                         "anchor": anchor, "frames": frames}, **extra)


def lens(img, cx, cy, select_fn, name, bezel=6):
    """A greyscale insert lens: ink bezel, a light grey dome-lit body and a soft highlight (the game tints it)."""
    bz = layer(img, name + " bezel")
    select_fn(Gimp.ChannelOps.REPLACE)
    fill_sel(bz, INK)
    body = layer(img, name + " lens")
    select_fn(Gimp.ChannelOps.REPLACE)
    Gimp.Selection.shrink(img, bezel)
    x0, y0, x1, y1 = Gimp.Selection.bounds(img)[-4:]
    gradient_sel(body, "#FFFFFF", "#9A9AA8", x0, y0, x1, y1)
    hi = layer(img, name + " highlight", 70.0)
    select_fn(Gimp.ChannelOps.REPLACE)
    Gimp.Selection.shrink(img, bezel + 4)
    img.select_ellipse(Gimp.ChannelOps.INTERSECT, x0, y0, (x1 - x0) * 0.75, (y1 - y0) * 0.55)
    fill_sel(hi, "#FFFFFF")
    none(img)
    blur(hi, 3)
    return body


# ---- F-L-A-P-S: five rollover inserts, a shield lens each with its letter ------------------------------
def make_flaps():
    cu, letters = 32, "FLAPS"
    c = cu * PPU
    img = new_image(c * len(letters), c)
    for i, ch in enumerate(letters):
        ox, cx, cy = i * c, i * c + c / 2, c / 2
        w, h = 22 * PPU, 24 * PPU

        def shield(op, ox=ox, cx=cx):
            top, bot = cy - h / 2, cy + h / 2
            pts = [(cx - w / 2, top + 10), (cx - w / 2 + 10, top), (cx + w / 2 - 10, top), (cx + w / 2, top + 10),
                   (cx + w / 2, cy + 6), (cx, bot), (cx - w / 2, cy + 6)]
            poly(img, pts, op)

        lens(img, cx, cy, shield, ch)
        t = text_layer(img, ch, "Russo One Regular", 13 * PPU, INK, cx, cy - 4, "letter " + ch)
        _ = (ox, t)
        none(img)
    save(img, "inserts_flaps")
    sheet("inserts_flaps", (cu, cu), len(letters), ["F", "L", "A", "P", "S"],
          note="greyscale lenses, tinted by the game; letter in ink")


# ---- Altitude ladder: five rungs (FL100..FL500), wing-shaped, pointing up ---------------------------------
def make_altitude():
    cw, ch = 48, 16
    W, H = cw * PPU, ch * PPU
    labels = ["FL100", "FL200", "FL300", "FL400", "FL500"]
    img = new_image(W * len(labels), H)
    for i, lab in enumerate(labels):
        cx, cy = i * W + W / 2, H / 2
        hw, hh = 22 * PPU, 6 * PPU

        def wing(op, cx=cx):
            pts = [(cx - hw, cy + hh), (cx - hw + 6 * PPU, cy - hh), (cx - 2 * PPU, cy - hh), (cx, cy - hh - 3 * PPU),
                   (cx + 2 * PPU, cy - hh), (cx + hw - 6 * PPU, cy - hh), (cx + hw, cy + hh)]
            poly(img, pts, op)

        lens(img, cx, cy, wing, lab, bezel=5)
        text_layer(img, lab, "Russo One Regular", 7 * PPU, INK, cx, cy + 3, "label " + lab, max_w=30 * PPU)
        none(img)
    save(img, "inserts_altitude")
    sheet("inserts_altitude", (cw, ch), len(labels), labels,
          note="greyscale lenses; GI colour per rung: DAY, CLEAR, DUSK, SUNSET, NIGHTV (STYLE.md)")
    # the altimeter: a cockpit-style tape the five rungs sit in. Frames: base, then the pointer at each level.
    make_altimeter()


ALT_PLATE_UNITS = (64, 112)
ALT_SOCKET = (46, 14)          # each rung socket, units; the rung lens (inserts_altitude) is 44 x 12 in it
ALT_SOCKET_X = 38.0            # socket centre from the plate's left edge, units
ALT_SOCKET_Y = [92.0 - 18.0 * k for k in range(5)]   # FL100 (bottom) .. FL500, from the plate's top edge


def make_altimeter():
    import random
    pw, ph = ALT_PLATE_UNITS[0] * PPU, ALT_PLATE_UNITS[1] * PPU
    labels = ["base", "fl100", "fl200", "fl300", "fl400", "fl500"]
    img = new_image(pw * len(labels), ph)
    for i, lab in enumerate(labels):
        ox = i * pw
        if lab != "base":
            # pointer frames are overlays: only the pointer, drawn over the plate and its lit rungs
            k = int(lab[2]) - 1
            yc = ALT_SOCKET_Y[k] * PPU
            ptr = layer(img, lab + " pointer")
            poly(img, [(ox + 14, yc - 13), (ox + 40, yc), (ox + 14, yc + 13)])
            fill_sel(ptr, "#B8C4DC")
            shadow_of(img, ptr, 3, 3, INK, lab + " pointer shadow")
            hi = layer(img, lab + " pointer light", 70.0)
            poly(img, [(ox + 16, yc - 10), (ox + 34, yc - 1), (ox + 16, yc - 2)])
            fill_sel(hi, "#FFFFFF")
            none(img)
            continue

        def plate(op, ox=ox):
            img.select_round_rectangle(op, ox + 2, 2, pw - 4, ph - 4, 28, 28)

        body = layer(img, lab + " plate")
        plate(Gimp.ChannelOps.REPLACE)
        gradient_sel(body, "#161C48", "#0C1032", ox, 0, ox, ph)
        # brushed texture: fixed-seed horizontal hairlines, faint
        brush = layer(img, lab + " brushed", 45.0)
        rnd = random.Random(1994)
        none(img)
        for y in range(8, ph - 8, 3):
            if rnd.random() < 0.55:
                x0 = ox + 10 + rnd.random() * 30
                img.select_rectangle(Gimp.ChannelOps.ADD, x0, y, pw - 20 - (x0 - ox) - rnd.random() * 30, 1)
        plate(Gimp.ChannelOps.INTERSECT)
        fill_sel(brush, "#222C66")
        bezel = layer(img, lab + " bezel")
        ring(img, plate, 9)
        gradient_sel(bezel, "#4A5470", "#1C2238", ox, 0, ox + pw, ph)
        inner = layer(img, lab + " bezel ink")
        ring(img, lambda op, ox=ox: img.select_round_rectangle(op, ox + 11, 11, pw - 22, ph - 22, 20, 20), 3)
        fill_sel(inner, INK)
        # tick scale up the left side: a major tick at each rung, minors between
        ticks = layer(img, lab + " ticks")
        none(img)
        for k in range(5):
            yc = ALT_SOCKET_Y[k] * PPU
            img.select_rectangle(Gimp.ChannelOps.ADD, ox + 16, yc - 2, 18, 4)
            if k < 4:
                for m in (1, 2):
                    ym = yc - m * 18 * PPU / 3
                    img.select_rectangle(Gimp.ChannelOps.ADD, ox + 22, ym - 1, 10, 2)
        fill_sel(ticks, "#4A5684")
        # ladder rails and the five rung sockets
        rails = layer(img, lab + " rails")
        sx = ox + ALT_SOCKET_X * PPU
        sw, sh_ = ALT_SOCKET[0] * PPU, ALT_SOCKET[1] * PPU
        img.select_rectangle(Gimp.ChannelOps.REPLACE, sx - sw / 2 - 6, ALT_SOCKET_Y[4] * PPU, 4, (ALT_SOCKET_Y[0] - ALT_SOCKET_Y[4]) * PPU)
        img.select_rectangle(Gimp.ChannelOps.ADD, sx + sw / 2 + 2, ALT_SOCKET_Y[4] * PPU, 4, (ALT_SOCKET_Y[0] - ALT_SOCKET_Y[4]) * PPU)
        fill_sel(rails, "#2E3866")
        for k in range(5):
            yc = ALT_SOCKET_Y[k] * PPU
            sock = layer(img, "%s socket %d" % (lab, k + 1))
            img.select_round_rectangle(Gimp.ChannelOps.REPLACE, sx - sw / 2, yc - sh_ / 2, sw, sh_, 12, 12)
            fill_sel(sock, "#05061A")
            lip = layer(img, "%s socket %d lip" % (lab, k + 1), 80.0)
            img.select_round_rectangle(Gimp.ChannelOps.REPLACE, sx - sw / 2, yc + sh_ / 2 - 3, sw, 3, 2, 2)
            fill_sel(lip, "#3A4470")
        none(img)
        title = text_layer(img, "ALTITUDE", "Saira Stencil One Regular", 6 * PPU, "#3A4470", pw / 2 + ox,
                           ph - 6.5 * PPU, lab + " title")
        _ = title
    save(img, "altitude_plate")
    sheet("altitude_plate", ALT_PLATE_UNITS, len(labels), labels,
          sockets={"centreXUnits": ALT_SOCKET_X, "centreYUnits": ALT_SOCKET_Y, "sizeUnits": list(ALT_SOCKET)},
          note="printed floor art (deck-dark): an altimeter tape whose five sockets hold inserts_altitude rungs "
               "(FL100 bottom); frames fl<N>00 are overlays holding only the pointer at that level")


# ---- shot arrows: chunky arrow (28 long), Mission arrow (34), progress pip (10) ----------------------------
def make_arrows():
    cu = 48
    c = cu * PPU
    img = new_image(c * 3, c)
    for i, (length, name) in enumerate(((28, "arrow"), (34, "arrow_mission"))):
        cx, cy = i * c + c / 2, c / 2
        L = length * PPU
        w = L * 0.62

        def arrow(op, cx=cx, L=L, w=w):
            top, bot = cy - L / 2, cy + L / 2
            neck = top + L * 0.46
            sw = w * 0.30
            notch = bot - L * 0.14
            pts = [(cx, top), (cx + w / 2, neck), (cx + sw, neck), (cx + sw, bot), (cx, notch), (cx - sw, bot),
                   (cx - sw, neck), (cx - w / 2, neck)]
            poly(img, pts, op)

        lens(img, cx, cy, arrow, name)
        # a speed stripe across the shaft, printed in ink (90s graphic accent)
        st = layer(img, name + " stripe", 55.0)
        arrow(Gimp.ChannelOps.REPLACE)
        Gimp.Selection.shrink(img, 8)
        img.select_rectangle(Gimp.ChannelOps.INTERSECT, cx - w * 0.5, cy + L * 0.10, w, 2 * PPU)
        fill_sel(st, INK)
        none(img)
    cx, cy = 2 * c + c / 2, c / 2
    r = 5 * PPU
    lens(img, cx, cy, lambda op: img.select_ellipse(op, cx - r, cy - r, 2 * r, 2 * r), "pip", bezel=5)
    none(img)
    save(img, "inserts_arrows")
    sheet("inserts_arrows", (cu, cu), 3, ["arrow", "arrow_mission", "pip"],
          note="greyscale lenses pointing up (rotate to the shot); arrow 28 long, Mission 34, pip 10")


# ---- slingshot plastics: the sling triangle inset 7 units, violet-to-pink plastic, wings and halftone ----
def make_plastics():
    tri = [(146.0, 668.0), (129.0, 722.0), (211.0, 776.0)]   # layout slingshots[0]
    (ax, ay), (bx, by), (cx_, cy_) = tri
    la = math.hypot(bx - cx_, by - cy_)
    lb = math.hypot(ax - cx_, ay - cy_)
    lc = math.hypot(ax - bx, ay - by)
    ix = (la * ax + lb * bx + lc * cx_) / (la + lb + lc)
    iy = (la * ay + lb * by + lc * cy_) / (la + lb + lc)
    s2 = (la + lb + lc) / 2
    area = math.sqrt(s2 * (s2 - la) * (s2 - lb) * (s2 - lc))
    rin = area / s2
    k = (rin - 7.0) / rin
    inset = [(ix + (x - ix) * k, iy + (y - iy) * k) for x, y in tri]
    x0, y0 = 120.0, 656.0           # anchor (playfield units) of the left plastic's cell
    cw, ch = 100, 128
    W, H = cw * PPU, ch * PPU
    P = [((x - x0) * PPU, (y - y0) * PPU) for x, y in inset]
    img = new_image(W, H)

    def shape(op):
        poly(img, P, op)

    xs = [p[0] for p in P]
    ys = [p[1] for p in P]
    shadow = layer(img, "cast shadow", 55.0)
    poly(img, [(x + 14, y + 20) for x, y in P])
    fill_sel(shadow, INK)
    none(img)
    blur(shadow, 6)
    body = layer(img, "plastic")
    shape(Gimp.ChannelOps.REPLACE)
    gradient_sel(body, PAL["VIOLET"], PAL["PINK"], min(xs), min(ys), max(xs), max(ys))
    dots = layer(img, "halftone", 60.0)
    none(img)
    step = 16
    for yy in range(int(min(ys)), int(max(ys)), step):
        for xx in range(int(min(xs)), int(max(xs)), step):
            t = (yy - min(ys)) / (max(ys) - min(ys))
            r = 1.0 + 5.0 * t
            off = step / 2 if (yy // step) % 2 else 0
            img.select_ellipse(Gimp.ChannelOps.ADD, xx + off - r, yy - r, 2 * r, 2 * r)
    shape(Gimp.ChannelOps.INTERSECT)
    fill_sel(dots, PAL["AQUA"])
    stripes = layer(img, "speed stripes")
    shape(Gimp.ChannelOps.REPLACE)
    Gimp.Selection.shrink(img, 10)
    inside = Gimp.Selection.save(img)
    for j, col in enumerate((PAL["YELLOW"], PAL["AQUA"], CREAM)):
        o = j * 30
        poly(img, [(min(xs) + 40 + o, max(ys)), (min(xs) + 58 + o, max(ys)), (min(xs) + 150 + o, min(ys) + 60),
                   (min(xs) + 132 + o, min(ys) + 60)])
        img.select_item(Gimp.ChannelOps.INTERSECT, inside)
        if not Gimp.Selection.is_empty(img):
            fill_sel(stripes, col)
    img.remove_channel(inside)
    none(img)
    # academy wings: two swept chevrons (original emblem; no roundel, star or insignia)
    wx, wy = ix * 1.0, iy
    wx, wy = (wx - x0) * PPU, (wy - y0) * PPU - 6
    wings = layer(img, "wings")
    for side in (-1, 1):
        poly(img, [(wx, wy), (wx + side * 70, wy - 34), (wx + side * 92, wy - 30), (wx + side * 30, wy + 4),
                   (wx + side * 60, wy - 6), (wx + side * 12, wy + 22)], Gimp.ChannelOps.ADD)
    img.select_ellipse(Gimp.ChannelOps.ADD, wx - 12, wy - 6, 24, 24)
    emblem = Gimp.Selection.save(img)
    shape(Gimp.ChannelOps.REPLACE)
    Gimp.Selection.shrink(img, 12)
    img.select_item(Gimp.ChannelOps.INTERSECT, emblem)
    img.remove_channel(emblem)
    fill_sel(wings, CREAM)
    none(img)
    shadow_of(img, wings, 5, 6, INK, "wings shadow")
    edge = layer(img, "polished edge")
    ring(img, shape, 7)
    fill_sel(edge, "#FFD0F0")
    hi = layer(img, "edge light", 80.0)
    ring(img, shape, 3)
    img.select_rectangle(Gimp.ChannelOps.INTERSECT, 0, 0, W * 0.6, H * 0.6)
    fill_sel(hi, "#FFFFFF")
    none(img)
    save(img, "plastic_sling_left")
    img.flip(Gimp.OrientationType.HORIZONTAL)
    # the mirror's cast shadow must still fall down-right: rebuild it on the flipped image
    for lyr in img.get_layers():
        if lyr.get_name() == "cast shadow":
            lyr.fill(Gimp.FillType.TRANSPARENT)
            poly(img, [(W - x + 14, y + 20) for x, y in P])
            fill_sel(lyr, INK)
            none(img)
            blur(lyr, 6)
    save(img, "plastic_sling_right")
    sheet("plastic_sling_left", (cw, ch), 1, ["plastic"], anchor=[x0, y0], note="over slingshots[0], inset 7")
    sheet("plastic_sling_right", (cw, ch), 1, ["plastic"], anchor=[round(2 * 355.5 - x0 - cw, 1), y0],
          note="over slingshots[1], mirror of the left")


# ---- nameplate decals: chunky 90s lettering on a gradient plate ----------------------------------------------
def make_decals():
    cw, ch = 64, 18
    W, H = cw * PPU, ch * PPU
    names = [("HANGAR", "ORANGE"), ("MISSION", "YELLOW"), ("AFTERBURNER", "AQUA"), ("FLIGHT DECK", "PINK")]
    img = new_image(W, H * len(names))
    for i, (nm, acc) in enumerate(names):
        oy = i * H

        def plate(op, oy=oy):
            img.select_round_rectangle(op, 6, oy + 6, W - 12, H - 12, 22, 22)

        sh = layer(img, nm + " shadow", 60.0)
        img.select_round_rectangle(Gimp.ChannelOps.REPLACE, 12, oy + 12, W - 12, H - 12, 22, 22)
        fill_sel(sh, INK)
        none(img)
        blur(sh, 3)
        base = layer(img, nm + " plate")
        plate(Gimp.ChannelOps.REPLACE)
        gradient_sel(base, PAL["DAY"], PAL["VIOLET"], 0, oy, 0, oy + H)
        zz = layer(img, nm + " zigzag", 70.0)
        none(img)
        for k in range(0, W, 24):
            poly(img, [(k, oy + H - 18), (k + 12, oy + H - 30), (k + 24, oy + H - 18), (k + 24, oy + H - 12),
                       (k + 12, oy + H - 24), (k, oy + H - 12)], Gimp.ChannelOps.ADD)
        plate(Gimp.ChannelOps.INTERSECT)
        fill_sel(zz, PAL[acc])
        rim = layer(img, nm + " rim")
        ring(img, plate, 5)
        gradient_sel(rim, "#F4FAFF", "#2B3550", 0, oy, 0, oy + H)
        none(img)
        t = text_layer(img, nm, "Russo One Regular", 10 * PPU, CREAM, W / 2, oy + H / 2 - 2, nm + " text",
                       max_w=W - 56)
        t2 = shadow_of(img, t, 4, 4, PAL["PINK"] if acc != "PINK" else PAL["VIOLET"], nm + " hot shadow")
        shadow_of(img, t, 7, 7, INK, nm + " ink shadow")
        _ = t2
    save(img, "decals_nameplates")
    sheet("decals_nameplates", (cw, ch), 1, [n for n, _ in names], note="printed plates, own colours")


for fn in (make_flaps, make_altitude, make_arrows, make_plastics, make_decals):
    fn()
    print("table_a gimp:", fn.__name__, "done", flush=True)
with open(os.path.join(OUT, "gimp_sheets.json"), "w") as f:
    json.dump({"pixelsPerUnit": PPU, "note": "made by tools/art/table_a/gimp/finish.py", "sheets": SHEETS}, f,
              indent=1, sort_keys=True)
    f.write("\n")
Gimp.context_pop()
