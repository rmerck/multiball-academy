# Contact sheet of the Table A masters for review (not shipped): every sheet with its frame labels, and an
# in-place preview that puts one frame of each master at its layout position on a plain playfield.
#   python3 tools/art/table_a/contact.py <out.png>       (needs numpy and Pillow, e.g. the art venv)
import json
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "out")
FONT = os.path.join(HERE, "..", "sources", "fonts", "RussoOne-Regular.ttf")
PPU = 4
S = 1.25            # preview pixels per playfield unit
DECK_A, DECK_B, INK = (20, 26, 68), (26, 33, 82), (10, 9, 32)
SHOT = {"AQUA": (99, 230, 216), "ORCHID": (185, 140, 255), "ORANGE": (255, 107, 26), "YELLOW": (255, 210, 60),
        "GREEN": (46, 209, 107), "PINK": (255, 46, 136), "CREAM": (255, 244, 214)}
GI = [(111, 180, 255), (242, 244, 255), (255, 176, 80), (255, 138, 92), (154, 107, 255)]


def font(n):
    return ImageFont.truetype(FONT, n)


def load_meta():
    meta = {}
    for name in ("sheets.json", "gimp_sheets.json"):
        meta.update(json.load(open(os.path.join(OUT, name)))["sheets"])
    return meta


def frame(meta, name, label):
    sh = meta[name]
    im = Image.open(os.path.join(OUT, sh["file"])).convert("RGBA")
    i = sh["frames"].index(label)
    r, c = divmod(i, sh["columns"])
    cw, ch = sh["cellPixels"]
    return im.crop((c * cw, r * ch, (c + 1) * cw, (r + 1) * ch))


def tint(im, rgb, level=1.0):
    r, g, b, a = im.split()
    lum = im.convert("L")
    out = Image.merge("RGBA", [lum.point(lambda v, k=k: int(v * k * level / 255)) for k in rgb] + [a])
    return out


def put(canvas, im, x, y, centre=True, rot=0.0):
    im = im.resize((max(1, round(im.width * S / PPU)), max(1, round(im.height * S / PPU))), Image.LANCZOS)
    if rot:
        im = im.rotate(rot, resample=Image.BICUBIC, expand=True)
    px, py = x * S, y * S
    if centre:
        px, py = px - im.width / 2, py - im.height / 2
    canvas.alpha_composite(im, (round(px), round(py)))


def preview(meta, lay):
    W, H = round(768 * S), round(960 * S)
    c = Image.new("RGBA", (W, H), (18, 18, 58, 255))
    d = ImageDraw.Draw(c)
    shell = [(x * S, y * S) for x, y in lay["shell"]]
    d.polygon(shell, fill=DECK_A)
    for w in lay["walls"]:
        d.line([(w["a"][0] * S, w["a"][1] * S), (w["b"][0] * S, w["b"][1] * S)], fill=(90, 104, 140),
               width=max(1, round(2 * w["r"] * S * 0.6)))
    for tri in lay["slingshots"]:
        d.polygon([(x * S, y * S) for x, y in tri], fill=(40, 46, 90), outline=(120, 130, 170))
    for f in lay["flippers"]:
        (px, py), a, L = f["pivot"], f["rest"], f["length"]
        d.line([(px * S, py * S), ((px + L * math.cos(a)) * S, (py + L * math.sin(a)) * S)], fill=(255, 46, 136),
               width=round(2 * f["baseRadius"] * S * 0.8))
    # floor pieces first, then inserts, then raised toys, ramps and plastics last
    put(c, frame(meta, "flight_deck", "lit"), 471, 146, centre=False)
    put(c, frame(meta, "propwash", "rot015"), *lay["propWash"]["c"])
    plate = frame(meta, "altitude_plate", "plate")
    put(c, plate, 222, 640)
    for k, lab in enumerate(["FL100", "FL200", "FL300", "FL400", "FL500"]):
        put(c, tint(frame(meta, "inserts_altitude", lab), GI[k], 0.95 if k == 2 else 0.55), 222, 682 - k * 20)
    flaps = [r for r in lay["rollovers"] if r["group"] == "flaps"]
    for r, ch in zip(flaps, "FLAPS"):
        put(c, tint(frame(meta, "inserts_flaps", ch), SHOT["CREAM"], 0.9 if ch in "FL" else 0.5), r["c"][0],
            r["c"][1] + 22)
    arrows = [(0, (118, 440), "AQUA"), (1, (192, 425), "ORCHID"), (2, (279, 392), "ORANGE"),
              (3, (441, 385), "YELLOW"), (4, (356, 404), "GREEN"), (5, (519, 425), "PINK"), (6, (593, 440), "AQUA")]
    lamps = {l["id"]: l["c"] for l in lay["lamps"]}
    for i, (ax, ay), col in arrows:
        lx, ly = lamps[i]
        ang = math.degrees(math.atan2(-(ay - ly), ax - lx)) - 90
        art = frame(meta, "inserts_arrows", "arrow_mission" if i == 3 else "arrow")
        put(c, tint(art, SHOT[col], 1.0 if i in (2, 5) else 0.55), lx, ly, rot=ang)
    for b in lay["bayDoors"]:
        put(c, frame(meta, "bay_door", "closed"), (b["a"][0] + b["b"][0]) / 2, b["a"][1])
    put(c, frame(meta, "pylon", "up"), *lay["pylon"]["c"])
    put(c, frame(meta, "gforce", "housing"), 128, 224, centre=False)
    dr = lay["drone"]
    put(c, frame(meta, "drone", "armour4"), (dr["leftX"] + dr["rightX"]) / 2, dr["y"])
    for k, b in enumerate(lay["bumpers"]):
        put(c, frame(meta, "jet_caps", "jet%d_%s" % (k + 1, "on" if k == 1 else "off")), *b["c"])
    domes = [("hangar", (279, 296)), ("mission", (473, 357)), ("afterburner", (391, 360)), ("runway", (145, 392)),
             ("skyway", (566, 392)), ("nest", (324, 168)), ("sling_left", (150, 692)), ("sling_right", (561, 692))]
    for k, (n, (x, y)) in enumerate(domes):
        put(c, frame(meta, "flasher_domes", n + ("_on" if k in (0, 4) else "_off")), x, y)
    put(c, frame(meta, "ramp_runway", "normal"), 40, 276, centre=False)
    put(c, frame(meta, "ramp_skyway", "to_deck"), 495, 236, centre=False)
    put(c, frame(meta, "plastic_sling_left", "plastic"), *meta["plastic_sling_left"]["anchor"], centre=False)
    put(c, frame(meta, "plastic_sling_right", "plastic"), *meta["plastic_sling_right"]["anchor"], centre=False)
    d = ImageDraw.Draw(c)
    d.text((16, H - 40), "in place on layout_a.json (table/a-geometry), %.2f px/unit; ball 24 units = %d px" % (S, 24 * S),
           font=font(16), fill=(200, 210, 240))
    return c


def sheets(meta, width):
    blocks = []
    for name in ["drone", "propwash", "pylon", "bay_door", "flight_deck", "ramp_runway", "ramp_skyway", "gforce",
                 "jet_caps", "flasher_domes", "inserts_flaps", "inserts_altitude", "altitude_plate", "inserts_arrows",
                 "plastic_sling_left", "plastic_sling_right", "decals_nameplates"]:
        sh = meta[name]
        im = Image.open(os.path.join(OUT, sh["file"])).convert("RGBA")
        k = min(1.0, (width - 20) / im.width, 520 / im.height)
        im = im.resize((max(1, round(im.width * k)), max(1, round(im.height * k))), Image.LANCZOS)
        bg = Image.new("RGBA", im.size, DECK_A + (255,))
        dd = ImageDraw.Draw(bg)
        step = max(8, round(sh["cellPixels"][0] * k / 4))
        for y in range(0, im.height, step):
            for x in range(0, im.width, step):
                if (x // step + y // step) % 2:
                    dd.rectangle((x, y, x + step - 1, y + step - 1), fill=DECK_B + (255,))
        bg.alpha_composite(im)
        head = "%s  (%s, %d frames, cell %dx%d px = %gx%g units)" % (
            sh["file"], ", ".join(sh["frames"][:6]) + (", ..." if len(sh["frames"]) > 6 else ""), len(sh["frames"]),
            sh["cellPixels"][0], sh["cellPixels"][1], sh["cellUnits"][0], sh["cellUnits"][1])
        blocks.append((head, bg))
    return blocks


def main(dst):
    meta = load_meta()
    lay = json.load(open(os.path.join(HERE, "layout_a.json")))
    pv = preview(meta, lay)
    col_w = 1240
    blocks = sheets(meta, col_w)
    # two columns of sheets beside the preview
    cols, heights = [[], []], [0, 0]
    for b in blocks:
        i = 0 if heights[0] <= heights[1] else 1
        cols[i].append(b)
        heights[i] += b[1].height + 40
    H = max(pv.height + 90, max(heights) + 90)
    W = pv.width + 2 * col_w + 60
    sheet = Image.new("RGBA", (W, H), (34, 34, 44, 255))
    d = ImageDraw.Draw(sheet)
    d.text((20, 18), "Multiball Academy - Table A art masters (phase 1)", font=font(30), fill=(255, 244, 214))
    sheet.alpha_composite(pv, (20, 70))
    for ci, col in enumerate(cols):
        x, y = pv.width + 40 + ci * col_w, 70
        for head, im in col:
            d.text((x, y), head, font=font(15), fill=(200, 210, 240))
            sheet.alpha_composite(im, (x, y + 22))
            y += im.height + 40
    sheet.convert("RGB").save(dst, optimize=True)
    print("contact sheet", sheet.size, dst)


if __name__ == "__main__":
    main(sys.argv[1])
