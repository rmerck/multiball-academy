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
    top = level == "fl500"
    k = 4 if top else 0
    gi_col = P.ALTITUDE_GI[k]
    st = Stage(L["width"], L["height"], PX_W / L["width"], 1)
    c = st.c
    out = os.path.join(HERE, "out")
    bg = skia.Image.open(os.path.join(out, "background_a.png"))
    c.drawImageRect(bg, skia.Rect.MakeWH(L["width"], L["height"]), skia.SamplingOptions(skia.FilterMode.kLinear,
                                                                                         skia.MipmapMode.kLinear))
    # general illumination: the grey light map in the Altitude colour, additive
    gi = np.asarray(Image.open(os.path.join(out, "gi_a.png")).convert("L"), np.float32) / 255.0
    strength = 0.42 if top else 0.34
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
    # altimeter pointer at the current level, the turntable, the deck
    draw_frame(c, "altitude_plate", "fl%d00" % (k + 1), *F.ALT_PLATE_AT, centre=False)
    pw = L["propWash"]["c"]
    draw_frame(c, "propwash_blur" if top else "propwash", "blur030" if top else "rot015", *pw)
    draw_frame(c, "flight_deck", "lit_fl500" if top else "off", *META["flight_deck"]["anchor"], centre=False)
    # lit lamps for a sample state
    plan = F.lamp_plan()
    if top:
        lit = {e["name"] for e in plan if e["source"] == "layout" and not e["name"].startswith(("Rank6", "Rank7", "Bonus5", "Combo4", "Combo5", "DroneArmour"))}
        lit |= {e["name"] for e in plan if e["name"].startswith(("Altitude", "Flaps", "Bogey", "Radar", "Multiball", "DeckLit"))}
        lit |= {e["name"] for e in plan if e["name"].startswith("Ladder") and e["name"][-1] in "12"}
        lit -= {"ShootAgain", "Kickback"}
    else:
        lit = {"ArrowHangar", "ArrowSkyway", "Lock1", "Course1", "Kickback", "ShootAgain", "RollCall2", "WingW",
               "FlapsF", "FlapsL", "AltitudeFL100", "LadderHangar1", "GForce1", "TaxiT"}
    for e in plan:
        if e["name"] in lit:
            lit_lamp(c, e)
    # toys
    for d in L["bayDoors"]:
        draw_frame(c, "bay_door", "closed", (d["a"][0] + d["b"][0]) / 2, d["a"][1])
    draw_frame(c, "pylon", "up" if top else "down", *L["pylon"]["c"])
    sprites = os.path.join(ROOT, "assets", "sprites")
    for i, bmp in enumerate(L["bumpers"]):
        (x, y), rr = bmp["c"], bmp["r"]
        sprite_file(c, os.path.join(sprites, "bumper.png"), x, y, 2 * rr + 6, tint=P.ORANGE)
        on = top and i == 1
        draw_frame(c, "jet_caps", "jet%d_%s" % (i + 1, "on" if on else "off"), x, y)
        if on:
            glow(c, x, y, rr * 1.8, neon(P.AQUA), 0.35)
    dr = L["drone"]
    draw_frame(c, "drone", "armour1" if top else "armour6", (dr["leftX"] + dr["rightX"]) / 2 + (14 if top else -10), dr["y"])
    domes = [(279.0, 296.0, "hangar"), (473.0, 357.0, "mission"), (391.0, 360.0, "afterburner"),
             (145.0, 392.0, "runway"), (566.0, 392.0, "skyway"), (324.0, 168.0, "nest")]
    for x, y, n in domes:
        if top and n in ("hangar", "skyway", "nest"):
            draw_frame(c, "flasher_domes", n + "_on", x, y)
            glow(c, x, y, 34, neon(dict(hangar=P.ORANGE, skyway=P.PINK, nest=P.CREAM)[n]), 0.4)
    for f, col in zip(L["flippers"], (P.PINK, P.PINK, P.AQUA)):
        flipper(c, f, col)
    cb = L["captiveBall"]
    sprite_file(c, os.path.join(sprites, "ball.png"), cb["home"][0], cb["home"][1], 2 * cb["r"] * 4 / 3)
    # balls: one on the floor, one passing under the Runway's plastic; three in play at FL500
    balls = [(300.0, 640.0), (192.0, 392.0)] + ([(470.0, 560.0)] if top else [])
    for x, y in balls:
        c.drawCircle(x + 4, y + 6, 15, blur(fill(C(P.INK, 120)), 3))
        sprite_file(c, os.path.join(sprites, "ball.png"), x, y, 32.0)
    draw_frame(c, "ramp_runway", "lit" if top else "normal", *META["ramp_runway"]["anchor"], centre=False)
    draw_frame(c, "ramp_skyway", "to_deck_lit" if top else "lit", *META["ramp_skyway"]["anchor"], centre=False)
    im = st.image()
    im.save(dst, optimize=True)
    print("composite", level, im.size, dst)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
