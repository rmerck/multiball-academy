# Table A toys: pre-rendered sprite sheets (Blender, Cycles CPU, headless).
#
# Build every toy, save its .blend, render its frames and pack the sheet:
#   Blender --background --factory-startup --python tools/art/table_a/blender/toys.py -- [--toy NAME] [--samples N]
# Re-render from a committed .blend (the .blend alone holds every state as a keyframe):
#   Blender --background --factory-startup tools/art/table_a/blender/<toy>.blend \
#           --python tools/art/table_a/blender/toys.py -- --from-blend --toy <toy>
#
# Output: tools/art/table_a/out/<toy>.png (straight alpha, IHDR/IDAT/IEND only) and out/sheets.json.
# Scale: 4 pixels per playfield unit, as assets/sprites. View: straight down (orthographic), light from the
# upper left, contact shadows from a shadow catcher. Everything here is modelled by this script; the only
# outside inputs are the OFL fonts in tools/art/sources/fonts.
import json
import math
import os
import struct
import sys
import zlib

import bpy
from mathutils import Vector

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
TA = os.path.normpath(os.path.join(HERE, ".."))
ROOT = os.path.normpath(os.path.join(TA, "..", "..", ".."))
FONTS = os.path.join(ROOT, "tools", "art", "sources", "fonts")
OUT = os.path.join(TA, "out")
PPU = 4  # pixels per playfield unit

# ---- palette (tools/art/palette.py names; Table A additions in STYLE.md) ----------------------------
HEX = {
    "INK": "#0A0920", "NIGHT": "#12123A", "DECK_A": "#141A44", "DECK_B": "#1A2152", "DECK_HI": "#2C3A7C",
    "VIOLET": "#6A35C9", "ORCHID": "#B98CFF", "TEAL": "#00A7A5", "AQUA": "#63E6D8", "PINK": "#FF2E88",
    "ORANGE": "#FF6B1A", "YELLOW": "#FFD23C", "GREEN": "#2ED16B", "BLUE": "#2F8CFF", "CREAM": "#FFF4D6",
    "WHITE": "#FFFFFF", "CH_MID": "#9FB4CC",
    # Table A: the Altitude GI ladder and the academy trainer livery
    "DAY": "#6FB4FF", "CLEAR": "#F2F4FF", "DUSK": "#FFB050", "SUNSET": "#FF8A5C", "NIGHTV": "#9A6BFF",
    "CORAL": "#FF5A4E", "SLATE": "#2A3048",
}


def lin(hexstr, a=1.0):
    h = hexstr.lstrip("#")
    c = [int(h[i:i + 2], 16) / 255.0 for i in (0, 2, 4)]
    c = [x / 12.92 if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4 for x in c]
    return (c[0], c[1], c[2], a)


# ---- the sheets: frames, cell size (units), columns, and where the cell sits ------------------------
# "anchor": "centre" = the cell is centred on the toy's position; [x, y] = the cell's top-left corner in
# playfield units (pieces drawn in table coordinates, such as the ramps).
SHEETS = {
    "drone": {"cell": (48, 48), "cols": 5, "anchor": "centre",
              "frames": ["armour6", "armour5", "armour4", "armour3", "armour2", "armour1", "armour0",
                         "dive", "down1", "down2"]},
    "propwash": {"cell": (84, 84), "cols": 6, "anchor": "centre",
                 "frames": ["rot%03d" % (k * 5) for k in range(24)]},
    "pylon": {"cell": (64, 64), "cols": 4, "anchor": "centre", "frames": ["down", "rise1", "rise2", "up"]},
    "bay_door": {"cell": (40, 32), "cols": 3, "anchor": "centre", "frames": ["closed", "dropping", "open"]},
    "flight_deck": {"cell": (100, 172), "cols": 2, "anchor": [471.0, 146.0], "frames": ["off", "lit"]},
    "ramp_runway": {"cell": (176, 412), "cols": 1, "anchor": [40.0, 276.0], "frames": ["normal"]},
    "ramp_skyway": {"cell": (176, 452), "cols": 2, "anchor": [495.0, 236.0], "frames": ["normal", "to_deck"]},
    "gforce": {"cell": (88, 88), "cols": 1, "anchor": [128.0, 224.0], "frames": ["housing"]},
    "jet_caps": {"cell": (56, 56), "cols": 2, "anchor": "centre",
                 "frames": ["jet1_off", "jet1_on", "jet2_off", "jet2_on", "jet3_off", "jet3_on"]},
    "flasher_domes": {"cell": (28, 28), "cols": 10, "anchor": "centre",
                      "frames": ["%s_%s" % (n, s) for s in ("off", "on") for n in
                                 ("hangar", "mission", "afterburner", "runway", "skyway", "nest",
                                  "sling_left", "sling_right", "deck", "left_arch")]},
}
DOME_COLOURS = ["ORANGE", "YELLOW", "GREEN", "ORCHID", "PINK", "CREAM", "AQUA", "AQUA", "AQUA", "ORCHID"]
JET_RADII = [24.0, 20.0, 24.0]          # layout bumpers 0, 1, 2
JET_ACCENT = ["PINK", "AQUA", "YELLOW"]


# ---- PNG: decode Blender's 8-bit RGBA, write IHDR/IDAT/IEND only -----------------------------------
def tga_read(path):
    """Blender's uncompressed 32-bit Targa (straight alpha) as an RGBA array, top row first."""
    data = open(path, "rb").read()
    idlen, cmap, itype = data[0], data[1], data[2]
    assert cmap == 0 and itype == 2, (path, cmap, itype)
    w, h = struct.unpack("<HH", data[12:16])
    bpp, desc = data[16], data[17]
    assert bpp == 32, (path, bpp)
    px = np.frombuffer(data, dtype=np.uint8, count=w * h * 4, offset=18 + idlen).reshape(h, w, 4)
    px = px[:, :, [2, 1, 0, 3]]
    if not desc & 0x20:
        px = px[::-1]
    return np.ascontiguousarray(px)


def png_write(path, rgba):
    h, w, _ = rgba.shape
    raw = b"".join(b"\x00" + rgba[y].tobytes() for y in range(h))

    def chunk(t, b):
        return struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t + b) & 0xFFFFFFFF)

    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


# ---- scene helpers ---------------------------------------------------------------------------------
def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.preferences.edit.keyframe_new_interpolation_type = "CONSTANT"


def setup_scene(name, samples, cell, centre, shadow=True):
    sc = bpy.context.scene
    sc.name = name
    r = sc.render
    r.engine = "CYCLES"
    sc.cycles.device = "CPU"
    sc.cycles.samples = samples
    sc.cycles.seed = 7
    sc.cycles.use_animated_seed = False
    sc.cycles.use_adaptive_sampling = False
    sc.cycles.use_denoising = False
    sc.cycles.film_transparent_glass = True
    sc.cycles.max_bounces = 8
    sc.cycles.transmission_bounces = 8
    sc.cycles.pixel_filter_type = "BLACKMAN_HARRIS"
    r.film_transparent = True
    r.threads_mode = "FIXED"
    r.threads = 8
    r.resolution_x, r.resolution_y = int(cell[0] * PPU), int(cell[1] * PPU)
    r.resolution_percentage = 100
    r.image_settings.file_format = "TARGA_RAW"
    r.image_settings.color_mode = "RGBA"
    r.use_stamp = False
    sc.view_settings.view_transform = "Standard"
    sc.view_settings.look = "None"
    sc.display_settings.display_device = "sRGB"
    # world: a faint night-blue ambient so shade sides are never black
    w = bpy.data.worlds.new("World")
    sc.world = w
    w.use_nodes = True
    nt = w.node_tree
    bg = nt.nodes.get("Background")
    bg.inputs[1].default_value = 0.7
    tc = nt.nodes.new("ShaderNodeTexCoord")
    sep = nt.nodes.new("ShaderNodeSeparateXYZ")
    ramp = nt.nodes.new("ShaderNodeValToRGB")
    cr = ramp.color_ramp
    cr.elements[0].position, cr.elements[0].color = 0.0, lin("#0A0920")
    cr.elements[1].position, cr.elements[1].color = 1.0, lin("#9FB4CC")
    mid = cr.elements.new(0.55)
    mid.color = lin("#2C3A7C")
    nt.links.new(tc.outputs["Generated"], sep.inputs[0])
    nt.links.new(sep.outputs["Z"], ramp.inputs[0])
    nt.links.new(ramp.outputs["Color"], bg.inputs[0])
    # camera straight down, orthographic, framing the cell
    cam = bpy.data.objects.new("Camera", bpy.data.cameras.new("Camera"))
    sc.collection.objects.link(cam)
    cam.data.type = "ORTHO"
    cam.data.ortho_scale = float(max(cell))
    cam.data.clip_start, cam.data.clip_end = 1.0, 1000.0
    cam.location = (centre[0], centre[1], 400.0)
    sc.camera = cam
    # key: a sun from the upper left; fill: a weak sun from the lower right
    sun = bpy.data.objects.new("Key", bpy.data.lights.new("Key", "SUN"))
    sc.collection.objects.link(sun)
    sun.data.energy = 3.6
    sun.data.angle = math.radians(6)
    sun.rotation_euler = Vector((-1.0, 1.0, 1.7)).normalized().to_track_quat("Z", "Y").to_euler()
    fill = bpy.data.objects.new("Fill", bpy.data.lights.new("Fill", "SUN"))
    sc.collection.objects.link(fill)
    fill.data.energy = 0.7
    fill.data.angle = math.radians(30)
    fill.rotation_euler = Vector((1.0, -0.6, 1.2)).normalized().to_track_quat("Z", "Y").to_euler()
    # softbox: a bright card up and to the left, seen only in reflections (chrome needs something to show)
    bpy.ops.mesh.primitive_plane_add(size=1.0, location=(centre[0] - 300, centre[1] + 300, 420))
    box_ = bpy.context.active_object
    box_.name = "Softbox"
    box_.scale = (320.0, 120.0, 1.0)
    box_.rotation_euler = Vector((-1.0, 1.0, 1.4)).normalized().to_track_quat("Z", "Y").to_euler()
    sm = bpy.data.materials.new("Softbox")
    sm.use_nodes = True
    nt = sm.node_tree
    for n in list(nt.nodes):
        if n.type != "OUTPUT_MATERIAL":
            nt.nodes.remove(n)
    em = nt.nodes.new("ShaderNodeEmission")
    em.inputs[0].default_value = lin("#FFF4E6")
    em.inputs[1].default_value = 6.0
    nt.links.new(em.outputs[0], nt.nodes.get("Material Output").inputs[0])
    box_.data.materials.append(sm)
    box_.visible_camera = False
    box_.visible_shadow = False
    if shadow:
        bpy.ops.mesh.primitive_plane_add(size=2000, location=(centre[0], centre[1], 0.0))
        p = bpy.context.active_object
        p.name = "ShadowCatcher"
        p.is_shadow_catcher = True
    return sc


def mat(name, colour, metallic=0.0, rough=0.35, coat=0.0, trans=0.0, ior=1.45, emit=None, strength=0.0,
        alpha=1.0, spec=0.5):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes.get("Principled BSDF")
    b.inputs["Base Color"].default_value = lin(colour) if isinstance(colour, str) else colour
    b.inputs["Metallic"].default_value = metallic
    b.inputs["Roughness"].default_value = rough
    b.inputs["IOR"].default_value = ior
    b.inputs["Coat Weight"].default_value = coat
    b.inputs["Coat Roughness"].default_value = 0.05
    b.inputs["Transmission Weight"].default_value = trans
    b.inputs["Alpha"].default_value = alpha
    b.inputs["Specular IOR Level"].default_value = spec
    b.inputs["Emission Color"].default_value = lin(emit) if emit else (0, 0, 0, 1)
    b.inputs["Emission Strength"].default_value = strength
    return m


def chrome():
    return mat("Chrome", "#E8F0FA", metallic=1.0, rough=0.12)


def plastic(c, rough=0.18, coat=0.6):
    return mat("Plastic_" + c, HEX[c], rough=rough, coat=coat)


def clear_plastic(c, tint=0.35):
    col = lin(HEX[c])
    col = tuple(1.0 - tint + tint * x for x in col[:3]) + (1.0,)
    return mat("Clear_" + c, col, rough=0.04, trans=1.0, ior=1.49)


def assign(obj, m):
    obj.data.materials.clear()
    obj.data.materials.append(m)
    return obj


def smooth(obj, bevel=0.0, segs=3):
    if bevel > 0:
        md = obj.modifiers.new("Bevel", "BEVEL")
        md.width = bevel
        md.segments = segs
        md.limit_method = "ANGLE"
    for poly in obj.data.polygons:
        poly.use_smooth = True
    return obj


def cyl(r, depth, loc, verts=48, m=None, r2=None):
    if r2 is None:
        bpy.ops.mesh.primitive_cylinder_add(vertices=verts, radius=r, depth=depth, location=loc)
    else:
        bpy.ops.mesh.primitive_cone_add(vertices=verts, radius1=r, radius2=r2, depth=depth, location=loc)
    o = bpy.context.active_object
    if m:
        assign(o, m)
    return o


def box(size, loc, m=None, rot=(0, 0, 0)):
    bpy.ops.mesh.primitive_cube_add(size=1, location=loc, rotation=rot)
    o = bpy.context.active_object
    o.scale = size
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    if m:
        assign(o, m)
    return o


def sphere(r, loc, m=None, segs=32):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=segs, ring_count=segs // 2, radius=r, location=loc)
    o = bpy.context.active_object
    smooth(o)
    if m:
        assign(o, m)
    return o


def prism(points2d, z0, z1, m=None, name="Prism"):
    """A vertical prism over a convex or concave 2D outline (Blender XY)."""
    me = bpy.data.meshes.new(name)
    n = len(points2d)
    verts = [(x, y, z0) for x, y in points2d] + [(x, y, z1) for x, y in points2d]
    faces = [tuple(range(n - 1, -1, -1)), tuple(range(n, 2 * n))]
    faces += [(i, (i + 1) % n, n + (i + 1) % n, n + i) for i in range(n)]
    me.from_pydata(verts, [], faces)
    me.update()
    o = bpy.data.objects.new(name, me)
    bpy.context.scene.collection.objects.link(o)
    if m:
        assign(o, m)
    return o


def text(body, size, loc, m, font="RussoOne-Regular.ttf", rot=(0, 0, 0), extrude=0.0, align="CENTER"):
    cu = bpy.data.curves.new("T_" + body, "FONT")
    cu.body = body
    cu.font = bpy.data.fonts.load(os.path.join(FONTS, font), check_existing=True)
    cu.size = size
    cu.align_x = align
    cu.align_y = "CENTER"
    cu.extrude = extrude
    o = bpy.data.objects.new("T_" + body, cu)
    bpy.context.scene.collection.objects.link(o)
    o.location = loc
    o.rotation_euler = rot
    o.data.materials.append(m)
    return o


def group(objs, name):
    e = bpy.data.objects.new(name, None)
    bpy.context.scene.collection.objects.link(e)
    for o in objs:
        o.parent = e
    return e


def key(obj, path, frame, value, index=-1):
    if index >= 0:
        getattr(obj, path)[index] = value
        obj.keyframe_insert(path, index=index, frame=frame)
    else:
        setattr(obj, path, value)
        obj.keyframe_insert(path, frame=frame)


def key_show(obj, frame, show):
    obj.hide_render = not show
    obj.keyframe_insert("hide_render", frame=frame)
    for c in obj.children:
        key_show(c, frame, show)


def key_emit(m, frame, strength):
    sock = m.node_tree.nodes.get("Principled BSDF").inputs["Emission Strength"]
    sock.default_value = strength
    sock.keyframe_insert("default_value", frame=frame)


def T(x, y, z=0.0):
    """Playfield units (y down) to Blender (y up)."""
    return (x, -y, z)


# ---- toys --------------------------------------------------------------------------------------------
def build_drone():
    """BANDIT: an academy target drone. Hexagon, a corner pointing down the table (layout: radius 15)."""
    body_r, armour_r = 12.0, 15.0
    corners = [(body_r * math.cos(math.radians(-90 + 60 * k)), body_r * math.sin(math.radians(-90 + 60 * k)))
               for k in range(6)]
    # world: corner pointing down the table = -Y in Blender
    corners = [(x, y) for x, y in corners]
    parts = []
    hull = prism(corners, 3.0, 10.0, mat("DroneHull", HEX["CREAM"], rough=0.25, coat=0.8), "Hull")
    smooth(hull, 0.8)
    parts.append(hull)
    # top: six wedges, hot orange and cream, the trainer's target pattern
    for k in range(6):
        a0, a1 = math.radians(-90 + 60 * k), math.radians(-90 + 60 * (k + 1))
        tri = [(0, 0), (body_r * 0.92 * math.cos(a0), body_r * 0.92 * math.sin(a0)),
               (body_r * 0.92 * math.cos(a1), body_r * 0.92 * math.sin(a1))]
        w = prism(tri, 10.0, 10.6, plastic("ORANGE" if k % 2 == 0 else "INK", 0.2), "Wedge%d" % k)
        parts.append(w)
    # sensor eye
    eye_m = mat("Eye", HEX["PINK"], rough=0.05, coat=1.0, emit=HEX["PINK"], strength=2.5)
    eye = sphere(3.6, (0, 0, 10.4), eye_m)
    eye.scale = (1, 1, 0.6)
    parts.append(eye)
    parts.append(cyl(4.4, 0.8, (0, 0, 10.7), m=chrome()))
    # three ducted rotor pods on alternate corners
    for k in (1, 3, 5):
        a = math.radians(-90 + 60 * k)
        c = (13.0 * math.cos(a), 13.0 * math.sin(a), 8.0)
        parts.append(cyl(3.6, 2.4, c, m=chrome()))
        parts.append(cyl(2.6, 2.6, c, m=mat("Duct", HEX["INK"], rough=0.6)))
    root = group(parts, "Drone")
    # armour: six teal plates, one per face, bolted on
    plates = []
    pm = mat("Armour", HEX["TEAL"], metallic=0.6, rough=0.28, coat=0.5)
    for k in range(6):
        a0, a1 = math.radians(-90 + 60 * k), math.radians(-90 + 60 * (k + 1))
        ri, ro = body_r - 0.5, armour_r
        quad = [(ri * math.cos(a0), ri * math.sin(a0)), (ro * math.cos(a0 + 0.06), ro * math.sin(a0 + 0.06)),
                (ro * math.cos(a1 - 0.06), ro * math.sin(a1 - 0.06)), (ri * math.cos(a1), ri * math.sin(a1))]
        p = prism(quad, 2.5, 9.0, pm, "Plate%d" % k)
        smooth(p, 0.5)
        am = (a0 + a1) / 2
        bolt = sphere(0.7, ((ri + ro) / 2 * math.cos(am), (ri + ro) / 2 * math.sin(am), 9.1), chrome(), 12)
        bolt.parent = p
        p.parent = root
        plates.append(p)
    # removal order: front corners first, so the throw faces strip first
    order = [0, 5, 1, 4, 2, 3]
    frames = SHEETS["drone"]["frames"]
    for f, label in enumerate(frames, start=1):
        if label.startswith("armour"):
            n = int(label[-1])
            for i, k in enumerate(order):
                key_show(plates[k], f, (6 - i) <= n)
            key(root, "location", f, (0, 0, 0))
            key(root, "rotation_euler", f, (0, 0, 0))
            key_emit(eye_m, f, 2.5)
        else:
            for p in plates:
                key_show(p, f, False)
            if label == "dive":
                key(root, "location", f, (0, 0, 1.0))
                key(root, "rotation_euler", f, (math.radians(-24), 0, 0))
                key_emit(eye_m, f, 6.0)
            else:
                sink = 4.0 if label == "down1" else 8.0
                key(root, "location", f, (0, 0, -sink))
                key(root, "rotation_euler", f, (math.radians(-10), math.radians(14), 0))
                key_emit(eye_m, f, 0.0)


def build_propwash():
    """Prop Wash: a flush turntable, r 38, with an original three-blade prop hub."""
    R = 38.0
    rot = bpy.data.objects.new("Turntable", None)
    bpy.context.scene.collection.objects.link(rot)
    parts = []
    parts.append(cyl(R, 1.0, (0, 0, -0.5), 128, mat("Disc", HEX["NIGHT"], rough=0.3, coat=0.9)))
    ring = cyl(R, 1.2, (0, 0, -0.4), 128, chrome())
    inner = cyl(R - 2.2, 1.6, (0, 0, -0.4), 128, mat("Disc2", HEX["DECK_B"], rough=0.3, coat=0.9))
    parts += [ring, inner]
    # airflow rings: printed arcs that rotate with the disc
    for k, rr in enumerate((14.0, 22.0, 30.0)):
        bpy.ops.mesh.primitive_torus_add(major_radius=rr, minor_radius=0.55, major_segments=96, minor_segments=8,
                                         location=(0, 0, 0.42))
        t = bpy.context.active_object
        t.scale = (1, 1, 0.15)
        assign(t, plastic(("AQUA", "DAY", "ORCHID")[k], 0.3, 0.2))
        parts.append(t)
    # three blades: a swept outline, cream with a coral band and a yellow tip
    blade = [(0, -3.2), (12, -4.8), (24, -5.4), (31, -4.4), (34.5, -1.6), (34, 1.8), (30, 3.6), (20, 3.8),
             (10, 3.4), (0, 3.0)]
    for k in range(3):
        a = math.radians(90 + 120 * k)
        ca, sa = math.cos(a), math.sin(a)

        def rotp(p, ca=ca, sa=sa):
            return (p[0] * ca - p[1] * sa, p[0] * sa + p[1] * ca)

        pts = [rotp(p) for p in blade]
        b = prism(pts, 0.5, 2.2, plastic("CREAM", 0.25), "Blade%d" % k)
        smooth(b, 0.4)
        parts.append(b)
        band = [rotp(p) for p in [(22, -5.6), (26, -5.4), (26, 4.0), (22, 4.0)]]
        parts.append(prism(band, 2.15, 2.35, plastic("CORAL", 0.25), "Band%d" % k))
        tip = [rotp(p) for p in [(29, -4.8), (31, -4.5), (34.6, -1.6), (34.1, 1.9), (30, 3.7), (29, 3.7)]]
        parts.append(prism(tip, 2.15, 2.35, plastic("YELLOW", 0.25), "Tip%d" % k))
    # spinner hub
    hub = cyl(7.5, 7.0, (0, 0, 5.0), 64, chrome(), r2=1.2)
    smooth(hub)
    parts += [hub, cyl(8.4, 1.6, (0, 0, 1.8), 64, plastic("INK", 0.4, 0.3))]
    for p in parts:
        p.parent = rot
    for f in range(1, 25):
        key(rot, "rotation_euler", f, (0, 0, -math.radians(5 * (f - 1))))


def build_pylon():
    """Pylon pop-up (r 10): an air-race pylon that rises through a chrome bezel."""
    bezel = cyl(11.8, 0.6, (0, 0, 0.3), 64, chrome())
    pit = cyl(10.4, 0.7, (0, 0, 0.32), 64, mat("Pit", HEX["INK"], rough=0.8))
    parts = []
    H = 26.0
    body = cyl(10.0, H, (0, 0, H / 2), 64, plastic("CORAL", 0.2), r2=7.0)
    smooth(body)
    parts.append(body)
    for z in (6.0, 14.0):
        rb = 10.0 - 3.0 * z / H
        band = cyl(rb + 0.08, 3.0, (0, 0, z), 64, plastic("WHITE", 0.2), r2=rb - 0.35 + 0.08)
        parts.append(band)
    # chequered cap
    for k in range(8):
        a0, a1 = math.radians(45 * k), math.radians(45 * (k + 1))
        tri = [(0, 0), (6.8 * math.cos(a0), 6.8 * math.sin(a0)), (6.8 * math.cos(a1), 6.8 * math.sin(a1))]
        parts.append(prism(tri, H, H + 0.6, plastic("WHITE" if k % 2 else "INK", 0.25), "Cheq%d" % k))
    parts.append(cyl(1.6, 1.4, (0, 0, H + 0.6), 24, chrome()))
    root = group(parts, "Pylon")
    tops = {"down": 0.6, "rise1": 9.0, "rise2": 17.5, "up": H + 0.6}
    for f, label in enumerate(SHEETS["pylon"]["frames"], start=1):
        key(root, "location", f, (0, 0, tops[label] - (H + 0.6)))
    _ = (bezel, pit)


def build_bay_door():
    """One Hangar bay door (drop target, 13.5 + 2 x 5 wide): ribbed hangar shutter, light grey so the game can
    tint it (as door.png)."""
    W, D, H = 23.0, 7.0, 16.0
    slot = box((W + 2.4, D + 2.4, 0.6), (0, 0, 0.3), mat("Slot", HEX["INK"], rough=0.8))
    rim = box((W + 3.6, D + 3.6, 0.5), (0, 0, 0.2), chrome())
    parts = []
    face = box((W, D, H), (0, 0, H / 2), mat("Door", "#D8DEE8", rough=0.3, coat=0.6))
    smooth(face, 0.6)
    parts.append(face)
    for k in range(5):
        x = -W / 2 + 2.6 + k * (W - 5.2) / 4
        parts.append(box((1.0, D + 0.3, 0.7), (x, 0, H + 0.1), mat("Rib", "#8C96AA", rough=0.4)))
    parts.append(box((W - 3, 1.2, 0.5), (0, -D / 2 + 1.0, H + 0.2), plastic("ORANGE", 0.3)))
    root = group(parts, "Door")
    zs = {"closed": 0.0, "dropping": -8.0, "open": -H + 0.4}
    for f, label in enumerate(SHEETS["bay_door"]["frames"], start=1):
        key(root, "location", f, (0, 0, zs[label]))
    _ = (slot, rim)


def build_flight_deck():
    """The Flight Deck floor (centre 521,232, 90 x 160): a carrier-style deck plate, with the CAT SHOT and deck
    drain recesses, landing lines, catapult track and edge lights (lit frame = the deck is open)."""
    cx, cy = 521.0, 232.0
    hw, hh = 45.0, 80.0
    # plate with rounded corners
    pts = []
    rc = 8.0
    for qx, qy, a0 in ((hw - rc, hh - rc, 0), (-hw + rc, hh - rc, 90), (-hw + rc, -hh + rc, 180),
                       (hw - rc, -hh + rc, 270)):
        for s in range(9):
            a = math.radians(a0 + s * 90 / 8)
            pts.append((cx + qx + rc * math.cos(a), -(cy) + qy + rc * math.sin(a)))
    deck_m = mat("DeckPlate", HEX["SLATE"], rough=0.75)
    nodes = deck_m.node_tree.nodes
    noise = nodes.new("ShaderNodeTexNoise")
    noise.inputs["Scale"].default_value = 0.9
    noise.inputs["Detail"].default_value = 6.0
    bump = nodes.new("ShaderNodeBump")
    bump.inputs["Strength"].default_value = 0.25
    deck_m.node_tree.links.new(noise.outputs["Fac"], bump.inputs["Height"])
    deck_m.node_tree.links.new(bump.outputs["Normal"], nodes.get("Principled BSDF").inputs["Normal"])
    plate = prism(pts, -1.0, 0.0, deck_m, "Deck")
    edge = prism(pts, -1.2, -0.05, chrome(), "Edge")
    edge.scale = (1.0, 1.0, 1.0)
    paint_w = plastic("CLEAR", 0.5, 0.0)
    paint_y = plastic("DUSK", 0.5, 0.0)

    def stripe(x0, y0, x1, y1, w, m, z=0.05):
        dx, dy = x1 - x0, y1 - y0
        L = math.hypot(dx, dy)
        o = box((L, w, 0.1), ((x0 + x1) / 2, -(y0 + y1) / 2, z), m, rot=(0, 0, math.atan2(-dy, dx)))
        return o

    # angled landing area: two yellow edge lines and a dashed white centreline, set down -> CAT SHOT
    sx, sy, ex, ey = 532.0, 300.0, 495.0, 160.0
    ang = math.atan2(ey - sy, ex - sx)
    nx, ny = -math.sin(ang), math.cos(ang)
    for side in (-1, 1):
        stripe(sx + side * 16 * nx, sy + side * 16 * ny, ex + side * 16 * nx, ey + side * 16 * ny, 1.4, paint_y)
    L = math.hypot(ex - sx, ey - sy)
    for k in range(9):
        t0, t1 = k / 9.0, k / 9.0 + 0.055
        stripe(sx + (ex - sx) * t0, sy + (ey - sy) * t0, sx + (ex - sx) * t1, sy + (ey - sy) * t1, 1.6, paint_w)
    # catapult track: chrome slot from the set-down towards the CAT SHOT
    stripe(532.0, 252.0, 498.0, 212.0, 2.4, chrome(), 0.08)
    stripe(532.0, 252.0, 498.0, 212.0, 1.0, mat("TrackSlot", HEX["INK"], rough=0.9), 0.12)
    # recesses: CAT SHOT (495,196, 14x16) and deck drain (495,292, 14x12)
    for (hx, hy, ax, ay) in ((495.0, 196.0, 14.0, 16.0), (495.0, 292.0, 14.0, 12.0)):
        box((2 * ax + 3, 2 * ay + 3, 0.3), (hx, -hy, 0.05), chrome())
        box((2 * ax, 2 * ay, 0.4), (hx, -hy, 0.1), mat("Recess", HEX["INK"], rough=0.95))
    # hazard chevrons at the drain's lip
    for k in range(4):
        x = 483.0 + k * 8
        stripe(x, 306.0, x + 4, 309.0, 1.2, paint_y)
    # deck-edge lights: 14 small lenses along both long edges (emit in the lit frame)
    lens_m = mat("EdgeLens", HEX["AQUA"], rough=0.1, coat=1.0, emit=HEX["AQUA"], strength=0.0)
    for k in range(7):
        y = cy - hh + 14 + k * (2 * hh - 28) / 6
        for x in (cx - hw + 4.5, cx + hw - 4.5):
            sphere(1.5, (x, -y, 0.2), lens_m, 16).scale = (1, 1, 0.5)
    # stencil lettering, low contrast (decoration only)
    text("FLIGHT DECK", 7.0, (cx - hw + 11, -cy, 0.06), mat("Stencil", "#4A5378", rough=0.6),
         font="SairaStencilOne-Regular.ttf", rot=(0, 0, math.radians(90)))
    frames = SHEETS["flight_deck"]["frames"]
    for f, label in enumerate(frames, start=1):
        key_emit(lens_m, f, 6.0 if label == "lit" else 0.0)
    _ = (plate, ang, L)


def _offset(pts, d):
    out = []
    for i, (x, y, z) in enumerate(pts):
        a = pts[max(i - 1, 0)]
        b = pts[min(i + 1, len(pts) - 1)]
        tx, ty = b[0] - a[0], b[1] - a[1]
        n = math.hypot(tx, ty) or 1.0
        out.append((x - ty / n * d, y + tx / n * d, z))
    return out


def _curve(name, pts, m, bevel=0.0, profile=None):
    cu = bpy.data.curves.new(name, "CURVE")
    cu.dimensions = "3D"
    cu.twist_mode = "Z_UP"
    sp = cu.splines.new("POLY")
    sp.points.add(len(pts) - 1)
    for p, (x, y, z) in zip(sp.points, pts):
        p.co = (x, y, z, 1.0)
    if profile is not None:
        cu.bevel_mode = "OBJECT"
        cu.bevel_object = profile
        cu.use_fill_caps = True
    else:
        cu.bevel_depth = bevel
        cu.bevel_resolution = 4
        cu.use_fill_caps = True
    o = bpy.data.objects.new(name, cu)
    bpy.context.scene.collection.objects.link(o)
    o.data.materials.append(m)
    return o


def _densify(pts, step=6.0):
    out = []
    for (a, b) in zip(pts[:-1], pts[1:]):
        L = math.hypot(b[0] - a[0], b[1] - a[1])
        n = max(1, int(L / step))
        for k in range(n):
            t = k / n
            out.append((a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t))
    out.append(pts[-1])
    return out


def _ramp(rail, name, colour, label, plastic_until=4, height=34.0, decal_side=1):
    """A ramp along a layout rail (playfield units): clear plastic channel from the mouth to the apex, then
    a two-wire chrome rail down to the inlane, on chrome posts."""
    pts = _densify(rail)
    s = [0.0]
    for a, b in zip(pts[:-1], pts[1:]):
        s.append(s[-1] + math.hypot(b[0] - a[0], b[1] - a[1]))
    # apex = the layout rail's point plastic_until
    apex_xy = rail[plastic_until]
    s_apex = min(range(len(pts)), key=lambda i: math.hypot(pts[i][0] - apex_xy[0], pts[i][1] - apex_xy[1]))
    S_ap, S_end = s[s_apex], s[-1]

    def z_at(si):
        if si <= S_ap:
            t = si / S_ap
            return 1.5 + height * (3 * t * t - 2 * t * t * t)
        t = (si - S_ap) / (S_end - S_ap)
        return 1.5 + height * (1 - t) ** 1.3

    P = [(x, -y, z_at(si)) for (x, y), si in zip(pts, s)]
    plastic_pts = P[:s_apex + 1]
    wire_pts = P[s_apex:]
    # U profile (width 30, walls 9 high)
    prof = bpy.data.curves.new(name + "_U", "CURVE")
    sp = prof.splines.new("POLY")
    u = [(-16, 9), (-15, 9), (-15, 0.8), (15, 0.8), (15, 9), (16, 9), (16, -0.4), (-16, -0.4)]
    sp.points.add(len(u) - 1)
    for p, (a, b) in zip(sp.points, u):
        p.co = (a, b, 0, 1)
    sp.use_cyclic_u = True
    profo = bpy.data.objects.new(name + "_U", prof)
    bpy.context.scene.collection.objects.link(profo)
    profo.hide_render = True
    _curve(name + "_Plastic", plastic_pts, clear_plastic(colour, 0.45), profile=profo)
    cm = chrome()
    # wire rail: two floor wires and two side wires
    for d, dz in ((-7.0, 0.0), (7.0, 0.0), (-12.0, 7.0), (12.0, 7.0)):
        q = [(x, y, z + dz) for x, y, z in _offset(wire_pts, d)]
        _curve(name + "_Wire%+d_%d" % (d, dz), q, cm, bevel=1.1)
    # posts every ~70 units of the wire part, and flat chrome lip at the mouth
    acc = 0.0
    for i in range(1, len(wire_pts)):
        a, b = wire_pts[i - 1], wire_pts[i]
        acc += math.hypot(b[0] - a[0], b[1] - a[1])
        if acc >= 70.0 and b[2] > 6:
            acc = 0.0
            for d in (-12.0, 12.0):
                x, y, z = _offset([a, b], d)[1]
                cyl(0.9, z, (x, y, z / 2), 12, cm)
    x0, y0, _ = P[0]
    box((34, 3, 1.2), (x0, y0 + 1.5, 0.6), cm)
    # decals on the plastic floor near the mouth: three chevrons in the shot colour, then the name
    tan = (P[3][0] - P[0][0], P[3][1] - P[0][1], P[3][2] - P[0][2])
    yaw = math.atan2(tan[1], tan[0])
    pitch = math.atan2(tan[2], math.hypot(tan[0], tan[1]))
    dm = mat(name + "_Decal", HEX[colour], rough=0.4, coat=0.3)
    ink = mat(name + "_Ink", HEX["INK"], rough=0.5)
    for k in range(3):
        i = 2 + k * 2
        x, y, z = P[i]
        for side in (-1, 1):
            arm = box((7.5, 2.2, 0.15), (0, 0, 0), dm)
            arm.rotation_mode = "ZYX"
            arm.location = (x + side * 3.0 * math.cos(yaw + math.pi / 2), y + side * 3.0 * math.sin(yaw + math.pi / 2),
                            z + 1.05)
            arm.rotation_euler = (0, -pitch, yaw + side * math.radians(-140) + math.pi)
    i = min(len(P) - 1, 9)
    x, y, z = P[i]
    t = text(label, 6.2, (x, y, z + 1.1), dm, rot=(0, -pitch, yaw - math.pi / 2 * decal_side + math.pi / 2))
    t.rotation_mode = "ZYX"
    t.rotation_euler = (0, -pitch, yaw - math.pi / 2)
    sh = text(label, 6.2, (x + 0.5, y - 0.5, z + 1.0), ink)
    sh.rotation_mode = "ZYX"
    sh.rotation_euler = (0, -pitch, yaw - math.pi / 2)
    return P


def load_layout():
    path = os.environ.get("PINBALL_LAYOUT", os.path.join(TA, "layout_a.json"))
    with open(path) as f:
        return json.load(f)


def build_ramp_runway():
    lay = load_layout()
    _ramp(lay["runwayRail"], "Runway", "ORCHID", "RUNWAY", plastic_until=3)


def build_ramp_skyway():
    lay = load_layout()
    _ramp(lay["skywayRail"], "Skyway", "PINK", "SKYWAY", plastic_until=3)
    # the deck branch: wire rail from the diverter to the deck set-down
    deck = _densify(lay["deckRail"][1:])
    n = len(deck)
    z0 = 9.7
    P = []
    for k, (x, y) in enumerate(deck):
        t = k / (n - 1)
        P.append((x, -y, (1 - t) * z0 + 1.5 * t + 14.0 * math.sin(math.pi * t)))
    cm = chrome()
    for d in (-7.0, 7.0):
        _curve("Deck_Wire%+d" % d, _offset(P, d), cm, bevel=1.0)
    for d in (-12.0, 12.0):
        _curve("Deck_Side%+d" % d, [(x, y, z + 7) for x, y, z in _offset(P, d)], cm, bevel=1.0)
    # diverter flap at the split (519, 372): turned across the Skyway in "to_deck"
    hinge = bpy.data.objects.new("DiverterHinge", None)
    bpy.context.scene.collection.objects.link(hinge)
    hinge.location = T(519.0 - 13.0, 380.0, 10.5)
    flap = box((22.0, 2.0, 7.0), (11.0, 0.0, 3.5), plastic("DUSK", 0.25))
    flap.parent = hinge
    cyl(1.6, 9.0, (0, 0, 4.0), 16, cm).parent = hinge
    for f, label in enumerate(SHEETS["ramp_skyway"]["frames"], start=1):
        key(hinge, "rotation_euler", f, (0, 0, math.radians(0 if label == "normal" else 35)))


def build_gforce():
    """G-FORCE captive ball housing: the two channel walls (layout captiveBall.walls, r 5) as chrome-capped
    rails, a rear stop post past the switch, and a clear sign plate over the rear end."""
    lay = load_layout()
    cb = lay["captiveBall"]
    cm = chrome()
    body = plastic("VIOLET", 0.25, 0.7)
    for w in cb["walls"]:
        (ax, ay), (bx, by) = w["a"], w["b"]
        dx, dy = bx - ax, by - ay
        L = math.hypot(dx, dy)
        ang = math.atan2(-dy, dx)
        mx, my = (ax + bx) / 2, (ay + by) / 2
        box((L, 2 * w["r"], 10.0), (mx, -my, 5.0), body, rot=(0, 0, ang))
        rail = box((L + 2, 3.2, 1.4), (mx, -my, 10.6), cm, rot=(0, 0, ang))
        smooth(rail, 0.5)
        for (px, py) in ((ax, ay), (bx, by)):
            cyl(w["r"] + 0.3, 11.5, (px, -py, 5.75), 32, cm)
    # rear stop beyond the switch (along -axis from the switch centre)
    sw = cb["switch"]
    sx, sy = (sw["a"][0] + sw["b"][0]) / 2, (sw["a"][1] + sw["b"][1]) / 2
    ax_, ay_ = cb["axis"]
    rx, ry = sx + ax_ * 7.0, sy + ay_ * 7.0
    stop = cyl(7.0, 9.0, (rx, -ry, 4.5), 48, plastic("INK", 0.5, 0.3))
    cyl(5.0, 1.0, (rx, -ry, 9.5), 48, mat("StopRubber", "#262233", rough=0.8))
    # switch blade
    box((math.hypot(sw["b"][0] - sw["a"][0], sw["b"][1] - sw["a"][1]), 1.0, 3.0), (sx, -sy, 1.5), cm,
        rot=(0, 0, math.atan2(-(sw["b"][1] - sw["a"][1]), sw["b"][0] - sw["a"][0])))
    # clear sign over the rear: "G-FORCE", raised on two posts, reading along the channel
    yaw = math.atan2(-ay_, ax_)
    mx, my = sx + ax_ * 2.0, sy + ay_ * 2.0
    sign = box((26.0, 15.0, 1.0), (mx, -my, 17.0), clear_plastic("AQUA", 0.5), rot=(0, 0, yaw + math.pi / 2))
    smooth(sign, 0.6)
    lab = text("G-FORCE", 5.0, (mx, -my, 17.6), plastic("AQUA", 0.3, 0.3), rot=(0, 0, yaw + math.pi))
    text("G-FORCE", 5.0, (mx + 0.4, -my - 0.4, 17.55), mat("GInk", HEX["INK"], rough=0.5), rot=(0, 0, yaw + math.pi))
    _ = (stop, lab)


def build_jet_caps():
    """Three jet bumper caps (r 24, 20, 24): jet-intake caps, a different accent per jet; on = the cap's ring
    and fan glow."""
    cm = chrome()
    roots = []
    for j, (R, acc) in enumerate(zip(JET_RADII, JET_ACCENT)):
        parts = []
        parts.append(cyl(R, 3.0, (0, 0, 13.0), 96, cm))
        ring_m = mat("JetRing%d" % j, HEX[acc], rough=0.15, coat=0.8, emit=HEX[acc], strength=0.0, trans=0.3)
        parts.append(cyl(R - 1.6, 3.4, (0, 0, 13.4), 96, ring_m))
        parts.append(cyl(R - 5.0, 3.6, (0, 0, 13.6), 96, mat("Intake%d" % j, HEX["INK"], rough=0.5)))
        fan_m = mat("Fan%d" % j, "#C8D4E6", metallic=0.9, rough=0.25, emit=HEX[acc], strength=0.0)
        nb = 14
        for k in range(nb):
            a = 2 * math.pi * k / nb
            bl = box(((R - 7.0) * 0.82, 1.6, 0.5), (0, 0, 0), fan_m)
            bl.location = ((R - 6) * 0.48 * math.cos(a), (R - 6) * 0.48 * math.sin(a), 15.6)
            bl.rotation_euler = (math.radians(30), 0, a)
            parts.append(bl)
        cone = cyl(R * 0.3, 5.0, (0, 0, 17.6), 48, cm, r2=0.6)
        smooth(cone)
        parts.append(cone)
        parts.append(cyl(R * 0.3 + 0.6, 0.8, (0, 0, 15.3), 48, plastic(acc, 0.2)))
        root = group(parts, "Jet%d" % j)
        roots.append((root, ring_m, fan_m))
    frames = SHEETS["jet_caps"]["frames"]
    for f, label in enumerate(frames, start=1):
        j = int(label[3]) - 1
        on = label.endswith("_on")
        for k, (root, ring_m, fan_m) in enumerate(roots):
            key_show(root, f, k == j)
            key_emit(ring_m, f, 5.0 if (on and k == j) else 0.0)
            key_emit(fan_m, f, 0.8 if (on and k == j) else 0.0)


def build_flasher_domes():
    """Flasher domes (16 units drawn): chrome base, coloured glass dome; on = lit from inside, white-hot core."""
    cm = chrome()
    roots = []
    for k, cname in enumerate(DOME_COLOURS):
        parts = [cyl(7.8, 2.0, (0, 0, 1.0), 64, cm)]
        gm = mat("Dome%d" % k, HEX[cname], rough=0.08, coat=1.0, trans=0.55, emit=HEX[cname], strength=0.0)
        d = sphere(6.3, (0, 0, 2.0), gm, 48)
        d.scale = (1, 1, 0.95)
        parts.append(d)
        core_m = mat("Core%d" % k, HEX["WHITE"], emit=HEX["CREAM"], strength=0.0)
        parts.append(sphere(2.2, (0, 0, 4.0), core_m, 24))
        roots.append((group(parts, "Dome%d" % k), gm, core_m))
    frames = SHEETS["flasher_domes"]["frames"]
    n = len(DOME_COLOURS)
    for f, label in enumerate(frames, start=1):
        i = (f - 1) % n
        on = label.endswith("_on")
        for k, (root, gm, core_m) in enumerate(roots):
            key_show(root, f, k == i)
            key_emit(gm, f, 7.0 if (on and k == i) else 0.25)
            key_emit(core_m, f, 30.0 if (on and k == i) else 0.0)


BUILD = {
    "drone": build_drone, "propwash": build_propwash, "pylon": build_pylon, "bay_door": build_bay_door,
    "flight_deck": build_flight_deck, "ramp_runway": build_ramp_runway, "ramp_skyway": build_ramp_skyway,
    "gforce": build_gforce, "jet_caps": build_jet_caps, "flasher_domes": build_flasher_domes,
}
NO_SHADOW_CATCHER = {"propwash", "flight_deck", "jet_caps"}   # floor pieces are the floor; caps sit on the bumper


def cell_centre(name):
    sh = SHEETS[name]
    w, h = sh["cell"]
    if sh["anchor"] == "centre":
        return (0.0, 0.0)
    x0, y0 = sh["anchor"]
    return (x0 + w / 2.0, -(y0 + h / 2.0))


def build(name, samples, blend_dir):
    reset()
    setup_scene(name, samples, SHEETS[name]["cell"], cell_centre(name), shadow=name not in NO_SHADOW_CATCHER)
    BUILD[name]()
    sc = bpy.context.scene
    sc.frame_start, sc.frame_end = 1, len(SHEETS[name]["frames"])
    os.makedirs(blend_dir, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(blend_dir, name + ".blend"), compress=True,
                                relative_remap=True)


def render_and_pack(name, tmp):
    sh = SHEETS[name]
    sc = bpy.context.scene
    cw, ch = sh["cell"][0] * PPU, sh["cell"][1] * PPU
    n = len(sh["frames"])
    cols = sh["cols"]
    rows = (n + cols - 1) // cols
    sheet = np.zeros((rows * ch, cols * cw, 4), dtype=np.uint8)
    os.makedirs(tmp, exist_ok=True)
    for i in range(n):
        sc.frame_set(i + 1)
        path = os.path.join(tmp, "%s_%02d.tga" % (name, i))
        sc.render.filepath = path
        bpy.ops.render.render(write_still=True)
        px = tga_read(path)
        assert px.shape == (ch, cw, 4), (px.shape, ch, cw)
        # fully transparent pixels carry no colour: zero them so the bytes do not depend on the renderer's
        # choice of colour under alpha 0
        px[px[..., 3] == 0] = 0
        r, c = divmod(i, cols)
        sheet[r * ch:(r + 1) * ch, c * cw:(c + 1) * cw] = px
    png_write(os.path.join(OUT, name + ".png"), sheet)


def write_manifest():
    meta = {"pixelsPerUnit": PPU, "note": "cells left to right, top to bottom, in frame order", "sheets": {}}
    for name, sh in SHEETS.items():
        meta["sheets"][name] = {"file": name + ".png", "cellUnits": list(sh["cell"]),
                                "cellPixels": [sh["cell"][0] * PPU, sh["cell"][1] * PPU], "columns": sh["cols"],
                                "anchor": sh["anchor"], "frames": sh["frames"]}
    meta["sheets"]["jet_caps"]["radii"] = JET_RADII
    meta["sheets"]["flasher_domes"]["colours"] = DOME_COLOURS
    with open(os.path.join(OUT, "sheets.json"), "w") as f:
        json.dump(meta, f, indent=1, sort_keys=True)
        f.write("\n")


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    toys = list(SHEETS)
    samples = 96
    from_blend = "--from-blend" in argv
    if "--toy" in argv:
        toys = argv[argv.index("--toy") + 1].split(",")
    if "--samples" in argv:
        samples = int(argv[argv.index("--samples") + 1])
    tmp = os.environ.get("TABLE_A_TMP", "/tmp/table_a_frames")
    for name in toys:
        if not from_blend:
            build(name, samples, HERE)
        render_and_pack(name, tmp)
        print("table_a: %s done" % name, flush=True)
    write_manifest()


main()
