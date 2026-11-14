# Sprites: everything the renderer draws over the table. 4 pixels per playfield unit, straight alpha.
# Light comes from the upper left in every sprite; every sprite carries its own dark rim.
#   python3 make_sprites.py [out_dir]      (default: ../../assets/sprites)
import math
import os
import sys

import numpy as np
import skia

import palette as P
from common import (C, ROOT, Stage, blur, chrome_sweep, fill, font, layout, lin, mix, plus, poly_path, rad, rrect,
                    save_png, shade, stroke, text_path)

L = layout()
BALL_R = L["ballRadius"]
FLIP = L["flipper"]
PL = L["plunger"]


def ball():
    """Chrome ball, 32 units square: the disc is 24 across, the rest is its dark rim.
    Mirror chrome: a white sky above a crisp horizon, the deck mirrored below it. Kept light overall
    (the visibility rule needs the disc's mean luminance above 0.70) and warm at the highlight."""
    u = P.BALL_SPRITE_UNITS
    st = Stage(u, u, 4, 4, transparent=True)
    c = st.c
    x = y = u / 2
    r = BALL_R
    c.drawCircle(x, y, r + 3.0, blur(fill(C(P.INK, 170)), 0.9))
    c.drawCircle(x, y, r + 2.0, fill(C(P.INK, 250)))
    c.save()
    cp = skia.Path(); cp.addCircle(x, y, r); c.clipPath(cp, skia.ClipOp.kIntersect, True)
    # Sky: white overhead, paling to a cool blue at the horizon.
    c.drawPaint(shade(lin((x, y - r), (x, y + 2.5), [C(P.WHITE), C("#FFFBF0"), C("#E4ECFC"), C("#C2D0F2")], [0, 0.35, 0.8, 1])))
    # The horizon bows down toward the viewer; below it the deck, darkest at the line and lightening away.
    hz = skia.Path(); hz.moveTo(x - r - 1, y + 0.6); hz.quadTo(x, y + 5.4, x + r + 1, y + 0.6)
    below = skia.Path(hz); below.lineTo(x + r + 1, y + r + 1); below.lineTo(x - r - 1, y + r + 1); below.close()
    c.save(); c.clipPath(below, skia.ClipOp.kIntersect, True)
    c.drawPaint(shade(lin((x, y + 2.5), (x, y + r), [C("#8E9CD6"), C("#C0CCEE"), C("#F0F4FE"), C("#FFF6E6")], [0, 0.30, 0.72, 1])))
    for j in range(3):
        y0 = y + 3.2 + j * 2.9; y1 = y0 + 2.9
        for i in range(-4, 4):
            if (i + j) % 2 == 0:
                w0 = 4.6 - j * 1.0; w1 = 4.6 - (j + 1) * 1.0
                c.drawPath(poly_path([(x + i * w0, y0), (x + (i + 1) * w0, y0), (x + (i + 1) * w1, y1), (x + i * w1, y1)]),
                           fill(C("#3F4F98", 125 - j * 34)))
    c.restore()
    c.drawPath(hz, blur(stroke(C("#2E3A80", 235), 0.9), 0.15))
    c.save(); c.translate(0, -1.0); c.drawPath(hz, blur(stroke(C(P.WHITE, 200), 0.6), 0.2)); c.restore()
    # Limb: a touch of shade at the very edge so the disc reads as a sphere, then a light rim inside the dark one.
    c.drawCircle(x, y, r, shade(rad((x, y), r, [C("#000000", 0), C("#000000", 0), C("#1C2460", 30)], [0, 0.88, 1])))
    c.restore()
    c.drawCircle(x, y, r - 0.6, stroke(C(P.CREAM, 250), 1.2))
    c.drawCircle(x - 4.0, y - 5.0, 2.9, blur(fill(C(P.WHITE)), 0.3))
    for dx, dy in ((5.2, 0), (0, 5.2)):
        c.drawLine(x - 4.0 - dx, y - 5.0 - dy, x - 4.0 + dx, y - 5.0 + dy, blur(stroke(C(P.WHITE, 235), 0.55), 0.2))
    return st.image()


def glow():
    """Additive light, white, tinted by the renderer. Falls to nothing well inside the edge."""
    st = Stage(64, 64, 4, 1, transparent=True)
    xs, ys = st.grid()
    d = np.hypot(xs - 32, ys - 32) / 32.0
    a = np.clip(1 - d, 0, 1) ** 2.2
    out = np.empty(xs.shape + (4,), np.float32)
    out[..., :3] = 255
    out[..., 3] = a * 255
    from PIL import Image
    return Image.fromarray(out.astype(np.uint8), "RGBA")


def flare():
    """Additive flasher burst, white, tinted by the renderer: a hot core, a soft bloom and a four-point star
    with fainter diagonals (the 90s lens flare, kept to flashers; never on a lamp at rest). numpy only."""
    n = 256
    ys, xs = np.mgrid[0:n, 0:n].astype(np.float64)
    dx = (xs + 0.5 - n / 2) / (n / 2)
    dy = (ys + 0.5 - n / 2) / (n / 2)
    d = np.hypot(dx, dy)
    core = np.clip(1 - d / 0.22, 0, 1) ** 1.6
    bloom = np.clip(1 - d, 0, 1) ** 3.0 * 0.55
    fade = np.clip(1 - d, 0, 1) ** 1.4

    def ray(u, v, width):
        return np.exp(-(v / width) ** 2) * fade * np.clip(1 - np.abs(u), 0, 1)

    r2 = np.sqrt(0.5)
    rays = ray(dx, dy, 0.035) + ray(dy, dx, 0.035)
    rays += 0.45 * (ray((dx + dy) * r2, (dx - dy) * r2, 0.025) + ray((dx - dy) * r2, (dx + dy) * r2, 0.025))
    a = np.clip(core + bloom + rays * 0.8, 0, 1)
    out = np.empty((n, n, 4), np.float64)
    out[..., :3] = 255
    out[..., 3] = np.round(a * 255)
    from PIL import Image
    return Image.fromarray(out.astype(np.uint8), "RGBA")


def ring():
    """Additive shock ring for a struck bumper or a flasher's edge, white, tinted by the renderer. numpy only."""
    n = 128
    ys, xs = np.mgrid[0:n, 0:n].astype(np.float64)
    d = np.hypot(xs + 0.5 - n / 2, ys + 0.5 - n / 2) / (n / 2)
    a = np.exp(-((d - 0.78) / 0.10) ** 2) * np.clip((1 - d) / 0.08, 0, 1)
    out = np.empty((n, n, 4), np.float64)
    out[..., :3] = 255
    out[..., 3] = np.round(np.clip(a, 0, 1) * 255)
    from PIL import Image
    return Image.fromarray(out.astype(np.uint8), "RGBA")


def _field_mask(scale, ss=4):
    """The playfield inside the shell, the lane island cut out, at `scale` pixels per unit (anti-aliased by
    drawing `ss` times larger), and each pixel's distance in units to the nearest wall chain."""
    from PIL import Image, ImageDraw
    w, h = int(round(L["width"] * scale)), int(round(L["height"] * scale))
    big = Image.new("L", (w * ss, h * ss), 0)
    d = ImageDraw.Draw(big)
    k = scale * ss
    d.polygon([(x * k, y * k) for x, y in L["shell"]], fill=255)
    d.polygon([(x * k, y * k) for x, y in L["laneIsland"]], fill=0)
    mask = np.asarray(big.resize((w, h), Image.BOX), np.float64) / 255.0
    ys, xs = np.mgrid[0:h, 0:w].astype(np.float64)
    ux, uy = (xs + 0.5) / scale, (ys + 0.5) / scale
    dist = np.full(ux.shape, 1e9)
    for chain in (L["shell"], L["laneIsland"]):
        for i in range(len(chain)):
            (ax, ay), (bx, by) = chain[i - 1], chain[i]
            dx, dy = bx - ax, by - ay
            t = np.clip(((ux - ax) * dx + (uy - ay) * dy) / max(1e-9, dx * dx + dy * dy), 0, 1)
            dist = np.minimum(dist, np.hypot(ux - ax - t * dx, uy - ay - t * dy))
    return mask, dist, ux, uy


def _light_png(alpha):
    from PIL import Image
    out = np.empty(alpha.shape + (4,), np.float64)
    out[..., :3] = 255
    out[..., 3] = np.round(np.clip(alpha, 0, 1) * 255)
    return Image.fromarray(out.astype(np.uint8), "RGBA")


def gi():
    """The general illumination as a coloured light map, drawn additively at full colour by the renderer:
    amber along the walls where the bulbs are, orange round the slingshots, magenta and cyan pools under the
    bumpers. The open middle of the table gets nothing, so the lit inserts stand out against its dark.
    Nothing outside the shell. Half a pixel per unit (it is soft). Straight alpha. numpy/Pillow."""
    from PIL import Image
    mask, dist, ux, uy = _field_mask(0.5)

    def rgb(h):
        return np.array(P.rgb(h), np.float64) / 255.0

    def pool(cx, cy, sigma):
        return np.exp(-((ux - cx) ** 2 + (uy - cy) ** 2) / (2 * sigma ** 2))

    light = np.zeros(ux.shape + (3,), np.float64)
    walls = np.exp(-dist / 30.0)
    # The launch lane is narrow, all of it near a wall: held down so it does not glare.
    lane = min(x for x, _ in L["laneIsland"])
    walls = np.where(ux > lane, walls * 0.4, walls)
    light += walls[..., None] * rgb(P.GI_COLOURS["walls"]) * 0.85
    for tri in L["slingshots"]:
        cx = sum(p[0] for p in tri) / 3.0
        cy = sum(p[1] for p in tri) / 3.0
        light += pool(cx, cy, 48.0)[..., None] * rgb(P.GI_COLOURS["slings"]) * 0.7
    for i, b in enumerate(L["bumpers"]):
        colour = P.GI_COLOURS["bumpers"][i % len(P.GI_COLOURS["bumpers"])]
        light += pool(b["c"][0], b["c"][1], 44.0)[..., None] * rgb(colour) * 0.55
    light *= mask[..., None]
    alpha = np.clip(light.max(axis=2), 0, 1)
    colour = np.where(alpha[..., None] > 1e-6, light / np.maximum(alpha[..., None], 1e-6), 0)
    out = np.empty(ux.shape + (4,), np.float64)
    out[..., :3] = np.round(np.clip(colour, 0, 1) * 255)
    out[..., 3] = np.round(alpha * 255)
    return Image.fromarray(out.astype(np.uint8), "RGBA")


def pool():
    """The dark pool drawn round each ball under the light show, ink, at the renderer's alpha: solid out to a
    little past the ball's rim, then a smooth fall to nothing at the edge. numpy only."""
    from PIL import Image
    n = 128
    ys, xs = np.mgrid[0:n, 0:n].astype(np.float64)
    d = np.hypot(xs + 0.5 - n / 2, ys + 0.5 - n / 2) / (n / 2)
    t = np.clip((d - 0.55) / 0.45, 0, 1)
    a = 1 - t * t * (3 - 2 * t)
    out = np.empty((n, n, 4), np.float64)
    out[..., :3] = P.rgb(P.POOL)
    out[..., 3] = np.round(a * 255)
    return Image.fromarray(out.astype(np.uint8), "RGBA")


def flipper():
    m = 9.0
    w = 2 * m + FLIP["baseRadius"] + FLIP["length"] + FLIP["tipRadius"]
    h = 2 * m + 2 * FLIP["baseRadius"]
    st = Stage(w, h, 4, 2, transparent=True)
    c = st.c
    px, py = m + FLIP["baseRadius"], h / 2
    tx = px + FLIP["length"]
    rb, rt = FLIP["baseRadius"], FLIP["tipRadius"]

    def bat(grow):
        a = math.asin((rb - rt) / FLIP["length"])
        p = skia.Path()
        p.moveTo(px + (rb + grow) * math.sin(a), py - (rb + grow) * math.cos(a))
        p.lineTo(tx + (rt + grow) * math.sin(a), py - (rt + grow) * math.cos(a))
        p.arcTo(skia.Rect.MakeLTRB(tx - rt - grow, py - rt - grow, tx + rt + grow, py + rt + grow),
                -90 + math.degrees(a), 180 - 2 * math.degrees(a), False)
        p.lineTo(px + (rb + grow) * math.sin(a), py + (rb + grow) * math.cos(a))
        p.arcTo(skia.Rect.MakeLTRB(px - rb - grow, py - rb - grow, px + rb + grow, py + rb + grow),
                90 - math.degrees(a), 180 + 2 * math.degrees(a), False)
        p.close()
        return p
    c.drawPath(bat(3.6), blur(fill(C(P.INK, 140)), 1.4))
    c.drawPath(bat(1.8), fill(C(P.INK, 250)))
    # Rubber ring (cream), then the glossy bat inside it.
    c.drawPath(bat(0.0), shade(lin((0, py - rb), (0, py + rb), [C(P.WHITE), C(P.CREAM), C("#B9A98C")], [0, 0.4, 1])))
    c.drawPath(bat(-2.6), shade(lin((0, py - rb), (0, py + rb),
                                    [C("#FFB3D2"), C(P.PINK), C("#C01060"), C("#7A0A48")], [0, 0.32, 0.72, 1])))
    c.save(); c.clipPath(bat(-2.6), skia.ClipOp.kIntersect, True)
    c.drawLine(px + 4, py - rb + 4.6, tx - 2, py - rt + 3.0, blur(stroke(C(P.WHITE, 225), 1.5), 0.5))
    c.drawLine(px + 10, py + rb - 4.2, tx - 4, py + rt - 2.4, blur(stroke(C("#FF9AD0", 120), 1.2), 0.7))
    c.restore()
    c.drawPath(bat(-2.6), stroke(C(P.INK, 120), 0.5))
    # Chrome pivot cap.
    c.drawCircle(px, py, 6.4, fill(C(P.INK)))
    c.drawCircle(px, py, 5.4, shade(chrome_sweep(px, py)))
    c.drawCircle(px, py, 2.6, shade(rad((px - 0.8, py - 1.0), 3.2, [C(P.WHITE), C(P.CH_MID), C(P.CH_LO)])))
    return st.image()


def bumper(part):
    """Thruster bumper, 64 units square, skirt radius 30 (the renderer scales it to the bumper's radius).
    'body' is neutral and takes the renderer's tint; 'cap' is drawn over it untinted."""
    st = Stage(64, 64, 4, 2, transparent=True)
    c = st.c
    x = y = 32.0
    r = 30.0
    if part == "body":
        c.drawCircle(x, y, r + 1.4, blur(fill(C(P.INK, 150)), 1.0))
        c.drawCircle(x, y, r, fill(C(P.INK, 250)))
        c.drawCircle(x, y, r - 1.8, shade(rad((x - 9, y - 11), r * 1.75,
                                              [C(P.WHITE), C("#ECECEC"), C("#8E8E8E"), C("#3C3C3C")], [0, 0.26, 0.78, 1])))
        # Nozzle petals pressed into the skirt.
        for k in range(16):
            a = math.radians(k * 22.5 + 11.25)
            c.drawLine(x + 20.5 * math.cos(a), y + 20.5 * math.sin(a), x + 27.2 * math.cos(a), y + 27.2 * math.sin(a),
                       stroke(C("#2A2A2A", 150), 1.0, cap=skia.Paint.kButt_Cap))
        c.drawCircle(x, y, 19.8, stroke(C("#1E1E1E", 200), 1.4))
        hi = skia.Path(); hi.addArc(skia.Rect.MakeLTRB(x - r + 5, y - r + 5, x + r - 5, y + r - 5), 198, 62)
        c.drawPath(hi, blur(stroke(C(P.WHITE, 235), 2.4), 0.6))
        lo = skia.Path(); lo.addArc(skia.Rect.MakeLTRB(x - r + 4.5, y - r + 4.5, x + r - 4.5, y + r - 4.5), 20, 70)
        c.drawPath(lo, blur(stroke(C("#BDBDBD", 150), 1.6), 0.8))
    else:
        c.drawCircle(x, y, 19.0, fill(C(P.INK, 250)))
        c.drawCircle(x, y, 17.8, shade(chrome_sweep(x, y)))
        c.drawCircle(x, y, 17.8, stroke(C(P.CH_HI, 150), 0.6))
        c.drawCircle(x, y, 11.6, fill(C(P.INK)))
        c.drawCircle(x, y, 10.4, shade(rad((x - 2.6, y - 3.2), 12.5, [C(P.WHITE), C(P.YELLOW), C("#E0661A"), C("#7A2A0C")],
                                           [0, 0.36, 0.8, 1])))
        c.drawCircle(x - 3.4, y - 4.0, 2.2, blur(fill(C(P.WHITE, 240)), 0.4))
    return st.image()


def bar():
    """Stand-up target face: neutral glossy slab, tinted by the renderer. 34 long, radius 5, margin 2."""
    st = Stage(48, 14, 4, 2, transparent=True)
    c = st.c
    p = skia.Path(); p.moveTo(7, 7); p.lineTo(41, 7)
    c.drawPath(p, stroke(C(P.INK, 250), 13.4))
    c.drawPath(p, stroke(C("#8C8C8C"), 10.0))
    c.save(); c.translate(0, -0.9); c.drawPath(p, stroke(C("#E6E6E6"), 6.4)); c.restore()
    c.save(); c.translate(0, -2.0); c.drawPath(p, blur(stroke(C(P.WHITE), 2.0), 0.4)); c.restore()
    return st.image()


def plunger():
    w, h = PL["width"], PL["height"]
    st = Stage(w, h, 4, 2, transparent=True)
    c = st.c
    cx = w / 2
    # Rod.
    rod = skia.Rect.MakeLTRB(cx - 5, 12, cx + 5, h)
    c.drawRect(skia.Rect.MakeLTRB(cx - 6.4, 12, cx + 6.4, h), fill(C(P.INK, 250)))
    c.drawRect(rod, shade(lin((cx - 5, 0), (cx + 5, 0), [C(P.CH_LO), C(P.CH_HI), C(P.CH_MID), C(P.CH_LO)], [0, 0.3, 0.6, 1])))
    # Spring coils.
    for i in range(7):
        yy = 20 + i * 6.0
        coil = skia.Path(); coil.moveTo(cx - 10.5, yy + 2.2); coil.lineTo(cx + 10.5, yy - 2.2)
        c.drawPath(coil, stroke(C(P.INK, 250), 4.6))
        c.drawPath(coil, stroke(C(mix(P.CH_MID, P.INK, 0.35)), 2.6))
        c.save(); c.translate(0, -0.6); c.drawPath(coil, stroke(C(P.CH_MID), 0.9)); c.restore()
    # Tip: a rubber-capped head the ball rests on.
    head = rrect(cx - 14, 1.5, 28, 13, 4.5)
    c.drawRRect(rrect(cx - 15.4, 0.2, 30.8, 15.6, 5.6), fill(C(P.INK, 250)))
    # Dark rubber: the ball rests on this at the start of every ball and must stand out against it.
    c.drawRRect(head, shade(lin((0, 1.5), (0, 14.5), [C("#5A48A8"), C("#33287A"), C("#1A1446")], [0, 0.4, 1])))
    c.drawRect(skia.Rect.MakeLTRB(cx - 14, 9.2, cx + 14, 11.6), fill(C(mix(P.YELLOW, P.INK, 0.25))))
    c.drawLine(cx - 9.5, 4.0, cx + 6, 4.0, blur(stroke(C(P.WHITE, 120), 1.0), 0.4))
    return st.image()


def button():
    """One neutral key, 96 units square, drawn nine-part with 32-unit corners and tinted by the renderer."""
    st = Stage(96, 96, 4, 2, transparent=True)
    c = st.c
    c.drawRRect(rrect(1.5, 1.5, 93, 93, 21), fill(C(P.INK, 250)))
    c.drawRRect(rrect(3.5, 3.5, 89, 89, 19.5), shade(lin((0, 3.5), (0, 92.5), [C("#F2F2F2"), C("#8A8A8A"), C("#4E4E4E")], [0, 0.12, 1])))
    c.drawRRect(rrect(7.5, 6.5, 81, 80, 16), shade(lin((0, 6.5), (0, 86.5), [C("#FFFFFF"), C("#D2D2D2"), C("#D2D2D2"), C("#9C9C9C")],
                                                      [0, 0.26, 0.74, 1])))
    # Gloss along the top edge and a soft return light along the bottom: both live inside the corner band.
    c.save(); c.clipRRect(rrect(7.5, 6.5, 81, 80, 16), skia.ClipOp.kIntersect, True)
    c.drawRRect(rrect(12, 9.5, 72, 13, 6.5), blur(fill(C(P.WHITE, 215)), 1.2))
    c.drawRRect(rrect(14, 78, 68, 5, 2.5), blur(fill(C(P.WHITE, 90)), 1.4))
    c.restore()
    c.drawRRect(rrect(7.5, 6.5, 81, 80, 16), stroke(C("#3A3A3A", 170), 0.8))
    return st.image()


def button_gloss():
    """Untinted lights for the button, drawn nine-part over the tinted face: tinting multiplies, so the
    highlights have to come from a second sprite."""
    st = Stage(96, 96, 4, 2, transparent=True)
    c = st.c
    face = rrect(7.5, 6.5, 81, 80, 16)
    c.save(); c.clipRRect(face, skia.ClipOp.kIntersect, True)
    c.drawRRect(rrect(11, 8.5, 74, 15, 7.5), shade(lin((0, 8.5), (0, 23.5), [C(P.WHITE, 225), C(P.WHITE, 40)])))
    c.drawRRect(rrect(15, 79, 66, 5, 2.5), blur(fill(C(P.WHITE, 95)), 1.4))
    c.drawCircle(18, 15, 3.0, blur(fill(C(P.WHITE, 240)), 0.6))
    c.restore()
    c.drawRRect(rrect(3.5, 3.5, 89, 89, 19.5), stroke(C(P.WHITE, 60), 0.7))
    return st.image()


def insert(kind):
    """Lamp insert with a dark bezel. Neutral lens: the renderer tints it (dim when unlit, full when lit)
    and adds glow.png for the halo. Round: 24 units for a 20-unit lens. Arrow: 36 units, points up, 28 long."""
    if kind == "round":
        u = 24.0
        st = Stage(u, u, 4, 4, transparent=True)
        c = st.c
        x = y = u / 2
        shape = skia.Path(); shape.addCircle(x, y, 9.0)
    else:
        u = 36.0
        st = Stage(u, u, 4, 4, transparent=True)
        c = st.c
        x = y = u / 2
        shape = poly_path([(x, y - 13.0), (x + 10.2, y - 0.2), (x + 4.9, y - 0.2), (x + 4.9, y + 12.6),
                           (x - 4.9, y + 12.6), (x - 4.9, y - 0.2), (x - 10.2, y - 0.2)])
    c.drawPath(shape, blur(stroke(C(P.INK, 170), 5.6, join=skia.Paint.kRound_Join), 0.8))
    c.drawPath(shape, stroke(C(P.INK, 252), 4.4, join=skia.Paint.kRound_Join))
    c.drawPath(shape, stroke(C("#7C7C7C"), 1.5, join=skia.Paint.kRound_Join))
    c.drawPath(shape, shade(rad((x - 2.5, y - 3.5), u * 0.62, [C(P.WHITE), C("#E8E8E8"), C("#A8A8A8")], [0, 0.5, 1])))
    c.save(); c.clipPath(shape, skia.ClipOp.kIntersect, True)
    for a in range(0, 360, 30):
        c.drawLine(x, y, x + u * math.cos(math.radians(a)), y + u * math.sin(math.radians(a)), stroke(C("#8A8A8A", 120), 0.5))
    c.drawCircle(x - 3.0, y - (3.6 if kind == "round" else 6.5), 1.5, blur(fill(C(P.WHITE)), 0.3))
    c.restore()
    return st.image()


def door():
    """One hangar bay door (drop target), seen standing: 23.5 by 10 units inside a 28 by 16 sprite.
    Neutral steel; tinted when marked. The numeral and the face lamp are drawn by the renderer."""
    st = Stage(28, 16, 4, 2, transparent=True)
    c = st.c
    c.drawRRect(rrect(0.9, 1.4, 26.2, 13.2, 2.6), fill(C(P.INK, 250)))
    c.drawRRect(rrect(2.25, 3.0, 23.5, 10.0, 1.6), shade(lin((0, 3), (0, 13), [C(P.WHITE), C("#D0D0D0"), C("#7A7A7A")], [0, 0.3, 1])))
    for yy in (5.5, 8.0, 10.5):
        c.drawLine(3.2, yy, 24.8, yy, stroke(C("#3A3A3A", 170), 0.5, cap=skia.Paint.kButt_Cap))
        c.drawLine(3.2, yy + 0.55, 24.8, yy + 0.55, stroke(C(P.WHITE, 150), 0.4, cap=skia.Paint.kButt_Cap))
    c.drawRRect(rrect(2.25, 3.0, 23.5, 10.0, 1.6), stroke(C("#2A2A2A", 200), 0.5))
    return st.image()


def spinner(frame, between=72.0, inset=7.0, colour=None):
    """Spinner between its posts, `between` units apart, in a (between + 8) by 24 sprite. Frame 0 edge-on,
    frame 1 flat. The Afterburner lane's is 72 apart (posts on the lane's walls); the circuit mouths' 57, in
    the circuits' colour. The blade starts `inset` units inside each post."""
    w = between + 8.0
    st = Stage(w, 24, 4, 2, transparent=True)
    c = st.c
    y = 12.0
    x0, x1 = 4.0, 4.0 + between
    hi, mid, lo = (P.WHITE, "#7FE0A8", "#0F6A3A") if colour is None else (P.WHITE, mix(colour, P.WHITE, 0.4), mix(colour, P.INK, 0.6))
    flat = ("#C8FFE0", P.GREEN, "#0F6A3A") if colour is None else (mix(colour, P.WHITE, 0.7), colour, mix(colour, P.INK, 0.6))
    c.drawLine(x0, y, x1, y, stroke(C(P.INK, 250), 2.6)); c.drawLine(x0, y, x1, y, stroke(C(P.CH_HI), 1.0))
    half = 1.9 if frame == 0 else 8.2
    blade = skia.Rect.MakeLTRB(x0 + inset, y - half, x1 - inset, y + half)
    c.drawRect(skia.Rect.MakeLTRB(x0 + inset - 1.2, y - half - 1.2, x1 - inset + 1.2, y + half + 1.2), fill(C(P.INK, 250)))
    if frame == 0:
        c.drawRect(blade, shade(lin((0, y - half), (0, y + half), [C(hi), C(mid), C(lo)])))
    else:
        c.drawRect(blade, shade(lin((0, y - half), (0, y + half), [C(flat[0]), C(flat[1]), C(flat[2])], [0, 0.35, 1])))
        c.save(); c.clipRect(blade, skia.ClipOp.kIntersect, True)
        for i in range(int((between - 2 * inset) / 8.4) + 2):
            xx = x0 + inset - 2 + i * 8.4
            c.drawPath(poly_path([(xx, y + half), (xx + 4.2, y + half), (xx + 10, y - half), (xx + 5.8, y - half)]), fill(C(P.CREAM, 235)))
        c.drawLine(x0 + inset, y - half + 1.5, x1 - inset, y - half + 1.5, blur(stroke(C(P.WHITE, 220), 1.0), 0.4))
        c.restore()
    for xx in (x0, x1):
        c.drawCircle(xx, y, 4.0, fill(C(P.INK, 250)))
        c.drawCircle(xx, y, 3.0, shade(chrome_sweep(xx, y)))
        c.drawCircle(xx - 0.8, y - 0.9, 0.9, fill(C(P.WHITE)))
    return st.image()


def post():
    """A bare metal post end: marks dead posts and wire-gate pivots. 16 units square. Not shipped: the posts
    are baked into the table background and none moves; kept for a table that has moving posts."""
    st = Stage(16, 16, 4, 4, transparent=True)
    c = st.c
    c.drawCircle(8, 8, 6.6, blur(fill(C(P.INK, 150)), 0.6))
    c.drawCircle(8, 8, 6.0, fill(C(P.INK, 250)))
    c.drawCircle(8, 8, 5.0, shade(chrome_sweep(8, 8)))
    c.drawCircle(8, 8, 2.4, shade(rad((7.3, 7.1), 3.0, [C(P.WHITE), C(P.CH_MID), C(P.CH_LO)])))
    c.drawLine(6.6, 8, 9.4, 8, stroke(C(P.INK, 200), 0.6, cap=skia.Paint.kButt_Cap))
    return st.image()


def kickback():
    """Spring launcher at the foot of the left outlane, 28 by 44 units; the plate is at the top."""
    st = Stage(28, 44, 4, 2, transparent=True)
    c = st.c
    cx = 14.0
    c.drawRRect(rrect(2.2, 20, 23.6, 22.5, 3.5), fill(C(P.INK, 250)))
    c.drawRRect(rrect(3.6, 21.4, 20.8, 19.7, 2.6), shade(lin((0, 21), (0, 41), [C("#3A4690"), C("#1A2152")])))
    for i in range(5):
        yy = 12.5 + i * 5.2
        coil = skia.Path(); coil.moveTo(cx - 7.5, yy + 1.8); coil.lineTo(cx + 7.5, yy - 1.8)
        c.drawPath(coil, stroke(C(P.INK, 250), 4.2)); c.drawPath(coil, stroke(C(mix(P.CH_MID, P.INK, 0.4)), 2.4))
        c.save(); c.translate(0, -0.5); c.drawPath(coil, stroke(C(P.CH_MID), 0.8)); c.restore()
    c.drawRRect(rrect(1.6, 1.4, 24.8, 10.6, 3.6), fill(C(P.INK, 250)))
    c.drawRRect(rrect(3.0, 2.8, 22.0, 7.8, 2.6), shade(lin((0, 2.8), (0, 10.6), [C("#C9A032"), C("#8E6A1C"), C("#5A3E10")], [0, 0.4, 1])))
    for i in range(4):
        xx = 5.0 + i * 5.2
        c.drawPath(poly_path([(xx, 10.6), (xx + 2.4, 10.6), (xx + 5.2, 2.8), (xx + 2.8, 2.8)]), fill(C(P.INK, 215)))
    c.drawLine(5, 4.2, 21, 4.2, blur(stroke(C(P.WHITE, 110), 0.8), 0.3))
    return st.image()


def dot():
    """One lamp of the title's dot display, 8 units square, neutral and tinted by the renderer: a round lens,
    brightest in the middle, with a dark rim so that lit dots stay apart at any scale."""
    st = Stage(8, 8, 4, 4, transparent=True)
    c = st.c
    x = y = 4.0
    c.drawCircle(x, y, 3.6, fill(C(P.INK, 230)))
    c.drawCircle(x, y, 3.0, shade(rad((x - 0.5, y - 0.6), 3.3, [C(P.WHITE), C("#E6E6E6"), C("#9A9A9A")], [0, 0.45, 1])))
    return st.image()


# ---- revision 3 ------------------------------------------------------------------------------------------
def _hexagon(cx, cy, r, rot=90.0):
    """A hexagon of corner radius r; rot 90 puts a corner straight down the table (the drone's)."""
    return poly_path([(cx + r * math.cos(math.radians(rot + 60 * k)), cy + r * math.sin(math.radians(rot + 60 * k))) for k in range(6)])


def drone(state):
    """The training drone seen from above, DRONE_SPRITE_UNITS square, the hexagon (the physics' own, radius
    L["drone"]["radius"], a corner down the table) in the middle. Its own cast shadow to the lower right,
    since it hovers. 'patrol': glossy blue shell, chrome rim, a hazard band on its two lower faces (the faces
    that throw the ball aside), a sensor eye. 'dive': the same, sinking: smaller shadow, darker, the eye shut.
    'down': knocked down, lying flat in its bay: scorched, no shadow, a warning flag. Not tinted."""
    u = P.DRONE_SPRITE_UNITS
    st = Stage(u, u, 4, 4, transparent=True)
    c = st.c
    x = y = u / 2
    r = L["drone"]["radius"]
    dark = 0.0 if state == "patrol" else 0.35 if state == "dive" else 0.6
    if state != "down":
        off = (4.5, 6.0) if state == "patrol" else (2.0, 2.6)
        c.save(); c.translate(*off); c.drawPath(_hexagon(x, y, r + 0.5), blur(fill(C(P.INK, 170 if state == "patrol" else 130)), 1.6)); c.restore()
    body = _hexagon(x, y, r)
    c.drawPath(_hexagon(x, y, r + 1.4), fill(C(P.INK, 250)))
    c.drawPath(body, shade(chrome_sweep(x, y)))
    shell = _hexagon(x, y, r - 2.6)
    top = [C(mix("#B8DCFF", P.INK, dark)), C(mix(P.BLUE, P.INK, dark)), C(mix("#14306E", P.INK, dark))]
    c.drawPath(shell, shade(lin((x - r, y - r), (x + r, y + r), top, [0, 0.45, 1])))
    c.save(); c.clipPath(shell, skia.ClipOp.kIntersect, True)
    # Hazard band across the two lower faces.
    band = poly_path([(x - r, y + r * 0.30), (x + r, y + r * 0.30), (x + r, y + r), (x - r, y + r)])
    c.drawPath(band, fill(C(mix(P.YELLOW, P.INK, 0.15 + dark))))
    c.save(); c.clipPath(band, skia.ClipOp.kIntersect, True)
    for i in range(-6, 7):
        xx = x + i * 4.2
        c.drawPath(poly_path([(xx, y + r * 0.30), (xx + 2.1, y + r * 0.30), (xx - 2.9, y + r), (xx - 5.0, y + r)]), fill(C(P.INK, 235)))
    c.restore()
    # Panel lines (the armour plates) and the glint along the upper left faces.
    for k in range(3):
        a = math.radians(-90 + 120 * k)
        c.drawLine(x + 5.2 * math.cos(a), y + 5.2 * math.sin(a), x + (r - 2.6) * math.cos(a), y + (r - 2.6) * math.sin(a),
                   stroke(C(P.INK, 150), 0.6, cap=skia.Paint.kButt_Cap))
    if state != "down":
        c.drawLine(x - r * 0.75, y - r * 0.20, x - r * 0.15, y - r * 0.62, blur(stroke(C(P.WHITE, 200), 1.2), 0.4))
    c.restore()
    c.drawPath(shell, stroke(C(P.INK, 200), 0.7))
    # The sensor eye.
    c.drawCircle(x, y - 1.0, 5.4, fill(C(P.INK)))
    c.drawCircle(x, y - 1.0, 4.4, shade(chrome_sweep(x, y - 1.0)))
    if state == "patrol":
        c.drawCircle(x, y - 1.0, 3.0, shade(rad((x - 0.8, y - 2.0), 3.6, [C("#FFE6F0"), C(P.PINK), C("#5A0830")], [0, 0.4, 1])))
        c.drawCircle(x - 1.0, y - 2.1, 0.9, fill(C(P.WHITE)))
    else:
        c.drawCircle(x, y - 1.0, 3.0, fill(C("#14102A")))
        c.drawLine(x - 2.4, y - 1.0, x + 2.4, y - 1.0, stroke(C(mix(P.CH_MID, P.INK, 0.4)), 0.8, cap=skia.Paint.kButt_Cap))
    if state == "down":
        # Scorch over the shell and a small chequered flag: hit enough, out of the fight.
        c.drawPath(shell, shade(rad((x + 3, y + 2), r, [C(P.INK, 170), C(P.INK, 60), C(P.INK, 0)], [0, 0.5, 1])))
        fx, fy = x + r * 0.42, y - r * 0.80
        c.drawLine(fx, fy, fx, fy + 9.0, stroke(C(P.INK), 1.6, cap=skia.Paint.kButt_Cap)); c.drawLine(fx, fy, fx, fy + 9.0, stroke(C(P.CH_MID), 0.8, cap=skia.Paint.kButt_Cap))
        flag = skia.Rect.MakeXYWH(fx + 0.4, fy - 0.2, 7.0, 4.8)
        c.drawRect(flag, fill(C(P.CREAM)))
        for i in range(4):
            for j in range(2):
                if (i + j) % 2 == 0:
                    c.drawRect(skia.Rect.MakeXYWH(fx + 0.4 + i * 1.75, fy - 0.2 + j * 2.4, 1.75, 2.4), fill(C(P.INK)))
        c.drawRect(flag, stroke(C(P.INK), 0.5))
    return st.image()


def captive_ball():
    """The G-force test's captive ball: steel, 22 across (TABLE_REV3 3.5), in a CAPTIVE_BALL_SPRITE_UNITS
    sprite. Darker and cooler than the ball in play, with no cream rim, so it is never taken for it."""
    u = P.CAPTIVE_BALL_SPRITE_UNITS
    st = Stage(u, u, 4, 4, transparent=True)
    c = st.c
    x = y = u / 2
    r = 11.0
    c.drawCircle(x, y, r + 2.6, blur(fill(C(P.INK, 160)), 0.9))
    c.drawCircle(x, y, r + 1.4, fill(C(P.INK, 250)))
    c.save()
    cp = skia.Path(); cp.addCircle(x, y, r); c.clipPath(cp, skia.ClipOp.kIntersect, True)
    c.drawPaint(shade(lin((x, y - r), (x, y + 2), [C("#E4ECF6"), C("#A7B6CC"), C("#6C7C98")], [0, 0.5, 1])))
    hz = skia.Path(); hz.moveTo(x - r - 1, y + 0.5); hz.quadTo(x, y + 4.6, x + r + 1, y + 0.5)
    below = skia.Path(hz); below.lineTo(x + r + 1, y + r + 1); below.lineTo(x - r - 1, y + r + 1); below.close()
    c.save(); c.clipPath(below, skia.ClipOp.kIntersect, True)
    c.drawPaint(shade(lin((x, y + 2), (x, y + r), [C("#2A3360"), C("#4A5888"), C("#7E8CB4")], [0, 0.5, 1])))
    c.restore()
    c.drawPath(hz, stroke(C("#1C2450", 230), 0.8))
    c.drawCircle(x, y, r, shade(rad((x, y), r, [C("#000000", 0), C("#000000", 0), C("#0A0E30", 70)], [0, 0.8, 1])))
    c.restore()
    c.drawCircle(x - 3.6, y - 4.4, 2.2, blur(fill(C(P.WHITE, 235)), 0.3))
    return st.image()


def standup(letter):
    """A stand-up target's face, upright, STANDUP_SPRITE_UNITS: a neutral glossy plate in a dark bezel with its
    letter in ink. The renderer tints it (dim when unlit, the lamp colour when lit) and rotates it by the
    stand-up's angle minus 90 degrees about its centre, the middle of the capsule."""
    w, h = P.STANDUP_SPRITE_UNITS
    st = Stage(w, h, 4, 4, transparent=True)
    c = st.c
    x, y = w / 2, h / 2
    c.drawRRect(rrect(x - 6.2, y - 13.6, 12.4, 27.2, 4.0), blur(fill(C(P.INK, 160)), 0.8))
    c.drawRRect(rrect(x - 5.6, y - 13.0, 11.2, 26.0, 3.6), fill(C(P.INK, 252)))
    face = rrect(x - 4.4, y - 11.8, 8.8, 23.6, 2.8)
    c.drawRRect(face, shade(lin((x - 4.4, 0), (x + 4.4, 0), [C(P.WHITE), C("#E2E2E2"), C("#9A9A9A")], [0, 0.4, 1])))
    c.save(); c.clipRRect(face, skia.ClipOp.kIntersect, True)
    c.drawLine(x - 2.6, y - 10.4, x - 2.6, y + 10.4, blur(stroke(C(P.WHITE, 230), 1.0), 0.3))
    c.restore()
    f = font("RussoOne-Regular.ttf", 10.5)
    p, _ = text_path(letter, f, x + 0.2, y + 3.9, 0, "center")
    c.drawPath(p, fill(C(P.INK)))
    return st.image()


def flasher_dome():
    """A flasher dome, FLASHER_DOME_UNITS across: clear glossy plastic over a chrome collar, neutral, tinted by
    the renderer (dim at rest, the flasher's colour when it fires, its flare added over it)."""
    u = P.FLASHER_DOME_UNITS
    st = Stage(u, u, 4, 4, transparent=True)
    c = st.c
    x = y = u / 2
    c.drawCircle(x, y, 7.6, fill(C(P.INK, 250)))
    c.drawCircle(x, y, 6.9, shade(chrome_sweep(x, y)))
    c.drawCircle(x, y, 5.6, fill(C(P.INK, 230)))
    c.drawCircle(x, y, 5.0, shade(rad((x - 1.6, y - 2.0), 7.0, [C(P.WHITE), C("#E0E0E0"), C("#A0A0A0"), C("#5A5A5A")], [0, 0.25, 0.7, 1])))
    for a in range(0, 360, 45):
        t = math.radians(a)
        c.drawLine(x + 1.4 * math.cos(t), y + 1.4 * math.sin(t), x + 4.6 * math.cos(t), y + 4.6 * math.sin(t), stroke(C("#8A8A8A", 110), 0.4))
    c.drawCircle(x - 1.9, y - 2.2, 1.3, blur(fill(C(P.WHITE)), 0.3))
    return st.image()


def gate():
    """The Afterburner lane's one-way gate: a wire flap hanging from its axle, seen from above, as long as the
    gate (laneGate, without its ghost points) plus the pivots, 14 high. The axle is the sprite's middle row and
    the flap hangs below it (down the table); the renderer rotates it along the gate and shortens it toward the
    axle as the flap swings up. The axle and its pivots are also baked into the background."""
    g = L["laneGate"][1:-1]
    ln = math.hypot(g[-1][0] - g[0][0], g[-1][1] - g[0][1])
    w, h = ln + 8.0, 14.0
    st = Stage(w, h, 4, 4, transparent=True)
    c = st.c
    y = h / 2
    x0, x1 = 4.0, w - 4.0
    flap = poly_path([(x0 + 3, y), (x0 + 3, y + 5.2), (x1 - 3, y + 5.2), (x1 - 3, y)], close=False)
    c.drawPath(flap, stroke(C(P.INK, 240), 2.2))
    c.drawPath(flap, stroke(C(P.CH_MID), 1.0))
    c.save(); c.translate(-0.2, -0.3); c.drawPath(flap, stroke(C(P.CH_HI), 0.4)); c.restore()
    for xx in np.arange(x0 + 12, x1 - 6, 10.0):
        c.drawLine(xx, y, xx, y + 5.2, stroke(C(P.INK, 220), 1.4, cap=skia.Paint.kButt_Cap))
        c.drawLine(xx, y, xx, y + 5.2, stroke(C(P.CH_MID), 0.6, cap=skia.Paint.kButt_Cap))
    return st.image()


def insert_shape(kind):
    """Lamp inserts of revision 3, like insert('round'): a neutral lens in a dark bezel, tinted by the renderer.
    'hex': drone armour, 16 units for an 11-unit hexagon (a flat side up). 'chevron': a rank stripe pointing up,
    24 by 14 units for an 18 by 9 chevron."""
    if kind == "hex":
        w = h = 16.0
        st = Stage(w, h, 4, 4, transparent=True)
        x, y = w / 2, h / 2
        shape = _hexagon(x, y, P.HEX_DIAMETER / 2, rot=0.0)
    else:
        cw, ch = P.CHEVRON_SIZE
        w, h = cw + 6.0, ch + 5.0
        st = Stage(w, h, 4, 4, transparent=True)
        x, y = w / 2, h / 2
        t = 3.6
        shape = poly_path([(x, y - ch / 2), (x + cw / 2, y + ch / 2 - t * 0.2), (x + cw / 2, y + ch / 2), (x + cw / 2 - t * 1.3, y + ch / 2),
                           (x, y - ch / 2 + t), (x - cw / 2 + t * 1.3, y + ch / 2), (x - cw / 2, y + ch / 2), (x - cw / 2, y + ch / 2 - t * 0.2)])
    c = st.c
    c.drawPath(shape, blur(stroke(C(P.INK, 170), 4.0, join=skia.Paint.kRound_Join), 0.6))
    c.drawPath(shape, stroke(C(P.INK, 252), 3.0, join=skia.Paint.kRound_Join))
    c.drawPath(shape, stroke(C("#7C7C7C"), 1.0, join=skia.Paint.kRound_Join))
    c.drawPath(shape, shade(rad((x - 2.0, y - 2.5), w * 0.6, [C(P.WHITE), C("#E8E8E8"), C("#A8A8A8")], [0, 0.5, 1])))
    c.save(); c.clipPath(shape, skia.ClipOp.kIntersect, True)
    c.drawCircle(x - 1.8, y - 2.0, 1.1, blur(fill(C(P.WHITE)), 0.3))
    c.restore()
    return st.image()


def _circuit_between():
    a, b = L["sensors"][0]["a"], L["sensors"][0]["b"]
    return math.hypot(b[0] - a[0], b[1] - a[1])


SPRITES = {
    "ball.png": ball, "glow.png": glow, "flipper.png": flipper, "bumper.png": lambda: bumper("body"),
    "bumper_cap.png": lambda: bumper("cap"), "bar.png": bar, "plunger.png": plunger, "button.png": button, "button_gloss.png": button_gloss,
    "insert_round.png": lambda: insert("round"), "insert_arrow.png": lambda: insert("arrow"), "door.png": door,
    "spinner_0.png": lambda: spinner(0), "spinner_1.png": lambda: spinner(1), "kickback.png": kickback,
    "flare.png": flare, "ring.png": ring, "gi.png": gi, "pool.png": pool, "dot.png": dot,
    # Revision 3.
    "drone.png": lambda: drone("patrol"), "drone_dive.png": lambda: drone("dive"), "drone_down.png": lambda: drone("down"),
    "captive_ball.png": captive_ball, "flasher_dome.png": flasher_dome, "gate.png": gate,
    "standup_t.png": lambda: standup("T"), "standup_a.png": lambda: standup("A"), "standup_x.png": lambda: standup("X"),
    "standup_i.png": lambda: standup("I"), "insert_hex.png": lambda: insert_shape("hex"),
    "insert_chevron.png": lambda: insert_shape("chevron"),
    "spinner_circuit_0.png": lambda: spinner(0, _circuit_between(), 6.0, P.AQUA),
    "spinner_circuit_1.png": lambda: spinner(1, _circuit_between(), 6.0, P.AQUA),
}


def main(out):
    for name, fn in SPRITES.items():
        save_png(fn(), os.path.join(out, name))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "assets", "sprites"))
