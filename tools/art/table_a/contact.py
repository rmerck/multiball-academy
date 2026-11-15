# Contact sheet of the Table A masters for review (not shipped): every sheet with its frame labels, the
# playfield images, and a full-table composite (composite.py) on the left.
#   python3 tools/art/table_a/contact.py <out.png> <composite.png>   (needs numpy and Pillow, e.g. the art venv)
import json
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


def sheets(meta, width):
    blocks = []
    for name in ["drone", "propwash", "propwash_blur", "propwash_ring", "pylon", "bay_door", "flight_deck", "ramp_runway", "ramp_skyway", "gforce",
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


def main(dst, composite):
    meta = load_meta()
    pv = Image.open(composite).convert("RGBA")
    pv = pv.resize((960, round(pv.height * 960 / pv.width)), Image.LANCZOS)
    col_w = 1240
    blocks = sheets(meta, col_w)
    for file, note in (("background_a.png", "table background (floor + raised parts), 1536x1920, 2 px/unit"),
                       ("floor_a.png", "flush floor only (luminance-checked), 1536x1920"),
                       ("gi_a.png", "GI light map, greyscale, 0.5 px/unit, tinted by the Altitude colour")):
        im = Image.open(os.path.join(OUT, file)).convert("RGBA")
        k = min((col_w - 20) / im.width, 620 / im.height)
        blocks.append((file + "  (" + note + ")", im.resize((round(im.width * k), round(im.height * k)), Image.LANCZOS)))
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
    d.text((20, 18), "Multiball Academy - Table A art masters (phase 2)", font=font(30), fill=(255, 244, 214))
    sheet.alpha_composite(pv, (20, 70))
    d.text((20, 74 + pv.height), "composite at iPhone 18 Pro width (1206 px), FL500 sample state", font=font(16),
           fill=(200, 210, 240))
    for ci, col in enumerate(cols):
        x, y = pv.width + 40 + ci * col_w, 70
        for head, im in col:
            d.text((x, y), head, font=font(15), fill=(200, 210, 240))
            sheet.alpha_composite(im, (x, y + 22))
            y += im.height + 40
    sheet.convert("RGB").save(dst, optimize=True)
    print("contact sheet", sheet.size, dst)


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
