# Full-table composite of the Table A art at iPhone Retina size (iPhone 18 Pro, 402 pt wide at 3x: the 768-unit
# playfield is 1206 px wide), with every master placed, in a sample game state at a given Altitude.
# Review only (not shipped).
#   build/art-venv/bin/python tools/art/table_a/composite.py <out.png> fl100|fl500
import math
import os
import sys

import numpy as np
import skia
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import make_floor as F  # noqa: E402
from make_floor import C, L, META, P, Stage, blur, draw_frame, fill, hexrgb, mix, plus, poly_path, rad, shade, stroke  # noqa: E402

ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
PX_W = 1206


def neon(col):
    return P.NEON.get(col, mix(col, P.WHITE, 0.25))


def sprite_file(c, path, x, y, w, h=None, angle=0.0, tint=None):
    im = Image.open(path).convert("RGBA")
    img = skia.Image.fromarray(np.ascontiguousarray(np.asarray(im)), colorType=skia.kRGBA_8888_ColorType,
                               alphaType=skia.kUnpremul_AlphaType)
    h = h if h is not None else w * im.height / im.width
    paint = skia.Paint(AntiAlias=True)
    if tint:
        r, g, b = hexrgb(tint)
        paint.setColorFilter(skia.ColorFilters.Blend(skia.Color(r, g, b), skia.BlendMode.kModulate))
    c.save(); c.translate(x, y); c.rotate(angle)
    c.drawImageRect(img, skia.Rect.MakeXYWH(-w / 2, -h / 2, w, h), skia.SamplingOptions(skia.FilterMode.kLinear,
                                                                                         skia.MipmapMode.kNone), paint)
    c.restore()


def glow(c, x, y, r, col, a):
    c.drawCircle(x, y, r, plus(shade(rad((x, y), r, [C(col, int(255 * a)), C(col, 0)]))))


def lit_lamp(c, e):
    (x, y), col, shape = e["c"], e["colour"], e["shape"]
    n = neon(col)
    if shape in ("arrow", "arrow_mission"):
        ax, ay = e["aim"]
        ang = math.degrees(math.atan2(ay - y, ax - x)) + 90.0
        draw_frame(c, "inserts_arrows", shape, x, y, angle=ang, tint=n)
        glow(c, x, y, 26, n, 0.45)
    elif shape == "pip":
        draw_frame(c, "inserts_arrows", "pip", x, y, tint=n)
        glow(c, x, y, 11, n, 0.45)
    elif shape == "rung":
        draw_frame(c, "inserts_altitude", e["name"][-5:], x, y, tint=n)
        glow(c, x, y, 24, n, 0.35)
    elif shape == "flaps":
        draw_frame(c, "inserts_flaps", e["letter"], x, y, tint=n)
        glow(c, x, y, 18, n, 0.4)
    else:
        d = e.get("d", 12.0)
        if shape == "hex":
            path = F.hexagon(x, y, d / 2 + 1.5)
        elif shape == "chevron":
            path = F.chevron_path(x, y, d, 9.0)
        else:
            path = poly_path([(x + d / 2 * math.cos(a), y + d / 2 * math.sin(a))
                              for a in np.linspace(0, 2 * math.pi, 40, endpoint=False)])
        c.drawPath(path, shade(rad((x - d * 0.15, y - d * 0.2), d * 0.8, [C(mix(n, P.WHITE, 0.55)), C(n)])))
        c.drawPath(path, stroke(C(P.INK), 1.2))
        glow(c, x, y, d * 1.2, n, 0.4)


def flipper(c, f, col):
    (px, py), a, L_, rb, rt = f["pivot"], f["rest"], f["length"], f["baseRadius"], f["tipRadius"]
    tx, ty = px + L_ * math.cos(a), py + L_ * math.sin(a)
    nx, ny = -math.sin(a), math.cos(a)
    body = skia.Path()
    body.moveTo(px + nx * rb, py + ny * rb); body.lineTo(tx + nx * rt, ty + ny * rt)
    body.lineTo(tx - nx * rt, ty - ny * rt); body.lineTo(px - nx * rb, py - ny * rb); body.close()
    body.addCircle(px, py, rb); body.addCircle(tx, ty, rt)
    c.save(); c.translate(4, 6); c.drawPath(body, blur(fill(C("#04030E", 160)), 2.5)); c.restore()
    c.drawPath(body, fill(C(P.INK)))
    c.save(); c.scale(1, 1)
    c.drawPath(body, shade(F.lin((px, py - rb), (px, py + rb), [C(mix(col, P.WHITE, 0.35)), C(col), C(mix(col, P.INK, 0.4))])))
    c.restore()
    c.drawPath(body, stroke(C(P.INK), 1.4))
    c.drawCircle(px, py, rb * 0.45, shade(F.chrome_sweep(px, py)))


def main(dst, level):
    im = render(level, PX_W / L["width"])
    im.save(dst, optimize=True)
    print("composite", level, im.size, dst)


def render(level, scale, worst=False, balls=True, ramps=True):
    """The table as the game draws it, in a sample state at level fl100 or fl500, `scale` pixels per unit.
    worst: every insert lit, every jet and dome on, the deck open, doors marked (tools/art/visibility_full.py
    --table-a); balls: the sample balls; ramps: the clear ramps on top (else left out)."""
    top = level == "fl500"
    k = 4 if top else 0
    gi_col = P.ALTITUDE_GI[k]
    st = Stage(L["width"], L["height"], scale, 1)
    c = st.c
    out = os.path.join(HERE, "out")
    bg = skia.Image.open(os.path.join(out, "background_a.png"))
    c.drawImageRect(bg, skia.Rect.MakeWH(L["width"], L["height"]), skia.SamplingOptions(skia.FilterMode.kLinear,
                                                                                         skia.MipmapMode.kLinear))
    # general illumination: the grey light map in the Altitude colour, additive (TableEffects, giGrey)
    gi = np.asarray(Image.open(os.path.join(out, "gi_a.png")).convert("L"), np.float32) / 255.0
    strength = P.TABLE_A_GI_STRENGTH[1] if top else P.TABLE_A_GI_STRENGTH[0]
    r, g, b = hexrgb(gi_col)
    rgba = np.zeros(gi.shape + (4,), np.uint8)
    rgba[..., 0] = np.clip(gi * r * strength, 0, 255)
    rgba[..., 1] = np.clip(gi * g * strength, 0, 255)
    rgba[..., 2] = np.clip(gi * b * strength, 0, 255)
    rgba[..., 3] = 255
    gimg = skia.Image.fromarray(rgba, colorType=skia.kRGBA_8888_ColorType)
    p = skia.Paint(); p.setBlendMode(skia.BlendMode.kPlus)
    c.drawImageRect(gimg, skia.Rect.MakeWH(L["width"], L["height"]), skia.SamplingOptions(skia.FilterMode.kLinear,
                                                                                          skia.MipmapMode.kNone), p)
    # altimeter pointer at the current level, the turntable, the deck (open when worst, or at FL500)
    draw_frame(c, "altitude_plate", "fl%d00" % (k + 1), *F.ALT_PLATE_AT, centre=False)
    pw = L["propWash"]["c"]
    draw_frame(c, "propwash_blur" if top else "propwash", "blur030" if top else "rot015", *pw)
    deck_open = top or worst
    draw_frame(c, "flight_deck", "lit_fl%d00" % (k + 1) if deck_open else "off", *META["flight_deck"]["anchor"],
               centre=False)
    if deck_open or True:
        # the deck flood (TableRenderer::drawDeckA): three soft lights in the GI colour over the plate
        dx, dy = META["flight_deck"]["anchor"]
        cw, ch = META["flight_deck"]["cellUnits"]
        a = P.TABLE_A_DECK_FLOOD[1 if deck_open else 0]
        for f in (-0.28, 0.0, 0.28):
            glow(c, dx + cw / 2, dy + ch / 2 + f * ch, cw * 0.62, gi_col, a)
    # lit lamps for a sample state
    plan = F.lamp_plan()
    if worst:
        lit = {e["name"] for e in plan}
    elif top:
        lit = {e["name"] for e in plan if e["source"] == "layout" and not e["name"].startswith(("Rank6", "Rank7", "Bonus5", "Combo4", "Combo5", "DroneArmour"))}
        lit |= {e["name"] for e in plan if e["name"].startswith(("Altitude", "Flaps", "Bogey", "Radar", "Multiball", "DeckLit"))}
        lit |= {e["name"] for e in plan if e["name"].startswith("Ladder") and e["name"][-1] in "12"}
        lit -= {"ShootAgain", "Kickback"}
    else:
        lit = {"ArrowHangar", "ArrowSkyway", "Lock1", "Course1", "Kickback", "ShootAgain", "RollCall2", "WingW",
               "FlapsF", "FlapsL", "AltitudeFL100", "LadderHangar1", "GForce1", "TaxiT"}
    for e in plan:
        if e["name"] in lit:
            if e["source"] == "decor":
                e = dict(e, colour=gi_col)
            lit_lamp(c, e)
    # toys
    for d in L["bayDoors"]:
        draw_frame(c, "bay_door", "closed", (d["a"][0] + d["b"][0]) / 2, d["a"][1],
                   tint=P.TINT["door_marked"] if worst else P.TINT["door"])
    draw_frame(c, "pylon", "up" if (top or worst) else "down", *L["pylon"]["c"])
    sprites = os.path.join(ROOT, "assets", "sprites")
    for i, bmp in enumerate(L["bumpers"]):
        (x, y), rr = bmp["c"], bmp["r"]
        sprite_file(c, os.path.join(sprites, "bumper.png"), x, y, 2 * rr + 6, tint=P.ORANGE)
        on = worst or (top and i == 1)
        draw_frame(c, "jet_caps", "jet%d_%s" % (i + 1, "on" if on else "off"), x, y)
        if on:
            glow(c, x, y, rr * 1.8, neon(P.AQUA), 0.35)
    dr = L["drone"]
    draw_frame(c, "drone", "armour1" if top else "armour6", (dr["leftX"] + dr["rightX"]) / 2 + (14 if top else -10), dr["y"])
    domes = [(279.0, 296.0, "hangar"), (473.0, 357.0, "mission"), (391.0, 360.0, "afterburner"),
             (145.0, 392.0, "runway"), (566.0, 392.0, "skyway"), (324.0, 168.0, "nest"),
             (150.0, 692.0, "sling_left"), (561.0, 692.0, "sling_right"), (476.0, 165.0, "deck")]
    dome_col = dict(hangar=P.ORANGE, mission=P.YELLOW, afterburner=P.GREEN, runway=P.ORCHID, skyway=P.PINK,
                    nest=P.CREAM, sling_left=P.AQUA, sling_right=P.AQUA, deck=P.AQUA)
    for x, y, n in domes:
        if n == "deck":
            draw_frame(c, "flasher_domes", "deck_off", x, y)
        if worst or (top and n in ("hangar", "skyway", "nest")):
            draw_frame(c, "flasher_domes", n + "_on", x, y)
            glow(c, x, y, 34, neon(dome_col[n]), 0.4)
    for f, col in zip(L["flippers"], (P.PINK, P.PINK, P.AQUA)):
        flipper(c, f, col)
    cb = L["captiveBall"]
    sprite_file(c, os.path.join(sprites, "ball.png"), cb["home"][0], cb["home"][1], 2 * cb["r"] * 4 / 3)
    # balls: one on the floor, one passing under the Runway's plastic; three in play at FL500
    if balls:
        for x, y in [(300.0, 640.0), (192.0, 392.0)] + ([(470.0, 560.0)] if top else []):
            c.drawCircle(x + 4, y + 6, 15, blur(fill(C(P.INK, 120)), 3))
            sprite_file(c, os.path.join(sprites, "ball.png"), x, y, 32.0)
    if ramps:
        draw_ramps(c, top)
    return st.image()


def draw_ramps(c, top):
    draw_frame(c, "ramp_runway", "lit" if top else "normal", *META["ramp_runway"]["anchor"], centre=False)
    draw_frame(c, "ramp_skyway", "to_deck_lit" if top else "lit", *META["ramp_skyway"]["anchor"], centre=False)


def ramp_layer(level, scale):
    """The ramps alone, straight-alpha RGBA, for measuring a ball under their plastic."""
    st = Stage(L["width"], L["height"], scale, 1, transparent=True)
    draw_ramps(st.c, level == "fl500")
    return st.image()


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
